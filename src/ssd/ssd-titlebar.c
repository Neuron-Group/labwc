// SPDX-License-Identifier: GPL-2.0-only

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_scene.h>
#include "buffer.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "scaled-buffer/scaled-img-buffer.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "ssd-shape.h"
#include "theme.h"
#include "view.h"

static void set_squared_corners(struct ssd *ssd, bool enable);
static void set_alt_button_icon(struct ssd *ssd, enum lab_node_type type, bool enable);
static void update_visible_buttons(struct ssd *ssd);

#define NSCDE_TITLE_TEXT_OFFSET 10
#define NSCDE_MIN_TITLE_TEXT_OFFSET 2

struct titlebar_layout {
	int content_x;
	int content_width;
	int bar_x;
	int bar_width;
	int button_cell_width;
	int button_spacing;
	int left_button_width;
	int right_button_width;
	int left_shoulder_x;
	int left_shoulder_width;
	int right_shoulder_x;
	int right_shoulder_width;
	int top_border_x;
	int top_border_width;
	int title_x;
	int title_width;
	int title_text_x;
	int title_text_width;
};

static int
get_titlebar_inner_height(const struct theme *theme)
{
	return MAX(theme->titlebar_height, 0);
}

static void
get_titlebar_content_inset(bool squared, int *inset)
{
	int value = squared ? 0 : rc.theme->window_titlebar_padding_width;
	if (inset) {
		*inset = value;
	}
}

static void
get_titlebar_button_cell_size(const struct theme *theme, int *width, int *height)
{
	int cell = get_titlebar_inner_height(theme);
	if (width) {
		*width = cell;
	}
	if (height) {
		*height = cell;
	}
}

static void
get_titlebar_text_geometry(struct titlebar_layout *layout)
{
	int inset = 0;

	if (layout->title_width <= 0) {
		layout->title_text_x = layout->title_x;
		layout->title_text_width = 0;
		return;
	}

	inset = (layout->title_width > 2 * NSCDE_TITLE_TEXT_OFFSET)
		? NSCDE_TITLE_TEXT_OFFSET
		: NSCDE_MIN_TITLE_TEXT_OFFSET;
	if (layout->title_width <= 2 * inset) {
		inset = layout->title_width / 2;
	}

	layout->title_text_x = layout->title_x + inset;
	layout->title_text_width = MAX(layout->title_width - 2 * inset, 0);
}

static void
get_titlebar_bar_geometry(struct ssd *ssd, bool squared, int *x, int *width)
{
	int view_width = ssd->view->current.width;
	int inset = 0;
	get_titlebar_content_inset(squared, &inset);
	int bar_x = inset;

	if (x) {
		*x = bar_x;
	}
	if (width) {
		*width = MAX(view_width - 2 * inset, 0);
	}
}

static void
get_titlebar_layout(struct ssd *ssd, bool squared, struct titlebar_layout *layout)
{
	struct theme *theme = rc.theme;
	int left_count = 0;
	int right_count = 0;

	memset(layout, 0, sizeof(*layout));
	get_titlebar_bar_geometry(ssd, squared,
		&layout->content_x, &layout->content_width);
	get_titlebar_button_cell_size(theme,
		&layout->button_cell_width, NULL);
	layout->button_spacing = theme->window_button_spacing;

	if (!ssd->titlebar.tree) {
		left_count = rc.nr_title_buttons_left;
		right_count = rc.nr_title_buttons_right;
	} else {
		struct ssd_titlebar_subtree *subtree =
			&ssd->titlebar.subtrees[SSD_ACTIVE];
		struct ssd_button *button;

		wl_list_for_each(button, &subtree->buttons_left, link) {
			if (button->node->enabled) {
				left_count++;
			}
		}
		wl_list_for_each(button, &subtree->buttons_right, link) {
			if (button->node->enabled) {
				right_count++;
			}
		}
	}

	if (left_count > 0) {
		layout->left_button_width = left_count * layout->button_cell_width
			+ (left_count - 1) * layout->button_spacing;
	}
	if (right_count > 0) {
		layout->right_button_width = right_count * layout->button_cell_width
			+ (right_count - 1) * layout->button_spacing;
	}

	layout->left_shoulder_x = -theme->border_width;
	layout->left_shoulder_width = MIN(layout->left_button_width,
		layout->button_cell_width) + theme->border_width;
	layout->right_shoulder_width = MIN(layout->right_button_width,
		layout->button_cell_width) + theme->border_width;
	layout->right_shoulder_x = layout->content_x + layout->content_width
		- layout->right_shoulder_width + theme->border_width;
	layout->bar_x = layout->content_x + layout->left_button_width;
	layout->bar_width = layout->content_width
		- layout->left_button_width - layout->right_button_width;
	layout->top_border_x = layout->content_x + layout->button_cell_width;
	layout->top_border_width = layout->content_width
		- 2 * layout->button_cell_width;
	layout->title_x = layout->bar_x;
	layout->title_width = layout->bar_width;
	if (layout->title_width < 0) {
		layout->title_width = 0;
	}
	if (layout->bar_width < 0) {
		layout->bar_width = 0;
	}
	if (layout->top_border_width < 0) {
		layout->top_border_width = 0;
	}
	if (layout->left_shoulder_width < 0) {
		layout->left_shoulder_width = 0;
	}
	if (layout->right_shoulder_width < 0) {
		layout->right_shoulder_width = 0;
	}
	get_titlebar_text_geometry(layout);
}

