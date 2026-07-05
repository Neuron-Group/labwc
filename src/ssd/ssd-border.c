// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "buffer.h"
#include "common/macros.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "ssd-shape.h"
#include "theme.h"
#include "view.h"

struct ssd_frame_band_geometry {
	int outer_width;
	int inner_width;
};

static int
get_titlebar_button_side_len(void)
{
	return MAX(rc.theme->titlebar_height, 0);
}

static int
get_titlebar_shoulder_height(struct ssd *ssd, int button_side_len)
{
	struct theme *theme = rc.theme;

	(void)ssd;
	return button_side_len + theme->border_width;
}

static int
get_titlebar_shoulder_width(struct ssd *ssd, int button_side_len)
{
	(void)ssd;
	return button_side_len + rc.theme->border_width;
}

static void
get_shoulder_strip_geometry(struct ssd *ssd, int button_side_len,
		int *x, int *width)
{
	int inset = MAX(rc.theme->window_titlebar_padding_width, 0);
	int view_width = ssd->view->current.width;

	if (x) {
		*x = rc.theme->border_width + inset + button_side_len;
	}
	if (width) {
		*width = MAX(view_width - 2 * inset - 2 * button_side_len, 0);
	}
}

static void
update_shoulder_shape(struct wlr_scene_buffer *scene_buffer,
		const struct ssd_shoulder_shape_spec *spec)
{
	struct lab_data_buffer *buffer;
	struct ssd_shape_placement placement;

	if (!scene_buffer || !spec) {
		return;
	}

	placement = ssd_shoulder_shape_get_placement(spec);
	buffer = ssd_shoulder_shape_render(spec, 1.0f);
	if (!buffer) {
		wlr_scene_buffer_set_buffer(scene_buffer, NULL);
		return;
	}

	wlr_scene_node_set_position(&scene_buffer->node,
		placement.x, placement.y);
	wlr_scene_buffer_set_buffer(scene_buffer, &buffer->base);
	wlr_scene_buffer_set_dest_size(scene_buffer,
		buffer->logical_width, buffer->logical_height);
	wlr_buffer_drop(&buffer->base);
}

static bool
use_custom_border_frame(struct ssd *ssd)
{
	/*
	 * Keep the NsCDE frame active for tiled/squared windows as well.
	 * Squared state still changes top/side ownership, but should not
	 * fall back to the older flat border bands.
	 */
	return ssd->titlebar.height > 0;
}

static bool
border_owns_top_edge(struct ssd *ssd)
{
	return ssd->titlebar.height <= 0 || ssd->state.was_squared;
}

static bool
border_owns_title_zone_sides(struct ssd *ssd)
{
	return ssd->titlebar.height <= 0 || ssd->state.was_squared;
}

static struct ssd_frame_band_geometry
get_frame_band_geometry(struct theme *theme, enum ssd_active_state active)
{
	int outer_width = MAX(theme->window[active].frame_outer_width, 0);
	outer_width = MIN(outer_width, theme->border_width);

	int inner_width = MAX(theme->window[active].frame_inner_width, 0);
	inner_width = MIN(inner_width, theme->border_width - outer_width);

	return (struct ssd_frame_band_geometry) {
		.outer_width = outer_width,
		.inner_width = inner_width,
	};
}

static int
get_outer_top_y(struct ssd *ssd, bool full_top_border, int outer_width)
{
	if (full_top_border) {
		return -(ssd->titlebar.height + rc.theme->border_width);
	}
	return -(ssd->titlebar.height + outer_width);
}

static int
get_inner_top_y(struct ssd *ssd, bool full_top_border, int outer_width)
{
	if (full_top_border) {
		return -(ssd->titlebar.height + rc.theme->border_width) + outer_width;
	}
	return -ssd->titlebar.height;
}

