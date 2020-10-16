/*  Bluetooth TBS - Call Control Profile - Client */

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
#include <bluetooth/services/vcs.h>

#include "vcs_internal.h"
#include "aics_internal.h"
#include "vocs_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_VCS_CLIENT)
#define LOG_MODULE_NAME bt_vcs_client
#include "common/log.h"

#define FIRST_HANDLE				0x0001
#define LAST_HANDLE				0xFFFF

struct vcs_instance_t {
	struct vcs_state_t state;
	uint8_t flags;

	uint16_t start_handle;
	uint16_t end_handle;
	uint16_t state_handle;
	uint16_t control_handle;
	uint16_t flag_handle;
	struct bt_gatt_subscribe_params state_sub_params;
	struct bt_gatt_subscribe_params flag_sub_params;

	bool busy;
	uint8_t write_buf[sizeof(struct vcs_control_t)];
	struct bt_gatt_write_params write_params;
	struct bt_gatt_read_params read_params;

	uint8_t vocs_inst_cnt;
	struct vocs_instance_t vocs[CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST];
	uint8_t aics_inst_cnt;
	struct aics_instance_t aics[CONFIG_BT_VCS_CLIENT_MAX_AICS_INST];
};

/* Callback functions */
static struct bt_vcs_cb_t *vcs_client_cb;

static struct bt_gatt_discover_params discover_params;
static struct vcs_instance_t *cur_vcs_inst;
static struct vocs_instance_t *cur_vocs_inst;
static struct aics_instance_t *cur_aics_inst;

static struct vcs_instance_t vcs_inst;
static struct bt_uuid_16 uuid = BT_UUID_INIT_16(0);
static int vcs_client_common_vcs_cp(struct bt_conn *conn, uint8_t opcode);

static uint8_t vcs_notify_handler(struct bt_conn *conn,
				  struct bt_gatt_subscribe_params *params,
				  const void *data, uint16_t length)
{
	uint16_t handle = params->value_handle;

	if (data) {
		if (handle == vcs_inst.state_handle) {
			if (length == sizeof(vcs_inst.state)) {
				memcpy(&vcs_inst.state, data, length);
				BT_DBG("Volume %u, mute %u, counter %u",
				       vcs_inst.state.volume,
				       vcs_inst.state.mute,
				       vcs_inst.state.change_counter);
				if (vcs_client_cb && vcs_client_cb->state) {
					vcs_client_cb->state(
						conn, 0, vcs_inst.state.volume,
						vcs_inst.state.mute);
				}
			}
		} else if (handle == vcs_inst.flag_handle) {
			if (length == sizeof(vcs_inst.flags)) {
				memcpy(&vcs_inst.flags, data, length);
				BT_DBG("Flags %u", vcs_inst.flags);
				if (vcs_client_cb && vcs_client_cb->flags) {
					vcs_client_cb->flags(conn, 0,
							     vcs_inst.flags);
				}
			}
		}
	}
	return BT_GATT_ITER_CONTINUE;
}