static void
update_titlebar_relief(struct ssd_titlebar_subtree *subtree,
		enum ssd_active_state active, struct titlebar_layout *layout)
{
	struct theme *theme = rc.theme;
	int bevel_w = theme->window[active].titlebar_bevel_width;
	int content_y = 0;
	int content_height = get_titlebar_inner_height(theme);
	if (subtree->left_shoulder_shape) {
		wlr_scene_node_set_enabled(&subtree->left_shoulder_shape->node,
			false);
	}
	if (subtree->right_shoulder_shape) {
		wlr_scene_node_set_enabled(&subtree->right_shoulder_shape->node,
			false);
	}
	if (subtree->left_shoulder_top_fill && subtree->left_shoulder_stile_fill) {
		wlr_scene_node_set_enabled(&subtree->left_shoulder_top_fill->node,
			false);
		wlr_scene_node_set_enabled(&subtree->left_shoulder_stile_fill->node,
			false);
		if (false) {
			wlr_scene_rect_set_size(subtree->left_shoulder_top_fill,
				layout->left_shoulder_width, theme->border_width);
			wlr_scene_node_set_position(
				&subtree->left_shoulder_top_fill->node,
				layout->left_shoulder_x, -theme->border_width);
			wlr_scene_rect_set_size(subtree->left_shoulder_stile_fill,
				theme->border_width, theme->titlebar_height);
			wlr_scene_node_set_position(
				&subtree->left_shoulder_stile_fill->node,
				layout->left_shoulder_x, 0);
		}
	}
	ssd_shoulder_relief_update(&subtree->left_shoulder,
		0, 0, 0, 0, 0, false, SSD_SHOULDER_TOP);
	if (subtree->right_shoulder_top_fill && subtree->right_shoulder_stile_fill) {
		wlr_scene_node_set_enabled(&subtree->right_shoulder_top_fill->node,
			false);
		wlr_scene_node_set_enabled(&subtree->right_shoulder_stile_fill->node,
			false);
		if (false) {
			wlr_scene_rect_set_size(subtree->right_shoulder_top_fill,
				layout->right_shoulder_width, theme->border_width);
			wlr_scene_node_set_position(
				&subtree->right_shoulder_top_fill->node,
				layout->right_shoulder_x, -theme->border_width);
			wlr_scene_rect_set_size(subtree->right_shoulder_stile_fill,
				theme->border_width, theme->titlebar_height);
			wlr_scene_node_set_position(
				&subtree->right_shoulder_stile_fill->node,
				layout->right_shoulder_x + layout->right_shoulder_width
					- theme->border_width,
				0);
		}
	}
	ssd_shoulder_relief_update(&subtree->right_shoulder,
		0, 0, 0, 0, 0, true, SSD_SHOULDER_TOP);
	if (subtree->top_border_fill) {
		wlr_scene_node_set_enabled(
			&subtree->top_border_fill->node, false);
	}
	ssd_compartment_relief_update(&subtree->top_border, 0, 0, 0);
	wlr_scene_node_set_position(&subtree->title_field.tree->node,
		layout->bar_x, content_y);
	ssd_compartment_relief_update(&subtree->title_field,
		layout->bar_width, content_height, bevel_w);

