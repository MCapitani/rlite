#ifndef __RINA_UIPCP_H__
#define __RINA_UIPCP_H__

#include "rlite/conf-msg.h"
#include "rlite/kernel-msg.h"
#include "rlite/list.h"
#include "rlite/appl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User IPCP data model. */
struct uipcps {
    /* Unix domain socket file descriptor used to accept request from
     * applications. */
    int lfd;

    /* List of userspace IPCPs: There is one for each non-shim IPCP. */
    struct list_head uipcps;

    /* Each element of this list corresponds to the registration of
     * and IPCP within a DIF. This is useful to implement the persistent
     * IPCP registration feature, where the "persistence" is to be intended
     * across subsequent uipcps restarts. */
    struct list_head ipcps_registrations;

    struct list_head ipcp_nodes;
};

#define RINA_PERSISTENT_REG_FILE   "/var/rlite/uipcps-pers-reg"

struct uipcp;

struct uipcp_ops {
    int (*init)(struct uipcp *);

    int (*fini)(struct uipcp *);

    int (*register_to_lower)(struct uipcp *uipcp, int reg,
                             const struct rina_name *dif_name,
                             unsigned int ipcp_id,
                             const struct rina_name *ipcp_name);

    int (*enroll)(struct uipcp *, struct rina_cmsg_ipcp_enroll *);

    int (*dft_set)(struct uipcp *, struct rina_cmsg_ipcp_dft_set *);

    char * (*rib_show)(struct uipcp *);

    int (*appl_register)(struct rlite_evloop *loop,
                         const struct rina_msg_base_resp *b_resp,
                         const struct rina_msg_base *b_req);

    int (*fa_req)(struct rlite_evloop *loop,
                  const struct rina_msg_base_resp *b_resp,
                  const struct rina_msg_base *b_req);

    int (*fa_resp)(struct rlite_evloop *loop,
                   const struct rina_msg_base_resp *b_resp,
                   const struct rina_msg_base *b_req);

    int (*flow_deallocated)(struct rlite_evloop *loop,
                            const struct rina_msg_base_resp *b_resp,
                            const struct rina_msg_base *b_req);
};

struct ipcp_node {
    unsigned int ipcp_id;
    unsigned int marked;
    unsigned int depth;
    unsigned int refcnt;

    struct list_head lowers;
    struct list_head uppers;

    struct list_head node;
};

struct flow_edge {
    struct ipcp_node *ipcp;
    unsigned int refcnt;

    struct list_head node;
};

struct uipcp {
    struct rlite_appl appl;
    struct uipcps *uipcps;
    unsigned int ipcp_id;

    struct uipcp_ops ops;
    void *priv;

    struct list_head node;
};

void *uipcp_server(void *arg);

int uipcp_add(struct uipcps *uipcps, uint16_t ipcp_id, const char *dif_type);

int uipcp_del(struct uipcps *uipcps, uint16_t ipcp_id);

struct uipcp *uipcp_lookup(struct uipcps *uipcps, uint16_t ipcp_id);

int uipcps_fetch(struct uipcps *uipcps);

int uipcps_lower_flow_added(struct uipcps *uipcps, unsigned int upper,
                            unsigned int lower);

int uipcps_lower_flow_removed(struct uipcps *uipcps, unsigned int upper,
                              unsigned int lower);

int uipcp_appl_register_resp(struct uipcp *uipcp, uint16_t ipcp_id,
                             uint8_t response,
                             const struct rina_kmsg_appl_register *req);

int uipcp_pduft_set(struct uipcp *uipcs, uint16_t ipcp_id,
                    uint64_t dest_addr, uint32_t local_port);

int uipcp_pduft_flush(struct uipcp *uipcp, uint16_t ipcp_id);

int uipcp_issue_fa_req_arrived(struct uipcp *uipcp, uint32_t kevent_id,
                               uint32_t remote_port, uint32_t remote_cep,
                               uint64_t remote_addr,
                               const struct rina_name *local_appl,
                               const struct rina_name *remote_appl,
                               const struct rina_flow_config *flowcfg);

int uipcp_issue_fa_resp_arrived(struct uipcp *uipcp, uint32_t local_port,
                          uint32_t remote_port, uint32_t remote_cep,
                          uint64_t remote_addr, uint8_t response,
                          const struct rina_flow_config *flowcfg);

#define UPRINT(_u, LEV, FMT, ...)    \
    DOPRINT("[" LEV "][%u]%s: " FMT, (_u)->ipcp_id, __func__, ##__VA_ARGS__)

#define UPD(_u, FMT, ...)   UPRINT(_u, "DBG", FMT, ##__VA_ARGS__)
#define UPI(_u, FMT, ...)   UPRINT(_u, "INF", FMT, ##__VA_ARGS__)
#define UPE(_u, FMT, ...)   UPRINT(_u, "ERR", FMT, ##__VA_ARGS__)


#ifdef __cplusplus
}
#endif

#endif /* __RINA_UIPCP_H__ */
