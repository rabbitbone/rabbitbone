#include <rabbitbone/net.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>
#include <rabbitbone/console.h>

static net_device_t *net_devices[NET_MAX_DEVS];
static usize net_device_count;
static spinlock_t net_lock;
static bool net_ready;

static bool net_frame_len_ok(usize len) {
    return len >= NET_ETH_HEADER_LEN && len <= NET_ETH_MAX_FRAME;
}

bool net_mac_is_zero(const u8 mac[NET_ETH_ADDR_LEN]) {
    if (!mac) return true;
    u8 v = 0;
    for (u32 i = 0; i < NET_ETH_ADDR_LEN; ++i) v |= mac[i];
    return v == 0;
}

bool net_mac_is_multicast(const u8 mac[NET_ETH_ADDR_LEN]) {
    return mac && ((mac[0] & 1u) != 0u);
}

void net_format_mac(const u8 mac[NET_ETH_ADDR_LEN], char *out, usize out_len) {
    if (!out || out_len == 0) return;
    if (!mac) {
        strlcpy(out, "00:00:00:00:00:00", out_len);
        return;
    }
    ksnprintf(out, out_len, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void net_init(void) {
    u64 flags = spin_lock_irqsave(&net_lock);
    memset(net_devices, 0, sizeof(net_devices));
    net_device_count = 0;
    net_ready = true;
    spin_unlock_irqrestore(&net_lock, flags);
}

net_status_t netdev_register(net_device_t *dev) {
    if (!dev || !dev->ops || !dev->ops->transmit || !dev->driver || dev->max_mtu < 576u) return NET_ERR_INVAL;
    if (!net_ready) net_init();
    u64 flags = spin_lock_irqsave(&net_lock);
    if (net_device_count >= NET_MAX_DEVS) {
        spin_unlock_irqrestore(&net_lock, flags);
        return NET_ERR_BUSY;
    }
    for (usize i = 0; i < net_device_count; ++i) {
        if (net_devices[i] == dev) {
            spin_unlock_irqrestore(&net_lock, flags);
            return NET_ERR_BUSY;
        }
    }
    usize index = net_device_count;
    spinlock_init(&dev->lock);
    if (dev->name[0] == 0) ksnprintf(dev->name, sizeof(dev->name), "net%llu", (unsigned long long)index);
    if (dev->mtu == 0) dev->mtu = 1500u;
    if (dev->mtu > dev->max_mtu) dev->mtu = dev->max_mtu;
    if (dev->rx_queue_len == 0 || dev->rx_queue_len > NET_RX_BACKLOG) dev->rx_queue_len = NET_RX_BACKLOG;
    if (dev->tx_queue_len == 0) dev->tx_queue_len = 64u;
    dev->rx_queue = (net_frame_slot_t *)kcalloc(dev->rx_queue_len, sizeof(net_frame_slot_t));
    if (!dev->rx_queue) {
        spin_unlock_irqrestore(&net_lock, flags);
        return NET_ERR_NOMEM;
    }
    net_devices[net_device_count++] = dev;
    spin_unlock_irqrestore(&net_lock, flags);

    char mac[24];
    net_format_mac(dev->mac, mac, sizeof(mac));
    KLOG(LOG_INFO, "net", "registered %s driver=%s mac=%s mtu=%u caps=0x%x", dev->name, dev->driver, mac, dev->mtu, dev->caps);
    return NET_OK;
}

usize netdev_count(void) { return net_device_count; }

net_device_t *netdev_get(usize index) {
    return index < net_device_count ? net_devices[index] : 0;
}

net_device_t *netdev_find(const char *name) {
    if (!name) return 0;
    for (usize i = 0; i < net_device_count; ++i) {
        net_device_t *dev = net_devices[i];
        if (dev && strcmp(dev->name, name) == 0) return dev;
    }
    return 0;
}

net_status_t netdev_open(net_device_t *dev) {
    if (!dev) return NET_ERR_NODEV;
    net_status_t st = NET_OK;
    if (dev->ops && dev->ops->open) st = dev->ops->open(dev);
    if (st == NET_OK) {
        u64 flags = spin_lock_irqsave(&dev->lock);
        dev->flags |= NETDEV_F_UP;
        spin_unlock_irqrestore(&dev->lock, flags);
    }
    return st;
}

void netdev_close(net_device_t *dev) {
    if (!dev) return;
    if (dev->ops && dev->ops->close) dev->ops->close(dev);
    u64 flags = spin_lock_irqsave(&dev->lock);
    dev->flags &= ~(NETDEV_F_UP | NETDEV_F_RUNNING);
    spin_unlock_irqrestore(&dev->lock, flags);
}

net_status_t netdev_send(net_device_t *dev, const void *frame, usize len, const net_tx_meta_t *meta) {
    if (!dev || !frame) return NET_ERR_INVAL;
    if (!net_frame_len_ok(len)) return NET_ERR_RANGE;
    if ((dev->flags & NETDEV_F_UP) == 0) return NET_ERR_NODEV;
    net_status_t st = dev->ops->transmit(dev, frame, len, meta);
    u64 flags = spin_lock_irqsave(&dev->lock);
    if (st == NET_OK) {
        ++dev->stats.tx_packets;
        dev->stats.tx_bytes += len;
        if (meta && (meta->flags & NET_TX_CSUM)) ++dev->stats.csum_tx_offloaded;
        if (meta && (meta->flags & NET_TX_VLAN)) ++dev->stats.vlan_tx;
    } else if (st == NET_ERR_BUSY) {
        ++dev->stats.tx_busy;
    } else {
        ++dev->stats.tx_errors;
    }
    spin_unlock_irqrestore(&dev->lock, flags);
    return st;
}

static void net_count_rx_address_stats(net_device_t *dev, const u8 *frame) {
    if (!dev || !frame) return;
    static const u8 broadcast[NET_ETH_ADDR_LEN] = { 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu };
    if (memcmp(frame, broadcast, sizeof(broadcast)) == 0) ++dev->stats.rx_broadcast;
    else if (net_mac_is_multicast(frame)) ++dev->stats.rx_multicast;
}

net_status_t netdev_receive(net_device_t *dev, const void *frame, usize len, const net_rx_meta_t *meta) {
    if (!dev || !frame) return NET_ERR_INVAL;
    if (!net_frame_len_ok(len)) return NET_ERR_RANGE;
    if (dev->rx_callback) dev->rx_callback(dev, frame, len, meta, dev->rx_callback_ctx);

    u64 flags = spin_lock_irqsave(&dev->lock);
    ++dev->stats.rx_packets;
    dev->stats.rx_bytes += len;
    if (meta && (meta->flags & NET_RX_CSUM_OK)) ++dev->stats.csum_rx_good;
    if (meta && (meta->flags & NET_RX_VLAN)) ++dev->stats.vlan_rx;
    net_count_rx_address_stats(dev, (const u8 *)frame);
    if (!dev->rx_queue || dev->rx_queue_len == 0 || dev->rx_count >= dev->rx_queue_len) {
        ++dev->stats.rx_dropped;
        spin_unlock_irqrestore(&dev->lock, flags);
        return NET_ERR_BUSY;
    }
    net_frame_slot_t *slot = &dev->rx_queue[dev->rx_tail];
    slot->len = (u16)len;
    slot->meta = meta ? *meta : (net_rx_meta_t){ 0, 0, NET_RX_OK };
    memcpy(slot->data, frame, len);
    dev->rx_tail = (u16)((dev->rx_tail + 1u) % dev->rx_queue_len);
    ++dev->rx_count;
    if (dev->rx_count > dev->stats.rx_queue_high_water) dev->stats.rx_queue_high_water = dev->rx_count;
    spin_unlock_irqrestore(&dev->lock, flags);
    return NET_OK;
}

net_status_t netdev_read_frame(net_device_t *dev, void *buffer, usize size, usize *read_out, net_rx_meta_t *meta_out) {
    if (read_out) *read_out = 0;
    if (!dev || (size && !buffer)) return NET_ERR_INVAL;
    netdev_poll(dev);
    u64 flags = spin_lock_irqsave(&dev->lock);
    if (dev->rx_count == 0 || !dev->rx_queue) {
        spin_unlock_irqrestore(&dev->lock, flags);
        return NET_OK;
    }
    net_frame_slot_t *slot = &dev->rx_queue[dev->rx_head];
    usize take = slot->len;
    if (take > size) {
        take = size;
        ++dev->stats.rx_truncated;
    }
    if (take) memcpy(buffer, slot->data, take);
    if (meta_out) *meta_out = slot->meta;
    if (read_out) *read_out = take;
    dev->rx_head = (u16)((dev->rx_head + 1u) % dev->rx_queue_len);
    --dev->rx_count;
    spin_unlock_irqrestore(&dev->lock, flags);
    return NET_OK;
}

net_status_t netdev_write_frame(net_device_t *dev, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    net_status_t st = netdev_send(dev, buffer, size, 0);
    if (st == NET_OK && written_out) *written_out = size;
    return st;
}

void netdev_poll(net_device_t *dev) {
    if (!dev) return;
    if (dev->ops && dev->ops->poll) dev->ops->poll(dev);
    u64 flags = spin_lock_irqsave(&dev->lock);
    ++dev->stats.polls;
    spin_unlock_irqrestore(&dev->lock, flags);
}

void net_poll_all(void) {
    for (usize i = 0; i < net_device_count; ++i) netdev_poll(net_devices[i]);
}

net_status_t netdev_set_promisc(net_device_t *dev, bool enabled) {
    if (!dev) return NET_ERR_NODEV;
    net_status_t st = NET_OK;
    if (dev->ops && dev->ops->set_promisc) st = dev->ops->set_promisc(dev, enabled);
    else if (enabled) st = NET_ERR_UNSUPPORTED;
    if (st == NET_OK) {
        u64 flags = spin_lock_irqsave(&dev->lock);
        if (enabled) dev->flags |= NETDEV_F_PROMISC;
        else dev->flags &= ~NETDEV_F_PROMISC;
        spin_unlock_irqrestore(&dev->lock, flags);
    }
    return st;
}

net_status_t netdev_set_allmulti(net_device_t *dev, bool enabled) {
    if (!dev) return NET_ERR_NODEV;
    net_status_t st = NET_OK;
    if (dev->ops && dev->ops->set_allmulti) st = dev->ops->set_allmulti(dev, enabled);
    else if (enabled) st = NET_ERR_UNSUPPORTED;
    if (st == NET_OK) {
        u64 flags = spin_lock_irqsave(&dev->lock);
        if (enabled) dev->flags |= NETDEV_F_ALLMULTI;
        else dev->flags &= ~NETDEV_F_ALLMULTI;
        spin_unlock_irqrestore(&dev->lock, flags);
    }
    return st;
}

net_status_t netdev_set_mtu(net_device_t *dev, u16 mtu) {
    if (!dev) return NET_ERR_NODEV;
    if (mtu < 576u || mtu > dev->max_mtu) return NET_ERR_RANGE;
    net_status_t st = NET_OK;
    if (dev->ops && dev->ops->set_mtu) st = dev->ops->set_mtu(dev, mtu);
    if (st == NET_OK) {
        u64 flags = spin_lock_irqsave(&dev->lock);
        dev->mtu = mtu;
        spin_unlock_irqrestore(&dev->lock, flags);
    }
    return st;
}

net_status_t netdev_set_vlan(net_device_t *dev, u16 vlan, bool enabled) {
    if (!dev || vlan >= 4096u) return NET_ERR_INVAL;
    if (dev->ops && dev->ops->set_vlan) return dev->ops->set_vlan(dev, vlan, enabled);
    return enabled ? NET_ERR_UNSUPPORTED : NET_OK;
}

void netdev_set_link(net_device_t *dev, bool up, u32 speed_mbps, u32 duplex) {
    if (!dev) return;
    u64 flags = spin_lock_irqsave(&dev->lock);
    bool old = (dev->flags & NETDEV_F_LINK_UP) != 0;
    if (up) dev->flags |= NETDEV_F_LINK_UP | NETDEV_F_RUNNING;
    else dev->flags &= ~(NETDEV_F_LINK_UP | NETDEV_F_RUNNING);
    dev->link_speed_mbps = up ? speed_mbps : 0;
    dev->link_duplex = up ? duplex : 0;
    if (old != up) ++dev->stats.link_changes;
    spin_unlock_irqrestore(&dev->lock, flags);
}

void netdev_set_rx_callback(net_device_t *dev, net_rx_callback_t callback, void *ctx) {
    if (!dev) return;
    u64 flags = spin_lock_irqsave(&dev->lock);
    dev->rx_callback = callback;
    dev->rx_callback_ctx = ctx;
    spin_unlock_irqrestore(&dev->lock, flags);
}

const char *net_status_name(net_status_t status) {
    switch (status) {
        case NET_OK: return "ok";
        case NET_ERR_INVAL: return "invalid";
        case NET_ERR_NOMEM: return "no-memory";
        case NET_ERR_NODEV: return "no-device";
        case NET_ERR_BUSY: return "busy";
        case NET_ERR_RANGE: return "range";
        case NET_ERR_IO: return "io";
        case NET_ERR_UNSUPPORTED: return "unsupported";
        default: return "unknown";
    }
}

void net_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    rabbitbone_buf_appendf(&bo, "net: count=%llu\n", (unsigned long long)net_device_count);
    if (net_device_count == 0) {
        rabbitbone_buf_appendf(&bo, "  hint: VMware virtualDev must be e1000 or e1000e; expected PCI ids include 8086:10d3, 8086:100f, 8086:100e\n");
    }
    for (usize i = 0; i < net_device_count; ++i) {
        net_device_t *dev = net_devices[i];
        if (!dev) continue;
        if (dev->ops && dev->ops->update_link) dev->ops->update_link(dev);
        char mac[24];
        net_format_mac(dev->mac, mac, sizeof(mac));
        rabbitbone_buf_appendf(&bo,
            "  %s driver=%s mac=%s mtu=%u max_mtu=%u flags=0x%x caps=0x%x link=%u speed=%u duplex=%u rxq=%u/%u txqlen=%u\n",
            dev->name, dev->driver, mac, dev->mtu, dev->max_mtu, dev->flags, dev->caps,
            (dev->flags & NETDEV_F_LINK_UP) ? 1u : 0u, dev->link_speed_mbps, dev->link_duplex,
            dev->rx_count, dev->rx_queue_len, dev->tx_queue_len);
        rabbitbone_buf_appendf(&bo,
            "    rx_packets=%llu rx_bytes=%llu rx_errors=%llu rx_dropped=%llu rx_truncated=%llu rx_multicast=%llu rx_broadcast=%llu\n",
            (unsigned long long)dev->stats.rx_packets, (unsigned long long)dev->stats.rx_bytes,
            (unsigned long long)dev->stats.rx_errors, (unsigned long long)dev->stats.rx_dropped,
            (unsigned long long)dev->stats.rx_truncated, (unsigned long long)dev->stats.rx_multicast,
            (unsigned long long)dev->stats.rx_broadcast);
        rabbitbone_buf_appendf(&bo,
            "    tx_packets=%llu tx_bytes=%llu tx_errors=%llu tx_dropped=%llu tx_busy=%llu tx_timeouts=%llu interrupts=%llu polls=%llu resets=%llu dma_errors=%llu\n",
            (unsigned long long)dev->stats.tx_packets, (unsigned long long)dev->stats.tx_bytes,
            (unsigned long long)dev->stats.tx_errors, (unsigned long long)dev->stats.tx_dropped,
            (unsigned long long)dev->stats.tx_busy, (unsigned long long)dev->stats.tx_timeouts,
            (unsigned long long)dev->stats.interrupts, (unsigned long long)dev->stats.polls,
            (unsigned long long)dev->stats.resets, (unsigned long long)dev->stats.dma_errors);
        if (dev->ops && dev->ops->format_driver) dev->ops->format_driver(dev, bo.buf + bo.used, bo.cap > bo.used ? bo.cap - bo.used : 0u);
        bo.used = strnlen(bo.buf, bo.cap);
    }
}

void net_log_devices(void) {
    KLOG(LOG_INFO, "net", "registered devices=%llu", (unsigned long long)net_device_count);
    for (usize i = 0; i < net_device_count; ++i) {
        net_device_t *dev = net_devices[i];
        if (!dev) continue;
        char mac[24];
        net_format_mac(dev->mac, mac, sizeof(mac));
        KLOG(LOG_INFO, "net", "%s driver=%s mac=%s mtu=%u flags=0x%x caps=0x%x link=%u speed=%u",
             dev->name, dev->driver, mac, dev->mtu, dev->flags, dev->caps,
             (dev->flags & NETDEV_F_LINK_UP) ? 1u : 0u, dev->link_speed_mbps);
    }
}
