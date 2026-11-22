/* The Linux test build's emulated gamepads (pad_script.c; CMake's KHDAYS_MOBILE_TEST, never a release):
 * KHDAYS_PAD_SCRIPT plays SDL virtual gamepads into the window as the game's frames pass.
 *
 * The script is steps separated by spaces. A step is FRAME[#PAD]OP: the game's VBlank it happens at
 * (counted from the last anchor before it, or from the start), the gamepad it is for (1 to 4; 1 when
 * left out) and what happens:
 *   *KIND    the gamepad is connected: ps5, switchpro, switchpro-nocapture or xbox
 *   /        it is disconnected
 *   +NAME    a button goes down, by its position's name as the settings file writes it (south, east,
 *            west, north, leftshoulder, rightshoulder, lefttrigger, righttrigger, leftstick,
 *            rightstick, start, back, guide, misc1, touchpad, dpup, dpdown, dpleft, dpright)
 *   -NAME    it is let go
 *   >AXIS:V  an axis moves: lx, ly, rx, ry (-32768 to 32767, y grows downwards), lt, rt (0 to 32767)
 *   !background / !foreground   the phone's app leaves the screen and comes back (the window minimized
 *                    and restored around the lifecycle events, as SDL does on iOS: nothing is
 *                    presented between the two)
 *   !touch=X,Y       a finger taps the window there (its points)
 *   !size=WxH        the window gets this size, as a turned, folded or unfolded phone's does
 *   !safe=L,T,R,B    its unsafe margins from now on (instead of KHDAYS_MOBILE_INSETS), with SDL's
 *                    safe-area event
 *   !report          the next picture is reported and saved (window.c's picture report)
 *   !thermal=N       the phone's thermal state from now on: 0 nominal, 1 fair, 2 serious, 3 critical
 *                    (host/render/picture_pace.h: the picture rate follows it)
 *   !slow=MS         each picture made MS milliseconds longer from now on (0: as it is): a phone too
 *                    slow for its picture rate
 * An anchor restarts the count: `t` when the title's menu first shows, `sN` when scene N begins (the
 * anchors begin in the order the script gives them).
 *
 * Example: "0*ps5 t 60+south 66-south" connects a DualSense at once and presses Cross a second after
 * the title's menu shows. Each step is logged ("pad script: frame N (scene S): pad 1 +south"). */
#ifndef KHDAYS_PAD_SCRIPT_H
#define KHDAYS_PAD_SCRIPT_H

#include <SDL3/SDL.h>

/* Reads KHDAYS_PAD_SCRIPT; 1 when there is a script. SDL's gamepads must be up. */
int khdays_pad_script_start(void);

/* Runs the steps due at VBlank `frame`, in scene `scene`; `title_shown`: the title's menu shows. */
void khdays_pad_script_frame(unsigned frame, unsigned scene, int title_shown);

/* Whether the script asked for a picture report since the last call ("!report"). */
int khdays_pad_script_take_report(void);

#endif