	int groove_w = theme->separator_groove_width;
	if (groove_w > 0 && subtree->groove_highlight) {
		wlr_scene_rect_set_size(subtree->groove_highlight,
			layout->bar_width, groove_w);
		wlr_scene_node_set_position(
			&subtree->groove_highlight->node, layout->bar_x,
			theme->titlebar_height);
		wlr_scene_rect_set_size(subtree->groove_shadow,
			layout->bar_width, groove_w);
		wlr_scene_node_set_position(
			&subtree->groove_shadow->node, layout->bar_x,
			theme->titlebar_height + groove_w);
	}
}

void
ssd_titlebar_create(struct ssd *ssd)
{
	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	struct titlebar_layout layout;
	get_titlebar_layout(ssd, false, &layout);

	ssd->titlebar.tree = lab_wlr_scene_tree_create(ssd->tree);
	node_descriptor_create(&ssd->titlebar.tree->node,
		LAB_NODE_TITLEBAR, view, /*data*/ NULL);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->titlebar.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		wlr_scene_node_set_position(&parent->node, 0, -theme->titlebar_height);

		struct wlr_buffer *titlebar_fill =
			&theme->window[active].titlebar_fill->base;

		/* Background */
		subtree->bar = lab_wlr_scene_buffer_create(parent, titlebar_fill);
		/*
		 * Work around the wlroots/pixman bug that widened 1px buffer
		 * becomes translucent when bilinear filtering is used.
		 * TODO: remove once https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3990
		 * is solved
		 */
		if (wlr_renderer_is_pixman(server.renderer)) {
			wlr_scene_buffer_set_filter_mode(
				subtree->bar, WLR_SCALE_FILTER_NEAREST);
		}
		wlr_scene_node_set_position(&subtree->bar->node,
			layout.bar_x, 0);
		subtree->left_shoulder_shape = lab_wlr_scene_buffer_create(parent, NULL);
		subtree->right_shoulder_shape = lab_wlr_scene_buffer_create(parent, NULL);

		subtree->left_shoulder_top_fill = lab_wlr_scene_rect_create(parent,
			1, 1, theme->window[active].title_bg.color);
		subtree->left_shoulder_stile_fill = lab_wlr_scene_rect_create(parent,
			1, 1, theme->window[active].title_bg.color);
		subtree->right_shoulder_top_fill = lab_wlr_scene_rect_create(parent,
			1, 1, theme->window[active].title_bg.color);
		subtree->right_shoulder_stile_fill = lab_wlr_scene_rect_create(parent,
			1, 1, theme->window[active].title_bg.color);
		subtree->top_border_fill = lab_wlr_scene_rect_create(parent,
			1, 1, theme->window[active].title_bg.color);

		/* Titlebar bevel lines (CDE/Motif relief) */
		float *hl = theme->window[active].titlebar_bevel_highlight_color;
		float *sh = theme->window[active].titlebar_bevel_shadow_color;
		ssd_shoulder_relief_create(parent, &subtree->left_shoulder,
			hl, sh, SSD_RELIEF_MODE_RELIEF,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_LEFT,
			SSD_RELIEF_FACE_RIGHT, SSD_RELIEF_FACE_RIGHT,
			SSD_RELIEF_FACE_DOWN, SSD_RELIEF_FACE_DOWN);
		ssd_shoulder_relief_create(parent, &subtree->right_shoulder,
			hl, sh, SSD_RELIEF_MODE_RELIEF,
			SSD_RELIEF_FACE_UP, SSD_RELIEF_FACE_RIGHT,
			SSD_RELIEF_FACE_LEFT, SSD_RELIEF_FACE_LEFT,
			SSD_RELIEF_FACE_DOWN, SSD_RELIEF_FACE_DOWN);
		ssd_compartment_relief_create(parent, &subtree->top_border,
			hl, sh, SSD_RELIEF_MODE_RELIEF);
		ssd_compartment_relief_create(parent, &subtree->title_field,
			hl, sh, SSD_RELIEF_MODE_RELIEF);
		update_titlebar_relief(subtree, active, &layout);

		/* Separator groove (P6): thin line between titlebar and client */
		int groove_w = theme->separator_groove_width;
		if (groove_w > 0) {
			float *groove_hl = theme->separator_groove_highlight_color;
			float *groove_sh = theme->separator_groove_shadow_color;
			subtree->groove_highlight = lab_wlr_scene_rect_create(
				parent, layout.bar_width, groove_w, groove_hl);
			subtree->groove_shadow = lab_wlr_scene_rect_create(
				parent, layout.bar_width, groove_w, groove_sh);
		} else {
			subtree->groove_highlight = NULL;
			subtree->groove_shadow = NULL;
		}

		/* Title */
		subtree->title = scaled_font_buffer_create(subtree->tree);
		assert(subtree->title);
		node_descriptor_create(&subtree->title->scene_buffer->node,
			LAB_NODE_TITLE, view, /*data*/ NULL);

		/* Buttons */
		int x = layout.content_x;

		/* FVWM title buttons occupy full-height square cells. */
		int y = 0;

		wl_list_init(&subtree->buttons_left);
		wl_list_init(&subtree->buttons_right);

		for (int b = 0; b < rc.nr_title_buttons_left; b++) {
			enum lab_node_type type = rc.title_buttons_left[b];
			struct lab_img **imgs =
				theme->window[active].button_imgs[type];
			attach_ssd_button(&subtree->buttons_left, type, parent,
				imgs, x, y, view);
			x += layout.button_cell_width + layout.button_spacing;
		}

		x = layout.content_x + layout.content_width + layout.button_spacing;
		for (int b = rc.nr_title_buttons_right - 1; b >= 0; b--) {
			x -= layout.button_cell_width + layout.button_spacing;
			enum lab_node_type type = rc.title_buttons_right[b];
			struct lab_img **imgs =
				theme->window[active].button_imgs[type];
			attach_ssd_button(&subtree->buttons_right, type, parent,
				imgs, x, y, view);
		}
	}

	update_visible_buttons(ssd);

	ssd_update_title(ssd);

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);
	if (maximized) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_MAXIMIZE, true);
		ssd->state.was_maximized = true;
	}
	if (squared) {
		ssd->state.was_squared = true;
	}
	set_squared_corners(ssd, maximized || squared);

	if (view->shaded) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_SHADE, true);
	}

	if (view->visible_on_all_workspaces) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_OMNIPRESENT, true);
	}
}

