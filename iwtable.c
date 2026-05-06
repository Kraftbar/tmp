#include <errno.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>

#define NLA_ALIGNTO 4
#define NLA_ALIGN(len) (((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_HDRLEN ((int) NLA_ALIGN(sizeof(struct nlattr)))
#define CMD_BUF_SIZE 8192
#define RX_BUF_SIZE 65536
#define SCAN_TIMEOUT_MS 15000

#define SEC_RSN 0x01
#define SEC_WPA 0x02
#define SEC_WPS 0x04

struct nl_req {
    struct nlmsghdr nlh;
    struct genlmsghdr genl;
    char buf[CMD_BUF_SIZE];
};

struct network {
    char bssid[32];
    char ssid[128];
    char security[32];
    char bw[16];
    char standard[16];
    int freq;
    int channel;
    uint32_t last_seen_ms;
    double signal_dbm;
    int has_signal;
    unsigned sec_flags;
    int privacy;
    int bw_rank;
    int std_rank;
    int legacy_ofdm;
};

struct network_list {
    struct network *items;
    size_t len;
    size_t cap;
};

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-i <interface>]\n", prog);
}

static void *nla_data(const struct nlattr *attr) {
    return (char *) attr + NLA_HDRLEN;
}

static int nla_payload_len(const struct nlattr *attr) {
    return attr->nla_len - NLA_HDRLEN;
}

static int nla_ok(const struct nlattr *attr, int remaining) {
    return remaining >= (int) sizeof(*attr) &&
        attr->nla_len >= sizeof(*attr) &&
        attr->nla_len <= remaining;
}

static struct nlattr *nla_next(const struct nlattr *attr, int *remaining) {
    int step = NLA_ALIGN(attr->nla_len);
    *remaining -= step;
    return (struct nlattr *) ((char *) attr + step);
}

static void nla_parse(struct nlattr **tb, int max, struct nlattr *attr, int len) {
    memset(tb, 0, (max + 1) * sizeof(*tb));

    while (nla_ok(attr, len)) {
        int type = attr->nla_type & NLA_TYPE_MASK;
        if (type <= max) {
            tb[type] = attr;
        }
        attr = nla_next(attr, &len);
    }
}

static uint16_t nla_get_u16(const struct nlattr *attr) {
    uint16_t value = 0;
    memcpy(&value, nla_data(attr), sizeof(value));
    return value;
}

static uint32_t nla_get_u32(const struct nlattr *attr) {
    uint32_t value = 0;
    memcpy(&value, nla_data(attr), sizeof(value));
    return value;
}

static int addattr(struct nl_req *req, size_t maxlen, uint16_t type, const void *data, size_t data_len) {
    size_t offset = NLMSG_ALIGN(req->nlh.nlmsg_len);
    size_t attr_len = NLA_HDRLEN + data_len;
    size_t total = offset + NLA_ALIGN(attr_len);
    struct nlattr *attr;

    if (total > maxlen) {
        errno = ENOBUFS;
        return -1;
    }

    attr = (struct nlattr *) ((char *) req + offset);
    attr->nla_type = type;
    attr->nla_len = attr_len;
    if (data_len > 0 && data != NULL) {
        memcpy(nla_data(attr), data, data_len);
    }

    req->nlh.nlmsg_len = total;
    return 0;
}

static int addattr_u32(struct nl_req *req, size_t maxlen, uint16_t type, uint32_t value) {
    return addattr(req, maxlen, type, &value, sizeof(value));
}

static struct nlattr *start_nested(struct nl_req *req, size_t maxlen, uint16_t type) {
    size_t offset = NLMSG_ALIGN(req->nlh.nlmsg_len);
    size_t total = offset + NLA_ALIGN(NLA_HDRLEN);
    struct nlattr *attr;

    if (total > maxlen) {
        errno = ENOBUFS;
        return NULL;
    }

    attr = (struct nlattr *) ((char *) req + offset);
    attr->nla_type = type | NLA_F_NESTED;
    attr->nla_len = NLA_HDRLEN;
    req->nlh.nlmsg_len = total;
    return attr;
}

