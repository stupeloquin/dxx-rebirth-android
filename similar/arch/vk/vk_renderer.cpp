/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * Vulkan renderer - draw functions replacing ogl.cpp.
 */

#include "dxxsconf.h"
#if DXX_USE_VULKAN

#include "vk_common.h"
#include "ogl_init.h"
#include "console.h"
#include "args.h"
#include "texmap.h"
#include "palette.h"
#include "rle.h"
#include "texmerge.h"
#include "config.h"
#include "piggy.h"
#include "segment.h"
#include "effects.h"
#include "weapon.h"
#include "powerup.h"
#include "laser.h"
#include "player.h"
#include "robot.h"
#include "gamefont.h"
#include "byteutil.h"
// Note: internal.h not included here - it has GL-specific macros (OGL_ENABLE etc.)
// We only need the CPAL2T palette helpers which are defined below.
#include "gauges.h"
#include "object.h"
#include "u_mem.h"

#include "common/3d/globvars.h"
#include "compiler-range_for.h"
#include "d_levelstate.h"
#include "d_zip.h"
#include "partial_range.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
using std::max;

// Fixed-point to float conversion (same as f2fl from maths.h, aliased as f2glf in ogl.cpp)
#define f2glf(x) (f2fl(x))

namespace dcx {

// Palette color helpers (from internal.h, without GL dependencies)
static constexpr const rgb_t &CPAL2T(const color_palette_index c)
{
	return gr_current_pal[static_cast<std::size_t>(c)];
}

static constexpr float CPAL2Tr(const color_palette_index c)
{
	return CPAL2T(c).r / 63.0f;
}

static constexpr float CPAL2Tg(const color_palette_index c)
{
	return CPAL2T(c).g / 63.0f;
}

static constexpr float CPAL2Tb(const color_palette_index c)
{
	return CPAL2T(c).b / 63.0f;
}

}  // namespace dcx

namespace {

// Convert fan to triangle list: {0,1,2, 0,2,3, 0,3,4, ...}
static uint32_t fan_to_list(dcx::vk_vertex *out, const dcx::vk_vertex *fan, uint32_t fan_count)
{
	if (fan_count < 3)
		return 0;
	uint32_t tri_count = fan_count - 2;
	for (uint32_t i = 0; i < tri_count; i++)
	{
		out[i * 3 + 0] = fan[0];
		out[i * 3 + 1] = fan[i + 1];
		out[i * 3 + 2] = fan[i + 2];
	}
	return tri_count * 3;
}

}  // anonymous namespace

namespace dcx {

// Append vertices to the per-frame ring buffer and issue a draw call
static void vk_emit_draw(vk_pipeline_id pipe_id, const vk_vertex *verts, uint32_t count)
{
	if (!g_vk.frame_started || count == 0)
		return;

	auto &frame = g_vk.frames[g_vk.current_frame];
	const VkDeviceSize needed = count * sizeof(vk_vertex);

	if (frame.vertex_offset + needed > VK_VERTEX_RING_SIZE)
	{
		con_puts(CON_VERBOSE, "VK: vertex ring buffer full, skipping draw");
		return;
	}

	// Copy vertices into ring buffer
	memcpy(static_cast<uint8_t *>(frame.vertex_mapped) + frame.vertex_offset, verts, needed);

	VkCommandBuffer cmd = frame.cmd;

	// Bind pipeline for current blend mode
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		g_vk.pipelines[pipe_id][g_vk.current_blend]);

	// Set viewport and scissor
	VkViewport vp{};
	vp.width = static_cast<float>(g_vk.swapchain_extent.width);
	vp.height = static_cast<float>(g_vk.swapchain_extent.height);
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &vp);

	VkRect2D scissor{};
	scissor.extent = g_vk.swapchain_extent;
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Push constants: MVP matrix + alpha ref
	vk_push_constants pc{};
	memcpy(pc.mvp, g_vk.mvp_matrix, sizeof(pc.mvp));
	pc.alpha_ref = 0.02f;
	vkCmdPushConstants(cmd, g_vk.pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pc), &pc);

	// Bind texture descriptor set
	VkDescriptorSet ds = g_vk.bound_texture ? g_vk.bound_texture : g_vk.white_texture.descriptor_set;
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		g_vk.pipeline_layout, 0, 1, &ds, 0, nullptr);

	// Bind vertex buffer
	VkDeviceSize offset = frame.vertex_offset;
	vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertex_buffer, &offset);

	vkCmdDraw(cmd, count, 1, 0, 0);

	frame.vertex_offset += needed;
}

