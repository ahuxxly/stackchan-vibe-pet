# Third-party notices / 第三方声明

Original project contributions are offered under [MIT](LICENSE). Third-party code and assets retain their own licenses and copyright notices. This is a source release, not a relicensing of dependencies or a universal binary distribution.

本项目原创代码使用 MIT 许可证；第三方代码、素材和依赖保留各自许可与署名。本次发布仅含源码和运行资源，不含固件二进制、私有参考素材或原机 Flash 备份。

## Runtime source and artwork

- **Corvelis/stackchan-pet-fw**, commit `7ea267d7609ce5226e53a6ed501a1d9fbc4d39de`: FaceController, FaceAssetProfile, AffectionController, supporting configuration/UTF-8 headers and the 65-image CoreS3 v2 face set. MIT, [license](licenses/pet-MIT.txt). Preserve [asset provenance](vendor/pet/THIRD_PARTY_NOTICES.md): the current v2 runtime images are Ailog's MIT-licensed contribution. Historical pre-v0.3 character images are not distributed here. The preserved upstream notice also describes unrelated upstream modules; it is attribution history, not an inventory of this export. Local changes include shared-canvas HUD, gaze presentation, gesture timing and render diagnostics.
- **Corvelis/VibeStackChan**, commit `8384b758b61cea3939be34148c01fa7631e0fbe0`: BLE HID report and Codex control protocol. MIT, [license](licenses/vibe-MIT.txt). The HID lineage also includes **GOROman/vibewatch**, [MIT notice](licenses/GOROman-MIT.txt). The local transport adds explicit pairing, bounded queues, connection epochs and state expiry. These community projects do not imply an affiliation with or endorsement by OpenAI.
- **M5Stack/StackChan-BSP**, commit `f7ed40e6f5d9a1d08440cb926f3a0865b81882f8`: touch, RGB and servo board support. MIT, [license](licenses/StackChan-BSP.txt). Exact original files are retained in `vendor/stackchan-bsp-quiet-original/`. Hash-guarded patches add quiet boot, deferred servo initialization, invalid-feedback handling, power diagnostics and pull-bias correction. No calibration values from a private device are included. Historical source-only fixtures in `tests/fixtures/` exercise earlier failure modes and retain this license.
- **FTServo Arduino**, Copyright (c) 2024 ftservo: archived SCSerial transport files and the BSP servo dependency. MIT, [license](licenses/FTServo-Arduino.txt).
- **M5Unified** `b1ffcc677014ed8bd01e5a1f240736ae654bfe12` (0.2.18): original audio driver files in `vendor/m5unified-audio-original/`; MIT, [license](licenses/M5Unified.txt). Local hash-guarded changes handle DMA failures, buffer ownership and orderly audio cancellation.
- **Espressif Arduino-ESP32 HWCDC.cpp**, Copyright 2015–2024 Espressif Systems (Shanghai) PTE LTD: verbatim source in `vendor/arduino-hwcdc-original/`, Apache-2.0, [license](licenses/Apache-2.0.txt). `prepare_usb_tx.py` applies an explicitly marked pending-TX service patch. The original header is retained.
- **Lumi / 浮光**: originally AI-generated artwork, then cropped and adapted into 18 states and 144 runtime frames. The exported Lumi assets and overview are offered under the project MIT license to the extent the contributor holds applicable rights. Private reference inputs and generation-session metadata are excluded. This does not assert exclusive copyright or third-party endorsement. Preview-only moods do not represent connected cloud services.

## Build dependencies

Versions and commits are pinned in `platformio.ini` and `scripts/build_fs.py`.

| Component | License / notice |
| --- | --- |
| M5GFX 0.2.36 (`7cb26b44ab31bb41a0a597196dd2c6d868869a21`) | MIT; [license](licenses/M5GFX.txt); bundled fonts have additional notices in upstream distribution |
| ArduinoJson 7.4.3 | MIT; [license](licenses/ArduinoJson.txt) |
| NimBLE-Arduino 2.5.0 | Apache-2.0; [license](licenses/NimBLE-Arduino.txt), [NOTICE](licenses/NimBLE-NOTICE.txt) |
| IRremoteESP8266 2.9.0 | LGPL-2.1; [license](licenses/IRremoteESP8266-LGPL-2.1.txt) |
| M5Utility 0.3.1, M5HAL 0.1.4, M5UnitUnified 0.6.4, M5Unit-NFC 0.1.2 | MIT, notices remain in fetched packages |
| PlatformIO espressif32 6.12.0 / Arduino-ESP32 framework | Multiple upstream component licenses, including Apache-2.0 and LGPL; not redistributed here |
| mklittlefs `943d2f76736eef6630edd4933e42c3016194770b` | MIT; [license](licenses/mklittlefs.txt); its TCLAP dependency is MIT and retains `tclap/COPYING` |
| littlefs `abd90cb84c818a663b584575b019258d01d0065e` | BSD-3-Clause; [license](licenses/littlefs.txt) |

The filesystem packer sources are downloaded from public upstream archives and verified by SHA-256 during the build. Build-generated vendor sources, toolchains and binaries are excluded from Git. The public source tree preserves all notices needed for the material it actually includes.

If you distribute firmware binaries, review every linked dependency's obligations, including LGPL source/relinking requirements, Apache NOTICE files and font notices. The project MIT license does not remove those obligations.

如需再分发自行构建的固件二进制，请另行履行所有链接依赖的许可要求，包括 LGPL 的源码/重新链接要求、Apache NOTICE 与字体声明；项目 MIT 许可不能替代这些义务。
