/* @file
 * @brief Object Transfer internal header file
 *
 * For use with the Object Transfer Service (OTS)
 *
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_BLUETOOTH_HOST_AUDIO_OTS_INTERNAL_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_HOST_AUDIO_OTS_INTERNAL_H_

#include <stdbool.h>
#include <zephyr/types.h>
#include <bluetooth/gatt.h>
#include "uint48_util.h"
#include "ots.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTS_FEATURE_LEN                (2 * sizeof(uint32_t))
#define OTS_SIZE_LEN                   (2 * sizeof(uint32_t))
#define OTS_PROPERTIES_LEN             (sizeof(uint32_t))
#define OTS_TYPE_MAX_LEN               (sizeof(struct bt_ots_obj_type))

#define OTS_MAX_OBJECT_META_SIZE       170

/** PSM for Object Transfer Service. */
#define OTS_L2CAP_PSM                  0x0025
/**Start of the usable range of Object IDs (values 0 to 0xFF are reserved)*/
#define OTS_ID_RANGE_START             0x000000000100
/** Type UUID of the directory listing object. */
#define OTS_DIR_LISTING_TYPE           0x2acb
/** ID of the directory listing object. */
#define OTS_DIR_LISTING_ID             0x000000000000
/** Name of the directory listing object. */
#define OTS_DIR_LISTING_NAME           "Directory_listing"

/** Maximum size of the directory listing object. */
#define BT_OTS_DIR_LISTING_MAX_SIZE \
	(OTS_MAX_OBJECT_META_SIZE * BT_OTS_MAX_OBJ_CNT)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_HOST_AUDIO_OTS_INTERNAL_H_ */
