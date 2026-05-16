#include <rabbitbone_sys.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define NETCTL_FRAME_MAX 1600u
#define NETCTL_ETH_LEN 14u
#define NETCTL_ARP_LEN 28u
#define NETCTL_IP_LEN 20u
#define NETCTL_UDP_LEN 8u
#define NETCTL_ICMP_LEN 8u
#define NETCTL_DHCP_FIXED 236u
#define NETCTL_DHCP_MAGIC 0x63825363u
#define NETCTL_BOOTP_CLIENT 1u
#define NETCTL_BOOTP_SERVER 2u
#define NETCTL_DHCP_DISCOVER 1u
#define NETCTL_DHCP_OFFER 2u
#define NETCTL_DHCP_REQUEST 3u
#define NETCTL_DHCP_ACK 5u
#define NETCTL_DHCP_NAK 6u
#define NETCTL_ARP_REQUEST 1u
#define NETCTL_ARP_REPLY 2u
#define NETCTL_ET_ARP 0x0806u
#define NETCTL_ET_IP 0x0800u
#define NETCTL_PROTO_ICMP 1u
#define NETCTL_PROTO_UDP 17u
#define NETCTL_PORT_DHCP_SERVER 67u
#define NETCTL_PORT_DHCP_CLIENT 68u
#define NETCTL_DEFAULT_TIMEOUT_TICKS 500u
#define NETCTL_DHCP_TIMEOUT_TICKS 700u
#define NETCTL_CONF_PATH "/tmp/net0.conf"
#define NETCTL_RESOLV_PATH "/etc/resolv.conf"

typedef struct netctl_config {
    u32 ip;
    u32 mask;
    u32 gw;
    u32 dns;
    u32 server;
    u32 lease;
    int valid;
} netctl_config_t;

typedef struct netctl_dhcp_offer {
    u32 yiaddr;
    u32 mask;
    u32 gw;
    u32 dns;
    u32 server;
    u32 lease;
    u8 msg_type;
    int valid;
} netctl_dhcp_offer_t;

typedef struct netctl_ctx {
    const char *dev_path;
    au_i64 fd;
    u8 mac[6];
    netctl_config_t conf;
} netctl_ctx_t;

static void out(const char *s) {
    if (!s) return;
    (void)au_write((au_i64)RABBITBONE_STDOUT, s, au_strlen(s));
}

static void err(const char *s) {
    if (!s) return;
    (void)au_write((au_i64)RABBITBONE_STDERR, s, au_strlen(s));
}

static int streq(const char *a, const char *b) { return a && b && au_strcmp(a, b) == 0; }
static int starts(const char *s, const char *p) {
    if (!s || !p) return 0;
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static au_usize cstrnlen(const char *s, au_usize max) {
    au_usize n = 0;
    if (!s) return 0;
    while (n < max && s[n]) ++n;
    return n;
}

static void append(char *buf, au_usize cap, const char *s) {
    if (!buf || cap == 0 || !s) return;
    au_usize used = cstrnlen(buf, cap);
    while (used + 1u < cap && *s) buf[used++] = *s++;
    buf[used] = 0;
}

static void u32_dec(char *buf, au_usize cap, u32 v) {
    char tmp[16];
    unsigned int n = 0;
    if (!buf || cap == 0) return;
    if (v == 0) { append(buf, cap, "0"); return; }
    while (v && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n) { char c[2]; c[0] = tmp[--n]; c[1] = 0; append(buf, cap, c); }
}

static void i64_dec(char *buf, au_usize cap, au_i64 v) {
    char tmp[32];
    unsigned int n = 0;
    unsigned long long x;
    if (!buf || cap == 0) return;
    if (v < 0) { append(buf, cap, "-"); x = (unsigned long long)(-(v + 1)) + 1ull; }
    else x = (unsigned long long)v;
    if (x == 0) { append(buf, cap, "0"); return; }
    while (x && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (x % 10ull)); x /= 10ull; }
    while (n) { char c[2]; c[0] = tmp[--n]; c[1] = 0; append(buf, cap, c); }
}


static u16 rd16(const u8 *p) { return (u16)(((u16)p[0] << 8u) | p[1]); }
static u32 rd32(const u8 *p) { return ((u32)p[0] << 24u) | ((u32)p[1] << 16u) | ((u32)p[2] << 8u) | p[3]; }
static void wr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8u); p[1] = (u8)v; }
static void wr32(u8 *p, u32 v) { p[0] = (u8)(v >> 24u); p[1] = (u8)(v >> 16u); p[2] = (u8)(v >> 8u); p[3] = (u8)v; }

static u16 checksum(const void *data, au_usize len) {
    const u8 *p = (const u8 *)data;
    u32 sum = 0;
    while (len > 1u) { sum += rd16(p); p += 2; len -= 2u; }
    if (len) sum += (u16)((u16)*p << 8u);
    while (sum >> 16u) sum = (sum & 0xffffu) + (sum >> 16u);
    return (u16)~sum;
}

static int parse_u32_dec(const char **pp, u32 *outv) {
    const char *p = pp ? *pp : 0;
    u32 v = 0;
    int any = 0;
    if (!p || !outv) return 0;
    while (is_digit(*p)) {
        u32 d = (u32)(*p - '0');
        if (v > (0xffffffffu - d) / 10u) return 0;
        v = v * 10u + d;
        any = 1;
        ++p;
    }
    if (!any) return 0;
    *pp = p;
    *outv = v;
    return 1;
}

static int parse_ip(const char *s, u32 *out_ip, const char **end_out) {
    const char *p = s;
    u32 b[4];
    if (!s || !out_ip) return 0;
    for (unsigned int i = 0; i < 4u; ++i) {
        if (!parse_u32_dec(&p, &b[i]) || b[i] > 255u) return 0;
        if (i != 3u) { if (*p != '.') return 0; ++p; }
    }
    *out_ip = (b[0] << 24u) | (b[1] << 16u) | (b[2] << 8u) | b[3];
    if (end_out) *end_out = p;
    return 1;
}

