/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Touch overlay for Android - provides virtual controls when no gamepad
 * is connected.  Renders translucent touch zones and converts multi-touch
 * events into synthetic joystick/keyboard SDL events.
 */

#ifdef __ANDROID__

#include <SDL.h>
#include <GLES/gl.h>
#include <cmath>
#include <array>
#include "touch.h"

namespace dcx {

namespace {

struct touch_zone {
	float x, y, w, h;  // normalized 0..1 coordinates
	bool pressed;
	SDL_FingerID finger_id;
};

// Virtual joystick state
static struct {
	float center_x, center_y;  // normalized
	float radius;              // normalized
	float dx, dy;              // deflection -1..1
	bool active;
	SDL_FingerID finger_id;
} v_stick;

// Button zones
enum {
	BTN_FIRE_PRIMARY = 0,
	BTN_FIRE_SECONDARY,
	BTN_FLARE,
	BTN_BOMB,
	BTN_AFTERBURNER,
	BTN_AUTOMAP,
	BTN_ESC,
	BTN_COUNT
};

static std::array<touch_zone, BTN_COUNT> buttons;
static bool overlay_enabled = true;
static bool overlay_initialized = false;
static int screen_width, screen_height;

// Map virtual buttons to SDL key scancodes
static constexpr std::array<SDL_Scancode, BTN_COUNT> button_scancodes = {{
	SDL_SCANCODE_LCTRL,    // fire primary
	SDL_SCANCODE_SPACE,    // fire secondary
	SDL_SCANCODE_F,        // flare
	SDL_SCANCODE_B,        // bomb
	SDL_SCANCODE_A,        // afterburner (d2 only, harmless for d1)
	SDL_SCANCODE_TAB,      // automap
	SDL_SCANCODE_ESCAPE,   // escape/menu
}};

static void send_key_event(SDL_Scancode scancode, bool pressed)
{
	SDL_Event ev{};
	ev.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.keysym.scancode = scancode;
	ev.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
	ev.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
	ev.key.repeat = 0;
	SDL_PushEvent(&ev);
}

static void send_joy_axis(Uint8 axis, Sint16 value)
{
	SDL_Event ev{};
	ev.type = SDL_JOYAXISMOTION;
	ev.jaxis.which = 0;
	ev.jaxis.axis = axis;
	ev.jaxis.value = value;
	SDL_PushEvent(&ev);
}

static void draw_circle(float cx, float cy, float r, int segments, float alpha)
{
	std::array<GLfloat, 128> verts;  // max 64 segments * 2
	if (segments > 64) segments = 64;
	for (int i = 0; i < segments; i++)
	{
		float angle = 2.0f * static_cast<float>(M_PI) * i / segments;
		verts[i * 2] = cx + r * cosf(angle);
		verts[i * 2 + 1] = cy + r * sinf(angle);
	}
	glColor4f(1.0f, 1.0f, 1.0f, alpha);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts.data());
	glDrawArrays(GL_LINE_LOOP, 0, segments);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_filled_rect(float x, float y, float w, float h, float r, float g, float b, float a)
{
	const GLfloat verts[] = {
		x, y,
		x + w, y,
		x, y + h,
		x + w, y + h,
	};
	glColor4f(r, g, b, a);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}

}  // anonymous namespace

void touch_overlay_init(int w, int h)
{
	screen_width = w;
	screen_height = h;

	// Virtual stick: left side of screen, lower area
	v_stick.center_x = 0.15f;
	v_stick.center_y = 0.70f;
	v_stick.radius = 0.12f;
	v_stick.dx = v_stick.dy = 0.0f;
	v_stick.active = false;

	// Buttons: right side of screen
	// Fire primary - large button, lower right
	buttons[BTN_FIRE_PRIMARY] = {0.80f, 0.60f, 0.15f, 0.15f, false, -1};
	// Fire secondary - above fire primary
	buttons[BTN_FIRE_SECONDARY] = {0.80f, 0.42f, 0.15f, 0.15f, false, -1};
	// Flare - left of fire primary
	buttons[BTN_FLARE] = {0.63f, 0.60f, 0.12f, 0.12f, false, -1};
	// Bomb - left of fire secondary
	buttons[BTN_BOMB] = {0.63f, 0.45f, 0.12f, 0.12f, false, -1};
	// Afterburner - smaller, above secondaries
	buttons[BTN_AFTERBURNER] = {0.75f, 0.28f, 0.10f, 0.10f, false, -1};
	// Automap - top right area
	buttons[BTN_AUTOMAP] = {0.88f, 0.02f, 0.10f, 0.08f, false, -1};
	// Escape/Menu - top left area
	buttons[BTN_ESC] = {0.02f, 0.02f, 0.10f, 0.08f, false, -1};

	overlay_initialized = true;
}

void touch_overlay_draw()
{
	if (!overlay_enabled || !overlay_initialized)
		return;

	// Save GL state
	glPushMatrix();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrthof(0, 1, 1, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Draw virtual stick base
	float alpha = v_stick.active ? 0.4f : 0.2f;
	draw_circle(v_stick.center_x, v_stick.center_y, v_stick.radius, 32, alpha);

	// Draw stick position indicator
	if (v_stick.active)
	{
		float knob_x = v_stick.center_x + v_stick.dx * v_stick.radius;
		float knob_y = v_stick.center_y + v_stick.dy * v_stick.radius;
		draw_circle(knob_x, knob_y, 0.025f, 16, 0.5f);
	}

	// Draw buttons
	static constexpr std::array<const char*, BTN_COUNT> btn_labels = {{
		"FIRE", "MISS", "FLR", "BMB", "AFT", "MAP", "ESC"
	}};
	(void)btn_labels;  // labels drawn by text overlay if available

	for (int i = 0; i < BTN_COUNT; i++)
	{
		auto &b = buttons[i];
		float a = b.pressed ? 0.4f : 0.15f;
		float r = (i == BTN_FIRE_PRIMARY) ? 1.0f : 0.7f;
		float g = (i == BTN_FIRE_SECONDARY) ? 0.5f : 0.7f;
		draw_filled_rect(b.x, b.y, b.w, b.h, r, g, 0.7f, a);
	}

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	// Restore GL state
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

int touch_overlay_handle_event(const SDL_Event &event)
{
	if (!overlay_enabled || !overlay_initialized)
		return 0;

	float fx, fy;
	SDL_FingerID fid;

	switch (event.type)
	{
		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
		{
			fx = event.tfinger.x;
			fy = event.tfinger.y;
			fid = event.tfinger.fingerId;

			// Check virtual stick zone (left 40% of screen, lower 60%)
			if (fx < 0.40f && fy > 0.40f)
			{
				float ddx = (fx - v_stick.center_x) / v_stick.radius;
				float ddy = (fy - v_stick.center_y) / v_stick.radius;
				float dist = sqrtf(ddx * ddx + ddy * ddy);
				if (dist > 1.0f)
				{
					ddx /= dist;
					ddy /= dist;
				}

				v_stick.dx = ddx;
				v_stick.dy = ddy;
				v_stick.active = true;
				v_stick.finger_id = fid;

				// Send as joystick axes: axis 0 = turn (X), axis 1 = pitch (Y)
				send_joy_axis(0, static_cast<Sint16>(ddx * 32767));
				send_joy_axis(1, static_cast<Sint16>(ddy * 32767));
				return 1;
			}

			// Check button zones
			for (int i = 0; i < BTN_COUNT; i++)
			{
				auto &b = buttons[i];
				if (fx >= b.x && fx <= b.x + b.w &&
				    fy >= b.y && fy <= b.y + b.h)
				{
					if (!b.pressed)
					{
						b.pressed = true;
						b.finger_id = fid;
						send_key_event(button_scancodes[i], true);
					}
					return 1;
				}
			}
			break;
		}

		case SDL_FINGERUP:
		{
			fid = event.tfinger.fingerId;

			// Release virtual stick
			if (v_stick.active && v_stick.finger_id == fid)
			{
				v_stick.active = false;
				v_stick.dx = v_stick.dy = 0.0f;
				send_joy_axis(0, 0);
				send_joy_axis(1, 0);
				return 1;
			}

			// Release buttons
			for (int i = 0; i < BTN_COUNT; i++)
			{
				auto &b = buttons[i];
				if (b.pressed && b.finger_id == fid)
				{
					b.pressed = false;
					send_key_event(button_scancodes[i], false);
					return 1;
				}
			}
			break;
		}
	}
	return 0;
}

void touch_overlay_set_enabled(bool enabled)
{
	overlay_enabled = enabled;
}

bool touch_overlay_is_enabled()
{
	return overlay_enabled;
}

}  // namespace dcx

#endif  // __ANDROID__
