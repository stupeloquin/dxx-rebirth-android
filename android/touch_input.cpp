/*
 * Android touch/gamepad input bridge - engine side, DXX-Rebirth.
 *
 * Same flat API as the dxx-redux version (android/touch_input.h), so a single
 * copy of the OpenTouch glue drives both engines. Only this file knows how
 * Rebirth stores its controls: the axes live in dcx::control_info and the
 * buttons in Controls.state, with the Descent 2 members added by
 * state_control_info.
 */

#include <array>
#include <cstring>

#include "dxxsconf.h"
#include "maths.h"
#include "game.h"
#include "fwd-game.h"
#include "kconfig.h"
#include "automap.h"
#include "window.h"
#include "timer.h"

#include "touch_input.h"

namespace dsx {

namespace {

/* Full deflection maps to the engine's per-frame maximum; kconfig_end_loop()
 * does the actual clamping, exactly as it does for mouse and joystick. */
constexpr float REL_GAIN = 40.0f;
constexpr fix REL_HOLD_TIME = F1_0 / 30;

struct touch_state
{
	/* absolute stick deflections, -1..1, held until changed */
	float forward{}, sideways{}, vertical{};
	float pitch{}, heading{}, bank{};

	/* relative deltas, expiring on a timer like the engine's mouse deltas */
	float pitch_rel{}, heading_rel{}, bank_rel{};

	std::array<uint8_t, DXX_TA_MAX> action_state{};
	std::array<uint8_t, DXX_TA_MAX> action_prev_state{};
	std::array<uint8_t, DXX_TA_MAX> action_count{};

	int select_weapon{};
};

touch_state touch;

float clampf(float v)
{
	return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

/* Only release what we set ourselves, so keyboard and mouse state survives. */
void apply_state(const enum dxx_touch_action a, uint8_t &dest)
{
	if (touch.action_state[a])
		dest = 1;
	else if (touch.action_prev_state[a])
		dest = 0;
}

/* Rebirth's triggers are "set it, the game consumes and clears it". */
void apply_count(const enum dxx_touch_action a, uint8_t &dest)
{
	if (touch.action_count[a])
	{
		dest += touch.action_count[a];
		touch.action_count[a] = 0;
	}
}

}

}

using namespace dsx;

extern "C" {

void dxx_touch_axis_forward(float v)
{
	touch.forward = clampf(v);
}

void dxx_touch_axis_sideways(float v)
{
	touch.sideways = clampf(v);
}

void dxx_touch_axis_vertical(float v)
{
	touch.vertical = clampf(v);
}

void dxx_touch_axis_pitch(float v, int relative)
{
	if (relative)
		touch.pitch_rel += v;
	else
		touch.pitch = clampf(v);
}

void dxx_touch_axis_heading(float v, int relative)
{
	if (relative)
		touch.heading_rel += v;
	else
		touch.heading = clampf(v);
}

void dxx_touch_axis_bank(float v, int relative)
{
	if (relative)
		touch.bank_rel += v;
	else
		touch.bank = clampf(v);
}

void dxx_touch_action(int state, int action)
{
	if (action < 0 || action >= DXX_TA_MAX)
		return;

	/* Count edges, not frames. */
	if (state && !touch.action_state[action])
		++touch.action_count[action];

	touch.action_state[action] = state ? 1 : 0;
}

void dxx_touch_select_weapon(int number_key)
{
	touch.select_weapon = number_key ? number_key : 10;
}

int dxx_touch_screen_mode(void)
{
	if (!window_get_front())
		return DXX_TS_BLANK;

	if (Automap_active)
		return DXX_TS_MAP;

	if (Game_wind && window_get_front() == static_cast<window *>(Game_wind))
		return DXX_TS_GAME;

	return DXX_TS_MENU;
}

void dxx_touch_apply_controls(void)
{
	static fix64 rel_clear_time = 0;
	const fix ft = FrameTime;

	if (timer_query() >= rel_clear_time)
	{
		touch.pitch_rel = touch.heading_rel = touch.bank_rel = 0;
		rel_clear_time = timer_query() + REL_HOLD_TIME;
	}

	/* --- Rotation. Pitch is halved to match the engine's own limit. --- */
	Controls.pitch_time += static_cast<fix>(touch.pitch * (ft / 2));
	Controls.heading_time += static_cast<fix>(touch.heading * ft);
	Controls.bank_time += static_cast<fix>(touch.bank * ft);

	Controls.pitch_time += static_cast<fix>(touch.pitch_rel * REL_GAIN * ft);
	Controls.heading_time += static_cast<fix>(touch.heading_rel * REL_GAIN * ft);
	Controls.bank_time += static_cast<fix>(touch.bank_rel * REL_GAIN * ft);

	/* --- Translation --- */
	Controls.forward_thrust_time += static_cast<fix>(touch.forward * ft);
	Controls.sideways_thrust_time += static_cast<fix>(touch.sideways * ft);
	Controls.vertical_thrust_time += static_cast<fix>(touch.vertical * ft);

	/* Button pairs for the axes the sticks do not cover. */
	if (touch.action_state[DXX_TA_SLIDE_UP])
		Controls.vertical_thrust_time += ft;
	if (touch.action_state[DXX_TA_SLIDE_DOWN])
		Controls.vertical_thrust_time -= ft;
	if (touch.action_state[DXX_TA_SLIDE_RIGHT])
		Controls.sideways_thrust_time += ft;
	if (touch.action_state[DXX_TA_SLIDE_LEFT])
		Controls.sideways_thrust_time -= ft;
	if (touch.action_state[DXX_TA_BANK_LEFT])
		Controls.bank_time += ft;
	if (touch.action_state[DXX_TA_BANK_RIGHT])
		Controls.bank_time -= ft;
	if (touch.action_state[DXX_TA_ACCELERATE])
		Controls.forward_thrust_time += ft;
	if (touch.action_state[DXX_TA_REVERSE])
		Controls.forward_thrust_time -= ft;

	/* --- Buttons --- */
	apply_state(DXX_TA_FIRE_PRIMARY, Controls.state.fire_primary);
	apply_state(DXX_TA_FIRE_SECONDARY, Controls.state.fire_secondary);
	apply_count(DXX_TA_FIRE_FLARE, Controls.state.fire_flare);
	apply_count(DXX_TA_DROP_BOMB, Controls.state.drop_bomb);
	apply_count(DXX_TA_CYCLE_PRIMARY, Controls.state.cycle_primary);
	apply_count(DXX_TA_CYCLE_SECONDARY, Controls.state.cycle_secondary);

	apply_state(DXX_TA_AUTOMAP, Controls.state.automap);
	apply_state(DXX_TA_REAR_VIEW, Controls.state.rear_view);

#if DXX_BUILD_DESCENT == 2
	apply_state(DXX_TA_AFTERBURNER, Controls.state.afterburner);
	apply_count(DXX_TA_HEADLIGHT, Controls.state.headlight);
	apply_state(DXX_TA_ENERGY_SHIELD, Controls.state.energy_to_shield);
#endif

	if (touch.select_weapon)
	{
		Controls.state.select_weapon = touch.select_weapon;
		touch.select_weapon = 0;
	}

	touch.action_prev_state = touch.action_state;
}

}