static void end_nested(struct nl_req *req, struct nlattr *attr) {
    attr->nla_len = (char *) req + req->nlh.nlmsg_len - (char *) attr;
}

static void init_genl_req(struct nl_req *req, uint16_t family, uint16_t flags, uint32_t seq, uint8_t cmd) {
    memset(req, 0, sizeof(*req));
    req->nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct genlmsghdr));
    req->nlh.nlmsg_type = family;
    req->nlh.nlmsg_flags = flags;
    req->nlh.nlmsg_seq = seq;
    req->genl.cmd = cmd;
    req->genl.version = 0;
}

static int nl_open(void) {
    struct sockaddr_nl addr;
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int nl_send(int fd, struct nl_req *req) {
    struct sockaddr_nl addr;
    struct iovec iov = {
        .iov_base = req,
        .iov_len = req->nlh.nlmsg_len,
    };
    struct msghdr msg = {
        .msg_name = &addr,
        .msg_namelen = sizeof(addr),
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;

    if (sendmsg(fd, &msg, 0) < 0) {
        return -1;
    }

    return 0;
}

static int recv_netlink(int fd, char *buf, size_t len, int timeout_ms) {
    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    for (;;) {
        int poll_rc = poll(&pfd, 1, timeout_ms);
        if (poll_rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (poll_rc == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        break;
    }

    return recv(fd, buf, len, 0);
}

static int join_mcgroup(int fd, int group_id) {
    return setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group_id, sizeof(group_id));
}

static int find_first_wireless(char *dst, size_t dst_size) {
    struct if_nameindex *ifs = if_nameindex();
    int found = 0;

    if (ifs == NULL) {
        return -1;
    }

    for (struct if_nameindex *it = ifs; it->if_index != 0 || it->if_name != NULL; it++) {
        char path[256];

        if (it->if_name == NULL) {
            continue;
        }

        snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", it->if_name);
        if (access(path, F_OK) == 0) {
            snprintf(dst, dst_size, "%s", it->if_name);
            found = 1;
            break;
        }

        snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", it->if_name);
        if (access(path, F_OK) == 0) {
            snprintf(dst, dst_size, "%s", it->if_name);
            found = 1;
            break;
        }
    }

    if_freenameindex(ifs);

    if (!found) {
        errno = ENODEV;
        return -1;
    }

    return 0;
}

static int parse_family_reply(struct nlmsghdr *nlh, int *family_id, int *scan_group_id) {
    struct genlmsghdr *genl = NLMSG_DATA(nlh);
    int attr_len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*genl));
    struct nlattr *attrs[CTRL_ATTR_MAX + 1];

    if (genl->cmd != CTRL_CMD_NEWFAMILY) {
        errno = EPROTO;
        return -1;
    }

    nla_parse(attrs, CTRL_ATTR_MAX, (struct nlattr *) ((char *) genl + GENL_HDRLEN), attr_len);

    if (attrs[CTRL_ATTR_FAMILY_ID] == NULL) {
        errno = EPROTO;
        return -1;
    }

    *family_id = (int) nla_get_u16(attrs[CTRL_ATTR_FAMILY_ID]);

    if (attrs[CTRL_ATTR_MCAST_GROUPS] != NULL) {
        struct nlattr *grp;
        int rem = nla_payload_len(attrs[CTRL_ATTR_MCAST_GROUPS]);

        for (grp = nla_data(attrs[CTRL_ATTR_MCAST_GROUPS]); nla_ok(grp, rem); grp = nla_next(grp, &rem)) {
            struct nlattr *grp_attrs[CTRL_ATTR_MCAST_GRP_MAX + 1];
            char *name;
            int nested_len = nla_payload_len(grp);

            nla_parse(grp_attrs, CTRL_ATTR_MCAST_GRP_MAX, nla_data(grp), nested_len);
            if (grp_attrs[CTRL_ATTR_MCAST_GRP_NAME] == NULL || grp_attrs[CTRL_ATTR_MCAST_GRP_ID] == NULL) {
                continue;
            }

            name = nla_data(grp_attrs[CTRL_ATTR_MCAST_GRP_NAME]);
            if (strcmp(name, "scan") == 0) {
                *scan_group_id = (int) nla_get_u32(grp_attrs[CTRL_ATTR_MCAST_GRP_ID]);
            }
        }
    }

    return 0;
}

static int resolve_nl80211(int fd, uint32_t *seq, int *family_id, int *scan_group_id) {
    char rx[RX_BUF_SIZE];
    struct nl_req req;
    ssize_t nread;

    *family_id = -1;
    *scan_group_id = -1;

    init_genl_req(&req, GENL_ID_CTRL, NLM_F_REQUEST | NLM_F_ACK, ++(*seq), CTRL_CMD_GETFAMILY);
    if (addattr(&req, sizeof(req), CTRL_ATTR_FAMILY_NAME, "nl80211", strlen("nl80211") + 1) != 0) {
        return -1;
    }

    if (nl_send(fd, &req) != 0) {
        return -1;
    }

    for (;;) {
        struct nlmsghdr *nlh;
        int rem;

        nread = recv_netlink(fd, rx, sizeof(rx), 3000);
        if (nread < 0) {
            return -1;
        }

        rem = (int) nread;
        for (nlh = (struct nlmsghdr *) rx; NLMSG_OK(nlh, rem); nlh = NLMSG_NEXT(nlh, rem)) {
            if (nlh->nlmsg_seq != *seq) {
                continue;
            }

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = NLMSG_DATA(nlh);
                if (err->error != 0) {
                    errno = -err->error;
                    return -1;
                }
                continue;
            }

            if (nlh->nlmsg_type == GENL_ID_CTRL) {
                if (parse_family_reply(nlh, family_id, scan_group_id) != 0) {
                    return -1;
                }
                return 0;
            }
        }
    }
}

