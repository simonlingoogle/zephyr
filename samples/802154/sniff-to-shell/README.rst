.. zephyr:code-sample:: 802154-sniff-to-shell
   :name: IEEE 802.15.4 Packet Sniffer to Shell
   :relevant-api: ieee802154_interface shell_interface

   Capture and display IEEE 802.15.4 packets via shell interface.

Overview
********

The IEEE 802.15.4 Sniff-to-Shell sample captures wireless packets from an IEEE 802.15.4 radio
and displays them on the console via the :ref:`Shell API <shell_api>`.

The source code shows how to:

#. Initialize an IEEE 802.15.4 radio device from the :ref:`devicetree <dt-guide>`
#. Configure the radio in raw mode for packet sniffing
#. Set up a packet reception handler to capture all wireless traffic
#. Output captured packet information to the shell console
#. Generate and configure a random MAC address for the radio interface

This sample is useful for debugging IEEE 802.15.4 networks, analyzing wireless traffic,
and learning about low-level 802.15.4 packet structures.

Requirements
************

Your board must:

#. Have an IEEE 802.15.4 radio transceiver (such as Nordic nRF52840 or similar)
#. Have the IEEE 802.15.4 radio configured in devicetree with the ``zephyr,ieee802154`` chosen node
#. Support UART console for shell output

The sample uses the shell interface to display captured packet information, so ensure your board
has a working UART console connection.

Building and Running
********************

Build and flash the 802.15.4 sniff-to-shell sample as follows, changing ``nrf52840dongle_nrf52840`` for your board:

.. zephyr-app-commands::
   :zephyr-app: samples/802154/sniff-to-shell
   :board: nrf52840dongle_nrf52840
   :goals: build flash
   :compact:

After flashing, the application starts listening for IEEE 802.15.4 packets on channel 19 (configurable 
via ``CONFIG_NET_CONFIG_IEEE802154_CHANNEL``). When packets are received, information about them 
will be printed to the console via the shell interface. The output shows packet length and the first 
16 bytes of packet data in hexadecimal format.

Sample output::

   *** Booting Zephyr OS build v3.7.0 ***
   [00:00:00.123,456] <inf> wpan_sniffer: Starting wpan sniffer app
   [00:00:00.234,567] <inf> wpan_sniffer: Ready to receive packets
   Packet received: len=25
   Data [len=25]: a1 88 01 cd ab 34 12 78 56 ff ff 01 80 02 03 04 
   Packet received: len=15
   Data [len=15]: 21 88 02 ff ff 12 34 56 78 ab cd ef 01 02 03

Build errors
************

You will see a build error if you try to build the 802.15.4 sniffer sample for a board
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
