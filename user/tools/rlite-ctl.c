#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <assert.h>

#include "../helpers.h"
#include "rlite/list.h"
#include "rlite/conf-msg.h"
#include "rlite/rlite.h"
#include "rlite/conf.h"


static int
uipcps_connect(void)
{
    struct sockaddr_un server_address;
    int ret;
    int sfd;

    /* Open a Unix domain socket towards the uipcps. */
    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("socket(AF_UNIX)");
        return -1;
    }
    memset(&server_address, 0, sizeof(server_address));
    server_address.sun_family = AF_UNIX;
    strncpy(server_address.sun_path, RLITE_UIPCPS_UNIX_NAME,
            sizeof(server_address.sun_path) - 1);
    ret = connect(sfd, (struct sockaddr *)&server_address,
                    sizeof(server_address));
    if (ret) {
        perror("connect(AF_UNIX, path)");
        PI("Warning: maybe uipcps are not running?\n");
        return -1;
    }

    return sfd;
}

static int
uipcps_disconnect(int sfd)
{
    return close(sfd);
}

typedef int (*response_handler_t )(struct rlite_msg_base_resp *);

static int
read_response(int sfd, response_handler_t handler)
{
    struct rlite_msg_base_resp *resp;
    char msgbuf[4096];
    char serbuf[4096];
    int ret;
    int n;

    n = read(sfd, serbuf, sizeof(serbuf));
    if (n < 0) {
        PE("read() error [%d]\n", n);
        return -1;
    }

    ret = deserialize_rlite_msg(rlite_conf_numtables, RLITE_CFG_MSG_MAX,
                               serbuf, n, msgbuf, sizeof(msgbuf));
    if (ret) {
        PE("error while deserializing response [%d]\n",
                ret);
        return -1;
    }

    resp = RLITE_MBR(msgbuf);
    ret = (resp->result) == 0 ? 0 : -1;

    PI("uipcps response [type=%u] --> %d\n", resp->msg_type, ret);

    if (!ret && handler) {
        ret = handler(resp);
    }

    return ret;
}

static int
request_response(struct rlite_msg_base *req, response_handler_t handler)
{
    int fd;
    int ret;

    fd = uipcps_connect();
    if (fd < 0) {
        return fd;
    }

    ret = rlite_msg_write_fd(fd, req);
    if (ret) {
        return ret;
    }

    ret = read_response(fd, handler);
    if (ret) {
        return ret;
    }

    return uipcps_disconnect(fd);
}

static int
ipcp_create(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    const char *ipcp_apn;
    const char *ipcp_api;
    struct rina_name ipcp_name;
    const char *dif_type;
    const char *dif_name;
    long int ipcp_id;
    int ret;

    assert(argc >= 4);
    ipcp_apn = argv[0];
    ipcp_api = argv[1];
    dif_type = argv[2];
    dif_name = argv[3];

    rina_name_fill(&ipcp_name, ipcp_apn, ipcp_api, NULL, NULL);

    ipcp_id = rl_conf_ipcp_create(ctrl, &ipcp_name, dif_type, dif_name);

    if (ipcp_id >= 0) {
        PI("IPCP of type '%s' created, assigned id %u\n", dif_type,
           (unsigned int)ipcp_id);
    }

    if (type_has_uipcp(dif_type)) {
        ret = rl_conf_ipcp_uipcp_wait(ctrl, (unsigned int)ipcp_id);
        if (ret) {
            PE("Cannot wait for uIPCP %u\n", (unsigned int)ipcp_id);
            rl_conf_ipcp_destroy(ctrl, (unsigned int)ipcp_id);

        } else {
            PI("uIPCP %u showed up\n", (unsigned int)ipcp_id);
        }
    }

    return ipcp_id < 0 ? -1 : 0;
}

