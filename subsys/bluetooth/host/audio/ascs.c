/* @file
 * @brief Bluetooth ASCS
 */
/*
 * Copyright (c) 2020 Intel Corporation
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
#include <bluetooth/audio.h>

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_ASCS)
#define LOG_MODULE_NAME bt_ascs
#include "common/log.h"

#include "../hci_core.h"
#include "../conn_internal.h"

#include "chan.h"
#include "endpoint.h"
#include "pacs_internal.h"

#define ASE_ID(_ase) ase->ep.status.id

struct bt_ascs_ase {
	struct bt_ascs *ascs;
	struct bt_audio_ep ep;
	struct k_work work;
};

struct bt_ascs {
	struct bt_conn *conn;
	uint8_t id;
	bt_addr_le_t peer;
	struct bt_ascs_ase ases[CONFIG_BT_ASCS_ASE_COUNT];
	struct bt_gatt_notify_params params;
	uint16_t handle;
};

static struct bt_ascs sessions[CONFIG_BT_MAX_CONN];

static void ascs_ase_cfg_changed(const struct bt_gatt_attr *attr,
				 uint16_t value)
{
	BT_DBG("attr %p value 0x%04x", attr, value);
}

static void ase_chan_del(struct bt_ascs_ase *ase)
{
	struct bt_audio_chan *chan = ase->ep.chan;

	if (!chan) {
		return;
	}

	BT_DBG("ase %p chan %p", ase, chan);

	bt_audio_chan_reset(chan);
}

NET_BUF_SIMPLE_DEFINE_STATIC(rsp_buf, CONFIG_BT_L2CAP_TX_MTU);

static void ascs_cp_rsp_alloc(uint8_t op)
{
	struct bt_ascs_cp_rsp *rsp;

	rsp = net_buf_simple_add(&rsp_buf, sizeof(*rsp));
	rsp->op = op;
	rsp->num_ase = 0;
}

/* Add response to an opcode/ASE ID */
static void ascs_cp_rsp_add(uint8_t id, uint8_t op, uint8_t code,
			    uint8_t reason)
{
	struct bt_ascs_cp_rsp *rsp = (void *)rsp_buf.__buf;
	struct bt_ascs_cp_ase_rsp *ase_rsp;

	BT_DBG("id 0x%02x op %s (0x%02x) code %s (0x%02x) reason %s (0x%02x)",
	       id, bt_ascs_op_str(op), op, bt_ascs_rsp_str(code), code,
	       bt_ascs_reason_str(reason), reason);

	/* Allocate response if buffer is empty */
	if (!rsp_buf.len) {
		ascs_cp_rsp_alloc(op);
	}

	if (rsp->num_ase == 0xff) {
		return;
	}

	switch (code) {
	/* If the Response_Code value is 0x01 or 0x02, Number_of_ASEs shall be
	 * set to 0xFF.
	 */
	case BT_ASCS_RSP_NOT_SUPPORTED:
	case BT_ASCS_RSP_TRUNCATED:
		rsp->num_ase = 0xff;
		break;
	default:
		rsp->num_ase++;
		break;
	}

	ase_rsp = net_buf_simple_add(&rsp_buf, sizeof(*ase_rsp));
	ase_rsp->id = id;
	ase_rsp->code = code;
	ase_rsp->reason = reason;
}

static void ascs_cp_rsp_add_errno(uint8_t id, uint8_t op, int err,
				  uint8_t reason)
{
	switch (err) {
	case -ENOBUFS:
	case -ENOMEM:
		return ascs_cp_rsp_add(id, op, BT_ASCS_RSP_NO_MEM,
				       BT_ASCS_REASON_NONE);
	case -EINVAL:
		switch (op) {
		case BT_ASCS_CONFIG_OP:
		/* Fallthrough */
		case BT_ASCS_QOS_OP:
			return ascs_cp_rsp_add(id, op,
					       BT_ASCS_RSP_CONF_INVALID,
					       reason);
		case BT_ASCS_ENABLE_OP:
		/* Fallthrough */
		case BT_ASCS_METADATA_OP:
			return ascs_cp_rsp_add(id, op,
					       BT_ASCS_RSP_METADATA_INVALID,
					       reason);
		default:
			return ascs_cp_rsp_add(id, op, BT_ASCS_RSP_UNSPECIFIED,
					       BT_ASCS_REASON_NONE);
		}
	case -ENOTSUP:
		switch (op) {
		case BT_ASCS_CONFIG_OP:
		/* Fallthrough */
		case BT_ASCS_QOS_OP:
			return ascs_cp_rsp_add(id, op,
					       BT_ASCS_RSP_CONF_UNSUPPORTED,
					       reason);
		case BT_ASCS_ENABLE_OP:
		/* Fallthrough */
		case BT_ASCS_METADATA_OP:
			return ascs_cp_rsp_add(id, op,
					       BT_ASCS_RSP_METADATA_UNSUPPORTED,
					       reason);
		default:
			return ascs_cp_rsp_add(id, op,
					       BT_ASCS_RSP_NOT_SUPPORTED,
					       BT_ASCS_REASON_NONE);
		}
	case -EBADMSG:
		return ascs_cp_rsp_add(id, op, BT_ASCS_RSP_INVALID_ASE_STATE,
					       BT_ASCS_REASON_NONE);
	default:
		return ascs_cp_rsp_add(id, op, BT_ASCS_RSP_UNSPECIFIED,
				       BT_ASCS_REASON_NONE);
	}
}