static void
update_button_state(struct ssd_button *button, enum lab_button_state state,
		bool enable)
{
	if (enable) {
		button->state_set |= state;
	} else {
		button->state_set &= ~state;
	}
	/* Switch the displayed icon buffer to the new one */
	for (uint8_t state_set = LAB_BS_DEFAULT;
			state_set <= LAB_BS_ALL; state_set++) {
		struct scaled_img_buffer *buffer = button->img_buffers[state_set];
		if (!buffer) {
			continue;
		}
		wlr_scene_node_set_enabled(&buffer->scene_buffer->node,
			state_set == button->state_set);
	}
}

static void
set_squared_corners(struct ssd *ssd, bool enable)
{
	struct theme *theme = rc.theme;
	struct titlebar_layout layout;
	get_titlebar_layout(ssd, enable, &layout);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];

		wlr_scene_node_set_position(&subtree->bar->node,
			layout.bar_x, 0);
		wlr_scene_buffer_set_dest_size(subtree->bar,
			layout.bar_width, get_titlebar_inner_height(theme));
		update_titlebar_relief(subtree, active, &layout);
	}
}

static void
set_alt_button_icon(struct ssd *ssd, enum lab_node_type type, bool enable)
{
	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];

		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			if (button->type == type) {
				update_button_state(button,
					LAB_BS_TOGGLED, enable);
			}
		}
		wl_list_for_each(button, &subtree->buttons_right, link) {
			if (button->type == type) {
				update_button_state(button,
					LAB_BS_TOGGLED, enable);
			}
		}
	}
}

/*
 * Usually this function just enables all the nodes for buttons, but some
 * buttons can be hidden for small windows (e.g. xterm -geometry 1x1).
 */