static int trigger_scan(int fd, int family_id, int ifindex, uint32_t *seq) {
    struct nl_req req;
    struct nlattr *ssids;

    init_genl_req(&req, family_id, NLM_F_REQUEST | NLM_F_ACK, ++(*seq), NL80211_CMD_TRIGGER_SCAN);
    if (addattr_u32(&req, sizeof(req), NL80211_ATTR_IFINDEX, (uint32_t) ifindex) != 0) {
        return -1;
    }

    ssids = start_nested(&req, sizeof(req), NL80211_ATTR_SCAN_SSIDS);
    if (ssids == NULL) {
        return -1;
    }
    if (addattr(&req, sizeof(req), 1, NULL, 0) != 0) {
        return -1;
    }
    end_nested(&req, ssids);

    return nl_send(fd, &req);
}

static int wait_for_scan_event(int fd, int family_id, uint32_t trigger_seq) {
    char rx[RX_BUF_SIZE];
    int acked = 0;

    for (;;) {
        struct nlmsghdr *nlh;
        int rem;
        ssize_t nread = recv_netlink(fd, rx, sizeof(rx), SCAN_TIMEOUT_MS);

        if (nread < 0) {
            return -1;
        }

        rem = (int) nread;
        for (nlh = (struct nlmsghdr *) rx; NLMSG_OK(nlh, rem); nlh = NLMSG_NEXT(nlh, rem)) {
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = NLMSG_DATA(nlh);
                if (nlh->nlmsg_seq == trigger_seq) {
                    if (err->error != 0) {
                        errno = -err->error;
                        return -1;
                    }
                    acked = 1;
                }
                continue;
            }

            if (nlh->nlmsg_type != family_id) {
                continue;
            }

            struct genlmsghdr *genl = NLMSG_DATA(nlh);
            if (genl->cmd == NL80211_CMD_SCAN_ABORTED) {
                errno = ECANCELED;
                return -1;
            }
            if (genl->cmd == NL80211_CMD_NEW_SCAN_RESULTS) {
                return 0;
            }
        }

        if (acked) {
            continue;
        }
    }
}

static int freq_to_channel(int freq) {
    if (freq >= 2412 && freq <= 2472) {
        return (freq - 2407) / 5;
    }
    if (freq == 2484) {
        return 14;
    }
    if (freq >= 5000 && freq <= 5895) {
        return (freq - 5000) / 5;
    }
    if (freq == 5955) {
        return 1;
    }
    if (freq >= 5975 && freq <= 7115) {
        return (freq - 5950) / 5;
    }
    return 0;
}

