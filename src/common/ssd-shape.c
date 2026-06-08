// SPDX-License-Identifier: GPL-2.0-only

#include "ssd-shape.h"

#include <assert.h>
#include <cairo.h>
#include "buffer.h"
#include "common/macros.h"

struct shape_point {
	double x;
	double y;
};

static void
set_cairo_color(cairo_t *cairo, const float color[4])
{
	cairo_set_source_rgba(cairo,
		color[0], color[1], color[2], color[3]);
}

static int
shape_total_size(const struct ssd_shoulder_shape_spec *spec)
{
	assert(spec);
	return MAX(1, spec->branch_width) + MAX(0, spec->nested_square_size);
}

static void
transform_point(const struct ssd_shoulder_shape_spec *spec,
		double src_x, double src_y, double *dst_x, double *dst_y)
{
	int total = shape_total_size(spec);
	double x = src_x;
	double y = src_y;

	switch (spec->direction) {
	case SSD_SHAPE_LEFT_UPPER:
		break;
	case SSD_SHAPE_RIGHT_UPPER:
		x = total - src_x;
		break;
	case SSD_SHAPE_LEFT_BOTTOM:
		y = total - src_y;
		break;
	case SSD_SHAPE_RIGHT_BOTTOM:
		x = total - src_x;
		y = total - src_y;
		break;
	}

	*dst_x = x;
	*dst_y = y;
}

static void
shape_anchor_local_point(const struct ssd_shoulder_shape_spec *spec,
		double *anchor_x, double *anchor_y)
{
	int branch_width = MAX(1, spec->branch_width);
	int nested = MAX(0, spec->nested_square_size);

	switch (spec->direction) {
	case SSD_SHAPE_LEFT_UPPER:
		switch (spec->anchor_kind) {
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_TOP:
			*anchor_x = branch_width;
			*anchor_y = branch_width;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_TOP:
			*anchor_x = branch_width + nested;
			*anchor_y = branch_width;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_BOTTOM:
			*anchor_x = branch_width;
			*anchor_y = branch_width + nested;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_BOTTOM:
			*anchor_x = branch_width + nested;
			*anchor_y = branch_width + nested;
			break;
		}
		break;
	case SSD_SHAPE_RIGHT_UPPER:
		switch (spec->anchor_kind) {
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_TOP:
			*anchor_x = 0;
			*anchor_y = branch_width;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_TOP:
			*anchor_x = nested;
			*anchor_y = branch_width;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_BOTTOM:
			*anchor_x = 0;
			*anchor_y = branch_width + nested;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_BOTTOM:
			*anchor_x = nested;
			*anchor_y = branch_width + nested;
			break;
		}
		break;
	case SSD_SHAPE_LEFT_BOTTOM:
		switch (spec->anchor_kind) {
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_TOP:
			*anchor_x = branch_width;
			*anchor_y = 0;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_TOP:
			*anchor_x = branch_width + nested;
			*anchor_y = 0;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_BOTTOM:
			*anchor_x = branch_width;
			*anchor_y = nested;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_BOTTOM:
			*anchor_x = branch_width + nested;
			*anchor_y = nested;
			break;
		}
		break;
	case SSD_SHAPE_RIGHT_BOTTOM:
		switch (spec->anchor_kind) {
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_TOP:
			*anchor_x = 0;
			*anchor_y = 0;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_TOP:
			*anchor_x = nested;
			*anchor_y = 0;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_LEFT_BOTTOM:
			*anchor_x = 0;
			*anchor_y = nested;
			break;
		case SSD_SHAPE_ANCHOR_NESTED_RIGHT_BOTTOM:
			*anchor_x = nested;
			*anchor_y = nested;
			break;
		}
		break;
	}
}

