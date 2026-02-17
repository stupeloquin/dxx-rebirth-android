/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Touch overlay for Android - provides dual virtual sticks and action
 * buttons when no gamepad is connected.  Renders translucent touch zones
 * using GLES 1.x and converts multi-touch events into synthetic keyboard
 * SDL events that map to the default keyboard bindings.
 *
 * Left stick  → W/S/A/D (accelerate, reverse, slide left/right)
 * Right stick → Arrow keys (pitch up/down, turn left/right)
 * Buttons     → Ctrl, Space, F, B, Q, E, Tab, Escape
 */

#ifdef __ANDROID__

#include "dxxsconf.h"
#include <SDL.h>
#if !DXX_USE_VULKAN
#include <GLES/gl.h>
#endif
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

// Virtual stick with 4-directional keyboard output
struct virtual_stick {
	float center_x, center_y;  // normalized
	float radius;              // normalized
	float dx, dy;              // deflection -1..1
	bool active;
	SDL_FingerID finger_id;
	// Track which directional keys are currently held
	bool key_up, key_down, key_left, key_right;
	// Scancodes for each direction
	SDL_Scancode sc_up, sc_down, sc_left, sc_right;
};

static virtual_stick left_stick, right_stick, fire_stick;

// Aspect ratio correction: in normalized 0..1 coords, a circle needs
// its X radius scaled by (height/width) to appear round on screen.
static float aspect_ratio;  // width / height

// Button zones
enum {
	BTN_FIRE_PRIMARY = 0,
	BTN_FIRE_SECONDARY,
	BTN_FLARE,
	BTN_BOMB,
	BTN_BANK_LEFT,
	BTN_BANK_RIGHT,
	BTN_AUTOMAP,
	BTN_ESC,
	BTN_ACCEPT,
	BTN_COUNT
};

static std::array<touch_zone, BTN_COUNT> buttons;
static bool overlay_enabled = true;
static bool overlay_initialized = false;
static bool controls_visible = true;  // false = only toggle button visible
static touch_zone toggle_button;      // always-visible hide/show button
static int screen_width, screen_height;

// Map virtual buttons to SDL key scancodes matching default keyboard bindings
static constexpr std::array<SDL_Scancode, BTN_COUNT> button_scancodes = {{
	SDL_SCANCODE_LCTRL,    // fire primary
	SDL_SCANCODE_SPACE,    // fire secondary
	SDL_SCANCODE_F,        // flare
	SDL_SCANCODE_B,        // bomb
	SDL_SCANCODE_Q,        // bank left
	SDL_SCANCODE_E,        // bank right
	SDL_SCANCODE_TAB,      // automap
	SDL_SCANCODE_ESCAPE,   // escape/back
	SDL_SCANCODE_RETURN,   // accept/enter
}};

