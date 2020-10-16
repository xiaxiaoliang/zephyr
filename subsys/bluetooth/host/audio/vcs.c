/*  Bluetooth VCS */

/*
 * Copyright (c) 2018 Intel Corporation
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
#include <bluetooth/services/vcs.h>

#include "vcs_internal.h"
#include "aics_internal.h"
#include "vocs_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_VCS)
#define LOG_MODULE_NAME bt_vcs
#include "common/log.h"

#define VCS_CP_LEN                      0x02
#define VCS_CP_ABS_VOL_LEN              0x03

#define VOLUME_DOWN(current_vol) \
	((uint8_t)MAX(0, (int)current_vol - vcs_inst.volume_step))
#define VOLUME_UP(current_vol) \
	((uint8_t)MIN(UINT8_MAX, (int)current_vol + vcs_inst.volume_step))

#define VALID_VCS_OPCODE(opcode) \
	((opcode) >= VCS_OPCODE_REL_VOL_DOWN && (opcode) <= VCS_OPCODE_MUTE)

struct vcs_inst_t {
	struct vcs_state_t state;
	uint8_t flags;
	struct bt_vcs_cb_t *cb;
	uint8_t volume_step;

	struct bt_gatt_service *service_p;
	/* TODO: Use instance pointers instead of indexes */
	struct bt_vocs *vocs_insts[CONFIG_BT_VCS_VOCS_INSTANCE_COUNT];
	struct bt_aics *aics_insts[CONFIG_BT_VCS_AICS_INSTANCE_COUNT];
};

#if defined(CONFIG_BT_VCS)

static struct vcs_inst_t vcs_inst = {
	.state.volume = 100,
	.volume_step = 1,
};

static void volume_state_cfg_changed(const struct bt_gatt_attr *attr,
				     uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t read_volume_state(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	BT_DBG("Volume %u, mute %u, counter %u",
	       vcs_inst.state.volume,
	       vcs_inst.state.mute,
	       vcs_inst.state.change_counter);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &vcs_inst.state, sizeof(vcs_inst.state));
}

