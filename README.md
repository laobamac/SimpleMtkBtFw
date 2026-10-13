# SimpleMtkBtFw

[![Build](https://github.com/flxxxxddd/SimpleMtkBtFw/actions/workflows/build.yml/badge.svg)](https://github.com/flxxxxddd/SimpleMtkBtFw/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/flxxxxddd/SimpleMtkBtFw)](https://github.com/flxxxxddd/SimpleMtkBtFw/releases)

[中文说明](README_CN.md) (upstream, not updated for this fork)

**Bluetooth for MediaTek MT7921 / MT7921K (AMD RZ608) / MT7922 on macOS.**

Two kexts:

- **`SimpleMtkBtFw.kext`** — loads the MediaTek firmware into the USB
  Bluetooth function of the card and enables the Bluetooth protocol. After
  that, macOS' own Bluetooth stack (`bluetoothd`) drives the controller.
- **`SimpleMtkBtPatcher.kext`** — a [Lilu](https://github.com/acidanthera/Lilu)
  plugin that fixes the places where MediaTek firmware and `bluetoothd`
  disagree (see [What changed in this fork](#what-changed-in-this-fork)).
  Without it, AirPods connect but play no sound.

Wi-Fi for the same card: **[SimpleMtkWlan](https://github.com/flxxxxddd/SimpleMtkWlan)**.

> This fork makes Bluetooth actually usable on MT7921 / RZ608 with macOS
> Sequoia: upstream loaded the firmware, but `bluetoothd` never brought the
> controller up (and later crashed), and AirPods stayed silent.

## Status

Tested on MT7921K (RZ608, USB `0e8d:0608`), macOS 15.8.1 Sequoia, SMBIOS
iMacPro1,1, Intel desktop.

| Feature | State |
|---|---|
| Firmware load (cold boot), Bluetooth on/off | ✅ |
| Pairing, auto-reconnect | ✅ |
| A2DP to ordinary speakers/headphones (SBC/AAC) | ✅ |
| AirPods: audio (AAC-ELD), battery, controls, switching between Apple devices | ✅ |
| BLE scanning (nearby Apple devices, Find My) | ✅ |
| HFP microphone (CVSD, 8 kHz) | ⚠️ works, but quiet and choppy |
| Wide-band speech (mSBC) | ❌ |
| Sleep / wake | ⚠️ untested |

Supported USB IDs: see [`SimpleMtkBtFw/Info.plist`](SimpleMtkBtFw/Info.plist)
(MT7921, MT7922, MT7922A and a generic MediaTek Bluetooth class match). The
firmware path accepts the internal chip IDs `0x7961` (MT7921) and `0x7922`.

Embedded firmware: `BT_RAM_CODE_MT7961_1_2_hdr.bin`,
`BT_RAM_CODE_MT7961_1a_2_hdr.bin`, `BT_RAM_CODE_MT7922_1_1_hdr.bin`.

## Installation (OpenCore)

1. Download **`SimpleMtkBtFw-x.y.z-RELEASE-AllInOne.zip`** from
   [Releases](https://github.com/flxxxxddd/SimpleMtkBtFw/releases). It
   contains every kext Bluetooth needs (Lilu, BlueToolFixup, SimpleMtkBtFw,
   SimpleMtkBtPatcher), a `config-snippet.plist` with the `Kernel → Add` and
   `NVRAM` entries and `INSTALL.txt`. (`SimpleMtkBtFw-x.y.z-RELEASE.zip` has
   only this project's two kexts.)
2. Put into `EFI/OC/Kexts` and add to `config.plist → Kernel → Add`, in this
   order:
   1. `Lilu.kext`
   2. `BlueToolFixup.kext` from [BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) (MinKernel `21.0.0`)
   3. `SimpleMtkBtFw.kext`
   4. `SimpleMtkBtPatcher.kext`
3. Add these NVRAM variables (`NVRAM → Add` **and** `NVRAM → Delete`, GUID
   `7C436110-AB2A-4BBB-A880-FE41995C9F82`):

   | Key | Type | Value |
   |---|---|---|
   | `bluetoothInternalControllerInfo` | Data | `0000000000000000000000000000` (14 zero bytes) |
   | `bluetoothExternalDongleFailed` | Data | `00` |

   With these, `bluetoothd` treats the card as a third-party dongle
   (`Chipset: THIRD_PARTY_DONGLE`) instead of assuming an Apple/Broadcom
   module and sending Broadcom-only commands.
4. Do not inject another Bluetooth firmware loader for the same device.
5. Reboot. `system_profiler SPBluetoothDataType` should show
   `Chipset: THIRD_PARTY_DONGLE` and `State: On`.

## Troubleshooting

* **Bluetooth device missing after a reboot** (nothing at the USB port, or
  `persistent enumeration failures` in the kernel log). The M.2 card keeps
  standby power and its Bluetooth core can stay wedged. Shut down and cut
  power (PSU switch or unplug) for ~30 seconds.
* **Loader log.** Early-boot kernel logs are usually lost, so the loader keeps
  its own log and publishes it on the USB device:

  ```sh
  ioreg -l -w0 | grep -A20 MtkBtLog
  ```

* **Patcher counters.** `ioreg -l -w0 | grep -A1 '"SimpleMtkBtPatcher"'`
  shows how many HCI Read Clock commands and ACL packets were rewritten. If
  the key is missing, the patcher is not loaded (check Lilu and the kext
  order).
* **`bluetoothd` keeps restarting / Bluetooth menu spins.** Check the NVRAM
  variables above. Crash reports in `/Library/Logs/DiagnosticReports` with
  `bcm_vsc.c` in the log mean `bluetoothd` thinks the chip is Broadcom.
* **After many `bluetoothd` crashes launchd throttles it** (10–20 minutes).
  Restart it with `sudo launchctl kickstart -k system/com.apple.bluetoothd`.
* **HCI trace.** `bluetoothd` can write a PacketLogger trace:

  ```sh
  sudo defaults write /Library/Preferences/com.apple.MobileBluetooth.debug HCITraces -dict HCIFileTraces -bool true
  sudo pkill bluetoothd
  # trace: /private/var/root/Library/Logs/Bluetooth/bluetoothd-hci-latest.pklg
  sudo defaults delete /Library/Preferences/com.apple.MobileBluetooth.debug   # to disable
  ```

* **Music sounds like a phone call.** That is HFP, macOS switches the
  headphones into headset mode while their microphone is the selected input.
  Choose another input device.

## Building

Requirements: macOS with Xcode.

```sh
git clone --recurse-submodules https://github.com/flxxxxddd/SimpleMtkBtFw.git
cd SimpleMtkBtFw
./scripts/bootstrap.sh          # MacKernelSDK submodule + Lilu SDK
for s in SimpleMtkBtFw SimpleMtkBtPatcher; do
  xcodebuild -project SimpleMtkBtFw.xcodeproj -scheme $s \
    -configuration Release -derivedDataPath build CODE_SIGNING_ALLOWED=NO build
done
# -> build/Build/Products/Release/{SimpleMtkBtFw,SimpleMtkBtPatcher}.kext
```

`SimpleMtkBtFw/FwBinary.cpp` is regenerated from `SimpleMtkBtFw/fw/*.bin` by
`scripts/zlib_compress_fw.py` during the build.

CI builds Debug and Release on every push and pull request; pushing a tag
`vX.Y.Z` publishes a GitHub release.

## What changed in this fork

* **`bluetoothd` finds and accepts the controller.** On SMBIOS models with
  built-in Bluetooth, `bluetoothd` looked for Apple's internal module, did not
  find it and looped until its watchdog killed it (this also made shutdown
  hang ~30 s in BlueTool). With the NVRAM variables above and the device's USB
  product name left untouched, it takes the third-party dongle path. Renaming
  the product to "Bluetooth USB Host Controller" must **not** be done: that
  name is reserved for the internal module, which is assumed to be Broadcom,
  and `bluetoothd` then aborts in `_BCM_ReadVerboseConfigVersionInfoParser`
  on MediaTek's reply to VSC `0xFC79`.
* **AirPods audio.** `bluetoothd` reads the BT clock with
  `HCI_Read_Clock(handle, Which_Clock = 0)` to timestamp AAC-ELD audio.
  MediaTek answers with Connection_Handle 0, so the result is never matched
  ("Failed to read BT clock"), the timestamps are wrong and AirPods pause the
  stream after a few seconds. The patcher hooks `IOUSBHostDevice::deviceRequest`
  and asks for the piconet clock of the connection (`Which_Clock = 1`); the
  Mac is the central, so the value is the same and the handle is returned.
* **ACL data as non-flushable.** The patcher also rewrites the ACL packet
  boundary flag from "automatically flushable" to "non-flushable" on bulk OUT
  transfers (`IOUSBHostPipe::io`), as Linux does for these controllers.
* **The Intel LE random-address workaround** inherited from
  IntelBluetoothFirmware is opt-in now (`-smbtrandaddr`).
* **Debuggability.** A boot log ring buffer (`MtkBtLog` on the USB device),
  WMT patch status per firmware section, patcher counters on `IOResources`.
* **Build.** Builds from a clean clone: MacKernelSDK as a submodule, Lilu SDK
  fetched by `scripts/bootstrap.sh`, firmware generator script included; CI
  and releases.

## Credits

* [laobamac (王孝慈)](https://github.com/laobamac) — SimpleMtkBtFw: MediaTek Bluetooth port, integration and testing
* [zxystd](https://github.com/zxystd) and [IntelBluetoothFirmware](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) contributors — original framework and firmware tooling
* MediaTek Inc. — firmware; Linux `btmtk` / `btusb` authors — reference
* [acidanthera](https://github.com/acidanthera) — Lilu, MacKernelSDK, BlueToolFixup
* `dawalishi821` — network card hardware

## License

See the individual source file headers and the included license files. Files
adapted from upstream keep their original copyright and SPDX headers.
Firmware is redistributed under the MediaTek firmware license.
