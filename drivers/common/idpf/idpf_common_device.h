/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Intel Corporation
 */

#ifndef _IDPF_COMMON_DEVICE_H_
#define _IDPF_COMMON_DEVICE_H_

#include <rte_mbuf_ptype.h>
#include <base/idpf_prototype.h>
#include <base/virtchnl2.h>
#include <idpf_common_logs.h>

#define IDPF_RSS_KEY_LEN	52

#define IDPF_CTLQ_ID		-1
#define IDPF_CTLQ_LEN		64
#define IDPF_DFLT_MBX_BUF_SIZE	4096

#define IDPF_DEFAULT_RXQ_NUM	16
#define IDPF_RX_BUFQ_PER_GRP	2
#define IDPF_RXQ_PER_GRP	1
#define IDPF_DEFAULT_TXQ_NUM	16
#define IDPF_TX_COMPLQ_PER_GRP	1
#define IDPF_TXQ_PER_GRP	1

#define IDPF_MIN_FRAME_SIZE	14

#define IDPF_MAX_PKT_TYPE	1024

#define IDPF_DFLT_INTERVAL	16

#define IDPF_GET_PTYPE_SIZE(p)						\
	(sizeof(struct virtchnl2_ptype) +				\
	 (((p)->proto_id_count ? ((p)->proto_id_count - 1) : 0) * sizeof((p)->proto_id[0])))

struct idpf_adapter {
	struct idpf_hw hw;
	struct virtchnl2_version_info virtchnl_version;
	struct virtchnl2_get_capabilities caps;
	volatile uint32_t pend_cmd; /* pending command not finished */
	uint32_t cmd_retval; /* return value of the cmd response from cp */
	uint8_t *mbx_resp; /* buffer to store the mailbox response from cp */

	uint32_t ptype_tbl[IDPF_MAX_PKT_TYPE] __rte_cache_min_aligned;

	uint32_t txq_model; /* 0 - split queue model, non-0 - single queue model */
	uint32_t rxq_model; /* 0 - split queue model, non-0 - single queue model */

	/* For timestamp */
	uint64_t time_hw;
};

struct idpf_chunks_info {
	uint32_t tx_start_qid;
	uint32_t rx_start_qid;
	/* Valid only if split queue model */
	uint32_t tx_compl_start_qid;
	uint32_t rx_buf_start_qid;

	uint64_t tx_qtail_start;
	uint32_t tx_qtail_spacing;
	uint64_t rx_qtail_start;
	uint32_t rx_qtail_spacing;
	uint64_t tx_compl_qtail_start;
	uint32_t tx_compl_qtail_spacing;
	uint64_t rx_buf_qtail_start;
	uint32_t rx_buf_qtail_spacing;
};

struct idpf_vport {
	struct idpf_adapter *adapter; /* Backreference to associated adapter */
	union {
		struct virtchnl2_create_vport info; /* virtchnl response info handling */
		uint8_t data[IDPF_DFLT_MBX_BUF_SIZE];
	} vport_info;
	uint16_t sw_idx; /* SW index in adapter->vports[]*/
	uint16_t vport_id;
	uint32_t txq_model;
	uint32_t rxq_model;
	uint16_t num_tx_q;
	/* valid only if txq_model is split Q */
	uint16_t num_tx_complq;
	uint16_t num_rx_q;
	/* valid only if rxq_model is split Q */
	uint16_t num_rx_bufq;

	uint16_t max_mtu;
	uint8_t default_mac_addr[VIRTCHNL_ETH_LENGTH_OF_ADDRESS];

	enum virtchnl_rss_algorithm rss_algorithm;
	uint16_t rss_key_size;
	uint16_t rss_lut_size;

	void *dev_data; /* Pointer to the device data */
	uint16_t max_pkt_len; /* Maximum packet length */

