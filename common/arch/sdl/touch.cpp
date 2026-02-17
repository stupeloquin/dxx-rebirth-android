/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Touch overlay for Android — floating dual thumbsticks with fire zone
 * and textured action buttons.
 *
 * Left half of screen  → floating movement stick (W/A/S/D)
 * Right half of screen → floating look stick (arrow keys)
 *   Upper-right zone   → also fires primary weapon (Ctrl) while looking
 *   Lower-left zone    → look only, no fire
 *
 * Buttons along top edge → Space, F, B, Q, E, Tab, Esc, Enter
 *
 * Sticks appear where the thumb touches and disappear on release.
 * Fire zone is separated by a diagonal: 2*fx - fy > 1.0
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

// stb_image for PNG texture loading
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#include "touch_icons.h"

namespace dcx {

namespace {

struct touch_zone {
	float x, y, w, h;  // normalized 0..1 coordinates
	bool pressed;
	SDL_FingerID finger_id;
};

// Virtual stick with 4-directional keyboard output
struct virtual_stick {
	float center_x, center_y;  // set dynamically on finger-down
	float radius;              // normalized (Y units)
	float dx, dy;              // deflection -1..1
	bool active;
	bool firing;               // right stick only: is Ctrl held?
	SDL_FingerID finger_id;
	bool key_up, key_down, key_left, key_right;
	SDL_Scancode sc_up, sc_down, sc_left, sc_right;
};

static virtual_stick left_stick, right_stick;

// Aspect ratio: width / height — used to make circles round
static float aspect_ratio;

// Button enum — no BTN_FIRE_PRIMARY, fire is handled by the right stick zone
enum {
	BTN_FIRE_SECONDARY = 0,
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
static bool controls_visible = true;
static touch_zone toggle_button;
static int screen_width, screen_height;

static constexpr std::array<SDL_Scancode, BTN_COUNT> button_scancodes = {{
	SDL_SCANCODE_SPACE,    // fire secondary
	SDL_SCANCODE_F,        // flare
	SDL_SCANCODE_B,        // bomb
	SDL_SCANCODE_Q,        // bank left
	SDL_SCANCODE_E,        // bank right
	SDL_SCANCODE_TAB,      // automap
	SDL_SCANCODE_ESCAPE,   // escape/back
	SDL_SCANCODE_RETURN,   // accept/enter
}};

struct btn_color { float r, g, b; };
static constexpr std::array<btn_color, BTN_COUNT> button_colors = {{
	{1.0f, 0.6f, 0.2f},   // fire secondary - orange
	{1.0f, 1.0f, 0.3f},   // flare - yellow
	{1.0f, 0.8f, 0.0f},   // bomb - gold
	{0.3f, 0.7f, 1.0f},   // bank left - blue
	{0.3f, 0.7f, 1.0f},   // bank right - blue
	{0.5f, 0.8f, 0.5f},   // automap - green
	{0.7f, 0.7f, 0.7f},   // escape - gray
	{0.4f, 1.0f, 0.4f},   // accept - bright green
}};

#if !DXX_USE_VULKAN
// Button icon textures (loaded from touch_icons.h PNGs)
static std::array<GLuint, BTN_COUNT> button_textures{};
// Track whether textures are real (>1x1) or placeholders
static std::array<bool, BTN_COUNT> button_texture_is_placeholder{};
#endif

// --- Key event injection ---

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

// --- Stick helpers ---

static void update_stick_keys(virtual_stick &stick, float dx, float dy)
{
	constexpr float threshold = 0.3f;
	bool want_left = dx < -threshold;
	bool want_right = dx > threshold;
	bool want_up = dy < -threshold;
	bool want_down = dy > threshold;

	if (want_left != stick.key_left) { stick.key_left = want_left; send_key_event(stick.sc_left, want_left); }
	if (want_right != stick.key_right) { stick.key_right = want_right; send_key_event(stick.sc_right, want_right); }
	if (want_up != stick.key_up) { stick.key_up = want_up; send_key_event(stick.sc_up, want_up); }
	if (want_down != stick.key_down) { stick.key_down = want_down; send_key_event(stick.sc_down, want_down); }
}

static void release_all_stick_keys(virtual_stick &stick)
{
	if (stick.key_left) { stick.key_left = false; send_key_event(stick.sc_left, false); }
	if (stick.key_right) { stick.key_right = false; send_key_event(stick.sc_right, false); }
	if (stick.key_up) { stick.key_up = false; send_key_event(stick.sc_up, false); }
	if (stick.key_down) { stick.key_down = false; send_key_event(stick.sc_down, false); }
}

// Compute stick deflection from current finger position
static void compute_stick_deflection(virtual_stick &stick, float fx, float fy)
{
	float ddx = (fx - stick.center_x) / (stick.radius / aspect_ratio);
	float ddy = (fy - stick.center_y) / stick.radius;
	float dist = sqrtf(ddx * ddx + ddy * ddy);
	if (dist > 1.0f) { ddx /= dist; ddy /= dist; }
	stick.dx = ddx;
	stick.dy = ddy;
	update_stick_keys(stick, ddx, ddy);
}

// Fire zone test: upper-right triangle of the right half
// In the right half (fx >= 0.5), remap fx to 0..1 within that half.
// Diagonal: 2*(fx-0.5) - fy > 0  →  upper-right fires
static bool in_fire_zone(float fx, float fy)
{
	float rx = (fx - 0.5f) * 2.0f;  // 0..1 within right half
	return (rx - fy) > 0.0f;
}

// Update fire state for the right stick based on current finger position
static void update_fire_state(virtual_stick &stick, float fx, float fy)
{
	bool want_fire = in_fire_zone(fx, fy);
	if (want_fire != stick.firing)
	{
		stick.firing = want_fire;
		send_key_event(SDL_SCANCODE_LCTRL, want_fire);
	}
}

// --- Texture loading ---

#if !DXX_USE_VULKAN
static GLuint load_png_texture(const unsigned char *data, std::size_t size, bool &is_placeholder)
{
	int w, h, channels;
	unsigned char *pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
	if (!pixels)
	{
		is_placeholder = true;
		return 0;
	}
	// Mark as placeholder if 1x1 (our fallback indicator)
	is_placeholder = (w <= 2 && h <= 2);

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(pixels);
	return tex;
}

static void init_button_textures()
{
	for (int i = 0; i < BTN_COUNT; i++)
	{
		if (static_cast<std::size_t>(i) < touch_icons::button_icon_count)
		{
			auto &icon = touch_icons::button_icons[i];
			button_textures[i] = load_png_texture(icon.data, icon.size, button_texture_is_placeholder[i]);
		}
		else
		{
			button_textures[i] = 0;
			button_texture_is_placeholder[i] = true;
		}
	}
}

static void destroy_button_textures()
{
	for (int i = 0; i < BTN_COUNT; i++)
	{
		if (button_textures[i])
		{
			glDeleteTextures(1, &button_textures[i]);
			button_textures[i] = 0;
		}
	}
}
#endif

// --- Drawing helpers (GLES 1.x) ---

#if !DXX_USE_VULKAN
static void draw_circle(float cx, float cy, float r, int segments,
                        float cr, float cg, float cb, float alpha)
{
	std::array<GLfloat, 128> verts;
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
	std::array<GLfloat, 130> verts;
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
	const GLfloat verts[] = { x, y, x + w, y, x, y + h, x + w, y + h };
	glColor4f(r, g, b, a);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_rect_outline(float x, float y, float w, float h,
                              float r, float g, float b, float a)
{
	const GLfloat verts[] = { x, y, x + w, y, x + w, y + h, x, y + h };
	glColor4f(r, g, b, a);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glDrawArrays(GL_LINE_LOOP, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_textured_rect(float x, float y, float w, float h,
                               GLuint tex, float alpha)
{
	const GLfloat verts[] = { x, y, x + w, y, x, y + h, x + w, y + h };
	const GLfloat texcoords[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex);
	glColor4f(1.0f, 1.0f, 1.0f, alpha);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
}

// Draw a floating stick (only when active)
static void draw_stick(const virtual_stick &stick, float cr, float cg, float cb)
{
	if (!stick.active)
		return;

	constexpr float alpha = 0.35f;
	const float rx = stick.radius / aspect_ratio;

	// Outer ring
	draw_circle(stick.center_x, stick.center_y, stick.radius, 32, cr, cg, cb, alpha);
	// Inner deadzone
	draw_circle(stick.center_x, stick.center_y, stick.radius * 0.3f, 16, cr, cg, cb, alpha * 0.5f);
	// Knob
	float knob_x = stick.center_x + stick.dx * rx;
	float knob_y = stick.center_y + stick.dy * stick.radius;
	draw_filled_circle(knob_x, knob_y, 0.025f, 16, cr, cg, cb, 0.5f);
	// Crosshair
	glColor4f(cr, cg, cb, alpha * 0.4f);
	const GLfloat cross_h[] = { stick.center_x - rx, stick.center_y, stick.center_x + rx, stick.center_y };
	const GLfloat cross_v[] = { stick.center_x, stick.center_y - stick.radius, stick.center_x, stick.center_y + stick.radius };
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, cross_h);
	glDrawArrays(GL_LINES, 0, 2);
	glVertexPointer(2, GL_FLOAT, 0, cross_v);
	glDrawArrays(GL_LINES, 0, 2);
	glDisableClientState(GL_VERTEX_ARRAY);
}

// Draw fire zone boundary — faint diagonal line on the right half
static void draw_fire_zone_line()
{
	// Diagonal from (0.5, 0.0) to (1.0, 1.0) — the fire zone boundary
	const GLfloat verts[] = { 0.5f, 0.0f, 1.0f, 1.0f };
	glColor4f(1.0f, 0.3f, 0.2f, 0.12f);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glDrawArrays(GL_LINES, 0, 2);
	glDisableClientState(GL_VERTEX_ARRAY);
}
#endif  // !DXX_USE_VULKAN

}  // anonymous namespace

// --- Public API ---

void touch_overlay_init(int w, int h)
{
	screen_width = w;
	screen_height = h;
	aspect_ratio = static_cast<float>(w) / static_cast<float>(h);

	// Left stick: movement (W/A/S/D) — center set on touch
	left_stick = {};
	left_stick.radius = 0.12f;
	left_stick.sc_up = SDL_SCANCODE_W;
	left_stick.sc_down = SDL_SCANCODE_S;
	left_stick.sc_left = SDL_SCANCODE_A;
	left_stick.sc_right = SDL_SCANCODE_D;

	// Right stick: look (arrows) + fire zone — center set on touch
	right_stick = {};
	right_stick.radius = 0.12f;
	right_stick.sc_up = SDL_SCANCODE_UP;
	right_stick.sc_down = SDL_SCANCODE_DOWN;
	right_stick.sc_left = SDL_SCANCODE_LEFT;
	right_stick.sc_right = SDL_SCANCODE_RIGHT;

	// Buttons along top edge (away from stick zones)
	// Top-left group
	buttons[BTN_ESC]            = {0.02f, 0.02f, 0.07f, 0.06f, false, -1};
	buttons[BTN_ACCEPT]         = {0.10f, 0.02f, 0.07f, 0.06f, false, -1};
	// Top-center
	buttons[BTN_FLARE]          = {0.36f, 0.02f, 0.08f, 0.06f, false, -1};
	buttons[BTN_BOMB]           = {0.46f, 0.02f, 0.08f, 0.06f, false, -1};
	buttons[BTN_AUTOMAP]        = {0.56f, 0.02f, 0.08f, 0.06f, false, -1};
	// Top-right
	buttons[BTN_FIRE_SECONDARY] = {0.83f, 0.02f, 0.15f, 0.06f, false, -1};
	// Upper sides (banking)
	buttons[BTN_BANK_LEFT]      = {0.02f, 0.12f, 0.07f, 0.08f, false, -1};
	buttons[BTN_BANK_RIGHT]     = {0.91f, 0.12f, 0.07f, 0.08f, false, -1};

	// Toggle button — always visible, top center-left
	toggle_button = {0.20f, 0.02f, 0.06f, 0.05f, false, -1};

	controls_visible = true;
	overlay_initialized = true;

#if !DXX_USE_VULKAN
	init_button_textures();
#endif
}

void touch_overlay_shutdown()
{
	if (!overlay_initialized)
		return;
#if !DXX_USE_VULKAN
	destroy_button_textures();
#endif
	// Release any held keys
	release_all_stick_keys(left_stick);
	release_all_stick_keys(right_stick);
	if (right_stick.firing)
	{
		right_stick.firing = false;
		send_key_event(SDL_SCANCODE_LCTRL, false);
	}
	for (int i = 0; i < BTN_COUNT; i++)
	{
		if (buttons[i].pressed)
		{
			buttons[i].pressed = false;
			send_key_event(button_scancodes[i], false);
		}
	}
	overlay_initialized = false;
}

void touch_overlay_draw()
{
	if (!overlay_enabled || !overlay_initialized)
		return;

#if DXX_USE_VULKAN
	(void)0;
#else
	// Save GL state
	GLboolean tex2d_was_enabled, depth_was_enabled, blend_was_enabled;
	GLint prev_matrix_mode;
	GLfloat prev_color[4];
	GLint prev_blend_src, prev_blend_dst;
	GLint prev_texture;
	glGetBooleanv(GL_TEXTURE_2D, &tex2d_was_enabled);
	glGetBooleanv(GL_DEPTH_TEST, &depth_was_enabled);
	glGetBooleanv(GL_BLEND, &blend_was_enabled);
	glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);
	glGetFloatv(GL_CURRENT_COLOR, prev_color);
	glGetIntegerv(GL_BLEND_SRC, &prev_blend_src);
	glGetIntegerv(GL_BLEND_DST, &prev_blend_dst);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture);