static u32 prefix_mask(u32 prefix) {
    if (prefix == 0) return 0;
    if (prefix >= 32u) return 0xffffffffu;
    return 0xffffffffu << (32u - prefix);
}

static void ip_str(u32 ip, char *buf, au_usize cap) {
    if (!buf || cap == 0) return;
    buf[0] = 0;
    u32_dec(buf, cap, (ip >> 24u) & 255u); append(buf, cap, ".");
    u32_dec(buf, cap, (ip >> 16u) & 255u); append(buf, cap, ".");
    u32_dec(buf, cap, (ip >> 8u) & 255u); append(buf, cap, ".");
    u32_dec(buf, cap, ip & 255u);
}

static void mac_str(const u8 mac[6], char *buf, au_usize cap) {
    static const char hx[] = "0123456789abcdef";
    if (!buf || cap < 18u) return;
    for (unsigned int i = 0; i < 6u; ++i) {
        buf[i * 3u + 0u] = hx[(mac[i] >> 4u) & 15u];
        buf[i * 3u + 1u] = hx[mac[i] & 15u];
        buf[i * 3u + 2u] = i == 5u ? 0 : ':';
    }
}

static int parse_mac_text(const char *s, u8 mac[6]) {
    if (!s || !mac) return 0;
    for (unsigned int i = 0; i < 6u; ++i) {
        if (!is_hex(s[0]) || !is_hex(s[1])) return 0;
        mac[i] = (u8)((hex_value(s[0]) << 4u) | hex_value(s[1]));
        s += 2;
        if (i != 5u) { if (*s != ':') return 0; ++s; }
    }
    return 1;
}

static int load_mac_from_kctl(u8 mac[6]) {
    char *buf = (char *)malloc(RABBITBONE_KCTL_OUT_MAX);
    if (!buf) return 0;
    au_memset(buf, 0, RABBITBONE_KCTL_OUT_MAX);
    au_i64 r = au_kctl(RABBITBONE_KCTL_OP_NET, buf, RABBITBONE_KCTL_OUT_MAX, 0);
    if (r < 0) { free(buf); return 0; }
    for (const char *p = buf; *p; ++p) {
        if (starts(p, "mac=") && parse_mac_text(p + 4, mac)) { free(buf); return 1; }
    }
    free(buf);
    return 0;
}

static int print_kernel_net_status(void) {
    char *buf = (char *)malloc(RABBITBONE_KCTL_OUT_MAX);
    if (!buf) return 0;
    au_memset(buf, 0, RABBITBONE_KCTL_OUT_MAX);
    au_i64 r = au_kctl(RABBITBONE_KCTL_OP_NET, buf, RABBITBONE_KCTL_OUT_MAX, 0);
    if (r >= 0) out(buf);
    free(buf);
    return r >= 0;
}

static void print_no_interface_hint(void) {
    err("netctl: no registered /dev/net0 interface\n");
    err("netctl: in VMware set ethernet0.virtualDev to e1000 or e1000e and keep ethernet0.startConnected TRUE\n");
    err("netctl: run 'pci' and check for Intel 8086:100f/100e or 8086:10d3 network controller\n");
}

static int read_file_small(const char *path, char *buf, au_usize cap) {
    if (!buf || cap == 0) return 0;
    buf[0] = 0;
    au_i64 fd = au_open(path);
    if (fd < 0) return 0;
    au_i64 n = au_read(fd, buf, cap - 1u);
    (void)au_close(fd);
    if (n < 0) return 0;
    buf[n] = 0;
    return 1;
}

static int find_key_ip(const char *text, const char *key, u32 *out_ip) {
    if (!text || !key || !out_ip) return 0;
    au_usize kl = au_strlen(key);
    const char *p = text;
    while (*p) {
        if ((p == text || p[-1] == '\n') && starts(p, key) && p[kl] == '=') return parse_ip(p + kl + 1u, out_ip, 0);
        while (*p && *p != '\n') ++p;
        if (*p == '\n') ++p;
    }
    return 0;
}

static int find_key_dec(const char *text, const char *key, u32 *out_v) {
    if (!text || !key || !out_v) return 0;
    au_usize kl = au_strlen(key);
    const char *p = text;
    while (*p) {
        if ((p == text || p[-1] == '\n') && starts(p, key) && p[kl] == '=') {
            const char *q = p + kl + 1u;
            return parse_u32_dec(&q, out_v);
        }
        while (*p && *p != '\n') ++p;
        if (*p == '\n') ++p;
    }
    return 0;
}

static int load_config(netctl_config_t *c) {
    char buf[512];
    if (!c) return 0;
    au_memset(c, 0, sizeof(*c));
    if (!read_file_small(NETCTL_CONF_PATH, buf, sizeof(buf))) return 0;
    if (!find_key_ip(buf, "ip", &c->ip)) return 0;
    if (!find_key_ip(buf, "mask", &c->mask)) c->mask = 0xffffff00u;
    (void)find_key_ip(buf, "gw", &c->gw);
    (void)find_key_ip(buf, "dns", &c->dns);
    (void)find_key_ip(buf, "server", &c->server);
    (void)find_key_dec(buf, "lease", &c->lease);
    c->valid = c->ip != 0;
    return c->valid;
}

static void append_key_ip(char *buf, au_usize cap, const char *key, u32 ip) {
    char tmp[32];
    tmp[0] = 0;
    ip_str(ip, tmp, sizeof(tmp));
    append(buf, cap, key); append(buf, cap, "="); append(buf, cap, tmp); append(buf, cap, "\n");
}

