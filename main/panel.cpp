#include "panel.h"
#include "panel_config.h"

#include <array>
#include <cstring>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
using namespace panel_config;
constexpr unsigned width = 128;
constexpr unsigned height = 64;
constexpr unsigned stride = width / 8;
using Frame = std::array<std::array<uint8_t, stride>, height>;

void send_byte(uint8_t value)
{
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        gpio_set_level(lcd_data, (value & mask) != 0);
        esp_rom_delay_us(10);
        gpio_set_level(lcd_clock, 1);
        esp_rom_delay_us(10);
        gpio_set_level(lcd_clock, 0);
        esp_rom_delay_us(10);
    }
}

void lcd_write(uint8_t sync, const uint8_t *values, unsigned size)
{
    gpio_set_level(lcd_select, 1);
    send_byte(sync);
    while (size--) {
        const uint8_t value = *values++;
        send_byte(value & 0xf0);
        send_byte(value << 4);
        esp_rom_delay_us(500);
    }
    gpio_set_level(lcd_select, 0);
}

void command(uint8_t value)
{
    lcd_write(0xf8, &value, 1);
}

void write_row(const Frame &frame, unsigned y)
{
    // GDRAM words 0–7 address the top half. Words 8–15 address the bottom half.
    const uint8_t address[] = {uint8_t(0x80 | (y & 31)), uint8_t(y < 32 ? 0x80 : 0x88)};
    lcd_write(0xf8, address, sizeof(address));
    lcd_write(0xfa, frame[y].data(), stride);
}

void lcd_init()
{
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << lcd_clock) | (1ULL << lcd_data) | (1ULL << lcd_select);
    config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&config));
    vTaskDelay(pdMS_TO_TICKS(100));
    command(0x30);
    vTaskDelay(pdMS_TO_TICKS(2));
    command(0x30);
    command(0x08);
    command(0x01);
    vTaskDelay(pdMS_TO_TICKS(3));
    command(0x06);
    command(0x34);
    command(0x36);
    command(0x32);
    command(0x0c);
    command(0x36);
}

// Five columns per glyph, least significant bit at the top.
constexpr uint8_t letters[][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};
constexpr uint8_t digits[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
};
constexpr uint8_t dash[] = {0x08,0x08,0x08,0x08,0x08};

void text(Frame &frame, unsigned x, unsigned y, const char *str, unsigned limit, bool invert = false)
{
    while (*str && limit--) {
        char c = *str++;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        const uint8_t *glyph = nullptr;
        if (c >= 'A' && c <= 'Z') glyph = letters[c - 'A'];
        else if (c >= '0' && c <= '9') glyph = digits[c - '0'];
        else if (c == '-') glyph = dash;
        if (glyph) {
            for (unsigned dx = 0; dx < 5; ++dx) {
                for (unsigned dy = 0; dy < 7; ++dy) {
                    if (!(glyph[dx] & (1U << dy))) continue;
                    auto &byte = frame[y + dy][(x + dx) / 8];
                    const uint8_t bit = 0x80 >> ((x + dx) % 8);
                    if (invert) byte &= ~bit;
                    else byte |= bit;
                }
            }
        }
        x += 6;
    }
}

void render(Frame &frame, PanelState state, unsigned selected, bool failed)
{
    frame = {};
    const char *status = "STARTING";
    switch (state.network) {
        case NetworkStatus::Starting: break;
        case NetworkStatus::Pairing: status = "PAIR VIA APP"; break;
        case NetworkStatus::Connecting: status = "JOINING THREAD"; break;
        case NetworkStatus::Thread: status = "THREAD READY"; break;
    }
    if (failed) status = "TOGGLE FAILED";
    text(frame, 2, 2, "HA", 2);
    text(frame, width - 2 - 6 * std::strlen(status), 2, status, 16);
    frame[11].fill(0xff);

    for (unsigned row = 0; row < count; ++row) {
        const unsigned top = 14 + 12 * row;
        const bool highlighted = row == selected;
        if (highlighted) {
            for (unsigned y = top; y < top + 11; ++y) frame[y].fill(0xff);
        }
        const bool ready = state.network == NetworkStatus::Thread;
        const bool on = state.on_mask & (1U << row);
        text(frame, 4, top + 2, labels[row], 15, highlighted);
        text(frame, 107, top + 2, ready ? (on ? " ON" : "OFF") : " --", 3, highlighted);
    }
}