static ssize_t write_vcs_control(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset,
				 uint8_t flags)
{
	const struct vcs_control_t *cp = buf;
	bool notify = false;
	bool volume_change = false;

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (!len || !buf) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	/* Check opcode before length */
	if (!VALID_VCS_OPCODE(cp->opcode)) {
		BT_DBG("Invalid opcode %u", cp->opcode);
		return BT_GATT_ERR(VCS_ERR_OP_NOT_SUPPORTED);
	}

	if ((len < VCS_CP_LEN) ||
	    (len == VCS_CP_ABS_VOL_LEN &&
	    cp->opcode != VCS_OPCODE_SET_ABS_VOL) ||
	    (len > VCS_CP_ABS_VOL_LEN)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	BT_DBG("Opcode %u, counter %u", cp->opcode, cp->counter);

	if (cp->counter != vcs_inst.state.change_counter) {
		return BT_GATT_ERR(VCS_ERR_INVALID_COUNTER);
	}

	switch (cp->opcode) {
	case VCS_OPCODE_REL_VOL_DOWN:
		BT_DBG("Relative Volume Down (0x00)");
		if (vcs_inst.state.volume) {
			vcs_inst.state.volume =
				VOLUME_DOWN(vcs_inst.state.volume);
			notify = true;
		}
		volume_change = true;
		break;
	case VCS_OPCODE_REL_VOL_UP:
		if (vcs_inst.state.volume != UINT8_MAX) {
			vcs_inst.state.volume =
				VOLUME_UP(vcs_inst.state.volume);
			notify = true;
		}
		volume_change = true;
		BT_DBG("Relative Volume Up (0x01)");
		break;
	case VCS_OPCODE_UNMUTE_REL_VOL_DOWN:
		BT_DBG("(Unmute) relative Volume Down (0x02)");
		if (vcs_inst.state.volume) {
			vcs_inst.state.volume =
				VOLUME_DOWN(vcs_inst.state.volume);
			notify = true;
		}
		if (vcs_inst.state.mute) {
			vcs_inst.state.mute = 0;
			notify = true;
		}
		volume_change = true;
		break;
	case VCS_OPCODE_UNMUTE_REL_VOL_UP:
		BT_DBG("(Unmute) relative Volume Up (0x03)");
		if (vcs_inst.state.volume != UINT8_MAX) {
			vcs_inst.state.volume =
				VOLUME_UP(vcs_inst.state.volume);
			notify = true;
		}
		if (vcs_inst.state.mute) {
			vcs_inst.state.mute = 0;
			notify = true;
		}
		volume_change = true;
		break;
	case VCS_OPCODE_SET_ABS_VOL:
		if (vcs_inst.state.volume != cp->volume) {
			vcs_inst.state.volume = cp->volume;
			notify = true;
		}
		volume_change = true;
		BT_DBG("Set Absolute Volume (0x04): %u", vcs_inst.state.volume);
		break;
	case VCS_OPCODE_UNMUTE:
		BT_DBG("Unmuted (0x05)");
		if (vcs_inst.state.mute) {
			vcs_inst.state.mute = 0;
			notify = true;
		}
		break;
	case VCS_OPCODE_MUTE:
		BT_DBG("Muted (0x06)");
		if (!vcs_inst.state.mute) {
			vcs_inst.state.mute = 1;
			notify = true;
		}
		break;
	default:
		return BT_GATT_ERR(VCS_ERR_OP_NOT_SUPPORTED);
	}

	if (notify) {
		vcs_inst.state.change_counter++;
		BT_DBG("New state: volume %u, mute %u, counter %u",
		       vcs_inst.state.volume,
		       vcs_inst.state.mute,
		       vcs_inst.state.change_counter);
		bt_gatt_notify_uuid(NULL, BT_UUID_VCS_STATE,
				    vcs_inst.service_p->attrs,
				    &vcs_inst.state, sizeof(vcs_inst.state));

		if (vcs_inst.cb && vcs_inst.cb->state) {
			vcs_inst.cb->state(NULL, 0, vcs_inst.state.volume,
					   vcs_inst.state.mute);
		}
	}

	if (volume_change && !vcs_inst.flags) {
		vcs_inst.flags = 1;
		bt_gatt_notify_uuid(NULL, BT_UUID_VCS_FLAGS,
				    vcs_inst.service_p->attrs,
				    &vcs_inst.flags, sizeof(vcs_inst.flags));

		if (vcs_inst.cb && vcs_inst.cb->state) {
			vcs_inst.cb->flags(NULL, 0, vcs_inst.flags);
		}
	}
	return len;
}

static void flags_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t read_flags(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	BT_DBG("0x%02x", vcs_inst.flags);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &vcs_inst.flags,
				 sizeof(vcs_inst.flags));
}

#define DUMMY_INCLUDE(i, _) BT_GATT_INCLUDE_SERVICE(NULL),
#define VOCS_INCLUDES(cnt) UTIL_LISTIFY(cnt, DUMMY_INCLUDE)
#define AICS_INCLUDES(cnt) UTIL_LISTIFY(cnt, DUMMY_INCLUDE)

#define BT_VCS_SERVICE_DEFINITION \
	BT_GATT_PRIMARY_SERVICE(BT_UUID_VCS), \
	VOCS_INCLUDES(CONFIG_BT_VCS_VOCS_INSTANCE_COUNT) \
	AICS_INCLUDES(CONFIG_BT_VCS_AICS_INSTANCE_COUNT) \
	BT_GATT_CHARACTERISTIC(BT_UUID_VCS_STATE, \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
			       BT_GATT_PERM_READ_ENCRYPT, \
			       read_volume_state, NULL, NULL), \
	BT_GATT_CCC(volume_state_cfg_changed, \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT), \
	BT_GATT_CHARACTERISTIC(BT_UUID_VCS_CONTROL, \
			       BT_GATT_CHRC_WRITE, \
			       BT_GATT_PERM_WRITE_ENCRYPT, \
			       NULL, write_vcs_control, NULL), \
	BT_GATT_CHARACTERISTIC(BT_UUID_VCS_FLAGS, \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
			       BT_GATT_PERM_READ_ENCRYPT, \
			       read_flags, NULL, NULL), \
	BT_GATT_CCC(flags_cfg_changed, \
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT)

