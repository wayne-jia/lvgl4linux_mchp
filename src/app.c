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
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/input.h>
#include <cairo.h>
#include <poll.h>
#include <errno.h>
#include "planes/engine.h"
#include "planes/kms.h"
#include "p_kms.h"
#include "lvgl.h"
#include "lv_conf.h"
#include "lv_demos.h"

/* LVGL Parameters */
#define LV_UNCACHED_BUFFER  0
#define LV_TICK_INC_VAL_MS  1
#define LV_TASK_INC_VAL_MS  LV_DEF_REFR_PERIOD
#define LV_FB_NUM_BUFFERS   3
#define HEO_FB_NUM_BUFFERS  1

#define ONE_MSECOND_IN_NANOSECONDS 1000000

/* GFX Parameters */
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480
#define HW_OVERLAY_INDEX 0
#define HEO_OVERLAY_INDEX 2
#define SCALE_IMAGE_W 400
#define SCALE_IMAGE_H 240

static pthread_t tick_thread;
static atomic_bool tick_running = false;

static const char* device_file = "atmel-hlcdc";
static int fd_drm;
static struct kms_device *device = NULL;
static struct plane_data* plane = NULL;
static struct plane_data* heo_plane = NULL;
static char heo_filename[16] = "mars.png";

static int fd_input;
static struct udev *udev;
static struct libinput *g_li;

static lv_coord_t touch_x = 0;
static lv_coord_t touch_y = 0;
static bool touch_pressed = false;
static int key_press_cnt = 0;

static lv_obj_t * labels[4];
static int current_idx = -1;