void vk_draw_triangles(const vk_vertex *verts, uint32_t count, bool textured, bool is_3d)
{
	vk_pipeline_id pipe;
	if (textured)
		pipe = is_3d ? VK_PIPE_TEXTURED_3D : VK_PIPE_TEXTURED_2D;
	else
		pipe = is_3d ? VK_PIPE_FLAT_3D : VK_PIPE_FLAT_2D;
	vk_emit_draw(pipe, verts, count);
}

void vk_draw_triangle_fan(const vk_vertex *verts, uint32_t count, bool textured, bool is_3d)
{
	if (count < 3)
		return;
	// Convert fan to triangle list
	const uint32_t max_tris = (count - 2) * 3;
	std::vector<vk_vertex> list_verts(max_tris);
	uint32_t list_count = fan_to_list(list_verts.data(), verts, count);
	vk_draw_triangles(list_verts.data(), list_count, textured, is_3d);
}

void vk_draw_lines(const vk_vertex *verts, uint32_t count, bool is_3d)
{
	vk_pipeline_id pipe = is_3d ? VK_PIPE_LINE_3D : VK_PIPE_LINE_2D;
	vk_emit_draw(pipe, verts, count);
}

// ============================================================
// Texture management stubs (interface with DXX bitmap system)
// ============================================================

void ogl_init_texture(ogl_texture &t, int w, int h, int flags)
{
	t = {};
	t.w = w;
	t.h = h;
	t.handle = 0;
	t.wrapstate = -1;
}

void ogl_init_texture_list_internal()
{
}

void ogl_smash_texture_list_internal()
{
}

void ogl_vivify_texture_list_internal()
{
}

void ogl_loadbmtexture_f(grs_bitmap &rbm, const opengl_texture_filter texfilt, bool texanis, bool edgepad)
{
	// For Vulkan, texture loading is deferred to the point of use
	// The actual Vulkan texture is created on first bind
}

void ogl_freebmtexture(grs_bitmap &bm)
{
	// Free the Vulkan texture if any
	if (bm.gltexture)
	{
		auto *vkt = reinterpret_cast<vk_texture *>(bm.gltexture->handle);
		if (vkt)
		{
			vk_destroy_texture(vkt);
			bm.gltexture->handle = 0;
		}
	}
}

// ============================================================
// Public drawing functions matching OGL interface
// ============================================================

void g3_draw_line(const g3_draw_line_context &context, const g3_draw_line_point &p0, const g3_draw_line_point &p1)
{
	std::array<vk_vertex, 2> verts{};
	verts[0].x = f2glf(p0.p3_vec.x);
	verts[0].y = f2glf(p0.p3_vec.y);
	verts[0].z = -f2glf(p0.p3_vec.z);
	verts[0].r = context.color_array[0];
	verts[0].g = context.color_array[1];
	verts[0].b = context.color_array[2];
	verts[0].a = context.color_array[3];

	verts[1].x = f2glf(p1.p3_vec.x);
	verts[1].y = f2glf(p1.p3_vec.y);
	verts[1].z = -f2glf(p1.p3_vec.z);
	verts[1].r = context.color_array[4];
	verts[1].g = context.color_array[5];
	verts[1].b = context.color_array[6];
	verts[1].a = context.color_array[7];

	vk_draw_lines(verts.data(), 2, true);
}