// Button colors: {r, g, b}
struct btn_color { float r, g, b; };
static constexpr std::array<btn_color, BTN_COUNT> button_colors = {{
	{1.0f, 0.3f, 0.2f},   // fire primary - red
	{1.0f, 0.6f, 0.2f},   // fire secondary - orange
	{1.0f, 1.0f, 0.3f},   // flare - yellow
	{1.0f, 0.8f, 0.0f},   // bomb - gold
	{0.3f, 0.7f, 1.0f},   // bank left - blue
	{0.3f, 0.7f, 1.0f},   // bank right - blue
	{0.5f, 0.8f, 0.5f},   // automap - green
	{0.7f, 0.7f, 0.7f},   // escape - gray
	{0.4f, 1.0f, 0.4f},   // accept - bright green
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

// Update directional keys from a virtual stick's deflection
static void update_stick_keys(virtual_stick &stick, float dx, float dy)
{
	constexpr float threshold = 0.3f;

	bool want_left = dx < -threshold;
	bool want_right = dx > threshold;
	bool want_up = dy < -threshold;
	bool want_down = dy > threshold;

	if (want_left != stick.key_left)
	{
		stick.key_left = want_left;
		send_key_event(stick.sc_left, want_left);
	}
	if (want_right != stick.key_right)
	{
		stick.key_right = want_right;
		send_key_event(stick.sc_right, want_right);
	}
	if (want_up != stick.key_up)
	{
		stick.key_up = want_up;
		send_key_event(stick.sc_up, want_up);
	}
	if (want_down != stick.key_down)
	{
		stick.key_down = want_down;
		send_key_event(stick.sc_down, want_down);
	}
}

// Release all held directional keys for a stick
static void release_all_stick_keys(virtual_stick &stick)
{
	if (stick.key_left)
	{
		stick.key_left = false;
		send_key_event(stick.sc_left, false);
	}
	if (stick.key_right)
	{
		stick.key_right = false;
		send_key_event(stick.sc_right, false);
	}
	if (stick.key_up)
	{
		stick.key_up = false;
		send_key_event(stick.sc_up, false);
	}
	if (stick.key_down)
	{
		stick.key_down = false;
		send_key_event(stick.sc_down, false);
	}
}

// Process a touch event against a virtual stick
// Returns true if the touch was consumed by this stick
static bool process_stick_touch(virtual_stick &stick, float fx, float fy, SDL_FingerID fid,
                                float zone_x_min, float zone_x_max, float zone_y_min)
{
	if (fx < zone_x_min || fx > zone_x_max || fy < zone_y_min)
		return false;

	float ddx = (fx - stick.center_x) / (stick.radius / aspect_ratio);
	float ddy = (fy - stick.center_y) / stick.radius;
	float dist = sqrtf(ddx * ddx + ddy * ddy);
	if (dist > 1.0f)
	{
		ddx /= dist;
		ddy /= dist;
	}

	stick.dx = ddx;
	stick.dy = ddy;
	stick.active = true;
	stick.finger_id = fid;
	update_stick_keys(stick, ddx, ddy);
	return true;
}

#if !DXX_USE_VULKAN
// Draw circles with aspect-ratio correction so they appear round.
// The radius `r` is in normalized Y units; X is scaled by 1/aspect_ratio.
static void draw_circle(float cx, float cy, float r, int segments,
                        float cr, float cg, float cb, float alpha)
{
	std::array<GLfloat, 128> verts;  // max 64 segments * 2
	if (segments > 64) segments = 64;
	const float rx = r / aspect_ratio;
	for (int i = 0; i < segments; i++)
	{
		float angle = 2.0f * static_cast<float>(M_PI) * i / segments;
		verts[i * 2] = cx + rx * cosf(angle);
		verts[i * 2 + 1] = cy + r * sinf(angle);
	}
	glColor4f(cr, cg, cb, alpha);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts.data());
	glDrawArrays(GL_LINE_LOOP, 0, segments);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_filled_circle(float cx, float cy, float r, int segments,
                               float cr, float cg, float cb, float alpha)
{
	// Triangle fan: center + rim vertices
	std::array<GLfloat, 130> verts;  // (1 + max 64) * 2
	if (segments > 64) segments = 64;
	const float rx = r / aspect_ratio;
	verts[0] = cx;
	verts[1] = cy;
	for (int i = 0; i <= segments; i++)
	{
		float angle = 2.0f * static_cast<float>(M_PI) * i / segments;
		verts[(i + 1) * 2] = cx + rx * cosf(angle);
		verts[(i + 1) * 2 + 1] = cy + r * sinf(angle);
	}
	glColor4f(cr, cg, cb, alpha);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts.data());
	glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_filled_rect(float x, float y, float w, float h,
                             float r, float g, float b, float a)
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

static void draw_rect_outline(float x, float y, float w, float h,
                              float r, float g, float b, float a)
{
	const GLfloat verts[] = {
		x, y,
		x + w, y,
		x + w, y + h,
		x, y + h,
	};
	glColor4f(r, g, b, a);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glDrawArrays(GL_LINE_LOOP, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_stick(const virtual_stick &stick, float cr, float cg, float cb)
{
	float alpha = stick.active ? 0.35f : 0.15f;
	const float rx = stick.radius / aspect_ratio;

	// Outer ring
	draw_circle(stick.center_x, stick.center_y, stick.radius, 32, cr, cg, cb, alpha);

	// Inner deadzone indicator
	draw_circle(stick.center_x, stick.center_y, stick.radius * 0.3f, 16, cr, cg, cb, alpha * 0.5f);

	// Knob position indicator when active
	if (stick.active)
	{
		float knob_x = stick.center_x + stick.dx * rx;
		float knob_y = stick.center_y + stick.dy * stick.radius;
		draw_filled_circle(knob_x, knob_y, 0.025f, 16, cr, cg, cb, 0.5f);
	}

	// Crosshair lines (aspect-corrected)
	glColor4f(cr, cg, cb, alpha * 0.4f);
	const GLfloat cross_h[] = {
		stick.center_x - rx, stick.center_y,
		stick.center_x + rx, stick.center_y,
	};
	const GLfloat cross_v[] = {
		stick.center_x, stick.center_y - stick.radius,
		stick.center_x, stick.center_y + stick.radius,
	};
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, cross_h);
	glDrawArrays(GL_LINES, 0, 2);
	glVertexPointer(2, GL_FLOAT, 0, cross_v);
	glDrawArrays(GL_LINES, 0, 2);
	glDisableClientState(GL_VERTEX_ARRAY);
}
#endif  // !DXX_USE_VULKAN

}  // anonymous namespace

void touch_overlay_init(int w, int h)
{
	screen_width = w;
	screen_height = h;
	aspect_ratio = static_cast<float>(w) / static_cast<float>(h);

	// Left stick: lower-left area - movement (W/A/S/D)
	left_stick.center_x = 0.15f;
	left_stick.center_y = 0.72f;
	left_stick.radius = 0.12f;
	left_stick.dx = left_stick.dy = 0.0f;
	left_stick.active = false;
	left_stick.key_left = left_stick.key_right = left_stick.key_up = left_stick.key_down = false;
	left_stick.sc_up = SDL_SCANCODE_W;       // accelerate
	left_stick.sc_down = SDL_SCANCODE_S;     // reverse
	left_stick.sc_left = SDL_SCANCODE_A;     // slide left
	left_stick.sc_right = SDL_SCANCODE_D;    // slide right

	// Right stick: lower-center area - rotation only (arrows)
	right_stick.center_x = 0.55f;
	right_stick.center_y = 0.72f;
	right_stick.radius = 0.12f;
	right_stick.dx = right_stick.dy = 0.0f;
	right_stick.active = false;
	right_stick.key_left = right_stick.key_right = right_stick.key_up = right_stick.key_down = false;
	right_stick.sc_up = SDL_SCANCODE_UP;      // pitch forward
	right_stick.sc_down = SDL_SCANCODE_DOWN;  // pitch backward
	right_stick.sc_left = SDL_SCANCODE_LEFT;  // turn left
	right_stick.sc_right = SDL_SCANCODE_RIGHT; // turn right

	// Fire stick: right-side wheel - fires AND rotates when dragged.
	// Same rotation scancodes as right_stick so both control the same axes.
	fire_stick.center_x = 0.85f;
	fire_stick.center_y = 0.55f;
	fire_stick.radius = 0.18f;
	fire_stick.dx = fire_stick.dy = 0.0f;
	fire_stick.active = false;
	fire_stick.key_left = fire_stick.key_right = fire_stick.key_up = fire_stick.key_down = false;
	fire_stick.sc_up = SDL_SCANCODE_UP;
	fire_stick.sc_down = SDL_SCANCODE_DOWN;
	fire_stick.sc_left = SDL_SCANCODE_LEFT;
	fire_stick.sc_right = SDL_SCANCODE_RIGHT;

	// Buttons layout:
	// Fire secondary - right side upper (above fire wheel)
	buttons[BTN_FIRE_SECONDARY] = {0.82f, 0.18f, 0.16f, 0.12f, false, -1};
	// Bank left - left side, above stick
	buttons[BTN_BANK_LEFT]      = {0.02f, 0.32f, 0.12f, 0.12f, false, -1};
	// Bank right - top right area
	buttons[BTN_BANK_RIGHT]     = {0.78f, 0.02f, 0.10f, 0.07f, false, -1};
	// Flare - top center-left
	buttons[BTN_FLARE]          = {0.36f, 0.02f, 0.10f, 0.07f, false, -1};
	// Bomb - top center-right
	buttons[BTN_BOMB]           = {0.50f, 0.02f, 0.10f, 0.07f, false, -1};
	// Automap - top right
	buttons[BTN_AUTOMAP]        = {0.90f, 0.02f, 0.08f, 0.07f, false, -1};
	// Escape/Back - top left
	buttons[BTN_ESC]            = {0.02f, 0.02f, 0.08f, 0.07f, false, -1};
	// Accept/Enter - next to escape, top left
	buttons[BTN_ACCEPT]         = {0.12f, 0.02f, 0.08f, 0.07f, false, -1};

	// Toggle button - small, always visible, top center
	toggle_button = {0.22f, 0.02f, 0.06f, 0.05f, false, -1};

	controls_visible = true;
	overlay_initialized = true;
}

void touch_overlay_draw()
{
	if (!overlay_enabled || !overlay_initialized)
		return;

#if DXX_USE_VULKAN
	// TODO: Vulkan touch overlay rendering
	(void)0;
#else
	// Save GL state that we modify
	GLboolean tex2d_was_enabled, depth_was_enabled, blend_was_enabled;
	GLint prev_matrix_mode;
	GLfloat prev_color[4];
	GLint prev_blend_src, prev_blend_dst;
	glGetBooleanv(GL_TEXTURE_2D, &tex2d_was_enabled);
	glGetBooleanv(GL_DEPTH_TEST, &depth_was_enabled);
	glGetBooleanv(GL_BLEND, &blend_was_enabled);
	glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);
	glGetFloatv(GL_CURRENT_COLOR, prev_color);
	glGetIntegerv(GL_BLEND_SRC, &prev_blend_src);
	glGetIntegerv(GL_BLEND_DST, &prev_blend_dst);

	// Set up orthographic projection for overlay
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrthof(0, 1, 1, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Always draw the toggle button (hide/show controls)
	{
		float ta = controls_visible ? 0.15f : 0.35f;
		draw_filled_rect(toggle_button.x, toggle_button.y, toggle_button.w, toggle_button.h,
		                 0.8f, 0.8f, 0.8f, ta);
		draw_rect_outline(toggle_button.x, toggle_button.y, toggle_button.w, toggle_button.h,
		                  1.0f, 1.0f, 1.0f, ta + 0.15f);
	}

	if (controls_visible)
	{
		// Draw left stick (blue tint - movement)
		draw_stick(left_stick, 0.4f, 0.6f, 1.0f);

		// Draw right stick (green tint - rotation only)
		draw_stick(right_stick, 0.4f, 1.0f, 0.6f);

		// Draw fire stick (red tint - fire + rotation wheel)
		draw_stick(fire_stick, 1.0f, 0.3f, 0.2f);

		// Draw buttons with distinct colors (skip fire primary, handled by fire_stick)
		for (int i = 0; i < BTN_COUNT; i++)
		{
			if (i == BTN_FIRE_PRIMARY)
				continue;
			auto &b = buttons[i];
			auto &c = button_colors[i];
			float alpha = b.pressed ? 0.45f : 0.18f;

			// Filled background
			draw_filled_rect(b.x, b.y, b.w, b.h, c.r, c.g, c.b, alpha);
			// Border outline
			draw_rect_outline(b.x, b.y, b.w, b.h, c.r, c.g, c.b, alpha + 0.15f);
		}
	}

	// Restore all GL state
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	if (tex2d_was_enabled) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
	if (depth_was_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (blend_was_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	glBlendFunc(prev_blend_src, prev_blend_dst);
	glColor4f(prev_color[0], prev_color[1], prev_color[2], prev_color[3]);
	glMatrixMode(prev_matrix_mode);
#endif  // !DXX_USE_VULKAN
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

			// Toggle button: always active, tap to hide/show controls
			if (event.type == SDL_FINGERDOWN &&
			    fx >= toggle_button.x && fx <= toggle_button.x + toggle_button.w &&
			    fy >= toggle_button.y && fy <= toggle_button.y + toggle_button.h)
			{
				controls_visible = !controls_visible;
				return 1;
			}

			// If controls are hidden, don't process anything else
			if (!controls_visible)
				break;

			// Check left stick zone (left 35% of screen, lower 50%)
			if ((!left_stick.active || left_stick.finger_id == fid) &&
			    process_stick_touch(left_stick, fx, fy, fid, 0.0f, 0.35f, 0.45f))
				return 1;

			// Check right stick zone (35%-75% of screen, lower 55%)
			if ((!right_stick.active || right_stick.finger_id == fid) &&
			    process_stick_touch(right_stick, fx, fy, fid, 0.35f, 0.75f, 0.45f))
				return 1;

			// Fire stick: right-side wheel that fires AND rotates.
			// Touch down starts firing; drag also controls rotation.
			if ((!fire_stick.active || fire_stick.finger_id == fid) &&
			    fx >= 0.72f && fy >= 0.30f)
			{
				if (!fire_stick.active && event.type == SDL_FINGERDOWN)
				{
					// Start firing on initial touch
					fire_stick.active = true;
					fire_stick.finger_id = fid;
					fire_stick.dx = fire_stick.dy = 0.0f;
					send_key_event(SDL_SCANCODE_LCTRL, true);  // fire primary
				}
				if (fire_stick.active && fire_stick.finger_id == fid)
				{
					// Update rotation from drag
					float ddx = (fx - fire_stick.center_x) / (fire_stick.radius / aspect_ratio);
					float ddy = (fy - fire_stick.center_y) / fire_stick.radius;
					float dist = sqrtf(ddx * ddx + ddy * ddy);
					if (dist > 1.0f)
					{
						ddx /= dist;
						ddy /= dist;
					}
					fire_stick.dx = ddx;
					fire_stick.dy = ddy;
					update_stick_keys(fire_stick, ddx, ddy);
					return 1;
				}
			}

			// Check button zones (skip fire primary - handled by fire_stick)
			for (int i = 0; i < BTN_COUNT; i++)
			{
				if (i == BTN_FIRE_PRIMARY)
					continue;
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

			// Release left stick
			if (left_stick.active && left_stick.finger_id == fid)
			{
				left_stick.active = false;
				left_stick.dx = left_stick.dy = 0.0f;
				release_all_stick_keys(left_stick);
				return 1;
			}

			// Release right stick
			if (right_stick.active && right_stick.finger_id == fid)
			{
				right_stick.active = false;
				right_stick.dx = right_stick.dy = 0.0f;
				release_all_stick_keys(right_stick);
				return 1;
			}

			// Release fire stick (stops firing AND releases rotation)
			if (fire_stick.active && fire_stick.finger_id == fid)
			{
				fire_stick.active = false;
				fire_stick.dx = fire_stick.dy = 0.0f;
				send_key_event(SDL_SCANCODE_LCTRL, false);  // stop firing
				release_all_stick_keys(fire_stick);
				return 1;
			}

			// Release buttons
			for (int i = 0; i < BTN_COUNT; i++)
			{
				if (i == BTN_FIRE_PRIMARY)
					continue;
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
