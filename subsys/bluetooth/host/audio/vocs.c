/*  Bluetooth VOCS
 *
 * Copyright (c) 2020 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/byteorder.h>

#include <device.h>
#include <init.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/services/vocs.h>

#include "vocs_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_VOCS)
#define LOG_MODULE_NAME bt_vocs
#include "common/log.h"

#define VALID_VOCS_OPCODE(opcode)	((opcode) == VOCS_OPCODE_SET_OFFSET)

static void offset_state_cfg_changed(const struct bt_gatt_attr *attr,
				     uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t read_offset_state(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 void *buf, uint16_t len, uint16_t offset)
{
	struct bt_vocs *inst = attr->user_data;

	BT_DBG("offset %d, counter %u",
	       inst->state.offset, inst->state.change_counter);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->state, sizeof(inst->state));
}

static void location_cfg_changed(const struct bt_gatt_attr *attr,
				 uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t write_location(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len, uint16_t offset,
			      uint8_t flags)
{
	struct bt_vocs *inst = attr->user_data;
	uint8_t location;

	if (len != sizeof(inst->location)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(&location, buf, len);
	BT_DBG("%02x", location);

	if (location != inst->location) {
		inst->location = location;
		bt_gatt_notify_uuid(NULL, BT_UUID_VOCS_LOCATION,
				    inst->service_p->attrs,
				    &inst->location, sizeof(inst->location));

		if (inst->cb && inst->cb->location) {
			inst->cb->location(NULL, inst->index, 0,
					   inst->location);
		}
	}

	return len;
}

static ssize_t read_location(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	struct bt_vocs *inst = attr->user_data;

	BT_DBG("0x%02x", inst->location);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->location,
				 sizeof(inst->location));
}

static ssize_t write_vocs_control(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr,
				  const void *buf, uint16_t len,
				  uint16_t offset, uint8_t flags)
{
	struct bt_vocs *inst = attr->user_data;
	const struct vocs_control_t *cp = buf;
	bool notify = false;

	if (!len || !buf) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	/* Check opcode before length */
	if (!VALID_VOCS_OPCODE(cp->opcode)) {
		BT_DBG("Invalid opcode %u", cp->opcode);
		return BT_GATT_ERR(VOCS_ERR_OP_NOT_SUPPORTED);
	}

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len != sizeof(struct vocs_control_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	BT_DBG("Opcode %u, counter %u", cp->opcode, cp->counter);


	if (cp->counter != inst->state.change_counter) {
		return BT_GATT_ERR(VOCS_ERR_INVALID_COUNTER);
	}

	switch (cp->opcode) {
	case VOCS_OPCODE_SET_OFFSET:
		BT_DBG("Set offset %d", cp->offset);
		if (cp->offset > UINT8_MAX || cp->offset < -UINT8_MAX) {
			return BT_GATT_ERR(VOCS_ERR_OUT_OF_RANGE);
		}

		if (inst->state.offset != sys_le16_to_cpu(cp->offset)) {
			inst->state.offset = sys_le16_to_cpu(cp->offset);
			notify = true;
		}
		break;
	default:
		return BT_GATT_ERR(VOCS_ERR_OP_NOT_SUPPORTED);
	}

	if (notify) {
		inst->state.change_counter++;
		BT_DBG("New state: offset %d, counter %u",
		       inst->state.offset, inst->state.change_counter);
		bt_gatt_notify_uuid(NULL, BT_UUID_VOCS_STATE,
				    inst->service_p->attrs,
				    &inst->state, sizeof(inst->state));

		if (inst->cb && inst->cb->state) {
			inst->cb->state(NULL, inst->index, 0,
					inst->state.offset);
		}

	}

	return len;
}

