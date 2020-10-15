/*  Bluetooth csis - Telephone Bearer Service */

/*
 * Copyright (c) 2019 Bose Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <zephyr/types.h>

#include <device.h>
#include <init.h>
#include <stdlib.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include "csis.h"
#include "csip.h"
#include "sih.h"

#define BT_CSIS_SIH_PRAND_SIZE          3
#define BT_CSIS_SIH_HASH_SIZE           3
#define CSIS_SET_LOCK_TIMER_VALUE       K_SECONDS(60)
#if defined(CONFIG_BT_PRIVACY)
/* The ADV time (in tens of milliseconds). Shall be less than the RPA.
 * Make it relatively smaller (90%) to handle all ranges. Maxmimum value is
 * 2^16 - 1 (UINT16_MAX).
 */
#define CSIS_ADV_TIME  (MIN((CONFIG_BT_RPA_TIMEOUT * 100 * 0.9), UINT16_MAX))
#else
/* Without privacy, connectable adv won't update the address when restarting,
 * so we might as well continue advertising non-stop.
 */
#define CSIS_ADV_TIME  0
#endif /* CONFIG_BT_PRIVACY */

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_CSIS)
#define LOG_MODULE_NAME bt_csis
#include "common/log.h"

#if defined(CONFIG_BT_RPA) && !defined(CONFIG_BT_BONDABLE)
#define SIRK_READ_PERM	(BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_READ_ENCRYPT)
#else
#define SIRK_READ_PERM	(BT_GATT_PERM_READ_ENCRYPT)
#endif

/* 16-byte key used to generate SIRKs.
 * This key has to be the same for all devices in the set.
 */
static uint8_t set_sirk_key_gen_key[16] = {
	0x92, 0x5f, 0xcb, 0xcb, 0x8a, 0xa8, 0x96, 0xe9,
	0x3e, 0x62, 0x01, 0x54, 0xf9, 0xad, 0xef, 0x54
};

static struct bt_csis_cb_t *csis_cbs;
struct csis_pending_notifications_t {
	bt_addr_le_t addr;
	bool pending;
	bool active;

/* Since there's a 1-to-1 connection between bonded devices, and devices in
 * the array containing this struct, if the security manager overwrites
 * the oldest keys, we also overwrite the oldest entry
 */
#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
	uint32_t age;
#endif /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
};

struct csis_instance_t {
	uint8_t set_sirk[BT_CSIP_SET_SIRK_SIZE];
	uint8_t psri[BT_CSIS_PSRI_SIZE];
	uint8_t set_size;
	uint8_t set_lock;
	uint8_t rank;
	bool pending_notification;
	struct k_delayed_work set_lock_timer;
	bt_addr_le_t lock_client_addr;
	const struct bt_gatt_service_static *service_p;
	struct csis_pending_notifications_t pend_notify[CONFIG_BT_MAX_PAIRED];
#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
	uint32_t age_counter;
#endif /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
#if defined(CONFIG_BT_EXT_ADV)
	struct bt_le_ext_adv *adv;
	struct bt_le_ext_adv_cb adv_cb;
#endif /* CONFIG_BT_EXT_ADV */
};

static struct csis_instance_t csis_inst;
extern const struct bt_gatt_service_static csis_svc;
static bt_addr_le_t server_dummy_addr; /* 0'ed address */

struct csis_cond_check_t {
	const bt_addr_le_t *addr;
	bool bonded;
};

static void check_bond(const struct bt_bond_info *info, void *data)
{
	struct csis_cond_check_t *bond_check = (struct csis_cond_check_t *)data;

	if (!bond_check->bonded) {
		bond_check->bonded = !bt_addr_le_cmp(bond_check->addr,
						     &info->addr);
	}
}

static bool is_bonded(struct bt_conn *conn)
{
	struct csis_cond_check_t bond_check = {
		.addr = bt_conn_get_dst(conn),
		.bonded = false
	};

	bt_foreach_bond(BT_ID_DEFAULT, check_bond, &bond_check);
	return bond_check.bonded;
}

static bool is_last_client_to_write(struct bt_conn *conn)
{
	if (conn) {
		return !bt_addr_le_cmp(bt_conn_get_dst(conn),
				       &csis_inst.lock_client_addr);
	} else {
		return !bt_addr_le_cmp(&server_dummy_addr,
				       &csis_inst.lock_client_addr);
	}
}

static void notify_lock_value(struct bt_conn *conn)
{
	bt_gatt_notify_uuid(conn, BT_UUID_CSIS_SET_LOCK, csis_svc.attrs,
			    &csis_inst.set_lock, sizeof(csis_inst.set_lock));
}

