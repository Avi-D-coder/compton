#include <xcb/xcb_image.h>

#include "backend_common.h"

static xcb_image_t *
generate_shadow_data(session_t *ps, double opacity, int width, int height) {
	xcb_image_t *ximage;
	int ylimit, xlimit;
	int swidth = width + ps->cgsize;
	int sheight = height + ps->cgsize;
	int center = ps->cgsize / 2;
	int x, y;
	unsigned char d;
	int x_diff;
	int opacity_int = (int)(opacity * 25);

	ximage = xcb_image_create_native(ps->c, swidth, sheight,
	                                 XCB_IMAGE_FORMAT_Z_PIXMAP, 8, 0, 0, NULL);

	if (!ximage) {
		log_error("Failed to create an X image");
		return 0;
	}

	unsigned char *data = ximage->data;
	uint32_t sstride = ximage->stride;

	/*
	 * Build the gaussian in sections
	 */

	/*
	 * center (fill the complete data array)
	 */

	// XXX If the center part of the shadow would be entirely covered by
	// the body of the window, we shouldn't need to fill the center here.
	// XXX In general, we want to just fill the part that is not behind
	// the window, in order to reduce CPU load and make transparent window
	// look correct
	if (ps->cgsize > 0) {
		d = ps->shadow_top[opacity_int * (ps->cgsize + 1) + ps->cgsize];
	} else {
		d = sum_gaussian(ps->gaussian_map, opacity, center, center, width, height);
	}
	memset(data, d, sheight * swidth);

	/*
	 * corners
	 */

	ylimit = ps->cgsize;
	if (ylimit > sheight / 2)
		ylimit = (sheight + 1) / 2;

	xlimit = ps->cgsize;
	if (xlimit > swidth / 2)
		xlimit = (swidth + 1) / 2;

	for (y = 0; y < ylimit; y++) {
		for (x = 0; x < xlimit; x++) {
			if (xlimit == ps->cgsize && ylimit == ps->cgsize) {
				d = ps->shadow_corner[opacity_int * (ps->cgsize + 1) *
				                          (ps->cgsize + 1) +
				                      y * (ps->cgsize + 1) + x];
			} else {
				d = sum_gaussian(ps->gaussian_map, opacity, x - center,
				                 y - center, width, height);
			}
			data[y * sstride + x] = d;
			data[(sheight - y - 1) * sstride + x] = d;
			data[(sheight - y - 1) * sstride + (swidth - x - 1)] = d;
			data[y * sstride + (swidth - x - 1)] = d;
		}
	}

	/*
	 * top/bottom
	 */

	x_diff = swidth - (ps->cgsize * 2);
	if (x_diff > 0 && ylimit > 0) {
		for (y = 0; y < ylimit; y++) {
			if (ylimit == ps->cgsize) {
				d = ps->shadow_top[opacity_int * (ps->cgsize + 1) + y];
			} else {
				d = sum_gaussian(ps->gaussian_map, opacity, center,
				                 y - center, width, height);
			}
			memset(&data[y * sstride + ps->cgsize], d, x_diff);
			memset(&data[(sheight - y - 1) * sstride + ps->cgsize], d, x_diff);
		}
	}

	/*
	 * sides
	 */

	for (x = 0; x < xlimit; x++) {
		if (xlimit == ps->cgsize) {
			d = ps->shadow_top[opacity_int * (ps->cgsize + 1) + x];
		} else {
			d = sum_gaussian(ps->gaussian_map, opacity, x - center, center,
			                 width, height);
		}
		for (y = ps->cgsize; y < sheight - ps->cgsize; y++) {
			data[y * sstride + x] = d;
			data[y * sstride + (swidth - x - 1)] = d;
		}
	}

	return ximage;
}

/**
 * Generate a 1x1 <code>Picture</code> of a particular color.
 */
