/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SSD_SHAPE_H
#define LABWC_SSD_SHAPE_H

#include <stdbool.h>

struct lab_data_buffer;

enum ssd_shape_direction {
	SSD_SHAPE_RIGHT_UPPER,
	SSD_SHAPE_LEFT_UPPER,
	SSD_SHAPE_RIGHT_BOTTOM,
	SSD_SHAPE_LEFT_BOTTOM,
};

enum ssd_shape_anchor {
	SSD_SHAPE_ANCHOR_NESTED_LEFT_TOP,
	SSD_SHAPE_ANCHOR_NESTED_RIGHT_TOP,
	SSD_SHAPE_ANCHOR_NESTED_LEFT_BOTTOM,
	SSD_SHAPE_ANCHOR_NESTED_RIGHT_BOTTOM,
};

struct ssd_shape_palette {
	float fill[4];
	float highlight[4];
	float shadow[4];
	float drop_shadow[4];
};

struct ssd_shape_style {
	int bevel_width;
	bool draw_fill;
	bool draw_outline;
	bool draw_drop_shadow;
	int drop_shadow_dx;
	int drop_shadow_dy;
};

struct ssd_shoulder_shape_spec {
	enum ssd_shape_direction direction;
	enum ssd_shape_anchor anchor_kind;
	int anchor_x;
	int anchor_y;
	int branch_width;
	int nested_square_size;
	struct ssd_shape_palette palette;
	struct ssd_shape_style style;
};

struct ssd_shape_placement {
	int x;
	int y;
	int width;
	int height;
};

struct ssd_shape_placement ssd_shoulder_shape_get_placement(
	const struct ssd_shoulder_shape_spec *spec);

struct lab_data_buffer *ssd_shoulder_shape_render(
	const struct ssd_shoulder_shape_spec *spec, float scale);

#endif /* LABWC_SSD_SHAPE_H */