static void ascs_cp_rsp_success(uint8_t id, uint8_t op)
{
	ascs_cp_rsp_add(id, op, BT_ASCS_RSP_SUCCESS, BT_ASCS_REASON_NONE);
}

/* Notify response to control point */
static void ascs_cp_notify(struct bt_ascs *ascs)
{
	struct bt_gatt_attr attr;

	BT_DBG("ascs %p handle 0x%04x len %u", ascs, ascs->handle, rsp_buf.len);

	memset(&attr, 0, sizeof(attr));
	attr.handle = ascs->handle;
	attr.uuid = BT_UUID_ASCS_ASE_CP;

	bt_gatt_notify(ascs->conn, &attr, rsp_buf.data, rsp_buf.len);
}

static void ase_release(struct bt_ascs_ase *ase, bool cache)
{
	int err;

	BT_DBG("ase %p", ase);

	err = bt_audio_chan_release(ase->ep.chan, cache);
	if (err) {
		ascs_cp_rsp_add_errno(ASE_ID(ase), BT_ASCS_RELEASE_OP, err,
				      BT_ASCS_REASON_NONE);
		return;
	}

	ascs_cp_rsp_success(ASE_ID(ase), BT_ASCS_RELEASE_OP);
}

static void ascs_clear(struct bt_ascs *ascs)
{
	int i;

	BT_DBG("ascs %p", ascs);

	bt_addr_le_copy(&ascs->peer, BT_ADDR_LE_ANY);

	for (i = 0; i < CONFIG_BT_ASCS_ASE_COUNT; i++) {
		struct bt_ascs_ase *ase = &ascs->ases[i];

		if (ase->ep.status.state != BT_ASCS_ASE_STATE_IDLE) {
			ase_release(ase, false);
			bt_audio_ep_set_state(&ase->ep, BT_ASCS_ASE_STATE_IDLE);
		}
	}

	bt_conn_unref(ascs->conn);
	ascs->conn = NULL;
}

static void ase_disable(struct bt_ascs_ase *ase)
{
	int err;

	BT_DBG("ase %p", ase);

	err = bt_audio_chan_disable(ase->ep.chan);
	if (err) {
		BT_ERR("Disable failed: %d", err);
		ascs_cp_rsp_add_errno(ASE_ID(ase), BT_ASCS_DISABLE_OP,
				      err, BT_ASCS_REASON_NONE);
		return;
	}

	ascs_cp_rsp_success(ASE_ID(ase), BT_ASCS_DISABLE_OP);
}

static void ascs_dettach(struct bt_ascs *ascs)
{
	int i;

	BT_DBG("ascs %p conn %p", ascs, ascs->conn);

	/* Update address in case it has changed */
	ascs->id = ascs->conn->id;
	bt_addr_le_copy(&ascs->peer, &ascs->conn->le.dst);

	/* TODO: Store the ASES in the settings? */

	for (i = 0; i < CONFIG_BT_ASCS_ASE_COUNT; i++) {
		struct bt_ascs_ase *ase = &ascs->ases[i];

		if (ase->ep.status.state != BT_ASCS_ASE_STATE_IDLE) {
			/* Cache if disconnected with codec configured */
			ase_release(ase, true);
		}
	}

	bt_conn_unref(ascs->conn);
	ascs->conn = NULL;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int i;

	BT_DBG("");

	for (i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		if (sessions[i].conn != conn) {
			continue;
		}

		/* Release existing ASEs if not bonded */
		if (!bt_addr_le_is_bonded(conn->id, &conn->le.dst)) {
			ascs_clear(&sessions[i]);
		} else {
			ascs_dettach(&sessions[i]);
		}
	}
}

static struct bt_conn_cb conn_cb = {
	.disconnected = disconnected,
};

static uint8_t ascs_attr_cb(const struct bt_gatt_attr *attr, uint16_t handle,
			    void *user_data)
{
	struct bt_ascs *ascs = user_data;

	ascs->handle = handle;

	return BT_GATT_ITER_CONTINUE;
}

