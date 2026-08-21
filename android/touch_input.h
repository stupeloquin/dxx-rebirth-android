/*
 * Android touch/gamepad input bridge.
 *
 * The OpenTouch layer (Clibs_OpenTouch/descent) talks to the engine only
 * through this flat API, so it never has to include Descent's headers - and
 * the matching .c/.cpp is the only place that knows about control_info.
 *
 * Kept identical to dxx-redux/android/touch_input.h: the whole point is that
 * one copy of the OpenTouch glue drives every Descent engine.
 */

#ifndef DXX_ANDROID_TOUCH_INPUT_H
#define DXX_ANDROID_TOUCH_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Actions, kept independent of OpenTouch's PORT_ACT_* codes. The glue
 * translates; see dxx_game_interface.cpp. */
enum dxx_touch_action
{
	DXX_TA_FIRE_PRIMARY,
	DXX_TA_FIRE_SECONDARY,
	DXX_TA_FIRE_FLARE,
	DXX_TA_DROP_BOMB,
	DXX_TA_CYCLE_PRIMARY,
	DXX_TA_CYCLE_SECONDARY,
	DXX_TA_ACCELERATE,
	DXX_TA_REVERSE,
	DXX_TA_SLIDE_LEFT,
	DXX_TA_SLIDE_RIGHT,
	DXX_TA_SLIDE_UP,
	DXX_TA_SLIDE_DOWN,
	DXX_TA_BANK_LEFT,
	DXX_TA_BANK_RIGHT,
	DXX_TA_AUTOMAP,
	DXX_TA_REAR_VIEW,
	DXX_TA_AFTERBURNER,	/* D2 only */
	DXX_TA_HEADLIGHT,	/* D2 only */
	DXX_TA_ENERGY_SHIELD,	/* D2 only */
	DXX_TA_TOGGLE_BOMB,	/* D2 only */
	DXX_TA_MAX
};

/* Screen mode, so the touch layer knows which control set to show. */
enum dxx_touch_screen
{
	DXX_TS_BLANK,
	DXX_TS_MENU,
	DXX_TS_GAME,
	DXX_TS_MAP
};

/* Axis rates, normalised to -1..1.
 *
 * "absolute" is a stick deflection and holds until changed; "relative" is a
 * swipe/gyro delta that is consumed by the next frame, matching how the engine
 * treats joystick vs mouse input. */
void dxx_touch_axis_forward(float v);
void dxx_touch_axis_sideways(float v);
void dxx_touch_axis_vertical(float v);
void dxx_touch_axis_pitch(float v, int relative);
void dxx_touch_axis_heading(float v, int relative);
void dxx_touch_axis_bank(float v, int relative);

void dxx_touch_action(int state, int action);

/* Select a weapon by its number key (1-5 primary, 6-0 secondary; 0 means the
 * '0' key, i.e. secondary 5). */
void dxx_touch_select_weapon(int number_key);

int dxx_touch_screen_mode(void);

/* Called at the end of kconfig_read_controls(), before the per-frame clamp, so
 * touch input is added to whatever the keyboard/mouse/joystick produced. */
void dxx_touch_apply_controls(void);

#ifdef __cplusplus
}
#endif

#endif /* DXX_ANDROID_TOUCH_INPUT_H */
