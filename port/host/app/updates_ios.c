/* A sideloaded iOS application is updated by installing another signed bundle. */
#include "updates.h"
#include <SDL3/SDL.h>

const char *khdays_updates_version(void)
{
    return KHDAYS_VERSION;
}
void khdays_updates_start(int enabled)
{
    (void)enabled;
}
int khdays_updates_offer(char *tag, size_t size)
{
    if (size)
        tag[0] = 0;
    return 0;
}
int khdays_updates_can_install(void)
{
    return 0;
}
void khdays_updates_download(void) {}
void khdays_updates_download_cancel(void) {}
int khdays_updates_download_state(long long *done, long long *total, char *why, size_t size)
{
    if (done)
        *done = 0;
    if (total)
        *total = 0;
    if (size)
        why[0] = 0;
    return KHDAYS_DOWNLOAD_NONE;
}
int khdays_updates_install(char *why, size_t size)
{
    SDL_strlcpy(why, "Install the updated release manually", size);
    return 0;
}
void khdays_updates_answer(int answer)
{
    (void)answer;
}