static struct bt_ascs *ascs_new(struct bt_conn *conn)
{
	static bool conn_cb_registered;
	int i;

	for (i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		if (!sessions[i].conn &&
		    !bt_addr_le_cmp(&sessions[i].peer, BT_ADDR_LE_ANY)) {
			struct bt_ascs *ascs = &sessions[i];

			memset(ascs->ases, 0, sizeof(ascs->ases));
			ascs->conn = bt_conn_ref(conn);

			/* Register callbacks if not registered */
			if (!conn_cb_registered) {
				bt_conn_cb_register(&conn_cb);
				conn_cb_registered = true;
			}

			if (!ascs->handle) {
				bt_gatt_foreach_attr_type(0x0001, 0xffff,
							  BT_UUID_ASCS_ASE_CP,
							  NULL, 1,
							  ascs_attr_cb, ascs);
			}

			return ascs;
		}
	}

	return NULL;
}

static void ase_chan_add(struct bt_ascs *ascs, struct bt_ascs_ase *ase,
			 struct bt_audio_chan *chan)
{
	BT_DBG("ase %p chan %p", ase, chan);
	ase->ep.chan = chan;
	chan->conn = ascs->conn;
}

static void ascs_attach(struct bt_ascs *ascs, struct bt_conn *conn)
{
	int i;

	BT_DBG("ascs %p conn %p", ascs, conn);

	ascs->conn = bt_conn_ref(conn);

	/* TODO: Load the ASEs from the settings? */

	for (i = 0; i < CONFIG_BT_ASCS_ASE_COUNT; i++) {
		if (ascs->ases[i].ep.chan) {
			ase_chan_add(ascs, &ascs->ases[i],
				     ascs->ases[i].ep.chan);
		}
	}
}

static struct bt_ascs *ascs_find(struct bt_conn *conn)
{
	int i;

	for (i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		if (sessions[i].conn == conn) {
			return &sessions[i];
		}

		/* Check if there is an existing session for the peer */
		if (!sessions[i].conn &&
		    bt_conn_is_peer_addr_le(conn, sessions[i].id,
					    &sessions[i].peer)) {
			ascs_attach(&sessions[i], conn);
			return &sessions[i];
		}
	}

	return NULL;
}

static struct bt_ascs *ascs_get(struct bt_conn *conn)
{
	struct bt_ascs *ascs;

	ascs = ascs_find(conn);
	if (ascs) {
		return ascs;
	}

	return ascs_new(conn);
}

NET_BUF_SIMPLE_DEFINE_STATIC(ase_buf, CONFIG_BT_L2CAP_TX_MTU);

static void ase_process(struct k_work *work)
{
	struct bt_ascs_ase *ase = CONTAINER_OF(work, struct bt_ascs_ase, work);
	struct bt_gatt_attr attr;

	bt_audio_ep_get_status(&ase->ep, &ase_buf);

	memset(&attr, 0, sizeof(attr));
	attr.handle = ase->ep.handle;
	attr.uuid = BT_UUID_ASCS_ASE;

	bt_gatt_notify(ase->ascs->conn, &attr, ase_buf.data, ase_buf.len);

	if (ase->ep.status.state == BT_ASCS_ASE_STATE_RELEASING &&
	    !ase->ep.chan) {
		bt_audio_ep_set_state(&ase->ep, BT_ASCS_ASE_STATE_IDLE);
	}

	if (ase->ep.status.state == BT_ASCS_ASE_STATE_IDLE) {
		return;
	}
}

static uint8_t ase_attr_cb(const struct bt_gatt_attr *attr, uint16_t handle,
			   void *user_data)
{
	struct bt_ascs_ase *ase = user_data;

	ase->ep.handle = handle;

	return BT_GATT_ITER_CONTINUE;
}

static void ase_init(struct bt_ascs_ase *ase, uint8_t id)
{
	memset(ase, 0, sizeof(*ase));
	bt_audio_ep_init(&ase->ep, BT_AUDIO_EP_LOCAL, 0x0000, id);
	bt_gatt_foreach_attr_type(0x0001, 0xffff, BT_UUID_ASCS_ASE,
				  UINT_TO_POINTER(id), 1, ase_attr_cb, ase);
	k_work_init(&ase->work, ase_process);
}

