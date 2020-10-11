/*  Bluetooth AICS
 *
 * Copyright (c) 2020 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AICS is a secondary service that can be included by other services.
 */

#include <zephyr.h>
#include <sys/byteorder.h>

#include <device.h>
#include <init.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/services/aics.h>

#include "aics_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_AICS)
#define LOG_MODULE_NAME bt_aics
#include "common/log.h"

#define VALID_AICS_OPCODE(opcode)                                              \
	((opcode) >= AICS_OPCODE_SET_GAIN && (opcode) <= AICS_OPCODE_SET_AUTO)

#define AICS_CP_LEN                 0x02
#define AICS_CP_SET_GAIN_LEN        0x03

void aics_state_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

ssize_t read_aics_state(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset);

ssize_t read_aics_gain_settings(struct bt_conn *conn,
				const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset);

ssize_t read_input_type(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset);

void aics_input_status_cfg_changed(const struct bt_gatt_attr *attr,
				   uint16_t value);

ssize_t read_input_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset);

ssize_t write_aics_control(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags);

void aics_input_desc_cfg_changed(const struct bt_gatt_attr *attr,
				 uint16_t value);

ssize_t write_input_desc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags);

ssize_t read_input_desc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset);

#define BT_AICS_SERVICE_DEFINITION(_aics) {                                    \
	BT_GATT_SECONDARY_SERVICE(BT_UUID_AICS),                               \
	BT_GATT_CHARACTERISTIC(BT_UUID_AICS_STATE,                             \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,        \
			       BT_GATT_PERM_READ_ENCRYPT,                      \
			       read_aics_state, NULL, &_aics),                 \
	BT_GATT_CCC(aics_state_cfg_changed,                                    \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),           \
	BT_GATT_CHARACTERISTIC(BT_UUID_AICS_GAIN_SETTINGS,                     \
			       BT_GATT_CHRC_READ,                              \
			       BT_GATT_PERM_READ_ENCRYPT,                      \
			       read_aics_gain_settings, NULL, &_aics),         \
	BT_GATT_CHARACTERISTIC(BT_UUID_AICS_INPUT_TYPE,                        \
			       BT_GATT_CHRC_READ,                              \
			       BT_GATT_PERM_READ_ENCRYPT,                      \
			       read_input_type, NULL, &_aics),                 \
	BT_GATT_CHARACTERISTIC(BT_UUID_AICS_INPUT_STATUS,                      \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,        \
			       BT_GATT_PERM_READ_ENCRYPT,                      \
			       read_input_status, NULL, &_aics),               \
	BT_GATT_CCC(aics_input_status_cfg_changed,                             \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),           \
	BT_GATT_CHARACTERISTIC(BT_UUID_AICS_CONTROL,                           \
			       BT_GATT_CHRC_WRITE,                             \
			       BT_GATT_PERM_WRITE_ENCRYPT,                     \
			       NULL, write_aics_control, &_aics),              \
	BT_GATT_CHARACTERISTIC(BT_UUID_AICS_DESCRIPTION,                       \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,        \
			       BT_GATT_PERM_READ_ENCRYPT,                      \
			       read_input_desc, NULL, &_aics),     \
	BT_GATT_CCC(aics_input_desc_cfg_changed,                               \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT)            \
	}


static struct bt_aics aics_insts[CONFIG_BT_AICS_MAX_INSTANCE_COUNT];
static uint32_t instance_cnt;
BT_GATT_SERVICE_INSTANCE_DEFINE(aics_service_list, aics_insts,
				CONFIG_BT_AICS_MAX_INSTANCE_COUNT,
				BT_AICS_SERVICE_DEFINITION);

void aics_state_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

ssize_t read_aics_state(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	struct bt_aics *inst = attr->user_data;

	BT_DBG("gain %d, mute %u, mode %u, counter %u",
	       inst->state.gain, inst->state.mute, inst->state.mode,
	       inst->state.change_counter);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->state, sizeof(inst->state));
}

ssize_t read_aics_gain_settings(struct bt_conn *conn,
				const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	struct bt_aics *inst = attr->user_data;
	BT_DBG("units %u, min %d, max %d", inst->gain_settings.units,
	       inst->gain_settings.minimum,
	       inst->gain_settings.maximum);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->gain_settings,
				 sizeof(inst->gain_settings));
}

ssize_t read_input_type(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	struct bt_aics *inst = attr->user_data;

	BT_DBG("%u", inst->type);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->type, sizeof(inst->type));
}

void aics_input_status_cfg_changed(const struct bt_gatt_attr *attr,
				   uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

ssize_t read_input_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	struct bt_aics *inst = attr->user_data;

		BT_DBG("%u", inst->status);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->status, sizeof(inst->status));
}

