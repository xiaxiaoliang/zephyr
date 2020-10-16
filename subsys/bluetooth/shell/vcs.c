/** @file
 *  @brief Bluetooth VCS server shell.
 *
 * Copyright (c) 2020 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <bluetooth/conn.h>
#include <bluetooth/services/vcs.h>
#include <shell/shell.h>
#include <stdlib.h>
#include <stdio.h>

#include "bt.h"

static void bt_vcs_state_cb(struct bt_conn *conn, int err, uint8_t volume,
			    uint8_t mute)
{
	if (err) {
		shell_error(ctx_shell, "VCS state get failed (%d)", err);
	} else {
		shell_print(ctx_shell, "VCS volume %u, mute %u", volume, mute);
	}
}

static void bt_vcs_flags_cb(struct bt_conn *conn, int err, uint8_t flags)
{
	if (err) {
		shell_error(ctx_shell, "VCS flags get failed (%d)", err);
	} else {
		shell_print(ctx_shell, "VCS flags 0x%02X", flags);
	}
}

static void bt_vcs_aics_state_cb(struct bt_conn *conn, uint8_t aics_index,
				 int err, int8_t gain, uint8_t mute,
				 uint8_t mode)
{
	if (err) {
		shell_error(ctx_shell, "AICS state get failed (%d) for "
			    "index %u", err, aics_index);
	} else {
		shell_print(ctx_shell, "AICS index %u state gain %d, mute %u, "
			    "mode %u", aics_index, gain, mute, mode);
	}

}

static void bt_vcs_aics_gain_setting_cb(struct bt_conn *conn,
					uint8_t aics_index, int err,
					uint8_t units, int8_t minimum,
					int8_t maximum)
{
	if (err) {
		shell_error(ctx_shell, "AICS gain settings get failed (%d) for "
			    "index %u", err, aics_index);
	} else {
		shell_print(ctx_shell, "AICS index %u gain settings units %u, "
			    "min %d, max %d", aics_index, units, minimum,
			    maximum);
	}

}

static void bt_vcs_aics_input_type_cb(struct bt_conn *conn, uint8_t aics_index,
				      int err, uint8_t input_type)
{
	if (err) {
		shell_error(ctx_shell, "AICS input type get failed (%d) for "
			    "index %u", err, aics_index);
	} else {
		shell_print(ctx_shell, "AICS index %u input type %u",
			    aics_index, input_type);
	}

}

static void bt_vcs_aics_status_cb(struct bt_conn *conn, uint8_t aics_index,
				  int err, bool active)
{
	if (err) {
		shell_error(ctx_shell, "AICS status get failed (%d) for "
			    "index %u", err, aics_index);
	} else {
		shell_print(ctx_shell, "AICS index %u status %s",
			    aics_index, active ? "active" : "inactive");
	}

}
static void bt_vcs_aics_description_cb(struct bt_conn *conn, uint8_t aics_index,
				       int err, char *description)
{
	if (err) {
		shell_error(ctx_shell, "AICS description get failed (%d) for "
			    "index %u", err, aics_index);
	} else {
		shell_print(ctx_shell, "AICS index %u description %s",
			    aics_index, description);
	}
}
static void bt_vocs_state_cb(struct bt_conn *conn, uint8_t vocs_index, int err,
			     int16_t offset)
{
	if (err) {
		shell_error(ctx_shell, "VOCS state get failed (%d) for "
			    "index %u", err, vocs_index);
	} else {
		shell_print(ctx_shell, "VOCS index %u offset %d",
			    vocs_index, offset);
	}
}

static void bt_vocs_location_cb(struct bt_conn *conn, uint8_t vocs_index,
				int err, uint8_t location)
{
	if (err) {
		shell_error(ctx_shell, "VOCS location get failed (%d) for "
			    "index %u", err, vocs_index);
	} else {
		shell_print(ctx_shell, "VOCS index %u location %u",
			    vocs_index, location);
	}
}

static void bt_vocs_description_cb(struct bt_conn *conn, uint8_t vocs_index,
				   int err, char *description)
{
	if (err) {
		shell_error(ctx_shell, "VOCS description get failed (%d) for "
			    "index %u", err, vocs_index);
	} else {
		shell_print(ctx_shell, "VOCS index %u description %s",
			    vocs_index, description);
	}
}


static struct bt_vcs_cb_t vcs_cbs = {
	.state = bt_vcs_state_cb,
	.flags = bt_vcs_flags_cb,

	/* Audio Input Control Service */
	.aics_cb = {
		.state = bt_vcs_aics_state_cb,
		.gain_setting = bt_vcs_aics_gain_setting_cb,
		.type = bt_vcs_aics_input_type_cb,
		.status = bt_vcs_aics_status_cb,
		.description = bt_vcs_aics_description_cb
	},

	.vocs_cb = {
		.state = bt_vocs_state_cb,
		.location = bt_vocs_location_cb,
		.description = bt_vocs_description_cb
	}
};

