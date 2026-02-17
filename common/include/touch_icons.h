/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Embedded PNG byte arrays for touch overlay button icons.
 *
 * Currently contains placeholder 1x1 white pixels so the texture loader
 * returns a valid but tiny texture.  The draw code detects these and
 * falls back to coloured rectangles.
 *
 * To add real icons:
 *   1. Create 64x64 or 128x128 RGBA PNGs
 *   2. Convert:  xxd -i icon.png > icon_data.h
 *   3. Paste the array here, update the corresponding entry
 */

#pragma once

#include <cstddef>

namespace dcx {
namespace touch_icons {

// Minimal valid 1x1 white RGBA PNG (68 bytes).
// Generated with correct CRC checksums via Python zlib.
// Using this as a placeholder for all icons.
static constexpr unsigned char placeholder_1x1_png[] = {
	0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
	0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
	0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
	0x0b,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0x0f,0x04,0x00,
	0x09,0xfb,0x03,0xfd,0xfb,0x5e,0x6b,0x2b,0x00,0x00,0x00,0x00,
	0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82,
};
static constexpr std::size_t placeholder_1x1_png_size = sizeof(placeholder_1x1_png);

// Icon data arrays — replace these with real icon PNGs when available.
// Each entry is {data pointer, size}.
struct icon_entry {
	const unsigned char *data;
	std::size_t size;
};

// Indices match the BTN_* enum in touch.cpp
static constexpr icon_entry button_icons[] = {
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_FIRE_SECONDARY
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_FLARE
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_BOMB
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_BANK_LEFT
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_BANK_RIGHT
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_AUTOMAP
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_ESC
	{ placeholder_1x1_png, placeholder_1x1_png_size }, // BTN_ACCEPT
};

static constexpr std::size_t button_icon_count = sizeof(button_icons) / sizeof(button_icons[0]);

}  // namespace touch_icons
}  // namespace dcx
