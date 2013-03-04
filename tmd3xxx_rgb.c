/*
 * Copyright (C) 2012 Sony Mobile Communications AB.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "DASH - tmd3xxx_rgb_input"

#include <stdlib.h>
#include <string.h>
#include "sensors_log.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include "sensors_list.h"
#include "sensors_fifo.h"
#include "sensors_select.h"
#include "sensor_util.h"
#include "sensors_id.h"
#include "sensors_config.h"

#define TMD3XXX_RGB_INPUT_NAME "taos_rgb"
#define TMD3XXX_RGB_CONVERT_AXES (1/12.742)
#define VALID_HANDLE(h) ((h) > CLIENT_ANDROID && (h) < MAX_CLIENTS)

enum tmd3xxx_rgb_clients {
	CLIENT_ANDROID = 0,
	MAX_CLIENTS = 2,
	CLIENT_DELAY_UNUSED = 0,
};

static int tmd3xxx_rgb_input_init(struct sensor_api_t *s);
static int tmd3xxx_rgb_input_activate(struct sensor_api_t *s, int enable);
static int tmd3xxx_rgb_input_fw_delay(struct sensor_api_t *s, int64_t ns);
static void tmd3xxx_rgb_input_close(struct sensor_api_t *s);
static void *tmd3xxx_rgb_input_read(void *arg);

struct sensor_desc {
	struct sensors_select_t select_worker;
	struct sensor_t sensor;
	struct sensor_api_t api;

	int input_fd;
	float current_data[3];

	char *rate_path;

	/* RGB counts */
	int red_data;
	int green_data;
	int blue_data;

	int64_t  delay_requests[MAX_CLIENTS];
};

static struct sensor_desc tmd3xxx_rgb_input = {
	.sensor = {
		name: "TMD/TSL 3XXX_RGB",
		vendor: "AMS-TAOS US Inc.",
		version: sizeof(sensors_event_t),
//		.handle = SENSOR_LIGHT_HANDLE,
//		.type = SENSOR_TYPE_LIGHT,
		handle: SENSOR_ACCELEROMETER_HANDLE,
		type: SENSOR_TYPE_ACCELEROMETER,
		maxRange: 65535,
		resolution: 1,
		power: 0.171,
		minDelay: 0
	},
	.api = {
		init: tmd3xxx_rgb_input_init,
		activate: tmd3xxx_rgb_input_activate,
		set_delay: tmd3xxx_rgb_input_fw_delay,
		close: tmd3xxx_rgb_input_close
	},
	.input_fd = -1,
	.red_data = 0,
	.green_data = 1,
	.blue_data = 2
};

static void tmd3xxx_rgb_set_rate(const struct sensor_desc *d, int rate_msec)
{
	int rate_fd;
	int rc = -1;

	if (!d->rate_path)
		return;

	rate_fd = open(d->rate_path, O_WRONLY);
	if (rate_fd >= 0) {
		char new_rate[20];
		rc = snprintf(new_rate, sizeof(new_rate), "%d\n", rate_msec);
		if (rc > 0)
			rc = write(rate_fd, new_rate, rc);
		close(rate_fd);
	}
	if (rc <= 0)
		ALOGE("%s: failed to set poll rate to %d ms", __func__, rate_msec);
	return;
}

static int tmd3xxx_rgb_input_init(struct sensor_api_t *s)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	int fd;
	fd = open_input_dev_by_name(TMD3XXX_RGB_INPUT_NAME, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		ALOGE("%s: unable to find %s input device!\n", __func__,
			TMD3XXX_RGB_INPUT_NAME);
		return -1;
	}
	close(fd);

	sensors_select_init(&d->select_worker, tmd3xxx_rgb_input_read, s, -1);
	return 0;
}

