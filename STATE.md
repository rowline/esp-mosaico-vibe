# STATE

按日期倒序。拍板与推翻只追加，不改写。

## 2026-10-04 夜 muse_gadget：语速调快、USB 链路卡死

拍板（Rollin）：

- 语速 3 档偏慢，改成 4 档（`tts_mosaico.c` 的 `SPEED`）。22:12 用 app-update 装上，等 Rollin 试听确认后再提交推送。esp-sr 解析器只报警告的改动也在这一版里。

悬着的事：

- ESP-Iris 的 USB 链路在板子运行约 3.5 小时后卡死：app-update 两次报 `HELLO did not validate the selected identity before the deadline`。当时 Mac 上仍能看到 USB 设备，也没有进程占着串口，屏幕可以正常唤醒。拔插 USB 线后恢复。原因还没查到，可能跟 Mac 睡眠或 USB 挂起有关，固件里 `CONFIG_TINYUSB_SUSPEND_CALLBACK` 没开。再出现时先拔插 USB 线；不行就按 ON/OFF 重启，或者按住 AI 开机进 Vibe Mode。

## 2026-10-04 晚 发布到 GitHub（Rollin 定：公开 fork）

- 工作区：https://github.com/rowline/esp-mosaico-vibe ，分支 `muse-gadget`（fork 自 esp-mosaico/esp-mosaico-vibe）。push 用 remote `rowline`；`origin` 仍指向上游，只读。
- Muse SDK：https://github.com/rowline/muse-gadget-sdk ，分支 `esp-mosaico`（fork 自 facebookincubator/muse-gadget-sdk），在工作区里是子模块 `projects/muse-gadget-sdk`。改 SDK 后要先在 SDK 里提交、推送，再回到工作区提交子模块指针。
- 没上传：`projects/muse_gadget/sdkconfig.local`（内网网关）、所有 build 目录（含 Muse SDK 令牌）、`.tools/`，都由 `.gitignore` 挡住。
- 公开的个人信息：问候名字 "Rollin"（`sdkconfig.mosaico`）和本文件，Rollin 同意公开。

## 2026-10-04 晚 muse_gadget：字幕、拍照、语音上板结果

当前板上固件是 18:45 编译的版本（18:48 用 app-update 装上），包含交互模块问候功能。

试过不行的路：

- 第一次说话时板子崩溃复位（断言 `s_task_stack_is_sane_when_cache_frozen`，从留存的 core dump 读到）。原因是在 Muse 对话任务里映射 voice_data 分区，而这个任务的栈在 PSRAM，映射会冻结 flash 缓存。现在改为开机时在主任务里映射（`tts_mosaico_start()`）。规则：栈在 PSRAM 的任务不能做 mmap、写 NVS、读写 flash。

已验证：

- 拍照：SC101IOT 拍 1280×720，编码成约 93 KB 的 JPEG 用了 326 ms，转正后方向对，Muse 收到了。
- 语音：一条 11.1 秒的回复只用 2.3 秒合成完，约实时的 4.8 倍。回复文字到达后 12 ms 开始出声。
- 内存：开机后片内空闲约 65–68 KB，DMA 最低 28 KB。之前是 49 KB 和 12 KB，把几个缓冲区挪到 PSRAM 后回升。
- System Update 后配对保留。

悬着的事：

- esp-sr 解析器给每个汉字打一行日志，已调成只报警告。这个改动在磁盘上，还没编译进固件，跟着下一次 app-update 一起装。
- 还没问 Rollin 语速（现在是 3）和音量听着是否合适。
- 中文字幕在实机上的显示效果，Rollin 还没确认。

## 2026-10-04 muse_gadget：中文字幕、摄像头、板上语音

改动在 Muse SDK 的 `esp-mosaico` 分支（`components/muse`）和 `projects/muse_gadget/components/` 下的 `mosaico_board`（字体）、`mosaico_camera`、`mosaico_tts`。

拍板（Rollin）：

- 语音用板上离线合成：esp-sr 中文 TTS，小乐音色，不走云端。
- 改分区：ota_0 缩到 0xaf0000（11 MB），新增 voice_data（0xd00000，3 MB）放语音数据。

推翻与试过不行的路：

- 摄像头原先按 OV3640 直接取 JPEG。实机子板是 SC101IOT，日志报 `format=JPEG is not supported`。现在改成取 UYVY，在板上用 esp_new_jpeg 编码，并逆时针转 90°。
- 字幕缓冲原来 400 字节，中文一个字占 3 字节，一页放不下，后半页会被跳过。MUSE_CAPTION_MAX 改成 800。
- 字体文件不用 Source Han Sans 的名字：OFL 规定改过的字体不能沿用保留名，所以叫 `cjk_16.bin`。

已验证：

