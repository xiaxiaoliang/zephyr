/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_SERVICES_VCS_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_SERVICES_VCS_H_

/** @brief Volume Control Service (VCS)
 *
 *  @defgroup bt_gatt_vcs Volume Control Service (VCS)
 *
 *  @ingroup bluetooth
 *  @{
 *
 *  [Experimental] Users should note that the APIs can change
 *  as a part of ongoing development.
 */

#include <zephyr/types.h>
#include <bluetooth/services/aics.h>
#include <bluetooth/services/vocs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VCS Error codes */
#define VCS_ERR_INVALID_COUNTER             0x80
#define VCS_ERR_OP_NOT_SUPPORTED            0x81

/** @brief Initializing structure for Volume Control Service */
struct bt_vcs_init {
	/** Initializing structure for Volume Offset Control Services */
	struct bt_vocs_init vocs_init[CONFIG_BT_VCS_VOCS_INSTANCE_COUNT];

	/** Initializing structure for Audio Input Control Services */
	struct bt_aics_init aics_init[CONFIG_BT_VCS_AICS_INSTANCE_COUNT];
};

/** @brief Initialize the Volume Control Service
 *
 *  This will enable the service and make it discoverable by clients.
 *
 *  @param init  Pointer to a initialization structure. May be NULL to use
 *               default values.
 *
 *  @return 0 if success, ERRNO on failure.
 */
int bt_vcs_init(struct bt_vcs_init *init);

/** @brief Deactivates a Audio Input Control Service instance.
 *
 *  Audio Input Control Services are activated by default, but this will allow
 *  the server deactivate a Audio Input Control Service.
 *
 *  @param aics_index    The index of the Audio Input Control Service instance.
 *
 *  @return 0 if success, ERRNO on failure.
 */
int bt_vcs_aics_deactivate(uint8_t aics_index);

/** @brief Activates a Audio Input Control Service instance.
 *
 *  Audio Input Control Services are activated by default, but this will allow
 *  the server reactivate a Audio Input Control Service instance after it has
 *  been deactived with @ref bt_vcs_aics_deactivate.
 *
 *  @param aics_index    The index of the Audio Input Control Service instance.
 *
 *  @return 0 if success, ERRNO on failure.
 */
int bt_vcs_aics_activate(uint8_t aics_index);

/** @brief Callback function for bt_vcs_discover
 *
 *  @param conn          The connection that was used to discover VCS.
 *  @param err           Error value. 0 on success, GATT error or ERRNO on fail.
 *  @param vocs_count    Number of Volume Offset Control Service instances
 *                       on peer device.
 *  @param aics_count    Number of Audio Input Control Service instances on
 *                       peer device.
 */
typedef void (*bt_vcs_discover_cb_t)(
	struct bt_conn *conn, int err, uint8_t vocs_count, uint8_t aics_count);

/** @brief Callback function for VCS volume state.
 *
 *  Called when the value is read,
 *  or if the value is changed by either the server or client.
 *
 *  @param conn    The connection to the peer device.
 *  @param err     Error value. 0 on success, GATT error or ERRNO on fail.
 *                 For notifications, this will always be 0.
 *  @param volume  The volume of the VCS server.
 *  @param mute    The mute setting of the VCS server.
 */
typedef void (*bt_vcs_state_cb_t)(
	struct bt_conn *conn, int err, uint8_t volume, uint8_t mute);

/** @brief Callback function for VCS flags.
 *
 *  Called when the value is read,
 *  or if the value is changed by either the server or client.
 *
 *  @param conn    The connection to the peer device.
 *  @param err     Error value. 0 on success, GATT error or ERRNO on fail.
 *                 For notifications, this will always be 0.
 *  @param flags   The flags of the VCS server.
 */
typedef void (*bt_vcs_flags_cb_t)(
	struct bt_conn *conn, int err, uint8_t flags);

/** @brief Callback function for writes.
 *
 *  @param conn    The connection to the peer device.
 *  @param err     Error value. 0 on success, GATT error on fail.
 */
typedef void (*bt_vcs_write_cb_t)(
	struct bt_conn *conn, int err);

struct bt_vcs_cb_t {
	/* VCS */
	bt_vcs_state_cb_t               state;
	bt_vcs_flags_cb_t               flags;
#if defined(CONFIG_BT_VCS_CLIENT)
	bt_vcs_discover_cb_t            discover;
	bt_vcs_write_cb_t               vol_down;
	bt_vcs_write_cb_t               vol_up;
	bt_vcs_write_cb_t               mute;
	bt_vcs_write_cb_t               unmute;
	bt_vcs_write_cb_t               vol_down_unmute;
	bt_vcs_write_cb_t               vol_up_unmute;
	bt_vcs_write_cb_t               vol_set;
#endif /* CONFIG_BT_VCS_CLIENT */

	/* Volume Offset Control Service */
	struct bt_vocs_cb               vocs_cb;

	/* Audio Input Control Service */
	struct bt_aics_cb               aics_cb;
};