static int
ipcp_destroy(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    const char *ipcp_apn;
    const char *ipcp_api;
    struct rina_name ipcp_name;
    struct rl_ipcp *rl_ipcp;
    int ret = -1;

    assert(argc >= 2);
    ipcp_apn = argv[0];
    ipcp_api = argv[1];

    rina_name_fill(&ipcp_name, ipcp_apn, ipcp_api, NULL, NULL);

    /* Does the request specifies an existing IPC process ? */
    rl_ipcp = rl_ctrl_lookup_ipcp_by_name(ctrl, &ipcp_name);
    if (!rl_ipcp) {
        PE("No such IPCP process\n");
        return -1;

    }

    /* Valid IPCP id. Forward the request to the kernel. */
    ret = rl_conf_ipcp_destroy(ctrl, rl_ipcp->id);
    if (!ret) {
        PI("IPCP %u destroyed\n", rl_ipcp->id);
    }

    return ret;
}

static int
ipcp_config(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    const char *ipcp_apn;
    const char *ipcp_api;
    const char *param_name;
    const char *param_value;
    struct rina_name ipcp_name;
    struct rl_ipcp *rl_ipcp;
    int ret = -1;  /* Report failure by default. */

    assert(argc >= 4);
    ipcp_apn = argv[0];
    ipcp_api = argv[1];
    param_name = argv[2];
    param_value = argv[3];

    rina_name_fill(&ipcp_name, ipcp_apn, ipcp_api, NULL, NULL);

    /* The request specifies an IPCP: lookup that. */
    rl_ipcp = rl_ctrl_lookup_ipcp_by_name(ctrl, &ipcp_name);
    if (!rl_ipcp) {
        PE("Could not find a suitable IPC process\n");
    } else {
        /* Forward the request to the kernel. */
        ret = rl_conf_ipcp_config(ctrl, rl_ipcp->id, param_name, param_value);
        if (!ret) {
            PI("IPCP %u configured correctly: %s <== %s\n", rl_ipcp->id,
               param_name, param_value);
        }
    }

    return ret;
}

static int
ipcp_register_common(int argc, char **argv, unsigned int reg,
                     struct rlite_ctrl *ctrl)
{
    struct rl_cmsg_ipcp_register req;
    const char *ipcp_apn;
    const char *ipcp_api;
    const char *dif_name;
    struct rl_ipcp *rl_ipcp;

    assert(argc >= 3);
    dif_name = argv[0];
    ipcp_apn = argv[1];
    ipcp_api = argv[2];

    rina_name_fill(&req.ipcp_name, ipcp_apn, ipcp_api, NULL, NULL);
    /* Lookup the id of the registering IPCP. */
    rl_ipcp = rl_ctrl_lookup_ipcp_by_name(ctrl, &req.ipcp_name);
    if (!rl_ipcp) {
        PE("Could not find the IPC process to register\n");
        return -1;
    }

    req.msg_type = RLITE_CFG_IPCP_REGISTER;
    req.event_id = 0;
    req.dif_name = strdup(dif_name);
    req.reg = reg;

    return request_response(RLITE_MB(&req), NULL);
}

static int
ipcp_register(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    return ipcp_register_common(argc, argv, 1, ctrl);
}

static int
ipcp_unregister(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    return ipcp_register_common(argc, argv, 0, ctrl);
}

static int
ipcp_enroll(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    struct rl_cmsg_ipcp_enroll req;
    const char *ipcp_apn;
    const char *ipcp_api;
    const char *neigh_ipcp_apn;
    const char *neigh_ipcp_api;
    const char *dif_name;
    const char *supp_dif_name;
    struct rl_ipcp *rl_ipcp;

    assert(argc >= 6);
    dif_name = argv[0];
    ipcp_apn = argv[1];
    ipcp_api = argv[2];
    neigh_ipcp_apn = argv[3];
    neigh_ipcp_api = argv[4];
    supp_dif_name = argv[5];

    rina_name_fill(&req.ipcp_name, ipcp_apn, ipcp_api, NULL, NULL);
    rl_ipcp = rl_ctrl_lookup_ipcp_by_name(ctrl, &req.ipcp_name);
    if (!rl_ipcp) {
        PE("Could not find enrolling IPC process\n");
        return -1;
    }

    req.msg_type = RLITE_CFG_IPCP_ENROLL;
    req.event_id = 0;
    req.dif_name = strdup(dif_name);
    rina_name_fill(&req.neigh_name, neigh_ipcp_apn, neigh_ipcp_api, NULL, NULL);
    req.supp_dif_name = strdup(supp_dif_name);

    return request_response(RLITE_MB(&req), NULL);
}