void
ssd_border_create(struct ssd *ssd)
{
	assert(ssd);
	assert(!ssd->border.tree);

	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int button_side_len = get_titlebar_button_side_len();
	int custom_top_x = 0;
	int custom_top_width = 0;
	bool full_top_border = border_owns_top_edge(ssd);
	bool full_side_border = border_owns_title_zone_sides(ssd);
	int side_height = full_side_border ? height + ssd->titlebar.height : height;
	int side_y = full_side_border ? -ssd->titlebar.height : 0;
	get_shoulder_strip_geometry(ssd, button_side_len,
		&custom_top_x, &custom_top_width);

	ssd->border.tree = lab_wlr_scene_tree_create(ssd->tree);
	wlr_scene_node_set_position(&ssd->border.tree->node, -theme->border_width, 0);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->border.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		struct ssd_frame_band_geometry frame =
			get_frame_band_geometry(theme, active);
		wlr_scene_node_set_enabled(&parent->node, active);
		float *fill = theme->window[active].title_bg.color;
		float *hl = theme->window[active].titlebar_bevel_highlight_color;
		float *sh = theme->window[active].titlebar_bevel_shadow_color;

		subtree->top_left_shoulder_shape =
			lab_wlr_scene_buffer_create(parent, NULL);
		subtree->top_right_shoulder_shape =
			lab_wlr_scene_buffer_create(parent, NULL);
		subtree->top_fill = lab_wlr_scene_rect_create(parent, 1, 1, fill);
		subtree->left_fill = lab_wlr_scene_rect_create(parent, 1, 1, fill);
		subtree->right_fill = lab_wlr_scene_rect_create(parent, 1, 1, fill);
		subtree->bottom_fill = lab_wlr_scene_rect_create(parent, 1, 1, fill);
		subtree->bottom_left_shoulder_shape =
			lab_wlr_scene_buffer_create(parent, NULL);
		subtree->bottom_right_shoulder_shape =
			lab_wlr_scene_buffer_create(parent, NULL);
		subtree->bottom_left_shoulder_stile_fill =
			lab_wlr_scene_rect_create(parent, 1, 1, fill);
		subtree->bottom_left_shoulder_bottom_fill =
			lab_wlr_scene_rect_create(parent, 1, 1, fill);
		subtree->bottom_right_shoulder_stile_fill =
			lab_wlr_scene_rect_create(parent, 1, 1, fill);
		subtree->bottom_right_shoulder_bottom_fill =
			lab_wlr_scene_rect_create(parent, 1, 1, fill);
		ssd_compartment_relief_create(parent, &subtree->left_border,
			hl, sh, SSD_RELIEF_MODE_RELIEF);
		ssd_compartment_relief_create(parent, &subtree->right_border,
			hl, sh, SSD_RELIEF_MODE_RELIEF);
		ssd_compartment_relief_create(parent, &subtree->top_border,
			hl, sh, SSD_RELIEF_MODE_RELIEF);
		ssd_compartment_relief_create(parent, &subtree->bottom_border,
			hl, sh, SSD_RELIEF_MODE_RELIEF);
		ssd_shoulder_relief_create(parent, &subtree->top_left_shoulder,
			hl, sh, SSD_RELIEF_MODE_RELIEF,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_LEFT,
			SSD_RELIEF_FACE_RIGHT, SSD_RELIEF_FACE_RIGHT,
			SSD_RELIEF_FACE_DOWN, SSD_RELIEF_FACE_DOWN);
		ssd_shoulder_relief_create(parent, &subtree->top_right_shoulder,
			hl, sh, SSD_RELIEF_MODE_RELIEF,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_RIGHT,
			SSD_RELIEF_FACE_LEFT, SSD_RELIEF_FACE_LEFT,
			SSD_RELIEF_FACE_DOWN, SSD_RELIEF_FACE_DOWN);
		ssd_shoulder_relief_create(parent, &subtree->bottom_left_shoulder,
			hl, sh, SSD_RELIEF_MODE_RELIEF,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_LEFT,
			SSD_RELIEF_FACE_RIGHT, SSD_RELIEF_FACE_RIGHT,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_DOWN);
		ssd_shoulder_relief_create(parent, &subtree->bottom_right_shoulder,
			hl, sh, SSD_RELIEF_MODE_RELIEF,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_RIGHT,
			SSD_RELIEF_FACE_LEFT, SSD_RELIEF_FACE_LEFT,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_DOWN);

		/* Multi-band border (P2): create outer and inner bands */
		int outer_width = frame.outer_width;
		int inner_width = frame.inner_width;

		if (outer_width > 0) {
			/* Outer band */
			float *outer_color_left = theme->window[active].frame_outer_color_left;
			float *outer_color_right = theme->window[active].frame_outer_color_right;
			float *outer_color_top = theme->window[active].frame_outer_color_top;
			float *outer_color_bottom = theme->window[active].frame_outer_color_bottom;

			subtree->outer_left = lab_wlr_scene_rect_create(parent,
				outer_width, side_height, outer_color_left);
			wlr_scene_node_set_position(&subtree->outer_left->node, 0, side_y);

			subtree->outer_right = lab_wlr_scene_rect_create(parent,
				outer_width, side_height, outer_color_right);
			wlr_scene_node_set_position(&subtree->outer_right->node,
				full_width - outer_width, side_y);

			subtree->outer_bottom = lab_wlr_scene_rect_create(parent,
				full_width, outer_width, outer_color_bottom);
			wlr_scene_node_set_position(&subtree->outer_bottom->node,
				0, height + inner_width);

			subtree->outer_top = lab_wlr_scene_rect_create(parent,
				full_top_border ? full_width : custom_top_width,
				outer_width, outer_color_top);
			wlr_scene_node_set_position(&subtree->outer_top->node,
				full_top_border ? 0 : custom_top_x,
				get_outer_top_y(ssd, full_top_border, outer_width));
			wlr_scene_node_set_enabled(&subtree->outer_top->node,
				full_top_border);

			/* Inner band (adjacent to outer band) */
			if (inner_width > 0) {
				float *inner_color_left = theme->window[active].frame_inner_color_left;
				float *inner_color_right = theme->window[active].frame_inner_color_right;
				float *inner_color_top = theme->window[active].frame_inner_color_top;
				float *inner_color_bottom = theme->window[active].frame_inner_color_bottom;

				subtree->inner_left = lab_wlr_scene_rect_create(parent,
					inner_width, side_height, inner_color_left);
				wlr_scene_node_set_position(&subtree->inner_left->node,
					outer_width, side_y);

				subtree->inner_right = lab_wlr_scene_rect_create(parent,
					inner_width, side_height, inner_color_right);
				wlr_scene_node_set_position(&subtree->inner_right->node,
					full_width - outer_width - inner_width, side_y);

				subtree->inner_bottom = lab_wlr_scene_rect_create(parent,
					full_width - 2 * outer_width, inner_width,
					inner_color_bottom);
				wlr_scene_node_set_position(&subtree->inner_bottom->node,
					outer_width, height);

				subtree->inner_top = lab_wlr_scene_rect_create(parent,
					full_top_border
						? MAX(full_width - 2 * outer_width, 0)
						: MAX(custom_top_width - 2 * outer_width, 0),
					inner_width,
					inner_color_top);
				wlr_scene_node_set_position(&subtree->inner_top->node,
					(full_top_border ? 0 : custom_top_x) + outer_width,
					get_inner_top_y(ssd, full_top_border, outer_width));
				wlr_scene_node_set_enabled(&subtree->inner_top->node,
					full_top_border);
			}
		}
	}

	if (view->maximized == VIEW_AXIS_BOTH) {
		wlr_scene_node_set_enabled(&ssd->border.tree->node, false);
	}

	if (view->current.width > 0 && view->current.height > 0) {
		/*
		 * The SSD is recreated by a Reconfigure request
		 * thus we may need to handle squared corners.
		 */
		ssd_border_update(ssd);
	}
}