static void
get_shoulder_points(const struct ssd_shoulder_shape_spec *spec,
		struct shape_point points[6])
{
	int branch_width = MAX(1, spec->branch_width);
	int total = shape_total_size(spec);
	struct shape_point canonical[6] = {
		{ 0, 0 },
		{ total, 0 },
		{ total, branch_width },
		{ branch_width, branch_width },
		{ branch_width, total },
		{ 0, total },
	};

	for (int i = 0; i < 6; i++) {
		transform_point(spec, canonical[i].x, canonical[i].y,
			&points[i].x, &points[i].y);
	}
}

static void
trace_shoulder_shape(cairo_t *cairo, const struct shape_point points[6])
{
	cairo_move_to(cairo, points[0].x, points[0].y);
	for (int i = 1; i < 6; i++) {
		cairo_line_to(cairo, points[i].x, points[i].y);
	}
	cairo_close_path(cairo);
}

static double
polygon_signed_area(const struct shape_point points[6])
{
	double area = 0;

	for (int i = 0; i < 6; i++) {
		const struct shape_point *a = &points[i];
		const struct shape_point *b = &points[(i + 1) % 6];

		area += a->x * b->y - b->x * a->y;
	}

	return area;
}

static bool
edge_is_highlight(const struct shape_point *a, const struct shape_point *b,
		double signed_area)
{
	double dx = b->x - a->x;
	double dy = b->y - a->y;
	double inward_x;
	double inward_y;
	double outward_x;
	double outward_y;

	if (signed_area >= 0) {
		inward_x = -dy;
		inward_y = dx;
	} else {
		inward_x = dy;
		inward_y = -dx;
	}

	outward_x = -inward_x;
	outward_y = -inward_y;

	if (fabs(outward_x) >= fabs(outward_y)) {
		return outward_x < 0;
	}

	return outward_y < 0;
}

static void
edge_inward_offset(const struct shape_point *a, const struct shape_point *b,
		double signed_area, int bevel_width,
		double *inward_x, double *inward_y)
{
	double dx = b->x - a->x;
	double dy = b->y - a->y;
	double x;
	double y;

	if (signed_area >= 0) {
		x = -dy;
		y = dx;
	} else {
		x = dy;
		y = -dx;
	}

	if (dx != 0) {
		x = 0;
		y = y < 0 ? -bevel_width : bevel_width;
	} else {
		x = x < 0 ? -bevel_width : bevel_width;
		y = 0;
	}

	*inward_x = x;
	*inward_y = y;
}

static void
draw_drop_shadow(cairo_t *cairo, const struct ssd_shoulder_shape_spec *spec,
		const struct shape_point points[6])
{
	if (!spec->style.draw_drop_shadow) {
		return;
	}

	cairo_save(cairo);
	cairo_translate(cairo,
		spec->style.drop_shadow_dx, spec->style.drop_shadow_dy);
	trace_shoulder_shape(cairo, points);
	set_cairo_color(cairo, spec->palette.drop_shadow);
	cairo_fill(cairo);
	cairo_restore(cairo);
}

static void
draw_fill(cairo_t *cairo, const struct ssd_shoulder_shape_spec *spec,
		const struct shape_point points[6])
{
	if (!spec->style.draw_fill) {
		return;
	}

	trace_shoulder_shape(cairo, points);
	set_cairo_color(cairo, spec->palette.fill);
	cairo_fill(cairo);
}