static void
update_visible_buttons(struct ssd *ssd)
{
	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	int inset;
	get_titlebar_content_inset(ssd->state.was_maximized || ssd->state.was_squared,
		&inset);
	int width = MAX(view->current.width - 2 * inset, 0);
	int button_width;
	get_titlebar_button_cell_size(theme, &button_width, NULL);
	int button_spacing = theme->window_button_spacing;
	int button_count_left = rc.nr_title_buttons_left;
	int button_count_right = rc.nr_title_buttons_right;

	/* Make sure infinite loop never occurs */
	assert(button_width > 0);

	/*
	 * The corner-left button is lastly removed as it's usually a window
	 * menu button (or an app icon button in the future).
	 *
	 * There is spacing to the inside of each button, including between the
	 * innermost buttons and the window title. See also get_title_offsets().
	 */
	while (width < ((button_width + button_spacing)
			* (button_count_left + button_count_right))) {
		if (button_count_left > button_count_right) {
			button_count_left--;
		} else {
			button_count_right--;
		}
	}

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		int button_count = 0;

		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			wlr_scene_node_set_enabled(button->node,
				button_count < button_count_left);
			button_count++;
		}

		button_count = 0;
		wl_list_for_each(button, &subtree->buttons_right, link) {
			wlr_scene_node_set_enabled(button->node,
				button_count < button_count_right);
			button_count++;
		}
	}
}

void
ssd_titlebar_update(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int width = view->current.width;
	struct theme *theme = rc.theme;

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);

	if (ssd->state.was_maximized != maximized
			|| ssd->state.was_squared != squared) {
		set_squared_corners(ssd, maximized || squared);
		if (ssd->state.was_maximized != maximized) {
			set_alt_button_icon(ssd, LAB_NODE_BUTTON_MAXIMIZE, maximized);
		}
		ssd->state.was_maximized = maximized;
		ssd->state.was_squared = squared;
	}

	if (ssd->state.was_shaded != view->shaded) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_SHADE, view->shaded);
		ssd->state.was_shaded = view->shaded;
	}

	if (ssd->state.was_omnipresent != view->visible_on_all_workspaces) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_OMNIPRESENT,
			view->visible_on_all_workspaces);
		ssd->state.was_omnipresent = view->visible_on_all_workspaces;
	}

	if (width == ssd->state.geometry.width) {
		return;
	}

	update_visible_buttons(ssd);

	struct titlebar_layout layout;
	get_titlebar_layout(ssd, maximized || squared, &layout);
	int y = 0;
	int x;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		wlr_scene_buffer_set_dest_size(subtree->bar,
			layout.bar_width, get_titlebar_inner_height(theme));
		wlr_scene_node_set_position(&subtree->bar->node,
			layout.bar_x, 0);
		update_titlebar_relief(subtree, active, &layout);

		x = layout.content_x;
		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			wlr_scene_node_set_position(button->node, x, y);
			x += layout.button_cell_width + layout.button_spacing;
		}

		x = layout.content_x + layout.content_width + layout.button_spacing;
		wl_list_for_each(button, &subtree->buttons_right, link) {
			x -= layout.button_cell_width + layout.button_spacing;
			wlr_scene_node_set_position(button->node, x, y);
		}
	}

	ssd_update_title(ssd);
}

void
ssd_titlebar_destroy(struct ssd *ssd)
{
	if (!ssd->titlebar.tree) {
		return;
	}

	zfree(ssd->state.title.text);
	wlr_scene_node_destroy(&ssd->titlebar.tree->node);
	ssd->titlebar = (struct ssd_titlebar_scene){0};
}

/*
 * For ssd_update_title* we do not early out because
 * .active and .inactive may result in different sizes
 * of the title (font family/size) or background of
 * the title (different button/border width).
 *
 * Both, wlr_scene_node_set_enabled() and wlr_scene_node_set_position()
 * check for actual changes and return early if there is no change in state.
 * Always using wlr_scene_node_set_enabled(node, true) will thus not cause
 * any unnecessary screen damage and makes the code easier to follow.
 */

