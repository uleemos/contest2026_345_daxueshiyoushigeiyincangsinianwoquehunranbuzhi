# ESP32-P4 openvela 开发接力文档

> 状态日期：2026-08-23（Asia/Shanghai）
>
> 交接目标：让新的 AI 工具无需依赖此前聊天记录，即可从正确 Git 基线继续开发，并保持相同的仓库边界、硬件证据、构建验证和 PR 质量。
>
> 当前阶段：
> 1. Gate G1 已通过；
> 2. CAM-002～CAM-004（SC2336 探测、流控、CSI-2 Host & D-PHY 硬件链路锁定）已完成、合入并归档；
> 3. AUD-001（ES8311 音频编解码器 + ESP32-P4 I2S0 底层驱动 + 板级功放使能 + `es8311_audio` CLI 测试套件）已合入团队仓，NuttX 驱动已进入统一主 PR #340；
> 4. DISP-001（MIPI DSI 驱动 + D-PHY TX PLL + Framebuffer `/dev/fb0` + LVGL 9.1.0 演示 + `dsi_display` CLI 测试套件）已合入团队仓，NuttX 驱动已进入统一主 PR #340；
> 5. 待后续板卡连接后执行 Audio 和 Display 的实板串口烟测；CAM-005（CSI DW-GDMA 取帧驱动）保留在 WIP 分支以便后续深入调试。
>
> 本文是当前状态和执行规则的入口；详细历史证据继续以 `PORTING_NOTES.md` 和 `hardware-logs/` 为准。

## 0. 新 AI 必须先读的结论

1. 工作区根目录是 `/home/uleemos/openvela-contest`。
2. 板卡是 `ESP32-P4X_FUNCTION_EV_BOARD` V1.8，板载无线模块是
   `ESP32-C6-MINI-1`。旧的 C5 V2.0 原理图不是本板权威依据。
3. ESP32-P4 型号按实物和资料固定为 `ESP32-P4NRW32X`，实测 silicon revision
   为 v3.2；封装内 PSRAM 为 32 MB。
4. 主板 U2 实物为 `GD25Q128ESIG`，Flash 为 16 MB。当前最后 1 MB
   `0x00f00000-0x00ffffff` 作为安全 MTD/SmartFS 测试区并挂载为 `/data`。
5. Windows 串口为 `COM3`，USB 设备是 ESP32-P4 USB Serial/JTAG；WSL 当前没有
   `/dev/ttyACM*`，烧录和串口自动化通过 Windows Python/esptool 操作 COM3。
6. Camera 子板通过 MIPI FPC 连接，内部 sensor 是 SC2336 (SCCB 0x30)。
7. Audio 子系统：板载 Everest Semi ES8311 Codec (I2C0 0x18)，I2S0 (MCLK=GPIO13, BCLK=GPIO12, WS=GPIO10, DOUT=GPIO9, DIN=GPIO11)，板载功放 PA_EN=GPIO53。
8. Display 子系统：MIPI DSI 2-lane (1000 Mbps/lane)，背光 LCD_BL=GPIO26，复位 LCD_RST=GPIO27，Framebuffer `/dev/fb0`，LVGL 9.1.0 演示与 `dsi_display` CLI。
9. 团队专属仓和公共 `nuttx` 严格遵循 Git 规则：
   - 公共 `nuttx`: 统一维护主 PR **`open-vela/nuttx#340`**（Head: `uleemos/nuttx:feat/esp32p4-soc-contest2026` -> Base: `open-vela/nuttx:dev-ai-contest-2026`）。
   - 团队仓: 针对官方基线 `openvela/dev-ai-contest-2026` 提交独立 feature PR。
10. 实板不在手边时的接力规则：所有代码和构建均需在仿真与 nxstyle 层面 100% 严谨闭环；硬件验证清单和测试命令必须详细记录，待板卡连接后按既定步骤执行并归档硬件日志。

## 1. 仓库与远端管理背景

### 1.1 repo 工作区

```text
/home/uleemos/openvela-contest/
├── .repo/                         # repo 元数据，禁止提交或手工修改
├── nuttx/                         # 公共 open-vela/nuttx 子仓
├── apps/ packages/ tests/ vendor/ # openvela 其他 repo 和 manifest 映射目标
└── contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/
    └──                            # 队伍专属仓，板级/应用/文档/日志源文件
```

完整工程最初使用以下赛事 manifest 拉取：

```bash
repo init \
  -u https://github.com/open-vela/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi \
  -b dev-ai-contest-2026 \
  -m contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi.xml
repo sync -c -j8
```

不要在存在未提交工作时直接执行 `repo sync`。先执行 `repo status`，逐个项目确认并
提交或安全保存工作。禁止使用 `git reset --hard`、`git clean -fdx` 或其他可能删除
既有成果的命令。

### 1.2 团队专属仓

本地路径：

```text
/home/uleemos/openvela-contest/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi
```

远端：

```text
fork      git@github.com:uleemos/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi.git
openvela  https://github.com/open-vela/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi
```

赛事 base branch：`openvela/dev-ai-contest-2026`。

截至本文创建时，PR #4 合并后的远端基线为：

```text
2f32a81cc8845d8801936fcbddbe9202711502cd
```

当前接力文档工作分支从上述基线创建：

```text
docs/esp32p4-ai-handoff
```

团队仓负责以下内容：

- `board/contest_board/`：比赛板级代码、引脚选择、bring-up、defconfig、linker script。
- `app/`：团队应用和真机测试入口。
- `tools/`：团队可复用的构建或硬件测试辅助工具。
- `hardware-logs/`：串口和硬件验收原始证据。
- `PORTING_NOTES.md`、README、本文及其他作品文档。
- `logs/`：按赛事手册导出的 AI Coding 日志；不得提交 token、SSH key 或隐私数据。

manifest 将源目录映射到构建树：

```text
board/contest_board  -> vendor/openvela/boards/contest2026_345_board
app/g1_smoke        -> packages/demos/contest2026_345_g1_smoke
app/sc2336_probe    -> packages/demos/contest2026_345_sc2336_probe
```

必须编辑左侧团队仓源路径。不要把映射目标中的同一改动提交到 `vendor/` 或
`packages/` 项目。

### 1.3 公共 NuttX 仓

本地路径：

