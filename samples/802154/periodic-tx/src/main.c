#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(wpan_periodic_tx, LOG_LEVEL_INF);

static const struct device *radio_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));
static const struct ieee802154_radio_api *radio_api;

static uint8_t src_mac_addr[8];
static uint16_t pan_id = 0x1234;
static uint8_t channel = 11;

static uint32_t packet_counter = 0;
static uint8_t seq_no = 0;

static uint8_t *get_mac(const struct device *dev) {
    src_mac_addr[7] = 0x00;
    src_mac_addr[6] = 0x12;
    src_mac_addr[5] = 0x4b;
    src_mac_addr[4] = 0x00;
    sys_rand_get(src_mac_addr, 4U);
    src_mac_addr[0] = (src_mac_addr[0] & ~0x01) | 0x02;
    return src_mac_addr;
}

static bool init_radio(void) {
    if (!device_is_ready(radio_dev)) return false;
    radio_api = (const struct ieee802154_radio_api *)radio_dev->api;

    get_mac(radio_dev);

    if (radio_api->set_channel(radio_dev, channel) != 0) return false;

    enum ieee802154_hw_caps caps = radio_api->get_capabilities(radio_dev);
    if (caps & IEEE802154_HW_FILTER) {
        struct ieee802154_filter f = {0};
        f.pan_id = pan_id;
        (void)radio_api->filter(radio_dev, true, IEEE802154_FILTER_TYPE_PAN_ID, &f);

        f.ieee_addr = src_mac_addr;
        (void)radio_api->filter(radio_dev, true, IEEE802154_FILTER_TYPE_IEEE_ADDR, &f);
    }

    if (radio_api->start(radio_dev) != 0) return false;
    return true;
}

static int create_802154_frame(uint8_t *buf, size_t buf_sz, const uint8_t *pl, size_t pl_len) {
    /* Data frame, PAN ID Compression=1, Dest=short(0b10), Src=extended(0b11) => FCF: 0x41 0xC8 */
    const uint8_t fcf0 = 0x41;
    const uint8_t fcf1 = 0xC8;

    const size_t hdr_len = 2 /*FCF*/ + 1 /*Seq*/ + 2 /*Dest PAN*/ + 2 /*Dest short*/ + 8 /*Src ext*/;
    if (pl_len + hdr_len > 127 - 2) return -EMSGSIZE; /* exclude FCS (added by HW) */
    if (buf_sz < hdr_len + pl_len) return -ENOMEM;

    size_t off = 0;
    buf[off++] = fcf0;
    buf[off++] = fcf1;
    buf[off++] = seq_no++;

    buf[off++] = (uint8_t)(pan_id & 0xFF);
    buf[off++] = (uint8_t)((pan_id >> 8) & 0xFF);

    buf[off++] = 0xFF;
    buf[off++] = 0xFF;

    memcpy(&buf[off], src_mac_addr, 8);
    off += 8;

    if (pl_len) {
        memcpy(&buf[off], pl, pl_len);
        off += pl_len;
    }

    return (int)off;
}

/* Simple buffer structure to hold frame data */
struct tx_buffer {
    uint8_t *data;
    size_t len;
};

static int send_packet(void) {
    static uint8_t frame[127];
    uint8_t payload[48];
    struct tx_buffer tx_buf;

    int paylen = snprintf((char *)payload, sizeof(payload),
                          "PKT#%08u TIME:%u", packet_counter++, k_uptime_get_32());
    if (paylen < 0) return -EIO;

    int flen = create_802154_frame(frame, sizeof(frame), payload, (size_t)paylen);
    if (flen < 0) return flen;

    /* Prepare buffer for transmission */
    tx_buf.data = frame;
    tx_buf.len = flen;

    /* Direct transmission using radio API */
    int ret = radio_api->tx(radio_dev, IEEE802154_TX_MODE_CSMA_CA, NULL, (void*)&tx_buf);

    if (!ret) {
        LOG_INF("TX #%u len=%d \"%.*s\"", packet_counter - 1, flen, paylen, payload);
    } else {
        LOG_ERR("TX failed: %d", ret);
    }
    return ret;
}

/* Required stub functions for nRF5 driver */
int net_recv_data(struct net_if *iface, struct net_pkt *pkt)
{
    if (pkt) {
        net_pkt_unref(pkt);
    }
    return 0;
}

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
    if (pkt) {
        net_pkt_unref(pkt);
    }
    return NET_CONTINUE;
}

int main(void) {
    LOG_INF("Starting 802.15.4 direct TX (no network stack)");
    if (!init_radio()) {
        LOG_ERR("Radio init failed");
        return -1;
    }

    LOG_INF("Ch:%u PAN:0x%04x SRC:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
            channel, pan_id,
            src_mac_addr[0], src_mac_addr[1], src_mac_addr[2], src_mac_addr[3],
            src_mac_addr[4], src_mac_addr[5], src_mac_addr[6], src_mac_addr[7]);

    for (;;) {
        (void)send_packet();
        k_sleep(K_SECONDS(1));
    }
}