static uint8_t vcs_client_read_volume_state_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = err;

	vcs_inst.busy = false;

	if (err) {
		BT_DBG("err: 0x%02X", err);
	} else if (data) {
		if (length == sizeof(vcs_inst.state)) {
			memcpy(&vcs_inst.state, data, length);
			BT_DBG("Volume %u, mute %u, counter %u",
			       vcs_inst.state.volume,
			       vcs_inst.state.mute,
			       vcs_inst.state.change_counter);
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(vcs_inst.state));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (vcs_client_cb && vcs_client_cb->state) {
		vcs_client_cb->state(conn, cb_err,
				  vcs_inst.state.volume,
				  vcs_inst.state.mute);
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t vcs_client_read_flag_cb(struct bt_conn *conn, uint8_t err,
				       struct bt_gatt_read_params *params,
				       const void *data, uint16_t length)
{
	uint8_t cb_err = err;

	vcs_inst.busy = false;

	if (err) {
		BT_DBG("err: 0x%02X", err);
	} else if (data) {
		if (length == sizeof(vcs_inst.flags)) {
			memcpy(&vcs_inst.flags, data, length);
			BT_DBG("Flags %u", vcs_inst.flags);
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(vcs_inst.flags));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (vcs_client_cb && vcs_client_cb->flags) {
		vcs_client_cb->flags(conn, cb_err, vcs_inst.flags);
	}

	return BT_GATT_ITER_STOP;
}

static void vcs_cp_notify_app(struct bt_conn *conn, uint8_t opcode, uint8_t err)
{
	if (!vcs_client_cb) {
		return;
	}

	switch (opcode) {
	case VCS_OPCODE_REL_VOL_DOWN:
		if (vcs_client_cb->vol_down) {
			vcs_client_cb->vol_down(conn, err);
		}
		break;
	case VCS_OPCODE_REL_VOL_UP:
		if (vcs_client_cb->vol_up) {
			vcs_client_cb->vol_up(conn, err);
		}
		break;
	case VCS_OPCODE_UNMUTE_REL_VOL_DOWN:
		if (vcs_client_cb->vol_down_unmute) {
			vcs_client_cb->vol_down_unmute(conn, err);
		}
		break;
	case VCS_OPCODE_UNMUTE_REL_VOL_UP:
		if (vcs_client_cb->vol_up_unmute) {
			vcs_client_cb->vol_up_unmute(conn, err);
		}
		break;
	case VCS_OPCODE_SET_ABS_VOL:
		if (vcs_client_cb->vol_set) {
			vcs_client_cb->vol_set(conn, err);
		}
		break;
	case VCS_OPCODE_UNMUTE:
		if (vcs_client_cb->unmute) {
			vcs_client_cb->unmute(conn, err);
		}
		break;
	case VCS_OPCODE_MUTE:
		if (vcs_client_cb->mute) {
			vcs_client_cb->mute(conn, err);
		}
		break;
	default:
		BT_DBG("Unknown opcode 0x%02x", opcode);
		break;
	}
}

static uint8_t internal_read_volume_state_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = 0;
	uint8_t opcode = vcs_inst.write_buf[0];
	struct vcs_control_t *cp = (struct vcs_control_t *)vcs_inst.write_buf;

	if (err) {
		BT_WARN("Volume state read failed: %d", err);
		cb_err = BT_ATT_ERR_UNLIKELY;
	} else if (data) {
		if (length == sizeof(vcs_inst.state)) {
			int write_err;

			memcpy(&vcs_inst.state, data, length);
			BT_DBG("Volume %u, mute %u, counter %u",
			       vcs_inst.state.volume,
			       vcs_inst.state.mute,
			       vcs_inst.state.change_counter);

			/* clear busy flag to reuse function */
			vcs_inst.busy = false;
			if (cp->opcode == VCS_OPCODE_SET_ABS_VOL) {
				write_err = bt_vcs_client_set_volume(
						conn, cp->volume);
			} else {
				write_err = vcs_client_common_vcs_cp(conn,
								     opcode);
			}
			if (write_err) {
				cb_err = BT_ATT_ERR_UNLIKELY;
			}
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(vcs_inst.state));
			cb_err = BT_ATT_ERR_UNLIKELY;
		}
	}

	if (cb_err) {
		vcs_inst.busy = false;
		vcs_cp_notify_app(conn, opcode, BT_ATT_ERR_UNLIKELY);
	}

	return BT_GATT_ITER_STOP;
}

static void vcs_client_write_vcs_cp_cb(struct bt_conn *conn, uint8_t err,
				       struct bt_gatt_write_params *params)
{
	uint8_t opcode = vcs_inst.write_buf[0];

	BT_DBG("err: 0x%02X", err);

	if (err == VCS_ERR_INVALID_COUNTER && vcs_inst.state_handle) {
		int read_err;

		vcs_inst.read_params.func = internal_read_volume_state_cb;
		vcs_inst.read_params.handle_count = 1;
		vcs_inst.read_params.single.handle = vcs_inst.state_handle;
		vcs_inst.read_params.single.offset = 0U;

		read_err = bt_gatt_read(conn, &vcs_inst.read_params);

		if (read_err) {
			BT_WARN("Could not read Volume state: %d", read_err);
		} else {
			return;
		}
	}

	vcs_inst.busy = false;

	vcs_cp_notify_app(conn, opcode, err);
}

#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
static struct vocs_instance_t *lookup_vocs_by_handle(uint16_t handle)
{
	for (int i = 0; i < vcs_inst.vocs_inst_cnt; i++) {
		if (vcs_inst.vocs[i].start_handle <= handle &&
		    vcs_inst.vocs[i].end_handle >= handle) {
			return &vcs_inst.vocs[i];
		}
	}
	BT_DBG("Could not find VOCS instance with handle 0x%04x", handle);
	return NULL;
}

static uint8_t vocs_notify_handler(struct bt_conn *conn,
				   struct bt_gatt_subscribe_params *params,
				   const void *data, uint16_t length)
{
	uint16_t handle = params->value_handle;
	struct vocs_instance_t *vocs_inst = lookup_vocs_by_handle(handle);
	char desc[MIN(CONFIG_BT_L2CAP_RX_MTU, BT_ATT_MAX_ATTRIBUTE_LEN) + 1];

	if (!vocs_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	if (data) {
		if (handle == vocs_inst->state_handle) {
			if (length == sizeof(vcs_inst.state)) {
				memcpy(&vocs_inst->state, data, length);
				BT_DBG("Index %u: Offset %d, counter %u",
				       vocs_inst->index,
				       vocs_inst->state.offset,
				       vocs_inst->state.change_counter);
				if (vcs_client_cb &&
				    vcs_client_cb->vocs_cb.state) {
					vcs_client_cb->vocs_cb.state(
						conn, vocs_inst->index, 0,
						vocs_inst->state.offset);
				}
			}
		} else if (handle == vocs_inst->desc_handle) {
			if (length > BT_ATT_MAX_ATTRIBUTE_LEN) {
				BT_DBG("Length (%u) too large", length);
				return BT_GATT_ITER_CONTINUE;
			}

			memcpy(desc, data, length);
			desc[length] = '\0';
			BT_DBG("Index %u: Output description: %s",
			       vocs_inst->index, log_strdup(desc));
			if (vcs_client_cb &&
			    vcs_client_cb->vocs_cb.description) {
				vcs_client_cb->vocs_cb.description(
					conn, vocs_inst->index, 0, desc);
			}
		} else if (handle == vocs_inst->location_handle) {
			if (length == sizeof(vocs_inst->location)) {
				memcpy(&vocs_inst->location, data, length);
				BT_DBG("Index %u: Location %u",
				       vocs_inst->index, vocs_inst->location);
				if (vcs_client_cb &&
				    vcs_client_cb->vocs_cb.location) {
					vcs_client_cb->vocs_cb.location(
						conn, vocs_inst->index, 0,
						vocs_inst->location);
				}
			}
		}
	}
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t vcs_client_vocs_read_offset_state_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	struct vocs_instance_t *vocs_inst =
		lookup_vocs_by_handle(params->single.handle);

	if (!vocs_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("Index %u: err: 0x%02X", vocs_inst->index, err);
	vocs_inst->busy = false;

	if (data) {
		if (length == sizeof(vocs_inst->state)) {
			memcpy(&vocs_inst->state, data, length);
			BT_DBG("Offset %d, counter %u",
			       vocs_inst->state.offset,
			       vocs_inst->state.change_counter);
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(vocs_inst->state));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (vcs_client_cb && vcs_client_cb->vocs_cb.state) {
		vcs_client_cb->vocs_cb.state(conn, vocs_inst->index, cb_err,
					     vocs_inst->state.offset);
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t vcs_client_vocs_read_location_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	struct vocs_instance_t *vocs_inst =
		lookup_vocs_by_handle(params->single.handle);

	if (!vocs_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("Index %u: err: 0x%02X", vocs_inst->index, err);
	vocs_inst->busy = false;

	if (data) {
		if (length == sizeof(vocs_inst->location)) {
			memcpy(&vocs_inst->location, data, length);
			BT_DBG("Location %u", vocs_inst->location);
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(vocs_inst->location));
			cb_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		}
	}

	if (vcs_client_cb && vcs_client_cb->vocs_cb.location) {
		vcs_client_cb->vocs_cb.location(conn, vocs_inst->index, cb_err,
						vocs_inst->location);
	}

	return BT_GATT_ITER_STOP;
}

static void vocs_cp_notify_app(struct bt_conn *conn, uint8_t index, uint8_t err)
{
	if (vcs_client_cb && vcs_client_cb->vocs_cb.set_offset) {
		vcs_client_cb->vocs_cb.set_offset(conn, index, err);
	}
}

static uint8_t internal_read_volume_offset_state_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = 0;
	struct vocs_control_t *cp = (struct vocs_control_t *)vcs_inst.write_buf;
	struct vocs_instance_t *vocs_inst =
		lookup_vocs_by_handle(params->single.handle);

	if (!vocs_inst) {
		BT_ERR("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	if (err) {
		BT_WARN("Volume state read failed: %d", err);
		cb_err = BT_ATT_ERR_UNLIKELY;
	} else if (data) {
		if (length == sizeof(vocs_inst->state)) {
			int write_err;

			memcpy(&vocs_inst->state, data, length);
			BT_DBG("Offset %d, counter %u",
			       vocs_inst->state.offset,
			       vocs_inst->state.change_counter);

			/* clear busy flag to reuse function */
			vocs_inst->busy = false;
			write_err = bt_vcs_client_vocs_set_offset(
					conn, vocs_inst->index, cp->offset);
			if (write_err) {
				cb_err = BT_ATT_ERR_UNLIKELY;
			}
		} else {
			BT_DBG("Invalid length %u (expected %zu)",
			       length, sizeof(vocs_inst->state));
			cb_err = BT_ATT_ERR_UNLIKELY;
		}
	}

	if (cb_err) {
		vocs_inst->busy = false;
		vocs_cp_notify_app(conn, vocs_inst->index, BT_ATT_ERR_UNLIKELY);
	}

	return BT_GATT_ITER_STOP;
}

static void vcs_client_write_vocs_cp_cb(struct bt_conn *conn, uint8_t err,
					struct bt_gatt_write_params *params)
{
	struct vocs_instance_t *vocs_inst =
		lookup_vocs_by_handle(params->handle);

	if (!vocs_inst) {
		BT_DBG("Instance not found");
		return;
	}

	BT_DBG("Index %u: err: 0x%02X", vocs_inst->index, err);

	if (err == VOCS_ERR_INVALID_COUNTER && vocs_inst->state_handle) {
		int read_err;

		vocs_inst->read_params.func =
			internal_read_volume_offset_state_cb;
		vocs_inst->read_params.handle_count = 1;
		vocs_inst->read_params.single.handle = vocs_inst->state_handle;
		vocs_inst->read_params.single.offset = 0U;

		read_err = bt_gatt_read(conn, &vocs_inst->read_params);

		if (read_err) {
			BT_WARN("Could not read Volume state: %d", read_err);
		} else {
			return;
		}
	}

	vocs_inst->busy = false;

	if (vcs_client_cb && vcs_client_cb->vocs_cb.set_offset) {
		vcs_client_cb->vocs_cb.set_offset(conn, vocs_inst->index, err);
	}
}

static uint8_t vcs_client_read_output_desc_cb(
	struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
	const void *data, uint16_t length)
{
	uint8_t cb_err = err;
	struct vocs_instance_t *vocs_inst =
		lookup_vocs_by_handle(params->single.handle);
	char desc[MIN(CONFIG_BT_L2CAP_RX_MTU, BT_ATT_MAX_ATTRIBUTE_LEN) + 1];

	if (!vocs_inst) {
		BT_DBG("Instance not found");
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("Index %u: err: 0x%02X", vocs_inst->index, err);
	vocs_inst->busy = false;

	if (data) {
		BT_HEXDUMP_DBG(data, length, "Output description read");

		if (length > BT_ATT_MAX_ATTRIBUTE_LEN) {
			BT_DBG("Length (%u) too large", length);
			return BT_GATT_ITER_CONTINUE;
		}

		/* TODO: Handle long reads */
		memcpy(desc, data, length);
		desc[length] = '\0';
		BT_DBG("Output description: %s", log_strdup(desc));
	}

	if (vcs_client_cb && vcs_client_cb->vocs_cb.description) {
		vcs_client_cb->vocs_cb.description(conn, vocs_inst->index,
						   cb_err, desc);
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t vocs_discover_func(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       struct bt_gatt_discover_params *params)
{
	int err;
	struct bt_gatt_chrc *chrc;
	uint8_t next_idx;
	uint8_t aics_cnt;
	uint8_t vocs_cnt;
	struct bt_gatt_subscribe_params *sub_params = NULL;

	if (!attr) {
		aics_cnt = vcs_inst.aics_inst_cnt;
		vocs_cnt = vcs_inst.vocs_inst_cnt;
		next_idx = cur_vocs_inst->index + 1;
		BT_DBG("Setup complete for VOCS %u / %u",
		       next_idx, vcs_inst.vocs_inst_cnt);
		(void)memset(params, 0, sizeof(*params));

		if (next_idx < vcs_inst.vocs_inst_cnt) {
			/* Discover characteristics */
			cur_vocs_inst = &vcs_inst.vocs[next_idx];
			discover_params.start_handle =
				cur_vocs_inst->start_handle;
			discover_params.end_handle = cur_vocs_inst->end_handle;
			discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
			discover_params.func = vocs_discover_func;

			err = bt_gatt_discover(conn, &discover_params);
			if (err) {
				BT_DBG("Discover failed (err %d)", err);
				cur_vcs_inst = NULL;
				cur_aics_inst = NULL;
				cur_vocs_inst = NULL;
				if (vcs_client_cb && vcs_client_cb->discover) {
					vcs_client_cb->discover(conn, err,
								aics_cnt,
								vocs_cnt);
				}
			}
		} else {
			cur_vcs_inst = NULL;
			cur_aics_inst = NULL;
			cur_vocs_inst = NULL;
			if (vcs_client_cb && vcs_client_cb->discover) {
				vcs_client_cb->discover(conn, 0, aics_cnt,
							vocs_cnt);
			}
		}
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("[ATTRIBUTE] handle 0x%04X", attr->handle);

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		chrc = (struct bt_gatt_chrc *)attr->user_data;
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_VOCS_STATE)) {
			BT_DBG("Volume offset state");
			cur_vocs_inst->state_handle = chrc->value_handle;
			sub_params = &cur_vocs_inst->state_sub_params;
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_VOCS_LOCATION)) {
			BT_DBG("Location");
			cur_vocs_inst->location_handle = chrc->value_handle;
			if (chrc->properties & BT_GATT_CHRC_NOTIFY) {
				sub_params =
					&cur_vocs_inst->location_sub_params;
			}
			if (chrc->properties &
				BT_GATT_CHRC_WRITE_WITHOUT_RESP) {
				cur_vocs_inst->location_writable = true;
			}
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_VOCS_CONTROL)) {
			BT_DBG("Control point");
			cur_vocs_inst->control_handle = chrc->value_handle;
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_VOCS_DESCRIPTION)) {
			BT_DBG("Description");
			cur_vocs_inst->desc_handle = chrc->value_handle;
			if (chrc->properties & BT_GATT_CHRC_NOTIFY) {
				sub_params = &cur_vocs_inst->desc_sub_params;
			}
			if (chrc->properties &
				BT_GATT_CHRC_WRITE_WITHOUT_RESP) {
				cur_vocs_inst->desc_writable = true;
			}
		}

		if (sub_params) {
			sub_params->value = BT_GATT_CCC_NOTIFY;
			sub_params->value_handle = chrc->value_handle;
			/*
			 * TODO: Don't assume that CCC is at handle + 2;
			 * do proper discovery;
			 */
			sub_params->ccc_handle = attr->handle + 2;
			sub_params->notify = vocs_notify_handler;
			bt_gatt_subscribe(conn, sub_params);
		}
	}

	return BT_GATT_ITER_CONTINUE;
}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */

#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0

static uint8_t aics_discover_func(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       struct bt_gatt_discover_params *params)
{
	int err;
	struct bt_gatt_chrc *chrc;
	uint8_t next_idx;
	uint8_t aics_cnt;
	uint8_t vocs_cnt;
	struct bt_gatt_subscribe_params *sub_params = NULL;

	if (!attr) {
		cur_aics_inst->cb = &vcs_client_cb->aics_cb;
		bt_aics_client_register(
			cur_aics_inst,
			AICS_CLI_VCS_CLIENT_INDEX(cur_aics_inst->index));
		aics_cnt = vcs_inst.aics_inst_cnt;
		vocs_cnt = vcs_inst.vocs_inst_cnt;
		next_idx = cur_aics_inst->index + 1;
		BT_DBG("Setup complete for AICS %u / %u",
		       next_idx, vcs_inst.aics_inst_cnt);
		(void)memset(params, 0, sizeof(*params));

		if (next_idx < vcs_inst.aics_inst_cnt) {
			/* Discover characteristics */
			cur_aics_inst = &vcs_inst.aics[next_idx];
			discover_params.start_handle =
				cur_aics_inst->start_handle;
			discover_params.end_handle = cur_aics_inst->end_handle;
			discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
			discover_params.func = aics_discover_func;

			err = bt_gatt_discover(conn, &discover_params);
			if (err) {
				BT_DBG("Discover failed (err %d)", err);
				cur_vcs_inst = NULL;
				if (vcs_client_cb && vcs_client_cb->discover) {
					vcs_client_cb->discover(conn, err,
								aics_cnt,
								vocs_cnt);
				}
			}
		}
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
		else if (vcs_inst.vocs_inst_cnt) {
			/* Discover characteristics */
			cur_vocs_inst = &vcs_inst.vocs[0];
			discover_params.start_handle =
				cur_vocs_inst->start_handle;
			discover_params.end_handle = cur_vocs_inst->end_handle;
			discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
			discover_params.func = vocs_discover_func;

			err = bt_gatt_discover(conn, &discover_params);
			if (err) {
				BT_DBG("Discover failed (err %d)", err);
				cur_vcs_inst = NULL;
				if (vcs_client_cb && vcs_client_cb->discover) {
					vcs_client_cb->discover(conn, err,
								aics_cnt,
								vocs_cnt);
				}
			}

		}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */
		else {
			cur_vcs_inst = NULL;
			if (vcs_client_cb && vcs_client_cb->discover) {
				vcs_client_cb->discover(conn, 0, aics_cnt,
							vocs_cnt);
			}
		}
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("[ATTRIBUTE] handle 0x%04X", attr->handle);

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		chrc = (struct bt_gatt_chrc *)attr->user_data;
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_AICS_STATE)) {
			BT_DBG("Audio Input state");
			cur_aics_inst->state_handle = chrc->value_handle;
			sub_params = &cur_aics_inst->state_sub_params;
		} else if (!bt_uuid_cmp(chrc->uuid,
					BT_UUID_AICS_GAIN_SETTINGS)) {
			BT_DBG("Gain settings");
			cur_aics_inst->gain_handle = chrc->value_handle;
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_AICS_INPUT_TYPE)) {
			BT_DBG("Input type");
			cur_aics_inst->type_handle = chrc->value_handle;
		} else if (!bt_uuid_cmp(chrc->uuid,
					BT_UUID_AICS_INPUT_STATUS)) {
			BT_DBG("Input status");
			cur_aics_inst->status_handle = chrc->value_handle;
			sub_params = &cur_aics_inst->status_sub_params;
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_AICS_CONTROL)) {
			BT_DBG("Control point");
			cur_aics_inst->control_handle = chrc->value_handle;
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_AICS_DESCRIPTION)) {
			BT_DBG("Description");
			cur_aics_inst->desc_handle = chrc->value_handle;
			if (chrc->properties & BT_GATT_CHRC_NOTIFY) {
				sub_params = &cur_aics_inst->desc_sub_params;
			}

			if (chrc->properties &
				BT_GATT_CHRC_WRITE_WITHOUT_RESP) {
				cur_aics_inst->desc_writable = true;
			}
		}

		if (sub_params) {
			sub_params->value = BT_GATT_CCC_NOTIFY;
			sub_params->value_handle = chrc->value_handle;
			/*
			 * TODO: Don't assume that CCC is at handle + 2;
			 * do proper discovery;
			 */
			sub_params->ccc_handle = attr->handle + 2;
			sub_params->notify = aics_client_notify_handler;
			bt_gatt_subscribe(conn, sub_params);
		}
	}

	return BT_GATT_ITER_CONTINUE;
}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0 */

