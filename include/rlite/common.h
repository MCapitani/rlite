/*
 * Common definitions for the rlite stack.
 *
 * Copyright (C) 2015-2016 Nextworks
 * Author: Vincenzo Maffione <v.maffione@gmail.com>
 *
 * This file is part of rlite.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __RLITE_COMMON_H__
#define __RLITE_COMMON_H__

/*
 * When compiling from userspace include <stdint.h>,
 * when compiling from kernelspace include <linux/types.h>
 */
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdio.h>
#include <stdint.h>
#include <asm/ioctl.h>
#endif

#include "rina/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RLITE_CTRLDEV_NAME "/dev/rlite"
#define RLITE_IODEV_NAME "/dev/rlite-io"
#define RLITE_UIPCPS_VAR "/run/rlite/"
#define RLITE_UIPCPS_UNIX_NAME RLITE_UIPCPS_VAR "uipcps-server"
#define RLITE_UIPCPS_PIDFILE RLITE_UIPCPS_VAR "uipcps.pid"

/* Application naming information:
 *   - Application Process Name
 *   - Application Process Instance
 *   - Application Entity Name
 *   - Application Entity Instance
 */
struct rina_name {
    char *apn;
    char *api;
    char *aen;
    char *aei;
} __attribute__((packed));

typedef uint16_t rl_port_t;
typedef uint16_t rl_ipcp_id_t;
typedef uint16_t rl_msg_t;

#define RL_IPCP_ID_NONE (~((rl_ipcp_id_t)0))
#define RL_PORT_ID_NONE (~((rl_port_t)0))
#define RL_ADDR_NULL (0U)

/* Maximum sizes for data transfer constants, to be used in CDAP
 * messages, user/kernel interfaces and the management layer in general. */
typedef uint64_t rlm_addr_t;
typedef uint64_t rlm_seq_t;
typedef uint32_t rlm_pdulen_t;
typedef uint32_t rlm_cepid_t;
typedef uint32_t rlm_qosid_t;

/* Data transfer constants used by a normal IPCP. */
struct pci_sizes {
    uint16_t addr;
    uint16_t seq;
    uint16_t pdulen;
    uint16_t cepid;
    uint16_t qosid;
};

/* Actual values for data transfer constants: can be redefined at
 * compile time, set default values here. */
#ifndef rl_addr_t
#define rl_addr_t uint32_t
#endif
#ifndef rl_seq_t
#define rl_seq_t uint32_t
#endif
#ifndef rl_pdulen_t
#define rl_pdulen_t uint16_t
#endif
#ifndef rl_cepid_t
#define rl_cepid_t uint16_t
#endif
#ifndef rl_qosid_t
#define rl_qosid_t uint16_t
#endif

#define RLITE_SUCC 0
#define RLITE_ERR 1

/* Expected control API version. */
#define RL_API_VERSION 2

/* All the possible messages begin like this. */
struct rl_msg_hdr {
    uint16_t version;
    rl_msg_t msg_type;
    uint32_t event_id;
} __attribute__((packed));

/* Base message, when no arguments are needed. */
struct rl_msg_base {
    struct rl_msg_hdr hdr;
} __attribute__((packed));

/* A simple response message layout that can be shared by many
 * different types. */
struct rl_msg_base_resp {
    struct rl_msg_hdr hdr;

    uint8_t result;
} __attribute__((packed));

/* Some useful macros for casting. */
#define RLITE_MB(m) (struct rl_msg_base *)(m)
#define RLITE_MBR(m) (struct rl_msg_base_resp *)(m)

/*
 * Flags valid for the control device. They can be
 * changed using an ioctl(fd, RLITE_IOCTL_CHFLAGS, flags).
 */

/* The control device wants to be notified about creation, removal or
 * update of IPCPs. */
#define RL_F_IPCPS (1 << 0)
#define RL_F_ALL RL_F_IPCPS

/* Bind the flow identified by port_id to
 * this rl_io device. */
#define RLITE_IO_MODE_APPL_BIND 86
/* Use this device to write/read management
 * PDUs for the IPCP specified by ipcp_id. */
#define RLITE_IO_MODE_IPCP_MGMT 88

struct rl_ioctl_info {
    uint8_t mode;
    rl_port_t port_id;
    rl_ipcp_id_t ipcp_id;
} __attribute__((packed));

#define RLITE_IOCTL_FLOW_BIND _IOW(0xAF, 0x00, struct rl_ioctl_info)
#define RLITE_IOCTL_CHFLAGS _IOW(0xAF, 0x01, uint64_t)
#define RLITE_IOCTL_MSS_GET _IOW(0xAF, 0x02, uint32_t *)

#define RLITE_MGMT_HDR_T_OUT_LOCAL_PORT 1
#define RLITE_MGMT_HDR_T_OUT_DST_ADDR 2
#define RLITE_MGMT_HDR_T_IN 3

