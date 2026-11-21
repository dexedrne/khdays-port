#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
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
    if ([value isEqualToString:@"stacked"]) return 0;
    return 2;  /* single screen by default */
}

/* The testing switches (docs/RENDERING.md, the picture report of host/app/window.c) on the phone, where
 * nothing sets the environment: each NAME=VALUE line of Documents/khdays-env.txt is set before the game
 * starts ("NAME=" unsets it). Without the file, the first launches trace the title's situation. */
static void take_environment(void)
{
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *path = [documents stringByAppendingPathComponent:@"khdays-env.txt"];
    NSString *text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    setenv("KHDAYS_TRACE_SITUATION", "1", 0);
    for (NSString *line in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSRange equals = [line rangeOfString:@"="];
        if (equals.location == NSNotFound || equals.location == 0 || [line hasPrefix:@"#"]) continue;
        NSString *name = [[line substringToIndex:equals.location]
            stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        NSString *value = [[line substringFromIndex:equals.location + 1]
            stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if (value.length == 0) unsetenv(name.UTF8String);
        else setenv(name.UTF8String, value.UTF8String, 1);
        fprintf(stderr, "ios: khdays-env.txt: %s=%s\n", name.UTF8String, value.UTF8String);
    }
}

/* What UIKit shows, for the picture report: the app's state and every window of every scene with its
 * level, visibility, root view controller, view and Metal layer (its drawable's size). A picture that
 * was drawn but is not seen is explained here: a window above the game's, a hidden or zero-sized one,
 * a Metal layer without a drawable size. Called on the main thread (the window's loop runs there). */
void khdays_ios_report(const char *when)
{
    UIApplication *app = UIApplication.sharedApplication;
    fprintf(stderr, "ios: %s: app state %ld (0 active, 1 inactive, 2 background), %lu scenes\n", when,
            (long)app.applicationState, (unsigned long)app.connectedScenes.count);
    for (UIScene *scene in app.connectedScenes) {
        fprintf(stderr, "ios:  scene %s, activation %ld (0 active, 1 inactive, 2 background, -1 unattached)\n",
                NSStringFromClass(scene.class).UTF8String, (long)scene.activationState);
        if (![scene isKindOfClass:UIWindowScene.class]) continue;
        UIWindowScene *ws = (UIWindowScene *)scene;
        fprintf(stderr, "ios:  screen %.0fx%.0f points, scale %.2f, native %.0fx%.0f, orientation %ld\n",
                ws.screen.bounds.size.width, ws.screen.bounds.size.height, ws.screen.nativeScale,
                ws.screen.nativeBounds.size.width, ws.screen.nativeBounds.size.height, (long)ws.interfaceOrientation);
        for (UIWindow *w in ws.windows) {
            UIView *v = w.rootViewController.view;
            CALayer *layer = v.layer;
            fprintf(stderr,
                    "ios:   window %s level %.0f key %d hidden %d alpha %.2f frame %.0f,%.0f %.0fx%.0f; root %s; "
                    "view %s frame %.0f,%.0f %.0fx%.0f hidden %d alpha %.2f scale %.2f; layer %s",
                    NSStringFromClass(w.class).UTF8String, (double)w.windowLevel, (int)w.isKeyWindow, (int)w.hidden,
                    w.alpha, w.frame.origin.x, w.frame.origin.y, w.frame.size.width, w.frame.size.height,
                    NSStringFromClass(w.rootViewController.class).UTF8String, NSStringFromClass(v.class).UTF8String,
                    v.frame.origin.x, v.frame.origin.y, v.frame.size.width, v.frame.size.height, (int)v.hidden,
                    v.alpha, v.contentScaleFactor, NSStringFromClass(layer.class).UTF8String);
            if ([layer isKindOfClass:CAMetalLayer.class]) {
                CAMetalLayer *metal = (CAMetalLayer *)layer;
                fprintf(stderr, " drawable %.0fx%.0f, pixel format %lu, opaque %d, device %s",
                        metal.drawableSize.width, metal.drawableSize.height, (unsigned long)metal.pixelFormat,
                        (int)metal.opaque, metal.device ? metal.device.name.UTF8String : "none");
            }
            fprintf(stderr, "; %lu subviews\n", (unsigned long)v.subviews.count);
        }
    }
    fflush(stderr);
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
    take_environment();
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