	/* RSS info */
	uint32_t *rss_lut;
	uint8_t *rss_key;
	uint64_t rss_hf;
	uint64_t last_general_rss_hf;

	/* MSIX info*/
	struct virtchnl2_queue_vector *qv_map; /* queue vector mapping */
	uint16_t max_vectors;
	struct virtchnl2_alloc_vectors *recv_vectors;

	/* Chunk info */
	struct idpf_chunks_info chunks_info;

	uint16_t devarg_id;

	bool stopped;

	bool rx_vec_allowed;
	bool tx_vec_allowed;
	bool rx_use_avx512;
	bool tx_use_avx512;

	struct virtchnl2_vport_stats eth_stats_offset;

	void *dev;
	/* Event from ipf */
	bool link_up;
	uint32_t link_speed;
};

/* Message type read in virtual channel from PF */
enum idpf_vc_result {
	IDPF_MSG_ERR = -1, /* Meet error when accessing admin queue */
	IDPF_MSG_NON,      /* Read nothing from admin queue */
	IDPF_MSG_SYS,      /* Read system msg from admin queue */
	IDPF_MSG_CMD,      /* Read async command result */
};

/* structure used for sending and checking response of virtchnl ops */
struct idpf_cmd_info {
	uint32_t ops;
	uint8_t *in_args;       /* buffer for sending */
	uint32_t in_args_size;  /* buffer size for sending */
	uint8_t *out_buffer;    /* buffer for response */
	uint32_t out_size;      /* buffer size for response */
};

/* notify current command done. Only call in case execute
 * _atomic_set_cmd successfully.
 */
static inline void
notify_cmd(struct idpf_adapter *adapter, int msg_ret)
{
	adapter->cmd_retval = msg_ret;
	/* Return value may be checked in anither thread, need to ensure the coherence. */
	rte_wmb();
	adapter->pend_cmd = VIRTCHNL2_OP_UNKNOWN;
}

/* clear current command. Only call in case execute
 * _atomic_set_cmd successfully.
 */
static inline void
clear_cmd(struct idpf_adapter *adapter)
{
	/* Return value may be checked in anither thread, need to ensure the coherence. */
	rte_wmb();
	adapter->pend_cmd = VIRTCHNL2_OP_UNKNOWN;
	adapter->cmd_retval = VIRTCHNL_STATUS_SUCCESS;
}

/* Check there is pending cmd in execution. If none, set new command. */
static inline bool
atomic_set_cmd(struct idpf_adapter *adapter, uint32_t ops)
{
	uint32_t op_unk = VIRTCHNL2_OP_UNKNOWN;
	bool ret = __atomic_compare_exchange(&adapter->pend_cmd, &op_unk, &ops,
					    0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);

	if (!ret)
		DRV_LOG(ERR, "There is incomplete cmd %d", adapter->pend_cmd);

	return !ret;
}

__rte_internal
int idpf_adapter_init(struct idpf_adapter *adapter);
__rte_internal
int idpf_adapter_deinit(struct idpf_adapter *adapter);
__rte_internal
int idpf_vport_init(struct idpf_vport *vport,
		    struct virtchnl2_create_vport *vport_req_info,
		    void *dev_data);
__rte_internal
int idpf_vport_deinit(struct idpf_vport *vport);
__rte_internal
int idpf_vport_rss_config(struct idpf_vport *vport);
__rte_internal
int idpf_vport_irq_map_config(struct idpf_vport *vport, uint16_t nb_rx_queues);
__rte_internal
int idpf_vport_irq_unmap_config(struct idpf_vport *vport, uint16_t nb_rx_queues);
__rte_internal
int idpf_vport_info_init(struct idpf_vport *vport,
			 struct virtchnl2_create_vport *vport_info);
__rte_internal
void idpf_vport_stats_update(struct virtchnl2_vport_stats *oes, struct virtchnl2_vport_stats *nes);

#endif /* _IDPF_COMMON_DEVICE_H_ */