/* Header used across user/kernel boundary when writing/reading
 * management SDUs from rlite-io devices working in RLITE_IO_MODE_IPCP_MGMT
 * mode.
 * Userspace can write a management SDU specifying either a local
 * port (type OUT_LOCAL_PORT) or a destination address (OUT_DST_ADDR). In
 * the former case 'local_port' should refer to an existing N-1 flow
 * ('remote_addr' is ignored), while in the latter 'remote_addr' should
 * refer to an N-IPCP that will be reached as specified by the PDUFT
 * ('local_port' is ignored).
 * When reading a management SDU, the header will contain the local port
 * where the SDU was received and the source (remote) address that sent it.
 */
struct rl_mgmt_hdr {
    rl_port_t local_port;
    rlm_addr_t remote_addr;
    uint8_t type;
} __attribute__((packed));

#define DTCP_PRESENT(_dc) ((_dc).flags != 0)

struct dtcp_config {
    uint32_t flags;
#define DTCP_CFG_FLOW_CTRL (1 << 0)
#define DTCP_CFG_RTX_CTRL (1 << 1)
#define DTCP_CFG_SHAPER (1 << 2)

    /* Flow control. */
    struct {
        uint8_t fc_type;
#define RLITE_FC_T_NONE 0
#define RLITE_FC_T_WIN 1
#define RLITE_FC_T_RATE 2
        union {
            struct {
                uint64_t sender_rate;
                uint64_t time_period; /* us */
            } r;
            struct {
                rlm_seq_t max_cwq_len; /* closed window queue */
                rlm_seq_t initial_credit;
            } w;
        } cfg;
    } fc;

    /* Retransmission control. */
    struct {
        uint32_t max_time_to_retry; /* R div initial_rtx_timeout */
        uint16_t data_rxms_max;
        uint16_t max_rtxq_len;
        uint32_t initial_rtx_timeout;
    } rtx;

    uint32_t initial_a; /* A */
    uint32_t bandwidth; /* in bps */
} __attribute__((packed));

struct rl_flow_config {
    /* Used by normal IPCP. */
    uint8_t msg_boundaries;
    uint8_t in_order_delivery;
    rlm_seq_t max_sdu_gap;
    struct dtcp_config dtcp;

    /* Currently used by shim-tcp4 and shim-udp4. */
    int32_t fd;
    uint32_t inet_ip;
    uint16_t inet_port;
} __attribute__((packed));

#define RL_MPL_MSECS_DFLT 1000
#define RL_DATA_RXMS_MAX_DFLT 10
#define RL_TTL_DFLT 64 /* default TTL */

/* Does a flow specification correspond to best effort QoS? */
static inline int
rina_flow_spec_best_effort(struct rina_flow_spec *spec)
{
    return spec->max_sdu_gap == ((rlm_seq_t)-1) && !spec->avg_bandwidth &&
           spec->max_loss == RINA_FLOW_SPEC_LOSS_MAX && !spec->max_delay &&
           !spec->max_jitter && !spec->in_order_delivery;
}

struct rl_flow_stats {
    /* EFCP sender statistics: packets and bytes successfully
     * processed for transmission, and packets dropped. */
    uint64_t tx_pkt;
    uint64_t tx_byte;
    uint64_t tx_err;

    /* EFCP receiver statistics: packets and bytes successfully
     * processed for transmission, and packets dropped. */
    uint64_t rx_pkt;
    uint64_t rx_byte;
    uint64_t rx_err;

    /* EFCP retransmission statistics. */
    uint64_t rtx_pkt;
    uint64_t rtx_byte;
};

static inline void
rl_flow_stats_init(struct rl_flow_stats *stats)
{
    stats->tx_pkt = stats->tx_byte = stats->tx_err = 0;
    stats->rx_pkt = stats->rx_byte = stats->rx_err = 0;
    stats->rtx_pkt = stats->rtx_byte = 0;
}

/* DTP state exported to userspace. */
struct rl_flow_dtp {
    /* Sender state. */
    rlm_seq_t snd_lwe;
    rlm_seq_t snd_rwe;
    rlm_seq_t next_seq_num_to_use;
    rlm_seq_t last_seq_num_sent;
    rlm_seq_t last_ctrl_seq_num_rcvd;
    unsigned int cwq_len;
    unsigned int max_cwq_len;
    unsigned int rtxq_len;
    unsigned int max_rtxq_len;
    unsigned rtt;        /* estimated round trip time, in usecs. */
    unsigned rtt_stddev; /* stddev in usecs */
    unsigned cgwin;      /* congestion window size, in PDUs */

    /* Receiver state. */
    rlm_seq_t rcv_lwe;
    rlm_seq_t rcv_next_seq_num;
    rlm_seq_t rcv_rwe;
    rlm_seq_t max_seq_num_rcvd;
    rlm_seq_t last_lwe_sent;
    rlm_seq_t last_seq_num_acked;
    rlm_seq_t next_snd_ctl_seq;
    unsigned int seqq_len;
};

#define RL_SHIM_UDP_PORT 0x0d1f

#define NPD(FMT, ...)

#define RL_VERB_QUIET 1
#define RL_VERB_WARN 2
#define RL_VERB_INFO 3
#define RL_VERB_DBG 4
#define RL_VERB_VERY 5

/* Memtrack machinery, enabled in debug mode. */
#ifdef RL_DEBUG
#define RL_MEMTRACK
#endif

#ifdef __cplusplus
}
#endif

#endif /* __RLITE_COMMON_H__ */
