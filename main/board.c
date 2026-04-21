#include "board.h"

#define BOARD_STATUS_LED_GPIO GPIO_NUM_2

void board_init(void)
{
    gpio_reset_pin(BOARD_STATUS_LED_GPIO);
    gpio_set_direction(BOARD_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_STATUS_LED_GPIO, 0);
}

gpio_num_t board_status_led_gpio(void)
{
    return BOARD_STATUS_LED_GPIO;
}

void board_status_led_set(bool on)
{
    gpio_set_level(BOARD_STATUS_LED_GPIO, on ? 1 : 0);
}