static void notify_client(struct bt_conn *conn, void *data)
{
	bt_addr_le_t *addr;
	struct bt_conn *excluded_conn = (struct bt_conn *)data;

	if (excluded_conn && conn == excluded_conn) {
		return;
	}

	notify_lock_value(conn);

	for (int i = 0; i < ARRAY_SIZE(csis_inst.pend_notify); i++) {
		addr = &csis_inst.pend_notify[i].addr;
		if (csis_inst.pend_notify[i].pending &&
		    !bt_addr_le_cmp(bt_conn_get_dst(conn), addr)) {
			csis_inst.pend_notify[i].pending = false;
			break;
		}
	}
}

static void notify_clients(struct bt_conn *excluded_client)
{
	bt_addr_le_t *addr;

	/* Mark all bonded devices as pending notifications, and clear those
	 * that are notified in `notify_client`
	 */
	for (int i = 0; i < ARRAY_SIZE(csis_inst.pend_notify); i++) {
		if (csis_inst.pend_notify[i].active) {
			addr = &csis_inst.pend_notify[i].addr;
			if (excluded_client &&
			    !bt_addr_le_cmp(bt_conn_get_dst(excluded_client),
					    addr)) {
				continue;
			}

			csis_inst.pend_notify[i].pending = true;
		}
	}
	bt_conn_foreach(BT_CONN_TYPE_ALL, notify_client, excluded_client);
}

static int generate_sirk(uint32_t seed, uint8_t sirk_dest[16])
{
	int err;

	/* r' = padding || r */
	memcpy(sirk_dest, &seed, sizeof(seed));
	memset(sirk_dest + sizeof(seed), 0, 16 - sizeof(seed));

	err = bt_encrypt_le(set_sirk_key_gen_key, sirk_dest, sirk_dest);
	if (err) {
		return err;
	}

	return 0;
}

static int generate_prand(uint32_t *dest)
{
	int res;
	bool valid = false;

	do {
		*dest = 0;
		res = bt_rand(dest, BT_CSIS_SIH_PRAND_SIZE);
		if (res) {
			return res;
		}

		/* Validate Prand: Must contain both a 1 and a 0 */
		if (*dest != 0 && *dest != 0x3FFFFF) {
			valid = true;
		}
	} while (!valid);

	*dest &= 0x3FFFFF;
	*dest |= BIT(22); /* bit 23 shall be 0, and bit 22 shall be 1 */
	return 0;
}

static ssize_t read_set_sirk(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	BT_HEXDUMP_DBG(&csis_inst.set_sirk, sizeof(csis_inst.set_sirk),
			"Set SIRK");
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 csis_inst.set_sirk,
				 sizeof(csis_inst.set_sirk));
}

static void set_sirk_cfg_changed(const struct bt_gatt_attr *attr,
				 uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t read_set_size(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	BT_DBG("%u", csis_inst.set_size);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &csis_inst.set_size,
				 sizeof(csis_inst.set_size));
}

static void set_size_cfg_changed(const struct bt_gatt_attr *attr,
				 uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t read_set_lock(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	BT_DBG("%u", csis_inst.set_lock);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &csis_inst.set_lock,
				 sizeof(csis_inst.set_lock));
}

static ssize_t write_set_lock(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len,
			      uint16_t offset, uint8_t flags)
{
	uint8_t val;
	bool notify;

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	} else if (len != sizeof(val)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(&val, buf, len);

	if (val != BT_CSIP_RELEASE_VALUE && val != BT_CSIP_LOCK_VALUE) {
		return BT_GATT_ERR(BT_CSIP_ERROR_LOCK_INVAL_VALUE);
	}

	if (csis_inst.set_lock == BT_CSIP_LOCK_VALUE) {
		if (val == BT_CSIP_LOCK_VALUE) {
			return BT_GATT_ERR(BT_CSIP_ERROR_LOCK_DENIED);
		} else if (!is_last_client_to_write(conn)) {
			return BT_GATT_ERR(BT_CSIP_ERROR_LOCK_RELEASE_DENIED);
		}
	}

	notify = csis_inst.set_lock != val;

	csis_inst.set_lock = val;
	if (csis_inst.set_lock == BT_CSIP_LOCK_VALUE) {
		if (conn) {
			bt_addr_le_copy(&csis_inst.lock_client_addr,
					bt_conn_get_dst(conn));
		}
		k_delayed_work_submit(&csis_inst.set_lock_timer,
				      CSIS_SET_LOCK_TIMER_VALUE);
	} else {
		memset(&csis_inst.lock_client_addr, 0,
		       sizeof(csis_inst.lock_client_addr));
		k_delayed_work_cancel(&csis_inst.set_lock_timer);
	}

	BT_DBG("%u", csis_inst.set_lock);

	if (notify) {
		/*
		 * The Spec states that all clients, except for the
		 * client writing the value, shall be notified
		 * (if subscribed)
		 */
		notify_clients(conn);

		if (csis_cbs && csis_cbs->locked) {
			bool locked = csis_inst.set_lock == BT_CSIP_LOCK_VALUE;

			csis_cbs->locked(conn, locked);
		}
	}
	return len;
}