static int save_config(const netctl_config_t *c) {
    char buf[384];
    if (!c || !c->ip) return 0;
    (void)au_mkdir("/tmp");
    buf[0] = 0;
    append_key_ip(buf, sizeof(buf), "ip", c->ip);
    append_key_ip(buf, sizeof(buf), "mask", c->mask ? c->mask : 0xffffff00u);
    append_key_ip(buf, sizeof(buf), "gw", c->gw);
    append_key_ip(buf, sizeof(buf), "dns", c->dns);
    append_key_ip(buf, sizeof(buf), "server", c->server);
    append(buf, sizeof(buf), "lease="); u32_dec(buf, sizeof(buf), c->lease); append(buf, sizeof(buf), "\n");
    (void)au_unlink(NETCTL_CONF_PATH);
    if (au_create(NETCTL_CONF_PATH, buf, au_strlen(buf)) < 0) return 0;
    if (c->dns) {
        char resolv[80];
        resolv[0] = 0;
        append(resolv, sizeof(resolv), "nameserver ");
        char dns[32]; dns[0] = 0; ip_str(c->dns, dns, sizeof(dns));
        append(resolv, sizeof(resolv), dns); append(resolv, sizeof(resolv), "\n");
        (void)au_unlink(NETCTL_RESOLV_PATH);
        (void)au_create(NETCTL_RESOLV_PATH, resolv, au_strlen(resolv));
    }
    return 1;
}

static void eth_header(u8 *f, const u8 dst[6], const u8 src[6], u16 type) {
    au_memcpy(f, dst, 6); au_memcpy(f + 6, src, 6); wr16(f + 12, type);
}

static au_usize build_ip_udp(u8 *f, const u8 dst_mac[6], const u8 src_mac[6], u32 src_ip, u32 dst_ip, u16 sport, u16 dport, au_usize udp_payload_len) {
    au_usize ip_off = NETCTL_ETH_LEN;
    au_usize udp_off = ip_off + NETCTL_IP_LEN;
    au_usize ip_len = NETCTL_IP_LEN + NETCTL_UDP_LEN + udp_payload_len;
    eth_header(f, dst_mac, src_mac, NETCTL_ET_IP);
    f[ip_off + 0] = 0x45; f[ip_off + 1] = 0;
    wr16(f + ip_off + 2, (u16)ip_len);
    wr16(f + ip_off + 4, (u16)(au_ticks() & 0xffffu));
    wr16(f + ip_off + 6, 0);
    f[ip_off + 8] = 64; f[ip_off + 9] = NETCTL_PROTO_UDP;
    wr16(f + ip_off + 10, 0);
    wr32(f + ip_off + 12, src_ip);
    wr32(f + ip_off + 16, dst_ip);
    wr16(f + ip_off + 10, checksum(f + ip_off, NETCTL_IP_LEN));
    wr16(f + udp_off + 0, sport);
    wr16(f + udp_off + 2, dport);
    wr16(f + udp_off + 4, (u16)(NETCTL_UDP_LEN + udp_payload_len));
    wr16(f + udp_off + 6, 0);
    return NETCTL_ETH_LEN + ip_len;
}

static int send_frame(au_i64 fd, const u8 *frame, au_usize len) {
    au_i64 w = au_write(fd, frame, len);
    return w == (au_i64)len;
}

static int send_frame_checked(au_i64 fd, const u8 *frame, au_usize len, const char *what) {
    au_i64 w = au_write(fd, frame, len);
    if (w == (au_i64)len) return 1;
    if (what) {
        char num[32];
        err(what);
        err(": write failed rc=");
        num[0] = 0; i64_dec(num, sizeof(num), w); err(num);
        err(" len=");
        num[0] = 0; u32_dec(num, sizeof(num), (u32)len); err(num);
        err("\n");
    }
    return 0;
}

static int recv_frame(au_i64 fd, u8 *frame, au_usize cap, au_usize *len_out) {
    au_i64 r = au_read(fd, frame, cap);
    if (r < 0) return -1;
    if (len_out) *len_out = (au_usize)r;
    return r > 0 ? 1 : 0;
}

static int open_ctx(netctl_ctx_t *ctx, const char *dev_path) {
    if (!ctx) return 0;
    au_memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;
    ctx->dev_path = dev_path ? dev_path : "/dev/net0";
    if (!load_mac_from_kctl(ctx->mac)) {
        print_no_interface_hint();
        return 0;
    }
    ctx->fd = au_open2(ctx->dev_path, RABBITBONE_O_RDWR);
    if (ctx->fd < 0) {
        err("netctl: unable to open raw net device for read/write\n");
        return 0;
    }
    (void)load_config(&ctx->conf);
    return 1;
}

static void close_ctx(netctl_ctx_t *ctx) {
    if (ctx && ctx->fd >= 0) (void)au_close(ctx->fd);
}

static void print_config(const netctl_config_t *c) {
    char ip[32], mask[32], gw[32], dns[32];
    ip[0] = mask[0] = gw[0] = dns[0] = 0;
    if (!c || !c->valid) { out("ip: not configured\n"); return; }
    ip_str(c->ip, ip, sizeof(ip)); ip_str(c->mask, mask, sizeof(mask)); ip_str(c->gw, gw, sizeof(gw)); ip_str(c->dns, dns, sizeof(dns));
    out("ip: "); out(ip); out(" mask "); out(mask); out(" gw "); out(gw); out(" dns "); out(dns); out("\n");
}

