## Unreleased

### Added
- `grok mode direct|proxy`: direct mode calls `https://api.x.ai/v1/responses` from the device (model `grok-4.7`) with BLE stopped and its controller RAM released. Proxy mode is unchanged.
- `grok key set <key>` / `grok key clear`: xAI key in its own NVS slot (`grokcfg/xaikey`), separate from the proxy token. Stored unencrypted.
- `grok mem`: internal and DMA heap free / largest block / minimum, and the calling task's free stack.
- Two-slot OTA partition layout (ota_0 `0x10000`, ota_1 `0x200000`, both `0x1F0000`; otadata `0x3F0000`). No OTA download yet.
- Browser companion in `web/`. It asks xAI from the server and sends `grok show` over USB. A local, editable Grok Bot list picks the voice for the next question.

### Changed
- Grok reply overlay uses the 8x8 `unscii_8` font (20 columns) so text is legible.
- The serial console is a small line reader instead of the linenoise REPL. Secrets typed after `grok key set`, `grok token set` and `wifi set <ssid>` echo as `*` and are never kept for Up-arrow recall. No TAB completion or multi-entry history.
- README flashing docs: every esptool command uses `-b 115200` (the FPGA's USB CDC bridge, `0x374E:0x0101`, runs 115200 only). The first flash writes the partition table at `0x8000`, the app at `0x10000` and blank otadata at `0x3F0000` with an explicit esptool command; `idf.py flash` also rewrites the bootloader at `0x1000`. Restore writes the full 4 MB backup at `0x0`.

### Fixed
- `ble: adv fields failed (30)` when direct mode stopped BLE: advertising is no longer restarted while the host is stopping.

## 0.14.0-grok

Modified version of the ModRetro Chromatic MCU firmware at `63ed3b62b16768ad2415c9e3b865b438ca40fcc6` (v4.6). GPL-3.0-or-later. See NOTICE.

### Added
- Wi-Fi station mode. Credentials live in the `grokcfg` NVS namespace and are set from the serial console.
- BLE peripheral `ChromaticGrok` with the same line protocol as the `mcu>` console.
- Grok link: `grok ask` posts to a configurable `http://` proxy, and `grok show` displays a reply sent by a companion. Short replies use the stock font on the 160×144 screen.
- Python proxy and serial companion in `companion/`.
- System-menu row GROK, and Start while the menu is open, to read and page a reply.

### Changed
- App version `0.14.0-grok`. Menu firmware string `4.6g` (display only).
- Custom partition table. The factory app is `0x1F0000` bytes. NVS stays at `0x9000` / `0x6000`.
- Light sleep stays off unless `grok sleep on`. Wi-Fi and BLE cannot survive the stock light sleep.
- Project name is `chromatic_grok`.

## v0.13.8

### Changed
- Updated FPGA version number

## v0.13.7

### Changed
- Change default value for usbcolorcorrection from 1->0.

## v0.13.5

### Note 
The FPGA must be updated to v18.9 or newer for PCB-aware battery voltage estimate

### Changed 
- Uses most significant bit of 16-bit ADC packet sent from FPGA to determine which
battery voltage calculation to execute.

## v0.13.4

### Note
The FPGA must be updated to v18.7 or newer for fixed sprite palette behavior

### Changed
- Fixed bug where sprite palettes are not set correctly when using palette
selection tab.

## v0.13.3

### Note
The FPGA must be updated to v18.6 or newer for palette selection and updated
frame blending behavior.

### Added
- The ability to change the color palette for non-GBC titles through a Palette tab.

### Changed
- Renamed the frame blending toggle button text to "1:1" from "3:1" to clarify
the updated FPGA approach.
- Added redundancy to two-option menu widgets. Press B to toggle previous choice.
- Added up/down roll over to bottom/top for OSD navigation.

## v0.13.2

### Added
- The ability to mock button to the FPGA.

### Changed
- Simplified the firmware version reported the system tab. Press A to see details.

## v0.13.0

### Added
- Option to ignore diagonal inputs from the D-Pad
- Option to use the background palette 0 color (prevents screen transition flashing)
- Option to set low battery icon display behavior
- Added message to brightness to indicate low power mode is active
- Added hot keys to the Controls tab

### Changed
- Tuned battery voltage thresholds for 1.2V NiMH AA's
- Prevent backlight adjustments when in low power mode
- Renamed 'USB Color Correction' menu option to 'Streaming'
- Redesigned Controls tab, toggle option text

## v0.12.3

### Note
The FPGA must be updated to v17.0 or newer as the underlying communication protocol changed.

## Added
- Sleep the MCU on power up to reduce power consumption.
- Support LiPo battery thresholds and charging indication in the battery menu.

## Fixed
- Highest brightness setting is not properly restored on power up.
- Hot key brightness adjustments are saved between power cycles

### Changed
- Supports updated UART protocol.

## Removed
- The Frame Blending text hint on the Display screen.

## v0.11.2
This is the first production release.