	// Set up orthographic projection
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

	// Toggle button (always visible)
	{
		float ta = controls_visible ? 0.15f : 0.35f;
		draw_filled_rect(toggle_button.x, toggle_button.y, toggle_button.w, toggle_button.h,
		                 0.8f, 0.8f, 0.8f, ta);
		draw_rect_outline(toggle_button.x, toggle_button.y, toggle_button.w, toggle_button.h,
		                  1.0f, 1.0f, 1.0f, ta + 0.15f);
	}

	if (controls_visible)
	{
		// Fire zone boundary (faint diagonal)
		draw_fire_zone_line();

		// Left stick (blue) — only visible when touching
		draw_stick(left_stick, 0.4f, 0.6f, 1.0f);

		// Right stick — red when firing, green when look-only
		if (right_stick.active && right_stick.firing)
			draw_stick(right_stick, 1.0f, 0.3f, 0.2f);
		else
			draw_stick(right_stick, 0.4f, 1.0f, 0.6f);

		// Buttons
		for (int i = 0; i < BTN_COUNT; i++)
		{
			auto &b = buttons[i];
			auto &c = button_colors[i];
			float alpha = b.pressed ? 0.45f : 0.18f;

			// Try textured rendering if we have a real (non-placeholder) icon
			if (button_textures[i] && !button_texture_is_placeholder[i])
			{
				draw_textured_rect(b.x, b.y, b.w, b.h, button_textures[i], alpha + 0.3f);
			}
			else
			{
				// Colored rectangle fallback
				draw_filled_rect(b.x, b.y, b.w, b.h, c.r, c.g, c.b, alpha);
				draw_rect_outline(b.x, b.y, b.w, b.h, c.r, c.g, c.b, alpha + 0.15f);
			}
		}
	}

