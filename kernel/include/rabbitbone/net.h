#ifndef RABBITBONE_NET_H
#define RABBITBONE_NET_H
#include <rabbitbone/types.h>
#include <rabbitbone/spinlock.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define NET_MAX_DEVS 4u
#define NET_NAME_LEN 16u
#define NET_ETH_ADDR_LEN 6u
#define NET_ETH_HEADER_LEN 14u
#define NET_ETH_MIN_FRAME 60u
#define NET_ETH_MAX_FRAME 2048u
#define NET_RX_BACKLOG 64u

#define NETDEV_F_UP             0x00000001u
#define NETDEV_F_RUNNING        0x00000002u
#define NETDEV_F_LINK_UP        0x00000004u
#define NETDEV_F_PROMISC        0x00000008u
#define NETDEV_F_ALLMULTI       0x00000010u
#define NETDEV_F_BROADCAST      0x00000020u
#define NETDEV_F_MULTICAST      0x00000040u
#define NETDEV_F_VLAN_FILTER    0x00000080u
#define NETDEV_F_RX_CSUM        0x00000100u
#define NETDEV_F_TX_CSUM        0x00000200u
#define NETDEV_F_TSO            0x00000400u
#define NETDEV_F_IRQ            0x00000800u

#define NETDEV_CAP_HW_CSUM      0x00000001u
#define NETDEV_CAP_TSO          0x00000002u
#define NETDEV_CAP_VLAN         0x00000004u
#define NETDEV_CAP_PROMISC      0x00000008u
#define NETDEV_CAP_ALLMULTI     0x00000010u
#define NETDEV_CAP_MULTICAST    0x00000020u
#define NETDEV_CAP_JUMBO        0x00000040u
#define NETDEV_CAP_IRQ          0x00000080u
#define NETDEV_CAP_POLL         0x00000100u

#define NET_LINK_HALF 1u
#define NET_LINK_FULL 2u

#define NET_RX_OK      0x00000001u
#define NET_RX_CSUM_OK 0x00000002u
#define NET_RX_VLAN    0x00000004u

#define NET_TX_CSUM    0x00000001u
#define NET_TX_TSO     0x00000002u
#define NET_TX_VLAN    0x00000004u

typedef enum net_status {
    NET_OK = 0,
    NET_ERR_INVAL = -1,
    NET_ERR_NOMEM = -2,
    NET_ERR_NODEV = -3,
    NET_ERR_BUSY = -4,
    NET_ERR_RANGE = -5,
    NET_ERR_IO = -6,
    NET_ERR_UNSUPPORTED = -7,
} net_status_t;

typedef struct net_stats {
    u64 rx_packets;
    u64 tx_packets;
    u64 rx_bytes;
    u64 tx_bytes;
    u64 rx_errors;
    u64 tx_errors;
    u64 rx_dropped;
    u64 tx_dropped;
    u64 rx_truncated;
    u64 rx_multicast;
    u64 rx_broadcast;
    u64 tx_busy;
    u64 tx_timeouts;
    u64 interrupts;
    u64 polls;
    u64 link_changes;
    u64 rx_queue_high_water;
    u64 resets;
    u64 dma_errors;
    u64 csum_rx_good;
    u64 csum_tx_offloaded;
    u64 vlan_rx;
    u64 vlan_tx;
} net_stats_t;

typedef struct net_rx_meta {
    u16 vlan_tag;
    u16 protocol;
    u32 flags;
} net_rx_meta_t;

typedef struct net_tx_meta {
    u16 vlan_tag;
    u16 mss;
    u16 header_len;
    u16 protocol;
    u32 flags;
} net_tx_meta_t;

struct net_device;
typedef void (*net_rx_callback_t)(struct net_device *dev, const void *frame, usize len, const net_rx_meta_t *meta, void *ctx);

typedef struct net_device_ops {
    net_status_t (*open)(struct net_device *dev);
    void (*close)(struct net_device *dev);
    net_status_t (*transmit)(struct net_device *dev, const void *frame, usize len, const net_tx_meta_t *meta);
    void (*poll)(struct net_device *dev);
    net_status_t (*set_promisc)(struct net_device *dev, bool enabled);
    net_status_t (*set_allmulti)(struct net_device *dev, bool enabled);
    net_status_t (*set_mac)(struct net_device *dev, const u8 mac[NET_ETH_ADDR_LEN]);
    net_status_t (*set_mtu)(struct net_device *dev, u16 mtu);
    net_status_t (*set_vlan)(struct net_device *dev, u16 vlan, bool enabled);
    void (*update_link)(struct net_device *dev);
    void (*format_driver)(struct net_device *dev, char *out, usize out_len);
} net_device_ops_t;

typedef struct net_frame_slot {
    u16 len;
    net_rx_meta_t meta;
    u8 data[NET_ETH_MAX_FRAME];
} net_frame_slot_t;

typedef struct net_device {
    char name[NET_NAME_LEN];
    const char *driver;
    u8 mac[NET_ETH_ADDR_LEN];
    u16 mtu;
    u16 max_mtu;
    u32 flags;
    u32 caps;
    u32 link_speed_mbps;
    u32 link_duplex;
    u32 tx_queue_len;
    u32 rx_queue_len;
    void *ctx;
    const net_device_ops_t *ops;
    net_stats_t stats;
    spinlock_t lock;
    net_frame_slot_t *rx_queue;
    u16 rx_head;
    u16 rx_tail;
    u16 rx_count;
    net_rx_callback_t rx_callback;
    void *rx_callback_ctx;
} net_device_t;

void net_init(void);
net_status_t netdev_register(net_device_t *dev);
usize netdev_count(void);
net_device_t *netdev_get(usize index);
net_device_t *netdev_find(const char *name);
net_status_t netdev_open(net_device_t *dev);
void netdev_close(net_device_t *dev);
net_status_t netdev_send(net_device_t *dev, const void *frame, usize len, const net_tx_meta_t *meta);
net_status_t netdev_receive(net_device_t *dev, const void *frame, usize len, const net_rx_meta_t *meta);
net_status_t netdev_read_frame(net_device_t *dev, void *buffer, usize size, usize *read_out, net_rx_meta_t *meta_out);
net_status_t netdev_write_frame(net_device_t *dev, const void *buffer, usize size, usize *written_out);
void netdev_poll(net_device_t *dev);
void net_poll_all(void);
net_status_t netdev_set_promisc(net_device_t *dev, bool enabled);
net_status_t netdev_set_allmulti(net_device_t *dev, bool enabled);
net_status_t netdev_set_mtu(net_device_t *dev, u16 mtu);
net_status_t netdev_set_vlan(net_device_t *dev, u16 vlan, bool enabled);
void netdev_set_link(net_device_t *dev, bool up, u32 speed_mbps, u32 duplex);
void netdev_set_rx_callback(net_device_t *dev, net_rx_callback_t callback, void *ctx);
const char *net_status_name(net_status_t status);
void net_format_status(char *out, usize out_len);
void net_log_devices(void);
bool net_mac_is_zero(const u8 mac[NET_ETH_ADDR_LEN]);
bool net_mac_is_multicast(const u8 mac[NET_ETH_ADDR_LEN]);
void net_format_mac(const u8 mac[NET_ETH_ADDR_LEN], char *out, usize out_len);

void rabbit_eth_init(void);
void rabbit_eth_poll(void);

#if defined(__cplusplus)
}
#endif
#endif
