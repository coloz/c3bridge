// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Upstream Host code overlays packed HCI/ATT structs onto little-endian wire
// bytes. MCS251 in arduino-stc51 is big-endian; compiling those overlays would
// silently corrupt opcodes/handles/lengths. The byte-oriented controller API is
// available separately and does not claim ArduinoBLE/GATT compatibility.
#if defined(STC_EXECUTION_MODE_MCS251) || (defined(STCXX_TARGET_ENDIAN_BIG) && STCXX_TARGET_ENDIAN_BIG)
#error "ArduinoBLE Host needs little-endian HCI/ATT support on MCS251; use ESPLinkHCI.h for controller access/beacons"
#endif

// This package always uses the external ESP32-C3 through ESPLink, independent
// of the host Arduino board. Board-specific upstream backends remain vendored
// for provenance, but are not selected by this package.
#define BLE_USE_ESPLINK 1