static struct bt_gatt_attr vcs_attrs[] = { BT_VCS_SERVICE_DEFINITION };
static struct bt_gatt_service vcs_svc;

int bt_vcs_init(struct bt_vcs_init *init)
{
	int err;
	int i;
	int j;

	vcs_svc = (struct bt_gatt_service)BT_GATT_SERVICE(vcs_attrs);

	for (j = 0, i = 0; i < ARRAY_SIZE(vcs_attrs); i++) {
		if (!bt_uuid_cmp(vcs_attrs[i].uuid, BT_UUID_GATT_INCLUDE)) {
			vcs_inst.vocs_insts[j] = bt_vocs_free_instance_get();

			if (!vcs_inst.vocs_insts[j]) {
				BT_DBG("Could not get free VOCS instances[%u]",
				       j);
				return -ENOMEM;
			}

			err = bt_vocs_init(vcs_inst.vocs_insts[j],
					   init ? &init->vocs_init[j] : NULL);
			if (err) {
				BT_DBG("Could not init VOCS instance[%u]: %d",
				       j, err);
				return err;
			}

			vcs_attrs[i].user_data =
				bt_vocs_svc_decl_get(vcs_inst.vocs_insts[j]);
			j++;

			if (j == CONFIG_BT_VCS_VOCS_INSTANCE_COUNT) {
				break;
			}
		}
	}

	for (j = 0, i = 0; i < ARRAY_SIZE(vcs_attrs); i++) {
		if (!bt_uuid_cmp(vcs_attrs[i].uuid, BT_UUID_GATT_INCLUDE)) {
			vcs_inst.aics_insts[j] = bt_aics_free_instance_get();

			if (!vcs_inst.aics_insts[j]) {
				BT_DBG("Could not get free AICS instances[%u]",
				       j);
				return -ENOMEM;
			}

			err = bt_aics_init(vcs_inst.aics_insts[j],
					   init ? &init->aics_init[j] : NULL);
			if (err) {
				BT_DBG("Could not init AICS instance[%u]: %d",
				       j, err);
				return err;
			}

			vcs_attrs[i].user_data =
				bt_aics_svc_decl_get(vcs_inst.aics_insts[j]);
			j++;

			if (j == CONFIG_BT_VCS_AICS_INSTANCE_COUNT) {
				break;
			}
		}
	}

	vcs_inst.service_p = &vcs_svc;
	err = bt_gatt_service_register(&vcs_svc);

	if (err) {
		BT_DBG("VCS service register failed: %d", err);
	}

	return err;
}


/****************************** PUBLIC API ******************************/
int bt_vcs_aics_deactivate(uint8_t aics_index)
{
	if (aics_index >= CONFIG_BT_VCS_AICS_INSTANCE_COUNT) {
		return -EINVAL;
	}

	return bt_aics_deactivate(AICS_VCS_INDEX(aics_index));
}

int bt_vcs_aics_activate(uint8_t aics_index)
{
	if (aics_index >= CONFIG_BT_VCS_AICS_INSTANCE_COUNT) {
		return -EINVAL;
	}

	return bt_aics_activate(AICS_VCS_INDEX(aics_index));
}