static void
draw_relief_edges(cairo_t *cairo, const struct ssd_shoulder_shape_spec *spec,
		const struct shape_point points[6])
{
	int bevel_width = MAX(1, spec->style.bevel_width);
	double signed_area = polygon_signed_area(points);

	trace_shoulder_shape(cairo, points);
	cairo_clip(cairo);

	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_NONE);

	for (int i = 0; i < 6; i++) {
		const struct shape_point *a = &points[i];
		const struct shape_point *b = &points[(i + 1) % 6];
		double inward_x;
		double inward_y;
		const float *color;

		edge_inward_offset(a, b, signed_area, bevel_width,
			&inward_x, &inward_y);

		color = edge_is_highlight(a, b, signed_area)
			? spec->palette.highlight
			: spec->palette.shadow;

		set_cairo_color(cairo, color);
		cairo_move_to(cairo, a->x, a->y);
		cairo_line_to(cairo, b->x, b->y);
		cairo_line_to(cairo, b->x + inward_x, b->y + inward_y);
		cairo_line_to(cairo, a->x + inward_x, a->y + inward_y);
		cairo_close_path(cairo);
		cairo_fill(cairo);
	}

	for (int i = 0; i < 6; i++) {
		const struct shape_point *prev = &points[(i + 5) % 6];
		const struct shape_point *curr = &points[i];
		const struct shape_point *next = &points[(i + 1) % 6];
		double prev_inward_x;
		double prev_inward_y;
		double next_inward_x;
		double next_inward_y;
		bool prev_highlight;
		bool next_highlight;
		const float *color;

		edge_inward_offset(prev, curr, signed_area, bevel_width,
			&prev_inward_x, &prev_inward_y);
		edge_inward_offset(curr, next, signed_area, bevel_width,
			&next_inward_x, &next_inward_y);
		prev_highlight = edge_is_highlight(prev, curr, signed_area);
		next_highlight = edge_is_highlight(curr, next, signed_area);
		color = (prev_highlight && next_highlight)
			? spec->palette.highlight
			: spec->palette.shadow;

		set_cairo_color(cairo, color);
		cairo_move_to(cairo, curr->x, curr->y);
		cairo_line_to(cairo,
			curr->x + prev_inward_x, curr->y + prev_inward_y);
		cairo_line_to(cairo,
			curr->x + prev_inward_x + next_inward_x,
			curr->y + prev_inward_y + next_inward_y);
		cairo_line_to(cairo,
			curr->x + next_inward_x, curr->y + next_inward_y);
		cairo_close_path(cairo);
		cairo_fill(cairo);
	}

	cairo_reset_clip(cairo);
}

static void
draw_outline(cairo_t *cairo, const struct ssd_shoulder_shape_spec *spec,
		const struct shape_point points[6])
{
	if (!spec->style.draw_outline) {
		return;
	}

	trace_shoulder_shape(cairo, points);
	set_cairo_color(cairo, spec->palette.shadow);
	cairo_stroke(cairo);
}

struct ssd_shape_placement
ssd_shoulder_shape_get_placement(const struct ssd_shoulder_shape_spec *spec)
{
	struct ssd_shape_placement placement = { 0 };
	double anchor_local_x;
	double anchor_local_y;
	int total;

	assert(spec);

	total = shape_total_size(spec);
	shape_anchor_local_point(spec, &anchor_local_x, &anchor_local_y);

	placement.x = spec->anchor_x - lround(anchor_local_x);
	placement.y = spec->anchor_y - lround(anchor_local_y);
	placement.width = total;
	placement.height = total;

	return placement;
}

struct lab_data_buffer *
ssd_shoulder_shape_render(const struct ssd_shoulder_shape_spec *spec,
		float scale)
{
	struct lab_data_buffer *buffer;
	struct ssd_shape_placement placement;
	struct shape_point points[6];
	cairo_t *cairo;

	assert(spec);

	placement = ssd_shoulder_shape_get_placement(spec);
	buffer = buffer_create_cairo(placement.width, placement.height, scale);
	if (!buffer) {
		return NULL;
	}

	get_shoulder_points(spec, points);

	cairo = cairo_create(buffer->surface);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_NONE);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cairo, 0, 0, 0, 0);
	cairo_paint(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);

	draw_drop_shadow(cairo, spec, points);
	draw_fill(cairo, spec, points);
	draw_relief_edges(cairo, spec, points);
	draw_outline(cairo, spec, points);

	cairo_surface_flush(buffer->surface);
	cairo_destroy(cairo);

	return buffer;
}