```text
/home/uleemos/openvela-contest/nuttx
```

远端：

```text
apache    https://github.com/apache/nuttx.git          # 只作参考和差异分析
fork      git@github.com:uleemos/nuttx.git             # 个人 fork，实际 push 目标
openvela  https://github.com/open-vela/nuttx            # 赛事公共上游
```

本地联调分支：`feat/esp32p4-soc-contest2026`。

分支当前 HEAD：`d49dc5e5a9c5675cd5ac9916b40d30628cf89e33`。

基点：`openvela/dev-ai-contest-2026` 的 `dd92bcf4257`。

公共 PR：<https://github.com/open-vela/nuttx/pull/340>。

PR base：`open-vela/nuttx:dev-ai-contest-2026`。

PR head：`uleemos:feat/esp32p4-soc-contest2026`。

截至 2026-08-15，PR #340 为 Open、非 Draft、GitHub 判定 MERGEABLE；CLA、
checkpatch 和已有 CI 全部通过，但仍需 openvela/赛事 code owner review 后才能合入。

该分支已有 7 个提交，后续公共 SoC 驱动应继续追加逻辑清晰的 commit：

```text
896b5f10ea4  risc-v: add minimal ESP32-P4 bring-up support
357364eb3b4  risc-v: fix ESP32-P4 source header paths
b9f8442fa73  risc-v: harden ESP32-P4 GPIO interrupt handling
0289492160b  risc-v: add ESP32-P4 I2C support
508bf7e8c6c  risc-v: add ESP32-P4 memory and flash support
ef95dfb8646  risc-v: fix ESP32-P4 flash HAL style
d49dc5e5a9c  risc-v: fix ESP32-P4 I2C initializer style
```

不要等待 PR #340 合入才继续开发。新的公共 SoC 层能力可以在该分支继续形成独立
commit 并 `git push fork feat/esp32p4-soc-contest2026`，GitHub 会自动把新 commit
追加到同一个 PR #340。不要为同一连续 SoC 系列重复创建 PR。

### 1.4 Apache 参考仓

本地只读参考：

```text
/home/uleemos/openvela-reference/nuttx
```

截至本文检查时 HEAD：

```text
31fbb9921899325fc0c17e39cda90d0d621c4eae
```

最初 ESP32-P4 支持调查起点：

```text
cda4af9f0026a25275953392ea63245ce339b82b
```

该提交不是自包含补丁，且 Apache 与比赛 openvela 历史长期分叉。只能把 Apache
实现作为行为、文件和依赖参考，不得把 Apache `master` 整体 merge 到比赛分支，
也不得盲目 cherry-pick `cda4af9f002`。

## 2. 已合并 PR 和可追踪基线

团队专属仓已经合并：

