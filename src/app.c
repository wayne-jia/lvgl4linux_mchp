/*
 * Copyright (C) 2026 Microchip Technology Inc.  All rights reserved.
 *   Wayne Jia <wayne.jia@microchip.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This application uses libdrm directly to allocate planes for LVGL using.
 */
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>
#include <cairo.h>
#include <drm_fourcc.h>
#include "planes/engine.h"
#include "planes/kms.h"
#include "p_kms.h"

static struct kms_device *device = NULL;

static void exit_handler(int s) {
	kms_device_close(device);
	exit(1);
}

static cairo_format_t drm2cairo(uint32_t format)
{
	switch (format)
	{
	case DRM_FORMAT_RGB565:
		return CAIRO_FORMAT_RGB16_565;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return CAIRO_FORMAT_ARGB32;
	}
	return CAIRO_FORMAT_INVALID;
}

static void draw_mesh(cairo_t* cr, int width, int height)
{
	cairo_pattern_t *pattern;
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_translate(cr, 0, 0);
	pattern = cairo_pattern_create_mesh();
	cairo_mesh_pattern_begin_patch(pattern);
	cairo_mesh_pattern_move_to(pattern,    0,    0);
	cairo_mesh_pattern_line_to(pattern, width,    0);
	cairo_mesh_pattern_line_to(pattern, width, height);
	cairo_mesh_pattern_line_to(pattern,    0, height);
	cairo_mesh_pattern_set_corner_color_rgb(pattern, 0, 1, 0, 0);
	cairo_mesh_pattern_set_corner_color_rgb(pattern, 1, 0, 1, 0);
	cairo_mesh_pattern_set_corner_color_rgb(pattern, 2, 0, 0, 1);
	cairo_mesh_pattern_set_corner_color_rgb(pattern, 3, 1, 1, 0);
	cairo_mesh_pattern_set_control_point(pattern, 0, width * .7, height * .7);
	cairo_mesh_pattern_set_control_point(pattern, 1, width * .9, height * .7);
	cairo_mesh_pattern_set_control_point(pattern, 2, width * .9, height * .9);
	cairo_mesh_pattern_set_control_point(pattern, 3, width * .7, height * .9);
	cairo_mesh_pattern_end_patch(pattern);
	cairo_set_source(cr, pattern);
	cairo_paint(cr);
	cairo_pattern_destroy(pattern);
}

int render_fb_mesh_pattern(struct kms_framebuffer* fb)
{
	cairo_t* cr;
	cairo_surface_t* surface;
	cairo_format_t cairo_format = drm2cairo(fb->format);

	surface = cairo_image_surface_create_for_data(fb->ptr,
						      cairo_format,
						      fb->width, fb->height,
						      cairo_format_stride_for_width(cairo_format, fb->width));
	cr = cairo_create(surface);
	draw_mesh(cr, fb->width, fb->height);
	cairo_surface_destroy(surface);
	cairo_destroy(cr);

	return 0;
}

int main(int argc, char *argv[])
{
	bool verbose = false;
	unsigned int i;
	int opt, idx;
	int fd;
	const char* config_file = "default.config";
	const char* device_file = "atmel-hlcdc";
	uint32_t framedelay = 33;
	uint32_t max_frames = 0;
	//struct plane_data** planes;
	struct stat s;
	struct sigaction sig_handler;

    struct plane_data* plane = NULL;

	sig_handler.sa_handler = exit_handler;
	sigemptyset(&sig_handler.sa_mask);
	sig_handler.sa_flags = 0;
	sigaction(SIGINT, &sig_handler, NULL);

	fd = drmOpen(device_file, NULL);
	if (fd < 0) {
		fprintf(stderr, "error: open() failed: %m\n");
		return 1;
	}

	device = kms_device_open(fd);
	if (!device)
		return 1;

	if (verbose) {
		kms_device_dump(device);
	}

	//data = calloc(1, sizeof(struct plane_data));

	plane = plane_create(device,
				    DRM_PLANE_TYPE_OVERLAY,
                    1, //overlay index
				    800,
				    480,
				    DRM_FORMAT_ARGB8888);
    if (!plane) {
        printf("error: failed to create plane\n");
        return 1;
    }

	plane_fb_map(plane);
    render_fb_mesh_pattern(plane->fbs[0]);
    plane_apply(plane);

    while (1) {
        sleep(1);
    }
	// for (i = 0; i < device->num_planes;i++) {
	// 	if (planes[i])
	// 		plane_free(planes[i]);
	// }

	// kms_device_close(device);
	// drmClose(fd);
	// free(planes);

	return 0;
}