static void set_lock_cfg_changed(const struct bt_gatt_attr *attr,
				 uint16_t value)
{
	BT_DBG("value 0x%04x", value);
}

static ssize_t read_rank(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	BT_DBG("%u", csis_inst.rank);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &csis_inst.rank, sizeof(csis_inst.rank));

}

static void set_lock_timer_handler(struct k_work *work)
{
	BT_DBG("Lock timeout, releasing");
	csis_inst.set_lock = BT_CSIP_RELEASE_VALUE;
	notify_clients(NULL);

	if (csis_cbs && csis_cbs->locked) {
		bool locked = csis_inst.set_lock == BT_CSIP_LOCK_VALUE;

		csis_cbs->locked(NULL, locked);
	}
}

static void csis_security_changed(struct bt_conn *conn, bt_security_t level,
				  enum bt_security_err err)
{
	bt_addr_le_t *addr;

	if (!is_bonded(conn)) {
		return;
	}

	for (int i = 0; i < ARRAY_SIZE(csis_inst.pend_notify); i++) {
		addr = &csis_inst.pend_notify[i].addr;
		if (csis_inst.pend_notify[i].pending &&
		    !bt_addr_le_cmp(bt_conn_get_dst(conn), addr)) {
			notify_lock_value(conn);
			csis_inst.pend_notify[i].pending = false;
			break;
		}
	}
}

static void csis_disconnected(struct bt_conn *conn, uint8_t reason)
{
	bt_addr_le_t *addr;

	BT_DBG("Disconnected: %s (reason %u)",
	       bt_addr_le_str(bt_conn_get_dst(conn)), reason);

	/*
	 * If lock was taken by non-bonded device, set lock to released value,
	 * and notify other connections.
	 */
	if (is_bonded(conn)) {
		return;
	}

	BT_DBG("Non-bonded device");
	if (is_last_client_to_write(conn)) {
		memset(&csis_inst.lock_client_addr, 0,
		sizeof(csis_inst.lock_client_addr));
		csis_inst.set_lock = BT_CSIP_RELEASE_VALUE;
		notify_clients(NULL);

		if (csis_cbs && csis_cbs->locked) {
			bool locked = csis_inst.set_lock == BT_CSIP_LOCK_VALUE;

			csis_cbs->locked(conn, locked);
		}
	}

	/* Check if the disconnected device once was bonded and stored
	 * here as a bonded device
	 */
	for (int i = 0; i < ARRAY_SIZE(csis_inst.pend_notify); i++) {
		addr = &csis_inst.pend_notify[i].addr;
		if (!bt_addr_le_cmp(bt_conn_get_dst(conn), addr)) {
			memset(&csis_inst.pend_notify[i], 0,
			       sizeof(csis_inst.pend_notify[i]));
			break;
		}
	}
}

