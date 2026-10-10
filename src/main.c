/* SSX Tricky US retail bring-up host. Optional intro skipping is controlled by SSX_SKIP_INTRO. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "ohci.h"
#include "movie_player.h"
#include "xinput_xbox.h"
#include "nv2a_gl.h"
#ifndef _WIN32
extern void xbox_FramebufferHighResolutionSource(const uint32_t *(*)(const void *,unsigned,unsigned *,unsigned *));
#endif

extern RECOMP_TLS uint32_t g_esp;
extern int recomp_dispatch_init(void);
extern void xbe_entry_point(void);
extern int ssx_apu_init(void);
extern void ssx_apu_shutdown(void);
extern void xbox_path_init(const char *, const char *);

int main(int argc, char **argv)
{
    const char *game_dir = argc > 1 ? argv[1] : "game";
    const char *save_dir = argc > 2 ? argv[2] : "saves";
    char path[4096];
    void *data;
    FILE *file;
    long size;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (snprintf(path, sizeof(path), "%s/default.xbe", game_dir) >= (int)sizeof(path))
        return 1;
    file = fopen(path, "rb");
    if (!file) { perror(path); return 1; }
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file); return 1;
    }
    data = malloc((size_t)size);
    if (!data) { fclose(file); return 1; }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file); free(data); return 1;
    }
    fclose(file);
    fprintf(stderr, "[SSX] Loading US retail XBE (%ld bytes)\n", size);
    if (!xbox_MemoryLayoutInit(data, (size_t)size)) {
        fprintf(stderr, "[SSX] Memory mapping failed\n");
        free(data); return 1;
    }
    xbox_kernel_init();
    xbox_path_init(game_dir, save_dir);
    ssx_movies_init(game_dir);
#ifndef _WIN32
    xbox_FramebufferHighResolutionSource(ssx_gl_present);
#endif
    xbox_kernel_bridge_init();
    g_esp = XBOX_STACK_TOP;
    if (!recomp_dispatch_init()) {
        fprintf(stderr, "[SSX] Dispatch initialization failed\n");
        return 1;
    }
    if (getenv("RECOMP_USB")) {
        xbox_InputInit();
        xbox_OhciInit();
    }
    if (getenv("RECOMP_AC97_READY") && !ssx_apu_init()) return 1;
    xbox_WatchdogStart();
    fprintf(stderr, "[SSX] Entering guest at 0x00154218; ESP=0x%08X\n", g_esp);
    xbe_entry_point();
    fprintf(stderr, "[SSX] Guest returned\n");
    ssx_apu_shutdown();
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(data);
    return 0;
}
