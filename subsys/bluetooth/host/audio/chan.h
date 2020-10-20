/* @file
 * @brief Internal APIs for Audio Channel handling

 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** Life-span states of ASE. Used only by internal APIs
 *  dealing with setting ASE to proper state depending on operational
 *  context.
 */
enum bt_audio_chan_state {
	/** Audio Stream Endpoint idle state */
	BT_AUDIO_CHAN_IDLE,
	/** Audio Stream Endpoint configured state */
	BT_AUDIO_CHAN_CONFIGURED,
	/** Audio Stream Endpoint streaming state */
	BT_AUDIO_CHAN_STREAMING,
};

#if defined(CONFIG_BT_AUDIO_DEBUG_CHAN)
void bt_audio_chan_set_state_debug(struct bt_audio_chan *chan, uint8_t state,
				   const char *func, int line);
#define bt_audio_chan_set_state(_chan, _state) \
	bt_audio_chan_set_state_debug(_chan, _state, __func__, __LINE__)
#else
void bt_audio_chan_set_state(struct bt_audio_chan *chan, uint8_t state);
#endif /* CONFIG_BT_AUDIO_DEBUG_CHAN */

/* Bind ISO channel */
struct bt_conn_iso *bt_audio_chan_bind(struct bt_audio_chan *chan,
				       struct bt_codec_qos *qos);

/* Connect ISO channel */
int bt_audio_chan_connect(struct bt_audio_chan *chan);

/* Disconnect ISO channel */
int bt_audio_chan_disconnect(struct bt_audio_chan *chan);

void bt_audio_chan_reset(struct bt_audio_chan *chan);
