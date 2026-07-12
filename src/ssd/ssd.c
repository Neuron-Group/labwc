// SPDX-License-Identifier: GPL-2.0-only

/*
 * Helpers for view server side decorations
 *
 * Copyright (C) Johan Malm 2020-2021
 */

#include "ssd.h"
#include <assert.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

static void
get_relief_colors(enum ssd_relief_mode mode,
		const float highlight[4], const float shadow[4],
		const float **top_left, const float **bottom_right)
{
	*top_left = mode == SSD_RELIEF_MODE_RECESS ? shadow : highlight;
	*bottom_right = mode == SSD_RELIEF_MODE_RECESS ? highlight : shadow;
}

static const float *
get_relief_face_color(enum ssd_relief_mode mode, enum ssd_relief_face face,
		const float highlight[4], const float shadow[4])
{
	bool use_highlight = face == SSD_RELIEF_FACE_UP
		|| face == SSD_RELIEF_FACE_LEFT;

	if (mode == SSD_RELIEF_MODE_RECESS) {
		use_highlight = !use_highlight;
	}

	return use_highlight ? highlight : shadow;
}

void
ssd_compartment_relief_create(struct wlr_scene_tree *parent,
		struct ssd_compartment_relief *relief, const float highlight[4],
		const float shadow[4], enum ssd_relief_mode mode)
{
	const float *top_left;
	const float *bottom_right;

	get_relief_colors(mode, highlight, shadow, &top_left, &bottom_right);

	relief->tree = lab_wlr_scene_tree_create(parent);
	relief->top = lab_wlr_scene_rect_create(relief->tree, 1, 1, top_left);
	relief->left = lab_wlr_scene_rect_create(relief->tree, 1, 1, top_left);
	relief->bottom = lab_wlr_scene_rect_create(relief->tree, 1, 1,
		bottom_right);
	relief->right = lab_wlr_scene_rect_create(relief->tree, 1, 1,
		bottom_right);
}

void
ssd_compartment_relief_update(struct ssd_compartment_relief *relief,
		int width, int height, int bevel_w)
{
	bool visible = width > 0 && height > 0 && bevel_w > 0;

	if (!relief->top) {
		return;
	}

	wlr_scene_node_set_enabled(&relief->tree->node, visible);
	if (!visible) {
		return;
	}

	wlr_scene_rect_set_size(relief->top, width, bevel_w);
	wlr_scene_node_set_position(&relief->top->node, 0, 0);
	wlr_scene_rect_set_size(relief->left, bevel_w, height);
	wlr_scene_node_set_position(&relief->left->node, 0, 0);
	wlr_scene_rect_set_size(relief->bottom, width, bevel_w);
	wlr_scene_node_set_position(&relief->bottom->node, 0,
		height - bevel_w);
	wlr_scene_rect_set_size(relief->right, bevel_w, height);
	wlr_scene_node_set_position(&relief->right->node,
		width - bevel_w, 0);
}

void
ssd_shoulder_relief_create(struct wlr_scene_tree *parent,
		struct ssd_shoulder_relief *relief, const float highlight[4],
		const float shadow[4], enum ssd_relief_mode mode,
		enum ssd_relief_face top_face, enum ssd_relief_face outer_face,
		enum ssd_relief_face cap_return_face, enum ssd_relief_face inner_face,
		enum ssd_relief_face cap_underside_face, enum ssd_relief_face bottom_face)
{
	const float *top_color = get_relief_face_color(mode, top_face,
		highlight, shadow);
	const float *outer_color = get_relief_face_color(mode, outer_face,
		highlight, shadow);
	const float *cap_return_color = get_relief_face_color(mode,
		cap_return_face, highlight, shadow);
	const float *inner_color = get_relief_face_color(mode, inner_face,
		highlight, shadow);
	const float *cap_underside_color = get_relief_face_color(mode,
		cap_underside_face, highlight, shadow);
	const float *bottom_color = get_relief_face_color(mode, bottom_face,
		highlight, shadow);

