/* A requested display must either start or terminate the host with failure.
 * Each subprocess injects one initialization failure; no GPU is needed. */
#include <SDL.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static int fail_stage;
ptrdiff_t xbox_GetMemoryOffset(void) { return 0; }
void xbox_InputKeyboardEvent(int scan,int pressed) { (void)scan; (void)pressed; }
static int fail(const char *where) { SDL_SetError("fixture failure: %s",where); return -1; }
static int fixture_init(Uint32 flags) {
    return fail_stage==1 ? fail("video init") : SDL_InitSubSystem(flags);
}
static SDL_Window *fixture_window(const char *title,int x,int y,int w,int h,Uint32 flags) {
    if(fail_stage==2){fail("window");return NULL;}
    return SDL_CreateWindow(title,x,y,w,h,flags);
}
static SDL_Renderer *fixture_renderer(SDL_Window *w,int index,Uint32 flags) {
    if(fail_stage==3){fail("renderer");return NULL;}
    return SDL_CreateRenderer(w,index,flags);
}
static int fixture_logical(SDL_Renderer *r,int w,int h) {
    return fail_stage==4 ? fail("logical size") : SDL_RenderSetLogicalSize(r,w,h);
}
static SDL_Texture *fixture_texture(SDL_Renderer *r,Uint32 format,int access,int w,int h) {
    if(fail_stage==5){fail("texture");return NULL;}
    return SDL_CreateTexture(r,format,access,w,h);
}
static SDL_Surface *fixture_surface(Uint32 flags,int w,int h,int depth,Uint32 format) {
    if(fail_stage==6){fail("surface");return NULL;}
    return SDL_CreateRGBSurfaceWithFormat(flags,w,h,depth,format);
}
static int fixture_thread(pthread_t *thread,const pthread_attr_t *attr,void *(*fn)(void *),void *arg) {
    return fail_stage==7 ? EAGAIN : pthread_create(thread,attr,fn,arg);
}
#define SDL_InitSubSystem fixture_init
#define SDL_CreateWindow fixture_window
#define SDL_CreateRenderer fixture_renderer
#define SDL_RenderSetLogicalSize fixture_logical
#define SDL_CreateTexture fixture_texture
#define SDL_CreateRGBSurfaceWithFormat fixture_surface
#define pthread_create fixture_thread
#ifndef FB_STARTUP_SOURCE
#define FB_STARTUP_SOURCE "../third_party/xboxrecomp/src/video/fb_present.c"
#endif
#include FB_STARTUP_SOURCE
int main(void) {
    int failures=0;
    setenv("SDL_VIDEODRIVER","dummy",1);
    for(int stage=0;stage<=7;stage++) {
        int output[2];if(pipe(output))return 2;
        fflush(NULL); /* Do not let the child's fatal flush repeat parent output. */
        pid_t child=fork();if(child<0)return 2;
        if(!child) {
            close(output[0]);dup2(output[1],STDERR_FILENO);close(output[1]);
            fail_stage=stage;
            if(stage)setenv("RECOMP_FB_WINDOW","1",1);else unsetenv("RECOMP_FB_WINDOW");
            xbox_FramebufferWindowStart();
            if(!stage)_Exit(0);
            SDL_Delay(500);
            _Exit(97); /* Guest survived a requested display's startup failure. */
        }
        close(output[1]);char message[4096];size_t used=0;ssize_t n;
        while(used<sizeof(message)-1 && (n=read(output[0],message+used,sizeof(message)-1-used))>0)used+=(size_t)n;
        message[used]=0;close(output[0]);int status;
        if(waitpid(child,&status,0)!=child)return 2;
        int code=WIFEXITED(status)?WEXITSTATUS(status):-1;
        int expected=stage?EXIT_FAILURE:EXIT_SUCCESS;
        int diagnostic=!stage || strstr(message,stage==7?"[FBWIN] display thread failed:":"[FBWIN] SDL display failed:");
        printf("stage %d: exit=%d expected=%d diagnostic=%s\n",stage,code,expected,diagnostic?"yes":"no");
        if(code!=expected || !diagnostic){fprintf(stderr,"%s",message);failures++;}
    }
    return failures?1:0;
}
