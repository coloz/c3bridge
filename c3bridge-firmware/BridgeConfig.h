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
#define C3_BRIDGE_VERSION "0.2.0"
#define C3_BRIDGE_CORE_VERSION "3.3.11"