static void output_desc_cfg_changed(const struct bt_gatt_attr *attr,
				    uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t write_output_desc(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset,
				 uint8_t flags)
{
	struct bt_vocs *inst = attr->user_data;

	if (len >= sizeof(inst->output_desc)) {
		BT_DBG("Output desc was clipped from length %u to %zu",
		       len, sizeof(inst->output_desc) - 1);
		/* We just clip the string value if it's too long */
		len = sizeof(inst->output_desc) - 1;
	}

	if (len != strlen(inst->output_desc) ||
	    memcmp(buf, inst->output_desc, len)) {
		memcpy(inst->output_desc, buf, len);
		inst->output_desc[len] = '\0';

		bt_gatt_notify_uuid(NULL, BT_UUID_VOCS_DESCRIPTION,
				    inst->service_p->attrs,
				    &inst->output_desc,
				    strlen(inst->output_desc));

		if (inst->cb && inst->cb->description) {
			inst->cb->description(NULL, inst->index, 0,
					      inst->output_desc);
		}
	}

	BT_DBG("%s", log_strdup(inst->output_desc));

	return len;
}

static ssize_t read_output_desc(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	struct bt_vocs *inst = attr->user_data;

	BT_DBG("%s", log_strdup(inst->output_desc));
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &inst->output_desc,
				 strlen(inst->output_desc));
}

#define BT_VOCS_SERVICE_DEFINITION(_vocs) { \
	BT_GATT_SECONDARY_SERVICE(BT_UUID_VOCS), \
	BT_GATT_CHARACTERISTIC(BT_UUID_VOCS_STATE, \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
			       BT_GATT_PERM_READ_ENCRYPT, \
			       read_offset_state, NULL, &_vocs), \
	BT_GATT_CCC(offset_state_cfg_changed, \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT), \
	BT_GATT_CHARACTERISTIC(BT_UUID_VOCS_LOCATION, \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
			       BT_GATT_PERM_READ_ENCRYPT, \
			       read_location, NULL, &_vocs), \
	BT_GATT_CCC(location_cfg_changed, \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT), \
	BT_GATT_CHARACTERISTIC(BT_UUID_VOCS_CONTROL, \
			       BT_GATT_CHRC_WRITE, \
			       BT_GATT_PERM_WRITE_ENCRYPT, \
			       NULL, write_vocs_control, &_vocs), \
	BT_GATT_CHARACTERISTIC(BT_UUID_VOCS_DESCRIPTION, \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
			       BT_GATT_PERM_READ_ENCRYPT, \
			       read_output_desc, NULL, &_vocs), \
	BT_GATT_CCC(output_desc_cfg_changed, \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT) \
	}

static struct bt_vocs vocs_insts[CONFIG_BT_VOCS_MAX_INSTANCE_COUNT];
static uint32_t instance_cnt;
BT_GATT_SERVICE_INSTANCE_DEFINE(vocs_service_list, vocs_insts,
				CONFIG_BT_VOCS_MAX_INSTANCE_COUNT,
				BT_VOCS_SERVICE_DEFINITION);

static int vocs_init(const struct device *unused)
{
	for (int i = 0; i < ARRAY_SIZE(vocs_insts); i++) {
		vocs_insts[i].index = i;
		vocs_insts[i].service_p = &vocs_service_list[i];
	}
	return 0;
}

