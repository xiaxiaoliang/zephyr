/*  Bluetooth AICS client */

/*
 * Copyright (c) 2020 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <zephyr/types.h>

#include <device.h>
#include <init.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/services/aics.h>

#include "aics_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_AICS_CLIENT)
#define LOG_MODULE_NAME bt_aics_client
#include "common/log.h"

/* The profile clients that uses the AICS are responsible for discovery and
 * will simply register any found AICS instances as pointers, which is stored
 * here
 */
struct aics_instance_t *aics_insts[CONFIG_BT_AICS_CLIENT_MAX_INSTANCE_COUNT];

static int aics_client_common_control(struct bt_conn *conn, uint8_t opcode,
				      uint8_t index);

static struct aics_instance_t *lookup_aics_by_handle(uint16_t handle)
{
	for (int i = 0; i < ARRAY_SIZE(aics_insts); i++) {
		if (aics_insts[i] &&
		    aics_insts[i]->start_handle <= handle &&
		    aics_insts[i]->end_handle >= handle) {
			return aics_insts[i];
		}
	}
	BT_DBG("Could not find AICS instance with handle 0x%04x", handle);
	return NULL;
}

uint8_t aics_client_notify_handler(struct bt_conn *conn,
				struct bt_gatt_subscribe_params *params,
				const void *data, uint16_t length)
{
	uint16_t handle = params->value_handle;
	struct aics_instance_t *aics_inst = lookup_aics_by_handle(handle);
	struct aics_state_t *state;
	uint8_t *status;
	char desc[MIN(CONFIG_BT_L2CAP_RX_MTU, BT_ATT_MAX_ATTRIBUTE_LEN) + 1];

	if (!aics_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	if (data) {
		if (handle == aics_inst->state_handle) {
			if (length == sizeof(*state)) {
				state = (struct aics_state_t *)data;
				BT_DBG("Index %u: Gain %d, mute %u, mode %u, "
				       "counter %u",
				       aics_inst->index, state->gain,
				       state->mute, state->mode,
				       state->change_counter);
				aics_inst->change_counter =
					state->change_counter;
				if (aics_inst->cb &&
				    aics_inst->cb->state) {
					aics_inst->cb->state(
						conn, aics_inst->index, 0,
						state->gain, state->mute,
						state->mode);
				}
			}
		} else if (handle == aics_inst->status_handle) {
			if (length == sizeof(*status)) {
				status = (uint8_t *)data;
				BT_DBG("Index %u: Status %u",
				       aics_inst->index, *status);
				if (aics_inst->cb &&
				    aics_inst->cb->status) {
					aics_inst->cb->status(conn,
							      aics_inst->index,
							      0, *status);
				}
			}
		} else if (handle == aics_inst->desc_handle) {
			if (length > BT_ATT_MAX_ATTRIBUTE_LEN) {
				BT_DBG("Length (%u) too large", length);
				return BT_GATT_ITER_CONTINUE;
			}
			memcpy(desc, data, length);
			desc[length] = '\0';
			BT_DBG("Index %u: Input description: %s",
			       aics_inst->index, log_strdup(desc));
			if (aics_inst->cb &&
			    aics_inst->cb->description) {
				aics_inst->cb->description(conn,
							   aics_inst->index, 0,
							   desc);
			}
		}
	}
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t aics_client_read_input_state_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	struct aics_instance_t *aics_inst =
		lookup_aics_by_handle(params->single.handle);
	struct aics_state_t *state = (struct aics_state_t *)data;

	if (!aics_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("Index %u: err: 0x%02X", aics_inst->index, err);
	aics_inst->busy = false;

	if (data) {
		if (length == sizeof(*state)) {
			BT_DBG("Gain %d, mute %u, mode %u, counter %u",
			       state->gain, state->mute, state->mode,
			       state->change_counter);
			aics_inst->change_counter = state->change_counter;
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(*state));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (aics_inst->cb && aics_inst->cb->state) {
		aics_inst->cb->state(conn, aics_inst->index, cb_err,
				     state->gain, state->mute, state->mode);
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t aics_client_read_gain_settings_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	struct aics_instance_t *aics_inst =
		lookup_aics_by_handle(params->single.handle);
	struct aics_gain_settings_t *gain_settings =
		(struct aics_gain_settings_t *)data;

	if (!aics_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("Index %u: err: 0x%02X", aics_inst->index, err);
	aics_inst->busy = false;

	if (data) {
		if (length == sizeof(*gain_settings)) {
			BT_DBG("Units %u, Max %d, Min %d",
			       gain_settings->units, gain_settings->maximum,
			       gain_settings->minimum);
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(*gain_settings));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (aics_inst->cb && aics_inst->cb->gain_setting) {
		aics_inst->cb->gain_setting(conn, aics_inst->index, cb_err,
					    gain_settings->units,
					    gain_settings->minimum,
					    gain_settings->maximum);
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t aics_client_read_input_type_cb(struct bt_conn *conn, uint8_t err,
					   struct bt_gatt_read_params *params,
					   const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	uint8_t *input_type = (uint8_t *)data;
	struct aics_instance_t *aics_inst =
		lookup_aics_by_handle(params->single.handle);

	if (!aics_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("Index %u: err: 0x%02X", aics_inst->index, err);
	aics_inst->busy = false;

	if (data) {
		if (length == sizeof(*input_type)) {
			BT_DBG("Type %u", *input_type);
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(*input_type));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (aics_inst->cb && aics_inst->cb->type) {
		aics_inst->cb->type(conn, cb_err, *input_type,
				    aics_inst->index);
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t aics_client_read_input_status_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	uint8_t *status = (uint8_t *)data;
	struct aics_instance_t *aics_inst =
		lookup_aics_by_handle(params->single.handle);

	if (!aics_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("Index %u: err: 0x%02X", aics_inst->index, err);
	aics_inst->busy = false;

	if (data) {
		if (length == sizeof(*status)) {
			BT_DBG("Status %u", *status);
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(*status));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (aics_inst->cb && aics_inst->cb->status) {
		aics_inst->cb->status(conn, aics_inst->index, cb_err, *status);
	}

	return BT_GATT_ITER_STOP;
}

static void aics_cp_notify_app(struct bt_conn *conn,
			       struct aics_instance_t *aics_inst,
			       uint8_t err)
{
	struct aics_control_t *cp =
		(struct aics_control_t *)aics_inst->write_buf;

	if (!aics_inst->cb) {
		return;
	}

	switch (cp->opcode) {
	case AICS_OPCODE_SET_GAIN:
		if (aics_inst->cb->set_gain) {
			aics_inst->cb->set_gain(conn, aics_inst->index, err);
		}
		break;
	case AICS_OPCODE_UNMUTE:
		if (aics_inst->cb->unmute) {
			aics_inst->cb->unmute(conn, aics_inst->index, err);
		}
		break;
	case AICS_OPCODE_MUTE:
		if (aics_inst->cb->mute) {
			aics_inst->cb->mute(conn, aics_inst->index, err);
		}
		break;
	case AICS_OPCODE_SET_MANUAL:
		if (aics_inst->cb->set_manual_mode) {
			aics_inst->cb->set_manual_mode(conn, aics_inst->index,
						       err);
		}
		break;
	case AICS_OPCODE_SET_AUTO:
		if (aics_inst->cb->set_auto_mode) {
			aics_inst->cb->set_auto_mode(conn, aics_inst->index,
						     err);
		}
		break;
	default:
		BT_DBG("Unknown opcode 0x%02x", cp->opcode);
		break;
	}
}

static uint8_t internal_read_input_state_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = 0;
	struct aics_instance_t *aics_inst =
		lookup_aics_by_handle(params->single.handle);
	struct aics_state_t *state = (struct aics_state_t *)data;

	if (!aics_inst) {
		BT_ERR("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	if (err) {
		BT_WARN("Volume state read failed: %d", err);
		cb_err = BT_ATT_ERR_UNLIKELY;
	} else if (data) {
		if (length == sizeof(*state)) {
			int write_err;
			struct aics_control_t *cp;

			BT_DBG("Gain %d, mute %u, mode %u, counter %u",
			       state->gain, state->mute, state->mode,
			       state->change_counter);
			aics_inst->change_counter = state->change_counter;

			/* clear busy flag to reuse function */
			aics_inst->busy = false;

			cp = (struct aics_control_t *)aics_inst->write_buf;
			if (cp->opcode == AICS_OPCODE_SET_GAIN) {
				struct aics_gain_control_t *set_gain_cp =
					(struct aics_gain_control_t *)cp;

				write_err = bt_aics_client_gain_set(
					conn, aics_inst->index,
					set_gain_cp->gain_setting);
			} else {
				write_err = aics_client_common_control(
					conn, cp->opcode, aics_inst->index);
			}

			if (write_err) {
				cb_err = BT_ATT_ERR_UNLIKELY;
			}
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(*state));
			cb_err = BT_ATT_ERR_UNLIKELY;
		}
	}

	if (cb_err) {
		aics_inst->busy = false;
		aics_cp_notify_app(conn, aics_inst, cb_err);
	}

	return BT_GATT_ITER_STOP;
}


static void aics_client_write_aics_cp_cb(struct bt_conn *conn, uint8_t err,
					 struct bt_gatt_write_params *params)
{
	struct aics_instance_t *aics_inst =
		lookup_aics_by_handle(params->handle);

	if (!aics_inst) {
		BT_DBG("Instance not found");
		return;
	}

	BT_DBG("Index %u: err: 0x%02X", aics_inst->index, err);

	if (err == AICS_ERR_INVALID_COUNTER && aics_inst->state_handle) {
		int read_err;

		aics_inst->read_params.func = internal_read_input_state_cb;
		aics_inst->read_params.handle_count = 1;
		aics_inst->read_params.single.handle = aics_inst->state_handle;
		aics_inst->read_params.single.offset = 0U;

		read_err = bt_gatt_read(conn, &aics_inst->read_params);

		if (read_err) {
			BT_WARN("Could not read Volume state: %d", read_err);
		} else {
			return;
		}
	}

	aics_inst->busy = false;

	aics_cp_notify_app(conn, aics_inst, err);
}

static int aics_client_common_control(struct bt_conn *conn, uint8_t opcode,
				      uint8_t index)
{
	int err;
	struct aics_instance_t *aics_inst;
	struct aics_control_t *cp;

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->control_handle) {
			BT_DBG("Handle not set for opcode %u", opcode);
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		}

		cp = (struct aics_control_t *)aics_inst->write_buf;
		cp->opcode = opcode;
		cp->counter = aics_inst->change_counter;
		aics_inst->write_params.offset = 0;
		aics_inst->write_params.data = aics_inst->write_buf;
		aics_inst->write_params.length =
			sizeof(opcode) + sizeof(aics_inst->change_counter);
		aics_inst->write_params.handle = aics_inst->control_handle;
		aics_inst->write_params.func = aics_client_write_aics_cp_cb;

		err = bt_gatt_write(conn, &aics_inst->write_params);
		if (!err) {
			aics_inst->busy = true;
		}
		return err;
	}

	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}


static uint8_t aics_client_read_input_desc_cb(struct bt_conn *conn, uint8_t err,
					   struct bt_gatt_read_params *params,
					   const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	char desc[MIN(CONFIG_BT_L2CAP_RX_MTU, BT_ATT_MAX_ATTRIBUTE_LEN) + 1];
	struct aics_instance_t *aics_inst =
		lookup_aics_by_handle(params->single.handle);

	if (!aics_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	aics_inst->busy = false;

	if (err) {
		BT_DBG("err: 0x%02X", err);
	} else if (data) {
		BT_HEXDUMP_DBG(data, length, "Input description read");

		if (length > BT_ATT_MAX_ATTRIBUTE_LEN) {
			BT_DBG("Length (%u) too large", length);
			return BT_GATT_ITER_CONTINUE;
		}

		/* TODO: Handle long reads */

		memcpy(desc, data, length);
		desc[length] = '\0';
		BT_DBG("Input description: %s", log_strdup(desc));
	}

	if (aics_inst->cb && aics_inst->cb->description) {
		aics_inst->cb->description(conn, aics_inst->index, cb_err,
					   desc);
	}

	return BT_GATT_ITER_STOP;
}

int bt_aics_client_register(struct aics_instance_t *aics_inst, uint8_t index)
{
	BT_DBG("%u", index);
	if (ARRAY_SIZE(aics_insts) > 0) {
		if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}
		aics_insts[index] = aics_inst;
	}
	return 0;
}

int bt_aics_client_unregister(uint8_t index)
{
	return bt_aics_client_register(NULL, index);
}

int bt_aics_client_input_state_get(struct bt_conn *conn, uint8_t index)
{
	int err;
	struct aics_instance_t *aics_inst;

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->state_handle) {
			BT_DBG("Handle not set");
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		}

		aics_inst->read_params.func = aics_client_read_input_state_cb;
		aics_inst->read_params.handle_count = 1;
		aics_inst->read_params.single.handle = aics_inst->state_handle;
		aics_inst->read_params.single.offset = 0U;

		err = bt_gatt_read(conn, &aics_inst->read_params);
		if (!err) {
			aics_inst->busy = true;
		}
		return err;
	}
	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}

int bt_aics_client_gain_setting_get(struct bt_conn *conn, uint8_t index)
{
	int err;
	struct aics_instance_t *aics_inst;

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->gain_handle) {
			BT_DBG("Handle not set");
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		}

		aics_inst->read_params.func = aics_client_read_gain_settings_cb;
		aics_inst->read_params.handle_count = 1;
		aics_inst->read_params.single.handle = aics_inst->gain_handle;
		aics_inst->read_params.single.offset = 0U;

		err = bt_gatt_read(conn, &aics_inst->read_params);
		if (!err) {
			aics_inst->busy = true;
		}
		return err;
	}

	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}

int bt_aics_client_input_type_get(struct bt_conn *conn, uint8_t index)
{
	int err;
	struct aics_instance_t *aics_inst;

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->type_handle) {
			BT_DBG("Handle not set");
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		}

		aics_inst->read_params.func = aics_client_read_input_type_cb;
		aics_inst->read_params.handle_count = 1;
		aics_inst->read_params.single.handle = aics_inst->type_handle;
		aics_inst->read_params.single.offset = 0U;

		err = bt_gatt_read(conn, &aics_inst->read_params);
		if (!err) {
			aics_inst->busy = true;
		}
		return err;
	}

	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}

int bt_aics_client_input_status_get(struct bt_conn *conn, uint8_t index)
{
	int err;
	struct aics_instance_t *aics_inst;

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->status_handle) {
			BT_DBG("Handle not set");
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		}

		aics_inst->read_params.func = aics_client_read_input_status_cb;
		aics_inst->read_params.handle_count = 1;
		aics_inst->read_params.single.handle = aics_inst->status_handle;
		aics_inst->read_params.single.offset = 0U;

		err = bt_gatt_read(conn, &aics_inst->read_params);
		if (!err) {
			aics_inst->busy = true;
		}
		return err;
	}

	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}

int bt_aics_client_input_unmute(struct bt_conn *conn, uint8_t index)
{
	return aics_client_common_control(conn, AICS_OPCODE_UNMUTE, index);
}

int bt_aics_client_input_mute(struct bt_conn *conn, uint8_t index)
{
	return aics_client_common_control(conn, AICS_OPCODE_MUTE, index);
}

int bt_aics_client_manual_input_gain_set(struct bt_conn *conn, uint8_t index)
{
	return aics_client_common_control(conn, AICS_OPCODE_SET_MANUAL,
					index);
}

int bt_aics_client_automatic_input_gain_set(struct bt_conn *conn,
					    uint8_t index)
{
	return aics_client_common_control(conn, AICS_OPCODE_SET_AUTO, index);
}

int bt_aics_client_gain_set(struct bt_conn *conn, uint8_t index, int8_t gain)
{
	int err;
	struct aics_instance_t *aics_inst;
	struct aics_gain_control_t cp = {
		.cp.opcode = AICS_OPCODE_SET_GAIN,
		.gain_setting = gain
	};

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->control_handle) {
			BT_DBG("Handle not set");
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		}

		cp.cp.counter = aics_inst->change_counter;

		memcpy(aics_inst->write_buf, &cp, sizeof(cp));
		aics_inst->write_params.offset = 0;
		aics_inst->write_params.data = aics_inst->write_buf;
		aics_inst->write_params.length = sizeof(cp);
		aics_inst->write_params.handle = aics_inst->control_handle;
		aics_inst->write_params.func = aics_client_write_aics_cp_cb;

		err = bt_gatt_write(conn, &aics_inst->write_params);
		if (!err) {
			aics_inst->busy = true;
		}
		return err;
	}

	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}