static int
ipcp_dft_set(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    struct rl_cmsg_ipcp_dft_set req;
    const char *ipcp_apn;
    const char *ipcp_api;
    const char *appl_apn;
    const char *appl_api;
    unsigned long remote_addr;
    struct rl_ipcp *rl_ipcp;

    assert(argc >= 5);
    ipcp_apn = argv[0];
    ipcp_api = argv[1];
    appl_apn = argv[2];
    appl_api = argv[3];
    errno = 0;
    remote_addr = strtoul(argv[4], NULL, 10);
    if (errno) {
        PE("Invalid address %s\n", argv[4]);
        return -1;
    }

    rina_name_fill(&req.ipcp_name, ipcp_apn, ipcp_api, NULL, NULL);
    rl_ipcp = rl_ctrl_lookup_ipcp_by_name(ctrl, &req.ipcp_name);
    if (!rl_ipcp) {
        PE("Could not find IPC process\n");
        return -1;
    }

    req.msg_type = RLITE_CFG_IPCP_DFT_SET;
    req.event_id = 0;
    rina_name_fill(&req.appl_name, appl_apn, appl_api, NULL, NULL);
    req.remote_addr = remote_addr;

    return request_response(RLITE_MB(&req), NULL);
}

static int
ipcps_show(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    rl_ctrl_ipcps_print(ctrl);

    return 0;
}

static int
flows_show(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    struct list_head flows;

    list_init(&flows);
    rl_conf_flows_fetch(ctrl, &flows);
    rl_conf_flows_print(ctrl, &flows);
    rl_conf_flows_purge(&flows);

    return 0;
}

static int
ipcp_rib_show_handler(struct rlite_msg_base_resp *b_resp)
{
    struct rl_cmsg_ipcp_rib_show_resp *resp =
        (struct rl_cmsg_ipcp_rib_show_resp *)b_resp;

    if (resp->dump) {
        printf("%s\n", resp->dump);
    }

    return 0;
}

static int
ipcp_rib_show(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    struct rl_cmsg_ipcp_rib_show_req req;
    const char *ipcp_apn;
    const char *ipcp_api;
    struct rl_ipcp *rl_ipcp;

    assert(argc >= 2);
    ipcp_apn = argv[0];
    ipcp_api = argv[1];

    rina_name_fill(&req.ipcp_name, ipcp_apn, ipcp_api, NULL, NULL);
    rl_ipcp = rl_ctrl_lookup_ipcp_by_name(ctrl, &req.ipcp_name);
    if (!rl_ipcp) {
        PE("Could not find IPC process\n");
        return -1;
    }

    req.msg_type = RLITE_CFG_IPCP_RIB_SHOW_REQ;
    req.event_id = 0;

    return request_response(RLITE_MB(&req),
                            ipcp_rib_show_handler);
}

static int
test(struct rlite_ctrl *ctrl)
{
    struct rina_name name;
    long int lret;
    int ret;

    /* Create an IPC process of type shim-loopback. */
    rina_name_fill(&name, "test-shim-loopback.IPCP", "1", NULL, NULL);
    lret = rl_conf_ipcp_create(ctrl, &name, "shim-loopback",
                                "test-shim-loopback.DIF");
    assert(lret >= 0);
    rina_name_free(&name);

    rina_name_fill(&name, "test-shim-loopback.IPCP", "2", NULL, NULL);
    lret = rl_conf_ipcp_create(ctrl, &name, "shim-loopback",
                              "test-shim-loopback.DIF");
    assert(lret >= 0);

    lret = rl_conf_ipcp_create(ctrl, &name, "shim-loopback",
                              "test-shim-loopback.DIF");
    assert(lret < 0);
    rina_name_free(&name);

    /* Destroy the IPCPs. */
    ret = rl_conf_ipcp_destroy(ctrl, 0);
    assert(!ret);
    ret = rl_conf_ipcp_destroy(ctrl, 1);
    assert(!ret);
    ret = rl_conf_ipcp_destroy(ctrl, 0);
    assert(ret);

    return 0;
}

struct cmd_descriptor {
    const char *name;
    const char *usage;
    unsigned int num_args;
    int (*func)(int argc, char **argv, struct rlite_ctrl *ctrl);
};

