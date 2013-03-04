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

#define LOG_TAG "DASH - tmd3xxx_als_ALS"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "sensors_log.h"
#include <linux/input.h>
#include "sensors_list.h"
#include "sensors_fifo.h"
#include "sensors_select.h"
#include "sensor_util.h"
#include "sensors_id.h"

static int tmd3xxx_als_init(struct sensor_api_t *s);
static int tmd3xxx_als_activate(struct sensor_api_t *s, int enable);
static int tmd3xxx_als_set_delay(struct sensor_api_t *s, int64_t ns);
static void tmd3xxx_als_close(struct sensor_api_t *s);
static void *tmd3xxx_als_read(void *arg);

struct sensor_desc {
	struct sensors_select_t select_worker;
	struct sensor_t sensor;
	struct sensor_api_t api;
	char *name;
};

static struct sensor_desc tmd3xxx_als = {
	.sensor = {
		.name = "TMD/TSL3XXX Ambient Light",
		.vendor = "AMS-TAOS US Inc",
		.version = sizeof(sensors_event_t),
		.handle = SENSOR_LIGHT_HANDLE,
		.type = SENSOR_TYPE_LIGHT,
		.maxRange = 65535.0,
		.resolution = 1.0,
		.power = 0.171,
		.minDelay = 0
	},
	.api = {
		.init = tmd3xxx_als_init,
		.activate = tmd3xxx_als_activate,
		.set_delay = tmd3xxx_als_set_delay,
		.close = tmd3xxx_als_close
	},
	.name = "taos_als",
};

static int tmd3xxx_als_init(struct sensor_api_t *s)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	int fd;

	fd = open_input_dev_by_name(d->name, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		ALOGE("%s: failed to open input dev %s, error: %s\n",
			__func__, d->name, strerror(errno));
		return -1;
	}
	close(fd);
	sensors_select_init(&d->select_worker, tmd3xxx_als_read, s, -1);
	return 0;
}

static int tmd3xxx_als_activate(struct sensor_api_t *s, int enable)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	int fd = d->select_worker.get_fd(&d->select_worker);

	if (enable && (fd < 0)) {
		fd = open_input_dev_by_name(d->name, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			ALOGE("%s: failed to open input dev %s, error: %s\n",
				__func__, d->name, strerror(errno));
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

static int tmd3xxx_als_set_delay(struct sensor_api_t *s, int64_t ns)
{
	return 0;
}

static void tmd3xxx_als_close(struct sensor_api_t *s)
{
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);

	d->select_worker.destroy(&d->select_worker);
}

static void *tmd3xxx_als_read(void *arg)
{
	struct sensor_api_t *s = arg;
	struct sensor_desc *d = container_of(s, struct sensor_desc, api);
	int bytes, i;
	struct input_event event[2];
	int fd = d->select_worker.get_fd(&d->select_worker);
	sensors_event_t data;
	float light = 0;

	bytes = read(fd, event, sizeof(event));
	if (bytes < 0) {
		ALOGE("%s: read failed, error: %d\n", __func__, bytes);
		return NULL;
	}

	for (i = 0; i < (bytes / (int)sizeof(struct input_event)); i++) {
		switch (event[i].type) {
		case EV_ABS:
			if (event[i].code == ABS_MISC)
				light = event[i].value;
			break;
		case EV_SYN:
			memset(&data, 0, sizeof(data));
			data.light = light;
			data.version = tmd3xxx_als.sensor.version;
			data.sensor = tmd3xxx_als.sensor.handle;
			data.type = tmd3xxx_als.sensor.type;
			data.timestamp = get_current_nano_time();
			sensors_fifo_put(&data);
			break;
		default:
			break;
		}
	}

	return NULL;
}

list_constructor(tmd3xxx_als_init_driver);
void tmd3xxx_als_init_driver()
{
	(void)sensors_list_register(&tmd3xxx_als.sensor, &tmd3xxx_als.api);
}