- 模拟器里中文字幕渲染正常（`MUSE_SIM_WIDE_FONT` 指向字体）。
- Muse host 测试 156 项通过，其中包括新加的中文换行测试，这条测试在旧代码上会挂。
- 语音的文本清理用桩函数跑过：Markdown、表情和链接去掉，缩写按中文拼读，百分号和温度读法正确，长句在逗号处分段，纯英文回复不读。
- 固件和 System Update 包编译通过。包里有分区表、ota_0 和 voice_data（0xd00000）。

未验证（都还没上板）：

- 中文字幕在实机上的显示效果。
- 拍照方向（按 BSP 给 AI 用的 ccw90 定的）和编码耗时。
- esp-sr 在 S31 上的合成速度：慢于实时的话，句子之间会有停顿。语速（3）和音量也还没听过。
- system-update 后配对是否保留。nvs 不在包里，按 recovery 的逻辑应该会保留。

已知与改动无关的问题：

- 设了 IDF_PATH 跑 host 测试时，`test_link_pairing_handshake` 会失败：IDF 6.2 的 mbedtls 不产出 `libp256m.a`。

悬着的事：

- 等 Rollin 跑 system-update。装完后抓日志，看 `tts:` 的合成耗时和 `camera:` 的编码耗时，再问照片方向对不对。
- ~~两个仓库的改动都还没提交~~ 已提交并推送，见最上面「发布到 GitHub」一节。

## 2026-10-04 muse_gadget：交互模块来人问候（`components/mosaico_presence`）

拍板（Rollin）：

- 问候在设备本地生成：开心动画、上扬提示音、按时段的字幕（如「晚上好，Rollin！」），显示 6 秒。不走 Muse 云端生成，不加真人语音。
- 每次有人来都亮交互模块的 6 颗灯（暖白），不看环境亮度。
- 18:40 推翻上一条的灭灯方式：实机上灯亮了不灭（人在板子前，人体感应一直报有动静）。改为灯和问候字幕一起亮、6 秒后一起灭；人一直在时有动静也不再亮灯。

实现时定的参数（改动写在 `mosaico_presence.c` 顶部的常量里）：

- 60 秒没有动静算人走了，之后再有动静算新来人；屏幕按 Muse 自己的自动休眠熄灭。
- 每次来人灯只亮 6 秒，和问候字幕同时消失（`WELCOME_MS`）。
- 离开满 10 分钟再来才重新问候；10 分钟内再来只亮灯、亮屏。
- 时间来自 SNTP（`ntp.aliyun.com`，时区 CST-8），Wi-Fi 连上后启动；对时前问候语是「你好，Rollin！」。

已验证：

- 两个源文件在 `-Werror` 下编译通过，0 警告。
- 本机桩函数跑了一遍状态机（18:40 按新灭灯规则重跑）：到达问候、灯 6 秒后随问候一起灭、人在时不重亮、10 分钟内再来只亮 6 秒灯不问候、字幕到时恢复、被语音字幕顶掉时不恢复，全部通过。

悬着的事：

- 18:3x Rollin 跑完 system-update，实机验证：交互模块（右槽）和摄像头（左槽）都识别到，来人亮灯、问候正常；灯不灭的问题见上面的推翻记录。
- 18:43 灭灯修复编译通过（0 编译警告），同时把 presence 任务栈（4 KB）和状态（832 B）挪进 PSRAM。这个任务及其调用链不碰 flash，栈放 PSRAM 安全。
- 18:48 app-update 装上 18:45 的版本（含另一个会话的 TTS 崩溃修复）：同一 Device ID，新 Boot ID 18024211459327827326，正常模式，0 次崩溃；Muse 就绪时空闲内部内存 65.2 KB（上一版 49.0 KB）。开机 7 秒检测到来人并问候（尚未对时，显示「你好，Rollin！」）。两边共用 build 目录，编译和装机前先互相打招呼。
- 18:5x Rollin 实机确认：来人后灯亮 6 秒后灭，问候正常。（灭灯没有日志可查，靠目测。）灭灯没有日志，`iris screenshot` 在这个应用上报 `screen mirror has an empty frame description`（Muse 自己驱动 LVGL，没接 Iris 的屏幕镜像），截不了图。
- 内部内存：Muse 就绪时的空闲内部内存从 74.7 KB 降到 49.0 KB（对比只带摄像头的版本，含两个会话的改动）。presence 自己的部分已挪走，剩下大头是 `mosaico_module_mgr` 开机就启动的两个任务（各 4 KB 内部栈，写死在 BSP 子模块里）和 RMT 驱动约 2.9 KB IRAM。要不要改 BSP，等 Rollin 定。
- 两个模块同时插着：开机都能识别、认领，交互模块正常亮灯；交互模块被认领期间拍照正常（18:46，1280×720，0.33 s 编码成 93 KB JPEG，方向对）。拍照和亮灯恰好同时进行还没试过。