static void summarize_security(struct network *net) {
    const char *base = "Open";

    if ((net->sec_flags & SEC_RSN) && (net->sec_flags & SEC_WPA)) {
        base = "WPA/WPA2";
    } else if (net->sec_flags & SEC_RSN) {
        base = "WPA2";
    } else if (net->sec_flags & SEC_WPA) {
        base = "WPA";
    } else if (net->privacy) {
        base = "Protected";
    }

    if (net->sec_flags & SEC_WPS) {
        snprintf(net->security, sizeof(net->security), "%s+WPS", base);
    } else {
        snprintf(net->security, sizeof(net->security), "%s", base);
    }
}

static void set_bw(struct network *net, const char *label, int rank) {
    if (rank >= net->bw_rank) {
        snprintf(net->bw, sizeof(net->bw), "%s", label);
        net->bw_rank = rank;
    }
}

static void set_standard(struct network *net, const char *label, int rank) {
    if (rank >= net->std_rank) {
        snprintf(net->standard, sizeof(net->standard), "%s", label);
        net->std_rank = rank;
    }
}

static void finalize_standard(struct network *net) {
    char base[sizeof(net->standard)];

    if (net->standard[0] == '\0') {
        if (net->freq >= 5925) {
            snprintf(net->standard, sizeof(net->standard), "11ax");
        } else if (net->freq >= 5000) {
            snprintf(net->standard, sizeof(net->standard), "11a");
        } else if (net->legacy_ofdm) {
            snprintf(net->standard, sizeof(net->standard), "11g");
        } else {
            snprintf(net->standard, sizeof(net->standard), "11b");
        }
    }

    snprintf(base, sizeof(base), "%s", net->standard);

    if (strcmp(base, "11n") == 0) {
        snprintf(net->standard, sizeof(net->standard), "11n (4)");
    } else if (strcmp(base, "11ac") == 0) {
        snprintf(net->standard, sizeof(net->standard), "11ac (5)");
    } else if (strcmp(base, "11ax") == 0) {
        if (net->freq >= 5925) {
            snprintf(net->standard, sizeof(net->standard), "11ax (6E)");
        } else {
            snprintf(net->standard, sizeof(net->standard), "11ax (6)");
        }
    } else if (strcmp(base, "11be") == 0) {
        snprintf(net->standard, sizeof(net->standard), "11be (7)");
    }
}

static int push_network(struct network_list *list, const struct network *net) {
    if (list->len == list->cap) {
        size_t new_cap = list->cap == 0 ? 64 : list->cap * 2;
        struct network *new_items = realloc(list->items, new_cap * sizeof(*new_items));
        if (new_items == NULL) {
            return -1;
        }
        list->items = new_items;
        list->cap = new_cap;
    }

    list->items[list->len++] = *net;
    return 0;
}