static void ase_status_changed(struct bt_audio_ep *ep, uint8_t old_state,
			       uint8_t state)
{
	struct bt_ascs_ase *ase = CONTAINER_OF(ep, struct bt_ascs_ase, ep);

	BT_DBG("ase %p conn %p", ase, ase->ascs->conn);

	if (state == BT_ASCS_ASE_STATE_RELEASING ||
	    state == BT_ASCS_ASE_STATE_IDLE) {
		ase_chan_del(ase);
	}

	if (!ase->ascs->conn || ase->ascs->conn->state != BT_CONN_CONNECTED) {
		return;
	}

	k_work_submit(&ase->work);
}

static struct bt_audio_ep_cb ase_cb = {
	.status_changed = ase_status_changed,
};

static struct bt_ascs_ase *ase_new(struct bt_ascs *ascs, uint8_t id)
{
	struct bt_ascs_ase *ase;
	int i;

	if (id) {
		if (id > CONFIG_BT_ASCS_ASE_COUNT) {
			return NULL;
		}
		i = id;
		ase = &ascs->ases[i - 1];
		goto done;
	}

	for (i = 0; i < CONFIG_BT_ASCS_ASE_COUNT; i++) {
		ase = &ascs->ases[i];

		if (!ase->ep.status.id) {
			i++;
			goto done;
		}
	}

	return NULL;

done:
	ase_init(ase, i);
	ase->ascs = ascs;

	bt_audio_ep_register_cb(&ase->ep, &ase_cb);

	return ase;
}

static struct bt_ascs_ase *ase_find(struct bt_ascs *ascs, uint8_t id)
{
	struct bt_ascs_ase *ase;

	if (!id || id > CONFIG_BT_ASCS_ASE_COUNT) {
		return NULL;
	}

	ase = &ascs->ases[id - 1];
	if (ase->ep.status.id == id) {
		return ase;
	}

	return NULL;
}

static struct bt_ascs_ase *ase_get(struct bt_ascs *ascs, uint8_t id)
{
	struct bt_ascs_ase *ase;

	ase = ase_find(ascs, id);
	if (ase) {
		return ase;
	}

	return ase_new(ascs, id);
}

static ssize_t ascs_ase_read(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	struct bt_ascs *ascs;
	struct bt_ascs_ase *ase;

	BT_DBG("conn %p attr %p buf %p len %u offset %u", conn, attr, buf, len,
	       offset);

	ascs = ascs_get(conn);
	if (!ascs) {
		BT_ERR("Unable to get ASCS session");
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	ase = ase_get(ascs, POINTER_TO_UINT(attr->user_data));
	if (!ascs) {
		BT_ERR("Unable to get ASE");
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	bt_audio_ep_get_status(&ase->ep, &ase_buf);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, ase_buf.data,
				 ase_buf.len);
}

static void ascs_cp_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	BT_DBG("attr %p value 0x%04x", attr, value);
}

static int ase_config(struct bt_ascs *ascs, struct bt_ascs_ase *ase,
		      const struct bt_ascs_config *cfg,
		      struct net_buf_simple *buf)
{
	sys_slist_t *lst;
	struct bt_audio_cap *cap;
	struct bt_audio_chan *chan;
	int err;

	BT_DBG("ase %p dir 0x%02x latency 0x%02x phy 0x%02x codec 0x%02x "
		"cid 0x%04x vid 0x%04x codec config len 0x%02x", ase, cfg->dir,
		cfg->latency, cfg->phy, cfg->codec.id, cfg->codec.cid,
		cfg->codec.vid, cfg->cc_len);