void _g3_draw_poly(grs_canvas &canvas, const std::span<g3_draw_tmap_point *const> pointlist, const uint8_t palette_color_index)
{
	if (pointlist.size() < 3 || pointlist.size() > MAX_POINTS_PER_POLY)
		return;

	auto &&rgb{PAL2T(palette_color_index)};
	const float cr = rgb.r / 63.0f;
	const float cg = rgb.g / 63.0f;
	const float cb = rgb.b / 63.0f;
	const float ca = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0f
		: 1.0f - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0f);

	std::array<vk_vertex, MAX_POINTS_PER_POLY> fan_verts;
	for (size_t i = 0; i < pointlist.size(); i++)
	{
		auto &pv = pointlist[i]->p3_vec;
		fan_verts[i] = {f2glf(pv.x), f2glf(pv.y), -f2glf(pv.z),
		                cr, cg, cb, ca, 0.0f, 0.0f};
	}

	vk_draw_triangle_fan(fan_verts.data(), static_cast<uint32_t>(pointlist.size()), false, true);
}

void _g3_draw_tmap(grs_canvas &canvas, const std::span<g3_draw_tmap_point *const> pointlist, const g3s_uvl *const uvl_list, const g3s_lrgb *const light_rgb, grs_bitmap &bm, const tmap_drawer_type tmap_drawer_ptr)
{
	const auto nv = pointlist.size();
	if (nv < 3)
		return;

	const float color_alpha = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0f
		: (tmap_drawer_ptr == draw_tmap_flat
			? (1.0f - static_cast<float>(canvas.cv_fade_level) / static_cast<float>(NUM_LIGHTING_LEVELS))
			: (1.0f - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0f)));

	bool textured = (tmap_drawer_ptr == draw_tmap);

	// TODO: Bind Vulkan texture from bitmap when textured
	if (!textured)
		vk_bind_texture(nullptr);

	std::array<vk_vertex, MAX_POINTS_PER_POLY> fan_verts;
	for (size_t i = 0; i < nv; i++)
	{
		auto &pv = pointlist[i]->p3_vec;
		vk_vertex &v = fan_verts[i];
		v.x = f2glf(pv.x);
		v.y = f2glf(pv.y);
		v.z = -f2glf(pv.z);
		v.a = color_alpha;

		if (tmap_drawer_ptr == draw_tmap_flat)
		{
			v.r = v.g = v.b = 0;
			v.u = v.v = 0;
		}
		else
		{
			if (bm.get_flag_mask(BM_FLAG_NO_LIGHTING))
				v.r = v.g = v.b = 1.0f;
			else
			{
				v.r = f2glf(light_rgb[i].r);
				v.g = f2glf(light_rgb[i].g);
				v.b = f2glf(light_rgb[i].b);
			}
			v.u = f2glf(uvl_list[i].u);
			v.v = f2glf(uvl_list[i].v);
		}
	}

	vk_draw_triangle_fan(fan_verts.data(), static_cast<uint32_t>(nv), textured, true);
}

void _g3_draw_tmap_2(grs_canvas &canvas, const std::span<g3_draw_tmap_point *const> pointlist, const std::span<const g3s_uvl, 4> uvl_list, const std::span<const g3s_lrgb, 4> light_rgb, grs_bitmap &bmbot, grs_bitmap &bm, const texture2_rotation_low orient, const tmap_drawer_type tmap_drawer_ptr)
{
	// Draw bottom texture first
	_g3_draw_tmap(canvas, pointlist, uvl_list.data(), light_rgb.data(), bmbot, tmap_drawer_ptr);

	// Then draw overlay texture with rotation
	const auto nv = pointlist.size();
	const float alpha = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0f
		: (1.0f - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0f));

	// TODO: Bind overlay Vulkan texture

	std::array<vk_vertex, MAX_POINTS_PER_POLY> fan_verts;
	for (size_t i = 0; i < nv; i++)
	{
		auto &pv = pointlist[i]->p3_vec;
		vk_vertex &v = fan_verts[i];
		v.x = f2glf(pv.x);
		v.y = f2glf(pv.y);
		v.z = -f2glf(pv.z);
		v.a = alpha;

		if (bm.get_flag_mask(BM_FLAG_NO_LIGHTING))
			v.r = v.g = v.b = 1.0f;
		else
		{
			v.r = f2glf(light_rgb[i].r);
			v.g = f2glf(light_rgb[i].g);
			v.b = f2glf(light_rgb[i].b);
		}

		const float uf = f2glf(uvl_list[i].u);
		const float vf = f2glf(uvl_list[i].v);
		switch (orient)
		{
			case texture2_rotation_low::_1:
				v.u = 1.0f - vf; v.v = uf; break;
			case texture2_rotation_low::_2:
				v.u = 1.0f - uf; v.v = 1.0f - vf; break;
			case texture2_rotation_low::_3:
				v.u = vf; v.v = 1.0f - uf; break;
			default:
				v.u = uf; v.v = vf; break;
		}
	}

	vk_draw_triangle_fan(fan_verts.data(), static_cast<uint32_t>(nv), true, true);
}