	relief->tree = lab_wlr_scene_tree_create(parent);
	relief->top = lab_wlr_scene_rect_create(relief->tree, 1, 1, top_color);
	relief->outer = lab_wlr_scene_rect_create(relief->tree, 1, 1, outer_color);
	relief->cap_return = lab_wlr_scene_rect_create(relief->tree, 1, 1,
		cap_return_color);
	relief->inner = lab_wlr_scene_rect_create(relief->tree, 1, 1, inner_color);
	relief->cap_underside = lab_wlr_scene_rect_create(relief->tree, 1, 1,
		cap_underside_color);
	relief->bottom = lab_wlr_scene_rect_create(relief->tree, 1, 1,
		bottom_color);
}

void
ssd_shoulder_relief_update(struct ssd_shoulder_relief *relief,
		int width, int height, int stile_w, int cap_h, int bevel_w,
		bool right_side, enum ssd_shoulder_orientation orientation)
{
	bool visible = width > 0 && height > 0 && bevel_w > 0;
	int stile_end_x;
	int stile_start_x;
	int cap_return_x;
	int cap_return_h;
	int inner_h;
	int cap_underside_x;
	int cap_underside_w;
	int top_x;
	int top_w;
	int bottom_x;
	int bottom_w;
	int cap_return_y;
	int inner_y;
	int cap_underside_y;

	if (!relief->top) {
		return;
	}

	wlr_scene_node_set_enabled(&relief->tree->node, visible);
	if (!visible) {
		return;
	}

	stile_w = MIN(stile_w, width);
	cap_h = MIN(cap_h, height);
	stile_w = MAX(stile_w, bevel_w);
	cap_h = MAX(cap_h, bevel_w);
	cap_return_h = MAX(cap_h, bevel_w);
	inner_h = MAX(height - cap_h, bevel_w);

	if (orientation == SSD_SHOULDER_TOP) {
		wlr_scene_rect_set_size(relief->top, width, bevel_w);
		wlr_scene_node_set_position(&relief->top->node, 0, 0);
		cap_return_y = 0;
		inner_y = cap_h;
		cap_underside_y = cap_h - bevel_w;
	} else {
		wlr_scene_rect_set_size(relief->bottom, width, bevel_w);
		wlr_scene_node_set_position(&relief->bottom->node,
			0, height - bevel_w);
		cap_return_y = height - cap_h;
		inner_y = 0;
		cap_underside_y = height - cap_h;
	}

	if (right_side) {
		stile_start_x = width - stile_w;
		cap_return_x = 0;
		cap_underside_x = 0;
		cap_underside_w = MAX(stile_start_x + bevel_w, bevel_w);
		top_x = stile_start_x;
		top_w = MAX(width - stile_start_x, bevel_w);
		bottom_x = stile_start_x;
		bottom_w = MAX(width - stile_start_x, bevel_w);

		wlr_scene_rect_set_size(relief->outer, bevel_w, height);
		wlr_scene_node_set_position(&relief->outer->node,
			width - bevel_w, 0);
		wlr_scene_rect_set_size(relief->cap_return, bevel_w, cap_return_h);
		wlr_scene_node_set_position(&relief->cap_return->node,
			cap_return_x, cap_return_y);
		wlr_scene_rect_set_size(relief->inner, bevel_w, inner_h);
		wlr_scene_node_set_position(&relief->inner->node,
			stile_start_x, inner_y);
	} else {
		stile_end_x = stile_w - bevel_w;
		cap_return_x = width - bevel_w;
		cap_underside_x = stile_end_x;
		cap_underside_w = MAX(width - cap_underside_x, bevel_w);
		top_x = 0;
		top_w = MAX(stile_w, bevel_w);
		bottom_x = 0;
		bottom_w = MAX(stile_w, bevel_w);

		wlr_scene_rect_set_size(relief->outer, bevel_w, height);
		wlr_scene_node_set_position(&relief->outer->node, 0, 0);
		wlr_scene_rect_set_size(relief->cap_return, bevel_w, cap_return_h);
		wlr_scene_node_set_position(&relief->cap_return->node,
			cap_return_x, cap_return_y);
		wlr_scene_rect_set_size(relief->inner, bevel_w, inner_h);
		wlr_scene_node_set_position(&relief->inner->node,
			stile_end_x, inner_y);
	}

	if (orientation == SSD_SHOULDER_TOP) {
		wlr_scene_rect_set_size(relief->cap_underside, cap_underside_w, bevel_w);
		wlr_scene_node_set_position(&relief->cap_underside->node,
			cap_underside_x, cap_underside_y);
		wlr_scene_rect_set_size(relief->bottom, bottom_w, bevel_w);
		wlr_scene_node_set_position(&relief->bottom->node,
			bottom_x, height - bevel_w);
	} else {
		wlr_scene_rect_set_size(relief->top, top_w, bevel_w);
		wlr_scene_node_set_position(&relief->top->node, top_x, 0);
		wlr_scene_rect_set_size(relief->cap_underside, cap_underside_w, bevel_w);
		wlr_scene_node_set_position(&relief->cap_underside->node,
			cap_underside_x, cap_underside_y);
	}
}

