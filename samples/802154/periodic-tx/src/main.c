#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(wpan_direct_tx, LOG_LEVEL_INF);

/* Use the default chosen 802.15.4 device */
static const struct device *radio_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));

static struct ieee802154_radio_api *radio_api;

static uint8_t src_mac_addr[8];
static uint8_t dst_mac_addr[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; /* Broadcast */

static uint32_t packet_counter = 0;
static uint16_t pan_id = 0x1234; /* PAN ID */
static uint8_t channel = 11; /* Channel 11-26 */

/* IEEE 802.15.4 frame structure */
struct ieee802154_frame {
    uint8_t frame_control[2];
    uint8_t sequence;
    uint8_t dest_pan_id[2];
    uint8_t dest_addr[8];
    uint8_t src_addr[8];
    uint8_t payload[];
} __packed;

static uint8_t *get_mac(const struct device *dev)
{
    src_mac_addr[7] = 0x00;
    src_mac_addr[6] = 0x12;
    src_mac_addr[5] = 0x4b;
    src_mac_addr[4] = 0x00;

    sys_rand_get(src_mac_addr, 4U);

    /* Set local administration bit */
    src_mac_addr[0] = (src_mac_addr[0] & ~0x01) | 0x02;

    return src_mac_addr;
}

static bool init_radio(void)
{
    if (!device_is_ready(radio_dev)) {
        LOG_ERR("Radio device not ready");
        return false;
    }

    radio_api = (struct ieee802154_radio_api *)radio_dev->api;

    /* Generate MAC address */
    get_mac(radio_dev);

    /* Set channel */
    if (radio_api->set_channel(radio_dev, channel) != 0) {
        LOG_ERR("Failed to set channel %d", channel);
        return false;
    }

    /* Set PAN ID if supported */
    if (IEEE802154_HW_FILTER & radio_api->get_capabilities(radio_dev)) {
        struct ieee802154_filter filter = {0};
        
        filter.pan_id = pan_id;
        filter.ieee_addr = src_mac_addr;
        
        radio_api->filter(radio_dev, true, IEEE802154_FILTER_TYPE_PAN_ID, &filter);
        radio_api->filter(radio_dev, true, IEEE802154_FILTER_TYPE_IEEE_ADDR, &filter);
    }

    /* Start the radio */
    if (radio_api->start(radio_dev) != 0) {
        LOG_ERR("Failed to start radio");
        return false;
    }

    return true;
}

static int create_802154_frame(uint8_t *buffer, size_t buffer_size, 
                              const uint8_t *payload, size_t payload_len)
{
    if (buffer_size < (23 + payload_len)) { /* Minimum frame size + payload */
        return -ENOMEM;
    }

    size_t offset = 0;
    
    /* Frame Control Field (2 bytes) */
    /* Frame Type: Data (001), Security: No, Frame Pending: No, AR: No, 
       PAN ID Compression: No, Dest Addr Mode: Extended (11), Src Addr Mode: Extended (11) */
    buffer[offset++] = 0x41; /* Frame control byte 1: 01000001 */
    buffer[offset++] = 0xCC; /* Frame control byte 2: 11001100 */
    
    /* Sequence Number */
    buffer[offset++] = packet_counter & 0xFF;
    
    /* Destination PAN ID (2 bytes, little endian) */
    buffer[offset++] = pan_id & 0xFF;
    buffer[offset++] = (pan_id >> 8) & 0xFF;
    
    /* Destination Address (8 bytes) - Broadcast */
    memcpy(&buffer[offset], dst_mac_addr, 8);
    offset += 8;
    
    /* Source Address (8 bytes) */
    memcpy(&buffer[offset], src_mac_addr, 8);
    offset += 8;
    
    /* Payload */
    if (payload && payload_len > 0) {
        memcpy(&buffer[offset], payload, payload_len);
        offset += payload_len;
    }
    
    return offset; /* Return total frame length */
}

static int send_packet(void)
{
    uint8_t frame_buffer[127]; /* Maximum 802.15.4 frame size */
    uint8_t payload[32];
    int frame_len;
    int ret;

    /* Prepare payload with packet counter and timestamp */
    int payload_len = snprintf(payload, sizeof(payload), 
                              "PKT#%08u TIME:%u", packet_counter++, k_uptime_get_32());

    /* Create 802.15.4 frame */
    frame_len = create_802154_frame(frame_buffer, sizeof(frame_buffer), 
                                   payload, payload_len);
    if (frame_len < 0) {
        LOG_ERR("Failed to create frame: %d", frame_len);
        return frame_len;
    }

    /* Transmit the frame directly through radio API */
    ret = radio_api->tx(radio_dev, IEEE802154_TX_MODE_DIRECT, 
                       frame_buffer, frame_len);
    
    if (ret == 0) {
        LOG_INF("Transmitted packet #%u (%d bytes): %s", 
                packet_counter - 1, frame_len, payload);
    } else {
        LOG_ERR("Transmission failed: %d", ret);
    }

    return ret;
}

int main(void)
{
    LOG_INF("Starting Direct IEEE 802.15.4 Transmitter");

    if (!init_radio()) {
        LOG_ERR("Failed to initialize radio");
        return -1;
    }

    LOG_INF("Radio initialized on channel %d, PAN ID: 0x%04X", channel, pan_id);
    LOG_INF("Source MAC: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
            src_mac_addr[0], src_mac_addr[1], src_mac_addr[2], src_mac_addr[3],
            src_mac_addr[4], src_mac_addr[5], src_mac_addr[6], src_mac_addr[7]);

    /* Main transmission loop - send 1 packet per second */
    while (1) {
        int ret = send_packet();
        if (ret != 0) {
            LOG_WRN("Packet transmission failed: %d", ret);
        }

        /* Wait 1 second before next transmission */
        k_sleep(K_SECONDS(1));
    }

    return 0;
}