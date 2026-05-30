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
ssd_handle_create(struct ssd *ssd)
{
	assert(ssd);

	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int handle_h = theme->handle_height;

	if (handle_h <= 0) {
		return;
	}

	ssd->handle.tree = lab_wlr_scene_tree_create(ssd->tree);
	wlr_scene_node_set_position(&ssd->handle.tree->node,
		-theme->border_width, height);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_handle_subtree *subtree = &ssd->handle.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->handle.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);

		int full_width = width + 2 * theme->border_width;
		float *hl = theme->handle_bevel_highlight_color;
		float *sh = theme->handle_bevel_shadow_color;

		subtree->highlight = lab_wlr_scene_rect_create(
			parent, full_width, handle_h, hl);
		wlr_scene_node_set_position(&subtree->highlight->node, 0, 0);

		subtree->shadow = lab_wlr_scene_rect_create(
			parent, full_width, handle_h, sh);
		wlr_scene_node_set_position(&subtree->shadow->node, 0, handle_h);
	}

	if (view->maximized == VIEW_AXIS_BOTH) {
		wlr_scene_node_set_enabled(&ssd->handle.tree->node, false);
	}
}

void
ssd_handle_update(struct ssd *ssd)
{
	assert(ssd);

	if (!ssd->handle.tree) {
		return;
	}

	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int handle_h = theme->handle_height;

	bool maximized = view->maximized == VIEW_AXIS_BOTH;

	if (maximized && ssd->handle.tree->node.enabled) {
		wlr_scene_node_set_enabled(&ssd->handle.tree->node, false);
		return;
	}
	if (!maximized && !ssd->handle.tree->node.enabled) {
		wlr_scene_node_set_enabled(&ssd->handle.tree->node, true);
	}

	int full_width = width + 2 * theme->border_width;

	wlr_scene_node_set_position(&ssd->handle.tree->node,
		-theme->border_width, height);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_handle_subtree *subtree = &ssd->handle.subtrees[active];
		wlr_scene_rect_set_size(subtree->highlight,
			full_width, handle_h);
		wlr_scene_rect_set_size(subtree->shadow,
			full_width, handle_h);
		wlr_scene_node_set_position(&subtree->shadow->node,
			0, handle_h);
	}
}

void
ssd_handle_destroy(struct ssd *ssd)
{
	assert(ssd);

	if (!ssd->handle.tree) {
		return;
	}

	wlr_scene_node_destroy(&ssd->handle.tree->node);
	ssd->handle = (struct ssd_handle_scene){0};
}