static void auth_pairing_complete(struct bt_conn *conn, bool bonded)
{
	/**
	 * If a pairing is complete for a bonded device, then we
	 * 1) Check if the device is already in the `pend_notify`, and if it is
	 * not, then we
	 * 2) Check if there's room for another device in the `pend_notify`
	 *    array. If there are no more room for a new device, then
	 * 3) Either we ignore this new device (bad luck), or we overwrite
	 *    the oldest entry, following the behavior of the key storage.
	 */
	bt_addr_le_t *addr;

	if (!bonded) {
		return;
	}

	/* Check if already in list, and do nothing if it is */
	for (int i = 0; i < ARRAY_SIZE(csis_inst.pend_notify); i++) {
		addr = &csis_inst.pend_notify[i].addr;
		if (csis_inst.pend_notify[i].active &&
			!bt_addr_le_cmp(bt_conn_get_dst(conn), addr)) {
#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
			csis_inst.pend_notify[i].age = csis_inst.age_counter++;
#endif /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
			return;
		}
	}

	/* Copy addr to list over devices to save notifications for */
	for (int i = 0; i < ARRAY_SIZE(csis_inst.pend_notify); i++) {
		addr = &csis_inst.pend_notify[i].addr;
		if (!csis_inst.pend_notify[i].active) {
			bt_addr_le_copy(addr, bt_conn_get_dst(conn));
			csis_inst.pend_notify[i].active = true;
#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
			csis_inst.pend_notify[i].age = csis_inst.age_counter++;
#endif /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
			return;
		}
	}

#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
	struct csis_pending_notifications_t *oldest = &csis_inst.pend_notify[0];

	for (int i = 1; i < ARRAY_SIZE(csis_inst.pend_notify); i++) {
		if (csis_inst.pend_notify[i].age < oldest->age) {
			oldest = &csis_inst.pend_notify[i];
		}
	}
	memset(oldest, 0, sizeof(*oldest));
	bt_addr_le_copy(&oldest->addr, &conn->le.dst);
	oldest->active = true;
	oldest->age = csis_inst.age_counter++;
#else
	BT_WARN("Could not add device to pending notification list");
#endif /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
}

static struct bt_conn_cb conn_callbacks = {
	.disconnected = csis_disconnected,
	.security_changed = csis_security_changed,
};

static const struct bt_conn_auth_cb auth_callbacks = {
	.pairing_complete = auth_pairing_complete
};

#if defined(CONFIG_BT_EXT_ADV)
static void adv_timeout(struct bt_le_ext_adv *adv,
			struct bt_le_ext_adv_sent_info *info)
{
	int err;

	__ASSERT(adv == csis_inst.adv, "Wrong adv set");

	err = bt_csis_advertise(true);

	if (err) {
		BT_ERR("Could not restart advertising: %d", err);
	}
}
#endif /* CONFIG_BT_EXT_ADV */

#define BT_CSIS_SERVICE_DEFINITION \
	BT_GATT_PRIMARY_SERVICE(BT_UUID_CSIS), \
	BT_GATT_CHARACTERISTIC(BT_UUID_CSIS_SET_SIRK, \
			BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
			SIRK_READ_PERM, \
			read_set_sirk, NULL, NULL), \
	BT_GATT_CCC(set_sirk_cfg_changed, \
			BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT), \
	BT_GATT_CHARACTERISTIC(BT_UUID_CSIS_SET_SIZE, \
			BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, \
			BT_GATT_PERM_READ_ENCRYPT, \
			read_set_size, NULL, NULL), \
	BT_GATT_CCC(set_size_cfg_changed, \
			BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT), \
	BT_GATT_CHARACTERISTIC(BT_UUID_CSIS_SET_LOCK, \
			BT_GATT_CHRC_READ | \
				BT_GATT_CHRC_NOTIFY | \
				BT_GATT_CHRC_WRITE, \
			BT_GATT_PERM_READ_ENCRYPT | \
				BT_GATT_PERM_WRITE_ENCRYPT, \
			read_set_lock, write_set_lock, NULL), \
	BT_GATT_CCC(set_lock_cfg_changed, \
			BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT), \
	BT_GATT_CHARACTERISTIC(BT_UUID_CSIS_RANK, \
			BT_GATT_CHRC_READ, \
			BT_GATT_PERM_READ_ENCRYPT, \
			read_rank, NULL, NULL) \


BT_GATT_SERVICE_DEFINE(csis_svc, BT_CSIS_SERVICE_DEFINITION);

static int bt_csis_init(const struct device *unused)
{
	/* TODO: Consider deferring init to when it's actually needed (lazy) */
	int res;

	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&auth_callbacks);

	k_delayed_work_init(&csis_inst.set_lock_timer,
			    set_lock_timer_handler);
	csis_inst.service_p = &csis_svc;
	csis_inst.rank = CONFIG_BT_CSIS_SET_RANK;
	csis_inst.set_size = CONFIG_BT_CSIS_SET_SIZE;
	csis_inst.set_lock = BT_CSIP_RELEASE_VALUE;
	res = generate_sirk(CONFIG_BT_CSIS_SET_SIRK_SEED,
			    csis_inst.set_sirk);
	if (res) {
		BT_DBG("Sirk generation failed for instance");
	}

#if defined(CONFIG_BT_EXT_ADV)
	csis_inst.adv_cb.sent = adv_timeout;
#endif /* CONFIG_BT_EXT_ADV */

	return res;
}