static au_usize add_dhcp_common_options(u8 *opt, au_usize cap, u8 msg_type, u32 requested_ip, u32 server_id, const u8 mac[6]) {
    au_usize n = 0;
    if (cap < 64u) return 0;
    opt[n++] = 53; opt[n++] = 1; opt[n++] = msg_type;
    opt[n++] = 57; opt[n++] = 2; wr16(opt + n, 1500); n += 2;
    opt[n++] = 61; opt[n++] = 7; opt[n++] = 1; au_memcpy(opt + n, mac, 6); n += 6;
    if (requested_ip) { opt[n++] = 50; opt[n++] = 4; wr32(opt + n, requested_ip); n += 4; }
    if (server_id) { opt[n++] = 54; opt[n++] = 4; wr32(opt + n, server_id); n += 4; }
    opt[n++] = 55; opt[n++] = 7; opt[n++] = 1; opt[n++] = 3; opt[n++] = 6; opt[n++] = 15; opt[n++] = 28; opt[n++] = 51; opt[n++] = 54;
    opt[n++] = 255;
    return n;
}

static int send_dhcp(netctl_ctx_t *ctx, u8 msg_type, u32 xid, u32 requested_ip, u32 server_id) {
    u8 frame[NETCTL_FRAME_MAX];
    static const u8 bcast[6] = {255,255,255,255,255,255};
    au_memset(frame, 0, sizeof(frame));
    au_usize bootp_off = NETCTL_ETH_LEN + NETCTL_IP_LEN + NETCTL_UDP_LEN;
    u8 *b = frame + bootp_off;
    b[0] = NETCTL_BOOTP_CLIENT; b[1] = 1; b[2] = 6; b[3] = 0;
    wr32(b + 4, xid);
    wr16(b + 10, 0x8000u);
    au_memcpy(b + 28, ctx->mac, 6);
    wr32(b + NETCTL_DHCP_FIXED, NETCTL_DHCP_MAGIC);
    au_usize opts = add_dhcp_common_options(b + NETCTL_DHCP_FIXED + 4u, 300u, msg_type, requested_ip, server_id, ctx->mac);
    au_usize dhcp_len = NETCTL_DHCP_FIXED + 4u + opts;
    au_usize frame_len = build_ip_udp(frame, bcast, ctx->mac, 0, 0xffffffffu, NETCTL_PORT_DHCP_CLIENT, NETCTL_PORT_DHCP_SERVER, dhcp_len);
    return send_frame_checked(ctx->fd, frame, frame_len, "dhcp");
}

static void parse_dhcp_options(const u8 *opt, au_usize len, netctl_dhcp_offer_t *offer) {
    au_usize i = 0;
    while (i < len) {
        u8 code = opt[i++];
        if (code == 0) continue;
        if (code == 255) break;
        if (i >= len) break;
        u8 olen = opt[i++];
        if (i + olen > len) break;
        if (code == 53 && olen >= 1u) offer->msg_type = opt[i];
        else if (code == 1 && olen >= 4u) offer->mask = rd32(opt + i);
        else if (code == 3 && olen >= 4u) offer->gw = rd32(opt + i);
        else if (code == 6 && olen >= 4u) offer->dns = rd32(opt + i);
        else if (code == 51 && olen >= 4u) offer->lease = rd32(opt + i);
        else if (code == 54 && olen >= 4u) offer->server = rd32(opt + i);
        i += olen;
    }
}

static int parse_dhcp_frame(const u8 *f, au_usize len, u32 xid, netctl_dhcp_offer_t *offer) {
    if (!f || !offer || len < NETCTL_ETH_LEN + NETCTL_IP_LEN + NETCTL_UDP_LEN + NETCTL_DHCP_FIXED + 4u) return 0;
    if (rd16(f + 12) != NETCTL_ET_IP) return 0;
    au_usize ip_off = NETCTL_ETH_LEN;
    if ((f[ip_off] >> 4u) != 4u || f[ip_off + 9] != NETCTL_PROTO_UDP) return 0;
    au_usize ihl = (au_usize)(f[ip_off] & 15u) * 4u;
    if (ihl < 20u || len < NETCTL_ETH_LEN + ihl + NETCTL_UDP_LEN) return 0;
    au_usize udp = NETCTL_ETH_LEN + ihl;
    if (rd16(f + udp + 0) != NETCTL_PORT_DHCP_SERVER || rd16(f + udp + 2) != NETCTL_PORT_DHCP_CLIENT) return 0;
    au_usize bootp = udp + NETCTL_UDP_LEN;
    if (len < bootp + NETCTL_DHCP_FIXED + 4u) return 0;
    const u8 *b = f + bootp;
    if (b[0] != NETCTL_BOOTP_SERVER || rd32(b + 4) != xid || rd32(b + NETCTL_DHCP_FIXED) != NETCTL_DHCP_MAGIC) return 0;
    au_memset(offer, 0, sizeof(*offer));
    offer->yiaddr = rd32(b + 16);
    parse_dhcp_options(b + NETCTL_DHCP_FIXED + 4u, len - bootp - NETCTL_DHCP_FIXED - 4u, offer);
    offer->valid = offer->yiaddr != 0 && (offer->msg_type == NETCTL_DHCP_OFFER || offer->msg_type == NETCTL_DHCP_ACK || offer->msg_type == NETCTL_DHCP_NAK);
    return offer->valid;
}

static int wait_dhcp(netctl_ctx_t *ctx, u32 xid, u8 wanted, netctl_dhcp_offer_t *offer) {
    u8 frame[NETCTL_FRAME_MAX];
    au_i64 start = au_ticks();
    for (;;) {
        au_usize len = 0;
        int rr = recv_frame(ctx->fd, frame, sizeof(frame), &len);
        if (rr > 0 && parse_dhcp_frame(frame, len, xid, offer)) {
            if (offer->msg_type == wanted || offer->msg_type == NETCTL_DHCP_NAK) return offer->msg_type == wanted;
        }
        au_i64 now = au_ticks();
        if (now >= start && (u64)(now - start) > NETCTL_DHCP_TIMEOUT_TICKS) return 0;
        (void)au_sleep(1);
    }
}

