/** @file
 *  @brief Internal APIs for Bluetooth AICS.
 */

/*
 * Copyright (c) 2020 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_AICS_INTERNAL_
#define ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_AICS_INTERNAL_
#include <zephyr/types.h>
#include <bluetooth/gatt.h>

/* AICS opcodes */
#define AICS_OPCODE_SET_GAIN                    0x01
#define AICS_OPCODE_UNMUTE                      0x02
#define AICS_OPCODE_MUTE                        0x03
#define AICS_OPCODE_SET_MANUAL                  0x04
#define AICS_OPCODE_SET_AUTO                    0x05

/* AICS status */
#define AICS_STATUS_INACTIVE                    0x00
#define AICS_STATUS_ACTIVE                      0x01

#define AICS_INPUT_MODE_IMMUTABLE(mode) \
	((mode) == AICS_MODE_MANUAL_ONLY || (mode) == AICS_MODE_AUTO_ONLY)


#define AICS_INPUT_MODE_SETTABLE(mode) \
	((mode) == AICS_MODE_MANUAL_ONLY || (mode) == AICS_MODE_MANUAL)

struct aics_control_t {
	uint8_t opcode;
	uint8_t counter;
} __packed;

struct aics_gain_control_t {
	struct aics_control_t cp;
	int8_t gain_setting;
} __packed;

struct aics_instance_t {
	uint8_t change_counter;
	uint8_t mode;
	bool desc_writable;

	uint16_t start_handle;
	uint16_t end_handle;
	uint16_t state_handle;
	uint16_t gain_handle;
	uint16_t type_handle;
	uint16_t status_handle;
	uint16_t control_handle;
	uint16_t desc_handle;
	struct bt_gatt_subscribe_params state_sub_params;
	struct bt_gatt_subscribe_params status_sub_params;
	struct bt_gatt_subscribe_params desc_sub_params;
	uint8_t subscribe_cnt;

	bool busy;
	uint8_t index;
	uint8_t write_buf[sizeof(struct aics_gain_control_t)];
	struct bt_gatt_write_params write_params;
	struct bt_gatt_read_params read_params;
	struct bt_aics_cb *cb;
};

struct aics_state_t {
	int8_t gain;
	uint8_t mute;
	uint8_t mode;
	uint8_t change_counter;
} __packed;

struct aics_gain_settings_t {
	uint8_t units;
	int8_t minimum;
	int8_t maximum;
} __packed;

struct bt_aics {
	struct aics_state_t state;
	struct aics_gain_settings_t gain_settings;
	bool initialized;
	uint8_t type;
	uint8_t status;
	uint8_t index;
	char input_desc[CONFIG_BT_AICS_MAX_INPUT_DESCRIPTION_SIZE];
	struct bt_aics_cb *cb;

	struct bt_gatt_service *service_p;
};

uint8_t aics_client_notify_handler(struct bt_conn *conn,
				   struct bt_gatt_subscribe_params *params,
				   const void *data, uint16_t length);

/* TODO: We might want to use a aics_inst pointer rather than an index to
 * handle the multiple instances. See the OTS implementation for an example
 * of doing so.
 */
int bt_aics_client_register(struct aics_instance_t *aics_inst, uint8_t index);
int bt_aics_client_unregister(uint8_t index);
int bt_aics_client_input_state_get(struct bt_conn *conn, uint8_t index);
int bt_aics_client_gain_setting_get(struct bt_conn *conn, uint8_t index);
int bt_aics_client_input_type_get(struct bt_conn *conn, uint8_t index);
int bt_aics_client_input_status_get(struct bt_conn *conn, uint8_t index);
int bt_aics_client_input_unmute(struct bt_conn *conn, uint8_t index);
int bt_aics_client_input_mute(struct bt_conn *conn, uint8_t index);
int bt_aics_client_manual_input_gain_set(struct bt_conn *conn, uint8_t index);
int bt_aics_client_automatic_input_gain_set(struct bt_conn *conn,
					    uint8_t index);
int bt_aics_client_gain_set(struct bt_conn *conn, uint8_t index, int8_t gain);
int bt_aics_client_input_description_get(struct bt_conn *conn, uint8_t index);
int bt_aics_client_input_description_set(struct bt_conn *conn, uint8_t index,
					 const char *description);

int bt_aics_deactivate(uint8_t aics_index);
int bt_aics_activate(uint8_t aics_index);
int bt_aics_cb_register(uint8_t index, struct bt_aics_cb *cb);
int bt_aics_input_state_get(uint8_t aics_index);
int bt_aics_gain_setting_get(uint8_t aics_index);
int bt_aics_input_type_get(uint8_t aics_index);
int bt_aics_input_status_get(uint8_t aics_index);
int bt_aics_input_unmute(uint8_t aics_index);
int bt_aics_input_mute(uint8_t aics_index);
int bt_aics_manual_input_gain_set(uint8_t aics_index);
int bt_aics_automatic_input_gain_set(uint8_t aics_index);
int bt_aics_gain_set(uint8_t aics_index, int8_t gain);
int bt_aics_input_description_get(uint8_t aics_index);
int bt_aics_input_description_set(uint8_t aics_index, const char *description);

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_AICS_INTERNAL_ */
