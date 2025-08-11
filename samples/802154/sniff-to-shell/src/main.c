#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <stdio.h>

LOG_MODULE_REGISTER(wpan_sniffer, LOG_LEVEL_INF);

/* Use the default chosen 802.15.4 device */
static const struct device *radio_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));

static struct ieee802154_radio_api *radio_api;

static uint8_t mac_addr[8];

static uint8_t *get_mac(const struct device *dev)
{
	mac_addr[7] = 0x00;
	mac_addr[6] = 0x12;
	mac_addr[5] = 0x4b;
	mac_addr[4] = 0x00;

	sys_rand_get(mac_addr, 4U);

	mac_addr[0] = (mac_addr[0] & ~0x01) | 0x02;

	return mac_addr;
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

		filter.ieee_addr = mac_addr;
		radio_api->filter(radio_dev, true,
			IEEE802154_FILTER_TYPE_IEEE_ADDR, &filter);
	}

#ifdef CONFIG_NET_CONFIG_SETTINGS
	radio_api->set_channel(radio_dev, CONFIG_NET_CONFIG_IEEE802154_CHANNEL);
#endif

	radio_api->start(radio_dev);

	return true;
}

int net_recv_data(struct net_if *iface, struct net_pkt *pkt)
{
    const struct shell *sh = shell_backend_uart_get_ptr();
    int len = net_pkt_get_len(pkt);

    shell_print(sh, "Packet received: len=%d", len);

    struct net_buf *buf = net_buf_frag_last(pkt->buffer);
    uint8_t *data = buf->data;

    shell_fprintf(sh, SHELL_NORMAL, "Data [len=%d]: ", buf->len);
    for (int i = 0; i < MIN(buf->len, 16); i++) {
        shell_fprintf(sh, SHELL_NORMAL, "%02x ", data[i]);
    }
    shell_fprintf(sh, SHELL_NORMAL, "\n");

    net_pkt_unref(pkt);
    return 0;
}

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	return NET_CONTINUE;
}

int main(void)
{
	LOG_INF("Starting wpan sniffer app");

	if (!init_radio()) {
		LOG_ERR("Failed to initialize radio");
		return -1;
	}

	LOG_INF("Ready to receive packets");
	return 0;
}