int bt_aics_client_input_description_get(struct bt_conn *conn, uint8_t index)
{
	int err;
	struct aics_instance_t *aics_inst;

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->desc_handle) {
			BT_DBG("Handle not set");
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		}

		aics_inst->read_params.func = aics_client_read_input_desc_cb;
		aics_inst->read_params.handle_count = 1;
		aics_inst->read_params.single.handle = aics_inst->desc_handle;
		aics_inst->read_params.single.offset = 0U;

		err = bt_gatt_read(conn, &aics_inst->read_params);
		if (!err) {
			aics_inst->busy = true;
		}
		return err;
	}

	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}

int bt_aics_client_input_description_set(struct bt_conn *conn, uint8_t index,
					 const char *description)
{
	int err;
	struct aics_instance_t *aics_inst;

	if (ARRAY_SIZE(aics_insts) > 0) {
		if (!conn) {
			return -ENOTCONN;
		} else if (index >= ARRAY_SIZE(aics_insts)) {
			return -EINVAL;
		}

		aics_inst = aics_insts[index];

		if (!aics_inst) {
			return -EINVAL;
		} else if (!aics_inst->desc_handle) {
			BT_DBG("Handle not set");
			return -EINVAL;
		} else if (aics_inst->busy) {
			return -EBUSY;
		} else if (!aics_inst->desc_writable) {
			BT_DBG("Description is not writable on peer "
			       "service instance");
			return -EPERM;
		}

		err = bt_gatt_write_without_response(conn,
						     aics_inst->desc_handle,
						     description,
						     strlen(description),
						     false);
		return err;
	}

	BT_DBG("Not supported");
	return -EOPNOTSUPP;
}