static void parse_ies(struct network *net, const unsigned char *ies, size_t len) {
    size_t pos = 0;

    while (pos + 2 <= len) {
        unsigned char id = ies[pos];
        unsigned char ie_len = ies[pos + 1];

        pos += 2;
        if (pos + ie_len > len) {
            break;
        }

        if (id == 0) {
            size_t copy_len = ie_len;
            if (copy_len >= sizeof(net->ssid)) {
                copy_len = sizeof(net->ssid) - 1;
            }
            memcpy(net->ssid, ies + pos, copy_len);
            net->ssid[copy_len] = '\0';
        } else if ((id == 1 || id == 50) && ie_len > 0) {
            for (unsigned char i = 0; i < ie_len; i++) {
                unsigned char rate = ies[pos + i] & 0x7f;
                if (rate == 12 || rate == 18 || rate == 24 || rate == 36 ||
                    rate == 48 || rate == 72 || rate == 96 || rate == 108) {
                    net->legacy_ofdm = 1;
                }
            }
        } else if (id == 61 && ie_len >= 2) {
            unsigned char ht_info = ies[pos + 1];
            unsigned char sec_offset = ht_info & 0x3;
            set_standard(net, "11n", 2);
            if ((ht_info & 0x4) != 0 && (sec_offset == 1 || sec_offset == 3)) {
                set_bw(net, "40", 2);
            }
        } else if (id == 45) {
            set_standard(net, "11n", 2);
        } else if (id == 48) {
            net->sec_flags |= SEC_RSN;
        } else if (id == 191) {
            set_standard(net, "11ac", 3);
        } else if (id == 192 && ie_len >= 1) {
            set_standard(net, "11ac", 3);
            switch (ies[pos]) {
            case 1:
                set_bw(net, "80", 3);
                break;
            case 2:
                set_bw(net, "160", 4);
                break;
            case 3:
                set_bw(net, "80+80", 5);
                break;
            default:
                break;
            }
        } else if (id == 221 && ie_len >= 4) {
            const unsigned char *oui = ies + pos;
            if (oui[0] == 0x00 && oui[1] == 0x50 && oui[2] == 0xf2) {
                if (oui[3] == 0x01) {
                    net->sec_flags |= SEC_WPA;
                } else if (oui[3] == 0x04) {
                    net->sec_flags |= SEC_WPS;
                }
            }
        } else if (id == 255 && ie_len >= 1) {
            switch (ies[pos]) {
            case 35:
            case 36:
                set_standard(net, "11ax", 4);
                break;
            case 106:
            case 108:
                set_standard(net, "11be", 5);
                break;
            default:
                break;
            }
        }

        pos += ie_len;
    }
}

static int parse_bss_message(struct nlmsghdr *nlh, struct network_list *list) {
    struct genlmsghdr *genl = NLMSG_DATA(nlh);
    int attr_len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*genl));
    struct nlattr *attrs[NL80211_ATTR_MAX + 1];
    struct nlattr *bss[NL80211_BSS_MAX + 1];
    struct network net;

    nla_parse(attrs, NL80211_ATTR_MAX, (struct nlattr *) ((char *) genl + GENL_HDRLEN), attr_len);
    if (attrs[NL80211_ATTR_BSS] == NULL) {
        return 0;
    }

    memset(&net, 0, sizeof(net));
    net.signal_dbm = -999.0;

    nla_parse(bss, NL80211_BSS_MAX, nla_data(attrs[NL80211_ATTR_BSS]), nla_payload_len(attrs[NL80211_ATTR_BSS]));

    if (bss[NL80211_BSS_BSSID] == NULL || nla_payload_len(bss[NL80211_BSS_BSSID]) < 6) {
        return 0;
    }

    {
        const unsigned char *mac = nla_data(bss[NL80211_BSS_BSSID]);
        snprintf(net.bssid, sizeof(net.bssid),
            "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    if (bss[NL80211_BSS_FREQUENCY] != NULL) {
        net.freq = (int) nla_get_u32(bss[NL80211_BSS_FREQUENCY]);
        net.channel = freq_to_channel(net.freq);
    }

    if (bss[NL80211_BSS_SEEN_MS_AGO] != NULL) {
        net.last_seen_ms = nla_get_u32(bss[NL80211_BSS_SEEN_MS_AGO]);
    }

    if (bss[NL80211_BSS_CAPABILITY] != NULL) {
        uint16_t cap = nla_get_u16(bss[NL80211_BSS_CAPABILITY]);
        if (cap & 0x0010) {
            net.privacy = 1;
        }
    }

    if (bss[NL80211_BSS_SIGNAL_MBM] != NULL) {
        int32_t mbm = (int32_t) nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]);
        net.signal_dbm = mbm / 100.0;
        net.has_signal = 1;
    } else if (bss[NL80211_BSS_SIGNAL_UNSPEC] != NULL) {
        unsigned char level = *(unsigned char *) nla_data(bss[NL80211_BSS_SIGNAL_UNSPEC]);
        net.signal_dbm = (double) level;
        net.has_signal = 1;
    }

    if (bss[NL80211_BSS_INFORMATION_ELEMENTS] != NULL) {
        parse_ies(&net,
            nla_data(bss[NL80211_BSS_INFORMATION_ELEMENTS]),
            (size_t) nla_payload_len(bss[NL80211_BSS_INFORMATION_ELEMENTS]));
    }
    if (net.ssid[0] == '\0' && bss[NL80211_BSS_BEACON_IES] != NULL) {
        parse_ies(&net,
            nla_data(bss[NL80211_BSS_BEACON_IES]),
            (size_t) nla_payload_len(bss[NL80211_BSS_BEACON_IES]));
    }

    if (net.ssid[0] == '\0') {
        snprintf(net.ssid, sizeof(net.ssid), "<hidden>");
    }
    if (net.bw[0] == '\0') {
        set_bw(&net, "20", 1);
    }
    finalize_standard(&net);
    summarize_security(&net);

    return push_network(list, &net);
}

