#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <stdio.h>

LOG_MODULE_REGISTER(wpan_tx, LOG_LEVEL_INF);

/* Use the default chosen 802.15.4 device */
static const struct device *radio_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));

static struct ieee802154_radio_api *radio_api;
static struct net_if *iface;

static uint8_t src_mac_addr[8];
static uint8_t dst_mac_addr[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; /* Broadcast */

static uint32_t packet_counter = 0;

static uint8_t *get_mac(const struct device *dev)
{
	src_mac_addr[7] = 0x00;
	src_mac_addr[6] = 0x12;
	src_mac_addr[5] = 0x4b;
	src_mac_addr[4] = 0x00;

	sys_rand_get(src_mac_addr, 4U);

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

	get_mac(radio_dev);

	if (IEEE802154_HW_FILTER & radio_api->get_capabilities(radio_dev)) {
		struct ieee802154_filter filter;

		filter.ieee_addr = src_mac_addr;
		radio_api->filter(radio_dev, true,
			IEEE802154_FILTER_TYPE_IEEE_ADDR, &filter);
	}

#ifdef CONFIG_NET_CONFIG_SETTINGS
	radio_api->set_channel(radio_dev, CONFIG_NET_CONFIG_IEEE802154_CHANNEL);
#endif

	radio_api->start(radio_dev);

	/* Get the network interface */
	iface = net_if_get_first_by_type(&NET_L2_GET_NAME(IEEE802154));
	if (!iface) {
		LOG_ERR("No IEEE 802.15.4 network interface found");
		return false;
	}

	return true;
}

static int send_packet(void)
{
	struct net_pkt *pkt;
	struct net_buf *buf;
	uint8_t payload[32];
	int ret;

	/* Create a new packet */
	pkt = net_pkt_alloc_with_buffer(iface, sizeof(payload), AF_UNSPEC, 0, K_SECONDS(1));
	if (!pkt) {
		LOG_ERR("Failed to allocate packet");
		return -ENOMEM;
	}

	/* Prepare payload with packet counter and timestamp */
	snprintf(payload, sizeof(payload), "PKT#%08u TIME:%u", packet_counter++, k_uptime_get_32());

	/* Add payload to packet */
	if (net_pkt_write(pkt, payload, strlen(payload)) < 0) {
		LOG_ERR("Failed to write payload to packet");
		net_pkt_unref(pkt);
		return -EIO;
	}

	/* Set packet addresses */
	net_pkt_set_ieee802154_lqi(pkt, 0xFF);
	net_pkt_set_ieee802154_rssi(pkt, 0);

	/* Send the packet via the radio API directly */
	buf = net_buf_alloc(&net_pkt_tx_pool, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("Failed to allocate TX buffer");
		net_pkt_unref(pkt);
		return -ENOMEM;
	}

	/* Copy packet data to buffer */
	net_buf_add_mem(buf, payload, strlen(payload));

	/* Transmit using radio API */
	ret = radio_api->tx(radio_dev, IEEE802154_TX_MODE_DIRECT, pkt, buf);
	if (ret != 0) {
		LOG_ERR("Failed to transmit packet: %d", ret);
		net_buf_unref(buf);
		net_pkt_unref(pkt);
		return ret;
	}

	LOG_INF("Transmitted packet #%u: %s", packet_counter - 1, payload);

	net_buf_unref(buf);
	net_pkt_unref(pkt);
	return 0;
}

int main(void)
{
	LOG_INF("Starting IEEE 802.15.4 periodic transmitter");

	if (!init_radio()) {
		LOG_ERR("Failed to initialize radio");
		return -1;
	}

	LOG_INF("Radio initialized, transmitting on channel %d", CONFIG_NET_CONFIG_IEEE802154_CHANNEL);
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