#if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0 || \
	CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0)
static uint8_t vcs_discover_include_func(struct bt_conn *conn,
					 const struct bt_gatt_attr *attr,
					 struct bt_gatt_discover_params *params)
{
	struct bt_gatt_include *include;
	uint8_t inst_idx;
	int err;

	if (!attr) {
		BT_DBG("Discover include complete for VCS: %u AICS and %u VOCS",
		       vcs_inst.aics_inst_cnt,
		       vcs_inst.vocs_inst_cnt);
		(void)memset(params, 0, sizeof(*params));
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
		if (vcs_inst.aics_inst_cnt) {
			/* Discover characteristics */
			cur_aics_inst = &vcs_inst.aics[0];
			discover_params.start_handle =
				cur_aics_inst->start_handle;
			discover_params.end_handle = cur_aics_inst->end_handle;
			discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
			discover_params.func = aics_discover_func;

			err = bt_gatt_discover(conn, &discover_params);
			if (err) {
				BT_DBG("Discover failed (err %d)", err);
				cur_vcs_inst = NULL;
				cur_aics_inst = NULL;
				if (vcs_client_cb && vcs_client_cb->discover) {
					vcs_client_cb->discover(conn, err, 0,
								0);
				}
			}
		} else
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
		if (vcs_inst.vocs_inst_cnt) {
			/* Discover characteristics */
			cur_vocs_inst = &vcs_inst.vocs[0];
			discover_params.start_handle =
				cur_vocs_inst->start_handle;
			discover_params.end_handle = cur_vocs_inst->end_handle;
			discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
			discover_params.func = vocs_discover_func;

			err = bt_gatt_discover(conn, &discover_params);
			if (err) {
				BT_DBG("Discover failed (err %d)", err);
				cur_vcs_inst = NULL;
				cur_aics_inst = NULL;
				cur_vocs_inst = NULL;
				if (vcs_client_cb && vcs_client_cb->discover) {
					vcs_client_cb->discover(conn, err, 0,
								0);
				}
			}
		} else
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */
		{
			cur_vcs_inst = NULL;
			if (vcs_client_cb && vcs_client_cb->discover) {
				vcs_client_cb->discover(conn, 0, 0, 0);
			}
		}
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("[ATTRIBUTE] handle 0x%04X", attr->handle);

	if (params->type == BT_GATT_DISCOVER_INCLUDE) {
		include = (struct bt_gatt_include *)attr->user_data;
		BT_DBG("Include UUID %s", bt_uuid_str(include->uuid));
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
		if (!bt_uuid_cmp(include->uuid, BT_UUID_AICS) &&
		    vcs_inst.aics_inst_cnt <
			CONFIG_BT_VCS_CLIENT_MAX_AICS_INST) {
			inst_idx = vcs_inst.aics_inst_cnt;
			vcs_inst.aics[inst_idx].start_handle =
				include->start_handle;
			vcs_inst.aics[inst_idx].end_handle =
				include->end_handle;
			vcs_inst.aics[inst_idx].index = inst_idx;
			vcs_inst.aics_inst_cnt++;
		}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
		if (!bt_uuid_cmp(include->uuid, BT_UUID_VOCS) &&
		    vcs_inst.vocs_inst_cnt <
			CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
			inst_idx = vcs_inst.vocs_inst_cnt;
			vcs_inst.vocs[inst_idx].start_handle =
				include->start_handle;
			vcs_inst.vocs[inst_idx].end_handle =
				include->end_handle;
			vcs_inst.vocs[inst_idx].index = inst_idx;
			vcs_inst.vocs_inst_cnt++;
		}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */
	}

	return BT_GATT_ITER_CONTINUE;
}
#endif /* (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0 ||
	*  CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0)
	*/