void g3_draw_bitmap(grs_canvas &canvas, const vms_vector &pos, const fix iwidth, const fix iheight, grs_bitmap &bm)
{
	const auto width = fixmul(iwidth, Matrix_scale.x);
	const auto height = fixmul(iheight, Matrix_scale.y);

	const auto &&rpv{vm_vec_build_rotated(vm_vec_build_sub(pos, View_position), View_matrix)};
	const float alpha = (canvas.cv_fade_level >= GR_FADE_OFF) ? 1.0f
		: (1.0f - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0f));

	// TODO: bind Vulkan texture from bitmap
	const float bmglu = 1.0f; // bm.gltexture ? bm.gltexture->u : 1.0f
	const float bmglv = 1.0f;

	const float vz = -f2glf(rpv.z);
	std::array<vk_vertex, 4> fan_verts;

	auto make_vert = [&](int idx, fix dx, fix dy, float tu, float tv) {
		auto pv = rpv;
		pv.x += dx;
		pv.y += dy;
		fan_verts[idx] = {f2glf(pv.x), f2glf(pv.y), vz,
		                  1.0f, 1.0f, 1.0f, alpha, tu, tv};
	};

	make_vert(0, -width,  height, 0.0f, 0.0f);
	make_vert(1,  width,  height, bmglu, 0.0f);
	make_vert(2,  width, -height, bmglu, bmglv);
	make_vert(3, -width, -height, 0.0f, bmglv);

	vk_draw_triangle_fan(fan_verts.data(), 4, true, true);
}

void ogl_draw_vertex_reticle(grs_canvas &canvas, int cross, int primary, int secondary, int color, int alpha, int size_offs)
{
	// Simplified reticle - draw crosshair as lines
	auto &&rgb{PAL2T(color)};
	const float cr = rgb.r / 63.0f;
	const float cg = rgb.g / 63.0f;
	const float cb = rgb.b / 63.0f;
	const float ca = 1.0f - static_cast<float>(alpha) / static_cast<float>(GR_FADE_LEVELS);

	const float cx = (canvas.cv_bitmap.bm_w / 2 + canvas.cv_bitmap.bm_x) / static_cast<float>(g_vk.screen_width);
	const float cy = 1.0f - (canvas.cv_bitmap.bm_h / 2 + canvas.cv_bitmap.bm_y) / static_cast<float>(g_vk.screen_height);
	const float sz = 0.02f;

	std::array<vk_vertex, 4> lines;
	// Horizontal line
	lines[0] = {cx - sz, cy, 0, cr, cg, cb, ca, 0, 0};
	lines[1] = {cx + sz, cy, 0, cr, cg, cb, ca, 0, 0};
	// Vertical line
	lines[2] = {cx, cy - sz, 0, cr, cg, cb, ca, 0, 0};
	lines[3] = {cx, cy + sz, 0, cr, cg, cb, ca, 0, 0};

	vk_draw_lines(lines.data(), 4, false);
}

