#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#include "rlite-list.h"
#include "uipcp-container.h"


struct inet4_endpoint {
    int fd;
    struct sockaddr_in addr;
    char *appl_name_s;

    struct list_head node;
};

struct shim_inet4 {
    struct list_head endpoints;
};

#define SHIM(_u)    ((struct shim_inet4 *)((_u)->priv))

static int
parse_directory(int addr2sock, struct sockaddr_in *addr,
                struct rina_name *appl_name)
{
    char *appl_name_s = rina_name_to_string(appl_name);
    const char *dirfile = "/etc/rlite/shim-inet4-dir";
    FILE *fin;
    char *linebuf = NULL;
    size_t sz;
    size_t n;
    int found = 0;

    if (!appl_name_s) {
        PE("Out of memory\n");
        return -1;
    }

    fin = fopen(dirfile, "r");
    if (!fin) {
        PE("Could not open directory file '%s'\n", dirfile);
        return -1;
    }

    while ((n = getline(&linebuf, &sz, fin)) > 0) {
        /* I know, strtok_r, strsep, etc. etc. I just wanted to have
         * some fun ;) */
        char *nm = linebuf;
        char *ip, *port, *eol;
        struct sockaddr_in cur_addr;
        int ret;

        while (*nm != '\0' && isspace(*nm)) nm++;
        if (*nm == '\0') continue;

        ip = nm;
        while (*ip != '\0' && !isspace(*ip)) ip++;
        if (*ip == '\0') continue;

        *ip = '\0';
        ip++;
        while (*ip != '\0' && isspace(*ip)) ip++;
        if (*ip == '\0') continue;

        port = ip;
        while (*port != '\0' && !isspace(*port)) port++;
        if (*port == '\0') continue;

        *port = '\0';
        port++;
        while (*port != '\0' && isspace(*port)) port++;
        if (*port == '\0') continue;

        eol = port;
        while (*eol != '\0' && !isspace(*eol)) eol++;
        if (*eol != '\0') *eol = '\0';

        memset(&cur_addr, 0, sizeof(cur_addr));
        cur_addr.sin_family = AF_INET;
        cur_addr.sin_port = htons(atoi(port));
        ret = inet_pton(AF_INET, ip, &cur_addr.sin_addr);
        if (ret != 1) {
            PE("Invalid IP address '%s'\n", ip);
            continue;
        }

        if (addr2sock) {
            if (strcmp(nm, appl_name_s) == 0) {
                memcpy(addr, &cur_addr, sizeof(cur_addr));
                found = 1;
            }

        } else { /* sock2addr */
            if (addr->sin_family == cur_addr.sin_family &&
                    addr->sin_port == cur_addr.sin_port &&
                    memcmp(&addr->sin_addr, &cur_addr.sin_addr,
                    sizeof(cur_addr.sin_addr)) == 0) {
                ret = rina_name_from_string(nm, appl_name);
                if (ret) {
                    PE("Invalid name '%s'\n", nm);
                }
                found = (ret == 0);
            }
        }

        printf("oho '%s' '%s'[%d] '%d'\n", nm, ip, ret, atoi(port));
    }

    if (linebuf) {
        free(linebuf);
    }

    fclose(fin);

    return found ? 0 : -1;
}

static int
appl_name_to_sock_addr(const struct rina_name *appl_name,
                       struct sockaddr_in *addr)
{
    return parse_directory(1, addr, (struct rina_name *)appl_name);
}

static void
accept_conn(struct rlite_evloop *loop, int fd)
{
}

static int
shim_inet4_appl_unregister(struct uipcp *uipcp,
                           struct rina_kmsg_appl_register *req)
{
    char *appl_name_s = rina_name_to_string(&req->appl_name);
    struct shim_inet4 *shim = SHIM(uipcp);
    struct inet4_endpoint *ep;
    int ret = -1;

    if (!appl_name_s) {
        PE("Out of memory\n");
        return -1;
    }

    list_for_each_entry(ep, &shim->endpoints, node) {
        if (strcmp(appl_name_s, ep->appl_name_s) == 0) {
            rlite_evloop_fdcb_del(&uipcp->appl.loop, ep->fd);
            list_del(&ep->node);
            close(ep->fd);
            free(ep->appl_name_s);
            free(ep);
            ret = 0;

            break;
        }
    }

    if (ret) {
        PE("Could not find endpoint for appl_name %s\n", appl_name_s);
    }

    if (appl_name_s) {
        free(appl_name_s);
    }

    return ret;
}