static int dump_scan_results(int fd, int family_id, int ifindex, uint32_t *seq, struct network_list *list) {
    char rx[RX_BUF_SIZE];
    struct nl_req req;
    uint32_t dump_seq;

    init_genl_req(&req, family_id, NLM_F_REQUEST | NLM_F_DUMP, ++(*seq), NL80211_CMD_GET_SCAN);
    dump_seq = *seq;

    if (addattr_u32(&req, sizeof(req), NL80211_ATTR_IFINDEX, (uint32_t) ifindex) != 0) {
        return -1;
    }

    if (nl_send(fd, &req) != 0) {
        return -1;
    }

    for (;;) {
        struct nlmsghdr *nlh;
        int rem;
        ssize_t nread = recv_netlink(fd, rx, sizeof(rx), 5000);

        if (nread < 0) {
            return -1;
        }

        rem = (int) nread;
        for (nlh = (struct nlmsghdr *) rx; NLMSG_OK(nlh, rem); nlh = NLMSG_NEXT(nlh, rem)) {
            if (nlh->nlmsg_seq != dump_seq) {
                continue;
            }

            if (nlh->nlmsg_type == NLMSG_DONE) {
                return 0;
            }

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = NLMSG_DATA(nlh);
                if (err->error != 0) {
                    errno = -err->error;
                    return -1;
                }
                continue;
            }

            if (nlh->nlmsg_type == family_id) {
                if (parse_bss_message(nlh, list) != 0) {
                    return -1;
                }
            }
        }
    }
}

static int cmp_networks(const void *a, const void *b) {
    const struct network *lhs = a;
    const struct network *rhs = b;

    if (lhs->has_signal && rhs->has_signal) {
        if (lhs->signal_dbm < rhs->signal_dbm) {
            return 1;
        }
        if (lhs->signal_dbm > rhs->signal_dbm) {
            return -1;
        }
    } else if (lhs->has_signal != rhs->has_signal) {
        return rhs->has_signal - lhs->has_signal;
    }

    if (lhs->freq != rhs->freq) {
        return rhs->freq - lhs->freq;
    }

    if (strcmp(lhs->ssid, rhs->ssid) != 0) {
        return strcmp(lhs->ssid, rhs->ssid);
    }

    return strcmp(lhs->bssid, rhs->bssid);
}

static size_t cap_width(size_t width, size_t limit) {
    return width > limit ? limit : width;
}

static void format_cell(char *dst, size_t dst_size, const char *src, size_t width) {
    size_t len = strlen(src);

    if (width + 1 > dst_size) {
        width = dst_size - 1;
    }

    if (len <= width) {
        snprintf(dst, dst_size, "%-*s", (int) width, src);
        return;
    }

    if (width <= 3) {
        snprintf(dst, dst_size, "%.*s", (int) width, src);
        return;
    }

    snprintf(dst, dst_size, "%.*s...", (int) (width - 3), src);
}

static void format_last_seen(char *dst, size_t dst_size, uint32_t last_seen_ms) {
    if (last_seen_ms < 1000) {
        snprintf(dst, dst_size, "%ums", last_seen_ms);
    } else if (last_seen_ms < 60000) {
        snprintf(dst, dst_size, "%.1fs", last_seen_ms / 1000.0);
    } else {
        snprintf(dst, dst_size, "%.1fm", last_seen_ms / 60000.0);
    }
}