	if (cfg->latency < 0x01 || cfg->latency > 0x03) {
		BT_ERR("Invalid latency: 0x%02x", cfg->latency);
		ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_CONFIG_OP,
				BT_ASCS_RSP_CONF_INVALID,
				BT_ASCS_REASON_LATENCY);
		return 0;
	}

	if (cfg->phy < 0x01 || cfg->phy > 0x03) {
		BT_ERR("Invalid PHY: 0x%02x", cfg->phy);
		ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_CONFIG_OP,
				BT_ASCS_RSP_CONF_INVALID, BT_ASCS_REASON_PHY);
		return 0;
	}

	switch (ase->ep.status.state) {
	/* Valid only if ASE_State field = 0x00 (Idle) */
	case BT_ASCS_ASE_STATE_IDLE:
	 /* or 0x01 (Codec Configured) */
	case BT_ASCS_ASE_STATE_CONFIG:
	 /* or 0x02 (QoS Configured) */
	case BT_ASCS_ASE_STATE_QOS:
		break;
	default:
		BT_ERR("Invalid state: %s",
		       bt_audio_ep_state_str(ase->ep.status.state));
		ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_CONFIG_OP,
				BT_ASCS_RSP_INVALID_ASE_STATE, 0x00);
		return 0;
	}

	/* Check if there are capabilities for the given direction */
	if (bt_audio_cap_get(cfg->dir, &lst, NULL) < 0) {
		goto not_found;
	}

	SYS_SLIST_FOR_EACH_CONTAINER(lst, cap, node) {
		struct bt_codec codec;

		/* Skip if capabilities don't match */
		if (cfg->codec.id != cap->codec->id) {
			continue;
		}

		/* Store current codec configuration to be able to restore it
		 * in case of error.
		 */
		memcpy(&codec, &ase->ep.codec, sizeof(codec));

		if (bt_audio_ep_set_codec(&ase->ep, cfg->codec.id,
					  sys_le16_to_cpu(cfg->codec.cid),
					  sys_le16_to_cpu(cfg->codec.vid),
					  buf, cfg->cc_len, &ase->ep.codec)) {
			memcpy(&ase->ep.codec, &codec, sizeof(codec));
			ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_CONFIG_OP,
					BT_ASCS_RSP_CONF_INVALID,
					BT_ASCS_REASON_CODEC_DATA_LEN);
			return 0;
		}

		if (ase->ep.chan) {
			err = bt_audio_chan_reconfig(ase->ep.chan, cap,
						     &ase->ep.codec);
			if (err) {
				uint8_t reason = BT_ASCS_REASON_CODEC_DATA;

				BT_ERR("Reconfig failed: %d", err);

				memcpy(&ase->ep.codec, &codec, sizeof(codec));
				ascs_cp_rsp_add_errno(ASE_ID(ase),
						      BT_ASCS_CONFIG_OP,
						      err, reason);
				return 0;
			}
			chan = ase->ep.chan;
		} else {
			chan = bt_audio_chan_config(ascs->conn, &ase->ep, cap,
						    &ase->ep.codec);
			if (!chan) {
				BT_ERR("Config failed");

				memcpy(&ase->ep.codec, &codec, sizeof(codec));
				ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_CONFIG_OP,
						BT_ASCS_RSP_CONF_REJECTED,
						BT_ASCS_REASON_CODEC_DATA);
				return 0;
			}
			ase_chan_add(ascs, ase, chan);
		}

		ascs_cp_rsp_success(ASE_ID(ase), BT_ASCS_CONFIG_OP);

		return 0;
	}

not_found:
	BT_ERR("Unable to find matching Capability");
	ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_CONFIG_OP,
			BT_ASCS_RSP_CAP_UNSUPPORTED, 0x00);
	return 0;
}

static ssize_t ascs_config(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_config_op *req;
	const struct bt_ascs_config *cfg;
	int i;

	if (buf->len < sizeof(*req)) {
		BT_ERR("Malformed ASE Config");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases * sizeof(*cfg)) {
		BT_ERR("Malformed ASE Config: len %u < %zu", buf->len,
		       req->num_ases * sizeof(*cfg));
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		struct bt_ascs_ase *ase;

		if (buf->len < sizeof(*cfg)) {
			BT_ERR("Malformed ASE Config: len %u < %zu", buf->len,
			       sizeof(*cfg));
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		}

		cfg = net_buf_simple_pull_mem(buf, sizeof(*cfg));

		if (buf->len < cfg->cc_len) {
			BT_ERR("Malformed ASE Codec Config len %u != %zu",
				buf->len, cfg->cc_len);
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		}

		BT_DBG("ase 0x%02x cc_len %u", cfg->ase, cfg->cc_len);

		if (cfg->ase) {
			ase = ase_get(ascs, cfg->ase);
		} else {
			ase = ase_new(ascs, 0);
		}

		if (!ase) {
			ascs_cp_rsp_add(cfg->ase, BT_ASCS_CONFIG_OP,
					BT_ASCS_RSP_INVALID_ASE, 0x00);
			BT_ERR("Unable to find ASE");
			continue;
		}

		if (ase_config(ascs, ase, cfg, buf) < 0) {
			BT_ERR("Malformed ASE Config");
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		}
	}

	return buf->size;
}