void
ssd_border_update(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	struct view *view = ssd->view;
	if (view->maximized == VIEW_AXIS_BOTH
			&& ssd->border.tree->node.enabled) {
		/* Disable borders on maximize */
		wlr_scene_node_set_enabled(&ssd->border.tree->node, false);
		ssd->margin = ssd_thickness(ssd->view);
	}

	if (view->maximized == VIEW_AXIS_BOTH) {
		return;
	} else if (!ssd->border.tree->node.enabled) {
		/* And re-enabled them when unmaximized */
		wlr_scene_node_set_enabled(&ssd->border.tree->node, true);
		ssd->margin = ssd_thickness(ssd->view);
	}

	struct theme *theme = rc.theme;

	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int bottom_shoulder_w = get_titlebar_shoulder_width(ssd,
		get_titlebar_button_side_len());
	int custom_top_x = 0;
	int custom_top_width = 0;

	/*
	 * From here on we have to cover the following border scenarios:
	 * Floating/titlebar frame (partial top border, rounded corners):
	 *    _____________
	 *   o           oox
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled/titlebar frame (full top border, squared corners):
	 *   _______________
	 *  |o           oox|
	 *  |---------------|
	 *  |_______________|
	 *
	 * Border-only frame (full border, no titlebar):
	 *   _______________
	 *  |_______________|
	 */

	bool full_side_border = border_owns_title_zone_sides(ssd);
	int side_height = full_side_border ? height + ssd->titlebar.height : height;
	int side_y = full_side_border ? -ssd->titlebar.height : 0;
	int top_width;
	int top_x;
	int button_side_len = get_titlebar_button_side_len();
	bottom_shoulder_w = MIN(bottom_shoulder_w, full_width / 2);
	get_shoulder_strip_geometry(ssd, button_side_len,
		&custom_top_x, &custom_top_width);
	top_width = border_owns_top_edge(ssd) ? full_width : custom_top_width;
	top_x = border_owns_top_edge(ssd) ? 0 : custom_top_x;
	/*
	 * Match the titlebar's top-center strip, not the border's rounded top
	 * edge. The border tree is offset by -border_width, so a titlebar center
	 * strip that starts at titlebar_height in titlebar-local coordinates maps
	 * to titlebar_height + border_width here.
	 */
	int top_border_x = custom_top_x;
	int top_border_w = custom_top_width;
	int top_relief_y = -(ssd->titlebar.height + theme->border_width);
	int bottom_border_x = custom_top_x;
	int bottom_border_w = custom_top_width;
	int bottom_shoulder_h = get_titlebar_shoulder_height(ssd, button_side_len);
	bottom_shoulder_h = MIN(bottom_shoulder_h, height + theme->border_width);
	int bottom_shoulder_y = height + theme->border_width - bottom_shoulder_h;
	int side_compartment_h = MAX(bottom_shoulder_y, 0);
	int bevel_w;
	bool custom_frame = use_custom_border_frame(ssd);
	bool full_top_border = border_owns_top_edge(ssd);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		struct ssd_frame_band_geometry frame =
			get_frame_band_geometry(theme, active);
		bevel_w = theme->window[active].titlebar_bevel_width;
		wlr_scene_node_set_enabled(&subtree->top_left_shoulder_shape->node,
			custom_frame && theme->border_width > 0);
		wlr_scene_node_set_enabled(&subtree->top_right_shoulder_shape->node,
			custom_frame && theme->border_width > 0);

		wlr_scene_node_set_enabled(&subtree->top_fill->node, custom_frame);
		wlr_scene_node_set_enabled(&subtree->left_fill->node, custom_frame);
		wlr_scene_node_set_enabled(&subtree->right_fill->node, custom_frame);
		wlr_scene_node_set_enabled(&subtree->bottom_fill->node, custom_frame);
		wlr_scene_node_set_enabled(&subtree->bottom_left_shoulder_shape->node,
			custom_frame && theme->border_width > 0);
		wlr_scene_node_set_enabled(&subtree->bottom_right_shoulder_shape->node,
			custom_frame && theme->border_width > 0);
		wlr_scene_node_set_enabled(
			&subtree->bottom_left_shoulder_stile_fill->node, false);
		wlr_scene_node_set_enabled(
			&subtree->bottom_left_shoulder_bottom_fill->node, false);
		wlr_scene_node_set_enabled(
			&subtree->bottom_right_shoulder_stile_fill->node, false);
		wlr_scene_node_set_enabled(
			&subtree->bottom_right_shoulder_bottom_fill->node, false);

		if (custom_frame) {
			wlr_scene_rect_set_size(subtree->top_fill,
				top_border_w, theme->border_width);
			wlr_scene_node_set_position(&subtree->top_fill->node,
				top_border_x, top_relief_y);
			wlr_scene_rect_set_size(subtree->left_fill,
				theme->border_width, side_compartment_h);
			wlr_scene_node_set_position(&subtree->left_fill->node, 0, 0);
			wlr_scene_rect_set_size(subtree->right_fill,
				theme->border_width, side_compartment_h);
			wlr_scene_node_set_position(&subtree->right_fill->node,
				full_width - theme->border_width, 0);
			wlr_scene_rect_set_size(subtree->bottom_fill,
				bottom_border_w, theme->border_width);
			wlr_scene_node_set_position(&subtree->bottom_fill->node,
				bottom_border_x, height);

			wlr_scene_node_set_position(&subtree->left_border.tree->node,
				0, 0);
			ssd_compartment_relief_update(&subtree->left_border,
				theme->border_width, side_compartment_h, bevel_w);
			wlr_scene_node_set_position(&subtree->right_border.tree->node,
				full_width - theme->border_width, 0);
			ssd_compartment_relief_update(&subtree->right_border,
				theme->border_width, side_compartment_h, bevel_w);
			wlr_scene_node_set_position(&subtree->top_border.tree->node,
				top_border_x, top_relief_y);
			ssd_compartment_relief_update(&subtree->top_border,
				top_border_w, theme->border_width, bevel_w);
			wlr_scene_node_set_position(&subtree->bottom_border.tree->node,
				bottom_border_x, height);
			ssd_compartment_relief_update(&subtree->bottom_border,
				bottom_border_w, theme->border_width, bevel_w);
			ssd_shoulder_relief_update(&subtree->top_left_shoulder,
				0, 0, 0, 0, 0, false, SSD_SHOULDER_TOP);
			ssd_shoulder_relief_update(&subtree->top_right_shoulder,
				0, 0, 0, 0, 0, true, SSD_SHOULDER_TOP);
			ssd_shoulder_relief_update(&subtree->bottom_left_shoulder,
				0, 0, 0, 0, 0, false, SSD_SHOULDER_BOTTOM);
			ssd_shoulder_relief_update(&subtree->bottom_right_shoulder,
				0, 0, 0, 0, 0, true, SSD_SHOULDER_BOTTOM);
			if (theme->border_width > 0) {
				struct ssd_shoulder_shape_spec left_spec = {
					.direction = SSD_SHAPE_LEFT_UPPER,
					.anchor_kind = SSD_SHAPE_ANCHOR_NESTED_RIGHT_BOTTOM,
					.anchor_x = top_border_x,
					.anchor_y = 0,
					.branch_width = theme->border_width,
					.nested_square_size = button_side_len,
					.palette = {
						.fill = { 0 },
						.highlight = { 0 },
						.shadow = { 0 },
						.drop_shadow = { 0 },
					},
					.style = {
						.bevel_width = bevel_w,
						.draw_fill = true,
						.draw_outline = false,
						.draw_drop_shadow = false,
						.drop_shadow_dx = 0,
						.drop_shadow_dy = 0,
					},
				};
				struct ssd_shoulder_shape_spec right_top_spec = left_spec;
				memcpy(left_spec.palette.fill, theme->window[active].title_bg.color,
					sizeof(left_spec.palette.fill));
				memcpy(left_spec.palette.highlight,
					theme->window[active].titlebar_bevel_highlight_color,
					sizeof(left_spec.palette.highlight));
				memcpy(left_spec.palette.shadow,
					theme->window[active].titlebar_bevel_shadow_color,
					sizeof(left_spec.palette.shadow));
				right_top_spec = left_spec;
				right_top_spec.direction = SSD_SHAPE_RIGHT_UPPER;
				right_top_spec.anchor_kind = SSD_SHAPE_ANCHOR_NESTED_LEFT_BOTTOM;
				right_top_spec.anchor_x = top_border_x + top_border_w;
				right_top_spec.anchor_y = 0;
				update_shoulder_shape(subtree->top_left_shoulder_shape,
					&left_spec);
				update_shoulder_shape(subtree->top_right_shoulder_shape,
					&right_top_spec);

				left_spec = (struct ssd_shoulder_shape_spec) {
					.direction = SSD_SHAPE_LEFT_BOTTOM,
					.anchor_kind = SSD_SHAPE_ANCHOR_NESTED_RIGHT_TOP,
					.anchor_x = bottom_border_x,
					.anchor_y = bottom_shoulder_y,
					.branch_width = theme->border_width,
					.nested_square_size = button_side_len,
					.palette = {
						.fill = { 0 },
						.highlight = { 0 },
						.shadow = { 0 },
						.drop_shadow = { 0 },
					},
					.style = {
						.bevel_width = bevel_w,
						.draw_fill = true,
						.draw_outline = false,
						.draw_drop_shadow = false,
						.drop_shadow_dx = 0,
						.drop_shadow_dy = 0,
					},
				};
				struct ssd_shoulder_shape_spec right_spec = left_spec;
				memcpy(left_spec.palette.fill, theme->window[active].title_bg.color,
					sizeof(left_spec.palette.fill));
				memcpy(left_spec.palette.highlight,
					theme->window[active].titlebar_bevel_highlight_color,
					sizeof(left_spec.palette.highlight));
				memcpy(left_spec.palette.shadow,
					theme->window[active].titlebar_bevel_shadow_color,
					sizeof(left_spec.palette.shadow));
				right_spec = left_spec;
					right_spec.direction = SSD_SHAPE_RIGHT_BOTTOM;
					right_spec.anchor_kind = SSD_SHAPE_ANCHOR_NESTED_LEFT_TOP;
					right_spec.anchor_x = bottom_border_x + bottom_border_w;
					right_spec.anchor_y = bottom_shoulder_y;
					update_shoulder_shape(subtree->bottom_left_shoulder_shape,
						&left_spec);
				update_shoulder_shape(subtree->bottom_right_shoulder_shape,
					&right_spec);
			}
		} else {
			ssd_compartment_relief_update(&subtree->top_border, 0, 0, 0);
			ssd_compartment_relief_update(&subtree->left_border, 0, 0, 0);
			ssd_compartment_relief_update(&subtree->right_border, 0, 0, 0);
			ssd_compartment_relief_update(&subtree->bottom_border, 0, 0, 0);
			ssd_shoulder_relief_update(&subtree->top_left_shoulder,
				0, 0, 0, 0, 0, false, SSD_SHOULDER_TOP);
			ssd_shoulder_relief_update(&subtree->top_right_shoulder,
				0, 0, 0, 0, 0, true, SSD_SHOULDER_TOP);
			ssd_shoulder_relief_update(&subtree->bottom_left_shoulder,
				0, 0, 0, 0, 0, false, SSD_SHOULDER_BOTTOM);
			ssd_shoulder_relief_update(&subtree->bottom_right_shoulder,
				0, 0, 0, 0, 0, true, SSD_SHOULDER_BOTTOM);
			wlr_scene_buffer_set_buffer(subtree->top_left_shoulder_shape, NULL);
			wlr_scene_buffer_set_buffer(subtree->top_right_shoulder_shape, NULL);
			wlr_scene_buffer_set_buffer(subtree->bottom_left_shoulder_shape, NULL);
			wlr_scene_buffer_set_buffer(subtree->bottom_right_shoulder_shape, NULL);
		}

		/* Multi-band border (P2): update outer and inner bands */
		int outer_width = frame.outer_width;
		int inner_width = frame.inner_width;

		if (outer_width > 0 && subtree->outer_left) {
			/* Outer band */
			wlr_scene_rect_set_size(subtree->outer_left,
				outer_width, side_height);
			wlr_scene_node_set_position(&subtree->outer_left->node,
				0, side_y);

			wlr_scene_rect_set_size(subtree->outer_right,
				outer_width, side_height);
			wlr_scene_node_set_position(&subtree->outer_right->node,
				full_width - outer_width, side_y);

			wlr_scene_rect_set_size(subtree->outer_bottom,
				full_width, outer_width);
			wlr_scene_node_set_position(&subtree->outer_bottom->node,
				0, height + inner_width);

			wlr_scene_rect_set_size(subtree->outer_top,
				top_width, outer_width);
			wlr_scene_node_set_position(&subtree->outer_top->node,
				top_x,
				get_outer_top_y(ssd, full_top_border, outer_width));
			wlr_scene_node_set_enabled(&subtree->outer_top->node,
				full_top_border && !custom_frame);
			wlr_scene_node_set_enabled(&subtree->outer_left->node, !custom_frame);
			wlr_scene_node_set_enabled(&subtree->outer_right->node, !custom_frame);
			wlr_scene_node_set_enabled(&subtree->outer_bottom->node, !custom_frame);

			/* Inner band */
			if (inner_width > 0 && subtree->inner_left) {
				wlr_scene_rect_set_size(subtree->inner_left,
					inner_width, side_height);
				wlr_scene_node_set_position(&subtree->inner_left->node,
					outer_width, side_y);

				wlr_scene_rect_set_size(subtree->inner_right,
					inner_width, side_height);
				wlr_scene_node_set_position(&subtree->inner_right->node,
					full_width - outer_width - inner_width, side_y);

				wlr_scene_rect_set_size(subtree->inner_bottom,
					full_width - 2 * outer_width, inner_width);
				wlr_scene_node_set_position(&subtree->inner_bottom->node,
					outer_width, height);

				wlr_scene_rect_set_size(subtree->inner_top,
					MAX(top_width - 2 * outer_width, 0), inner_width);
				wlr_scene_node_set_position(&subtree->inner_top->node,
					top_x + outer_width,
					get_inner_top_y(ssd, full_top_border, outer_width));
				wlr_scene_node_set_enabled(&subtree->inner_top->node,
					full_top_border && !custom_frame);
				wlr_scene_node_set_enabled(&subtree->inner_left->node, !custom_frame);
				wlr_scene_node_set_enabled(&subtree->inner_right->node, !custom_frame);
				wlr_scene_node_set_enabled(&subtree->inner_bottom->node, !custom_frame);
			}

			wlr_scene_node_raise_to_top(
				&subtree->bottom_left_shoulder_shape->node);
			wlr_scene_node_raise_to_top(
				&subtree->bottom_right_shoulder_shape->node);
			wlr_scene_node_raise_to_top(
				&subtree->top_left_shoulder_shape->node);
			wlr_scene_node_raise_to_top(
				&subtree->top_right_shoulder_shape->node);
		}
	}
}

void
ssd_border_destroy(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	wlr_scene_node_destroy(&ssd->border.tree->node);
	ssd->border = (struct ssd_border_scene){0};
}