static int cmd_dhcp(netctl_ctx_t *ctx) {
    u32 xid = (u32)((u32)au_ticks() << 16u) ^ (u32)au_getpid() ^ ((u32)ctx->mac[4] << 8u) ^ ctx->mac[5];
    netctl_dhcp_offer_t offer;
    au_memset(&offer, 0, sizeof(offer));
    out("dhcp: discover\n");
    for (unsigned int attempt = 0; attempt < 3u; ++attempt) {
        if (!send_dhcp(ctx, NETCTL_DHCP_DISCOVER, xid, 0, 0)) continue;
        if (wait_dhcp(ctx, xid, NETCTL_DHCP_OFFER, &offer)) break;
    }
    if (!offer.valid || offer.msg_type != NETCTL_DHCP_OFFER) { err("dhcp: no offer\n"); return 2; }
    char ip[32]; ip[0] = 0; ip_str(offer.yiaddr, ip, sizeof(ip));
    out("dhcp: offer "); out(ip); out("\n");
    netctl_dhcp_offer_t ack;
    au_memset(&ack, 0, sizeof(ack));
    for (unsigned int attempt = 0; attempt < 3u; ++attempt) {
        if (!send_dhcp(ctx, NETCTL_DHCP_REQUEST, xid, offer.yiaddr, offer.server)) continue;
        if (wait_dhcp(ctx, xid, NETCTL_DHCP_ACK, &ack)) break;
    }
    if (!ack.valid || ack.msg_type != NETCTL_DHCP_ACK) { err("dhcp: no ack\n"); return 3; }
    ctx->conf.ip = ack.yiaddr ? ack.yiaddr : offer.yiaddr;
    ctx->conf.mask = ack.mask ? ack.mask : (offer.mask ? offer.mask : 0xffffff00u);
    ctx->conf.gw = ack.gw ? ack.gw : offer.gw;
    ctx->conf.dns = ack.dns ? ack.dns : offer.dns;
    ctx->conf.server = ack.server ? ack.server : offer.server;
    ctx->conf.lease = ack.lease ? ack.lease : offer.lease;
    ctx->conf.valid = 1;
    if (!save_config(&ctx->conf)) { err("dhcp: lease accepted but config save failed\n"); return 4; }
    out("dhcp: bound "); ip_str(ctx->conf.ip, ip, sizeof(ip)); out(ip); out("\n");
    print_config(&ctx->conf);
    return 0;
}

static int send_arp(netctl_ctx_t *ctx, u16 op, const u8 dst_mac[6], const u8 target_mac[6], u32 sender_ip, u32 target_ip) {
    u8 frame[NETCTL_ETH_LEN + NETCTL_ARP_LEN];
    eth_header(frame, dst_mac, ctx->mac, NETCTL_ET_ARP);
    u8 *a = frame + NETCTL_ETH_LEN;
    wr16(a + 0, 1); wr16(a + 2, NETCTL_ET_IP); a[4] = 6; a[5] = 4; wr16(a + 6, op);
    au_memcpy(a + 8, ctx->mac, 6); wr32(a + 14, sender_ip);
    au_memcpy(a + 18, target_mac, 6); wr32(a + 24, target_ip);
    return send_frame(ctx->fd, frame, sizeof(frame));
}

static int resolve_arp(netctl_ctx_t *ctx, u32 ip, u8 mac[6]) {
    static const u8 bcast[6] = {255,255,255,255,255,255};
    static const u8 zero[6] = {0,0,0,0,0,0};
    u8 frame[NETCTL_FRAME_MAX];
    for (unsigned int attempt = 0; attempt < 3u; ++attempt) {
        (void)send_arp(ctx, NETCTL_ARP_REQUEST, bcast, zero, ctx->conf.ip, ip);
        au_i64 start = au_ticks();
        for (;;) {
            au_usize len = 0;
            int rr = recv_frame(ctx->fd, frame, sizeof(frame), &len);
            if (rr > 0 && len >= NETCTL_ETH_LEN + NETCTL_ARP_LEN && rd16(frame + 12) == NETCTL_ET_ARP) {
                const u8 *a = frame + NETCTL_ETH_LEN;
                if (rd16(a + 0) == 1 && rd16(a + 2) == NETCTL_ET_IP && a[4] == 6 && a[5] == 4 && rd16(a + 6) == NETCTL_ARP_REPLY && rd32(a + 14) == ip && rd32(a + 24) == ctx->conf.ip) {
                    au_memcpy(mac, a + 8, 6);
                    return 1;
                }
            }
            au_i64 now = au_ticks();
            if (now >= start && (u64)(now - start) > 120u) break;
            (void)au_sleep(1);
        }
    }
    return 0;
}

static u32 next_hop_ip(const netctl_config_t *c, u32 target) {
    if (!c || !c->valid) return 0;
    if (c->mask && ((target & c->mask) == (c->ip & c->mask))) return target;
    return c->gw ? c->gw : target;
}

static int build_icmp_echo(u8 *f, const u8 dst_mac[6], const u8 src_mac[6], u32 src_ip, u32 dst_ip, u16 ident, u16 seq) {
    static const char payload[] = "rabbitbone-netctl";
    au_usize payload_len = sizeof(payload) - 1u;
    au_usize ip_off = NETCTL_ETH_LEN;
    au_usize icmp_off = ip_off + NETCTL_IP_LEN;
    au_usize icmp_len = NETCTL_ICMP_LEN + payload_len;
    eth_header(f, dst_mac, src_mac, NETCTL_ET_IP);
    f[ip_off] = 0x45; f[ip_off + 1] = 0;
    wr16(f + ip_off + 2, (u16)(NETCTL_IP_LEN + icmp_len));
    wr16(f + ip_off + 4, (u16)(au_ticks() & 0xffffu));
    wr16(f + ip_off + 6, 0);
    f[ip_off + 8] = 64; f[ip_off + 9] = NETCTL_PROTO_ICMP;
    wr16(f + ip_off + 10, 0);
    wr32(f + ip_off + 12, src_ip); wr32(f + ip_off + 16, dst_ip);
    wr16(f + ip_off + 10, checksum(f + ip_off, NETCTL_IP_LEN));
    f[icmp_off + 0] = 8; f[icmp_off + 1] = 0; wr16(f + icmp_off + 2, 0); wr16(f + icmp_off + 4, ident); wr16(f + icmp_off + 6, seq);
    au_memcpy(f + icmp_off + 8, payload, payload_len);
    wr16(f + icmp_off + 2, checksum(f + icmp_off, icmp_len));
    return (int)(NETCTL_ETH_LEN + NETCTL_IP_LEN + icmp_len);
}

