/**
 * Common functions and helpers for BSIM audio tests
 *
 * Copyright (c) 2019 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_TEST_BSIM_BT_AUDIO_TEST_
#define ZEPHYR_TEST_BSIM_BT_AUDIO_TEST_

#include "kernel.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#define WAIT_TIME (30 * 1e6) /*seconds*/

#define WAIT_FOR(cond) while (!(cond)) { k_sleep(K_MSEC(1)); }

#define FAIL(...)							\
	do {								\
		bst_result = Failed;					\
		bs_trace_error_time_line(__VA_ARGS__);			\
	} while (0)

#define PASS(...)							\
	do {								\
		passes++;						\
		printk("Passes %u/%u\n", passes, expected_passes);	\
		if (passes == expected_passes) {			\
			bst_result = Passed;				\
		}							\
		bs_trace_info_time(1, __VA_ARGS__);			\
	} while (0)

#define SET_STEP(s)							\
	do {								\
		if (s == step + 1) {					\
			step = s;					\
			printk("Step %u\n", step);			\
		} else {						\
			FAIL("Invalid step %u (expected %u)", s, step + 1); \
		}							\
	} while (0)

extern const struct bt_data ad[];

void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		  struct net_buf_simple *ad);
void disconnected(struct bt_conn *conn, uint8_t reason);
void test_tick(bs_time_t HW_device_time);
void test_init(void);

#endif /* ZEPHYR_TEST_BSIM_BT_AUDIO_TEST_ */