DEVICE_INIT(bt_csis, "bt_csis", &bt_csis_init, NULL, NULL,
	    APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

static int csis_update_psri(void)
{
	int res = 0;
	uint32_t prand;
	uint32_t hash;

#if IS_ENABLED(CONFIG_BT_CSIS_TEST_SIRK)
	prand = 0x69f563;
	static uint8_t test_sirk[] = {
		0xb8, 0x03, 0xea, 0xc6, 0xaf, 0xbb, 0x65, 0xa2,
		0x5a, 0x41, 0xf1, 0x53, 0x05, 0x68, 0x8e, 0x83
	};
	memcpy(csis_inst.set_sirk, test_sirk, sizeof(test_sirk));
#else
	res = generate_prand(&prand);
	if (res) {
		BT_WARN("Could not generate new prand");
		return res;
	}
#endif
	res = sih(csis_inst.set_sirk, prand, &hash);
	if (res) {
		BT_WARN("Could not generate new PSRI");
		return res;
	}

	memcpy(csis_inst.psri, &hash, 3);
	memcpy(csis_inst.psri + 3, &prand, 3);
	return res;
}

/****************************** Public API ******************************/
void bt_csis_register_cb(struct bt_csis_cb_t *cb)
{
	csis_cbs = cb;
}

int bt_csis_advertise(bool enable)
{
	int err;

	if (enable) {
		struct bt_data ad[2] = {
			BT_DATA_BYTES(BT_DATA_FLAGS,
				      BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)
		};

		if (csis_update_psri() != 0) {
			return -EAGAIN;
		}

		ad[1].type = BT_CSIS_AD_TYPE;
		ad[1].data_len = sizeof(csis_inst.psri);
		ad[1].data = csis_inst.psri;

#if defined(CONFIG_BT_EXT_ADV)
		struct bt_le_ext_adv_start_param param;
		if (!csis_inst.adv) {
			struct bt_le_adv_param param;

			memset(&param, 0, sizeof(param));
			param.options |= BT_LE_ADV_OPT_CONNECTABLE;
			param.options |= BT_LE_ADV_OPT_SCANNABLE;
			param.options |= BT_LE_ADV_OPT_USE_NAME;

			param.id = BT_ID_DEFAULT;
			param.sid = 0;
			param.interval_min = BT_GAP_ADV_FAST_INT_MIN_2;
			param.interval_max = BT_GAP_ADV_FAST_INT_MAX_2;

			err = bt_le_ext_adv_create(&param, &csis_inst.adv_cb,
						   &csis_inst.adv);
			if (err) {
				BT_DBG("Could not create adv set: %d", err);
				return err;
			}
		}

		err = bt_le_ext_adv_set_data(csis_inst.adv, ad, ARRAY_SIZE(ad),
					     NULL, 0);

		if (err) {
			BT_DBG("Could not set adv data: %d", err);
			return err;
		}

		memset(&param, 0, sizeof(param));
		param.timeout = CSIS_ADV_TIME;
		err = bt_le_ext_adv_start(csis_inst.adv, &param);
#else
		err = bt_le_adv_start(BT_LE_ADV_CONN_NAME,
				      ad, ARRAY_SIZE(ad), NULL, 0);
#endif /* CONFIG_BT_EXT_ADV */

		if (err) {
			BT_DBG("Could not start adv: %d", err);
			return err;
		}
	} else {
#if defined(CONFIG_BT_EXT_ADV)
		err = bt_le_ext_adv_stop(csis_inst.adv);
#else
		err = bt_le_adv_stop();
#endif /* CONFIG_BT_EXT_ADV */

		if (err) {
			BT_DBG("Could not stop start adv: %d", err);
			return err;
		}
	}

	return err;
}

int bt_csis_lock(bool lock, bool force)
{
	uint8_t lock_val;
	int err = 0;

	if (lock) {
		lock_val = BT_CSIP_LOCK_VALUE;
	} else {
		lock_val = BT_CSIP_RELEASE_VALUE;
	}

	if (!lock && force) {
		csis_inst.set_lock = BT_CSIP_RELEASE_VALUE;
		notify_clients(NULL);

		if (csis_cbs && csis_cbs->locked) {
			csis_cbs->locked(NULL, false);
		}
	} else {
		err = write_set_lock(NULL, NULL, &lock_val,
				     sizeof(lock_val), 0, 0);
	}

	if (err < 0) {
		return err;
	} else {
		return 0;
	}
}

void bt_csis_print_sirk(void)
{
	BT_HEXDUMP_DBG(&csis_inst.set_sirk, sizeof(csis_inst.set_sirk),
			"Set SIRK");
}