ssize_t write_aics_control(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	struct bt_aics *inst = attr->user_data;
	const struct aics_gain_control_t *cp = buf;
	bool notify = false;

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (!len || !buf) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	/* Check opcode before length */
	if (!VALID_AICS_OPCODE(cp->cp.opcode)) {
		BT_DBG("Invalid opcode %u", cp->cp.opcode);
		return BT_GATT_ERR(AICS_ERR_OP_NOT_SUPPORTED);
	}

	if ((len < AICS_CP_LEN) ||
	    (len == AICS_CP_SET_GAIN_LEN &&
	     cp->cp.opcode != AICS_OPCODE_SET_GAIN) ||
	    (len > AICS_CP_SET_GAIN_LEN)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	BT_DBG("Opcode %u, counter %u", cp->cp.opcode, cp->cp.counter);
	if (cp->cp.counter != inst->state.change_counter) {
		return BT_GATT_ERR(AICS_ERR_INVALID_COUNTER);
	}

	switch (cp->cp.opcode) {
	case AICS_OPCODE_SET_GAIN:
		BT_DBG("Set gain %d", cp->gain_setting);
		if (cp->gain_setting < inst->gain_settings.minimum ||
		    cp->gain_setting > inst->gain_settings.maximum) {
			return BT_GATT_ERR(AICS_ERR_OUT_OF_RANGE);
		}
		if (AICS_INPUT_MODE_SETTABLE(inst->state.mode) &&
		    inst->state.gain != cp->gain_setting) {
			inst->state.gain = cp->gain_setting;
			notify = true;
		}
		break;
	case AICS_OPCODE_UNMUTE:
		BT_DBG("Unmute");
		if (inst->state.mute == AICS_STATE_MUTE_DISABLED) {
			return BT_GATT_ERR(AICS_ERR_MUTE_DISABLED);
		}
		if (inst->state.mute != AICS_STATE_UNMUTED) {
			inst->state.mute = AICS_STATE_UNMUTED;
			notify = true;
		}
		break;
	case AICS_OPCODE_MUTE:
		BT_DBG("Mute");
		if (inst->state.mute == AICS_STATE_MUTE_DISABLED) {
			return BT_GATT_ERR(AICS_ERR_MUTE_DISABLED);
		}
		if (inst->state.mute != AICS_STATE_MUTED) {
			inst->state.mute = AICS_STATE_MUTED;
			notify = true;
		}
		break;
	case AICS_OPCODE_SET_MANUAL:
		BT_DBG("Set manual mode");
		if (AICS_INPUT_MODE_IMMUTABLE(inst->state.mode)) {
			return BT_GATT_ERR(AICS_ERR_GAIN_MODE_NO_SUPPORT);
		}
		if (inst->state.mode != AICS_MODE_MANUAL) {
			inst->state.mode = AICS_MODE_MANUAL;
			notify = true;
		}
		break;
	case AICS_OPCODE_SET_AUTO:
		BT_DBG("Set automatic mode");
		if (AICS_INPUT_MODE_IMMUTABLE(inst->state.mode)) {
			return BT_GATT_ERR(AICS_ERR_GAIN_MODE_NO_SUPPORT);
		}
		if (inst->state.mode != AICS_MODE_AUTO) {
			inst->state.mode = AICS_MODE_AUTO;
			notify = true;
		}
		break;
	default:
		return BT_GATT_ERR(AICS_ERR_OP_NOT_SUPPORTED);
	}

	if (notify) {
		inst->state.change_counter++;

		BT_DBG("New state: gain %d, mute %u, mode %u, counter %u",
		       inst->state.gain, inst->state.mute, inst->state.mode,
		       inst->state.change_counter);
		bt_gatt_notify_uuid(NULL, BT_UUID_AICS_STATE,
				    inst->service_p->attrs, &inst->state,
				    sizeof(inst->state));

		if (inst->cb && inst->cb->state) {
			inst->cb->state(NULL, inst->index, 0, inst->state.gain,
					inst->state.mute, inst->state.mode);
		}
	}

	return len;
}

void aics_input_desc_cfg_changed(const struct bt_gatt_attr *attr,
				 uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

ssize_t write_input_desc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags)
{
	struct bt_aics *inst = attr->user_data;

	if (len >= sizeof(inst->input_desc)) {
		BT_DBG("Output desc was clipped from length %u to %zu",
		       len, sizeof(inst->input_desc) - 1);
		/* We just clip the string value if it's too long */
		len = sizeof(inst->input_desc) - 1;
	}

	if (memcmp(buf, inst->input_desc, len)) {
		memcpy(inst->input_desc, buf, len);
		inst->input_desc[len] = '\0';

		bt_gatt_notify_uuid(NULL, BT_UUID_AICS_DESCRIPTION,
				    inst->service_p->attrs, &inst->input_desc,
				    strlen(inst->input_desc));

		if (inst->cb && inst->cb->description) {
			inst->cb->description(NULL, inst->index, 0,
					      inst->input_desc);
		}
	}

	BT_DBG("%s", log_strdup(inst->input_desc));

	return len;
}

ssize_t read_input_desc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	struct bt_aics *inst = attr->user_data;

	BT_DBG("%s", log_strdup(inst->input_desc));
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->input_desc,
				 strlen(inst->input_desc));
}