bool ogl_ubitblt_i(unsigned dw, unsigned dh, unsigned dx, unsigned dy, unsigned sw, unsigned sh, unsigned sx, unsigned sy, const grs_bitmap &src, grs_bitmap &dest, const opengl_texture_filter texfilt)
{
	// TODO: implement proper texture blit for Vulkan
	// For now, create a temporary texture and draw a quad
	const float xo = dx / static_cast<float>(g_vk.screen_width);
	const float xs = dw / static_cast<float>(g_vk.screen_width);
	const float yo = 1.0f - dy / static_cast<float>(g_vk.screen_height);
	const float ys = dh / static_cast<float>(g_vk.screen_height);

	std::array<vk_vertex, 4> fan;
	fan[0] = {xo, yo, 0, 1, 1, 1, 1, 0, 0};
	fan[1] = {xo + xs, yo, 0, 1, 1, 1, 1, 1, 0};
	fan[2] = {xo + xs, yo - ys, 0, 1, 1, 1, 1, 1, 1};
	fan[3] = {xo, yo - ys, 0, 1, 1, 1, 1, 0, 1};

	vk_draw_triangle_fan(fan.data(), 4, true, false);
	return false;
}

bool ogl_ubitblt(unsigned w, unsigned h, unsigned dx, unsigned dy, unsigned sx, unsigned sy, const grs_bitmap &src, grs_bitmap &dest)
{
	return ogl_ubitblt_i(w, h, dx, dy, w, h, sx, sy, src, dest, opengl_texture_filter::classic);
}

bool ogl_ubitmapm_cs(grs_canvas &canvas, int x, int y, int dw, int dh, grs_bitmap &bm, int c)
{
	ogl_colors colors;
	return ogl_ubitmapm_cs(canvas, x, y, dw, dh, bm,
		(c == opengl_bitmap_use_dst_canvas) ? colors.init(c) : ogl_colors::white);
}

bool ogl_ubitmapm_cs(grs_canvas &canvas, const int entry_x, const int entry_y, const int entry_dw, const int entry_dh, grs_bitmap &bm, const ogl_colors::array_type &color_array)
{
	return ogl_ubitmapm_cs(canvas, entry_x, entry_y, entry_dw, entry_dh, bm, color_array, false);
}

bool ogl_ubitmapm_cs(grs_canvas &canvas, int x0, int y0, int dw, int dh, grs_bitmap &bm, const ogl_colors::array_type &color_array, bool fill)
{
	// TODO: proper bitmap rendering with Vulkan texture
	const int x = x0 + canvas.cv_bitmap.bm_x;
	const int y = y0 + canvas.cv_bitmap.bm_y;
	const float xo = x / static_cast<float>(g_vk.screen_width);
	const float yo = 1.0f - y / static_cast<float>(g_vk.screen_height);
	const float xs = (dw > 0 ? dw : bm.bm_w) / static_cast<float>(g_vk.screen_width);
	const float ys = (dh > 0 ? dh : bm.bm_h) / static_cast<float>(g_vk.screen_height);

	std::array<vk_vertex, 4> fan;
	for (int i = 0; i < 4; i++)
	{
		fan[i].r = color_array[i * 4 + 0];
		fan[i].g = color_array[i * 4 + 1];
		fan[i].b = color_array[i * 4 + 2];
		fan[i].a = color_array[i * 4 + 3];
	}
	fan[0].x = xo;      fan[0].y = yo;      fan[0].z = 0; fan[0].u = 0; fan[0].v = 0;
	fan[1].x = xo + xs; fan[1].y = yo;      fan[1].z = 0; fan[1].u = 1; fan[1].v = 0;
	fan[2].x = xo + xs; fan[2].y = yo - ys; fan[2].z = 0; fan[2].u = 1; fan[2].v = 1;
	fan[3].x = xo;      fan[3].y = yo - ys; fan[3].z = 0; fan[3].u = 0; fan[3].v = 1;

	vk_draw_triangle_fan(fan.data(), 4, true, false);
	return false;
}

bool ogl_ubitblt_cs(grs_canvas &canvas, int dw, int dh, int dx, int dy, int sx, int sy)
{
	// Stub for screen copy
	return false;
}

