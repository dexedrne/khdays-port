#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CABase.h>
#import <Metal/Metal.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <string.h>

extern int khdays_game_main(int, char **);
static SDL_AtomicInt game_done;
static int game_status;

int khdays_ios_layout(void)
{
    /* (its own pool: the window's loop, which calls this, never returns to one that drains) */
    @autoreleasepool {
        NSString *value = [NSUserDefaults.standardUserDefaults stringForKey:@"layout"];
        if ([value isEqualToString:@"side"]) return 1;
        if ([value isEqualToString:@"stacked"]) return 0;
        return 2;  /* single screen by default */
    }
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
        if (value.length == 0) {
            unsetenv(name.UTF8String);
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name.UTF8String); /* SDL_getenv reads its own copy */
        } else {
            setenv(name.UTF8String, value.UTF8String, 1);
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name.UTF8String, value.UTF8String, true);
        }
        fprintf(stderr, "ios: khdays-env.txt: %s=%s\n", name.UTF8String, value.UTF8String);
    }
}

/* Until round 5 SDL kept what the port wrote through its own file functions (the settings file, a folder or
 * two) in the app's Library/Application Support, where it takes relative paths from on iOS
 * (port/host/app/fs_path.h), not in Documents where the port works and the Files app and devicectl look.
 * Those files are moved to Documents/saves once, before the game starts: a settings file Documents does not
 * have yet keeps its settings; one Documents already has is kept, and the other is put beside it as
 * <name>.from-application-support. Said line by line. Folders (an empty logs/) are left where they are. */
static void move_out_of_application_support(void)
{
    @autoreleasepool {
        NSFileManager *files = NSFileManager.defaultManager;
        NSString *support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
        NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        NSString *from = [support stringByAppendingPathComponent:@"saves"];
        NSString *to = [documents stringByAppendingPathComponent:@"saves"];
        BOOL folder = NO;
        if (support == nil || documents == nil || ![files fileExistsAtPath:from isDirectory:&folder] || !folder) return;
        [files createDirectoryAtPath:to withIntermediateDirectories:YES attributes:nil error:nil];
        for (NSString *name in [files contentsOfDirectoryAtPath:from error:nil]) {
            NSString *source = [from stringByAppendingPathComponent:name];
            NSString *target = [to stringByAppendingPathComponent:name];
            NSError *error = nil;
            if (![files fileExistsAtPath:source isDirectory:&folder] || folder) continue;
            if ([files fileExistsAtPath:target]) {
                target = [target stringByAppendingString:@".from-application-support"];
                [files removeItemAtPath:target error:nil];
            }
            if ([files moveItemAtPath:source toPath:target error:&error]) {
                fprintf(stderr, "ios: Library/Application Support/saves/%s moved to Documents/saves/%s\n", name.UTF8String,
                        target.lastPathComponent.UTF8String);
            } else {
                fprintf(stderr, "ios: Library/Application Support/saves/%s could not be moved to Documents: %s\n",
                        name.UTF8String, error.localizedDescription.UTF8String);
            }
        }
    }
}

/* What UIKit shows, for the picture report: the app's state and every window of every scene with its
 * level, visibility, root view controller, view and Metal layer (its drawable's size). A picture that
 * was drawn but is not seen is explained here: a window above the game's, a hidden or zero-sized one,
 * a Metal layer without a drawable size. Called on the main thread (the window's loop runs there). */
