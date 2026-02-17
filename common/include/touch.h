/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#ifdef __ANDROID__

#include <SDL.h>

namespace dcx {

void touch_overlay_init(int screen_w, int screen_h);
void touch_overlay_shutdown();
void touch_overlay_draw();
int touch_overlay_handle_event(const SDL_Event &event);
void touch_overlay_set_enabled(bool enabled);
bool touch_overlay_is_enabled();

}

#endif
