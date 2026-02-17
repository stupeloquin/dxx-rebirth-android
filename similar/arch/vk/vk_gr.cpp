/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * Vulkan renderer - gr_init, gr_close, gr_set_mode, gr_flip.
 */

#include "dxxsconf.h"
#if DXX_USE_VULKAN

#include "vk_common.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <SDL_log.h>
#include "game.h"
#include "gr.h"
#include "gamefont.h"
#include "palette.h"
#include "u_mem.h"
#include "dxxerror.h"
#include "inferno.h"
#include "strutil.h"
#include "args.h"
#include "console.h"
#include "config.h"
#include "vers_id.h"

using std::min;
using std::max;

namespace dcx {

static int gr_installed;
int linedotscale{1};

SDL_Window *g_pRebirthSDLMainWindow;
static int g_iRebirthWindowX, g_iRebirthWindowY;

void ogl_swap_buffers_internal(void)
{
	// In Vulkan mode, presentation is handled by vk_present()
}

}

namespace dsx {

static void gr_set_mode_from_window_size(SDL_Window *const SDLWindow)
{
	assert(SDLWindow);
	int w, h;
	SDL_Vulkan_GetDrawableSize(SDLWindow, &w, &h);
	gr_set_mode(screen_mode(w, h));
}

void gr_set_mode_from_window_size()
{
	gr_set_mode_from_window_size(g_pRebirthSDLMainWindow);
}

}

namespace dcx {

int gr_check_fullscreen(void)
{
	return !!(SDL_GetWindowFlags(g_pRebirthSDLMainWindow) & SDL_WINDOW_FULLSCREEN);
}

void gr_toggle_fullscreen()
{
	const auto SDLWindow{g_pRebirthSDLMainWindow};
	const auto is_fullscreen_before_change = SDL_GetWindowFlags(SDLWindow) & SDL_WINDOW_FULLSCREEN;
	CGameCfg.WindowMode = !!is_fullscreen_before_change;
	if (!is_fullscreen_before_change)
		SDL_GetWindowPosition(SDLWindow, &g_iRebirthWindowX, &g_iRebirthWindowY);
	SDL_SetWindowFullscreen(SDLWindow, is_fullscreen_before_change ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (is_fullscreen_before_change)
	{
		const auto mode{Game_screen_mode};
		SDL_SetWindowPosition(SDLWindow, g_iRebirthWindowX, g_iRebirthWindowY);
		SDL_SetWindowSize(SDLWindow, SM_W(mode), SM_H(mode));
	}
	gr_set_mode_from_window_size(SDLWindow);
}

}

namespace dsx {

int gr_set_mode(screen_mode mode)
{
	unsigned char *gr_bm_data;
	const uint_fast32_t w = SM_W(mode), h = SM_H(mode);

	gr_bm_data = grd_curscreen->sc_canvas.cv_bitmap.get_bitmap_data();
	auto gr_new_bm_data = reinterpret_cast<uint8_t *>(d_realloc(gr_bm_data, w * h));
	if (!gr_new_bm_data)
		return 0;
	*grd_curscreen = {};
	grd_curscreen->set_screen_width_height(w, h);
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->get_screen_width() * CGameCfg.AspectX, grd_curscreen->get_screen_height() * CGameCfg.AspectY);
	gr_init_canvas(grd_curscreen->sc_canvas, gr_new_bm_data, bm_mode::ogl, w, h);

	const auto SDLWindow{g_pRebirthSDLMainWindow};
	if (!(SDL_GetWindowFlags(SDLWindow) & SDL_WINDOW_FULLSCREEN))
		SDL_SetWindowSize(SDLWindow, w, h);

	const auto w640 = w / 640;
	const auto h480 = h / 480;
	const auto min_wh = std::min(w640, h480);
	linedotscale = min_wh < 1 ? 1 : min_wh;

	// Initialize or recreate Vulkan swapchain
	if (g_vk.initialized)
		vk_recreate_swapchain(w, h);

	gamefont_choose_game_font(w, h);
	gr_remap_color_fonts();