static int
shim_inet4_appl_register(struct rlite_evloop *loop,
                         const struct rina_msg_base_resp *b_resp,
                         const struct rina_msg_base *b_req)
{
    struct rlite_appl *application = container_of(loop, struct rlite_appl,
                                                   loop);
    struct uipcp *uipcp = container_of(application, struct uipcp, appl);
    struct rina_kmsg_appl_register *req =
                (struct rina_kmsg_appl_register *)b_resp;
    struct shim_inet4 *shim = SHIM(uipcp);
    struct inet4_endpoint *ep;
    int ret;

    if (!req->reg) {
        /* Process the unregistration. */
        return shim_inet4_appl_unregister(uipcp, req);
    }

    /* Process the registration. */

    ep = malloc(sizeof(*ep));
    if (!ep) {
        PE("Out of memory\n");
        return -1;
    }

    ep->appl_name_s = rina_name_to_string(&req->appl_name);
    if (!ep->appl_name_s) {
        PE("Out of memory\n");
        goto err1;
    }

    ret = appl_name_to_sock_addr(&req->appl_name, &ep->addr);
    if (ret) {
        PE("Failed to get inet4 address from appl_name '%s'\n",
           ep->appl_name_s);
        goto err2;
    }

    ep->fd = socket(PF_INET, SOCK_STREAM, 0);
    if (ep->fd < 0) {
        PE("socket() failed [%d]\n", errno);
        goto err2;
    }

    {
        int enable = 1;

        if (setsockopt(ep->fd, SOL_SOCKET, SO_REUSEADDR, &enable,
                       sizeof(enable))) {
            PE("setsockopt(SO_REUSEADDR) failed [%d]\n", errno);
            goto err3;
        }
    }

    if (bind(ep->fd, (struct sockaddr *)&ep->addr, sizeof(ep->addr))) {
        PE("bind() failed [%d]\n", errno);
        goto err3;
    }

    if (listen(ep->fd, 5)) {
        PE("listen() failed [%d]\n", errno);
        goto err3;
    }

    rlite_evloop_fdcb_add(&uipcp->appl.loop, ep->fd, accept_conn);

    list_add_tail(&ep->node, &shim->endpoints);

    return 0;

err3:
    close(ep->fd);
err2:
    free(ep->appl_name_s);
err1:
    free(ep);

    return -1;
}

static int
shim_inet4_fa_req(struct rlite_evloop *loop,
                  const struct rina_msg_base_resp *b_resp,
                  const struct rina_msg_base *b_req)
{
    struct rlite_appl *application = container_of(loop, struct rlite_appl,
                                                   loop);
    struct uipcp *uipcp = container_of(application, struct uipcp, appl);
    struct rina_kmsg_fa_req *req = (struct rina_kmsg_fa_req *)b_resp;
    struct sockaddr_in local_addr, remote_addr;
    int ret;

    PD("[uipcp %u] Got reflected message\n", uipcp->ipcp_id);

    assert(b_req == NULL);

    ret = appl_name_to_sock_addr(&req->local_application, &local_addr);
    if (ret) {
        PE("Failed to get inet4 address for local application\n");
    }

    ret = appl_name_to_sock_addr(&req->remote_application, &remote_addr);
    if (ret) {
        PE("Failed to get inet4 address for remote application\n");
    }

    return 0;
}

static int
shim_inet4_fa_req_arrived(struct rlite_evloop *loop,
                      const struct rina_msg_base_resp *b_resp,
                      const struct rina_msg_base *b_req)
{
    struct rlite_appl *application = container_of(loop, struct rlite_appl,
                                                   loop);
    struct uipcp *uipcp = container_of(application, struct uipcp, appl);
    struct rina_kmsg_fa_req_arrived *req =
                    (struct rina_kmsg_fa_req_arrived *)b_resp;
    assert(b_req == NULL);

    PD("flow request arrived: [ipcp_id = %u, data_port_id = %u]\n",
            req->ipcp_id, req->port_id);

    (void)uipcp;

    return 0;
}

static int
shim_inet4_fa_resp(struct rlite_evloop *loop,
              const struct rina_msg_base_resp *b_resp,
              const struct rina_msg_base *b_req)
{
    struct rlite_appl *application = container_of(loop, struct rlite_appl,
                                                   loop);
    struct uipcp *uipcp = container_of(application, struct uipcp, appl);
    struct rina_kmsg_fa_resp *resp =
                (struct rina_kmsg_fa_resp *)b_resp;

    PD("[uipcp %u] Got reflected message\n", uipcp->ipcp_id);

    assert(b_req == NULL);

    (void)resp;

    return 0;
}

static int
shim_inet4_flow_deallocated(struct rlite_evloop *loop,
                       const struct rina_msg_base_resp *b_resp,
                       const struct rina_msg_base *b_req)
{
    struct rlite_appl *application = container_of(loop, struct rlite_appl,
                                                   loop);
    struct uipcp *uipcp = container_of(application, struct uipcp, appl);
    struct rina_kmsg_flow_deallocated *req =
                (struct rina_kmsg_flow_deallocated *)b_resp;

    (void)req;
    (void)uipcp;

    return 0;
}

static int
shim_inet4_init(struct uipcp *uipcp)
{
    struct shim_inet4 *shim = malloc(sizeof(*shim));

    if (!shim) {
        PE("Out of memory\n");
        return -1;
    }

    uipcp->priv = shim;

    list_init(&shim->endpoints);

    return 0;
}

static int
shim_inet4_fini(struct uipcp *uipcp)
{
    struct shim_inet4 *shim = SHIM(uipcp);
    struct list_head *elem;
    struct inet4_endpoint *ep;

    while ((elem = list_pop_front(&shim->endpoints))) {
        ep = container_of(elem, struct inet4_endpoint, node);
        close(ep->fd);
        free(ep->appl_name_s);
        free(ep);
    }

    return 0;
}

struct uipcp_ops shim_inet4_ops = {
    .init = shim_inet4_init,
    .fini = shim_inet4_fini,
    .appl_register = shim_inet4_appl_register,
    .fa_req = shim_inet4_fa_req,
    .fa_req_arrived = shim_inet4_fa_req_arrived,
    .fa_resp = shim_inet4_fa_resp,
    .flow_deallocated = shim_inet4_flow_deallocated,
};

