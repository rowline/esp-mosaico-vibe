# STATE

按日期倒序。拍板与推翻只追加，不改写。

## 2026-10-05 傍晚 muse_gadget：新命令上板；USB 卡死暂不查

上板验证（板上跑的是工作树全量编译的版本，内容和已推送的一致，只是测试入口开着）：

- `voice.say` 能用：13:58 Muse 用板子的声音说了「你好」，走的是 Mac 语音，0.6 秒说完。
- 第一次让它「用 gadget 说一句你好」，Muse 回答「小设备没有扬声器」，改用 `display.draw_url` 下载了一张图显示。估计是记着昨天还没有这条命令时的结论。新开对话或者明说「用 voice.say」之后就正常了。
- `lights.set` 被 Muse 调用了好几次，开灯、关灯都成功。

拍板（Rollin）：USB 卡死先不查，只做记录。

USB 卡死目前知道的（都还没验证）：

- 今天出现了 3 次：13:07（开机 21 分钟）、13:38（开机 3.5 分钟），以及昨晚 22:18。每次都是 ESP-Iris 不再应答，板子本身照常运行，没有重启；拔插后 Iris 重装 TinyUSB 才恢复。13:07 那次拔插后连 USB 设备都没出现，要按住 AI 键开机进 Vibe Mode。
- 线索 1：`link.image` 的日志显示，开机以来片内 DMA 内存最低到过 0（`dma … min=0K`）。如果 TinyUSB 或 Iris 某一刻申请不到 DMA 内存，USB 通道就可能卡死。
- 线索 2：两次卡死前不久，交互模块都检测到「有人走近」（亮灯、唤醒屏幕），分别隔了 1.5 秒和 24 秒。关联不强。
- 想好的下一步：用 `heap_caps_register_failed_alloc_callback` 记录每次内存申请失败（大小、类型、函数名），再把 DMA 内存历史最低值的每次刷新记进日志，等下次卡死时对照。
- 停掉的东西：后台抓日志已经停了。要继续查时，日志从 `.codex-runs/mosaico/*-monitor/raw.log` 里读，那里每行一条 JSON；`iris logs` 的终端输出会中途停止打印，不可靠。

## 2026-10-05 下午 让 Muse 主动调用板子：已提交，未上板

- 默认分支已改（Rollin 定）：rowline/esp-mosaico-vibe → `muse-gadget`，rowline/muse-gadget-sdk → `esp-mosaico`。
- 10-04 23:11–23:17 写下、一直没提交的那批改动，Rollin 让全部提交，写的人是谁没查清：SDK 7bc0f5e（`voice.say`、`voice.configure`、`display.show_text`、`display.configure`，`gadget_platform.h` 钩子），工作区 ed40da6（交互模块的 `presence.read`、`lights.set`、`ir.send_nec`）。
- 已验证：整个工作区固件编译通过（0 编译警告）；SDK 主机测试 157 项通过、2 项因环境跳过；六条指令都在固件里。
- 未验证：这些指令还没在板子上让 Muse 真正调用过。下次装机后，让 Muse 说一句话、点一次灯、读一次有没有人，看日志里的指令请求和返回结果。

## 2026-10-05 下午 muse_gadget：PSRAM 崩溃的解法是不从 PSRAM 执行代码；字幕去 Markdown

拍板（Rollin）：不退回只用板上 esp-sr 的稳定版，就在 Mac 语音方案上把问题解决。关 PMP 内存保护的实验被权限拦下，没有做，也不再追。

推翻：

- 关掉 `CONFIG_SPIRAM_XIP_FROM_PSRAM`（写在 `sdkconfig.mosaico`），代码和常量改在 flash 里执行，PSRAM 只放数据，那条只读/可读写的 PMP 分界就不存在了。PMP 内存保护照常开着。
  - 对照：原配置下，锤子测试加边下载边播放，4 分钟左右崩一次；关掉后同样的测试跑 10 分钟零崩溃，两核各锤约 580 万轮、0 个错字。随后又跑了 15 分钟锤子测试，Rollin 同时用中英文对话，有语音、没崩。
  - 副作用：ESP-Iris 的三项「放 PSRAM」配置依赖 XIP，跟着失效，Iris 的缓冲落回片内 RAM。开机后片内空闲 64.5 KB，和之前持平。PSRAM 空闲多出约 4 MB。