static int parse_icmp_reply(const u8 *f, au_usize len, u32 from, u32 self, u16 ident, u16 seq) {
    if (len < NETCTL_ETH_LEN + NETCTL_IP_LEN + NETCTL_ICMP_LEN || rd16(f + 12) != NETCTL_ET_IP) return 0;
    au_usize ip = NETCTL_ETH_LEN;
    if ((f[ip] >> 4u) != 4u || f[ip + 9] != NETCTL_PROTO_ICMP) return 0;
    au_usize ihl = (au_usize)(f[ip] & 15u) * 4u;
    if (ihl < 20u || len < NETCTL_ETH_LEN + ihl + NETCTL_ICMP_LEN) return 0;
    if (rd32(f + ip + 12) != from || rd32(f + ip + 16) != self) return 0;
    const u8 *ic = f + NETCTL_ETH_LEN + ihl;
    return ic[0] == 0 && ic[1] == 0 && rd16(ic + 4) == ident && rd16(ic + 6) == seq;
}

static int ensure_config(netctl_ctx_t *ctx) {
    if (ctx->conf.valid) return 1;
    out("netctl: no config, trying DHCP\n");
    return cmd_dhcp(ctx) == 0;
}

static int resolve_name(netctl_ctx_t *ctx, const char *name, u32 *out_ip);

static int cmd_ping(netctl_ctx_t *ctx, const char *target_s) {
    u32 target = 0;
    u8 dst_mac[6];
    u8 frame[NETCTL_FRAME_MAX];
    if (!target_s || !*target_s) { err("usage: ping HOST|A.B.C.D\n"); return 2; }
    if (!ensure_config(ctx)) return 3;
    if (!parse_ip(target_s, &target, 0)) {
        if (!ctx->conf.dns) { err("ping: target is not an IPv4 address and no DNS resolver is configured\n"); return 2; }
        if (!resolve_name(ctx, target_s, &target)) { err("ping: DNS failed\n"); return 2; }
    }
    u32 hop = next_hop_ip(&ctx->conf, target);
    if (!hop) { err("ping: no gateway\n"); return 4; }
    if (!resolve_arp(ctx, hop, dst_mac)) { err("ping: arp failed\n"); return 5; }
    u16 ident = (u16)au_getpid();
    u16 seq = 1;
    int len = build_icmp_echo(frame, dst_mac, ctx->mac, ctx->conf.ip, target, ident, seq);
    au_i64 t0 = au_ticks();
    if (!send_frame(ctx->fd, frame, (au_usize)len)) { err("ping: send failed\n"); return 6; }
    for (;;) {
        au_usize got = 0;
        int rr = recv_frame(ctx->fd, frame, sizeof(frame), &got);
        if (rr > 0 && parse_icmp_reply(frame, got, target, ctx->conf.ip, ident, seq)) {
            au_i64 dt = au_ticks() - t0;
            char ip[32]; ip[0] = 0; ip_str(target, ip, sizeof(ip));
            out("reply from "); out(ip); out(" time_ticks="); char d[24]; d[0] = 0; u32_dec(d, sizeof(d), (u32)(dt < 0 ? 0 : dt)); out(d); out("\n");
            return 0;
        }
        au_i64 now = au_ticks();
        if (now >= t0 && (u64)(now - t0) > NETCTL_DEFAULT_TIMEOUT_TICKS) { err("ping: timeout\n"); return 7; }
        (void)au_sleep(1);
    }
}

static int cmd_arp(netctl_ctx_t *ctx, const char *target_s) {
    u32 target = 0;
    u8 mac[6];
    if (!target_s || !parse_ip(target_s, &target, 0)) { err("usage: netctl arp A.B.C.D\n"); return 2; }
    if (!ensure_config(ctx)) return 3;
    if (!resolve_arp(ctx, target, mac)) { err("arp: no reply\n"); return 4; }
    char ip[32], m[24]; ip[0] = m[0] = 0; ip_str(target, ip, sizeof(ip)); mac_str(mac, m, sizeof(m));
    out(ip); out(" is-at "); out(m); out("\n");
    return 0;
}

static int cmd_ip(netctl_ctx_t *ctx, int argc, char **argv) {
    if (argc <= 0) { print_config(&ctx->conf); return 0; }
    const char *ip_arg = argv[0];
    if (streq(ip_arg, "addr") && argc > 1) ip_arg = argv[1];
    u32 ip = 0, mask = 0xffffff00u;
    const char *end = 0;
    if (!parse_ip(ip_arg, &ip, &end)) { err("usage: netctl ip A.B.C.D/PREFIX [gw A.B.C.D] [dns A.B.C.D]\n"); return 2; }
    if (*end == '/') {
        ++end;
        u32 pfx = 0;
        if (!parse_u32_dec(&end, &pfx) || pfx > 32u) return 2;
        mask = prefix_mask(pfx);
    }
    ctx->conf.ip = ip; ctx->conf.mask = mask; ctx->conf.valid = 1;
    for (int i = 1; i < argc; ++i) {
        if (streq(argv[i], "gw") && i + 1 < argc) { (void)parse_ip(argv[++i], &ctx->conf.gw, 0); }
        else if (streq(argv[i], "dns") && i + 1 < argc) { (void)parse_ip(argv[++i], &ctx->conf.dns, 0); }
        else if (streq(argv[i], "mask") && i + 1 < argc) { (void)parse_ip(argv[++i], &ctx->conf.mask, 0); }
    }
    if (!save_config(&ctx->conf)) { err("ip: save failed\n"); return 3; }
    print_config(&ctx->conf);
    return 0;
}

