/**
 * @file
 * @brief Internal Header for Bluetooth Volumen Control Service (VCS).
 *
 * Copyright (c) 2020 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_VCS_INTERNAL_
#define ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_VCS_INTERNAL_

/* VCS opcodes */
#define VCS_OPCODE_REL_VOL_DOWN			0x00
#define VCS_OPCODE_REL_VOL_UP			0x01
#define VCS_OPCODE_UNMUTE_REL_VOL_DOWN		0x02
#define VCS_OPCODE_UNMUTE_REL_VOL_UP		0x03
#define VCS_OPCODE_SET_ABS_VOL			0x04
#define VCS_OPCODE_UNMUTE			0x05
#define VCS_OPCODE_MUTE				0x06

struct vcs_state_t {
	uint8_t volume;
	uint8_t mute;
	uint8_t change_counter;
} __packed;

struct vcs_control_t {
	uint8_t opcode;
	uint8_t counter;
	uint8_t volume;
} __packed;

int bt_vcs_client_read_volume_state(struct bt_conn *conn);
int bt_vcs_client_read_flags(struct bt_conn *conn);
int bt_vcs_client_volume_down(struct bt_conn *conn);
int bt_vcs_client_volume_up(struct bt_conn *conn);
int bt_vcs_client_unmute_volume_down(struct bt_conn *conn);
int bt_vcs_client_unmute_volume_up(struct bt_conn *conn);
int bt_vcs_client_set_volume(struct bt_conn *conn, uint8_t volume);
int bt_vcs_client_unmute(struct bt_conn *conn);
int bt_vcs_client_mute(struct bt_conn *conn);
int bt_vcs_client_vocs_read_offset_state(struct bt_conn *conn,
					 uint8_t vocs_index);
int bt_vcs_client_vocs_read_location(struct bt_conn *conn, uint8_t vocs_index);
int bt_vcs_client_vocs_set_location(struct bt_conn *conn, uint8_t vocs_index,
				    uint8_t location);
int bt_vcs_client_vocs_set_offset(struct bt_conn *conn, uint8_t vocs_index,
				  int16_t offset);
int bt_vcs_client_vocs_read_output_description(struct bt_conn *conn,
					       uint8_t vocs_index);
int bt_vcs_client_vocs_set_output_description(struct bt_conn *conn,
					      uint8_t vocs_index,
					      const char *description);
int bt_vcs_client_aics_read_input_state(struct bt_conn *conn,
					uint8_t aics_index);
int bt_vcs_client_aics_read_gain_setting(struct bt_conn *conn,
					 uint8_t aics_index);
int bt_vcs_client_aics_read_input_type(struct bt_conn *conn,
				       uint8_t aics_index);
int bt_vcs_client_aics_read_input_status(struct bt_conn *conn,
					 uint8_t aics_index);
int bt_vcs_client_aics_input_unmute(struct bt_conn *conn, uint8_t aics_index);
int bt_vcs_client_aics_input_mute(struct bt_conn *conn, uint8_t aics_index);
int bt_vcs_client_aics_set_manual_input_gain(struct bt_conn *conn,
					     uint8_t aics_index);
int bt_vcs_client_aics_set_automatic_input_gain(struct bt_conn *conn,
						uint8_t aics_index);
int bt_vcs_client_aics_set_gain(struct bt_conn *conn, uint8_t aics_index,
				int8_t gain);
int bt_vcs_client_aics_read_input_description(struct bt_conn *conn,
					      uint8_t aics_index);
int bt_vcs_client_aics_set_input_description(struct bt_conn *conn,
					     uint8_t aics_index,
					     const char *description);

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_VCS_INTERNAL_*/
