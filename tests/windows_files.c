/* Native Win32 file semantics, including handle reuse during save enumeration. */
#include "kernel.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
void xbox_log(int level,const char *subsystem,const char *format,...)
{(void)level;(void)subsystem;(void)format;}
BOOL xbox_translate_path(const char *path,WCHAR *out,DWORD capacity)
{return MultiByteToWideChar(CP_UTF8,0,path,-1,out,capacity)>0;}
int main(void)
{
 char temporary[MAX_PATH],root[MAX_PATH],path[MAX_PATH];
 assert(GetTempPathA(MAX_PATH,temporary));
 snprintf(root,sizeof root,"%sssx-win-files-%lu",temporary,GetCurrentProcessId());
 assert(CreateDirectoryA(root,NULL));
 snprintf(path,sizeof path,"%s\\SaveMeta.xbx",root);
 HANDLE file=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,0,NULL);
 assert(file!=INVALID_HANDLE_VALUE);
 DWORD written;assert(WriteFile(file,"ABCDEF",6,&written,NULL)&&written==6);
 XBOX_IO_STATUS_BLOCK ios;LARGE_INTEGER offset;char data[8]={0};offset.QuadPart=0;
 assert(xbox_NtReadFile(file,NULL,NULL,NULL,&ios,data,2,&offset)==STATUS_SUCCESS);
 assert(ios.Information==2&&!memcmp(data,"AB",2));
 assert(xbox_NtReadFile(file,NULL,NULL,NULL,&ios,data,2,NULL)==STATUS_SUCCESS);
 assert(ios.Information==2&&!memcmp(data,"CD",2));
 assert(xbox_NtReadFile(file,NULL,NULL,NULL,&ios,data,8,NULL)==STATUS_SUCCESS);
 assert(ios.Information==2&&!memcmp(data,"EF",2));
 assert(xbox_NtReadFile(file,NULL,NULL,NULL,&ios,data,1,NULL)==STATUS_END_OF_FILE&&ios.Information==0);
 XBOX_FILE_STANDARD_INFORMATION info;
 assert(xbox_NtQueryInformationFile(file,&ios,&info,sizeof info,XboxFileStandardInformation)==STATUS_SUCCESS);
 assert(info.EndOfFile.QuadPart==6&&!info.Directory);
 assert(xbox_NtClose(file)==STATUS_SUCCESS);
 /* Opening the same directory repeatedly normally recycles the native handle.
  * A fresh handle's first non-restarting scan must still begin at its first file. */
 for(unsigned iteration=0;iteration<128;iteration++) {
  HANDLE directory=CreateFileA(root,FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
  assert(directory!=INVALID_HANDLE_VALUE);
  unsigned char buffer[512];
  NTSTATUS status=xbox_NtQueryDirectoryFile(directory,NULL,NULL,NULL,&ios,buffer,sizeof buffer,XboxFileDirectoryInformation,NULL,FALSE);
  if(status!=STATUS_SUCCESS){fprintf(stderr,"Fresh enumeration failed at cycle%u: %08lx\n",iteration,(unsigned long)status);return 1;}
  XBOX_FILE_DIRECTORY_INFORMATION *entry=(void *)buffer;
  assert(entry->FileNameLength==12&&!memcmp(entry->FileName,"SaveMeta.xbx",12));
  assert(xbox_NtQueryDirectoryFile(directory,NULL,NULL,NULL,&ios,buffer,sizeof buffer,XboxFileDirectoryInformation,NULL,FALSE)==STATUS_NO_MORE_FILES);
  assert(xbox_NtQueryDirectoryFile(directory,NULL,NULL,NULL,&ios,buffer,sizeof buffer,XboxFileDirectoryInformation,NULL,FALSE)==STATUS_NO_MORE_FILES);
  assert(xbox_NtQueryDirectoryFile(directory,NULL,NULL,NULL,&ios,buffer,sizeof buffer,XboxFileDirectoryInformation,NULL,TRUE)==STATUS_SUCCESS);
  assert(xbox_NtClose(directory)==STATUS_SUCCESS);
 }
 assert(DeleteFileA(path));assert(RemoveDirectoryA(root));
 puts("Windows partial reads, offsets, EOF, file information and 128 closed/reused directory scans pass");return 0;
}