void create_right_top_labels(void) {
    lv_obj_t * cont = lv_obj_create(lv_screen_active());
    lv_obj_set_size(cont, 100, 220);
    lv_obj_align(cont, LV_ALIGN_TOP_RIGHT, -10, 10);

    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 12, 0);

    lv_obj_set_style_bg_opa(cont, LV_OPA_10, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    const char * texts[] = {"0.5x", "1.0x", "1.5x", "2.0x"};

    for(int i = 0; i < 4; i++) {
        labels[i] = lv_label_create(cont);
        lv_label_set_text(labels[i], texts[i]);

        lv_obj_set_style_bg_color(labels[i], lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(labels[i], LV_OPA_60, 0);
        lv_obj_set_style_text_color(labels[i], lv_color_hex(0xFFFFFF), 0);

        lv_obj_set_style_pad_hor(labels[i], 20, 0);
        lv_obj_set_style_pad_ver(labels[i], 8, 0);
        lv_obj_set_style_radius(labels[i], 5, 0);
        lv_obj_set_style_text_align(labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(labels[i], 80);
    }
}

void trigger_next_highlight(void) {
    if(current_idx >= 0 && current_idx < 4) {
        lv_obj_set_style_bg_color(labels[current_idx], lv_color_hex(0x444444), 0);
        lv_obj_set_style_text_color(labels[current_idx], lv_color_hex(0xFFFFFF), 0);
    }

    current_idx = (current_idx + 1) % 4;

    lv_obj_set_style_bg_color(labels[current_idx], lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_bg_opa(labels[current_idx], LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(labels[current_idx], lv_color_hex(0x000000), 0);
}

static void *keypad_thread_handler(void *arg) {
    int fd;
    struct input_event ev;
	float scale_factors[] = {0.5, 1.0, 1.5, 2.0};

    fd = open("/dev/input/event1", O_RDONLY);
    if (fd < 0) {
        perror("Error: Cannot open /dev/input/event1");
        return NULL;
    }

    printf("Keypad thread started, listening on event1...\n");

    while (1) {
        if (read(fd, &ev, sizeof(struct input_event)) <= 0) {
            continue;
        }

        if (ev.type == EV_KEY && ev.value == 1) {
			if (key_press_cnt > 3)
				key_press_cnt = 0;

			trigger_next_highlight();
			printf("Scale to %f\n", scale_factors[key_press_cnt]);
            plane_set_scale(heo_plane, scale_factors[key_press_cnt]);
			plane_apply(heo_plane);
			key_press_cnt++;
        }
    }

    close(fd);
    return NULL;
}

void start_keypad_listener(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, keypad_thread_handler, NULL) != 0) {
        printf("Error: Failed to create keypad thread\n");
    } else {
        pthread_detach(thread);
    }
}

static void *lv_tick_thread_func(void *arg) {
    (void)arg;
    struct timespec req = { .tv_sec = 0, .tv_nsec = 1000000 }; // 1ms

    while (atomic_load_explicit(&tick_running, memory_order_relaxed)) {
        lv_tick_inc(LV_TICK_INC_VAL_MS);
        nanosleep(&req, NULL);
    }
    return NULL;
}

int lv_tick_thread_start(void) {
    if (atomic_load(&tick_running)) return 0;
    atomic_store(&tick_running, true);
    return pthread_create(&tick_thread, NULL, lv_tick_thread_func, NULL);
}

int lv_tick_thread_stop(void) {
    if (!atomic_load(&tick_running)) return 0;
    atomic_store(&tick_running, false);
    return pthread_join(tick_thread, NULL);
}

/* libinput open/close */
static int open_restricted(const char *path, int flags, void *user_data) {
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) {
    close(fd);
}

static const struct libinput_interface li_iface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

/* LVGL input callback (v9.4) */
static void lv_indev_drv_read_cb(lv_indev_t * _, lv_indev_data_t * data) {
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state   = touch_pressed ? LV_INDEV_STATE_PRESSED
                                  : LV_INDEV_STATE_RELEASED;
}

/* Handle libinput event */
static void process_libinput(struct libinput *li) {
    libinput_dispatch(li);
    struct libinput_event *ev;
    while ((ev = libinput_get_event(li))) {
        switch (libinput_event_get_type(ev)) {
        case LIBINPUT_EVENT_TOUCH_DOWN: {
            struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
            touch_x = (lv_coord_t)libinput_event_touch_get_x(t);
            touch_y = (lv_coord_t)libinput_event_touch_get_y(t);
            touch_pressed = true;
			//printf("Touch Down: x=%d, y=%d\n", touch_x, touch_y);
            break;
        }
        case LIBINPUT_EVENT_TOUCH_MOTION: {
            struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
            touch_x = (lv_coord_t)libinput_event_touch_get_x(t);
            touch_y = (lv_coord_t)libinput_event_touch_get_y(t);
            break;
        }
        case LIBINPUT_EVENT_TOUCH_UP:
            touch_pressed = false;
            break;
        default:
            break;
        }
        libinput_event_destroy(ev);
    }
}

static void lv_disp_drv_flush_cb(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * color_p)
{
    uint32_t bytes_per_pixel = LV_COLOR_DEPTH / 8;
    int32_t area_w = lv_area_get_width(area);
    int32_t area_h = lv_area_get_height(area);
    uint32_t line_width_bytes = area_w * bytes_per_pixel;
	uint8_t *base_ptr = (uint8_t *)plane->bufs[0];

    for(int32_t y = area->y1; y <= area->y2; y++) {
        uint8_t * fb_line_ptr = &base_ptr[(y * SCREEN_WIDTH + area->x1) * bytes_per_pixel];
        memcpy(fb_line_ptr, color_p, line_width_bytes);
        color_p += line_width_bytes;
    }

    lv_display_flush_ready(disp_drv);
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

void gfx_backend_init(void)
{
	fd_drm = drmOpen(device_file, NULL);
	if (fd_drm < 0) {
		fprintf(stderr, "error: open() failed: %m\n");
		return;
	}

	device = kms_device_open(fd_drm);
	if (!device)
		return;

	plane = plane_create_buffered(device,
				    DRM_PLANE_TYPE_PRIMARY,
                    HW_OVERLAY_INDEX,
				    SCREEN_WIDTH,
				    SCREEN_HEIGHT,
				    DRM_FORMAT_RGB565,
					LV_FB_NUM_BUFFERS);
    if (!plane) {
        printf("error: failed to create plane\n");
        return;
    }

	heo_plane = plane_create_buffered(device,
				    DRM_PLANE_TYPE_OVERLAY,
                    HEO_OVERLAY_INDEX,
				    SCALE_IMAGE_W,
				    SCALE_IMAGE_H,
				    DRM_FORMAT_XRGB8888,
					HEO_FB_NUM_BUFFERS);
    if (!heo_plane) {
        printf("error: failed to create heo plane\n");
        return;
    }

	plane_fb_map(plane);
	plane_fb_map(heo_plane);
}

void gfx_backend_deinit(void)
{
	if (plane)
		plane_free(plane);

	if (heo_plane)
		plane_free(heo_plane);

	if (device)
		kms_device_close(device);

	if (fd_drm >= 0)
		drmClose(fd_drm);
}

void input_init(void)
{
	udev = udev_new();
	if (!udev) {
		fprintf(stderr, "Failed to create udev\n");
		return;
	}

	g_li = libinput_udev_create_context(&li_iface, NULL, udev);
	if (!g_li) {
		fprintf(stderr, "Failed to create libinput context\n");
		udev_unref(udev);
		return;
	}

	if (libinput_udev_assign_seat(g_li, "seat0") != 0) {
		fprintf(stderr, "Failed to assign seat\n");
		libinput_unref(g_li);
		udev_unref(udev);
		return;
	}
	fd_input = libinput_get_fd(g_li);

	lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	//lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, lv_indev_drv_read_cb);
}

void input_deinit(void)
{
	libinput_unref(g_li);
	udev_unref(udev);
}

void lvgl_init(void)
{
	if (plane->bufs[0] == NULL || plane->bufs[1] == NULL || plane->bufs[2] == NULL) {
		printf("error: plane buffers not mapped\n");
		return;
	}

	lv_init();

	/* Display */
	lv_display_t * display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
	lv_display_set_buffers(display, plane->bufs[1], plane->bufs[2], SCREEN_WIDTH * SCREEN_HEIGHT * (LV_COLOR_DEPTH / 8), LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(display, lv_disp_drv_flush_cb);

	//lv_demo_widgets();
	//lv_demo_benchmark();
	//lv_demo_stress();

	plane_set_pos(plane, 0, 0);
	plane_apply(plane);
}

void lvgl_deinit(void)
{
	lv_deinit();
}

static void exit_handler(int s) {
	lvgl_deinit();
	input_deinit();
	gfx_backend_deinit();
	exit(1);
}

static void btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    const char * txt = lv_label_get_text(label);

    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        printf("Button Clicked: %s\n", txt);
		if(strcmp(txt, "1x") == 0) {
			plane_set_scale(heo_plane, 1.0);
		}
		else if(strcmp(txt, "0.5x") == 0) {
			plane_set_scale(heo_plane, 0.5);
		}
		else if(strcmp(txt, "1.5x") == 0) {
			plane_set_scale(heo_plane, 1.5);
		}
		else if(strcmp(txt, "2x") == 0) {
			plane_set_scale(heo_plane, 2.0);
		}

		plane_apply(heo_plane);
    }
}

void create_bottom_buttons(void) {
    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, lv_pct(100), 70);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const char * btn_texts[] = {"0.5x", "1x", "1.5x", "2x"};

    for(int i = 0; i < 4; i++) {
        lv_obj_t * btn = lv_btn_create(cont);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, btn_texts[i]);
        lv_obj_center(label);
    }
}