xcb_render_picture_t
solid_picture(session_t *ps, bool argb, double a, double r, double g, double b) {
	xcb_pixmap_t pixmap;
	xcb_render_picture_t picture;
	xcb_render_create_picture_value_list_t pa;
	xcb_render_color_t col;
	xcb_rectangle_t rect;

	pixmap = x_create_pixmap(ps, argb ? 32 : 8, ps->root, 1, 1);
	if (!pixmap)
		return None;

	pa.repeat = True;
	picture = x_create_picture_with_standard_and_pixmap(
	    ps, argb ? XCB_PICT_STANDARD_ARGB_32 : XCB_PICT_STANDARD_A_8, pixmap,
	    XCB_RENDER_CP_REPEAT, &pa);

	if (!picture) {
		xcb_free_pixmap(ps->c, pixmap);
		return None;
	}

	col.alpha = a * 0xffff;
	col.red = r * 0xffff;
	col.green = g * 0xffff;
	col.blue = b * 0xffff;

	rect.x = 0;
	rect.y = 0;
	rect.width = 1;
	rect.height = 1;

	xcb_render_fill_rectangles(ps->c, XCB_RENDER_PICT_OP_SRC, picture, col, 1, &rect);
	xcb_free_pixmap(ps->c, pixmap);

	return picture;
}

/**
 * Generate shadow <code>Picture</code> for a window.
 */
bool build_shadow(session_t *ps, double opacity, const int width, const int height,
                  xcb_render_picture_t shadow_pixel, xcb_pixmap_t *pixmap,
                  xcb_render_picture_t *pict) {
	xcb_image_t *shadow_image = NULL;
	xcb_pixmap_t shadow_pixmap = None, shadow_pixmap_argb = None;
	xcb_render_picture_t shadow_picture = None, shadow_picture_argb = None;
	xcb_gcontext_t gc = None;

	shadow_image = generate_shadow_data(ps, opacity, width, height);
	if (!shadow_image) {
		log_error("Failed to make shadow");
		return false;
	}

	shadow_pixmap =
	    x_create_pixmap(ps, 8, ps->root, shadow_image->width, shadow_image->height);
	shadow_pixmap_argb =
	    x_create_pixmap(ps, 32, ps->root, shadow_image->width, shadow_image->height);

	if (!shadow_pixmap || !shadow_pixmap_argb) {
		log_error("Failed to create shadow pixmaps");
		goto shadow_picture_err;
	}

	shadow_picture = x_create_picture_with_standard_and_pixmap(
	    ps, XCB_PICT_STANDARD_A_8, shadow_pixmap, 0, NULL);
	shadow_picture_argb = x_create_picture_with_standard_and_pixmap(
	    ps, XCB_PICT_STANDARD_ARGB_32, shadow_pixmap_argb, 0, NULL);
	if (!shadow_picture || !shadow_picture_argb)
		goto shadow_picture_err;

	gc = xcb_generate_id(ps->c);
	xcb_create_gc(ps->c, gc, shadow_pixmap, 0, NULL);

	xcb_image_put(ps->c, shadow_pixmap, gc, shadow_image, 0, 0, 0);
	xcb_render_composite(ps->c, XCB_RENDER_PICT_OP_SRC, shadow_pixel, shadow_picture,
	                     shadow_picture_argb, 0, 0, 0, 0, 0, 0, shadow_image->width,
	                     shadow_image->height);

	*pixmap = shadow_pixmap_argb;
	*pict = shadow_picture_argb;

	xcb_free_gc(ps->c, gc);
	xcb_image_destroy(shadow_image);
	xcb_free_pixmap(ps->c, shadow_pixmap);
	xcb_render_free_picture(ps->c, shadow_picture);

	return true;

shadow_picture_err:
	if (shadow_image)
		xcb_image_destroy(shadow_image);
	if (shadow_pixmap)
		xcb_free_pixmap(ps->c, shadow_pixmap);
	if (shadow_pixmap_argb)
		xcb_free_pixmap(ps->c, shadow_pixmap_argb);
	if (shadow_picture)
		xcb_render_free_picture(ps->c, shadow_picture);
	if (shadow_picture_argb)
		xcb_render_free_picture(ps->c, shadow_picture_argb);
	if (gc)
		xcb_free_gc(ps->c, gc);

	return false;
}
