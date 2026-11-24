#pragma once

#include <cstdint>
#include <esp_err.h>

enum class NetworkStatus { Starting, Pairing, Connecting, Thread };

struct PanelState {
    NetworkStatus network;
    uint8_t on_mask;
};

using ReadPanelState = PanelState (*)();
using TogglePanelRow = esp_err_t (*)(unsigned row);

void run_panel(ReadPanelState read_state, TogglePanelRow toggle);
