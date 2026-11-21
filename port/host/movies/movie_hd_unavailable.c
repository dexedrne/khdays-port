/* The original cartridge movies use the native MobiClip decoder. External HD videos need a host decoder. */
#include "movie_hd.h"
#include <stdio.h>

int khdays_hd_open_part(const char *path, double from, double until, KhdaysHdInfo *info, char *why, size_t size)
{
    (void)path; (void)from; (void)until; (void)info;
    snprintf(why, size, "external HD video playback is unavailable on this host");
    return 0;
}

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    return khdays_hd_open_part(path, 0, 0, info, why, size);
}
int khdays_hd_status(char *why, size_t size)
{
    if (size) *why = 0;
    return 2;
}
void khdays_hd_fades(double in, double out) { (void)in; (void)out; }
void khdays_hd_start(void) {}
void khdays_hd_pause(int pause) { (void)pause; }
void khdays_hd_close(void) {}
void khdays_hd_set_gain(float gain) { (void)gain; }
int khdays_hd_picture_begin(KhdaysHdPicture *picture) { (void)picture; return 0; }
void khdays_hd_picture_done(void) {}
int khdays_hd_picture_rgba(uint8_t **rgba, size_t *capacity, int *w, int *h)
{
    (void)rgba; (void)capacity; (void)w; (void)h;
    return 0;
}
void khdays_hd_voice_play(const char *const *paths, int count)
{
    (void)paths;
    if (count) fprintf(stderr, "movies: external voice playback is unavailable on this host\n");
}
void khdays_hd_voice_stop(void) {}
void khdays_hd_voice_pause(int pause) { (void)pause; }
void khdays_hd_voice_set_gain(float gain) { (void)gain; }
