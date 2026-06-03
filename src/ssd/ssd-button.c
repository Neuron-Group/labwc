// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <float.h>
#include <wlr/types/wlr_scene.h>
#include "common/macros.h"
#include "config/rcxml.h"
#include "common/list.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "node.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "scaled-buffer/scaled-img-buffer.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"

static bool
button_has_bevel(void)
{
	float *bg = rc.theme->button_bg_color;
	return bg[0] != FLT_MIN && bg[3] > 0.0f;
}

static void
get_button_cell_geometry(int *button_width, int *button_height)
{
	int cell = MAX(rc.theme->titlebar_height, 0);
	if (button_width) {
		*button_width = cell;
	}
	if (button_height) {
		*button_height = cell;
	}
}

static void
get_button_content_geometry(enum lab_node_type type,
		int cell_width, int cell_height,
		int *content_x, int *content_y,
		int *content_width, int *content_height)
{
	int button_width = MIN(rc.theme->window_button_width, cell_width);
	int button_height = MIN(rc.theme->window_button_height, cell_height);
	int extent = MIN(button_width, button_height);
	int inset = MAX(extent / 5, 2);
	int origin_x = (cell_width - button_width) / 2;
	int origin_y = (cell_height - button_height) / 2;

	if (type == LAB_NODE_BUTTON_WINDOW_ICON) {
		int width = MAX(button_width - 2 * inset, 1);
		int height = MAX(button_height - 2 * inset, 1);
		if (content_x) {
			*content_x = origin_x + (button_width - width) / 2;
		}
		if (content_y) {
			*content_y = origin_y + (button_height - height) / 2;
		}
		if (content_width) {
			*content_width = width;
		}
		if (content_height) {
			*content_height = height;
		}
		return;
	}

	int glyph = MAX((extent * 2) / 3, 1);
	if (content_x) {
		*content_x = origin_x + (button_width - glyph) / 2;
	}
	if (content_y) {
		*content_y = origin_y + (button_height - glyph) / 2;
	}
	if (content_width) {
		*content_width = glyph;
	}
	if (content_height) {
		*content_height = glyph;
	}
}

static void
update_button_bevel(struct ssd_button *button)
{
	if (!button || !button->bg_fill) {
		return;
	}

	struct theme *theme = rc.theme;
	const float *fill = button->pressed
		? theme->button_pressed_bg_color
		: theme->button_bg_color;
	wlr_scene_rect_set_color(button->bg_fill, fill);

	if (button->bg_top) {
		const float *top_left = button->pressed
			? theme->button_pressed_bg_highlight_color
			: theme->button_bg_highlight_color;
		const float *bottom_right = button->pressed
			? theme->button_pressed_bg_shadow_color
			: theme->button_bg_shadow_color;
		wlr_scene_rect_set_color(button->bg_top, top_left);
		wlr_scene_rect_set_color(button->bg_left, top_left);
		wlr_scene_rect_set_color(button->bg_bottom, bottom_right);
		wlr_scene_rect_set_color(button->bg_right, bottom_right);
	}

	if (button->content_tree) {
		wlr_scene_node_set_position(&button->content_tree->node,
			button->content_x + (button->pressed ? 1 : 0),
			button->content_y + (button->pressed ? 1 : 0));
	}
}

/* Internal API */

struct ssd_button *
attach_ssd_button(struct wl_list *button_parts, enum lab_node_type type,
		struct wlr_scene_tree *parent,
		struct lab_img *imgs[LAB_BS_ALL + 1],
		int x, int y, struct view *view)
{
	struct wlr_scene_tree *root = lab_wlr_scene_tree_create(parent);
	wlr_scene_node_set_position(&root->node, x, y);

	assert(node_type_contains(LAB_NODE_BUTTON, type));
	struct ssd_button *button = znew(*button);
	button->node = &root->node;
	button->type = type;
	node_descriptor_create(&root->node, type, view, button);
	wl_list_append(button_parts, &button->link);

	/* Hitbox / Button bevel background (P5) */
	int button_width;
	int button_height;
	get_button_cell_geometry(&button_width, &button_height);
	bool has_bevel = button_has_bevel();
	float invisible[4] = { 0, 0, 0, 0 };
	if (has_bevel) {
		button->bg_fill = lab_wlr_scene_rect_create(root,
			MAX(button_width - 2, 0), MAX(button_height - 2, 0),
			invisible);
		wlr_scene_node_set_position(&button->bg_fill->node, 1, 1);
		button->bg_top = lab_wlr_scene_rect_create(root,
			MAX(button_width - 1, 0), 1, invisible);
		button->bg_left = lab_wlr_scene_rect_create(root,
			1, MAX(button_height - 1, 0), invisible);
		wlr_scene_node_set_position(&button->bg_left->node, 0, 1);
		button->bg_bottom = lab_wlr_scene_rect_create(root,
			MAX(button_width - 1, 0), 1, invisible);
		wlr_scene_node_set_position(&button->bg_bottom->node, 1,
			button_height - 1);
		button->bg_right = lab_wlr_scene_rect_create(root,
			1, MAX(button_height - 1, 0), invisible);
		wlr_scene_node_set_position(&button->bg_right->node,
			button_width - 1, 0);
	} else {
		button->bg_fill = lab_wlr_scene_rect_create(root,
			button_width, button_height, invisible);
	}
	button->content_tree = lab_wlr_scene_tree_create(root);

	int content_x;
	int content_y;
	int content_width;
	int content_height;
	get_button_content_geometry(type, button_width, button_height,
		&content_x, &content_y, &content_width, &content_height);
	button->content_x = 0;
	button->content_y = 0;

	if (type == LAB_NODE_BUTTON_WINDOW_ICON) {
		struct scaled_icon_buffer *icon_buffer =
			scaled_icon_buffer_create(button->content_tree,
				content_width, content_height);
		assert(icon_buffer);
		struct wlr_scene_node *icon_node = &icon_buffer->scene_buffer->node;
		scaled_icon_buffer_set_view(icon_buffer, view);
		wlr_scene_node_set_position(icon_node, content_x, content_y);
		button->window_icon = icon_buffer;
	} else {
		for (uint8_t state_set = LAB_BS_DEFAULT;
				state_set <= LAB_BS_ALL; state_set++) {
			if (!imgs[state_set]) {
				continue;
			}
			struct scaled_img_buffer *img_buffer = scaled_img_buffer_create(
				button->content_tree, imgs[state_set],
				content_width, content_height,
				LAB_SCALE_FILTER_NEAREST);
			assert(img_buffer);
			struct wlr_scene_node *icon_node = &img_buffer->scene_buffer->node;
			wlr_scene_node_set_position(icon_node, content_x, content_y);
			wlr_scene_node_set_enabled(icon_node, false);
			button->img_buffers[state_set] = img_buffer;
		}
		/* Initially show non-hover, non-toggled, unrounded variant */
		wlr_scene_node_set_enabled(
			&button->img_buffers[LAB_BS_DEFAULT]->scene_buffer->node, true);
	}
	update_button_bevel(button);

	return button;
}

void
ssd_button_set_pressed(struct ssd_button *button, bool pressed)
{
	if (!button || button->pressed == pressed) {
		return;
	}
	button->pressed = pressed;
	update_button_bevel(button);
}

/* called from node descriptor destroy */
void ssd_button_free(struct ssd_button *button)
{
	wl_list_remove(&button->link);
	free(button);
}