	// Restore GL state
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	if (tex2d_was_enabled) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
	if (depth_was_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (blend_was_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	glBlendFunc(prev_blend_src, prev_blend_dst);
	glColor4f(prev_color[0], prev_color[1], prev_color[2], prev_color[3]);
	glBindTexture(GL_TEXTURE_2D, prev_texture);
	glMatrixMode(prev_matrix_mode);
#endif
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
		{
			fx = event.tfinger.x;
			fy = event.tfinger.y;
			fid = event.tfinger.fingerId;

			// Toggle button: always active
			if (fx >= toggle_button.x && fx <= toggle_button.x + toggle_button.w &&
			    fy >= toggle_button.y && fy <= toggle_button.y + toggle_button.h)
			{
				controls_visible = !controls_visible;
				return 1;
			}

			if (!controls_visible)
				break;

			// Check buttons first (priority over sticks)
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

			// Left half → movement stick
			if (fx < 0.5f && !left_stick.active)
			{
				left_stick.active = true;
				left_stick.finger_id = fid;
				left_stick.center_x = fx;
				left_stick.center_y = fy;
				left_stick.dx = left_stick.dy = 0.0f;
				return 1;
			}

			// Right half → look stick (+ fire zone check)
			if (fx >= 0.5f && !right_stick.active)
			{
				right_stick.active = true;
				right_stick.finger_id = fid;
				right_stick.center_x = fx;
				right_stick.center_y = fy;
				right_stick.dx = right_stick.dy = 0.0f;
				right_stick.firing = false;
				// Check if initial touch is in fire zone
				update_fire_state(right_stick, fx, fy);
				return 1;
			}
			break;
		}

		case SDL_FINGERMOTION:
		{
			fx = event.tfinger.x;
			fy = event.tfinger.y;
			fid = event.tfinger.fingerId;

			// Left stick motion
			if (left_stick.active && left_stick.finger_id == fid)
			{
				compute_stick_deflection(left_stick, fx, fy);
				return 1;
			}

			// Right stick motion (+ dynamic fire zone transitions)
			if (right_stick.active && right_stick.finger_id == fid)
			{
				compute_stick_deflection(right_stick, fx, fy);
				update_fire_state(right_stick, fx, fy);
				return 1;
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

			// Release right stick (+ stop firing)
			if (right_stick.active && right_stick.finger_id == fid)
			{
				right_stick.active = false;
				right_stick.dx = right_stick.dy = 0.0f;
				release_all_stick_keys(right_stick);
				if (right_stick.firing)
				{
					right_stick.firing = false;
					send_key_event(SDL_SCANCODE_LCTRL, false);
				}
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
