#pragma once
// Runtime link has only TX/RX/GND. GPIO4/5 are defaults, not a board wiring claim.
#ifndef C3_LINK_TX_PIN
#define C3_LINK_TX_PIN 4
#endif
#ifndef C3_LINK_RX_PIN
#define C3_LINK_RX_PIN 5
#endif
#ifndef C3_LINK_BASE_BAUD
#define C3_LINK_BASE_BAUD 921600
#endif
#ifndef C3_LINK_RX_BUFFER
#define C3_LINK_RX_BUFFER 8192
#endif
#ifndef C3_LINK_TX_BUFFER
#define C3_LINK_TX_BUFFER 4096
#endif
#ifndef C3_HCI_MAX_PACKET
#define C3_HCI_MAX_PACKET 1024
#endif
#ifndef C3_HCI_RX_DEPTH
#define C3_HCI_RX_DEPTH 12
#endif
#ifndef C3_HCI_TX_DEPTH
#define C3_HCI_TX_DEPTH 4
#endif
// Diagnostics on the console port (USB CDC or UART0). The protocol UART is
// never written, so the link behaves identically with this enabled. Set to 1
// here or with a build-wide -DC3_BRIDGE_LOG=1.
#ifndef C3_BRIDGE_LOG
#define C3_BRIDGE_LOG 0
#endif
#ifndef C3_BRIDGE_LOG_BAUD
#define C3_BRIDGE_LOG_BAUD 115200
#endif
#define C3_BRIDGE_VERSION "0.1.0"
#define C3_BRIDGE_CORE_VERSION "3.3.11"