	return 0;
}

void gr_set_attributes(void)
{
	// No GL attributes needed for Vulkan
}

int gr_init()
{
	if (gr_installed == 1)
		return -1;

	gr_set_attributes();

	assert(!g_pRebirthSDLMainWindow);
	unsigned sdl_window_flags = SDL_WINDOW_VULKAN;
	if (CGameArg.SysNoBorders)
		sdl_window_flags |= SDL_WINDOW_BORDERLESS;
	if (!CGameCfg.WindowMode && !CGameArg.SysWindow)
		sdl_window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	const auto mode{Game_screen_mode};
	const auto SDLWindow = SDL_CreateWindow(DESCENT_VERSION,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		SM_W(mode), SM_H(mode), sdl_window_flags);
	if (!SDLWindow)
	{
		con_printf(CON_URGENT, "VK: SDL_CreateWindow failed: %s", SDL_GetError());
		return -1;
	}
	SDL_GetWindowPosition(SDLWindow, &g_iRebirthWindowX, &g_iRebirthWindowY);
	g_pRebirthSDLMainWindow = SDLWindow;

	if (const auto window_icon = SDL_LoadBMP(DXX_SDL_WINDOW_ICON_BITMAP))
		SDL_SetWindowIcon(SDLWindow, window_icon);

	// Initialize Vulkan
	int w, h;
	SDL_Vulkan_GetDrawableSize(SDLWindow, &w, &h);
	SDL_Log("VK: gr_init - initializing Vulkan renderer %dx%d", w, h);
	if (!vk_init(SDLWindow, w, h))
	{
		con_puts(CON_URGENT, "VK: Failed to initialize Vulkan renderer");
		SDL_Log("VK: Failed to initialize Vulkan renderer");
		return -1;
	}
	SDL_Log("VK: gr_init - Vulkan renderer initialized successfully");

	grd_curscreen = std::make_unique<grs_screen>();
	*grd_curscreen = {};
	grd_curscreen->sc_canvas.cv_bitmap.bm_data = NULL;

	grd_curscreen->sc_canvas.cv_fade_level = GR_FADE_OFF;
	grd_curscreen->sc_canvas.cv_font = NULL;
	grd_curscreen->sc_canvas.cv_font_fg_color = 0;
	grd_curscreen->sc_canvas.cv_font_bg_color = 0;
	gr_set_current_canvas(grd_curscreen->sc_canvas);

	gr_installed = 1;
	return 0;
}

void gr_close()
{
	vk_shutdown();

	if (grd_curscreen)
	{
		if (grd_curscreen->sc_canvas.cv_bitmap.bm_mdata)
			d_free(grd_curscreen->sc_canvas.cv_bitmap.bm_mdata);
		grd_curcanv = nullptr;
		grd_curscreen.reset();
	}
}

}

namespace dcx {

uint_fast32_t gr_list_modes(std::array<screen_mode, 50> &gsmodes)
{
	// SDL2 Vulkan window can be any size
	return 0;
}

void gr_palette_step_up(int r, int g, int b)
{
	// Palette step-up handled in ogl_do_palfx via Vulkan
}

void gr_palette_load(palette_array_t &pal)
{
	copy_bound_palette(gr_current_pal, pal);
	gr_palette_step_up(0, 0, 0);
	reset_computed_colors();
}

static unsigned s_frame_count = 0;

void gr_flip(void)
{
	if (!g_vk.frame_started && !vk_begin_frame())
		return;

	vk_end_frame();
	vk_present();

	s_frame_count++;
	if (s_frame_count <= 5 || s_frame_count % 60 == 0)
		SDL_Log("VK: gr_flip frame %u", s_frame_count);

	// Begin next frame immediately
	vk_begin_frame();
}

#if DXX_USE_SCREENSHOT_FORMAT_LEGACY
void write_bmp(PHYSFS_File *const TGAFile, const unsigned w, const unsigned h)
{
	// Screenshot not yet implemented for Vulkan
}
#endif

}

#endif  // DXX_USE_VULKAN