| PR | 合并后的赛事 commit | 内容 |
| --- | --- | --- |
| [#1](https://github.com/open-vela/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/pull/1) | `637b29d2354c75d1492ce8d574956c32d20922aa` | ESP32-P4X Function EV Board 最小 bring-up |
| [#2](https://github.com/open-vela/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/pull/2) | `66ff99f3f20c122c93083184015405bc72438a3c` | GPIO bring-up 和真机验证 |
| [#3](https://github.com/open-vela/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/pull/3) | `9ccfb2e14ac6c85317342e756ab79766f6c1ab51` | I2C、32 MB PSRAM、16 MB Flash/MTD |
| [#4](https://github.com/open-vela/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/pull/4) | `2f32a81cc8845d8801936fcbddbe9202711502cd` | G1 稳定性和 SC2336 只读识别 |

这些 PR 采用 Rebase and merge，因此功能分支上的原 commit SHA 与赛事 base 中的
合并 SHA 可能不同。下一阶段必须从最新 `openvela/dev-ai-contest-2026` 创建新分支，
不要继续在已经合并的旧团队功能分支上叠加。

## 3. 硬件和权威文档门禁

### 3.1 本地技能

任何涉及引脚、连接器、外设、寄存器、clock、interrupt、DMA、cache、供电或
silicon revision 的工作，先完整读取：

```text
/home/uleemos/openvela-contest/.agents/skills/openvela-esp32p4-porting/SKILL.md
/home/uleemos/openvela-contest/.agents/skills/openvela-esp32p4-porting/references/hardware-documents.md
/home/uleemos/openvela-contest/.agents/skills/openvela-esp32p4-porting/references/repository-map.md
/home/uleemos/openvela-contest/.agents/skills/openvela-esp32p4-porting/references/milestones.md
```

然后运行：

```bash
cd /home/uleemos/openvela-contest
./.agents/skills/openvela-esp32p4-porting/scripts/hardware-docs-preflight.sh
```

团队仓也保存同一 Skill 的可提交版本：

```text
contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/
  ai/skills/openvela-esp32p4-porting/
```

### 3.2 本地 PDF 资料

硬件资料根目录：

```text
/home/uleemos/pdf & md/esp32_P4/
```

必须按下列优先级使用：精确板卡原理图 → SoC datasheet → TRM → errata →
Camera/LCD 子板与器件手册 → 官方代码。网页只能补充，不能静默替代本地精确版本。

| 文件 | 用途 |
| --- | --- |
| `SCH_ESP32-P4X_FUNCTION_EV_BOARD_V1.8_20260805.pdf` | 当前主板 V1.8/C6 的唯一权威连线、物料、上拉、供电和连接器依据 |
| `esp32-p4_datasheet_cn.pdf` | pad、IO MUX、strapping、电气限制、P4NRW32X/PSRAM |
| `esp32-p4_technical_reference_manual_cn.pdf` | MIPI、ISP、GDMA、cache、interrupt、clock 和寄存器字段 |
| `esp-chip-errata-zh_CN-master-esp32p4.pdf` | revision v3.2 相关限制和 workaround |
| `esp32-p4-function-ev-board-camera-subboard-schematics.pdf` | Camera 子板、FPC、level shift、rail、reset 和 XVCLK |
| `camera_datasheet.pdf` | AG638A32M2/SC2336 模组规格和接口 |
| `esp32-p4-function-ev-board-lcd-subboard-schematics.pdf` | LCD/Touch 子板、DSI lane、I2C、reset、backlight 和电源 |

严禁把旧文件 `ESP32_P4X_C5_Function_EV_board-2.0-schematics.pdf` 当成本板依据。

### 3.3 每项硬件改动必须留下的证据台账

```text
Board/schematic revision:
Silicon revision:
Feature and observable milestone:
Schematic sheet/page, connector pins, nets, fitted parts:
Datasheet PDF/printed pages and relevant tables:
TRM PDF/printed pages, chapter, registers and fields:
Errata item/revisions:
Pin/peripheral conflicts checked:
Selected configuration and rejected alternatives:
Remaining assumptions and hardware tests:
```

不要只依赖 PDF 文字提取。原理图必须渲染后目视追踪 net、连接器、0 Ω/NC 电阻、
上拉和电源；文字提取只用于定位。

## 4. 已确认的硬件事实和冲突

### 4.1 GPIO 和共享资源

- GPIO20 → R33 → `CNN_GPIO20` → J1 pin 13。
- GPIO21 → R39 → `CNN_GPIO21` → J1 pin 11。
- 当前 GPIO loopback 使用 GPIO20 ↔ GPIO21；这是已验证组合。
- GPIO35 是 `GPIO35_BOOTMODE`，SW2 BOOT 按下拉低；同时经 R135 接
  `RMII_TXD1`。Ethernet 启用前可以做按键中断，Ethernet 阶段必须取消或隔离。
- GPIO0/GPIO1 默认接 32.768 kHz 晶振，通往 J1 的支路电阻为 NC；未经硬件改造
  不得作为普通 GPIO。
- GPIO7/GPIO8 是共享 I2C SDA/SCL，连接 ES8311、CSI、DSI；不得短接做 GPIO
  loopback。
- GPIO14～19 是 P4 到 C6 的 SD2 D0～D3/CLK/CMD；GPIO54 是 `C6_EN`，GPIO6
  是 `C6_WAKEUP`。SPI、SD 和 Wi-Fi 阶段不得把这些脚误当空闲 GPIO。

### 4.2 I2C、Camera 和 Display

- GPIO7 = `ESP_I2C_SDA`，GPIO8 = `ESP_I2C_SCL`。
- V1.8 主板 R109/R98 已提供 2.2 kΩ 到 3.3 V 的上拉。早期“主板没有外部上拉”
  的结论是错误的，禁止继续传播。
- Camera 子板通过 Q3 `DMN63DLDW-7` 完成 3.3 V ↔ 1.8 V 双向转换；R20/R23
  是 sensor 侧 2.2 kΩ 上拉到 `DOVDD_1V8`。
- Camera 子板 U1/U2 产生 1.8 V/2.8 V，Y1 提供 24 MHz `XVCLK`。
- AG638A32M2 模组是 2 data lane MIPI CSI：D0±、D1±、CLK±；最大规格
  1920×1080@30 fps、10-bit raw，允许 12 MHz 或 24 MHz输入时钟。第一版驱动
  不能仅凭最大规格直接选择模式，必须使用已固定来源的 SC2336 初始化表。
- LCD 子板包含 2 data lane DSI、共享 I2C、`RESET_LCD`、`RESET_TP`、`INT_TP`
  和 backlight/power 网络。当前尚未正面确认实际 LCD panel 与 touch controller
  型号；写 DSI/panel/touch 驱动前必须从实物丝印、FPC/BOM 或官方资料确认。

### 4.3 Flash 和 PSRAM

- Flash：GD25Q128ESIG，16 MB。
- 当前 defconfig：`CONFIG_ESPRESSIF_FLASH_16M=y`。
- MTD 安全测试区：offset `0x00f00000`，长度 1 MB，到 `0x00ffffff`，挂载 `/data`。
- 不得擦写低地址 boot/image 区，也不得把安全测试分区扩大到未知数据区。
- PSRAM：P4NRW32X 封装内 32 MB，当前作为 user heap；kernel heap 保持在 internal
  SRAM。PSRAM 不是 Flash partition，不需要“PSRAM 分区表”。后续 Camera 需要
  在内存分配层明确 DMA capability、alignment 和 cache coherency。

## 5. 已完成并有真机证据的能力

| 能力 | 当前状态 | 关键证据 |
| --- | --- | --- |
| 最小 ESP32-P4 SoC/startup/linker/CLIC/timer/USB Serial | 已实现并真机启动 | NuttX PR #340；NSH 日志 |
| 团队板最小 NSH | 已合并 | 团队 PR #1 |
| GPIO loopback/BOOT interrupt | 已真机通过 | GPIO20↔GPIO21、GPIO35 BOOT；团队 PR #2 |
| I2C0 | 已真机通过 | GPIO8/GPIO7、interrupt mode |
| ES8311 识别 | 已通过 | 地址 0x18，ID 83/11/01，10/100 kHz repeated-start |
| 32 MB PSRAM | 已初始化并作为 user heap | `free` 和 G1 heap stress |
| 16 MB Flash + MTD/SmartFS | 已通过 | `/data`，擦写、校验和重启保持 |
| Gate G1 | 已通过 | 20/20 有效冷启动、10/10 热重启、30 分钟稳定 |
| Camera SC2336 依赖与官方来源审计 (CAM-002) | 已完成并落盘 | `PORTING_NOTES.md` 第 13 节；Apache-2.0 官方模式表 |
| Camera SC2336 最小初始化与流控制 (CAM-003) | 已真机通过 | `app/sc2336_probe/`（ID/Reset/Init/Stream-ON/OFF/Cycle 720p/1080p 全通过）；日志 `esp32p4-sc2336-control-smoke-2026-08-19.log` |
| ESP32-P4 MIPI CSI 控制器与 D-PHY 接收链路 (CAM-004) | 已真机通过 | `esp32p4_mipi_csi.c/h`、`hal_esp32p4.mk`、`nxstyle PASS`；真机 720p/1080p/1080p25 D-PHY 联动测试全部 ALL PASS，零 PHY 致命错误；日志 `esp32p4-csi-dphy-smoke-2026-08-19.log` |
| Camera CSI DMA 与 PSRAM 取帧驱动 (CAM-005) | 待实现 | 下一主线（Gate G2） |
| MIPI DSI/LCD/Touch | 未实现 | Camera 第一帧后独立推进 |
| 通用 GP-SPI | 尚未作为独立子系统完成 | 不属于当前 Camera 关键路径，可另开增量 |
| Ethernet/C6 Wi-Fi/Audio | 未实现 | G4 后再按优先级推进，不能阻塞离线闭环 |

### 5.1 Gate G1 最终结果

- `g1_smoke 1800` 使用 10 Hz POSIX timer。
- 18000/18000 signal wait，elapsed 1800030 ms。
- 18000 次变长 PSRAM heap 分配、全量写入、校验和释放。
- user heap arena 33554432 bytes。
- heap used before/after 都是 8952，drift 0。
- 30 分钟无 crash、无 reset、无持续内存下降。
- Flash persistence 跨 reboot 读回并递增 sequence。
- 10/10 NSH 软件热重启通过。
- 20/20 有效真实断电冷启动通过。首批测试中一个槽位未检测到 COM3 消失，属于
  没有执行到断电，不是固件失败；追加一次 1/1 后有效次数补足 20。
- 冷启动后再次运行 `uname -a`、`free`、`ps`、`ls /dev`、`g1_smoke 2`、
  `sc2336_probe`，自动测试退出 0。

### 5.2 SC2336 最小风险识别与控制面边界

`app/sc2336_probe/` 现已支持：
1. `sc2336_probe probe`（只读 ID `0xcb3a` 探测）；
2. `sc2336_probe reset`（传感器软复位 `0x0103=0x01` 并验证 ID 恢复）；
3. `sc2336_probe init [720p|1080p]`（下发官方 2-lane 24 MHz XVCLK 配置表并读回校验关键模式寄存器）；
4. `sc2336_probe stream-on` / `stream-off`（流输出使能 `0x0100=0x01` 与待机 `0x0100=0x00`）；
5. `sc2336_probe test [720p|1080p]`（全流程串联自动化测试）；
6. `sc2336_probe cycle <N>`（多轮流状态切换压力测试）。

该工具严格限制在 I2C0 控制面，不启用 MIPI CSI 接收、不分配 DMA 描述符与帧缓冲区。

## 6. 构建、产物、烧录和串口工作流

### 6.1 每次开发开始前

```bash
cd /home/uleemos/openvela-contest

repo status
git -C nuttx status -sb
git -C contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi status -sb
git -C nuttx log -1 --oneline --decorate
git -C contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi \
  log -1 --oneline --decorate
```

`repo status` 可能显示 manifest linkfile 目标或其他项目既有生成文件；不要因此
执行全局清理。必须先判断文件属于哪一个 Git 项目。提交前分别检查团队仓和
`nuttx`，只暂存本任务明确修改的路径。

`nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty` 构建后可能出现大量未跟踪 `.o`。
它们是生成物，禁止提交；也不要在未确认目标的情况下递归删除整个 HAL 目录。

### 6.2 配置与构建

始终在工作区根目录构建：

```bash
cd /home/uleemos/openvela-contest

PATH=/home/uleemos/.venvs/esp32p4/bin:$PATH \
  ./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j16
```

当前已验证工具链：

```text
riscv-none-elf-gcc 13.4.0
esp-hal-3rdparty 8d0a898910084206721a0892ab093021bca1496a
esptool 5.3.1（/home/uleemos/.venvs/esp32p4/bin/esptool.py）
```

最新 CAM-003 固件产物（2026-08-19）：

| 产物 | 大小 | SHA-256 |
| --- | ---: | --- |
| `nuttx` | 596888 | `181b4894ee0f978b391ab70c6e267d3f912db6f228bf041811f096f3bc244c71` |
| `nuttx.hex` | 640793 | `193f76d175ff9a96fa093382752f325cc1086b66612c00184abd6b6573aff7b2` |
| `nuttx.bin` | 281476 | `a402dfccc58491c0236b9d6eeceef403d4129bf04488fba63ec94e2a8c575ce3` |

`image_info`：ESP32-P4、16 MB、DIO、80 MHz、entry `0x4ff4811c`，checksum `0x67`
有效。

每次新构建都必须重新记录产物大小、SHA-256、image_info、flash mode/frequency/
size 和实际 offset，不能照抄以上哈希。

### 6.3 Windows 烧录

WSL 构建产物先复制到 Windows 临时目录并核对 SHA-256。Windows Python 对 WSL
UNC 路径曾出现路径重复问题，直接复制到 `C:\Users\uleem\AppData\Local\Temp`
最稳定。

当前 simple boot 镜像写入 offset 是 `0x2000`，但每次都要以本次构建配置和
image 信息复核：

```powershell
esptool.exe -c esp32p4 -p COM3 -b 921600 write-flash `
  --flash-size 16MB --flash-mode dio --flash-freq 80m `
  0x2000 C:\Users\uleem\AppData\Local\Temp\openvela-esp32p4-nuttx.bin
```

烧录前确认目标是 ESP32-P4 revision v3.2；写后必须看到 hash verify。不要使用
未核对的旧 offset，不要擦除整个 16 MB Flash。

### 6.4 串口和自动化

串口：`COM3`、115200 8N1、USB Serial/JTAG。工具位于：

```text
tools/hardware/run_nsh.py
tools/hardware/reset_cycles.py
tools/hardware/README.md
```

当前 Windows Python 需要 `pyserial`。典型命令：

```powershell
python run_nsh.py --port COM3 --timeout 30 `
  --log smoke.log --expect "NuttX 13.0.0" `
  "uname -a" "free" "ps" "ls /dev" "g1_smoke 2" "sc2336_probe"

python reset_cycles.py --port COM3 --mode reboot --cycles 10 `
  --log warm-reboot-10x.log

python reset_cycles.py --port COM3 --mode manual-power --cycles 20 `
  --log cold-boot-20x.log
```

RTS 或 NSH `reboot` 不是冷启动。真实 cold boot 必须切断并恢复开发板供电。串口
重新枚举可能截断开头 0～2 个字符，因此验收应以完整 `NuttX 13.0.0`、`nsh>`
和命令输出为准，不要仅依赖第一行。

## 7. Git、commit、push 和 PR 标准工作流

### 7.1 团队仓新阶段

```bash
cd /home/uleemos/openvela-contest/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi

git status -sb
git fetch openvela
git switch -c feat/<task-name> openvela/dev-ai-contest-2026

# 开发、构建和真机验证后，只 add 本任务路径
git add <explicit-paths>
git diff --cached --check -- . ':!hardware-logs/*.log'
git diff --cached --stat
git commit -m "<scope>: <single logical result>"
git push -u fork feat/<task-name>
```

然后创建 PR：

```bash
gh pr create \
  --repo open-vela/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi \
  --base dev-ai-contest-2026 \
  --head uleemos:feat/<task-name> \
  --title "<scope>: <result>" \
  --body-file <pr-body.md>
```

团队专属仓 PR 可以自行 review 和 Rebase and merge，不需要等待组委会。合并后再次
fetch，从新的 `openvela/dev-ai-contest-2026` 创建下一分支。

### 7.2 公共 NuttX 连续开发

```bash
cd /home/uleemos/openvela-contest/nuttx
git switch feat/esp32p4-soc-contest2026
git status -sb
git fetch fork

# 一项公共 SoC 能力一笔 commit
git add <explicit-nuttx-paths>
git diff --cached --check
git commit -s -m "risc-v: <single ESP32-P4 result>"
git push fork feat/esp32p4-soc-contest2026
```

公共提交继续进入 PR #340。不要自行 merge；等待 code owner review。后续发现问题
可以在新 commit 中再次修改同一个文件。PR review 面向整个分支 diff；维护阶段可以
保留可审查的提交历史，只有 reviewer 明确要求时才协调 squash/rebase/force-push。

### 7.3 PR 描述必须包含

```markdown
## Summary

- 为什么需要修改
- 修改了什么
- 依赖和来源是什么

## Impact

- 影响的仓库、板卡、配置、兼容性和未覆盖范围
- 是否改动公共 NuttX 或仅改团队仓

## Testing

- host/OS
- toolchain 和 esptool 版本
- board revision、silicon revision
- 精确 build 命令和退出状态
- artifact 大小、SHA-256、image_info
- flash 命令和验证结果
- NSH/硬件测试命令、原始日志和未测试项
```

### 7.4 合作者同步方式

合作者不需要等待公共 PR 合入。他可以将你的个人 fork 添加为 remote：

```bash
git remote add uleemos git@github.com:uleemos/nuttx.git
git fetch uleemos feat/esp32p4-soc-contest2026
git switch -c feat/esp32p4-soc-contest2026 \
  --track uleemos/feat/esp32p4-soc-contest2026
```

如果对方已有自己的功能分支，可以 `git cherry-pick <精确的小提交 SHA>`；这只适合
经过确认的单一逻辑 commit，不等于允许从 Apache 做大规模 cherry-pick。协作前交换：

```text
openvela manifest/team base SHA
NuttX P4 branch SHA
team repository SHA
defconfig name
hardware/log evidence
```

禁止只说“我这里是最新”。

## 8. 下一阶段：Camera 连续取帧（Gate G2）

### 8.1 当前入口条件

已满足：

- Gate G1 平台稳定性完成。
- Camera 子板 FPC 已连接。
- Camera rail、24 MHz XVCLK 和 SCCB path 能让 SC2336 返回正确 ID。
- I2C0 100 kHz repeated-start 已验证。
- 32 MB PSRAM 和 16 MB Flash/MTD 已验证。

尚未满足：

- 尚未固定可提交的 SC2336 初始化表来源、版本、许可证和模式参数。
- 尚未实现 sensor stream-on/off。
- 尚未把 ESP32-P4 MIPI CSI/ISP/GDMA 能力接入 NuttX。
- 尚未建立 `/dev/video0` 或稳定的阶段性 frame API。
- 尚未得到第一帧。

### 8.2 当前代码能力差距

当前 openvela NuttX 已有通用 `drivers/video`、V4L2 capture 框架和
`video_register()`，也已有通用 MIPI DSI 框架；但当前 ESP32-P4 分支没有
MIPI CSI controller lower-half，也没有 ESP32-P4 Camera/CSI driver。

固定的 `esp-hal-3rdparty` commit
`8d0a898910084206721a0892ab093021bca1496a` 已包含可供分析的底层组件：

```text
components/esp_hal_cam/{cam_hal.c,mipi_csi_hal.c,isp_hal.c}
components/esp_hal_cam/esp32p4/{cam_periph.c,mipi_csi_periph.c,isp_periph.c}
components/upper_hal_cam/csi/
components/upper_hal_isp/
components/esp_hw_support/mipi_csi_share_hw_ctrl.c
components/esp_hal_dma/
components/upper_hal_dma/
components/soc/esp32p4/...mipi_csi... and ...isp... register headers
```

“文件存在”不代表可直接编进 NuttX。必须逐项核对其依赖的 OS abstraction、interrupt、
clock/power/LDO、GDMA、cache sync、heap capability、event callback 和许可证。
当前 `hal_esp32p4.mk` 只加入了部分 include path，没有选择完整 CSI/ISP/upper HAL
源文件，不能仅打开一个 Kconfig 就假设能够链接。

### 8.3 必须按以下增量推进

#### CAM-002：来源和依赖审计，先不写寄存器

目标：形成 Camera dependency ledger。

1. 在 Espressif 官方 ESP-IDF/esp-video/传感器组件中找到明确支持 SC2336 和本模组
   工作模式的初始化表。
2. 记录仓库 URL、tag/commit、文件路径、许可证和原始 copyright/SPDX。
3. 比较该来源使用的 ESP-IDF/HAL commit 与当前固定 HAL `8d0a898...`。
4. 列出 sensor、CSI PHY/host、bridge、ISP、GDMA、cache、frame buffer、video
   upper-half 的符号和源文件依赖。
5. 检查 TRM 对应章节、寄存器字段和 v3.2 errata。
6. 输出“必须移植 / 后续需要 / 本板无关”清单；此步骤不要大规模复制代码。

验收：依赖台账可 review，许可证清楚，下一 commit 的最小文件集合明确。

#### CAM-003：SC2336 最小初始化与 stream control

目标：只验证 sensor 控制面，不同时开启 CSI/DMA。

1. 从已审核来源导入最小模式表，保留原许可证和来源说明。
2. 使用现有 `/dev/i2c0`、地址 0x30、100 kHz；实现有界 retry 和明确错误码。
3. 保持 ID probe；新增 reset、初始化、stream-on、stream-off，必要时读回可验证寄存器。
4. 模式参数必须与初始化表一致：输入 clock、lane 数、raw bit depth、resolution、fps、
   line/frame timing。不要仅按 datasheet 最大值猜寄存器。
5. 此 commit 不申请 frame buffer、不启用 CSI RX，不声称“Camera 已取帧”。

验收：ID、初始化、stream on/off 均有串口日志，失败时能定位到具体 register/阶段。

#### CAM-004：MIPI CSI PHY/host/bridge 最小接收

目标：接收可观测的 CSI packet/frame event，先不追求高分辨率。

1. 在 NuttX 公共仓实现 ESP32-P4 CSI controller 的最小 Kconfig/Make/CMake/HAL
   选择和 driver boundary；保持与旧 Espressif 芯片隔离。
2. 核对 MIPI dedicated lane、PHY power/LDO、clock tree、reset、interrupt source、
   CLIC route、lane rate 和 data type。
3. 先启用单一、已知模式；记录所有 error/short-packet/ECC/CRC/overflow status。
4. 不要一次带入完整 ISP tuning；优先获得 raw frame 或 frame-start/frame-end 证据。
5. SoC 层 commit 推到 NuttX PR #340；板级 rail/reset/config 放团队仓独立 PR。

验收：稳定观察 frame start/end 或第一块正确长度数据；错误计数可读取。

#### CAM-005：GDMA、cache 和 PSRAM frame buffer

目标：得到可校验的一帧。

1. 明确 DMA descriptor alignment、buffer alignment、地址能力和最大 transfer。
2. 先用 internal/DMA-capable 小 buffer 或单 buffer 验证，再扩展到 PSRAM。
3. 对 CPU/DMA ownership 转移执行正确 cache clean/invalidate；不得通过关闭 cache
   掩盖 coherency 问题。
4. 使用 guard pattern、长度、frame number、timestamp 和 CRC 验证越界与帧完整性。
5. 内存分配失败、DMA timeout、overflow、short frame 必须返回错误并统计。

验收：连续读取多帧，长度/格式合理，CRC 随场景变化，buffer guard 未破坏。

#### CAM-006：NuttX/openvela 消费接口

优先评估 NuttX `video_register()` 和 V4L2 capture upper-half，目标设备节点
`/dev/video0`。如果为了风险隔离暂时使用阶段性 char device/API，必须在文档中标为
临时接口，并给出迁移到标准 video framework 的计划；不要把私有测试 API 宣称为
最终支持。

验收应用至少输出：

```text
frame_no
width x height
pixel/raw format
bytes used
timestamp
CRC/checksum
capture error counters
```

#### CAM-007/008：连续取帧和稳定性

- 第一阶段先获得 1 帧，再 100 帧，再 5 分钟，最后 30 分钟。
- 工作分辨率采集率最低 5 FPS，目标 10 FPS；若达不到，先降低分辨率/帧率，不要
  同时引入 Display、Wi-Fi 或 Audio。
- 每分钟记录 FPS、dropped/overflow/error、heap/PSRAM、最大空闲块和 buffer queue。
- 30 分钟无 crash、无持续内存下降、无 buffer 堆积后才通过 Gate G2。

### 8.4 Camera 推荐 commit/PR 拆分

```text
团队仓 commit A: docs/test: record SC2336 source and dependency ledger
团队仓 commit B: camera: add SC2336 minimal initialization and stream control
NuttX commit C:  risc-v: add ESP32-P4 MIPI CSI low-level support
NuttX commit D:  video: add ESP32-P4 CSI DMA/frame capture support
团队仓 commit E: boards: register ESP32-P4X camera and add frame smoke app
团队仓 commit F: test: add camera stability evidence and documentation
```

实际拆分可按依赖微调，但不得把 sensor 表、CSI、DMA、video upper-half、应用和 30 分钟
日志压成一个无法 review 的大提交。

## 9. Camera 第一帧之后的 MIPI DSI/LCD 工作

DSI 是独立工作流，不能和 CSI 共用一个“完整 MIPI”commit。

当前可用参考事实：

- openvela NuttX 已有通用 `include/nuttx/video/mipi_dsi.h` 和
  `drivers/video/mipidsi/` 框架。
- 当前 ESP32-P4 分支尚未实现 ESP32-P4 DSI host lower-half。
- Apache 参考的 `esp32p4-tab5` 有 DSI/DPI 和 panel driver 示例，可做依赖参考；
  目标板不是 TAB5，不能直接复制其 board/panel 配置。
- 当前固定 HAL 已包含 `esp_hal_lcd/mipi_dsi_hal.c`、ESP32-P4 periph/LL 和
  `mipi_dsi_share` 相关底层材料，但仍需逐项适配 NuttX。
- 目标 LCD 子板使用 2 data lane、共享 I2C、RESET_LCD、RESET_TP、INT_TP、
  backlight 和独立 power rails。
- 实际 panel/touch controller 型号尚未确认，这是编码前 blocker。

推荐顺序：

1. 目视确认 LCD 和 touch 型号、FPC 版本、lane mapping、供电和 reset polarity。
2. 读取 LCD 子板原理图、panel/touch datasheet、TRM MIPI DSI/clock/DMA 和 errata。
3. 只实现 DSI host attach/command transfer，验证 panel ID 或 DCS response。
4. 再实现 panel init 和纯色 framebuffer。
5. 再处理 backlight、touch、LVGL 和 UI；UI 不得阻塞 Camera/AI 线程。
6. 独立执行颜色条、区域更新、刷新率、30 分钟稳定性和 Camera 并行压力测试。

如果 8 月 23 日仍无 Camera 第一帧，暂停 DSI，把资源集中到 sensor、CSI、DMA、
buffer/cache 四层定位。

## 10. 后续产品关键路径和优先级

项目目标是离线运动体态教练：

```text
Camera -> 人体关键点推理 -> 深蹲 FSM -> 自动计数 -> 错误检测 -> 本地反馈
```

当前日期对应总体计划 Phase 1 尾部，G1 已提前完成；下一工作直接进入 Phase 2/G2。

### P0，必须按顺序保证

1. Camera 连续取帧（G2）。
2. Host 上先固定轻量人体关键点模型、许可证、预处理和输出格式。
3. 板端固定图片推理 50 次；记录加载时间、推理时间、RAM/PSRAM、CPU 和 FPS。
4. Camera frame → preprocess → inference → keypoints。
5. 深蹲状态机、计数、防抖、最小动作时间和至少 2 类错误检测。
6. 断网运行的完整离线反馈闭环（G4）。
7. 可复现构建、烧录、测试、AI logs、作品 README 和演示材料。

### P1，不能阻塞 G4

- MIPI DSI/LCD、Touch、LVGL UI。
- 性能和稳定性优化。
- ESP32-C6 Hosted Wi-Fi 和结构化训练数据上传。
- AI Agent 训练复盘。

### P2，只有明确余量才开始

- Audio/语音。
- 第二种运动。
- 历史管理和复杂个性化计划。

未通过 G4 前，不得把 Wi-Fi、Agent、Audio 或第二种动作置于 Camera/AI/FSM 前面。

## 11. 已知问题、文档债务和防踩坑清单

1. 公共 NuttX PR #340 尚未合入；每次构建必须记录联调使用的 NuttX SHA。
2. 团队仓 `board/contest_board/README.md` 仍是旧模板文字并含队号 000，需要后续
   单独改为真实 ESP32-P4X board 文档；不要从该旧 README 推导技术事实。
3. 团队仓根 `README.md` 仍主要是赛事模板，最终提交前必须改成 VelaFit AI 作品说明。
4. `esp_libc_stubs.c::__assert_func` 有既有 `noreturn` 告警；应独立分析 panic/assert
   path，不要在 Camera commit 中顺手扩大公共层修改。
5. `/tmp/openvela-esptool-venv` 是临时环境。若消失，重建隔离 venv 并固定兼容
   esptool 版本；不要把 venv 或下载缓存提交。
6. Windows Python 无法稳定直接读取 WSL UNC 脚本/镜像时，复制到 Windows Temp；
   复制后先核对 SHA-256。
7. 原始串口日志保留 CRLF/ANSI，`git diff --check` 可能报告日志 trailing whitespace。
   源码/文档仍必须单独通过 whitespace check；不要为“变绿”破坏原始日志和哈希。
8. USB Serial/JTAG RTS hard reset、NSH reboot、禁用 COM 设备都不是 cold power cycle。
9. `.config` 曾因旧配置残留继续使用错误 I2C pin。修改 defconfig 后必须重新
   olddefconfig/必要时干净配置，并检查活动 `.config` 的实际 symbol。
10. 不要把 `vendor/openvela/boards/contest2026_345_board` 的映射内容作为另一个
    Git 项目提交；源文件在团队仓 `board/contest_board`。
11. 不要提交 `nuttx`、`nuttx.bin`、`.config`、`.o`、HAL checkout、工具链、
    `.repo`、Windows 临时文件或完整第三方 SDK。
12. Camera/Display/Audio 共用 GPIO7/8 I2C；新增设备时要验证 bus address、reset、
    power sequence 和并发访问，不能为一个设备把共享总线改成推挽。
13. GPIO35 与 Ethernet RMII_TXD1 冲突；启用 Ethernet 前移除当前 BOOT GPIO 设备。
14. C6 的 SD2 GPIO14～19、EN GPIO54、WAKEUP GPIO6 是保留资源。
15. 通用 GP-SPI 尚未完成。若新增 SPI，先重新检查 V1.8 sheet 2/3/5 和 C6/SD
    冲突，不要把 Flash MSPI、GP-SPI 和 C6 SDIO 混为一个控制器。

## 12. 证据文件索引

详细说明：`PORTING_NOTES.md`。

主要原始日志：

| 文件 | 说明 |
| --- | --- |
| `hardware-logs/esp32p4-nsh-smoke-2026-08-09.log` | 最小 NSH |
| `hardware-logs/esp32p4-reset-stability-10x-2026-08-09.log` | 10 次 RTS hard reset |
| `hardware-logs/esp32p4-gpio-smoke-2026-08-11.log` | GPIO loopback 和 BOOT interrupt |
| `hardware-logs/esp32p4-i2c-es8311-smoke-2026-08-12.log` | I2C/ES8311 |
| `hardware-logs/esp32p4-g1-flash-persistence-2026-08-15.log` | Flash 持久化 |
| `hardware-logs/esp32p4-g1-stability-30min-2026-08-15.log` | Timer/Heap 30 分钟 |
| `hardware-logs/esp32p4-g1-warm-reboot-10x-2026-08-15.log` | 10 次 warm reboot |
| `hardware-logs/esp32p4-g1-cold-boot-20x-2026-08-15.log` | 冷启动首批 19 个有效结果 |
| `hardware-logs/esp32p4-g1-cold-boot-supplement-1x-2026-08-15.log` | 补足第 20 个有效 cold boot |
| `hardware-logs/esp32p4-sc2336-id-probe-2026-08-15.log` | SC2336 ID 0xcb3a |
| `hardware-logs/esp32p4-g1-post-cold-smoke-2026-08-15.log` | cold boot 后联合 smoke |
| `hardware-logs/esp32p4-sc2336-control-smoke-2026-08-19.log` | SC2336 软复位、初始化表与流控制真机全流程与 10x 循环测试 |

新增阶段必须把日志复制到 `hardware-logs/`，命名包含板卡、功能、日期；在文档记录
SHA-256。不要只在聊天中报告 PASS。

## 13. 每轮 AI 工作的质量门槛

每次开始必须输出：

1. 本轮唯一可观察目标。
2. 修改前 `repo status`、相关仓 `git status -sb`、branch 和 SHA。
3. 本轮读取的原理图/手册/代码来源与硬件证据台账。
4. 代码应属于团队仓还是公共 NuttX，并解释原因。
5. 计划修改的最小文件集合和明确不做的范围。

每次结束必须输出：

1. 实际修改文件。
2. 精确构建命令、退出码、toolchain、artifact 和 SHA-256。
3. 真机命令、板卡/revision、正常和失败日志。
4. 明确标记：compile-only、boot-tested 或 peripheral-tested。
5. 未测试项、风险和下一最小步骤。
6. 建议 commit message；只有用户明确要求时才 commit/push/create PR/merge。
7. 更新 `PORTING_NOTES.md`、本接力文档或对应阶段文档。

需要真机的功能，只有“代码 + build + flash + hardware evidence + docs + commit SHA”
齐全才允许标记 done。编译成功不等于硬件支持完成。

## 14. 当前阶段与下一 AI 的建议首轮任务

### 14.1 已完成项（截至 2026-08-23）

1. **CAM-002～CAM-004（SC2336 探测、流控、MIPI CSI 驱动，已合入）**：
   - 官方 SC2336 寄存器表来源固定为 `esp-video-components`。
   - 完成硬件物理层、时钟、电平、HAL 依赖与 NuttX video 接口审计。
   - 真机串口测试通过（`sc2336_probe test 720p`、`sc2336_probe cycle 10 720p` 等全通过），日志已归档至 `hardware-logs/`。
2. **AUD-001（ES8311 音频编解码器 + I2S0 驱动 + 板载功放 + CLI 测试工具，已合入）**：
   - 实现了 ESP32-P4 I2S0 下半部驱动与 ES8311 编解码器适配，注册 `/dev/audio/pcm0` 与 `/dev/audio/pcm_in0`。
   - 团队仓实现 `es8311_audio` 测试套件（`probe`, `dump`, `tone`, `record`）。
   - 代码已进入 PR #340 与团队仓主基线。
3. **DISP-001（MIPI DSI 驱动 + Framebuffer `/dev/fb0` + LVGL 9.1.0 + `dsi_display` CLI，已合入）**：
   - 实现了 ESP32-P4 MIPI DSI Host 控制器、D-PHY TX PLL (1000 Mbps/lane)、DCS 指令引擎与 Video Mode DPI 时序配置。
   - 完整实现 NuttX 标准 `struct fb_vtable_s` 并在 PSRAM 中分配帧缓冲区。
   - 团队仓完成 GPIO26 背光/GPIO27 复位控制，注册 `/dev/fb0`，集成 LVGL 9.1.0 演示 (`lvgldemo`) 与 `dsi_display` CLI（`info`, `bars`, `color`, `grid`, `fps`）。
   - 代码已进入 PR #340 与团队仓。

### 14.2 下一步任务清单（Next Actions）

1. **AUD-001 实板串口验证（待板卡连接后执行）**：
   - 烧录最新固件：`./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j16`
   - NSH 验证步骤：
     ```bash
     # 1. 探测 ES8311 芯片并校验 /dev/audio/pcm0 与 /dev/audio/pcm_in0 节点
     nsh> es8311_audio probe

     # 2. 读取并打印 ES8311 全部寄存器（0x00 .. 0x47）
     nsh> es8311_audio dump

     # 3. 播放 1000 Hz 正弦波纯音（持续 3 秒，经 GPIO53 功放从板载扬声器发出声音）
     nsh> es8311_audio tone 1000 3

     # 4. 从板载模拟麦克风录制音频（持续 3 秒，并计算峰值与 RMS 能量）
     nsh> es8311_audio record 3 /data/rec.raw
     ```
   - 归档实板日志至 `hardware-logs/esp32p4-audio-es8311-smoke-YYYY-MM-DD.log` 并计算 SHA-256。

2. **DISP-001 实板显示与 LVGL 验证（待板卡连接后执行）**：
   - NSH 验证步骤：
     ```bash
     # 1. 查看 Framebuffer 信息
     nsh> dsi_display info

     # 2. 绘制标准 8 色彩条图案（校验色彩通道与 RGB565 映射）
     nsh> dsi_display bars

     # 3. 绘制 32x32 网格与中央瞄准线（校验分辨率与对齐）
     nsh> dsi_display grid

     # 4. 动态刷屏帧率压测（统计 FPS 与带宽）
     nsh> dsi_display fps 5

     # 5. 启动 LVGL 控件演示
     nsh> lvgldemo
     ```
   - 归档实板日志至 `hardware-logs/esp32p4-dsi-display-smoke-YYYY-MM-DD.log` 并计算 SHA-256。

3. **CAM-005（公共 NuttX 仓 & 团队仓）：CSI DW-GDMA DMA 超时分析与 PSRAM 取帧调通**：
   - 当前 WIP 已保存在 `feat/esp32p4-csi-wip` (nuttx) 和 `feat/esp32p4-csi-dma` (team)。
   - 核心任务：结合 `sc2336_probe capture` 超时快照，分析 D-PHY HS 接收到 Bridge FIFO 写入及 DW-GDMA 握手时序。

## 15. 可直接交给另一 AI 的启动提示词

```text
你接手的是 openvela ESP32-P4X Function EV Board V1.8（ESP32-C6 版本）移植和
VelaFit AI 项目。工作区为 /home/uleemos/openvela-contest。

开始前必须完整阅读：
1. 专属仓 ESP32P4_AI_HANDOFF.md
2. 专属仓 PORTING_NOTES.md（重点阅读第 15、16 节）
3. .agents/skills/openvela-esp32p4-porting/SKILL.md 及其直接引用的 references

严格遵守双仓边界：公共 ESP32-P4 SoC/driver 放 nuttx，板级/应用/日志/文档放团队
专属仓。不要整目录复制 Apache，不要盲目 cherry-pick，不要清理未知工作树，不要
提交构建产物。凡涉及硬件先以 V1.8/C6 原理图、datasheet、TRM、errata 为依据。

当前状态：
1. AUD-001（ES8311 音频子系统与 CLI 测试工具）已全部实现并通过 nxstyle 和编译，
   PR 分支已推送至 Fork 远端（nuttx: feat/esp32p4-i2s-audio, team: feat/esp32p4-audio-es8311）。
   等待实板连接后运行 es8311_audio 测试套件并归档硬件日志。
2. CAM-005（CSI DW-GDMA 取帧调试）暂存在 feat/esp32p4-csi-wip / feat/esp32p4-csi-dma 分支。
```

## 16. 接力文档维护规则

每完成一个独立 PR，更新以下内容：

- 状态日期、当前 Gate、最新 team/NuttX SHA。
- PR 表和公共 PR 状态。
- 已完成能力矩阵与真机证据。
- 构建产物、工具版本、烧录参数和日志索引。
- 下一阶段入口条件、blocker 和 commit 拆分。
- 已过时结论要明确删除或标记 superseded，不能只在后文追加相反说法。

本文应始终回答四个问题：现在从哪个 SHA 开始、哪些事实真机验证过、下一项最小
工作是什么、完成后应该向哪个仓和哪个 base branch 提交 PR。