/**
 * @brief This will discover all characteristics on the server, retrieving the
 * handles of the writeable characteristics and subscribing to all notify and
 * indicate characteristics.
 */
static uint8_t vcs_discover_func(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 struct bt_gatt_discover_params *params)
{
	int err = 0;
	struct bt_gatt_chrc *chrc;
	struct bt_gatt_subscribe_params *sub_params = NULL;

	if (!attr) {
		BT_DBG("Setup complete for VCS");
		(void)memset(params, 0, sizeof(*params));
#if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0 || \
	CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0)
		/* Discover included services */
		discover_params.start_handle = vcs_inst.start_handle;
		discover_params.end_handle = vcs_inst.end_handle;
		discover_params.type = BT_GATT_DISCOVER_INCLUDE;
		discover_params.func = vcs_discover_include_func;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			BT_DBG("Discover failed (err %d)", err);
			cur_vcs_inst = NULL;
			if (vcs_client_cb && vcs_client_cb->discover) {
				vcs_client_cb->discover(conn, err, 0, 0);
			}
		}
#else
		if (vcs_client_cb && vcs_client_cb->discover) {
			vcs_client_cb->discover(conn, err, 0, 0);
		}
#endif /* (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0 ||
	* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0)
	*/
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("[ATTRIBUTE] handle 0x%04X", attr->handle);

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		chrc = (struct bt_gatt_chrc *)attr->user_data;
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_VCS_STATE)) {
			BT_DBG("Volume state");
			vcs_inst.state_handle = chrc->value_handle;
			sub_params = &vcs_inst.state_sub_params;
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_VCS_CONTROL)) {
			BT_DBG("Control Point");
			vcs_inst.control_handle = chrc->value_handle;
		} else if (!bt_uuid_cmp(chrc->uuid, BT_UUID_VCS_FLAGS)) {
			BT_DBG("Flags");
			vcs_inst.flag_handle = chrc->value_handle;
			sub_params = &vcs_inst.flag_sub_params;
		}

		if (sub_params) {
			sub_params->value = BT_GATT_CCC_NOTIFY;
			sub_params->value_handle = chrc->value_handle;
			/*
			 * TODO: Don't assume that CCC is at handle + 2;
			 * do proper discovery;
			 */
			sub_params->ccc_handle = attr->handle + 2;
			sub_params->notify = vcs_notify_handler;
			bt_gatt_subscribe(conn, sub_params);
		}
	}

	return BT_GATT_ITER_CONTINUE;
}