struct border
ssd_thickness(struct view *view)
{
	assert(view);
	/*
	 * Check preconditions for displaying SSD. Note that this
	 * needs to work even before ssd_create() has been called.
	 *
	 * For that reason we are not using the .enabled state of
	 * the titlebar node here but rather check for the view
	 * boolean. If we were to use the .enabled state this would
	 * cause issues on Reconfigure events with views which were
	 * in border-only deco mode as view->ssd would only be set
	 * after ssd_create() returns.
	 */
	if (!view->ssd_mode || view->fullscreen) {
		return (struct border){ 0 };
	}

	struct theme *theme = rc.theme;

	if (view->maximized == VIEW_AXIS_BOTH) {
		struct border thickness = { 0 };
		if (view_titlebar_visible(view)) {
			thickness.top += theme->titlebar_height;
		}
		return thickness;
	}

	struct border thickness = {
		.top = theme->titlebar_height + theme->border_width,
		.right = theme->border_width,
		.bottom = theme->border_width,
		.left = theme->border_width,
	};
	if (!view_titlebar_visible(view)) {
		thickness.top -= theme->titlebar_height;
	}
	return thickness;
}

struct wlr_box
ssd_max_extents(struct view *view)
{
	assert(view);
	struct border border = ssd_thickness(view);

	int eff_width = view->current.width;
	int eff_height = view_effective_height(view, /* use_pending */ false);

	return (struct wlr_box){
		.x = view->current.x - border.left,
		.y = view->current.y - border.top,
		.width = eff_width + border.left + border.right,
		.height = eff_height + border.top + border.bottom,
	};
}

/*
 * Resizing and mouse contexts like 'Left', 'TLCorner', etc. in the vicinity of
 * SSD borders, titlebars and extents can have effective "corner regions" that
 * behave differently from single-edge contexts.
 *
 * Corner regions are active whenever the cursor is within a prescribed size
 * (generally rc.resize_corner_range, but clipped to view size) of the view
 * bounds, so check the cursor against the view here.
 */
enum lab_node_type
ssd_get_resizing_type(const struct ssd *ssd, struct wlr_cursor *cursor)
{
	struct view *view = ssd ? ssd->view : NULL;
	if (!view || !cursor || !view->ssd_mode || view->fullscreen
			|| !view_is_resizable(view)) {
		return LAB_NODE_NONE;
	}

	struct wlr_box view_box = view->current;
	view_box.height = view_effective_height(view, /* use_pending */ false);

	if (view_titlebar_visible(view)) {
		/* If the titlebar is visible, consider it part of the view */
		int titlebar_height = rc.theme->titlebar_height;
		view_box.y -= titlebar_height;
		view_box.height += titlebar_height;
	}

	if (wlr_box_contains_point(&view_box, cursor->x, cursor->y)) {
		/* A cursor in bounds of the view is never in an SSD context */
		return LAB_NODE_NONE;
	}

	int corner_height = MAX(0, MIN(rc.resize_corner_range, view_box.height / 2));
	int corner_width = MAX(0, MIN(rc.resize_corner_range, view_box.width / 2));
	bool left = cursor->x < view_box.x + corner_width;
	bool right = cursor->x > view_box.x + view_box.width - corner_width;
	bool top = cursor->y < view_box.y + corner_height;
	bool bottom = cursor->y > view_box.y + view_box.height - corner_height;

	if (top && left) {
		return LAB_NODE_CORNER_TOP_LEFT;
	} else if (top && right) {
		return LAB_NODE_CORNER_TOP_RIGHT;
	} else if (bottom && left) {
		return LAB_NODE_CORNER_BOTTOM_LEFT;
	} else if (bottom && right) {
		return LAB_NODE_CORNER_BOTTOM_RIGHT;
	} else if (top) {
		return LAB_NODE_BORDER_TOP;
	} else if (bottom) {
		return LAB_NODE_BORDER_BOTTOM;
	} else if (left) {
		return LAB_NODE_BORDER_LEFT;
	} else if (right) {
		return LAB_NODE_BORDER_RIGHT;
	}

	return LAB_NODE_NONE;
}