void khdays_ios_report(const char *when)
{
    @autoreleasepool {
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
}

/* The screen's refreshes for the game's clock (port/runtime/core/clock.h, the screen's lock): Windows asks
 * its compositor; here a display link says how long a refresh lasts and when the last one was. It runs on
 * the main run loop, which SDL_PumpEvents turns, and stops by itself in the background (the clock takes
 * times older than half a second for none). KHDAYS_SCREEN_LOCK=off in khdays-env.txt keeps the lock off. */
extern void khdays_clock_screen_timing(unsigned long long period_ns, unsigned long long refresh_at_ns);

@interface KhdaysRefresh : NSObject
- (void)refreshed:(CADisplayLink *)link;
@end

@implementation KhdaysRefresh
- (void)refreshed:(CADisplayLink *)link
{
    static double said;
    const double period = link.targetTimestamp - link.timestamp, ago = CACurrentMediaTime() - link.timestamp;
    const Uint64 now = SDL_GetTicksNS();
    if (period <= 0.0 || ago < 0.0 || ago > 0.5) return;
    if (period < said * 0.98 || period > said * 1.02) {
        said = period;
        fprintf(stderr, "ios: the screen refreshes every %.2f ms (%.1f Hz); the clock's lock takes its times\n",
                period * 1000.0, 1.0 / period);
        fflush(stderr);
    }
    khdays_clock_screen_timing((unsigned long long)(period * 1e9), now - (Uint64)(ago * 1e9));
}
@end

/* The picture rate (port/host/render/picture_pace.h, host/app/mobile_touch.h): the screen's most, the phone's
 * heat, and the rate asked of the display. The display link asks for 120 refreshes a second (with Info.plist's
 * CADisableMinimumFrameDurationOnPhone, without which an iPhone stays at 60) while the pictures hold it, 60 when
 * the picture pace steps down: the screen then slows too, which saves power, and the clock's lock follows it. */
static CADisplayLink *refresh_link; /* (watch_refreshes; NULL with KHDAYS_SCREEN_LOCK=off) */
static int screen_most_hz;

static void ask_rate(int hz)
{
    if (refresh_link == nil) return;
    if (@available(iOS 15.0, *)) {
        refresh_link.preferredFrameRateRange = CAFrameRateRangeMake(hz > 60 ? 80.0f : 60.0f, (float)hz, (float)hz);
    } else {
        refresh_link.preferredFramesPerSecond = hz;
    }
}

int khdays_ios_screen_most_hz(void)
{
    return screen_most_hz;
}

int khdays_ios_thermal_state(void)
{
    return (int)NSProcessInfo.processInfo.thermalState; /* (any thread) */
}

void khdays_ios_picture_rate(int hz)
{
    fprintf(stderr, "ios: the display link asks for %d refreshes a second (the screen's most %d)\n", hz, screen_most_hz);
    fflush(stderr);
    if ([NSThread isMainThread]) {
        ask_rate(hz);
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{ ask_rate(hz); });
    }
}

static void watch_refreshes(void)
{
    const char *lock = getenv("KHDAYS_SCREEN_LOCK");
    screen_most_hz = (int)UIScreen.mainScreen.maximumFramesPerSecond;
    fprintf(stderr, "ios: the screen shows at most %d pictures a second; thermal state %ld (0 nominal, 1 fair, "
            "2 serious, 3 critical)\n", screen_most_hz, (long)NSProcessInfo.processInfo.thermalState);
    [NSNotificationCenter.defaultCenter addObserverForName:NSProcessInfoThermalStateDidChangeNotification object:nil
                                                     queue:nil usingBlock:^(NSNotification *note) {
        (void)note;
        fprintf(stderr, "ios: the thermal state is now %ld (0 nominal, 1 fair, 2 serious, 3 critical)\n",
                (long)NSProcessInfo.processInfo.thermalState);
        fflush(stderr);
    }];
    if (lock != NULL && strcmp(lock, "off") == 0) {
        fprintf(stderr, "ios: KHDAYS_SCREEN_LOCK=off: the screen's refreshes are not watched\n");
        return;
    }
    KhdaysRefresh *target = [[KhdaysRefresh alloc] init]; /* kept for the app's life */
    CADisplayLink *link = [CADisplayLink displayLinkWithTarget:target selector:@selector(refreshed:)];
    refresh_link = link;
    ask_rate(screen_most_hz > 60 ? screen_most_hz : 60); /* (the picture pace asks again as it chooses) */
    [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
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
    move_out_of_application_support();
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
    /* the home indicator hidden until the screen is touched (Info.plist hides the status bar at the launch; SDL's
     * view controller keeps it hidden over its full-screen window). SDL_IOS_HIDE_HOME_INDICATOR=2 in
     * khdays-env.txt dims it instead, the first swipe from the edge then only showing it. */
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return 1;
    watch_refreshes();
    SDL_Thread *thread = SDL_CreateThread(run_game, "game", NULL);
    if (!thread) return 1;
    while (!SDL_GetAtomicInt(&game_done)) {
        SDL_PumpEvents();
        SDL_Delay(1);
    }
    SDL_WaitThread(thread, NULL);
    return game_status;
}
