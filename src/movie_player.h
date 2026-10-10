#ifndef SSX_MOVIE_PLAYER_H
#define SSX_MOVIE_PLAYER_H
#include <stdint.h>
typedef struct {
    void (*present)(const uint32_t *pixels, void *opaque); /* 640x480 BGRA */
    int (*skip)(void *opaque);
    void *opaque;
    int realtime, audio;
    void (*audio_pcm)(const int16_t *samples, unsigned frames, void *opaque);
} SsxMovieOutput;
/* 1 = completed, 2 = user skipped, 0 = failure (never fake completion). */
int ssx_movie_play(const char *path, const SsxMovieOutput *output);
void ssx_movies_init(const char *game_directory);
#endif
