.. zephyr:code-sample:: 802154-periodic-tx
   :name: IEEE 802.15.4 Periodic Transmitter
   :relevant-api: ieee802154_interface

   Transmit IEEE 802.15.4 packets continuously at 1 Hz.

Overview
********

The IEEE 802.15.4 Periodic Transmitter sample continuously transmits wireless packets from an 
IEEE 802.15.4 radio at a rate of 1 packet per second.

The source code shows how to:

#. Initialize an IEEE 802.15.4 radio device from the :ref:`devicetree <dt-guide>`
#. Configure the radio in raw mode for packet transmission
#. Create and format network packets with payload data
#. Transmit packets using the IEEE 802.15.4 radio API
#. Generate and configure a random source MAC address
#. Use broadcast addressing for transmitted packets

This sample is useful for testing IEEE 802.15.4 networks, generating test traffic,
and learning about IEEE 802.15.4 packet transmission. It can be used together with
the :zephyr:code-sample:`802154-sniff-to-shell` sample to test packet reception.

Requirements
************

Your board must:

#. Have an IEEE 802.15.4 radio transceiver (such as Nordic nRF52840 or similar)
#. Have the IEEE 802.15.4 radio configured in devicetree with the ``zephyr,ieee802154`` chosen node
#. Support UART console for logging output

The sample uses logging to display transmission status and packet information.

Building and Running
********************

Build and flash the 802.15.4 periodic transmitter sample as follows, changing ``nrf52840dongle_nrf52840`` for your board:

.. zephyr-app-commands::
   :zephyr-app: samples/802154/periodic-tx
   :board: nrf52840dongle_nrf52840
   :goals: build flash
   :compact:

After flashing, the application starts transmitting IEEE 802.15.4 packets on channel 19 (configurable 
via ``CONFIG_NET_CONFIG_IEEE802154_CHANNEL``) at a rate of 1 packet per second. Each packet contains
a sequence number and timestamp for identification.

Sample output::

   *** Booting Zephyr OS build v3.7.0 ***
   [00:00:00.123,456] <inf> wpan_tx: Starting IEEE 802.15.4 periodic transmitter
   [00:00:00.234,567] <inf> wpan_tx: Radio initialized, transmitting on channel 19
   [00:00:00.345,678] <inf> wpan_tx: Source MAC: 02:ab:cd:ef:00:12:4b:00
   [00:00:01.000,000] <inf> wpan_tx: Transmitted packet #0: PKT#00000000 TIME:1000
   [00:00:02.000,000] <inf> wpan_tx: Transmitted packet #1: PKT#00000001 TIME:2000
   [00:00:03.000,000] <inf> wpan_tx: Transmitted packet #2: PKT#00000002 TIME:3000

Testing with Packet Sniffer
****************************

To verify packet transmission, you can use the IEEE 802.15.4 packet sniffer sample on another board:

#. Build and flash the :zephyr:code-sample:`802154-sniff-to-shell` sample on a second board
#. Ensure both boards are configured for the same channel (default: 19)
#. Run both applications simultaneously
#. The sniffer should display the packets transmitted by this sample

Build errors
************

You will see a build error if you try to build the 802.15.4 transmitter sample for a board
that doesn't have IEEE 802.15.4 radio support configured in its devicetree.

On GCC-based toolchains, the error looks like this:

.. code-block:: none

   devicetree error: chosen node zephyr,ieee802154 not found

This means your board either doesn't have an IEEE 802.15.4 radio or it's not properly
configured in the devicetree.

Adding board support
********************

To add IEEE 802.15.4 support for your board, you need to configure the radio device in your devicetree.
For boards with an IEEE 802.15.4 radio, add the chosen node to your devicetree:

.. code-block:: DTS

   / {
   	chosen {
   		zephyr,ieee802154 = &ieee802154;
   	};
   };

And ensure your radio device is properly defined, for example:

.. code-block:: DTS

   &radio {
   	status = "okay";
   };

For Nordic nRF52840-based boards, the IEEE 802.15.4 radio is typically already configured.

Tips:

- See :dtcompatible:`ieee802154` for more information on IEEE 802.15.4 devicetree configuration
- Check the devicetree files for supported 802.15.4 boards like ``nrf52840dongle_nrf52840`` 
  or ``nrf52840dk_nrf52840`` for reference implementations
- See :ref:`get-devicetree-outputs` for details on how to examine your board's devicetree
- The sample requires ``CONFIG_IEEE802154=y`` and ``CONFIG_IEEE802154_RAW_MODE=y`` to be set
- If your board doesn't have built-in IEEE 802.15.4 support, you may need to add an external
  radio module and configure it in a :ref:`devicetree overlay <set-devicetree-overlays>`
- Adjust the transmission rate by modifying the ``k_sleep(K_SECONDS(1))`` call in the main loop