struct ssd *
ssd_create(struct view *view, bool active)
{
	assert(view);
	struct ssd *ssd = znew(*ssd);

	ssd->view = view;
	ssd->tree = lab_wlr_scene_tree_create(view->scene_tree);

	/*
	 * Attach node_descriptor to the root node so that get_cursor_context()
	 * detect cursor hovering on borders and extents.
	 */
	node_descriptor_create(&ssd->tree->node,
		LAB_NODE_SSD_ROOT, view, /*data*/ NULL);

	wlr_scene_node_lower_to_bottom(&ssd->tree->node);
	ssd->titlebar.height = rc.theme->titlebar_height;
	ssd_shadow_create(ssd);
	ssd_extents_create(ssd);
	/*
	 * We need to create the borders after the titlebar because it sets
	 * ssd->state.squared which ssd_border_create() reacts to.
	 * TODO: Set the state here instead so the order does not matter
	 * anymore.
	 */
	ssd_titlebar_create(ssd);
	ssd_border_create(ssd);
	wlr_scene_node_raise_to_top(&ssd->titlebar.tree->node);
	if (!view_titlebar_visible(view)) {
		/* Ensure we keep the old state on Reconfigure or when exiting fullscreen */
		ssd_set_titlebar(ssd, false);
	}
	ssd->margin = ssd_thickness(view);
	ssd_set_active(ssd, active);
	ssd_enable_keybind_inhibit_indicator(ssd, view->inhibits_keybinds);
	ssd->state.geometry = view->current;
	ssd->state.was_resizable = view_is_resizable(view);

	return ssd;
}

struct border
ssd_get_margin(const struct ssd *ssd)
{
	return ssd ? ssd->margin : (struct border){ 0 };
}

int
ssd_get_corner_width(void)
{
	/* ensure a minimum corner width */
	return MAX(rc.corner_radius, 5);
}

void
ssd_update_margin(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}
	ssd->margin = ssd_thickness(ssd->view);
}

void
ssd_update_geometry(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}

	struct view *view = ssd->view;
	assert(view);

	struct wlr_box cached = ssd->state.geometry;
	struct wlr_box current = view->current;

	int eff_width = current.width;
	int eff_height = view_effective_height(view, /* use_pending */ false);
	bool resizable = view_is_resizable(view);

	bool update_area = eff_width != cached.width || eff_height != cached.height;
	bool update_extents = update_area
		|| current.x != cached.x || current.y != cached.y
		|| ssd->state.was_resizable != resizable;

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);

	bool state_changed = ssd->state.was_maximized != maximized
		|| ssd->state.was_shaded != view->shaded
		|| ssd->state.was_squared != squared
		|| ssd->state.was_resizable != resizable
		|| ssd->state.was_omnipresent != view->visible_on_all_workspaces;

	/*
	 * (Un)maximization updates titlebar visibility with
	 * maximizedDecoration=none
	 */
	ssd_set_titlebar(ssd, view_titlebar_visible(view));

	if (update_extents) {
		ssd_extents_update(ssd);
	}

	if (update_area || state_changed) {
		ssd_titlebar_update(ssd);
		ssd_border_update(ssd);
		ssd_shadow_update(ssd);
	}

	if (update_extents) {
		ssd->state.geometry = current;
	}
	ssd->state.was_resizable = resizable;
}

void
ssd_set_titlebar(struct ssd *ssd, bool enabled)
{
	if (!ssd || ssd->titlebar.tree->node.enabled == enabled) {
		return;
	}
	wlr_scene_node_set_enabled(&ssd->titlebar.tree->node, enabled);
	ssd->titlebar.height = enabled ? rc.theme->titlebar_height : 0;
	ssd_border_update(ssd);
	ssd_extents_update(ssd);
	ssd_shadow_update(ssd);
	ssd->margin = ssd_thickness(ssd->view);
}