static void ase_qos(struct bt_ascs_ase *ase, const struct bt_ascs_qos *qos)
{
	struct bt_audio_chan *chan = ase->ep.chan;
	struct bt_codec_qos *cqos = &ase->ep.qos;
	int err;

	cqos->interval = sys_get_le24(qos->interval);
	cqos->framing = qos->framing;
	cqos->phy = qos->phy;
	cqos->sdu = sys_le16_to_cpu(qos->sdu);
	cqos->rtn = qos->rtn;
	cqos->latency = sys_le16_to_cpu(qos->latency);
	cqos->pd = sys_get_le24(qos->pd);

	BT_DBG("ase %p cig 0x%02x cis 0x%02x interval %u framing 0x%02x "
	       "phy 0x%02x sdu %u rtn %u latency %u pd %u", ase, qos->cig,
	       qos->cis, cqos->interval, cqos->framing, cqos->phy, cqos->sdu,
	       cqos->rtn, cqos->latency, cqos->pd);

	err = bt_audio_chan_qos(chan, cqos);
	if (err) {
		uint8_t reason = BT_ASCS_REASON_NONE;

		BT_ERR("QoS failed: err %d", err);

		memset(cqos, 0, sizeof(*cqos));

		if (err == -ENOTSUP) {
			if (!qos->interval) {
				reason = BT_ASCS_REASON_INTERVAL;
			} else if (qos->framing == 0xff) {
				reason = BT_ASCS_REASON_FRAMING;
			} else if (!qos->phy) {
				reason = BT_ASCS_REASON_PHY;
			} else if (qos->sdu == 0xffff) {
				reason = BT_ASCS_REASON_SDU;
			} else if (!qos->latency) {
				reason = BT_ASCS_REASON_LATENCY;
			} else if (!qos->pd) {
				reason = BT_ASCS_REASON_PD;
			}
		}

		ascs_cp_rsp_add_errno(ASE_ID(ase), BT_ASCS_QOS_OP,
				      err, reason);
		return;
	}

	ase->ep.cig = qos->cig;
	ase->ep.cis = qos->cis;

	ascs_cp_rsp_success(ASE_ID(ase), BT_ASCS_QOS_OP);
}

static ssize_t ascs_qos(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_qos_op *req;
	const struct bt_ascs_qos *qos;
	int i;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases * sizeof(*qos)) {
		BT_ERR("Malformed ASE QoS: len %u < %zu", buf->len,
		       req->num_ases * sizeof(*qos));
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		struct bt_ascs_ase *ase;

		qos = net_buf_simple_pull_mem(buf, sizeof(*qos));

		BT_DBG("ase 0x%02x", qos->ase);

		ase = ase_find(ascs, qos->ase);
		if (!ase) {
			ascs_cp_rsp_add(qos->ase, BT_ASCS_QOS_OP,
					BT_ASCS_RSP_INVALID_ASE, 0x00);
			BT_ERR("Unable to find ASE");
			continue;
		}

		ase_qos(ase, qos);
	}

	return buf->size;
}

static int ase_metadata(struct bt_ascs_ase *ase, uint8_t op,
			struct bt_ascs_metadata *meta,
			struct net_buf_simple *buf)
{
	int err;

	BT_DBG("ase %p meta->len %u", ase, meta->len);

	if (!meta->len) {
		goto done;
	}

	if (bt_audio_ep_set_metadata(&ase->ep, buf, meta->len,
				     &ase->ep.codec)) {
		ascs_cp_rsp_add(ASE_ID(ase), op,
				BT_ASCS_RSP_METADATA_INVALID,
				BT_ASCS_REASON_METADATA);
		return 0;
	}

	err = bt_audio_chan_metadata(ase->ep.chan, ase->ep.codec.meta_count,
				     ase->ep.codec.meta);
	if (err) {
		BT_ERR("Metadata failed: %d", err);
		ascs_cp_rsp_add_errno(ASE_ID(ase), op, err,
				      buf->len ? *buf->data : 0x00);
		return -EFAULT;
	}

done:
	ascs_cp_rsp_success(ASE_ID(ase), op);

	return 0;
}

static int ase_enable(struct bt_ascs_ase *ase, struct bt_ascs_metadata *meta,
		      struct net_buf_simple *buf)
{
	int err;

	BT_DBG("ase %p buf->len %u", ase, buf->len);

	if (bt_audio_ep_set_metadata(&ase->ep, buf, meta->len,
				     &ase->ep.codec)) {
		ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_ENABLE_OP,
				BT_ASCS_RSP_METADATA_INVALID,
				BT_ASCS_REASON_METADATA);
		return 0;
	}

	err = bt_audio_chan_enable(ase->ep.chan, ase->ep.codec.meta_count,
				   ase->ep.codec.meta);
	if (err) {
		BT_ERR("Enable rejected: %d", err);
		ascs_cp_rsp_add_errno(ASE_ID(ase), BT_ASCS_ENABLE_OP, err,
				      BT_ASCS_REASON_NONE);
		return -EFAULT;
	}

	ascs_cp_rsp_success(ASE_ID(ase), BT_ASCS_ENABLE_OP);

	return 0;
}