static int aics_init(const struct device *unused)
{
	for (int i = 0; i < ARRAY_SIZE(aics_insts); i++) {
		aics_insts[i].index = i;
		aics_insts[i].service_p = &aics_service_list[i];
	}
	return 0;
}

DEVICE_INIT(bt_aics, "bt_aics", &aics_init, NULL, NULL, APPLICATION,
	    CONFIG_KERNEL_INIT_PRIORITY_DEVICE);


void *bt_aics_svc_decl_get(struct bt_aics *aics)
{
	return aics->service_p->attrs;
}

int bt_aics_init(struct bt_aics *aics, struct bt_aics_init *init)
{
	int err;

	if (aics->initialized) {
		return -EALREADY;
	}

	if (init) {
		struct bt_gatt_attr *attr;
		struct bt_gatt_chrc *chrc;

		if (init->mute > AICS_STATE_MUTE_DISABLED ||
		    init->mode > AICS_MODE_AUTO ||
		    (init->input_type > AICS_INPUT_TYPE_NETWORK &&
			init->input_type != AICS_INPUT_TYPE_OTHER)) {
			return -EINVAL;
		}

		aics->state.gain = init->gain;
		aics->state.mute = init->mute;
		aics->state.mode = init->mode;
		aics->gain_settings.units = init->units;
		aics->gain_settings.minimum = init->min_gain;
		aics->gain_settings.maximum = init->max_gain;
		aics->type = init->input_type;
		aics->status = init->input_state;

		strncpy(aics->input_desc, init->input_desc,
			sizeof(aics->input_desc));

		for (int i = 0; i < aics->service_p->attr_count; i++) {
			attr = &aics->service_p->attrs[i];

			if (init->desc_writable &&
			    !bt_uuid_cmp(attr->uuid,
					 BT_UUID_AICS_DESCRIPTION)) {
				/* Update attr and chrc to be writable */
				chrc = aics->service_p->attrs[i - 1].user_data;
				attr->write = write_input_desc;
				attr->perm |= BT_GATT_PERM_WRITE_ENCRYPT;
				chrc->properties |=
					BT_GATT_CHRC_WRITE_WITHOUT_RESP;

				break;
			}
		}
	}

	err = bt_gatt_service_register(aics->service_p);
	if (err) {
		BT_DBG("Could not register AICS service");
		return err;
	}

	aics->initialized = true;
	return 0;
}

struct bt_aics *bt_aics_free_instance_get(void)
{
	if (instance_cnt >= CONFIG_BT_AICS_MAX_INSTANCE_COUNT) {
		return NULL;
	}

	return &aics_insts[instance_cnt++];
}

/****************************** PUBLIC API ******************************/
int bt_aics_deactivate(uint8_t index)
{
	struct bt_aics *inst;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -EINVAL;
	}

	inst = &aics_insts[index];

	if (inst->status == AICS_STATUS_ACTIVE) {
		inst->status = AICS_STATUS_INACTIVE;
		BT_DBG("Index %u: Status was set to inactive", index);

		bt_gatt_notify_uuid(NULL, BT_UUID_AICS_INPUT_STATUS,
				    inst->service_p->attrs,
				    &inst->status, sizeof(inst->status));


		if (inst->cb && inst->cb->status) {
			inst->cb->status(NULL, inst->index, 0, inst->status);
		}
	}

	return 0;
}

int bt_aics_activate(uint8_t index)
{
	struct bt_aics *inst;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -EINVAL;
	}

	inst = &aics_insts[index];

	if (inst->status == AICS_STATUS_INACTIVE) {
		inst->status = AICS_STATUS_ACTIVE;
		BT_DBG("Index %u: Status was set to active", index);

		bt_gatt_notify_uuid(NULL, BT_UUID_AICS_INPUT_STATUS,
				    inst->service_p->attrs,
				    &inst->status, sizeof(inst->status));


		if (inst->cb && inst->cb->status) {
			inst->cb->status(NULL, inst->index, 0, inst->status);
		}
	}

	return 0;
}