void
ssd_destroy(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}

	/* Maybe reset hover view */
	struct view *view = ssd->view;
	if (server.hovered_button && node_view_from_node(
			server.hovered_button->node) == view) {
		server.hovered_button = NULL;
	}
	if (server.pressed_button && node_view_from_node(
			server.pressed_button->node) == view) {
		server.pressed_button = NULL;
	}

	/* Destroy subcomponents */
	ssd_titlebar_destroy(ssd);
	ssd_border_destroy(ssd);
	ssd_extents_destroy(ssd);
	ssd_shadow_destroy(ssd);
	wlr_scene_node_destroy(&ssd->tree->node);

	free(ssd);
}

enum lab_ssd_mode
ssd_mode_parse(const char *mode)
{
	if (!mode) {
		return LAB_SSD_MODE_INVALID;
	}
	if (!strcasecmp(mode, "none")) {
		return LAB_SSD_MODE_NONE;
	} else if (!strcasecmp(mode, "border")) {
		return LAB_SSD_MODE_BORDER;
	} else if (!strcasecmp(mode, "full")) {
		return LAB_SSD_MODE_FULL;
	} else {
		return LAB_SSD_MODE_INVALID;
	}
}

void
ssd_set_active(struct ssd *ssd, bool active)
{
	if (!ssd) {
		return;
	}
	enum ssd_active_state active_state;
	FOR_EACH_ACTIVE_STATE(active_state) {
		wlr_scene_node_set_enabled(
			&ssd->border.subtrees[active_state].tree->node,
			active == active_state);
		wlr_scene_node_set_enabled(
			&ssd->titlebar.subtrees[active_state].tree->node,
			active == active_state);
		if (ssd->shadow.subtrees[active_state].tree) {
			wlr_scene_node_set_enabled(
				&ssd->shadow.subtrees[active_state].tree->node,
				active == active_state);
		}
	}
}

void
ssd_enable_shade(struct ssd *ssd, bool enable)
{
	if (!ssd) {
		return;
	}
	ssd_titlebar_update(ssd);
	ssd_border_update(ssd);
	wlr_scene_node_set_enabled(&ssd->extents.tree->node, !enable);
	ssd_shadow_update(ssd);
}

void
ssd_enable_keybind_inhibit_indicator(struct ssd *ssd, bool enable)
{
	if (!ssd) {
		return;
	}

	struct ssd_border_subtree *subtree = &ssd->border.subtrees[SSD_ACTIVE];
	float *color = enable
		? rc.theme->window_toggled_keybinds_color
		: rc.theme->window[SSD_ACTIVE].border_color;

	/*
	 * The NsCDE titlebar frame no longer keeps a flat fallback top rect.
	 * Only the border-only path still uses simple top band scene-rects.
	 */
	if (subtree->outer_top) {
		wlr_scene_rect_set_color(subtree->outer_top, color);
	}
	if (subtree->inner_top) {
		wlr_scene_rect_set_color(subtree->inner_top, color);
	}
}

bool
ssd_debug_is_root_node(const struct ssd *ssd, struct wlr_scene_node *node)
{
	if (!ssd || !node) {
		return false;
	}
	return node == &ssd->tree->node;
}

const char *
ssd_debug_get_node_name(const struct ssd *ssd, struct wlr_scene_node *node)
{
	if (!ssd || !node) {
		return NULL;
	}
	if (node == &ssd->tree->node) {
		return "view->ssd";
	}
	if (node == &ssd->titlebar.subtrees[SSD_ACTIVE].tree->node) {
		return "titlebar.active";
	}
	if (node == &ssd->titlebar.subtrees[SSD_INACTIVE].tree->node) {
		return "titlebar.inactive";
	}
	if (node == &ssd->border.subtrees[SSD_ACTIVE].tree->node) {
		return "border.active";
	}
	if (node == &ssd->border.subtrees[SSD_INACTIVE].tree->node) {
		return "border.inactive";
	}
	if (node == &ssd->extents.tree->node) {
		return "extents";
	}
	if (ssd->handle.tree && node == &ssd->handle.tree->node) {
		return "handle";
	}
	return NULL;
}