DEVICE_INIT(bt_vocs, "bt_vocs", &vocs_init, NULL, NULL, APPLICATION,
	    CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

void *bt_vocs_svc_decl_get(struct bt_vocs *vocs)
{
	return vocs->service_p->attrs;
}

int bt_vocs_init(struct bt_vocs *vocs, struct bt_vocs_init *init)
{
	int err;

	if (vocs->initialized) {
		return -EALREADY;
	}

	if (init) {
		struct bt_gatt_attr *attr;
		struct bt_gatt_chrc *chrc;

		if (init->offset > VOCS_MAX_OFFSET ||
		    init->offset < VOCS_MIN_OFFSET) {
			BT_DBG("Invalid offset %d", init->offset);
			return -EINVAL;
		}

		vocs->location = init->location;
		vocs->state.offset = init->offset;
		strncpy(vocs->output_desc, init->output_desc,
			sizeof(vocs->output_desc));

		for (int i = 0; i < vocs->service_p->attr_count; i++) {
			attr = &vocs->service_p->attrs[i];

			if (init->location_writable &&
			    !bt_uuid_cmp(attr->uuid, BT_UUID_VOCS_LOCATION)) {
				/* Update attr and chrc to be writable */
				chrc = vocs->service_p->attrs[i - 1].user_data;
				attr->write = write_location;
				attr->perm |= BT_GATT_PERM_WRITE_ENCRYPT;
				chrc->properties |=
					BT_GATT_CHRC_WRITE_WITHOUT_RESP;
			} else if (init->desc_writable &&
				   !bt_uuid_cmp(attr->uuid,
						BT_UUID_VOCS_DESCRIPTION)) {
				/* Update attr and chrc to be writable */
				chrc = vocs->service_p->attrs[i - 1].user_data;
				attr->write = write_output_desc;
				attr->perm |= BT_GATT_PERM_WRITE_ENCRYPT;
				chrc->properties |=
					BT_GATT_CHRC_WRITE_WITHOUT_RESP;
			}
		}
	}

	err = bt_gatt_service_register(vocs->service_p);
	if (err) {
		BT_DBG("Could not register VOCS service");
		return err;
	}

	vocs->initialized = true;
	return 0;
}

struct bt_vocs *bt_vocs_free_instance_get(void)
{
	if (instance_cnt >= CONFIG_BT_VOCS_MAX_INSTANCE_COUNT) {
		return NULL;
	}

	return &vocs_insts[instance_cnt++];
}

int bt_vocs_offset_state_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(vocs_insts)) {
		return -ERANGE;
	}

	if (vocs_insts[index].cb && vocs_insts[index].cb->state) {
		vocs_insts[index].cb->state(
			NULL, vocs_insts[index].index, 0,
			vocs_insts[index].state.offset);
	}

	return 0;
}

int bt_vocs_location_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(vocs_insts)) {
		return -ERANGE;
	}

	if (vocs_insts[index].cb && vocs_insts[index].cb->location) {
		vocs_insts[index].cb->location(
			NULL, vocs_insts[index].index, 0,
			vocs_insts[index].location);
	}

	return 0;
}

int bt_vocs_location_set(uint8_t index, uint8_t location)
{
	struct bt_gatt_attr attr;
	int err;

	if (index >= ARRAY_SIZE(vocs_insts)) {
		return -ERANGE;
	}

	attr.user_data = &vocs_insts[index];

	err = write_location(NULL, &attr, &location, sizeof(location), 0, 0);

	return err > 0 ? 0 : err;
}

int bt_vocs_state_set(uint8_t index, int16_t offset)
{
	struct bt_gatt_attr attr;
	struct vocs_control_t cp;
	int err;

	if (index >= ARRAY_SIZE(vocs_insts)) {
		return -ERANGE;
	}

	cp.opcode = VOCS_OPCODE_SET_OFFSET;
	cp.counter = vocs_insts[index].state.change_counter;
	cp.offset = sys_cpu_to_le16(offset);

	attr.user_data = &vocs_insts[index];

	err = write_vocs_control(NULL, &attr, &cp, sizeof(cp), 0, 0);

	return err > 0 ? 0 : err;
}

int bt_vocs_output_description_get(uint8_t index)
{
	if (index >= ARRAY_SIZE(vocs_insts)) {
		return -ERANGE;
	}

	if (vocs_insts[index].cb && vocs_insts[index].cb->description) {
		vocs_insts[index].cb->description(
			NULL, vocs_insts[index].index, 0,
			vocs_insts[index].output_desc);
	}

	return 0;
}

int bt_vocs_output_description_set(uint8_t index, const char *description)
{
	struct bt_gatt_attr attr;
	int err;

	if (index >= ARRAY_SIZE(vocs_insts)) {
		return -ERANGE;
	}

	attr.user_data = &vocs_insts[index];

	err = write_output_desc(NULL, &attr, description, strlen(description),
				0, 0);
	return err > 0 ? 0 : err;
}

int bt_vocs_cb_register(uint8_t index, struct bt_vocs_cb *cb)
{
	if (index < ARRAY_SIZE(vocs_insts)) {
		vocs_insts[index].cb = cb;
	} else {
		return -ERANGE;
	}

	return 0;
}
