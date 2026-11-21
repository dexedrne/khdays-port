/* Cartridge-only acceptance runs always use the original MobiClip frames and audio. */
#include "video_mod.h"
#include "movie_hd.h"

void khdays_video_movie_opened(int slot, const char *path) { khdays_movie_hd_stream(slot, path, 0); }
void khdays_video_movie_stopped(void) { khdays_movie_hd_stopped(); }
void khdays_video_mod_frame(u32 index) { (void)index; }
int khdays_video_mod_picture(void) { return -1; }
int khdays_video_mod_sound(void) { return 0; }
void khdays_video_mod_mix(u32 rate, float gain, float out[2]) { (void)rate; (void)gain; (void)out; }