struct EncoderInput {
    int moves = 0;
    bool clicked = false;
};

portMUX_TYPE input_lock = portMUX_INITIALIZER_UNLOCKED;
EncoderInput input;

unsigned encoder_bits()
{
    return (gpio_get_level(encoder_a) << 1) | gpio_get_level(encoder_b);
}

void sample_encoder(void *)
{
    // The esp_timer task samples independently of LCD traffic.
    constexpr int8_t transitions[] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
    static unsigned previous = encoder_bits();
    static int steps = 0;
    static bool raw = gpio_get_level(encoder_click) == 0;
    static bool pressed = raw;
    static bool armed = false;
    static int64_t changed_at = esp_timer_get_time();

    const unsigned current = encoder_bits();
    if ((previous ^ current) == 3) steps = 0;
    else steps += transitions[previous * 4 + current];
    previous = current;

    const bool click_level = gpio_get_level(encoder_click) == 0;
    const int64_t now = esp_timer_get_time();
    if (click_level != raw) {
        raw = click_level;
        changed_at = now;
    }
    bool clicked = false;
    if (now - changed_at >= 25000 && pressed != raw) {
        pressed = raw;
        clicked = !pressed && armed;
        armed = pressed;
    }

    if (steps >= encoder_steps || steps <= -encoder_steps || clicked) {
        portENTER_CRITICAL(&input_lock);
        if (steps >= encoder_steps || steps <= -encoder_steps) {
            const int direction = steps > 0 ? 1 : -1;
            input.moves += reverse_encoder ? -direction : direction;
            steps = 0;
        }
        input.clicked |= clicked;
        portEXIT_CRITICAL(&input_lock);
    }
}

void start_encoder()
{
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << encoder_a) | (1ULL << encoder_b) | (1ULL << encoder_click);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
    esp_timer_create_args_t timer_config = {};
    timer_config.callback = sample_encoder;
    timer_config.name = "panel_encoder";
    timer_config.skip_unhandled_events = true;
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_config, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000));
}
}

void run_panel(ReadPanelState read_state, TogglePanelRow toggle)
{
    lcd_init();
    start_encoder();

    static Frame desired;
    static Frame shown;
    auto state = read_state();
    unsigned selected = 0;
    unsigned scan = 0;
    int64_t failed_until = 0;
    bool failed = false;
    render(desired, state, selected, failed);
    for (unsigned y = 0; y < height; ++y) {
        write_row(desired, y);
        vTaskDelay(1);
    }
    shown = desired;
    // Display control is a basic instruction. Keep graphics enabled when switching back.
    command(0x32);
    command(0x0c);
    command(0x36);

    while (true) {
        portENTER_CRITICAL(&input_lock);
        const auto events = input;
        input = {};
        portEXIT_CRITICAL(&input_lock);

        const auto next = read_state();
        const int delta = events.moves % int(count);
        const unsigned next_selected = (int(selected) + delta + int(count)) % int(count);
        if (events.clicked && next.network == NetworkStatus::Thread) {
            failed_until = toggle(next_selected) == ESP_OK ? 0 : esp_timer_get_time() + 1500000;
        }
        const bool show_error = esp_timer_get_time() < failed_until;
        if (next_selected != selected || next.network != state.network ||
            next.on_mask != state.on_mask || show_error != failed) {
            selected = next_selected;
            state = next;
            failed = show_error;
            render(desired, state, selected, failed);
        }

        for (unsigned checked = 0; checked < height; ++checked) {
            const unsigned y = scan;
            scan = (scan + 1) % height;
            if (shown[y] == desired[y]) continue;
            write_row(desired, y);
            shown[y] = desired[y];
            break;
        }
        vTaskDelay(1);
    }
}