void ogl_upixelc(const grs_bitmap &cv_bitmap, unsigned x, unsigned y, const color_palette_index c)
{
	const float px = (x + cv_bitmap.bm_x) / static_cast<float>(g_vk.screen_width);
	const float py = 1.0f - (y + cv_bitmap.bm_y) / static_cast<float>(g_vk.screen_height);

	auto &&rgb{PAL2T(c)};
	const float cr = CPAL2Tr(c);
	const float cg = CPAL2Tg(c);
	const float cb = CPAL2Tb(c);

	// Draw as a small filled rectangle (1 pixel)
	const float pw = 1.0f / g_vk.screen_width;
	const float ph = 1.0f / g_vk.screen_height;

	std::array<vk_vertex, 4> fan;
	fan[0] = {px, py, 0, cr, cg, cb, 1, 0, 0};
	fan[1] = {px + pw, py, 0, cr, cg, cb, 1, 0, 0};
	fan[2] = {px + pw, py - ph, 0, cr, cg, cb, 1, 0, 0};
	fan[3] = {px, py - ph, 0, cr, cg, cb, 1, 0, 0};

	vk_draw_triangle_fan(fan.data(), 4, false, false);
}

color_palette_index ogl_ugpixel(const grs_bitmap &bitmap, unsigned x, unsigned y)
{
	// Reading pixels back from Vulkan is expensive; return black
	return color_palette_index{0};
}

void ogl_urect(grs_canvas &canvas, const int left, const int top, const int right, const int bot, const color_palette_index c)
{
	const float xo = (left + canvas.cv_bitmap.bm_x) / static_cast<float>(g_vk.screen_width);
	const float xf = (right + 1 + canvas.cv_bitmap.bm_x) / static_cast<float>(g_vk.screen_width);
	const float yo = 1.0f - (top + canvas.cv_bitmap.bm_y) / static_cast<float>(g_vk.screen_height);
	const float yf = 1.0f - (bot + 1 + canvas.cv_bitmap.bm_y) / static_cast<float>(g_vk.screen_height);

	const float cr = CPAL2Tr(c);
	const float cg = CPAL2Tg(c);
	const float cb = CPAL2Tb(c);
	const float ca = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0f
		: 1.0f - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0f);

	std::array<vk_vertex, 4> fan;
	fan[0] = {xo, yo, 0, cr, cg, cb, ca, 0, 0};
	fan[1] = {xo, yf, 0, cr, cg, cb, ca, 0, 0};
	fan[2] = {xf, yf, 0, cr, cg, cb, ca, 0, 0};
	fan[3] = {xf, yo, 0, cr, cg, cb, ca, 0, 0};

	vk_draw_triangle_fan(fan.data(), 4, false, false);
}

void ogl_ulinec(grs_canvas &canvas, const int left, const int top, const int right, const int bot, const int c)
{
	const float ca = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0f
		: 1.0f - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0f);

	const float cr = CPAL2Tr(c);
	const float cg = CPAL2Tg(c);
	const float cb = CPAL2Tb(c);

	const float xo = (left + canvas.cv_bitmap.bm_x) / static_cast<float>(g_vk.screen_width);
	const float xf = (right + canvas.cv_bitmap.bm_x) / static_cast<float>(g_vk.screen_width);
	const float yo = 1.0f - (top + canvas.cv_bitmap.bm_y + 0.5f) / static_cast<float>(g_vk.screen_height);
	const float yf = 1.0f - (bot + canvas.cv_bitmap.bm_y + 0.5f) / static_cast<float>(g_vk.screen_height);

	std::array<vk_vertex, 2> verts;
	verts[0] = {xo, yo, 0, cr, cg, cb, ca, 0, 0};
	verts[1] = {xf, yf, 0, cr, cg, cb, ca, 0, 0};

	vk_draw_lines(verts.data(), 2, false);
}

void ogl_toggle_depth_test(int enable)
{
	// Depth test is baked into pipelines; 3D pipelines have it on, 2D off
	(void)enable;
}