static cairo_surface_t *
scale_surface(cairo_surface_t *old_surface,
	      int old_width, int old_height,
	      int new_width, int new_height)
{
	cairo_surface_t *new_surface = cairo_surface_create_similar(old_surface,
								    CAIRO_CONTENT_COLOR_ALPHA,
								    new_width,
								    new_height);
	cairo_t *cr = cairo_create (new_surface);

	/* Scale *before* setting the source surface (1) */
	cairo_scale(cr, (double)new_width / old_width, (double)new_height / old_height);
	cairo_set_source_surface(cr, old_surface, 0, 0);

	/* To avoid getting the edge pixels blended with 0 alpha, which would
	 * occur with the default EXTEND_NONE. Use EXTEND_PAD for 1.2 or newer (2)
	 */
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REFLECT);

	/* Replace the destination with the source instead of overlaying */
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

	/* Do the actual drawing */
	cairo_paint (cr);

	cairo_destroy (cr);

	return new_surface;
}

int render_fb_image(struct kms_framebuffer* fb, const char* filename)
{
	if (heo_plane->bufs[0] == NULL) {
		printf("error: heo plane buffers not mapped\n");
		return -1;
	}

	void* ptr = heo_plane->bufs[0];
	int err;
	cairo_t* cr;
	cairo_surface_t* surface;
	cairo_surface_t* image;
	cairo_format_t cairo_format = drm2cairo(fb->format);

	printf("heo format: %d, size: %dx%d\n",
	    cairo_format,
	    fb->width, fb->height);

	surface = cairo_image_surface_create_for_data(ptr,
						      cairo_format,
						      fb->width, fb->height,
						      cairo_format_stride_for_width(cairo_format, fb->width));
	cr = cairo_create(surface);

	image = cairo_image_surface_create_from_png(filename);

	printf("scale image size %dx%d\n",
	    cairo_image_surface_get_width(image),
	    cairo_image_surface_get_height(image));

#if 0
	if (cairo_image_surface_get_width(image) != (int)fb->width ||
	    cairo_image_surface_get_height(image) != (int)fb->height) {

		printf("image scaled to %dx%d\n", fb->width, fb->height);
		image = scale_surface(image,
				      cairo_image_surface_get_width(image),
				      cairo_image_surface_get_height(image),
				      fb->width,
				      fb->height);
	}
#endif

	cairo_set_source_surface(cr, image, 0, 0);
	cairo_paint(cr);
	cairo_surface_destroy(image);

	cairo_surface_destroy(surface);
	cairo_destroy(cr);

	return 0;
}

int main(int argc, char *argv[])
{
	struct sigaction sig_handler;
	sig_handler.sa_handler = exit_handler;
	sigemptyset(&sig_handler.sa_mask);
	sig_handler.sa_flags = 0;
	sigaction(SIGINT, &sig_handler, NULL);

	gfx_backend_init();
	lvgl_init();
	input_init();
	start_keypad_listener();
	lv_tick_thread_start();

	render_fb_image(heo_plane->fbs[0], heo_filename);
	plane_set_pos(heo_plane, 0, 0);
	plane_apply(heo_plane);
	//create_bottom_buttons();
	create_right_top_labels();

	struct pollfd pfd = { .fd = fd_input, .events = POLLIN, .revents = 0 };
    while (1) { 
		int ret = poll(&pfd, 1, LV_TASK_INC_VAL_MS);

		if (ret > 0 && (pfd.revents & POLLIN)) {
			process_libinput(g_li);
		}

		lv_task_handler();
    }

	lvgl_deinit();
	input_deinit();
	gfx_backend_deinit();
	lv_tick_thread_stop();
	return 0;
}
