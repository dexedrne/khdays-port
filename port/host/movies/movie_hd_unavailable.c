/* The original cartridge movies use the native MobiClip decoder. External HD videos need a host decoder. */
#include "movie_hd.h"
#include <stdio.h>
#include <stdlib.h>

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
/* KHDAYS_TRACE_MOVIE_HD: each pause asked of the player is said, though there is none here -- the Linux
 * test of the phone's build reads who pauses the HD video and the voice, and when (the app sent to the
 * background, the settings menu, a lost gamepad). */
static void trace(const char *what, int pause)
{
    static int on = -1;
    if (on < 0) on = getenv("KHDAYS_TRACE_MOVIE_HD") != NULL;
    if (on) fprintf(stderr, "movie hd (no player in this build): %s %s asked\n", what, pause ? "pause" : "resume");
}
void khdays_hd_pause(int pause) { trace("video", pause); }
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
void khdays_hd_voice_pause(int pause) { trace("voice", pause); }
void khdays_hd_voice_set_gain(float gain) { (void)gain; }
