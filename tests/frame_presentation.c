/* Integration fixture: exercise the actual SDL presentation
 * loop with a controlled producer. Run with SDL_VIDEODRIVER=dummy. */
#include <SDL.h>
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static atomic_uint presents,first_pixel,last_pixel,keys,focus_clears,polls,image_width,image_height;
static unsigned char *ram;
static uint32_t high_pixels[2560*1920];
static unsigned high_scale=2;
static const uint32_t *high_frame(const void *address,unsigned pitch,unsigned *w,unsigned *h){
    assert(address==ram+0x1000 && pitch==2560);*w=640*high_scale;*h=480*high_scale;return high_pixels;
}
ptrdiff_t xbox_GetMemoryOffset(void){return (ptrdiff_t)ram;}
void xbox_InputKeyboardEvent(int scan,int pressed){
    (void)pressed;
    if(scan<0)atomic_fetch_add(&focus_clears,1);
    else atomic_fetch_add(&keys,1);
}
static int fixture_update(SDL_Texture *t,const SDL_Rect *r,const void *pixels,int pitch){
    uint32_t first,last;int w,h;SDL_QueryTexture(t,NULL,NULL,&w,&h);
    atomic_store(&image_width,w);atomic_store(&image_height,h);
    memcpy(&first,pixels,4);memcpy(&last,(const char*)pixels+(h-1)*pitch+(w-1)*4,4);
    atomic_store(&first_pixel,first);atomic_store(&last_pixel,last);
    return SDL_UpdateTexture(t,r,pixels,pitch);
}
static void fixture_present(SDL_Renderer *r){
    SDL_RenderPresent(r);atomic_fetch_add(&presents,1);
}
static int fixture_poll(SDL_Event *e){atomic_fetch_add(&polls,1);return SDL_PollEvent(e);}
#define SDL_PollEvent fixture_poll
#define SDL_UpdateTexture fixture_update
#define SDL_RenderPresent fixture_present
#include "../third_party/xboxrecomp/src/video/fb_present.c"
#undef SDL_PollEvent
#undef SDL_UpdateTexture
#undef SDL_RenderPresent
static void await_pixel(uint32_t value){
    uint64_t end=SDL_GetTicks64()+1000;
    while(atomic_load(&first_pixel)!=value || atomic_load(&last_pixel)!=value){
        assert(SDL_GetTicks64()<end);SDL_Delay(1);
    }
}
static void fill(uint32_t value){
    for(unsigned i=0;i<640*480;i++)memcpy(ram+0x1000+i*4,&value,4);
    xbox_FramebufferWindowSet(0x1000,2560);await_pixel(value);
}
int main(void){
    setenv("RECOMP_FB_WINDOW","1",1);unsetenv("RECOMP_FB_SOURCE_PACING");unsetenv("RECOMP_FB_LEGACY_PACING");
    ram=calloc(1,0x1000+640*480*4);assert(ram);
    xbox_FramebufferWindowStart();
    xbox_FramebufferMovieFrame(NULL); /* a notification without a usable image */
    SDL_Delay(100);unsigned idle_polls=atomic_load(&polls);
    SDL_Delay(100);assert(atomic_load(&polls)-idle_polls<100);
    fill(0xff010203);
    for(unsigned n=1;n<=20;n++){fill(0xff000000|n);SDL_Delay(30);}
    SDL_Delay(30);unsigned before=atomic_load(&presents);
    SDL_Delay(100);assert(atomic_load(&presents)==before);
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.keysym.scancode=SDL_SCANCODE_A;
    assert(SDL_PushEvent(&e)==1);
    e.type=SDL_WINDOWEVENT;e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    assert(SDL_PushEvent(&e)==1);
    uint64_t limit=SDL_GetTicks64()+500;
    while(!atomic_load(&keys)||!atomic_load(&focus_clears)){assert(SDL_GetTicks64()<limit);SDL_Delay(1);}
    e.type=SDL_WINDOWEVENT;e.window.event=SDL_WINDOWEVENT_EXPOSED;
    assert(SDL_PushEvent(&e)==1);limit=SDL_GetTicks64()+500;
    while(atomic_load(&presents)==before){assert(SDL_GetTicks64()<limit);SDL_Delay(1);}
    uint32_t *movie=malloc(640*480*4);assert(movie);
    for(unsigned i=0;i<640*480;i++)movie[i]=0xffabc123;
    xbox_FramebufferMovieFrame(movie);await_pixel(0xffabc123);
    xbox_FramebufferMovieFrame(NULL);await_pixel(0xff000014);
    /* Updates during presentation/wait transitions must not be lost. */
    for(unsigned n=21;n<=120;n++)fill(0xff000000|n);
    /* HD image, native movie overlay, HD restoration and NULL-source fallback. */
    for(unsigned i=0;i<1280*960;i++)high_pixels[i]=0xff6789ab;
    xbox_FramebufferHighResolutionSource(high_frame);xbox_FramebufferWindowSet(0x1000,2560);await_pixel(0xff6789ab);
    assert(atomic_load(&image_width)==1280 && atomic_load(&image_height)==960);
    xbox_FramebufferMovieFrame(movie);await_pixel(0xffabc123);assert(atomic_load(&image_width)==640);
    xbox_FramebufferMovieFrame(NULL);await_pixel(0xff6789ab);assert(atomic_load(&image_width)==1280);
    for(unsigned scale=3;scale<=4;scale++) {
        high_scale=scale;unsigned value=0xff678900|scale;
        for(unsigned i=0;i<640*480*scale*scale;i++)high_pixels[i]=value;
        xbox_FramebufferWindowSet(0x1000,2560);await_pixel(value);
        assert(atomic_load(&image_width)==640*scale && atomic_load(&image_height)==480*scale);
        xbox_FramebufferMovieFrame(movie);await_pixel(0xffabc123);assert(atomic_load(&image_width)==640);
        xbox_FramebufferMovieFrame(NULL);await_pixel(value);assert(atomic_load(&image_width)==640*scale);
    }
    high_scale=5;xbox_FramebufferWindowSet(0x1000,2560);await_pixel(0xff000078);assert(atomic_load(&image_width)==640);
    xbox_FramebufferHighResolutionSource(NULL);fill(0xff123456);assert(atomic_load(&image_width)==640);
    printf("fixture passed: %u presentations; idle input/redraw/movie/latest frame verified\n",atomic_load(&presents));fflush(stdout);
    e.type=SDL_QUIT;assert(SDL_PushEvent(&e)==1);
    SDL_Delay(1000);fprintf(stderr,"window close did not terminate\n");return 1;
}
