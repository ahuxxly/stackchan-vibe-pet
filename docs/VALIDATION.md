# Public source validation / 开源源码验证

Validated locally on 2026-11-07 for the `0.1.11-power1` source export. These checks do not replace physical-device acceptance.

2026-11-07 对 `0.1.11-power1` 开源导出版执行了以下本地验证；这些结果不能替代实机验收。

- Firmware build: **PASS**, ESP32-S3 / CoreS3, pinned dependencies and hash-guarded patches.
- Runtime assets: **PASS**, 65 CLASSIC JPEGs plus 144 Lumi JPEGs; expected dimensions, SHA-256 and CRC checks.
- LittleFS package: **PASS**, all 211 files unpacked and matched byte-for-byte.
- Host regression suite: **PASS**, 28 entry points including real-source driver tests, model-based failure injection, rendering, audio ownership, HID freshness and motion interlocks.
- Python unit suite: **PASS**, 29 tests covering resource corruption, release guards, USB patching and firmware version identity.
- Source preservation: **PASS**, all 35 firmware source files match the latest local development source byte-for-byte. Only public packaging, documentation, sanitized resource metadata and host-test portability were adapted; release builds ignore private configuration headers.
- Publication privacy checks: **PASS**, reviewed allowlist, synthetic-only test identities, image metadata review and Gitleaks 8.30.1 scan. No detected secrets. Automated scans cannot guarantee detection of every secret format.
- Fresh CI: the repository workflow rebuilds and runs host/Python tests on each pushed commit. Check the run for the exact commit you intend to use.

Validation used an Apple Silicon Mac and existing cached pinned build dependencies. A local successful build does not by itself prove a fresh online dependency installation succeeds elsewhere; the clean CI run checks that separately.

本地验证使用 Apple Silicon Mac 与已有的固定版本依赖缓存。首次联网安装是否可复现，以对应提交的干净 CI 结果为准。

No device was opened, paired, recorded, powered for motion, flashed or restored as part of this publication. Latest-version motion recovery, real audio and host integration remain subject to the limitations in [README](../README.md) and [SAFETY](SAFETY.md).

本次开源发布未连接或刷写设备、未配对、未录音、未开启电机、未执行恢复。最新版本的实体运动、真实录放音与主机联动仍受 README 和安全说明中的验收边界约束。
