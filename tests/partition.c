#include "kernel.h"
#include "xbox_memory_layout.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)
extern recomp_func_t recomp_lookup_kernel(uint32_t);
extern RECOMP_TLS uint32_t g_eax, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void xbox_kernel_set_thunk_address(uint32_t, uint32_t);
static uint32_t *guest;
static uint32_t bridge_call(unsigned slot, const uint32_t *args, size_t count)
{
    g_esp=0x20000;guest[g_esp/4]=0x12345678;
    memcpy((char*)guest+g_esp+4,args,count*4);
    recomp_lookup_kernel(guest[0x10000/4+slot])();
    return g_eax;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    xbox_path_init("game", argv[1]);
    char path[1024];
    CHECK(xbox_translate_path("\\Device\\Harddisk0\\Partition0", path, sizeof(path)));
    struct stat st;
    CHECK(stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 4096);
    XBOX_ANSI_STRING name = {0};
    char device[] = "\\Device\\Harddisk0\\Partition0";
    name.Buffer = device;
    name.Length = strlen(device);
    name.MaximumLength = name.Length + 1;
    XBOX_OBJECT_ATTRIBUTES attrs = {0};
    attrs.ObjectName = &name;
    XBOX_IO_STATUS_BLOCK ios = {0};
    HANDLE handle = NULL;
    CHECK(xbox_NtOpenFile(&handle, XBOX_GENERIC_READ | XBOX_GENERIC_WRITE,
                         &attrs, &ios, 3, 0) == 0);
    unsigned char sector[512], actual[512];
    memset(sector, 0xa5, sizeof(sector));
    LARGE_INTEGER offset;
    offset.QuadPart = 0x800;
    CHECK(xbox_NtWriteFile(handle, NULL, NULL, NULL, &ios, sector, sizeof(sector), &offset) == 0);
    CHECK(ios.Information == sizeof(sector));
    CHECK(xbox_NtClose(handle) == 0);
    xbox_path_init("game", argv[1]);
    CHECK(xbox_NtOpenFile(&handle, XBOX_GENERIC_READ, &attrs, &ios, 3, 0) == 0);
    CHECK(xbox_NtReadFile(handle, NULL, NULL, NULL, &ios, actual, sizeof(actual), &offset) == 0);
    CHECK(ios.Information == sizeof(actual) && memcmp(sector, actual, sizeof(sector)) == 0);
    CHECK(xbox_NtClose(handle) == 0);
    CHECK(xbox_NtOpenFile(&handle, XBOX_GENERIC_READ, &attrs, &ios, 3, XBOX_FILE_DIRECTORY_FILE) == 0);
    CHECK(xbox_NtClose(handle) == 0);
    CHECK(xbox_translate_path("\\Device\\Harddisk0\\Partition1\\TDATA\\test", path, sizeof(path)));
    CHECK(strstr(path, "Partition1.img") == NULL);
    CHECK(strstr(path, "/TitleData/test") != NULL);
    char link_text[]="\\??\\U:", target_text[]="\\Device\\Harddisk0\\Partition1\\UDATA\\45410004";
    XBOX_ANSI_STRING link={strlen(link_text),sizeof(link_text),link_text};
    XBOX_ANSI_STRING target={strlen(target_text),sizeof(target_text),target_text};
    CHECK(xbox_IoCreateSymbolicLink(&link,&target)==0);
    CHECK(xbox_translate_path("U:\\slot\\save.dat",path,sizeof(path)));
    CHECK(strstr(path,"/UserData/45410004/slot/save.dat")!=NULL);
    CHECK(strstr(path,argv[1])==path);
    char mixed[1024];
    snprintf(mixed,sizeof(mixed),"%s/UserData/MixedCase",argv[1]);
    CHECK(mkdir(mixed,0755)==0 || errno==EEXIST);
    strcat(mixed,"/SaveImage.xbx");
    FILE *file=fopen(mixed,"wb");CHECK(file!=NULL);fputs("image",file);fclose(file);
    CHECK(xbox_translate_path("\\Device\\Harddisk0\\Partition1\\udata\\mixedcase\\saveimage.xbx",path,sizeof(path)));
    CHECK(strcmp(path,mixed)==0);
    CHECK(xbox_translate_path("\\Device\\Harddisk0\\Partition1\\UDATA\\MIXEDCASE\\NewSave.dat",path,sizeof(path)));
    CHECK(strstr(path,"/UserData/MixedCase/NewSave.dat")!=NULL);
    char root_name[]="\\Device\\Harddisk0\\Partition1\\UDATA\\MixedCase";
    XBOX_ANSI_STRING root_string={strlen(root_name),sizeof(root_name),root_name};
    attrs.ObjectName=&root_string;
    HANDLE directory=NULL;
    CHECK(xbox_NtOpenFile(&directory,XBOX_GENERIC_READ,&attrs,&ios,3,XBOX_FILE_DIRECTORY_FILE)==0);
    char relative_name[]="saveimage.xbx";
    XBOX_ANSI_STRING relative={strlen(relative_name),sizeof(relative_name),relative_name};
    attrs.RootDirectory=directory;attrs.ObjectName=&relative;
    CHECK(xbox_NtOpenFile(&handle,XBOX_GENERIC_READ,&attrs,&ios,3,0)==0);
    char contents[5];offset.QuadPart=0;
    CHECK(xbox_NtReadFile(handle,NULL,NULL,NULL,&ios,contents,5,&offset)==0);
    CHECK(!memcmp(contents,"image",5));
    CHECK(xbox_NtClose(handle)==0);
    /* Closing a partially consumed search must discard its cursor, including
       when the host allocator reuses the same handle address. */
    for (int i=0;i<128;i++) {
        XBOX_OBJECT_ATTRIBUTES search_attrs={0};
        search_attrs.ObjectName=&root_string;
        HANDLE search=NULL;
        CHECK(xbox_NtOpenFile(&search,XBOX_GENERIC_READ,&search_attrs,&ios,3,XBOX_FILE_DIRECTORY_FILE)==0);
        unsigned char entries[512];
        CHECK(xbox_NtQueryDirectoryFile(search,NULL,NULL,NULL,&ios,entries,sizeof(entries),
              XboxFileDirectoryInformation,&relative,FALSE)==0);
        PXBOX_FILE_DIRECTORY_INFORMATION entry=(void*)entries;
        CHECK(entry->FileNameLength==13 && !memcmp(entry->FileName,"SaveImage.xbx",13));
        CHECK(xbox_NtClose(search)==0);
    }
    /* Exercise the guest ABI too: its close wrapper must run kernel cleanup. */
    guest=VirtualAlloc(NULL,64u*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(guest!=NULL);g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)guest;
    guest[0x10000/4]=0x80000000u|202;
    guest[0x10004/4]=0x80000000u|207;
    guest[0x10008/4]=0x80000000u|187;
    xbox_kernel_set_thunk_address(0x10000,3);xbox_kernel_bridge_init();
    guest[0x30000/4]=0;guest[0x30004/4]=0x30100;guest[0x30008/4]=0;
    guest[0x30100/4]=(strlen(root_name)+1)*65536+strlen(root_name);
    guest[0x30104/4]=0x30200;strcpy((char*)guest+0x30200,root_name);
    for(int i=0;i<128;i++) {
        uint32_t open_args[]={0x30300,XBOX_GENERIC_READ,0x30000,0x30400,3,XBOX_FILE_DIRECTORY_FILE};
        CHECK(bridge_call(0,open_args,6)==0);
        uint32_t token=guest[0x30300/4];
        uint32_t query_args[]={token,0,0,0,0x30400,0x30500,512,XboxFileDirectoryInformation,0,0};
        CHECK(bridge_call(1,query_args,10)==0);
        PXBOX_FILE_DIRECTORY_INFORMATION entry=(void*)((char*)guest+0x30500);
        CHECK(entry->FileNameLength==13 && !memcmp(entry->FileName,"SaveImage.xbx",13));
        CHECK(bridge_call(2,&token,1)==0);
    }
    CHECK(xbox_NtDeleteFile(&attrs)==0);
    CHECK(xbox_NtClose(directory)==0);
    return 0;
}