/**
 * @brief This will discover all characteristics on the server, retrieving the
 * handles of the writeable characteristics and subscribing to all notify and
 * indicate characteristics.
 */
static uint8_t primary_discover_func(struct bt_conn *conn,
				     const struct bt_gatt_attr *attr,
				     struct bt_gatt_discover_params *params)
{
	int err;
	struct bt_gatt_service_val *prim_service;

	if (!attr) {
		BT_DBG("Could not find a VCS instance on the server");
		cur_vcs_inst = NULL;
		if (vcs_client_cb && vcs_client_cb->discover) {
			vcs_client_cb->discover(conn, -ENODATA, 0, 0);
		}
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("[ATTRIBUTE] handle 0x%04X", attr->handle);

	if (params->type == BT_GATT_DISCOVER_PRIMARY) {
		BT_DBG("Primary discover complete");
		prim_service = (struct bt_gatt_service_val *)attr->user_data;
		discover_params.start_handle = attr->handle + 1;

		cur_vcs_inst = &vcs_inst;
		vcs_inst.start_handle = attr->handle + 1;
		vcs_inst.end_handle = prim_service->end_handle;

		/* Discover characteristics */
		discover_params.uuid = NULL;
		discover_params.start_handle = vcs_inst.start_handle;
		discover_params.end_handle = vcs_inst.end_handle;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		discover_params.func = vcs_discover_func;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			BT_DBG("Discover failed (err %d)", err);
			cur_vcs_inst = NULL;
			if (vcs_client_cb && vcs_client_cb->discover) {
				vcs_client_cb->discover(conn, err, 0, 0);
			}
		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_CONTINUE;
}

static int vcs_client_common_vcs_cp(struct bt_conn *conn, uint8_t opcode)
{
	int err;

	if (!conn) {
		return -ENOTCONN;
	}

	if (!vcs_inst.control_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vcs_inst.busy) {
		return -EBUSY;
	}

	vcs_inst.busy = true;
	vcs_inst.write_buf[0] = opcode;
	vcs_inst.write_buf[1] = vcs_inst.state.change_counter;
	vcs_inst.write_params.offset = 0;
	vcs_inst.write_params.data = vcs_inst.write_buf;
	vcs_inst.write_params.length =
		sizeof(opcode) + sizeof(vcs_inst.state.change_counter);
	vcs_inst.write_params.handle = vcs_inst.control_handle;
	vcs_inst.write_params.func = vcs_client_write_vcs_cp_cb;

	err = bt_gatt_write(conn, &vcs_inst.write_params);
	if (!err) {
		vcs_inst.busy = true;
	}
	return err;
}

int bt_vcs_discover(struct bt_conn *conn)
{
	/* TODO: Read states in init */

	/*
	 * This will initiate a discover procedure. The procedure will do the
	 * following sequence:
	 * 1) Primary discover for the VCS
	 * 2) Characteristic discover of the VCS
	 * 3) Discover services included in VCS (VOCS and AICS)
	 * 4) For each included service found; discovery of the characteristics
	 * 5) When everything above have been discovered, the callback is called
	 */

	if (!conn) {
		return -ENOTCONN;
	} else if (cur_vcs_inst) {
		return -EBUSY;
	}

	cur_aics_inst = NULL;
	cur_vocs_inst = NULL;
	memset(&discover_params, 0, sizeof(discover_params));
	memset(&vcs_inst, 0, sizeof(vcs_inst));
	memcpy(&uuid, BT_UUID_VCS, sizeof(uuid));
	for (int i = 0; i < ARRAY_SIZE(vcs_inst.aics); i++) {
		bt_aics_client_unregister(AICS_CLI_VCS_CLIENT_INDEX(i));
	}
	discover_params.func = primary_discover_func;
	discover_params.uuid = &uuid.uuid;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;
	discover_params.start_handle = FIRST_HANDLE;
	discover_params.end_handle = LAST_HANDLE;
	return bt_gatt_discover(conn, &discover_params);
}

void bt_vcs_client_cb_register(struct bt_vcs_cb_t *cb)
{
	vcs_client_cb = cb;
}

int bt_vcs_client_read_volume_state(struct bt_conn *conn)
{
	int err;

	if (!conn) {
		return -ENOTCONN;
	}

	if (!vcs_inst.state_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vcs_inst.busy) {
		return -EBUSY;
	}

	vcs_inst.read_params.func = vcs_client_read_volume_state_cb;
	vcs_inst.read_params.handle_count = 1;
	vcs_inst.read_params.single.handle = vcs_inst.state_handle;
	vcs_inst.read_params.single.offset = 0U;

	err = bt_gatt_read(conn, &vcs_inst.read_params);
	if (!err) {
		vcs_inst.busy = true;
	}
	return err;
}

int bt_vcs_client_read_flags(struct bt_conn *conn)
{
	int err;

	if (!conn) {
		return -ENOTCONN;
	}

	if (!vcs_inst.flag_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vcs_inst.busy) {
		return -EBUSY;
	}

	vcs_inst.read_params.func = vcs_client_read_flag_cb;
	vcs_inst.read_params.handle_count = 1;
	vcs_inst.read_params.single.handle = vcs_inst.flag_handle;
	vcs_inst.read_params.single.offset = 0U;

	err = bt_gatt_read(conn, &vcs_inst.read_params);
	if (!err) {
		vcs_inst.busy = true;
	}
	return err;
}

int bt_vcs_client_volume_down(struct bt_conn *conn)
{
	return vcs_client_common_vcs_cp(conn, VCS_OPCODE_REL_VOL_DOWN);
}

int bt_vcs_client_volume_up(struct bt_conn *conn)
{
	return vcs_client_common_vcs_cp(conn, VCS_OPCODE_REL_VOL_UP);
}

int bt_vcs_client_unmute_volume_down(struct bt_conn *conn)
{
	return vcs_client_common_vcs_cp(conn, VCS_OPCODE_UNMUTE_REL_VOL_DOWN);
}

int bt_vcs_client_unmute_volume_up(struct bt_conn *conn)
{
	return vcs_client_common_vcs_cp(conn, VCS_OPCODE_UNMUTE_REL_VOL_UP);
}

int bt_vcs_client_set_volume(struct bt_conn *conn, uint8_t volume)
{
	int err;
	struct vcs_control_t cp = {
		.opcode = VCS_OPCODE_SET_ABS_VOL,
		.counter = vcs_inst.state.change_counter,
		.volume = volume
	};

	if (!conn) {
		return -ENOTCONN;
	}

	if (!vcs_inst.control_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vcs_inst.busy) {
		return -EBUSY;
	}

	memcpy(vcs_inst.write_buf, &cp, sizeof(cp));
	vcs_inst.busy = true;
	vcs_inst.write_params.offset = 0;
	vcs_inst.write_params.data = vcs_inst.write_buf;
	vcs_inst.write_params.length = sizeof(cp);
	vcs_inst.write_params.handle = vcs_inst.control_handle;
	vcs_inst.write_params.func = vcs_client_write_vcs_cp_cb;

	err = bt_gatt_write(conn, &vcs_inst.write_params);
	if (!err) {
		vcs_inst.busy = true;
	}
	return err;
}

int bt_vcs_client_unmute(struct bt_conn *conn)
{
	return vcs_client_common_vcs_cp(conn, VCS_OPCODE_UNMUTE);
}

int bt_vcs_client_mute(struct bt_conn *conn)
{
	return vcs_client_common_vcs_cp(conn, VCS_OPCODE_MUTE);
}

int bt_vcs_client_vocs_read_offset_state(struct bt_conn *conn,
					 uint8_t vocs_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	int err;
	struct vocs_instance_t *vocs_inst;

	if (!conn) {
		return -ENOTCONN;
	} else if (vocs_index >= CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		return -EINVAL;
	}

	vocs_inst = &vcs_inst.vocs[vocs_index];

	if (!vocs_inst->state_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vocs_inst->busy) {
		return -EBUSY;
	}

	vocs_inst->read_params.func = vcs_client_vocs_read_offset_state_cb;
	vocs_inst->read_params.handle_count = 1;
	vocs_inst->read_params.single.handle = vocs_inst->state_handle;
	vocs_inst->read_params.single.offset = 0U;

	err = bt_gatt_read(conn, &vocs_inst->read_params);
	if (!err) {
		vocs_inst->busy = true;
	}
	return err;
#else
	BT_DBG("Not supported");
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0 */
}

int bt_vcs_client_vocs_set_location(struct bt_conn *conn, uint8_t vocs_index,
				    uint8_t location)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	int err;
	struct vocs_instance_t *vocs_inst;

	if (!conn) {
		return -ENOTCONN;
	} else if (vocs_index >= CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		return -EINVAL;
	}

	vocs_inst = &vcs_inst.vocs[vocs_index];

	if (!vocs_inst->location_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vocs_inst->busy) {
		return -EBUSY;
	} else if (!vocs_inst->location_writable) {
		BT_DBG("Location is not writable on peer service instance");
		return -EPERM;
	}

	memcpy(vocs_inst->write_buf, &location, sizeof(location));

	err = bt_gatt_write_without_response(conn, vocs_inst->location_handle,
					     vocs_inst->write_buf,
					     sizeof(location), false);

	return err;
#else
	BT_DBG("Not supported");
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0 */
}

int bt_vcs_client_vocs_read_location(struct bt_conn *conn, uint8_t vocs_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	int err;
	struct vocs_instance_t *vocs_inst;

	if (!conn) {
		return -ENOTCONN;
	} else if (vocs_index >= CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		return -EINVAL;
	}

	vocs_inst = &vcs_inst.vocs[vocs_index];

	if (!vocs_inst->location_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vocs_inst->busy) {
		return -EBUSY;
	}

	vocs_inst->read_params.func = vcs_client_vocs_read_location_cb;
	vocs_inst->read_params.handle_count = 1;
	vocs_inst->read_params.single.handle = vocs_inst->location_handle;
	vocs_inst->read_params.single.offset = 0U;

	err = bt_gatt_read(conn, &vocs_inst->read_params);
	if (!err) {
		vocs_inst->busy = true;
	}
	return err;
#else
	BT_DBG("Not supported");
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0 */
}

int bt_vcs_client_vocs_set_offset(struct bt_conn *conn, uint8_t vocs_index,
				  int16_t offset)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	int err;
	struct vocs_instance_t *vocs_inst;
	struct vocs_control_t cp = {
		.opcode = VOCS_OPCODE_SET_OFFSET,
		.offset = offset
	};