static void
ssd_update_title_positions(struct ssd *ssd)
{
	struct theme *theme = rc.theme;
	struct titlebar_layout layout;
	get_titlebar_layout(ssd,
		ssd->state.was_maximized || ssd->state.was_squared, &layout);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		struct scaled_font_buffer *title = subtree->title;
		int x, y;

		x = layout.title_text_x;
		y = (get_titlebar_inner_height(theme) - title->height) / 2;

		if (layout.title_width <= 0) {
			wlr_scene_node_set_enabled(&title->scene_buffer->node, false);
			continue;
		}
		wlr_scene_node_set_enabled(&title->scene_buffer->node, true);

		if (theme->window_label_text_justify == LAB_JUSTIFY_CENTER) {
			x += (layout.title_text_width - title->width) / 2;
		} else if (theme->window_label_text_justify == LAB_JUSTIFY_RIGHT) {
			x += layout.title_text_width - title->width;
		} else if (theme->window_label_text_justify == LAB_JUSTIFY_LEFT) {
			/* TODO: maybe add some theme x padding here? */
		}
		wlr_scene_node_set_position(&title->scene_buffer->node, x, y);
	}
}

/*
 * Get left/right offsets of the title area based on visible/hidden states of
 * buttons set in update_visible_buttons().
 */
static void
get_title_offsets(struct ssd *ssd, int *offset_left, int *offset_right)
{
	struct view *view = ssd->view;
	struct titlebar_layout layout;
	get_titlebar_layout(ssd,
		ssd->state.was_maximized || ssd->state.was_squared, &layout);
	*offset_left = layout.title_text_x;
	*offset_right = view->current.width
		- (layout.title_text_x + layout.title_text_width);
}

void
ssd_update_title(struct ssd *ssd)
{
	if (!ssd || !rc.show_title) {
		return;
	}

	struct view *view = ssd->view;
	/* view->title is never NULL (instead it can be an empty string) */
	assert(view->title);

	struct theme *theme = rc.theme;
	struct ssd_state_title *state = &ssd->state.title;
	bool title_unchanged = state->text && !strcmp(view->title, state->text);

	int offset_left, offset_right;
	get_title_offsets(ssd, &offset_left, &offset_right);
	int title_bg_width = view->current.width - offset_left - offset_right;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		struct ssd_state_title_width *dstate = &state->dstates[active];
		const float *text_color = theme->window[active].label_text_color;
		struct font *font = active ?
			&rc.font_activewindow : &rc.font_inactivewindow;

		if (title_bg_width <= 0) {
			dstate->truncated = true;
			continue;
		}

		if (title_unchanged
				&& !dstate->truncated && dstate->width < title_bg_width) {
			/* title the same + we don't need to resize title */
			continue;
		}

		const float bg_color[4] = {0, 0, 0, 0}; /* ignored */
		scaled_font_buffer_update(subtree->title, view->title,
			title_bg_width, font,
			text_color, bg_color);

		/* And finally update the cache */
		dstate->width = subtree->title->width;
		dstate->truncated = title_bg_width <= dstate->width;
	}

	if (!title_unchanged) {
		xstrdup_replace(state->text, view->title);
	}
	ssd_update_title_positions(ssd);
}

void
ssd_update_hovered_button(struct wlr_scene_node *node)
{
	struct ssd_button *button = NULL;

	if (node && node->data) {
		button = node_try_ssd_button_from_node(node);
		if (button == server.hovered_button) {
			/* Cursor is still on the same button */
			return;
		}
	}

	/* Disable old hover */
	if (server.hovered_button) {
		update_button_state(server.hovered_button, LAB_BS_HOVERED, false);
	}
	server.hovered_button = button;
	if (button) {
		update_button_state(button, LAB_BS_HOVERED, true);
	}
}

void
ssd_update_pressed_button(struct wlr_scene_node *node)
{
	struct ssd_button *button = NULL;

	if (node && node->data) {
		button = node_try_ssd_button_from_node(node);
	}

	if (server.pressed_button == button) {
		return;
	}

	if (server.pressed_button) {
		ssd_button_set_pressed(server.pressed_button, false);
	}
	server.pressed_button = button;
	if (button) {
		ssd_button_set_pressed(button, true);
	}
}

bool
ssd_should_be_squared(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int corner_width = ssd_get_corner_width();

	return (view_is_tiled_and_notify_tiled(view)
			|| view->current.width < corner_width * 2)
		&& view->maximized != VIEW_AXIS_BOTH;
}