改动：

- 字幕分页（`muse_chat_text.c`）不显示 Markdown：行首的 `#`、`- * +` 列表号、`>` 去掉，行内的 `**`、`` ` ``、`__`、`~~` 去掉，`[文字](链接)` 只留文字。这些符号不占宽度，换行按去掉后的样子算。新增一条测试，host 测试共 157 项通过。
- 13:3x 从 Vibe Mode 装上（app-update），还要请 Rollin 看字幕效果。

悬着的事：

- USB 链路卡死又出现了一次（约 13:07，开机 21 分钟后，当时在跑锤子测试）。这次拔插后 Mac 上连 USB 设备都没有，Rollin 按住 AI 键开机进了 Vibe Mode 才装上新固件。断连的原因还没查到：板子里留存的日志在拔插之前就被覆盖了。现在后台在长时间抓日志，等下次出现。
- 都还没提交：XIP 配置、字幕去 Markdown、远程 TTS、测试入口（`tts_bench.c` 是调试代码，要拿掉或者改成用开关控制）。

## 2026-10-05 muse_gadget：英文用 Mac 的 Qwen3-TTS；PSRAM 访问错崩溃（未解决）

拍板（Rollin）：

- 回复不分中英文，都先交给 Mac 的 Qwen3-TTS 读（`~/tts-service`，`CONFIG_MOSAICO_TTS_URL` 写在 `sdkconfig.local`）；Mac 连不上时，中文退回板上的 esp-sr。
- 选择「保持现状，接着查」：板上现在跑的是带 Mac 语音和测试入口的版本，对话中随时可能崩。

Mac 这边（`~/tts-service`，改前备份为 `*.bak-20261004`、`*.bak-20261005`）：

- 监听 `0.0.0.0:8010`，只放行本机和板子（`TTS_ALLOW` 写在 `run.sh`，填本机和板子的内网地址），其余返回 403。HomeAgent 等本机调用不受影响。
- 新增三个可选请求字段：`language`、`sample_rate`、`pace`。`pace` 的意思是先发 1 秒，之后不超过实时的 pace 倍。板子请求 16 kHz、`pace` 1.5。
- Mac 防火墙开着隐身模式，Homebrew Python 3.11 不在放行名单里。不过板子实际连上了（Mac 端日志有板子地址发来的请求），说明没被挡。

试过不行的路：

- 下载缓冲只开 1 秒，读不完的语音流堆在 lwIP 里，占住 Wi-Fi 接收缓冲，片内 DMA 内存被吃到 1 KB 以下，板子崩溃。改成整条消息存进 PSRAM 缓冲，再加上服务端限速后，这个问题消失。
- 打开 `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` 反而更糟：Wi-Fi 接收缓冲在这颗芯片上进不了 PSRAM，还多出一批常驻的发送缓冲，DMA 最低值从约 6 KB 降到约 2 KB。已撤回。
- 加过 MMU 映射监测：每 10 ms 查一遍 256 个 PSRAM 页，没抓到任何改动。之后板子空闲时崩了一次，怀疑监测本身也是诱因，已删掉。

已确认的事实（5 份 core dump）：

- 全部是 Load access fault（mcause=5）。故障地址都是合法的 PSRAM 数据：0x5039cb18、0x5039cc54、0x5039cd1c（ESP-Iris 的服务状态，堆分配）、0x503a0c80（Muse 的 `s_turn`）、0x503a7b94（lwIP socket 表）。
- 这些地址全部落在 PSRAM 中 `.rodata` 拷贝末尾那条 PMP 分界之后约 46 KB 以内。分界在 `_rodata_reserved_end`（0x5039c380），从这里起由只读变为可读写。分界之后先是页对齐空隙（被回收给堆用），再往后是 `.ext_ram.bss`（从 0x503a0000 开始）。
- 用测试入口读了两个核的 PMP 寄存器，两边完全一样，都是对的：0x5039c380–0x50febd00 可读写。PSRAM 没开 ECC。
- 可复现：同时跑锤子测试（RPC 29524/2）和「边下载边播放」测试（RPC 29524/1，负载 p5），4 分钟左右崩一次。空闲时也崩过一次。

悬着的事：

- 想做的对照实验是关掉 PMP 内存保护（`CONFIG_ESP_SYSTEM_MEMPROT=n`），看还崩不崩。这一步被权限拦下了（会削弱安全保护），要 Rollin 决定做不做。
- 根因大概率在 ESP32-S31 加 ESP-IDF 主干（7b9cc1ac）这一层。8 月 19 日的 01b86f1269 刚改过 S31 的 PSRAM 分区保护。可以拿上面的证据去给乐鑫提 issue。
- 测试入口（`tts_bench.c`，ESP-Iris RPC 服务号 29524，方法 1 拉语音、2 锤子测试、3 读 PMP）是调试代码，查完要拿掉，或者改成用开关控制。
- 取日志不要依赖 `mosaico.py iris logs` 的终端输出：它会中途停止打印，实际上日志还在收。完整记录在 `.codex-runs/mosaico/*-monitor/raw.log` 里，每行一条 JSON。
- 都还没提交：两个仓库里有我的远程 TTS 和测试入口改动，也有另一个会话的 voice.say 改动，提交时要按文件分开。

## 2026-10-04 深夜 muse_gadget：把板子能力注册给 Muse（voice.say 等 7 条命令）

背景：Rollin 问 Muse「你能用 gadget 说话吗」，Muse 答「没有扬声器播放的指令」。Muse 和板子之间是 Meta 的 Home Link 协议（`link.register` 带 `commands_v2` 清单，Muse 下发 `link.invoke`），不是 MCP；清单里原来只有 health、discover、draw_url、show_animation、camera.capture。

拍板（Rollin）：加 `voice.say`，并把板子其它能力都暴露给 Muse。

改动：

- SDK（`esp-mosaico` 分支）：`main/gadget_platform.h` 新增两个弱钩子 `muse_gadget_platform_add_commands()` / `muse_gadget_platform_command()`；`noise_control` 公开 `noise_ctrl_add_command()`；`muse_glue.c` 注册并处理 `voice.say`、`voice.configure`、`display.show_text`、`display.configure`；`muse_voice.c` 新增 `muse_voice_say()`，在语音任务空闲时合成播放、字幕按页跟随，按 AI 键打断转为录音。
- 工程：`mosaico_platform/mosaico_commands.c` 注册 `presence.read`、`lights.set`、`ir.send_nec`，并在 `voice.say` 描述里补上语言说明；`mosaico_presence` 加状态查询和灯/红外请求（由 presence 任务代为执行，200 ms 内响应）。
- 注册 JSON 的打印缓冲上限从 8 KB 提到 12 KB。

已验证：

- 固件编译通过（0 编译警告；「1/2 app partitions too small」是原有分区提示）。
- SDK host 测试 156 项通过（`test_link_ota` 的注册测试加了钩子桩）。

- 23:21 app-update 装上：同一 Device ID 4553502d49524953010030eda0f46f0a，新 Boot ID 9810234534496346233，正常模式，0 次崩溃，空闲内部内存 58.6 KB。新一次开机的 `link.register` 从 1931 字节变成 5486 字节，Muse 服务端回 200 并 ack。

未验证：

- Muse 实际调用 `voice.say` 等新命令：要在 Muse 里让它「用 gadget 说一句话」，看设备日志里有没有 `invoke request: command=voice.say` 和 `muse_voice: saying ...`。灯、红外、presence.read 同理。

悬着的事：

- `muse_voice.c` 用到的 `MUSE_TTS_LATER` 来自另一个会话尚未提交的 `muse_tts.h` 改动（远程 TTS），两边要一起提交；本会话没有提交任何东西。
- 两个仓库里还有另一个会话的未提交改动（`mosaico_tts/tts_remote.*`、`tts_bench.*`、`muse_chat_session.cpp`），提交时按文件分开。

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