static int cmd_vcs_init(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	struct bt_vcs_init vcs_init;

	memset(&vcs_init, 0, sizeof(vcs_init));

	for (int i = 0; i < ARRAY_SIZE(vcs_init.vocs_init); i++) {
		vcs_init.vocs_init[i].location_writable = true;
		vcs_init.vocs_init[i].desc_writable = true;
		snprintf(vcs_init.vocs_init[i].output_desc,
			 sizeof(vcs_init.vocs_init[i].output_desc),
			 "Output %d", i + 1);
	}

	for (int i = 0; i < ARRAY_SIZE(vcs_init.aics_init); i++) {
		vcs_init.aics_init[i].desc_writable = true;
		snprintf(vcs_init.aics_init[i].input_desc,
			 sizeof(vcs_init.aics_init[i].input_desc),
			 "Input %d", i + 1);
	}

	result = bt_vcs_init(&vcs_init);

	if (result) {
		shell_print(shell, "Fail: %d", result);
		return result;
	}

	bt_vcs_server_cb_register(&vcs_cbs);
	return result;
}

static int cmd_vcs_volume_step(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int step = strtol(argv[1], NULL, 0);

	if (step > UINT8_MAX || step == 0) {
		shell_error(shell, "Step size out of range; 1-255, was %u",
			    step);
		return -ENOEXEC;
	}

	result = bt_vcs_volume_step_set(step);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_state_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result = bt_vcs_volume_get(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_flags_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result = bt_vcs_flags_get(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_volume_down(
	const struct shell *shell, size_t argc, char **argv)
{
	int result = bt_vcs_volume_down(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_volume_up(
	const struct shell *shell, size_t argc, char **argv)

{
	int result = bt_vcs_volume_up(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_unmute_volume_down(
	const struct shell *shell, size_t argc, char **argv)
{
	int result = bt_vcs_unmute_volume_down(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_unmute_volume_up(
	const struct shell *shell, size_t argc, char **argv)
{
	int result = bt_vcs_unmute_volume_up(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_volume_set(
	const struct shell *shell, size_t argc, char **argv)

{
	int result;
	int volume = strtol(argv[1], NULL, 0);

	if (volume > UINT8_MAX) {
		shell_error(shell, "Volume shall be 0-255, was %d", volume);
		return -ENOEXEC;
	}

	result = bt_vcs_volume_set(NULL, volume);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}


static int cmd_vcs_unmute(
	const struct shell *shell, size_t argc, char **argv)
{
	int result = bt_vcs_unmute(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_mute(
	const struct shell *shell, size_t argc, char **argv)
{
	int result = bt_vcs_mute(NULL);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_vocs_state_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_vocs_state_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}
static int cmd_vcs_vocs_location_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_vocs_location_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}


static int cmd_vcs_vocs_location_set(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);
	int location = strtol(argv[2], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}
	if (location > UINT16_MAX || location < 0) {
		shell_error(shell, "Invalid location (%u-%u), was %u",
			    0, UINT16_MAX, location);
		return -ENOEXEC;

	}

	result = bt_vcs_vocs_location_set(NULL, location, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_vocs_offset_set(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);
	int offset = strtol(argv[2], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	if (offset > UINT8_MAX || offset < -UINT8_MAX) {
		shell_error(shell, "Offset shall be %d-%d, was %d",
			    -UINT8_MAX, UINT8_MAX, offset);
		return -ENOEXEC;
	}

	result = bt_vcs_vocs_state_set(NULL, index, offset);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_vocs_output_description_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_vocs_description_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_vocs_output_description_set(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);
	char *description = argv[2];

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_vocs_description_set(NULL, index, description);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;

}

static int cmd_vcs_aics_input_state_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_state_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_gain_setting_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_gain_setting_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_input_type_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_type_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_input_status_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_status_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_input_unmute(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_unmute(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_input_mute(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_mute(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_manual_input_gain_set(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_manual_gain_set(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_automatic_input_gain_set(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_automatic_gain_set(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_gain_set(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);
	int gain = strtol(argv[2], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	if (gain > INT8_MAX || gain < INT8_MIN) {
		shell_error(shell, "Offset shall be %d-%d, was %d",
			    INT8_MIN, INT8_MAX, gain);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_gain_set(NULL, index, gain);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_input_description_get(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_description_get(NULL, index);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;
}

static int cmd_vcs_aics_input_description_set(
	const struct shell *shell, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);
	char *description = argv[2];

	if (index > CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		shell_error(shell, "Index out of range; 0-%u, was %u",
			    CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST, index);
		return -ENOEXEC;
	}

	result = bt_vcs_aics_description_set(NULL, index, description);

	if (result) {
		shell_print(shell, "Fail: %d", result);
	}
	return result;

}

static int cmd_vcs(const struct shell *shell, size_t argc,
				 char **argv)
{
	if (argc > 1) {
		shell_error(shell, "%s unknown parameter: %s",
			    argv[0], argv[1]);
	} else {
		shell_error(shell, "%s Missing subcommand", argv[0]);
	}

	return -ENOEXEC;
}

SHELL_STATIC_SUBCMD_SET_CREATE(vcs_cmds,
	SHELL_CMD_ARG(init, NULL,
		      "Initialize the service and register callbacks",
		      cmd_vcs_init, 1, 0),
	SHELL_CMD_ARG(state_get, NULL,
		      "Get volume state of the VCS server. Should be done "
		      "before sending any control messages",
		      cmd_vcs_state_get, 1, 0),
	SHELL_CMD_ARG(flags_get, NULL,
		      "Read volume flags",
		      cmd_vcs_flags_get, 1, 0),
	SHELL_CMD_ARG(volume_down, NULL,
		      "Turn the volume down",
		      cmd_vcs_volume_down, 1, 0),
	SHELL_CMD_ARG(volume_up, NULL,
		      "Turn the volume up",
		      cmd_vcs_volume_up, 1, 0),
	SHELL_CMD_ARG(unmute_volume_down, NULL,
		      "Turn the volume down, and unmute",
		      cmd_vcs_unmute_volume_down, 1, 0),
	SHELL_CMD_ARG(unmute_volume_up, NULL,
		      "Turn the volume up, and unmute",
		      cmd_vcs_unmute_volume_up, 1, 0),
	SHELL_CMD_ARG(volume_set, NULL,
		      "Set an absolute volume <volume>",
		      cmd_vcs_volume_set, 2, 0),
	SHELL_CMD_ARG(unmute, NULL,
		      "Unmute",
		      cmd_vcs_unmute, 1, 0),
	SHELL_CMD_ARG(mute, NULL,
		      "Mute",
		      cmd_vcs_mute, 1, 0),
	SHELL_CMD_ARG(step, NULL,
		      "Set step size",
		      cmd_vcs_volume_step, 2, 0),
	SHELL_CMD_ARG(vocs_state_get, NULL,
		      "Get the offset state of a VOCS instance <inst_index>",
		      cmd_vcs_vocs_state_get, 2, 0),
	SHELL_CMD_ARG(vocs_location_get, NULL,
		      "Get the location of a VOCS instance <inst_index>",
		      cmd_vcs_vocs_location_get, 2, 0),
	SHELL_CMD_ARG(vocs_location_set, NULL,
		      "Set the location of a VOCS instance <inst_index> "
		      "<location>",
		      cmd_vcs_vocs_location_set, 3, 0),
	SHELL_CMD_ARG(vocs_offset_set, NULL,
		      "Set the offset for a VOCS instance <inst_index> "
		      "<offset>",
		      cmd_vcs_vocs_offset_set, 3, 0),
	SHELL_CMD_ARG(vocs_output_description_get, NULL,
		      "Get the output description of a VOCS instance "
		      "<inst_index>",
		      cmd_vcs_vocs_output_description_get, 2, 0),
	SHELL_CMD_ARG(vocs_output_description_set, NULL,
		      "Set the output description of a VOCS instance "
		      "<inst_index> <description>",
		      cmd_vcs_vocs_output_description_set, 3, 0),
	SHELL_CMD_ARG(aics_input_state_get, NULL,
		      "Get the input state of a AICS instance <inst_index>",
		      cmd_vcs_aics_input_state_get, 2, 0),
	SHELL_CMD_ARG(aics_gain_setting_get, NULL,
		      "Get the gain settings of a AICS instance <inst_index>",
		      cmd_vcs_aics_gain_setting_get, 2, 0),
	SHELL_CMD_ARG(aics_input_type_get, NULL,
		      "Get the input type of a AICS instance <inst_index>",
		      cmd_vcs_aics_input_type_get, 2, 0),
	SHELL_CMD_ARG(aics_input_status_get, NULL,
		      "Get the input status of a AICS instance <inst_index>",
		      cmd_vcs_aics_input_status_get, 2, 0),
	SHELL_CMD_ARG(aics_input_unmute, NULL,
		      "Unmute the input of a AICS instance <inst_index>",
		      cmd_vcs_aics_input_unmute, 2, 0),
	SHELL_CMD_ARG(aics_input_mute, NULL,
		      "Mute the input of a AICS instance <inst_index>",
		      cmd_vcs_aics_input_mute, 2, 0),
	SHELL_CMD_ARG(aics_manual_input_gain_set, NULL,
		      "Set the gain mode of a AICS instance to manual "
		      "<inst_index>",
		      cmd_vcs_aics_manual_input_gain_set, 2, 0),
	SHELL_CMD_ARG(aics_automatic_input_gain_set, NULL,
		      "Set the gain mode of a AICS instance to automatic "
		      "<inst_index>",
		      cmd_vcs_aics_automatic_input_gain_set, 2, 0),
	SHELL_CMD_ARG(aics_gain_set, NULL,
		      "Set the gain in dB of a AICS instance <inst_index> "
		      "<gain (-128 to 127)>",
		      cmd_vcs_aics_gain_set, 3, 0),
	SHELL_CMD_ARG(aics_input_description_get, NULL,
		      "Read the input description of a AICS instance "
		      "<inst_index>",
		      cmd_vcs_aics_input_description_get, 2, 0),
	SHELL_CMD_ARG(aics_input_description_set, NULL,
		      "Set the input description of a AICS instance "
		      "<inst_index> <description>",
		      cmd_vcs_aics_input_description_set, 3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_ARG_REGISTER(vcs, &vcs_cmds, "Bluetooth VCS shell commands",
		       cmd_vcs, 1, 1);