	if (!conn) {
		return -ENOTCONN;
	} else if (vocs_index >= CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		BT_WARN("invalid vocs index %u", vocs_index);
		return -EINVAL;
	}

	vocs_inst = &vcs_inst.vocs[vocs_index];

	if (!vocs_inst->control_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vocs_inst->busy) {
		return -EBUSY;
	}

	cp.counter = vocs_inst->state.change_counter;

	memcpy(vocs_inst->write_buf, &cp, sizeof(cp));
	vocs_inst->write_params.offset = 0;
	vocs_inst->write_params.data = vocs_inst->write_buf;
	vocs_inst->write_params.length = sizeof(cp);
	vocs_inst->write_params.handle = vocs_inst->control_handle;
	vocs_inst->write_params.func = vcs_client_write_vocs_cp_cb;

	err = bt_gatt_write(conn, &vocs_inst->write_params);
	if (!err) {
		vocs_inst->busy = true;
	}
	return err;
#else
	BT_DBG("Not supported");
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0 */
}

int bt_vcs_client_vocs_read_output_description(struct bt_conn *conn,
					       uint8_t vocs_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	int err;
	struct vocs_instance_t *vocs_inst;

	if (!conn) {
		return -ENOTCONN;
	} else if (vocs_index >= CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		return -EINVAL;
	}

	vocs_inst = &vcs_inst.vocs[vocs_index];

	if (!vocs_inst->desc_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vocs_inst->busy) {
		return -EBUSY;
	}

	vocs_inst->read_params.func = vcs_client_read_output_desc_cb;
	vocs_inst->read_params.handle_count = 1;
	vocs_inst->read_params.single.handle = vocs_inst->desc_handle;
	vocs_inst->read_params.single.offset = 0U;

	err = bt_gatt_read(conn, &vocs_inst->read_params);
	if (!err) {
		vocs_inst->busy = true;
	}
	return err;
#else
	BT_DBG("Not supported");
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0 */
}