void bt_vcs_server_cb_register(struct bt_vcs_cb_t *cb)
{
	int err;

	vcs_inst.cb = cb;

	for (int i = 0; i < CONFIG_BT_VCS_AICS_INSTANCE_COUNT; i++) {
		if (cb) {
			err = bt_aics_cb_register(AICS_VCS_INDEX(i),
						  &vcs_inst.cb->aics_cb);
		} else {
			err = bt_aics_cb_register(i, NULL);
		}

		if (err) {
			BT_WARN("[%d] Could not register AICS callbacks", i);
		}
	}

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	for (int i = 0; i < CONFIG_BT_VCS_VOCS_INSTANCE_COUNT; i++) {
		if (cb) {
			err = bt_vocs_cb_register(i, &vcs_inst.cb->vocs_cb);
		} else {
			err = bt_vocs_cb_register(i, NULL);
		}

		if (err) {
			BT_WARN("[%d] Could not register VOCS callbacks", i);
		}
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0 */
}

#endif /* CONFIG_BT_VCS */

#if defined(CONFIG_BT_VCS_CLIENT) || defined(CONFIG_BT_VCS)

int bt_vcs_volume_step_set(uint8_t volume_step)
{
#if defined(CONFIG_BT_VCS)
	if (volume_step > 0) {
		vcs_inst.volume_step = volume_step;
	} else {
		return -EINVAL;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_volume_get(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_read_volume_state(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		if (vcs_inst.cb && vcs_inst.cb->state) {
			vcs_inst.cb->state(NULL, 0, vcs_inst.state.volume,
					vcs_inst.state.mute);
		}

		return 0;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_flags_get(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_read_flags(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		if (vcs_inst.cb && vcs_inst.cb->state) {
			vcs_inst.cb->flags(NULL, 0, vcs_inst.flags);
		}

		return 0;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_volume_down(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_volume_down(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		const struct vcs_control_t cp = {
			.opcode = VCS_OPCODE_REL_VOL_DOWN,
			.counter = vcs_inst.state.change_counter,
		};
		int err = write_vcs_control(NULL, NULL, &cp, VCS_CP_LEN, 0, 0);

		return err > 0 ? 0 : err;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_volume_up(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_volume_up(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		const struct vcs_control_t cp = {
			.opcode = VCS_OPCODE_REL_VOL_UP,
			.counter = vcs_inst.state.change_counter,
		};
		int err = write_vcs_control(NULL, NULL, &cp, VCS_CP_LEN, 0, 0);

		return err > 0 ? 0 : err;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_unmute_volume_down(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_unmute_volume_down(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		const struct vcs_control_t cp = {
			.opcode = VCS_OPCODE_UNMUTE_REL_VOL_DOWN,
			.counter = vcs_inst.state.change_counter,
		};
		int err = write_vcs_control(NULL, NULL, &cp, VCS_CP_LEN, 0, 0);

		return err > 0 ? 0 : err;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_unmute_volume_up(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_unmute_volume_up(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		const struct vcs_control_t cp = {
			.opcode = VCS_OPCODE_UNMUTE_REL_VOL_UP,
			.counter = vcs_inst.state.change_counter,
		};
		int err = write_vcs_control(NULL, NULL, &cp, VCS_CP_LEN, 0, 0);

		return err > 0 ? 0 : err;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_volume_set(struct bt_conn *conn, uint8_t volume)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_set_volume(conn, volume);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		const struct vcs_control_t cp = {
			.opcode = VCS_OPCODE_SET_ABS_VOL,
			.counter = vcs_inst.state.change_counter,
			.volume = volume
		};
		int err = write_vcs_control(NULL, NULL, &cp, VCS_CP_ABS_VOL_LEN,
					    0, 0);

		return err > 0 ? 0 : err;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_unmute(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_unmute(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		const struct vcs_control_t cp = {
			.opcode = VCS_OPCODE_UNMUTE,
			.counter = vcs_inst.state.change_counter,
		};
		int err = write_vcs_control(NULL, NULL, &cp, VCS_CP_LEN, 0, 0);

		return err > 0 ? 0 : err;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_mute(struct bt_conn *conn)
{
#if defined(CONFIG_BT_VCS_CLIENT)
	if (conn) {
		return bt_vcs_client_mute(conn);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS)
	if (!conn) {
		const struct vcs_control_t cp = {
			.opcode = VCS_OPCODE_MUTE,
			.counter = vcs_inst.state.change_counter,
		};
		int err = write_vcs_control(NULL, NULL, &cp, VCS_CP_LEN, 0, 0);

		return err > 0 ? 0 : err;
	}
#endif /* CONFIG_BT_VCS */
	return -EOPNOTSUPP;
}

int bt_vcs_vocs_state_get(struct bt_conn *conn, uint8_t vocs_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	if (conn) {
		return bt_vcs_client_vocs_read_offset_state(conn, vocs_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	if (!conn) {
		return bt_vocs_offset_state_get(vocs_index);
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_vocs_location_get(struct bt_conn *conn, uint8_t vocs_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	if (conn) {
		return bt_vcs_client_vocs_read_location(conn, vocs_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	if (!conn) {
		return bt_vocs_location_get(vocs_index);
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_vocs_location_set(struct bt_conn *conn, uint8_t vocs_index,
			     uint8_t location)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	if (conn) {
		return bt_vcs_client_vocs_set_location(conn, vocs_index,
						       location);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	if (!conn) {
		return bt_vocs_location_set(vocs_index, location);
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_vocs_state_set(struct bt_conn *conn, uint8_t vocs_index,
			  int16_t offset)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	if (conn) {
		return bt_vcs_client_vocs_set_offset(conn, vocs_index, offset);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	if (!conn) {
		return bt_vocs_state_set(vocs_index, offset);
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_vocs_description_get(struct bt_conn *conn, uint8_t vocs_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	if (conn) {
		return bt_vcs_client_vocs_read_output_description(conn,
								  vocs_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	if (!conn) {
		return bt_vocs_output_description_get(vocs_index);
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_vocs_description_set(struct bt_conn *conn, uint8_t vocs_index,
				const char *description)
{
#if CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST > 0
	if (conn) {
		return bt_vcs_client_vocs_set_output_description(
			conn, vocs_index, description);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_VOCS_INST */

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	if (!conn) {
		return bt_vocs_output_description_set(vocs_index, description);
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_state_get(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_read_input_state(conn, aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_input_state_get(AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_gain_setting_get(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_read_gain_setting(conn, aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_gain_setting_get(AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_type_get(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_read_input_type(conn, aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	return bt_aics_input_type_get(AICS_VCS_INDEX(aics_index));
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_status_get(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_read_input_status(conn, aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_input_status_get(AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_unmute(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_input_unmute(conn, aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_input_unmute(AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_mute(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_input_mute(conn, aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_input_mute(AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_manual_gain_set(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_set_manual_input_gain(conn,
								aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_manual_input_gain_set(
			AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_automatic_gain_set(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_set_automatic_input_gain(conn,
								   aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_automatic_input_gain_set(
			AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_gain_set(struct bt_conn *conn, uint8_t aics_index, int8_t gain)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_set_gain(conn, aics_index, gain);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_gain_set(AICS_VCS_INDEX(aics_index), gain);
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_description_get(struct bt_conn *conn, uint8_t aics_index)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_read_input_description(conn,
								 aics_index);
	}
#endif /* CONFIG_BT_VCS_CLIENT_MAX_AICS_INST */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_input_description_get(
			AICS_VCS_INDEX(aics_index));
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}

int bt_vcs_aics_description_set(struct bt_conn *conn, uint8_t aics_index,
				const char *description)
{
#if CONFIG_BT_VCS_CLIENT_MAX_AICS_INST > 0
	if (conn) {
		return bt_vcs_client_aics_set_input_description(conn,
								aics_index,
								description);
	}
#endif /* CONFIG_BT_VCS_CLIENT */

#if defined(CONFIG_BT_VCS_AICS_INSTANCE_COUNT)
	if (!conn) {
		return bt_aics_input_description_set(AICS_VCS_INDEX(aics_index),
						     description);
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */
	return -EOPNOTSUPP;
}
#endif /* CONFIG_BT_VCS_CLIENT || CONFIG_BT_VCS */