static ssize_t ascs_enable(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_enable_op *req;
	struct bt_ascs_metadata *meta;
	int i;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases * sizeof(*meta)) {
		BT_ERR("Malformed ASE Metadata: len %u < %zu", buf->len,
		       req->num_ases * sizeof(*meta));
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		struct bt_ascs_ase *ase;
		struct bt_ascs_metadata *meta;

		meta = net_buf_simple_pull_mem(buf, sizeof(*meta));

		BT_DBG("ase 0x%02x meta->len %u", meta->ase, meta->len);

		if (buf->len < meta->len) {
			BT_ERR("Malformed ASE Enable Metadata len %u != %zu",
				buf->len, meta->len);
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		}

		ase = ase_find(ascs, meta->ase);
		if (!ase) {
			ascs_cp_rsp_add(meta->ase, BT_ASCS_ENABLE_OP,
					BT_ASCS_RSP_INVALID_ASE, 0x00);
			BT_ERR("Unable to find ASE");
			continue;
		}

		ase_enable(ase, meta, buf);
	}

	return buf->size;
}

static void ase_start(struct bt_ascs_ase *ase)
{
	int err;

	BT_DBG("ase %p", ase);

	err = bt_audio_chan_start(ase->ep.chan);
	if (err) {
		BT_ERR("Start failed: %d", err);
		ascs_cp_rsp_add(ASE_ID(ase), BT_ASCS_START_OP, err,
				BT_ASCS_REASON_NONE);
		return;
	}

	ascs_cp_rsp_success(ASE_ID(ase), BT_ASCS_START_OP);
}

static ssize_t ascs_start(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_start_op *req;
	int i;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases) {
		BT_ERR("Malformed ASE Start: len %u < %zu", buf->len,
		       req->num_ases);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		struct bt_ascs_ase *ase;
		uint8_t id;

		id = net_buf_simple_pull_u8(buf);

		BT_DBG("ase 0x%02x", id);

		ase = ase_find(ascs, id);
		if (!ase) {
			ascs_cp_rsp_add(id, BT_ASCS_START_OP,
					BT_ASCS_RSP_INVALID_ASE, 0x00);
			BT_ERR("Unable to find ASE");
			continue;
		}

		ase_start(ase);
	}

	return buf->size;
}

static ssize_t ascs_disable(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_disable_op *req;
	int i;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases) {
		BT_ERR("Malformed ASE Disable: len %u < %zu", buf->len,
		       req->num_ases);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		struct bt_ascs_ase *ase;
		uint8_t id;

		id = net_buf_simple_pull_u8(buf);

		BT_DBG("ase 0x%02x", id);

		ase = ase_find(ascs, id);
		if (!ase) {
			ascs_cp_rsp_add(id, BT_ASCS_DISABLE_OP,
					BT_ASCS_RSP_INVALID_ASE_STATE, 0);
			BT_ERR("Unable to find ASE");
			continue;
		}

		ase_disable(ase);
	}

	return buf->size;
}

static void ase_stop(struct bt_ascs_ase *ase)
{
	int err;

	BT_DBG("ase %p", ase);

	err = bt_audio_chan_stop(ase->ep.chan);
	if (err) {
		BT_ERR("Stop failed: %d", err);
		ascs_cp_rsp_add_errno(ASE_ID(ase), BT_ASCS_STOP_OP, err,
				      BT_ASCS_REASON_NONE);
		return;
	}

	ascs_cp_rsp_success(ASE_ID(ase), BT_ASCS_STOP_OP);
}

static ssize_t ascs_stop(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_start_op *req;
	int i;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases) {
		BT_ERR("Malformed ASE Start: len %u < %zu", buf->len,
		       req->num_ases);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		struct bt_ascs_ase *ase;
		uint8_t id;

		id = net_buf_simple_pull_u8(buf);

		BT_DBG("ase 0x%02x", id);

		ase = ase_find(ascs, id);
		if (!ase) {
			ascs_cp_rsp_add(id, BT_ASCS_STOP_OP,
					BT_ASCS_RSP_INVALID_ASE, 0x00);
			BT_ERR("Unable to find ASE");
			continue;
		}

		ase_stop(ase);
	}

	return buf->size;
}

static ssize_t ascs_metadata(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_metadata_op *req;
	struct bt_ascs_metadata *meta;
	int i;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases * sizeof(*meta)) {
		BT_ERR("Malformed ASE Metadata: len %u < %zu", buf->len,
		       req->num_ases * sizeof(*meta));
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		struct bt_ascs_ase *ase;

		meta = net_buf_simple_pull_mem(buf, sizeof(*meta));

		if (buf->len < meta->len) {
			BT_ERR("Malformed ASE Metadata: len %u < %zu", buf->len,
			       meta->len);
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		}

		BT_DBG("ase 0x%02x meta->len %u", meta->ase, meta->len);

		ase = ase_find(ascs, meta->ase);
		if (!ase) {
			ascs_cp_rsp_add(meta->ase, BT_ASCS_METADATA_OP,
					BT_ASCS_RSP_INVALID_ASE, 0x00);
			BT_ERR("Unable to find ASE");
			continue;
		}

		ase_metadata(ase, BT_ASCS_METADATA_OP, meta, buf);
	}

	return buf->size;
}