static int tmd3xxx_rgb_input_activate(struct sensor_api_t *s, int enable)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	int fd = d->select_worker.get_fd(&d->select_worker);

	/* suspend/resume will be handled in kernel-space */
	if (enable && (fd < 0)) {
		fd = open_input_dev_by_name(TMD3XXX_RGB_INPUT_NAME,
			O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			ALOGE("%s: failed to open input dev %s\n", __func__,
				TMD3XXX_RGB_INPUT_NAME);
			return -1;
		}
		d->select_worker.set_fd(&d->select_worker, fd);
		d->select_worker.resume(&d->select_worker);
	} else if (!enable && (fd > 0)) {
		d->select_worker.set_fd(&d->select_worker, -1);
		d->select_worker.suspend(&d->select_worker);
	}

	return 0;
}

static int tmd3xxx_rgb_input_set_delay(struct sensor_api_t *s)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	int i;
	int64_t usec = d->delay_requests[0];
	int64_t x;

	for (i = 0; i < MAX_CLIENTS; i++) {
		x = d->delay_requests[i];
		if (x > CLIENT_DELAY_UNUSED && x < usec)
			usec = x;
	}

	if (usec < d->sensor.minDelay) {
		usec = d->sensor.minDelay;
	}

	tmd3xxx_rgb_set_rate(d, usec / 1000);

	return 0;
}

static int tmd3xxx_rgb_input_fw_delay(struct sensor_api_t *s, int64_t ns)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	d->delay_requests[CLIENT_ANDROID] = ns / 1000;
	return tmd3xxx_rgb_input_set_delay(s);
}

int tmd3xxx_rgb_input_request_delay(int *handle, int64_t ns)
{
	struct sensor_desc *d = &tmd3xxx_rgb_input;
	int err;
	int h = *handle;
	int i;

	ns /= 1000;

	if (!ns && VALID_HANDLE(h)) {
		/* Need to release handle */
		*handle = -1;
		goto found;
	}

	if (!ns) /* Error */
		return -1;

	if (!VALID_HANDLE(h)) {
		/* Need to allocate new handle */
		for (i = CLIENT_ANDROID + 1; i < MAX_CLIENTS; i++) {
			if (d->delay_requests[i] == CLIENT_DELAY_UNUSED) {
				*handle = h = i;
				goto found;
			}
		}
	}

found:
	d->delay_requests[h] = ns;
	err = tmd3xxx_rgb_input_set_delay(&tmd3xxx_rgb_input.api);
	if (err) {
		/* Delay not set - deallocate handle */
		d->delay_requests[h] = CLIENT_DELAY_UNUSED;
		*handle = -1;
	}
	return err;
}

static void tmd3xxx_rgb_input_close(struct sensor_api_t *s)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);

	d->select_worker.destroy(&d->select_worker);
	free(d->rate_path);
}

static void *tmd3xxx_rgb_input_read(void *arg)
{
	struct sensor_api_t *s = arg;
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	struct input_event event;
	int fd = d->select_worker.get_fd(&d->select_worker);
	sensors_event_t data;

	memset(&data, 0, sizeof(data));
	while (read(fd, &event, sizeof(event)) > 0) {
		switch (event.type) {
		case EV_ABS:
			switch (event.code) {
			case ABS_X:
				d->current_data[0] = (float)event.value;
				break;

			case ABS_Y:
				d->current_data[1] = (float)event.value;
				break;

			case ABS_Z:
				d->current_data[2] = (float)event.value;
				break;

			case ABS_MISC:
			default:
				break;
			}
			break;

		case EV_SYN:
			data.acceleration.x = d->current_data[d->red_data];
			data.acceleration.y = d->current_data[d->green_data];
			data.acceleration.z = d->current_data[d->blue_data];
			data.acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;

			data.sensor = tmd3xxx_rgb_input.sensor.handle;
			data.type = tmd3xxx_rgb_input.sensor.type;
			data.version = tmd3xxx_rgb_input.sensor.version;
			data.timestamp = get_current_nano_time();

			sensors_fifo_put(&data);

			goto exit;

		default:
			goto exit;
		}
	}

exit:
	return NULL;
}

list_constructor(tmd3xxx_rgb_input_init_driver);
void tmd3xxx_rgb_input_init_driver()
{
	(void)sensors_list_register(&tmd3xxx_rgb_input.sensor,
			&tmd3xxx_rgb_input.api);
}