static au_usize dns_name_encode(const char *name, u8 *outb, au_usize cap) {
    au_usize n = 0;
    const char *p = name;
    if (!name || !*name) return 0;
    while (*p) {
        const char *dot = p;
        au_usize labellen = 0;
        while (dot[labellen] && dot[labellen] != '.') ++labellen;
        if (labellen == 0 || labellen > 63u || n + 1u + labellen >= cap) return 0;
        outb[n++] = (u8)labellen;
        au_memcpy(outb + n, p, labellen); n += labellen;
        p = dot + labellen;
        if (*p == '.') ++p;
    }
    if (n >= cap) return 0;
    outb[n++] = 0;
    return n;
}

static int dns_skip_name(const u8 *msg, au_usize len, au_usize *off) {
    au_usize p = off ? *off : 0;
    unsigned int jumps = 0;
    while (p < len) {
        u8 c = msg[p++];
        if (c == 0) { *off = p; return 1; }
        if ((c & 0xc0u) == 0xc0u) {
            if (p >= len || ++jumps > 8u) return 0;
            ++p; *off = p; return 1;
        }
        if ((c & 0xc0u) != 0 || p + c > len) return 0;
        p += c;
    }
    return 0;
}

static int parse_dns_response(const u8 *udp_payload, au_usize len, u16 id, u32 *out_ip) {
    if (!udp_payload || len < 12u || rd16(udp_payload) != id) return 0;
    u16 flags = rd16(udp_payload + 2);
    if ((flags & 0x8000u) == 0 || (flags & 0x000fu) != 0) return 0;
    u16 qd = rd16(udp_payload + 4), an = rd16(udp_payload + 6);
    au_usize off = 12u;
    for (u16 i = 0; i < qd; ++i) {
        if (!dns_skip_name(udp_payload, len, &off) || off + 4u > len) return 0;
        off += 4u;
    }
    for (u16 i = 0; i < an; ++i) {
        if (!dns_skip_name(udp_payload, len, &off) || off + 10u > len) return 0;
        u16 type = rd16(udp_payload + off); u16 cls = rd16(udp_payload + off + 2); u16 rdlen = rd16(udp_payload + off + 8);
        off += 10u;
        if (off + rdlen > len) return 0;
        if (type == 1u && cls == 1u && rdlen == 4u) { *out_ip = rd32(udp_payload + off); return 1; }
        off += rdlen;
    }
    return 0;
}


static int resolve_name(netctl_ctx_t *ctx, const char *name, u32 *out_ip) {
    u32 dns_ip = ctx->conf.dns;
    u8 dst_mac[6];
    u8 frame[NETCTL_FRAME_MAX];
    if (!ctx || !name || !*name || !out_ip || !dns_ip) return 0;
    u32 hop = next_hop_ip(&ctx->conf, dns_ip);
    if (!resolve_arp(ctx, hop, dst_mac)) return 0;
    au_usize dns_off = NETCTL_ETH_LEN + NETCTL_IP_LEN + NETCTL_UDP_LEN;
    u8 *q = frame + dns_off;
    au_memset(frame, 0, sizeof(frame));
    u16 id = (u16)(au_ticks() ^ au_getpid() ^ 0x4d53u);
    wr16(q + 0, id); wr16(q + 2, 0x0100u); wr16(q + 4, 1); wr16(q + 6, 0); wr16(q + 8, 0); wr16(q + 10, 0);
    au_usize off = 12u;
    au_usize qn = dns_name_encode(name, q + off, 220u);
    if (!qn) return 0;
    off += qn; wr16(q + off, 1); off += 2; wr16(q + off, 1); off += 2;
    u16 sport = (u16)(49152u + (id & 1023u));
    au_usize frame_len = build_ip_udp(frame, dst_mac, ctx->mac, ctx->conf.ip, dns_ip, sport, 53u, off);
    if (!send_frame(ctx->fd, frame, frame_len)) return 0;
    au_i64 start = au_ticks();
    for (;;) {
        au_usize got = 0;
        int rr = recv_frame(ctx->fd, frame, sizeof(frame), &got);
        if (rr > 0 && got >= NETCTL_ETH_LEN + NETCTL_IP_LEN + NETCTL_UDP_LEN && rd16(frame + 12) == NETCTL_ET_IP) {
            au_usize ip = NETCTL_ETH_LEN;
            au_usize ihl = (au_usize)(frame[ip] & 15u) * 4u;
            if ((frame[ip] >> 4u) == 4u && frame[ip + 9] == NETCTL_PROTO_UDP && got >= NETCTL_ETH_LEN + ihl + NETCTL_UDP_LEN) {
                au_usize udp = NETCTL_ETH_LEN + ihl;
                if (rd32(frame + ip + 12) == dns_ip && rd16(frame + udp + 2) == sport) {
                    if (parse_dns_response(frame + udp + NETCTL_UDP_LEN, got - udp - NETCTL_UDP_LEN, id, out_ip)) return 1;
                }
            }
        }
        au_i64 now = au_ticks();
        if (now >= start && (u64)(now - start) > NETCTL_DEFAULT_TIMEOUT_TICKS) return 0;
        (void)au_sleep(1);
    }
}

