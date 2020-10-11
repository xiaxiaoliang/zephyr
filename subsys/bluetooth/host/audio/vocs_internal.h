/** @file
 *  @brief Internal APIs for Bluetooth VOCS.
 *
 * Copyright (c) 2020 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_VOCS_INTERNAL_
#define ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_VOCS_INTERNAL_
#include <zephyr/types.h>

#define VOCS_MAX_DESC_SIZE                      32

/* VOCS opcodes */
#define VOCS_OPCODE_SET_OFFSET                  0x01

struct vocs_control_t {
	uint8_t opcode;
	uint8_t counter;
	int16_t offset;
} __packed;

struct vocs_state_t {
	int16_t offset;
	uint8_t change_counter;
} __packed;

struct vocs_instance_t {
	struct vocs_state_t state;
	bool location_writable;
	uint8_t location;
	bool desc_writable;

	uint16_t start_handle;
	uint16_t end_handle;
	uint16_t state_handle;
	uint16_t location_handle;
	uint16_t control_handle;
	uint16_t desc_handle;
	struct bt_gatt_subscribe_params state_sub_params;
	struct bt_gatt_subscribe_params location_sub_params;
	struct bt_gatt_subscribe_params desc_sub_params;
	uint8_t subscribe_cnt;

	bool busy;
	uint8_t index;
	uint8_t write_buf[sizeof(struct vocs_control_t)];
	struct bt_gatt_write_params write_params;
	struct bt_gatt_read_params read_params;
};

struct bt_vocs {
	struct vocs_state_t state;
	uint8_t location;
	uint8_t index;
	bool initialized;
	char output_desc[CONFIG_BT_VOCS_MAX_OUTPUT_DESCRIPTION_SIZE];
	struct bt_vocs_cb *cb;

	struct bt_gatt_service *service_p;
};

int bt_vocs_offset_state_get(uint8_t index);
int bt_vocs_location_get(uint8_t index);
int bt_vocs_location_set(uint8_t index, uint8_t location);
int bt_vocs_state_set(uint8_t index, int16_t offset);
int bt_vocs_output_description_get(uint8_t index);
int bt_vocs_output_description_set(uint8_t index, const char *description);
int bt_vocs_cb_register(uint8_t index, struct bt_vocs_cb *cb);

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_VOCS_INTERNAL_ */
