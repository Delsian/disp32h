# ESP32-H2 Matter control panel

An ESP32-H2 drives the 128×64 ST7920 LCD and rotary encoder on a RepRapDiscount Full Graphic Smart Controller. Turn the knob to select a row and press to toggle it. The panel exposes four virtual Matter on/off plug endpoints over Thread. A Home Assistant automation links each endpoint to a chosen light, switch, or fan and reflects changes made in Home Assistant back on the display. Assign each target to one row.

## Wire the panel

The defaults target the 4 MB ESP32-H2-DevKitM-1 and the standard RepRapDiscount ST7920 connector numbering. Some clones reverse the IDC shrouds or use a different controller. Check the signal names and verify 5 V and ground with a meter before applying power.

| Signal | RepRapDiscount header | ESP32-H2 |
| --- | --- | --- |
| LCD serial clock (D4 / SCLK) | EXP1 pin 5 | GPIO9 through 3.3-to-5 V buffer |
| LCD serial data (E / SID) | EXP1 pin 3 | GPIO10 through 3.3-to-5 V buffer |
| LCD active-high select (RS / CS) | EXP1 pin 4 | GPIO11 through 3.3-to-5 V buffer |
| Encoder A | EXP2 pin 3 | GPIO12 |
| Encoder B | EXP2 pin 5 | GPIO7 |
| Encoder push | EXP1 pin 2 | GPIO8 |
| Ground | EXP1 pin 9 | Common ground |
| Display supply | EXP1 pin 10 | Regulated 5 V |

Use a 74AHCT125 powered from the LCD's 5 V rail, with its three used active-low output enables tied to ground. The default firmware pulls encoder inputs up internally to 3.3 V. Verify the encoder lines are dry contacts to ground on your module before wiring them directly. If a clone pulls any encoder line up to 5 V, level-shift that input to 3.3 V. Use a common ground, and leave the rest of EXP1/EXP2 disconnected.

See the [standard LCD connector mapping](https://github.com/Klipper3d/klipper/blob/master/config/sample-lcd.cfg) and [ESP32-H2-DevKitM-1 header diagrams](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32h2/esp32-h2-devkitm-1/user_guide.html#header-block).

## Build and flash

Open `disp32h.code-workspace` in VS Code and install the recommended PlatformIO IDE extension. Run **Build**, **Upload**, or **Monitor** under the `esp32-h2` environment in the PlatformIO sidebar. The extension also exposes these tasks in **Terminal → Run Task**.

From a shell with PlatformIO Core installed:

```sh
pio run
pio run -t upload
pio device monitor
```

Connect to the DevKit's native **ESP32-H2 USB** port for the 115200 serial console. If multiple serial devices are present, pass `--upload-port` or `--port` with the proper device path. The first build downloads the pinned pioarduino platform, ESP-IDF, and managed components. Pin assignments, row labels, and encoder direction and resolution are in `main/panel_config.h`.

## Add to Home Assistant

1. Enable the Matter integration and make sure Home Assistant can reach an active Thread border router. The Companion app needs Bluetooth and the credentials for the preferred Thread network. See the [Home Assistant Matter guide](https://www.home-assistant.io/integrations/matter/#adding-a-matter-device-to-home-assistant) and [Thread guide](https://www.home-assistant.io/integrations/thread/).
2. Open the serial monitor and reset the panel. The startup log lists the endpoint IDs and, on an uncommissioned panel, the Matter QR payload and manual pairing code. In the Home Assistant Companion app use **Add Matter device** and enter that manual code.
3. Open **Desk Control Panel** in Home Assistant. Identify the four Matter switches by their endpoint IDs in the serial log and Home Assistant device diagnostics. You can also toggle each switch in Home Assistant and watch the corresponding LCD row change.
4. Copy `home-assistant/panel_row.yaml` into `config/blueprints/automation/disp32h/panel_row.yaml` in Home Assistant. Reload automations, then create one automation from **Matter control panel row** for each row you want to link. Run each automation once after setup to copy the target's current state to the panel.
5. Press the encoder and confirm the selected device toggles. Change the same target in Home Assistant and confirm the LCD follows. Repeat for each linked row.

The firmware uses ESP-Matter development attestation. On Android, register test vendor ID `0xFFF1` and product ID `0x8000` in the Google Home Developer Console before commissioning. On iOS, accept the development-device prompt. See the [Home Assistant development-device instructions](https://www.home-assistant.io/integrations/matter/#experiment-with-matter-using-a-esp32-dev-board) and [test vendor setup](https://nabucasa.github.io/matter-example-apps/).

The LCD displays the Matter endpoint's state. It updates immediately on a local toggle, and the blueprint synchronizes target state as Home Assistant receives it. **THREAD READY** means the radio is attached and the panel has a Matter fabric. Delivery still depends on Home Assistant and the target entity being available.

For a full reset, first remove the panel in Home Assistant, then run `pio run -t erase` and upload again. Erasing flash removes all device settings and generates a new pairing code on the next boot.