static int cmd_dns(netctl_ctx_t *ctx, const char *name) {
    u32 dns_ip = ctx->conf.dns;
    u32 answer = 0;
    u8 dst_mac[6];
    u8 frame[NETCTL_FRAME_MAX];
    if (!name || !*name) { err("usage: netctl dns name\n"); return 2; }
    if (!ensure_config(ctx)) return 3;
    if (!dns_ip) { err("dns: no resolver\n"); return 4; }
    u32 hop = next_hop_ip(&ctx->conf, dns_ip);
    if (!resolve_arp(ctx, hop, dst_mac)) { err("dns: arp failed\n"); return 5; }
    au_usize dns_off = NETCTL_ETH_LEN + NETCTL_IP_LEN + NETCTL_UDP_LEN;
    u8 *q = frame + dns_off;
    au_memset(frame, 0, sizeof(frame));
    u16 id = (u16)(au_ticks() ^ au_getpid());
    wr16(q + 0, id); wr16(q + 2, 0x0100u); wr16(q + 4, 1); wr16(q + 6, 0); wr16(q + 8, 0); wr16(q + 10, 0);
    au_usize off = 12u;
    au_usize qn = dns_name_encode(name, q + off, 220u);
    if (!qn) { err("dns: invalid name\n"); return 6; }
    off += qn; wr16(q + off, 1); off += 2; wr16(q + off, 1); off += 2;
    au_usize frame_len = build_ip_udp(frame, dst_mac, ctx->mac, ctx->conf.ip, dns_ip, (u16)(49152u + (id & 1023u)), 53u, off);
    if (!send_frame(ctx->fd, frame, frame_len)) { err("dns: send failed\n"); return 7; }
    au_i64 start = au_ticks();
    for (;;) {
        au_usize got = 0;
        int rr = recv_frame(ctx->fd, frame, sizeof(frame), &got);
        if (rr > 0 && got >= NETCTL_ETH_LEN + NETCTL_IP_LEN + NETCTL_UDP_LEN && rd16(frame + 12) == NETCTL_ET_IP) {
            au_usize ip = NETCTL_ETH_LEN;
            au_usize ihl = (au_usize)(frame[ip] & 15u) * 4u;
            if ((frame[ip] >> 4u) == 4u && frame[ip + 9] == NETCTL_PROTO_UDP && got >= NETCTL_ETH_LEN + ihl + NETCTL_UDP_LEN) {
                au_usize udp = NETCTL_ETH_LEN + ihl;
                if (rd32(frame + ip + 12) == dns_ip && rd16(frame + udp + 2) == (u16)(49152u + (id & 1023u))) {
                    if (parse_dns_response(frame + udp + NETCTL_UDP_LEN, got - udp - NETCTL_UDP_LEN, id, &answer)) {
                        char s[32]; s[0] = 0; ip_str(answer, s, sizeof(s)); out(name); out(" A "); out(s); out("\n"); return 0;
                    }
                }
            }
        }
        au_i64 now = au_ticks();
        if (now >= start && (u64)(now - start) > NETCTL_DEFAULT_TIMEOUT_TICKS) { err("dns: timeout\n"); return 8; }
        (void)au_sleep(1);
    }
}

static int cmd_status(netctl_ctx_t *ctx) {
    char mac[24]; mac[0] = 0; mac_str(ctx->mac, mac, sizeof(mac));
    out("iface: "); out(ctx->dev_path); out(" mac "); out(mac); out("\n");
    print_config(&ctx->conf);
    (void)print_kernel_net_status();
    return 0;
}

static void usage(void) {
    out("usage:\n");
    out("  netctl status\n");
    out("  netctl dhcp\n");
    out("  netctl ip A.B.C.D/PREFIX [gw A.B.C.D] [dns A.B.C.D]\n");
    out("  netctl ping HOST|A.B.C.D\n");
    out("  netctl arp A.B.C.D\n");
    out("  netctl dns name\n");
}

static const char *base_name(const char *s) {
    const char *b = s;
    if (!s) return "netctl";
    for (const char *p = s; *p; ++p) if (*p == '/') b = p + 1;
    return b;
}

int main(int argc, char **argv) {
    const char *argv0 = argc > 0 ? base_name(argv[0]) : "netctl";
    const char *cmd = argc > 1 ? argv[1] : "status";
    int cmd_index = 1;
    if (streq(argv0, "ping")) { cmd = "ping"; cmd_index = 0; }
    else if (streq(argv0, "dhcp") || streq(argv0, "ifup")) { cmd = "dhcp"; cmd_index = 0; }
    else if (streq(argv0, "ip")) { cmd = "ip"; cmd_index = 0; }
    else if (streq(argv0, "arp")) { cmd = "arp"; cmd_index = 0; }
    netctl_ctx_t ctx;
    int status_cmd = streq(cmd, "status") || streq(cmd, "show");
    if (!open_ctx(&ctx, "/dev/net0")) {
        if (status_cmd) (void)print_kernel_net_status();
        return 1;
    }
    int rc = 0;
    if (status_cmd) rc = cmd_status(&ctx);
    else if (streq(cmd, "dhcp") || streq(cmd, "up")) rc = cmd_dhcp(&ctx);
    else if (streq(cmd, "ping")) rc = cmd_ping(&ctx, argc > cmd_index + 1 ? argv[cmd_index + 1] : 0);
    else if (streq(cmd, "arp")) rc = cmd_arp(&ctx, argc > cmd_index + 1 ? argv[cmd_index + 1] : 0);
    else if (streq(cmd, "dns") || streq(cmd, "resolve")) rc = cmd_dns(&ctx, argc > cmd_index + 1 ? argv[cmd_index + 1] : 0);
    else if (streq(cmd, "ip") || streq(cmd, "addr")) rc = cmd_ip(&ctx, argc - cmd_index - 1, argv + cmd_index + 1);
    else { usage(); rc = 2; }
    close_ctx(&ctx);
    return rc;
}
