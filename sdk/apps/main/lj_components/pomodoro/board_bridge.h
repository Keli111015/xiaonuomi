#pragma once
#include "esp_err.h"
/* Called after Wi-Fi is connected. All execution is deferred to a worker. */
esp_err_t board_bridge_init(void);
esp_err_t board_bridge_forward(const char *command);

int board_bridge_cli_init(void);
