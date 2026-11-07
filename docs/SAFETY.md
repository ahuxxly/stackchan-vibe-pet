# 安全说明 / Safety guide

## 中文

这是实验性嵌入式固件。软件保护和测试不等同于安全认证。请把显示、主机控制、音频、电源和机械动作分别验证。

### 1. 刷写与恢复

- 仅针对官方 StackChan / CoreS3、ESP32-S3、16 MiB Flash 配置开展验证。名称相似的开发板、不同版本底座或舵机不代表兼容。
- 刷写前核对芯片、Flash 容量、分区表、镜像偏移和每个文件的摘要；保存并验证自己的私有备份，提前确认恢复方法。
- 本项目输出分段镜像，没有通用刷写命令。不要直接运行 `pio upload`、猜测偏移、擦除整片或照抄其他设备的恢复步骤。
- NVS 可能包含配对信息、用户数据和校准值。整片备份也可能包含敏感信息。不要上传、公开或在设备之间交换这些文件。整片恢复可能覆盖现有数据。
- 哈希保护或构建检查失败时应定位原因，不要删掉检查来完成构建。

### 2. 机械运动与供电

**运动默认关闭。`0.1.11-power1` 只验证了关闭状态的供电 GPIO 配置；实际运动恢复尚未验收。** 不要把开启运动作为首次启动步骤。

- 将设备放在稳固平面上，头部周围留出空间，USB 线留有余量。手指、头发和线缆远离运动部位。不要无人看管地进行运动实验。
- 舵机重新上电时可能保持旧目标并突然动作，即使接下来只打算读取姿态，也不能保证不动。
- 若自行开展受控验证，应有人在旁，先确认新鲜的 `MOVE OFF` 状态并准备物理电源开关。仅在理解风险后进行一次明确的本地尝试；不要连按重试。
- `HOLD MOVE` 需要约 1 秒长按后松开。电源、IMU、预热和姿态检查可能阻止启动。`POWER ERR`、`SERVO ERR` 或 `IMU STOP` 应作为停止排查的信号，不要绕过保护、强行校零或放宽阈值。
- 发生突跳、卡住、异常声音或不符合预期的动作，立即点 `STOP`。若未停止或关电状态无法确认，立即使用设备的物理电源开关关闭电源。
- 软件 STOP 是停止请求，不是经认证的紧急停止装置。不能用状态文字代替电气确认，也不能依据关闭状态读回推断开启电压正确。
- 先独立验证供电与稳定姿态，再考虑小幅动作和停止测试。舞蹈、长时间运动与无人看管运行不应作为首次测试。

### 3. BLE/HID 与主机授权

- BLE/HID 是实验性的社区协议实现，不是官方 Codex 接口保证。只与自己控制并信任的主机配对。连接状态、颜色和简短状态文字不能完整表达主机将执行的操作。
- `HOLD OK` 可以批准主机请求。操作前必须在主机上阅读具体命令、目标、权限及后果。不要仅凭桌宠显示决定是否批准。
- 本固件不提供远程批准、自动放行权限或绕过主机确认的工作流。不要把触摸、表情或过期状态解释为授权。
- `SEND`、`FAST`、`FORK`、`MAC MIC` 会发送主机控制；仅在已了解并验证主机行为后使用。设备上的 `STOP` 不是撤销主机操作的保证。

### 4. 录音与隐私

- 本地录音需要手动长按并松开，等待 `SPEAK NOW`，每次约 5 秒。播放必须另行点击；录音保存在易失性内存，不上传、不写文件。
- STOP / CLEAR、离开音频页和关闭屏幕会取消并清空本地录音。真实录音与回放质量尚未在当前版本完成验收。
- 录音前确认周围的人知情并同意，避免录入秘密或不必要的个人信息。
- `MAC MIC` 控制的是主机侧功能。主机软件可能采用不同的录音、存储或联网行为；本地音频的隐私说明不适用于它。
- 分享问题报告时仅提供必要且脱敏的信息，不上传备份、配对记录、设备标识、私人日志或会话内容。

## English

This is experimental embedded firmware. Software guards and tests are not a safety certification. Validate display behavior, host controls, audio, power and mechanical motion separately.

### 1. Flashing and recovery

- The target is the official StackChan / CoreS3 configuration with an ESP32-S3 and 16 MiB of flash. Similar board names, different bases or different servos do not establish compatibility.
- Verify the chip, flash capacity, partition layout, image offsets and file digests before any write. Make and verify your own private backup and establish a recovery procedure first.
- The build produces split images, not a universal flashing command. Do not simply run `pio upload`, guess offsets, erase the entire chip or copy another device's recovery procedure.
- NVS can contain pairing information, user data and calibration. Full-flash backups can also contain sensitive information. Do not publish or exchange them between devices. Full-flash restoration can overwrite existing data.
- Investigate failed hash guards or build checks instead of removing them.

### 2. Motion and power

**Motion is off by default. On `0.1.11-power1`, only the power-off GPIO configuration has been verified; physical motion recovery has not passed acceptance.** Do not enable motion as a first-boot step.

- Place the device on a stable surface, clear the space around its head and leave slack in the USB cable. Keep fingers, hair and cables away from moving parts. Do not leave motion experiments unattended.
- Reapplying servo power may cause a sudden move toward a previously held target. Planning only to read the pose does not guarantee that the device will stay still.
- For your own controlled validation, remain beside the device, confirm fresh `MOVE OFF` status and have the physical power switch ready. Make only a deliberate local attempt after understanding the risk; do not repeatedly retry.
- `HOLD MOVE` requires about a one-second hold followed by release. Power, IMU, warm-up and pose checks can prevent startup. Treat `POWER ERR`, `SERVO ERR` and `IMU STOP` as reasons to stop and investigate. Do not bypass guards, force calibration or loosen thresholds.
- For sudden jumps, jams, unusual noise or unexpected movement, tap `STOP` immediately. If it does not stop, or power-off cannot be confirmed, immediately turn off the device with its physical power switch.
- Software STOP is a shutdown request, not a certified emergency stop. Displayed state is not electrical confirmation, and power-off readback does not prove a correct power-on voltage.
- Validate power and stable pose feedback before considering small movements and stop tests. Dancing, extended motion and unattended operation are not appropriate first tests.

### 3. BLE/HID and host authorization

- This is an experimental community protocol implementation, not a guaranteed official Codex interface. Pair only with a host you control and trust. Link status, colors and short labels cannot fully describe what the host will do.
- `HOLD OK` can approve a host request. Read the actual command, target, requested permissions and consequences on the host before acting. Do not approve solely from the pet's display.
- The firmware does not provide a workflow for remote approval, automatic permission grants or bypassing host confirmation. Touches, expressions and stale status are not authorization.
- `SEND`, `FAST`, `FORK` and `MAC MIC` send host controls. Use them only after understanding and verifying host behavior. The device's `STOP` is not guaranteed to undo a host action.

### 4. Recording and privacy

- Local recording requires a manual hold and release, followed by waiting for `SPEAK NOW`. Each capture lasts about five seconds. Playback is a separate action. Audio stays in volatile memory, with no upload or file storage.
- STOP / CLEAR, leaving the audio page and turning off the screen cancel and clear local audio. Actual recording and playback quality have not passed acceptance testing on this version.
- Make sure people nearby know about and consent to recording. Avoid capturing secrets or unnecessary personal information.
- `MAC MIC` controls a host-side feature. Host software may record, retain or transmit audio differently; the local-audio privacy description does not apply to it.
- Share only necessary, sanitized diagnostics. Do not publish backups, pairing records, device identities, private logs or conversation content.
