# Reinstall, restore, or go back to stock

All commands run on Windows in the **ESP-IDF 5.3 command prompt**, in the
folder that holds the files. The Chromatic's USB port is COM4 here; use your
port if it differs. The port runs at **115200 baud only**.

**Before any esptool command, close Miniterm** (and any other serial
monitor, the browser companion, or the Arduino serial monitor). Only one
program can hold COM4; esptool fails with "could not open port" or
"Failed to connect" otherwise.

Never run `erase_flash`, `idf.py erase-flash` or `idf.py flash` on this
device. None of the commands below touch the bootloader at `0x1000`.

## 1. Reinstall this firmware from scratch over USB

Use the files from a build folder (`partition-table.bin`,
`chromatic_grok.bin`, `ota_data_initial.bin`). Write all three in one
command:

```bat
esptool.py --chip esp32 -p COM4 -b 115200 write_flash 0x8000 partition-table.bin 0x10000 chromatic_grok.bin 0x3F0000 ota_data_initial.bin
```

Wait for "Hash of data verified" three times. If it is interrupted, run it
again. `ota_data_initial.bin` at `0x3F0000` is required: it resets the
boot-slot choice so the app you just wrote to `0x10000` (ota_0) is the one
that boots, even if a wireless update had switched the device to ota_1.
A USB flash does not need a signed image.

This leaves NVS (`0x9000`) alone, so Wi-Fi, the xAI key and the mode
normally survive. Check with `wifi status`. If they were lost (or this is a
new device), open Miniterm on COM4 at 115200 and enter them again:

```text
mcu> wifi set <ssid> <password>      (password echoes as ****)
mcu> grok key set <xAI API key>      (echoes as ****)
mcu> grok mode direct
```

Then reset the Chromatic and run `wifi status` and `grok status`.

## 2. Restore a full 4 MB backup

A backup made with
`esptool.py --chip esp32 -p COM4 -b 115200 read_flash 0 0x400000 <file>`
goes back in one write at `0x0`:

```bat
esptool.py --chip esp32 -p COM4 -b 115200 write_flash 0x0 <file>
```

It takes several minutes at 115200. Keep the Chromatic powered and connected
until the hash is verified. This puts back everything the backup holds:
bootloader, partition table, NVS, app.

**Keep backups outside OneDrive** (and other cloud-synced folders). A
backup contains NVS in plain text: your Wi-Fi password, the xAI key and the
proxy token. Store it on a local disk or an encrypted USB stick.

## 3. Go back to ModRetro firmware

1. Restore your **stock** backup (the one taken before the first custom
   flash, e.g. `chromatic-stock-4mb.bin`) with the command in section 2.
   This puts back the stock bootloader, the stock 1 MB partition table and
   the stock app. Writing only a stock app at `0x10000` is not enough,
   because this firmware replaced the partition table.
2. Then run the official **MR Updater** to bring it to ModRetro's current
   firmware:
   https://support.modretro.com/en_us/chromatic-firmware-updater-ryhoYnzCx

If you have no stock backup, the MR Updater alone is the way back.

## 4. The signing private key

Wireless updates (`grok update`) only install images signed with the
release private key (`chromatic_signing_key.pem`). It lives only on the PC
that created it.

- **Back it up in a password manager** (paste the whole PEM, including the
  BEGIN/END lines, into a secure note). Without it no future wireless
  update can be signed; every device then needs a USB flash of a build with
  a new public key.
- Never put it in git, in a chat, in OneDrive, or on the build machine.
  Only the public key is shared and built into the firmware.
- If it leaks, anyone could sign an image the device accepts (they would
  still need to run `grok update <url>` on its console). Make a new key,
  rebuild with the new public key, and USB-flash that build.
