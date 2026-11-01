#pragma once
/* Copy to board_bridge_local.h on BOTH boards with the same random PMK/LMK.
 * Never commit actual keys. Both MACs are Wi-Fi STA MACs, not BLE MACs. */
#define BOARD_BRIDGE_VOICE_MAC {0x68,0xee,0x8f,0x44,0xbd,0x30}
#define BOARD_BRIDGE_ACTION_MAC {0x68,0xee,0x8f,0x44,0xb1,0x0c}
#error "Fill in two random 16-byte PMK/LMK initializers before using this example"
#define BOARD_BRIDGE_PMK {0}
#define BOARD_BRIDGE_LMK {0}
