#pragma once

#include <stdbool.h>

#include "driver/gpio.h"

void board_init(void);
gpio_num_t board_status_led_gpio(void);
void board_status_led_set(bool on);