int bt_aics_cb_register(uint8_t index, struct bt_aics_cb *cb)
{
	if (index < ARRAY_SIZE(aics_insts)) {
		aics_insts[index].cb = cb;
	} else {
		return -ERANGE;
	}

	return 0;
}

int bt_aics_input_state_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	if (aics_insts[index].cb && aics_insts[index].cb->state) {
		aics_insts[index].cb->state(NULL, aics_insts[index].index, 0,
					    aics_insts[index].state.gain,
					    aics_insts[index].state.mute,
					    aics_insts[index].state.mode);
	}

	return 0;
}

int bt_aics_gain_setting_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	if (aics_insts[index].cb && aics_insts[index].cb->gain_setting) {
		aics_insts[index].cb->gain_setting(
			NULL, aics_insts[index].index, 0,
			aics_insts[index].gain_settings.units,
			aics_insts[index].gain_settings.minimum,
			aics_insts[index].gain_settings.maximum);
	}

	return 0;
}

int bt_aics_input_type_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	if (aics_insts[index].cb && aics_insts[index].cb->type) {
		aics_insts[index].cb->type(NULL, aics_insts[index].index, 0,
					   aics_insts[index].type);
	}

	return 0;
}

int bt_aics_input_status_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	if (aics_insts[index].cb && aics_insts[index].cb->status) {
		aics_insts[index].cb->status(NULL, aics_insts[index].index, 0,
					     aics_insts[index].status);
	}

	return 0;
}

int bt_aics_input_unmute(uint8_t index)
{
	struct bt_gatt_attr attr;
	struct aics_gain_control_t cp;
	int err;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	cp.cp.opcode = AICS_OPCODE_UNMUTE;
	cp.cp.counter = aics_insts[index].state.change_counter;

	attr.user_data = &aics_insts[index];

	err = write_aics_control(NULL, &attr, &cp, sizeof(cp), 0, 0);

	return err > 0 ? 0 : err;
}

int bt_aics_input_mute(uint8_t index)
{
	struct bt_gatt_attr attr;
	struct aics_gain_control_t cp;
	int err;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	cp.cp.opcode = AICS_OPCODE_MUTE;
	cp.cp.counter = aics_insts[index].state.change_counter;

	attr.user_data = &aics_insts[index];

	err = write_aics_control(NULL, &attr, &cp, sizeof(cp), 0, 0);

	return err > 0 ? 0 : err;
}

int bt_aics_manual_input_gain_set(uint8_t index)
{
	struct bt_gatt_attr attr;
	struct aics_gain_control_t cp;
	int err;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	cp.cp.opcode = AICS_OPCODE_SET_MANUAL;
	cp.cp.counter = aics_insts[index].state.change_counter;

	attr.user_data = &aics_insts[index];

	err = write_aics_control(NULL, &attr, &cp, sizeof(cp), 0, 0);

	return err > 0 ? 0 : err;
}

int bt_aics_automatic_input_gain_set(uint8_t index)
{
	struct bt_gatt_attr attr;
	struct aics_gain_control_t cp;
	int err;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	cp.cp.opcode = AICS_OPCODE_SET_AUTO;
	cp.cp.counter = aics_insts[index].state.change_counter;

	attr.user_data = &aics_insts[index];

	err = write_aics_control(NULL, &attr, &cp, sizeof(cp), 0, 0);

	return err > 0 ? 0 : err;
}

int bt_aics_gain_set(uint8_t index, int8_t gain)
{
	struct bt_gatt_attr attr;
	struct aics_gain_control_t cp;
	int err;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	cp.cp.opcode = AICS_OPCODE_SET_GAIN;
	cp.cp.counter = aics_insts[index].state.change_counter;
	cp.gain_setting = gain;

	attr.user_data = &aics_insts[index];

	err = write_aics_control(NULL, &attr, &cp, sizeof(cp), 0, 0);

	return err > 0 ? 0 : err;
}

int bt_aics_input_description_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	if (aics_insts[index].cb && aics_insts[index].cb->description) {
		aics_insts[index].cb->description(NULL, aics_insts[index].index,
						  0,
						  aics_insts[index].input_desc);
	}

	return 0;
}

int bt_aics_input_description_set(uint8_t index, const char *description)
{
	struct bt_gatt_attr attr;
	int err;

	if (index >= ARRAY_SIZE(aics_insts)) {
		return -ERANGE;
	}

	attr.user_data = &aics_insts[index];

	err = write_input_desc(NULL, &attr, &description, strlen(description),
			       0, 0);

	return err > 0 ? 0 : err;
}
