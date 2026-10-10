/* Exercise native display start/close while another worker keeps presenting.
 * No guest image or guest memory is involved. Closing the display exits this
 * fixture via the same ordinary lifecycle used by the game. */
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void xbox_FramebufferWindowStart(void);
extern void xbox_FramebufferMovieFrame(const uint32_t *);
ptrdiff_t xbox_GetMemoryOffset(void) { return 0; }
void xbox_InputKeyboardEvent(int key, int down) { (void)key; (void)down; }

static DWORD WINAPI repeated_start(void *unused)
{
    (void)unused;
    for (;;) {
        xbox_FramebufferWindowStart();
        Sleep(0);
    }
}

int main(void)
{
    static uint32_t pixels[640 * 480];
    if (_putenv_s("RECOMP_FB_WINDOW", "1")) return 1;
    for (unsigned i = 0; i < 640 * 480; ++i) pixels[i] = 0x123456;
    xbox_FramebufferMovieFrame(pixels);
    xbox_FramebufferWindowStart();
    HWND window = NULL;
    for (unsigned i = 0; i < 500 && !window; ++i) {
        window = FindWindowA("XboxRecompFramebuffer", "Xbox Recomp - Framebuffer");
        if (!window) Sleep(10);
    }
    if (!window) { fprintf(stderr, "Display did not start\n"); return 1; }
    HANDLE worker = CreateThread(NULL, 0, repeated_start, NULL, 0, NULL);
    if (!worker) return 1;
    CloseHandle(worker);
    if (!PostMessageA(window, WM_CLOSE, 0, 0)) return 1;
    /* Successful ordinary display closure terminates all workers with code0.
     * A reopened display leaves this timer running and fails the fixture. */
    Sleep(5000);
    fprintf(stderr, "Display failed to exit while repeated start calls continued\n");
    return 1;
}
