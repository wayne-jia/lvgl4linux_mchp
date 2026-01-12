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
#define LV_FB_NUM_BUFFERS   2

#define ONE_MSECOND_IN_NANOSECONDS 1000000

/* GFX Parameters */
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480
#define HW_OVERLAY_INDEX 0


static pthread_t tick_thread;
static atomic_bool tick_running = false;

static const char* device_file = "atmel-hlcdc";
static int fd_drm;
static struct kms_device *device = NULL;
static struct plane_data* plane = NULL;

static int fd_input;
static struct udev *udev;
static struct libinput *g_li;

static lv_coord_t touch_x = 0;
static lv_coord_t touch_y = 0;
static bool touch_pressed = false;

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
static void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
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
			printf("Touch Down: x=%d, y=%d\n", touch_x, touch_y);
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
	// for(int y = area->y1; y <= area->y2; y++) {
    //     memcpy(&plane->bufs[1][y * SCREEN_WIDTH + area->x1],
    //            color_p,
    //            (area->x2 - area->x1 + 1) * sizeof(lv_color_t));
    //     color_p += (area->x2 - area->x1 + 1);
    // }
	plane_apply(plane);
	// printf("Flushed area: (%d, %d) - (%d, %d)\n",
	//        area->x1, area->y1, area->x2, area->y2);
	lv_disp_flush_ready(disp_drv);
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

	//kms_device_dump(device);

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

	plane_fb_map(plane);
}

void gfx_backend_deinit(void)
{
	if (plane)
		plane_free(plane);

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
}

void input_deinit(void)
{
	libinput_unref(g_li);
	udev_unref(udev);
}

void lvgl_init(void)
{
	if (plane->bufs[0] == NULL || plane->bufs[1] == NULL) {
		printf("error: plane buffers not mapped\n");
		return;
	}

	lv_init();

	lv_tick_thread_start();

	/* Input */
	lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

	/* Display */
	lv_display_t * display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
	lv_display_set_buffers(display, plane->bufs[0], plane->bufs[1], SCREEN_WIDTH * SCREEN_HEIGHT * (LV_COLOR_DEPTH / 8), LV_DISPLAY_RENDER_MODE_DIRECT);
	lv_display_set_flush_cb(display, lv_disp_drv_flush_cb);

	lv_demo_widgets();
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


int main(int argc, char *argv[])
{
	struct sigaction sig_handler;
	sig_handler.sa_handler = exit_handler;
	sigemptyset(&sig_handler.sa_mask);
	sig_handler.sa_flags = 0;
	sigaction(SIGINT, &sig_handler, NULL);

	gfx_backend_init();
    input_init();
	lvgl_init();

	struct pollfd pfd = { .fd = fd_input, .events = POLLIN, .revents = 0 };
    while (1) { 
		int ret = poll(&pfd, 1, LV_TASK_INC_VAL_MS); // 10ms timeout to keep GUI responsive

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