static void print_table(struct network_list *list) {
    size_t ssid_w = 4;
    size_t std_w = 3;
    size_t sec_w = 3;

    for (size_t i = 0; i < list->len; i++) {
        size_t ssid_len = strlen(list->items[i].ssid);
        size_t std_len = strlen(list->items[i].standard);
        size_t sec_len = strlen(list->items[i].security);

        if (ssid_len > ssid_w) {
            ssid_w = ssid_len;
        }
        if (std_len > std_w) {
            std_w = std_len;
        }
        if (sec_len > sec_w) {
            sec_w = sec_len;
        }
    }

    ssid_w = cap_width(ssid_w, 32);
    std_w = cap_width(std_w, 10);
    sec_w = cap_width(sec_w, 16);

    printf("%-*s  %-17s  %4s  %5s  %6s  %-*s  %8s  %7s  %-*s\n",
        (int) ssid_w, "SSID", "BSSID", "CHAN", "FREQ", "BW", (int) std_w, "STD", "SIGNAL", "LAST", (int) sec_w, "SEC");
    printf("%-*s  %-17s  %4s  %5s  %6s  %-*s  %8s  %7s  %-*s\n",
        (int) ssid_w, "----", "-----------------", "----", "-----", "------", (int) std_w, "---", "--------", "-------", (int) sec_w, "---");

    for (size_t i = 0; i < list->len; i++) {
        char ssid[64];
        char standard[32];
        char sec[32];
        char signal[16];
        char last_seen[16];

        format_cell(ssid, sizeof(ssid), list->items[i].ssid, ssid_w);
        format_cell(standard, sizeof(standard), list->items[i].standard, std_w);
        format_cell(sec, sizeof(sec), list->items[i].security, sec_w);
        format_last_seen(last_seen, sizeof(last_seen), list->items[i].last_seen_ms);

        if (list->items[i].has_signal) {
            snprintf(signal, sizeof(signal), "%7.1f", list->items[i].signal_dbm);
        } else {
            snprintf(signal, sizeof(signal), "%7s", "-");
        }

        printf("%s  %-17s  %4d  %5d  %6s  %s  %8s  %7s  %s\n",
            ssid,
            list->items[i].bssid,
            list->items[i].channel,
            list->items[i].freq,
            list->items[i].bw,
            standard,
            signal,
            last_seen,
            sec);
    }
}

int main(int argc, char *argv[]) {
    const char *iface = NULL;
    char iface_buf[IF_NAMESIZE];
    struct network_list networks = {0};
    uint32_t seq = 0;
    int fd = -1;
    int family_id = -1;
    int scan_group_id = -1;
    int ifindex;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            iface = argv[++i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (iface == NULL) {
        if (find_first_wireless(iface_buf, sizeof(iface_buf)) != 0) {
            fprintf(stderr, "Could not detect a wireless interface\n");
            return 1;
        }
        iface = iface_buf;
    }

    ifindex = if_nametoindex(iface);
    if (ifindex == 0) {
        perror(iface);
        return 1;
    }

    fd = nl_open();
    if (fd < 0) {
        perror("netlink");
        return 1;
    }

    if (resolve_nl80211(fd, &seq, &family_id, &scan_group_id) != 0) {
        perror("resolve nl80211");
        close(fd);
        return 1;
    }

    if (scan_group_id > 0 && join_mcgroup(fd, scan_group_id) != 0) {
        perror("join scan multicast group");
        close(fd);
        return 1;
    }

    if (trigger_scan(fd, family_id, ifindex, &seq) != 0) {
        perror("trigger scan");
        fprintf(stderr, "Hint: run %s with sudo or CAP_NET_ADMIN.\n", argv[0]);
        close(fd);
        return 1;
    }

    if (wait_for_scan_event(fd, family_id, seq) != 0) {
        perror("wait for scan");
        close(fd);
        return 1;
    }

    if (dump_scan_results(fd, family_id, ifindex, &seq, &networks) != 0) {
        perror("get scan results");
        close(fd);
        free(networks.items);
        return 1;
    }

    close(fd);

    if (networks.len == 0) {
        fprintf(stderr, "No BSS entries found\n");
        free(networks.items);
        return 1;
    }

    qsort(networks.items, networks.len, sizeof(*networks.items), cmp_networks);
    print_table(&networks);
    free(networks.items);
    return 0;
}