/** @brief Discover VCS and included services for a connection.
 *
 *  This will start a GATT discovery and setup handles and subscriptions.
 *  This shall be called once before any other actions can
 *  completed for the peer device.
 *
 *  @param conn          The connection to discover VCS for.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_discover(struct bt_conn *conn);


/** @brief Set the VCS volume step size.
 *
 *  Set the value that the volume changes, when changed relatively with e.g.
 *  @ref bt_vcs_volume_down or @ref bt_vcs_volume_up.
 *
 *  @param volume_step  The volume step size (1-255).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_volume_step_set(uint8_t volume_step);

/** @brief Read the VCS volume state.
 *
 *  @param conn   Connection to the peer device,
 *                or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_volume_get(struct bt_conn *conn);

/** @brief Read the VCS flags.
 *
 *  @param conn   Connection to peer device, or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_flags_get(struct bt_conn *conn);

/** @brief Turn the volume down by one step on the server.
 *
 *  @param conn   Connection to peer device, or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_volume_down(struct bt_conn *conn);

/** @brief Turn the volume up by one step on the server.
 *
 *  @param conn   Connection to peer device, or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_volume_up(struct bt_conn *conn);

/** @brief Turn the volume down and unmute the server.
 *
 *  @param conn   Connection to peer device, or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_unmute_volume_down(struct bt_conn *conn);

/** @brief Turn the volume up and unmute the server.
 *
 *  @param conn   Connection to peer device, or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_unmute_volume_up(struct bt_conn *conn);

/** @brief Set the volume on the server
 *
 *  @param conn   Connection to peer device, or NULL to set local server value.
 *  @param volume The absolute volume to set.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_volume_set(struct bt_conn *conn, uint8_t volume);

/** @brief Unmute the server.
 *
 *  @param conn   Connection to peer device, or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_unmute(struct bt_conn *conn);

/** @brief Mute the server.
 *
 *  @param conn   Connection to peer device, or NULL to read local server value.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_mute(struct bt_conn *conn);

/** @brief Read the Volume Offset Control Service offset state.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param vocs_index    The index of the Volume Offset Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_vocs_state_get(struct bt_conn *conn, uint8_t vocs_index);

/** @brief Read the Volume Offset Control Service location.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param vocs_index    The index of the Volume Offset Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_vocs_location_get(struct bt_conn *conn, uint8_t vocs_index);

/** @brief Set the Volume Offset Control Service location.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param vocs_index    The index of the Volume Offset Control Service
 *                       (as there may be multiple).
 *  @param location      The location to set.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_vocs_location_set(struct bt_conn *conn, uint8_t vocs_index,
			     uint8_t location);

/** @brief Set the Volume Offset Control Service offset state.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to set local server value.
 *  @param vocs_index    The index of the Volume Offset Control Service
 *                       (as there may be multiple).
 *  @param offset        The offset to set (-255 to 255).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_vocs_state_set(struct bt_conn *conn, uint8_t vocs_index,
			  int16_t offset);

/** @brief Read the Volume Offset Control Service output description.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param vocs_index    The index of the Volume Offset Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_vocs_description_get(struct bt_conn *conn, uint8_t vocs_index);

/** @brief Set the Volume Offset Control Service description.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to set local server value.
 *  @param vocs_index    The index of the Volume Offset Control Service
 *                       (as there may be multiple).
 *  @param description   The description to set. Value will be copied.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_vocs_description_set(struct bt_conn *conn, uint8_t vocs_index,
				const char *description);

/** @brief Read the Audio Input Control Service input state.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_state_get(struct bt_conn *conn, uint8_t aics_index);

/** @brief Read the Audio Input Control Service gain settings.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_gain_setting_get(struct bt_conn *conn, uint8_t aics_index);

/** @brief Read the Audio Input Control Service input type.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_type_get(struct bt_conn *conn, uint8_t aics_index);

/** @brief Read the Audio Input Control Service input status.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_status_get(struct bt_conn *conn, uint8_t aics_index);

/** @brief Mute the Audio Input Control Service input.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_unmute(struct bt_conn *conn, uint8_t aics_index);

/** @brief Unmute the Audio Input Control Service input.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_mute(struct bt_conn *conn, uint8_t aics_index);

/** @brief Set input gain to manual.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to set local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_manual_gain_set(struct bt_conn *conn, uint8_t aics_index);

/** @brief Set the input gain to automatic.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to set local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_automatic_gain_set(struct bt_conn *conn, uint8_t aics_index);

/** @brief Set the input gain.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to set local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *  @param gain          The gain in dB to set (-128 to 127).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_gain_set(struct bt_conn *conn, uint8_t aics_index, int8_t gain);

/** @brief Read the Audio Input Control Service description.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to read local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_description_get(struct bt_conn *conn, uint8_t aics_index);

/** @brief Set the Audio Input Control Service description.
 *
 *  @param conn          Connection to peer device,
 *                       or NULL to set local server value.
 *  @param aics_index    The index of the Audio Input Control Service
 *                       (as there may be multiple).
 *  @param description   The description to set.
 *
 *  @return 0 on success, GATT error value on fail.
 */
int bt_vcs_aics_description_set(struct bt_conn *conn, uint8_t aics_index,
				const char *description);

/** @brief Registers the callbacks used by the VCS server.
 *
 *  @param cb   The callback structure.
 */
void bt_vcs_server_cb_register(struct bt_vcs_cb_t *cb);

/** @brief Registers the callbacks used by the VCS client.
 *
 *  @param cb   The callback structure.
 */
void bt_vcs_client_cb_register(struct bt_vcs_cb_t *cb);

#ifdef __cplusplus
}
#endif

/**
 *  @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_SERVICES_VCS_H_ */
