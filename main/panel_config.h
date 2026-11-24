#pragma once

#include <driver/gpio.h>

namespace panel_config {
inline constexpr gpio_num_t lcd_clock = GPIO_NUM_9;
inline constexpr gpio_num_t lcd_data = GPIO_NUM_10;
inline constexpr gpio_num_t lcd_select = GPIO_NUM_11;
inline constexpr gpio_num_t encoder_a = GPIO_NUM_12;
inline constexpr gpio_num_t encoder_b = GPIO_NUM_7;
inline constexpr gpio_num_t encoder_click = GPIO_NUM_8;

// Four transitions per detent on the original encoder. Some clones use two.
inline constexpr int encoder_steps = 4;
inline constexpr bool reverse_encoder = false;
inline constexpr const char *labels[] = {"MAIN LIGHT", "DESK LAMP", "FAN", "OUTLET"};
inline constexpr unsigned count = sizeof(labels) / sizeof(labels[0]);
static_assert(count == 4, "The 128x64 layout requires four rows");
static_assert(encoder_steps == 2 || encoder_steps == 4);
}
