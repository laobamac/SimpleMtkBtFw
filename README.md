# SimpleMtkBtFw

[中文说明](README_CN.md)

SimpleMtkBtFw is a MediaTek USB Bluetooth driver for macOS.

It is not a Wi-Fi driver and not a standalone Bluetooth stack. After firmware initialization is complete, Bluetooth functionality is handled by macOS.

## Supported Hardware

See [SimpleMtkBtFw/Info.plist](SimpleMtkBtFw/Info.plist) for the exact USB matching list.

Current personalities cover MT7921, MT7922, MT7922A, and a generic MediaTek Bluetooth USB class match. At runtime, the firmware path only accepts MediaTek internal device IDs `0x7961` and `0x7922`.

Included firmware:

- `BT_RAM_CODE_MT7961_1_2_hdr.bin`
- `BT_RAM_CODE_MT7961_1a_2_hdr.bin`
- `BT_RAM_CODE_MT7922_1_1_hdr.bin`

## Build

From the project root:

```sh
xcodebuild -project SimpleMtkBtFw.xcodeproj -scheme SimpleMtkBtFw -configuration Release build
xcodebuild -project SimpleMtkBtFw.xcodeproj -scheme SimpleMtkBtPatcher -configuration Release build
```

The project contains two KEXTs:

- `SimpleMtkBtFw.kext`: loads MediaTek Bluetooth firmware.
- `SimpleMtkBtPatcher.kext`: Lilu-based compatibility patcher.

## Installation

Only OpenCore injection is recommended.

Load order:

1. `Lilu.kext`
2. `SimpleMtkBtFw.kext`
3. `SimpleMtkBtPatcher.kext`

Do not inject another Bluetooth firmware loader that binds to the same USB Bluetooth device.

## Troubleshooting

- Check kernel logs for `SimpleMtkBtFw` and `SimpleMtkBtPatcher`.
- Confirm that the USB device is matched by `SimpleMtkBtFw/Info.plist`.
- Confirm that the internal device ID in logs is `0x7961` or `0x7922`.
- Check IORegistry for `FirmwareLoaded` and `fw_name`.

## Credits

- `laobamac` - SimpleMtkBtFw author, responsible for the MediaTek Bluetooth port, integration, and testing.
- `zxystd` and IntelBluetoothFirmware contributors - original framework, retained upstream code, and firmware generation helpers.
- MediaTek Inc. - MediaTek Bluetooth firmware format and Linux driver reference material.
- Linux kernel Bluetooth contributors - `btmtk` / `btusb` logic and device ID references.
- `vit9696`, acidanthera, and Lilu contributors - Lilu patching framework.

## Copyright And Licensing

See individual source file headers and included license files. Files adapted from or retained from upstream keep their original copyright and SPDX/license headers; newly written SimpleMtkBtFw files use `laobamac` copyright headers.

Before publishing binary packages that include firmware blobs, confirm the firmware redistribution terms.

## Disclaimer

This is an experimental driver for unsupported hardware. Use it at your own risk.