static struct cmd_descriptor cmd_descriptors[] = {
    {
        .name = "ipcp-create",
        .usage = "IPCP_APN IPCP_API DIF_TYPE DIF_NAME",
        .num_args = 4,
        .func = ipcp_create,
    },
    {
        .name = "ipcp-destroy",
        .usage = "IPCP_APN IPCP_API",
        .num_args = 2,
        .func = ipcp_destroy,
    },
    {
        .name = "ipcp-config",
        .usage = "IPCP_APN IPCP_API PARAM_NAME PARAM_VALUE",
        .num_args = 4,
        .func = ipcp_config,
    },
    {
        .name = "ipcp-register",
        .usage = "DIF_NAME IPCP_APN IPCP_API",
        .num_args = 3,
        .func = ipcp_register,
    },
    {
        .name = "ipcp-unregister",
        .usage = "DIF_NAME IPCP_APN IPCP_API",
        .num_args = 3,
        .func = ipcp_unregister,
    },
    {
        .name = "ipcp-enroll",
        .usage = "DIF_NAME IPCP_APN IPCP_API NEIGH_IPCP_APN NEIGH_IPCP_API SUPP_DIF_NAME",
        .num_args = 6,
        .func = ipcp_enroll,
    },
    {
        .name = "ipcp-dft-set",
        .usage = "IPCP_APN IPCP_API APPL_APN APPL_API REMOTE_ADDR",
        .num_args = 5,
        .func = ipcp_dft_set,
    },
    {
        .name = "ipcps-show",
        .usage = "",
        .num_args = 0,
        .func = ipcps_show,
    },
    {
        .name = "ipcp-rib-show",
        .usage = "IPCP_APN IPCP_API",
        .num_args = 2,
        .func = ipcp_rib_show,
    },
    {
        .name = "flows-show",
        .usage = "",
        .num_args = 0,
        .func = flows_show,
    },
};

#define NUM_COMMANDS    (sizeof(cmd_descriptors)/sizeof(struct cmd_descriptor))

static void
usage(int i)
{
    if (i >= 0 && i < NUM_COMMANDS) {
        printf("    %s %s\n", cmd_descriptors[i].name, cmd_descriptors[i].usage);
        return;
    }

    printf("\nAvailable commands:\n");

    for (i = 0; i < NUM_COMMANDS; i++) {
        printf("    %s %s\n", cmd_descriptors[i].name, cmd_descriptors[i].usage);
    }
}

static int
process_args(int argc, char **argv, struct rlite_ctrl *ctrl)
{
    const char *cmd;
    int i;

    if (argc < 2) {
        /* No command, assume ipcps-show. */
        cmd = "ipcps-show";

    } else {
        cmd = argv[1];
    }

    for (i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(cmd, cmd_descriptors[i].name) == 0) {
            assert(cmd_descriptors[i].func);

            if (argc - 2 < cmd_descriptors[i].num_args) {
                /* Not enough arguments. */
                PE("Not enough arguments\n");
                usage(i);
                return -1;
            }

            return cmd_descriptors[i].func(argc - 2, argv + 2, ctrl);
        }
    }

    PE("Unknown command '%s'\n", cmd);
    usage(-1);

    return -1;
}

static void
sigint_handler(int signum)
{
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    struct rlite_ctrl ctrl;
    struct sigaction sa;
    int enable_testing = 0;
    int ret;

    ret = rl_ctrl_init(&ctrl, NULL);
    if (ret) {
        return ret;
    }

    /* Set an handler for SIGINT and SIGTERM so that we can remove
     * the Unix domain socket used to access the uipcp server. */
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ret = sigaction(SIGINT, &sa, NULL);
    if (ret) {
        perror("sigaction(SIGINT)");
        exit(EXIT_FAILURE);
    }
    ret = sigaction(SIGTERM, &sa, NULL);
    if (ret) {
        perror("sigaction(SIGTERM)");
        exit(EXIT_FAILURE);
    }

    if (enable_testing) {
        /* Run the hardwired test script. */
        test(&ctrl);
    }

    ret = process_args(argc, argv, &ctrl);

    rl_ctrl_fini(&ctrl);

    return ret;
}