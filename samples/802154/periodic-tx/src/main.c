#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(periodic_tx, LOG_LEVEL_INF);

static const struct device *radio_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));
static const struct ieee802154_radio_api *radio_api;

static uint8_t src_mac_addr[8];
static uint16_t pan_id = 0x1234;
static uint8_t channel = 19;

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
    if (!device_is_ready(radio_dev)) {
        LOG_ERR("Radio device not ready");
        return false;
    }
    
    radio_api = (const struct ieee802154_radio_api *)radio_dev->api;

    get_mac(radio_dev);

    if (radio_api->set_channel(radio_dev, channel) != 0) {
        LOG_ERR("Failed to set channel");
        return false;
    }

    enum ieee802154_hw_caps caps = radio_api->get_capabilities(radio_dev);
    LOG_INF("Radio capabilities: 0x%08x", caps);
    
    if (caps & IEEE802154_HW_FILTER) {
        struct ieee802154_filter f = {0};
        f.pan_id = pan_id;
        (void)radio_api->filter(radio_dev, true, IEEE802154_FILTER_TYPE_PAN_ID, &f);

        f.ieee_addr = src_mac_addr;
        (void)radio_api->filter(radio_dev, true, IEEE802154_FILTER_TYPE_IEEE_ADDR, &f);
    }

    // **CRITICAL**: Bring the network interface UP
    struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(IEEE802154));
    if (!iface) {
        LOG_ERR("No IEEE 802.15.4 interface found");
        return false;
    }
    
    LOG_INF("Bringing IEEE 802.15.4 interface UP");
    int ret = net_if_up(iface);
    if (ret != 0) {
        LOG_ERR("Failed to bring interface UP: %d", ret);
        return false;
    }
    
    // Verify interface is up
    if (!net_if_is_up(iface)) {
        LOG_ERR("Failed to bring interface UP");
        return false;
    }

    if (radio_api->start(radio_dev) != 0) {
        LOG_ERR("Failed to start radio");
        return false;
    }
    
    LOG_INF("Radio initialization completed successfully");
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

    int paylen = snprintf((char *)payload, sizeof(payload),
                          "PKT#%08u TIME:%u", packet_counter++, k_uptime_get_32());
    if (paylen < 0) return -EIO;

    int flen = create_802154_frame(frame, sizeof(frame), payload, (size_t)paylen);
    if (flen < 0) return flen;

    /* Create proper network packet */
    struct net_pkt *pkt = net_pkt_alloc_with_buffer(NULL, flen, AF_UNSPEC, 0, K_NO_WAIT);
    if (!pkt) {
        LOG_ERR("Failed to allocate packet");
        return -ENOMEM;
    }

    /* Copy frame data to packet */
    if (net_pkt_write(pkt, frame, flen) < 0) {
        LOG_ERR("Failed to write to packet");
        net_pkt_unref(pkt);
        return -EIO;
    }

    /* Direct transmission using radio API - let driver use pkt->frags */
    // int ret = radio_api->tx(radio_dev, IEEE802154_TX_MODE_DIRECT, pkt, NULL);
    int ret = -EIO;

    if (ret) {
        LOG_INF("TX #%u len=%d \"%.*s\"", packet_counter - 1, flen, paylen, payload);
    } else {
        LOG_ERR("TX failed: %d", ret);
        /* Only unref on failure - driver handles success case */
        net_pkt_unref(pkt);
    }
    
    return ret;
}

#ifndef CONFIG_NET_L2_IEEE802154
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
#endif // CONFIG_NET_L2_IEEE802154

/* Shell commands for controlling the transmitter */
static int cmd_send_packet(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    
    int ret = send_packet();
    if (ret == 0) {
        shell_print(sh, "Packet sent successfully");
    } else {
        shell_print(sh, "Failed to send packet: %d", ret);
    }
    return 0;
}

static int cmd_set_channel(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_print(sh, "Usage: set_channel <11-26>");
        return -EINVAL;
    }
    
    int new_channel = atoi(argv[1]);
    if (new_channel < 11 || new_channel > 26) {
        shell_print(sh, "Channel must be between 11 and 26");
        return -EINVAL;
    }
    
    if (radio_api->set_channel(radio_dev, new_channel) != 0) {
        shell_print(sh, "Failed to set channel");
        return -EIO;
    }
    
    channel = new_channel;
    shell_print(sh, "Channel set to %d", channel);
    return 0;
}

static int cmd_set_pan_id(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_print(sh, "Usage: set_pan_id <0x0000-0xFFFF>");
        return -EINVAL;
    }
    
    uint16_t new_pan_id = strtoul(argv[1], NULL, 0);
    
    if (radio_api->get_capabilities(radio_dev) & IEEE802154_HW_FILTER) {
        struct ieee802154_filter f = {0};
        f.pan_id = new_pan_id;
        if (radio_api->filter(radio_dev, true, IEEE802154_FILTER_TYPE_PAN_ID, &f) != 0) {
            shell_print(sh, "Failed to set PAN ID filter");
            return -EIO;
        }
    }
    
    pan_id = new_pan_id;
    shell_print(sh, "PAN ID set to 0x%04x", pan_id);
    return 0;
}

static int cmd_show_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    
    shell_print(sh, "=== IEEE 802.15.4 Transmitter Status ===");
    shell_print(sh, "Channel: %d", channel);
    shell_print(sh, "PAN ID: 0x%04x", pan_id);
    shell_print(sh, "Source MAC: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                src_mac_addr[0], src_mac_addr[1], src_mac_addr[2], src_mac_addr[3],
                src_mac_addr[4], src_mac_addr[5], src_mac_addr[6], src_mac_addr[7]);
    shell_print(sh, "Packets sent: %u", packet_counter);
    shell_print(sh, "Current sequence: %u", seq_no);
    return 0;
}

static int cmd_reset_counter(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    
    packet_counter = 0;
    seq_no = 0;
    shell_print(sh, "Packet counter and sequence number reset");
    return 0;
}

/* Shell command structure */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_tx,
    SHELL_CMD(send, NULL, "Send a single packet", cmd_send_packet),
    SHELL_CMD(channel, NULL, "Set channel (11-26)", cmd_set_channel),
    SHELL_CMD(pan_id, NULL, "Set PAN ID (hex)", cmd_set_pan_id),
    SHELL_CMD(status, NULL, "Show transmitter status", cmd_show_status),
    SHELL_CMD(reset, NULL, "Reset packet counter", cmd_reset_counter),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(tx, &sub_tx, "IEEE 802.15.4 TX commands", NULL);

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
        // (void)send_packet();
        k_sleep(K_SECONDS(1));
    }
}