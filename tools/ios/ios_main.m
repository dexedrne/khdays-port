#import <Foundation/Foundation.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>

extern int khdays_game_main(int, char **);
static SDL_AtomicInt game_done;
static int game_status;

int khdays_ios_layout(void)
{
    NSString *value = [NSUserDefaults.standardUserDefaults stringForKey:@"layout"];
    if ([value isEqualToString:@"side"]) return 1;
    return [value isEqualToString:@"single"] ? 2 : 0;
}

static int run_game(void *unused)
{
    (void)unused;
    char *args[] = {"khdays-native", NULL};
    game_status = khdays_game_main(1, args);
    SDL_SetAtomicInt(&game_done, 1);
    return game_status;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return 1;
    SDL_Thread *thread = SDL_CreateThread(run_game, "game", NULL);
    if (!thread) return 1;
    while (!SDL_GetAtomicInt(&game_done)) {
        SDL_PumpEvents();
        SDL_Delay(1);
    }
    SDL_WaitThread(thread, NULL);
    return game_status;
}