int bt_vcs_client_vocs_set_output_description(struct bt_conn *conn,
					      uint8_t vocs_index,
					      const char *description)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	int err;
	struct vocs_instance_t *vocs_inst;

	if (!conn) {
		return -ENOTCONN;
	} else if (vocs_index >= CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST) {
		return -EINVAL;
	}

	vocs_inst = &vcs_inst.vocs[vocs_index];

	if (!vocs_inst->desc_handle) {
		BT_DBG("Handle not set");
		return -EINVAL;
	} else if (vocs_inst->busy) {
		return -EBUSY;
	} else if (!vocs_inst->desc_writable) {
		BT_DBG("Description is not writable on peer service instance");
		return -EPERM;
	}

	err = bt_gatt_write_without_response(conn, vocs_inst->desc_handle,
					     description, strlen(description),
					     false);

	return err;
#else
	BT_DBG("Not supported");
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0 */
}

int bt_vcs_client_aics_read_input_state(struct bt_conn *conn,
					uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_input_state_get(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_read_gain_setting(struct bt_conn *conn,
					 uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_gain_setting_get(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_read_input_type(struct bt_conn *conn, uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_input_type_get(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_read_input_status(struct bt_conn *conn,
					 uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_input_status_get(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_input_unmute(struct bt_conn *conn, uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_input_unmute(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_input_mute(struct bt_conn *conn, uint8_t aics_index)
{

	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_input_mute(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_set_manual_input_gain(struct bt_conn *conn,
					     uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_manual_input_gain_set(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_set_automatic_input_gain(struct bt_conn *conn,
						uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_automatic_input_gain_set(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_set_gain(struct bt_conn *conn, uint8_t aics_index,
				int8_t gain)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_gain_set(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index), gain);
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_read_input_description(struct bt_conn *conn,
					      uint8_t aics_index)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_input_description_get(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index));
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}

int bt_vcs_client_aics_set_input_description(struct bt_conn *conn,
					     uint8_t aics_index,
					     const char *description)
{
	if (CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0) {
		return bt_aics_client_input_description_set(
			conn, AICS_CLI_VCS_CLIENT_INDEX(aics_index),
			description);
	} else {
		BT_DBG("Not supported");
		return -EOPNOTSUPP;
	}
}