static ssize_t ascs_release(struct bt_ascs *ascs, struct net_buf_simple *buf)
{
	const struct bt_ascs_release_op *req;
	int i;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("num_ases %u", req->num_ases);

	if (buf->len < req->num_ases) {
		BT_ERR("Malformed ASE Release: len %u < %zu", buf->len,
		       req->num_ases);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	for (i = 0; i < req->num_ases; i++) {
		uint8_t id;
		struct bt_ascs_ase *ase;

		id = net_buf_simple_pull_u8(buf);

		BT_DBG("ase 0x%02x", id);

		ase = ase_find(ascs, id);
		if (!ase) {
			ascs_cp_rsp_add(id, BT_ASCS_RELEASE_OP,
					BT_ASCS_RSP_INVALID_ASE, 0);
			BT_ERR("Unable to find ASE");
			continue;
		}

		ase_release(ase, false);
	}

	return buf->size;
}

static ssize_t ascs_cp_write(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr, const void *data,
			     uint16_t len, uint16_t offset, uint8_t flags)
{
	struct bt_ascs *ascs;
	const struct bt_ascs_ase_cp *req;
	struct net_buf_simple buf;
	ssize_t ret;

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len < sizeof(*req)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	net_buf_simple_init_with_data(&buf, (void *) data, len);

	req = net_buf_simple_pull_mem(&buf, sizeof(*req));

	BT_DBG("conn %p attr %p buf %p len %u offset %u op %s (0x%02x)", conn,
	       attr, data, len, offset, bt_ascs_op_str(req->op), req->op);

	/* Reset/empty response buffer before using it again */
	net_buf_simple_reset(&rsp_buf);

	ascs = ascs_get(conn);
	if (!ascs) {
		BT_ERR("Unable to get ASCS session");
		len = BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
		ascs_cp_rsp_add(0, req->op, BT_ASCS_RSP_UNSPECIFIED, 0x00);
		goto respond;
	}

	switch (req->op) {
	case BT_ASCS_CONFIG_OP:
		ret = ascs_config(ascs, &buf);
		break;
	case BT_ASCS_QOS_OP:
		ret = ascs_qos(ascs, &buf);
		break;
	case BT_ASCS_ENABLE_OP:
		ret = ascs_enable(ascs, &buf);
		break;
	case BT_ASCS_START_OP:
		ret = ascs_start(ascs, &buf);
		break;
	case BT_ASCS_DISABLE_OP:
		ret = ascs_disable(ascs, &buf);
		break;
	case BT_ASCS_STOP_OP:
		ret = ascs_stop(ascs, &buf);
		break;
	case BT_ASCS_METADATA_OP:
		ret = ascs_metadata(ascs, &buf);
		break;
	case BT_ASCS_RELEASE_OP:
		ret = ascs_release(ascs, &buf);
		break;
	default:
		ascs_cp_rsp_add(0x00, req->op, BT_ASCS_RSP_NOT_SUPPORTED, 0);
		BT_DBG("Unknown opcode");
		len = BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}

	if (ret == BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN)) {
		ascs_cp_rsp_add(0, req->op, BT_ASCS_RSP_TRUNCATED,
				BT_ASCS_REASON_NONE);
	}

respond:
	ascs_cp_notify(ascs);

	return len;
}

BT_GATT_SERVICE_DEFINE(ascs_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_ASCS),
	BT_GATT_CHARACTERISTIC(BT_UUID_ASCS_ASE,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT,
			       ascs_ase_read, NULL, UINT_TO_POINTER(1)),
	BT_GATT_CCC(ascs_ase_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
#if CONFIG_BT_ASCS_ASE_COUNT > 1
	BT_GATT_CHARACTERISTIC(BT_UUID_ASCS_ASE,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT,
			       ascs_ase_read, NULL, UINT_TO_POINTER(2)),
	BT_GATT_CCC(ascs_ase_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
#endif
#if CONFIG_BT_ASCS_ASE_COUNT > 2
	BT_GATT_CHARACTERISTIC(BT_UUID_ASCS_ASE,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT,
			       ascs_ase_read, NULL, UINT_TO_POINTER(3)),
	BT_GATT_CCC(ascs_ase_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
#endif
	BT_GATT_CHARACTERISTIC(BT_UUID_ASCS_ASE_CP,
			       BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP |
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE_ENCRYPT,
			       NULL, ascs_cp_write, NULL),
	BT_GATT_CCC(ascs_cp_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
);
