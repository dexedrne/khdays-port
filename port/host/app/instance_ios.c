/* One game process in the iOS application container. */
#include "instance.h"
#include <SDL3/SDL.h>

static int slot;
int khdays_host_player_slot(void)
{
    return slot = 1;
}
int khdays_host_player_slot_taken(void)
{
    return slot;
}
int khdays_host_other_copies(void)
{
    return 0;
}
const char *khdays_host_user_folder(void)
{
    return "saves";
}

void khdays_host_automatic_name(char *out, size_t size)
{
    SDL_snprintf(out, size, "Player");
}

int khdays_host_player_name(const char *nickname, unsigned short *name, int room)
{
    const char *text = SDL_getenv("KHDAYS_NAME");
    if (!text || !*text)
        text = nickname && *nickname ? nickname : "Player";
    int n = 0;
    while (*text && n < room) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32 && c < 127)
            name[n++] = c;
    }
    return n;
}
