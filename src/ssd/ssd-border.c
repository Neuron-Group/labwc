// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "common/macros.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

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
	int corner_width = ssd_get_corner_width();

	ssd->border.tree = lab_wlr_scene_tree_create(ssd->tree);
	wlr_scene_node_set_position(&ssd->border.tree->node, -theme->border_width, 0);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->border.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		float *color_left = theme->window[active].border_color_left;
		float *color_right = theme->window[active].border_color_right;
		float *color_top = theme->window[active].border_color_top;
		float *color_bottom = theme->window[active].border_color_bottom;

		subtree->left = lab_wlr_scene_rect_create(parent,
			theme->border_width, height, color_left);
		wlr_scene_node_set_position(&subtree->left->node, 0, 0);

		subtree->right = lab_wlr_scene_rect_create(parent,
			theme->border_width, height, color_right);
		wlr_scene_node_set_position(&subtree->right->node,
			theme->border_width + width, 0);

		subtree->bottom = lab_wlr_scene_rect_create(parent,
			full_width, theme->border_width, color_bottom);
		wlr_scene_node_set_position(&subtree->bottom->node,
			0, height);

		subtree->top = lab_wlr_scene_rect_create(parent,
			MAX(width - 2 * corner_width, 0), theme->border_width, color_top);
		wlr_scene_node_set_position(&subtree->top->node,
			theme->border_width + corner_width,
			-(ssd->titlebar.height + theme->border_width));

		/* Multi-band border (P2): create outer and inner bands */
		int outer_width = theme->window[active].frame_outer_width;
		int inner_width = theme->window[active].frame_inner_width;

		if (outer_width > 0) {
			/* Outer band */
			float *outer_color_left = theme->window[active].frame_outer_color_left;
			float *outer_color_right = theme->window[active].frame_outer_color_right;
			float *outer_color_top = theme->window[active].frame_outer_color_top;
			float *outer_color_bottom = theme->window[active].frame_outer_color_bottom;

			subtree->outer_left = lab_wlr_scene_rect_create(parent,
				outer_width, height, outer_color_left);
			wlr_scene_node_set_position(&subtree->outer_left->node, 0, 0);

			subtree->outer_right = lab_wlr_scene_rect_create(parent,
				outer_width, height, outer_color_right);
			wlr_scene_node_set_position(&subtree->outer_right->node,
				outer_width + width, 0);

			subtree->outer_bottom = lab_wlr_scene_rect_create(parent,
				width + 2 * outer_width, outer_width, outer_color_bottom);
			wlr_scene_node_set_position(&subtree->outer_bottom->node,
				0, height);

			subtree->outer_top = lab_wlr_scene_rect_create(parent,
				MAX(width - 2 * corner_width, 0), outer_width, outer_color_top);
			wlr_scene_node_set_position(&subtree->outer_top->node,
				outer_width + corner_width,
				-(ssd->titlebar.height + outer_width));

			/* Inner band (adjacent to outer band) */
			if (inner_width > 0) {
				float *inner_color_left = theme->window[active].frame_inner_color_left;
				float *inner_color_right = theme->window[active].frame_inner_color_right;
				float *inner_color_top = theme->window[active].frame_inner_color_top;
				float *inner_color_bottom = theme->window[active].frame_inner_color_bottom;

				subtree->inner_left = lab_wlr_scene_rect_create(parent,
					inner_width, height, inner_color_left);
				wlr_scene_node_set_position(&subtree->inner_left->node,
					outer_width, 0);

				subtree->inner_right = lab_wlr_scene_rect_create(parent,
					inner_width, height, inner_color_right);
				wlr_scene_node_set_position(&subtree->inner_right->node,
					outer_width + width, 0);

				subtree->inner_bottom = lab_wlr_scene_rect_create(parent,
					width + 2 * outer_width + 2 * inner_width, inner_width,
					inner_color_bottom);
				wlr_scene_node_set_position(&subtree->inner_bottom->node,
					0, height + outer_width);

				subtree->inner_top = lab_wlr_scene_rect_create(parent,
					MAX(width - 2 * corner_width, 0), inner_width,
					inner_color_top);
				wlr_scene_node_set_position(&subtree->inner_top->node,
					outer_width + corner_width,
					-(ssd->titlebar.height + outer_width + inner_width));
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
	int corner_width = ssd_get_corner_width();

	/*
	 * From here on we have to cover the following border scenarios:
	 * Non-tiled (partial border, rounded corners):
	 *    _____________
	 *   o           oox
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled (full border, squared corners):
	 *   _______________
	 *  |o           oox|
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled or non-tiled with zero title height (full boarder, no title):
	 *   _______________
	 *  |_______________|
	 */

	int side_height = ssd->state.was_squared
		? height + ssd->titlebar.height
		: height;
	int side_y = ssd->state.was_squared
		? -ssd->titlebar.height
		: 0;
	int top_width = ssd->titlebar.height <= 0 || ssd->state.was_squared
		? full_width
		: MAX(width - 2 * corner_width, 0);
	int top_x = ssd->titlebar.height <= 0 || ssd->state.was_squared
		? 0
		: theme->border_width + corner_width;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];

		wlr_scene_rect_set_size(subtree->left,
			theme->border_width, side_height);
		wlr_scene_node_set_position(&subtree->left->node,
			0, side_y);

		wlr_scene_rect_set_size(subtree->right,
			theme->border_width, side_height);
		wlr_scene_node_set_position(&subtree->right->node,
			theme->border_width + width, side_y);

		wlr_scene_rect_set_size(subtree->bottom,
			full_width, theme->border_width);
		wlr_scene_node_set_position(&subtree->bottom->node,
			0, height);

		wlr_scene_rect_set_size(subtree->top,
			top_width, theme->border_width);
		wlr_scene_node_set_position(&subtree->top->node,
			top_x, -(ssd->titlebar.height + theme->border_width));

		/* Multi-band border (P2): update outer and inner bands */
		int outer_width = theme->window[active].frame_outer_width;
		int inner_width = theme->window[active].frame_inner_width;

		if (outer_width > 0 && subtree->outer_left) {
			/* Outer band */
			wlr_scene_rect_set_size(subtree->outer_left,
				outer_width, side_height);
			wlr_scene_node_set_position(&subtree->outer_left->node,
				0, side_y);

			wlr_scene_rect_set_size(subtree->outer_right,
				outer_width, side_height);
			wlr_scene_node_set_position(&subtree->outer_right->node,
				outer_width + width, side_y);

			wlr_scene_rect_set_size(subtree->outer_bottom,
				width + 2 * outer_width, outer_width);
			wlr_scene_node_set_position(&subtree->outer_bottom->node,
				0, height);

			wlr_scene_rect_set_size(subtree->outer_top,
				top_width, outer_width);
			wlr_scene_node_set_position(&subtree->outer_top->node,
				top_x, -(ssd->titlebar.height + outer_width));

			/* Inner band */
			if (inner_width > 0 && subtree->inner_left) {
				wlr_scene_rect_set_size(subtree->inner_left,
					inner_width, side_height);
				wlr_scene_node_set_position(&subtree->inner_left->node,
					outer_width, side_y);

				wlr_scene_rect_set_size(subtree->inner_right,
					inner_width, side_height);
				wlr_scene_node_set_position(&subtree->inner_right->node,
					outer_width + width, side_y);

				wlr_scene_rect_set_size(subtree->inner_bottom,
					width + 2 * outer_width + 2 * inner_width, inner_width);
				wlr_scene_node_set_position(&subtree->inner_bottom->node,
					0, height + outer_width);

				wlr_scene_rect_set_size(subtree->inner_top,
					top_width, inner_width);
				wlr_scene_node_set_position(&subtree->inner_top->node,
					top_x, -(ssd->titlebar.height + outer_width + inner_width));
			}
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