void ogl_set_blending(const gr_blend cv_blend_func)
{
	switch (cv_blend_func)
	{
		case gr_blend::additive_a:
			g_vk.current_blend = VK_BLEND_ADDITIVE_A;
			break;
		case gr_blend::additive_c:
			g_vk.current_blend = VK_BLEND_ADDITIVE_C;
			break;
		case gr_blend::normal:
		default:
			g_vk.current_blend = VK_BLEND_NORMAL;
			break;
	}
}

void ogl_start_frame(grs_canvas &canvas)
{
	g_vk.is_3d_mode = true;

	// Set up perspective projection (matching GL: fov=90, aspect=1, near=0.1, far=5000)
	vk_mat4_perspective(g_vk.projection_matrix, 90.0f, 1.0f, 0.1f, 5000.0f);
	vk_mat4_identity(g_vk.modelview_matrix);
	vk_update_mvp();

	g_vk.current_blend = VK_BLEND_NORMAL;
}

void ogl_end_frame()
{
	g_vk.is_3d_mode = false;

	// Switch to orthographic projection for 2D
	vk_mat4_ortho(g_vk.projection_matrix, 0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
	vk_mat4_identity(g_vk.modelview_matrix);
	vk_update_mvp();
}

void ogl_do_palfx()
{
	// Palette effects - draw fullscreen tinted quad
	// Simplified stub for now
}

void ogl_init_shared_palette()
{
}

void ogl_init_pixel_buffers(unsigned w, unsigned h)
{
}

void ogl_close_pixel_buffers()
{
}

void ogl_set_screen_mode()
{
}

// -------------------------------------------------------
// Stubs for symbols that were in ogl.cpp but are referenced
// by other game code through ogl_init.h declarations.
// -------------------------------------------------------

static std::array<ogl_texture, 20000> ogl_texture_list;
static unsigned ogl_texture_list_cur;

ogl_texture* ogl_get_free_texture()
{
	for (unsigned i = ogl_texture_list.size(); i--;)
	{
		if (ogl_texture_list[ogl_texture_list_cur].handle <= 0 &&
		    ogl_texture_list[ogl_texture_list_cur].w == 0)
			return &ogl_texture_list[ogl_texture_list_cur];
		if (++ogl_texture_list_cur >= ogl_texture_list.size())
			ogl_texture_list_cur = 0;
	}
	throw std::runtime_error("Vulkan: texture list full");
}

const ogl_colors::array_type ogl_colors::white = {{
	1.0, 1.0, 1.0, 1.0,
	1.0, 1.0, 1.0, 1.0,
	1.0, 1.0, 1.0, 1.0,
	1.0, 1.0, 1.0, 1.0,
}};

const ogl_colors::array_type &ogl_colors::init_maybe_white(const int c)
{
	return c == -1 ? white : init_palette(c);
}

const ogl_colors::array_type &ogl_colors::init_palette(const unsigned c)
{
	const auto &rgb = gr_current_pal[c];
	const float r = rgb.r / 63.0f, g = rgb.g / 63.0f, b = rgb.b / 63.0f;
	a = {{
		r, g, b, 1.0f,
		r, g, b, 1.0f,
		r, g, b, 1.0f,
		r, g, b, 1.0f,
	}};
	return a;
}

int gr_ucircle(grs_canvas &, const fix, const fix, const fix, const uint8_t)
{
	// TODO: Vulkan circle rendering
	return 0;
}

int gr_disk(grs_canvas &, const fix, const fix, const fix, const uint8_t)
{
	// TODO: Vulkan disk rendering
	return 0;
}

void g3_draw_sphere(grs_canvas &, const g3_rotated_point &, fix, const uint8_t)
{
	// TODO: Vulkan sphere rendering
}

}  // namespace dcx

namespace dsx {

void ogl_cache_level_textures()
{
	// Pre-cache textures for the level - stub for Vulkan
}

}  // namespace dsx

#endif  // DXX_USE_VULKAN
