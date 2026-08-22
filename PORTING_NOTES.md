# ESP32-P4X Function EV Board 移植记录

## 1. 文档范围

本文记录 Apache NuttX ESP32-P4 支持与比赛 openvela 基线之间的依赖和差异，作为后续增量移植的依据。

当前实板的唯一板级基准为 `ESP32-P4X_FUNCTION_EV_BOARD` V1.8
（2026-08-05），板载无线模块 U1 是 `ESP32-C6-MINI-1`。对应原理图为
`SCH_ESP32-P4X_FUNCTION_EV_BOARD_V1.8_20260805.pdf`，SHA-256
`a87457fdcc4c8b3b3b5461603ce87a82b2ce9e99d851e4e615d4f5732aa649a5`。
此前使用的 C5 V2.0 原理图不再作为本项目的板级证据。

当前阶段已在依赖分析基础上完成最小 P4 USB NSH 构建：

- 不复制 Apache 整个目录。
- 不 cherry-pick Apache 提交。
- 只按文件和接口边界增量加入 P4 SoC、公共层适配和团队板级代码。
- 不启用或移植 PSRAM、Ethernet、LCD、Camera、Audio 等后续外设。
- Apache 源码只作为功能和来源参考，比赛交付仍以 openvela `dev-ai-contest-2026` 为基线。

分析日期：2026-08-09。

## 2. 仓库边界和当前状态

### 2.1 工作区

- repo 工作区：`~/openvela-contest`
- openvela NuttX 项目：`~/openvela-contest/nuttx`
- 团队项目：`~/openvela-contest/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi`
- Apache 参考项目：`~/openvela-reference/nuttx`

### 2.2 Git 状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| openvela `nuttx` | `feat/esp32p4-soc-contest2026` | 从 `dd92bcf4257` 创建，已落地最小 P4 SoC 支持和兼容层 |
| 团队项目 | `feat/esp32-p4x-bringup` | 跟踪 `fork/feat/esp32-p4x-bringup` |
| Apache 参考项目 | `master` at `31fbb9921899` | 已有成功的 P4 usbconsole 构建产物 |

开始分析前已经存在、必须保留的工作包括：

- 团队 manifest 修改：`contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi.xml`
- 团队项目未跟踪的 `ai/`
- repo 中 apps/testing 等其他现有修改和 manifest linkfile 映射

本轮没有清理、回退或覆盖这些内容。为读取 Apache partial clone 中缺失的提交对象，执行了 `git fetch apache --tags`；这只更新 `nuttx/.git` 中的远端引用和对象，不改变工作树文件。

## 3. Apache 可复现参考

### 3.1 提交和配置

| 项目 | 值 |
| --- | --- |
| 初始 P4 支持提交 | `cda4af9f0026a25275953392ea63245ce339b82b` |
| 已验证 Apache 参考提交 | `31fbb9921899325fc0c17e39cda90d0d621c4eae` |
| 2026-08-09 获取的 Apache master | `b8e26b127e4c0b17652aba1770da5cda6e6b6fd1` |
| Board config | `esp32p4-function-ev-board:usbconsole` |
| 芯片 revision 配置 | 最低 rev 3.1，范围 3.1 至 3.99 |
| Flash | 4 MiB、DIO、80 MHz |
| Boot | Espressif simple boot |
| Console | USB Serial/JTAG；UART0 未启用 |

从 `31fbb9921899` 到本次获取的 Apache master，以下路径没有新增提交：

```text
arch/risc-v/src/esp32p4/
arch/risc-v/include/esp32p4/
arch/risc-v/src/common/espressif/
boards/risc-v/esp32p4/
tools/espressif/
```

因此本轮分析使用的已验证参考没有遗漏上述路径上的更新。

### 3.2 工具链和产物

已存在构建目录：`~/openvela-reference/nuttx/build-p4-usb`。

| 项目 | 值 |
| --- | --- |
| 构建系统 | CMake + Ninja |
| 编译器 | `/home/uleemos/toolchains/riscv-none-elf-gcc-14.2.0-3/bin/riscv-none-elf-gcc` |
| 版本 | xPack GNU RISC-V Embedded GCC 14.2.0 |
| `nuttx` | 412200 bytes；SHA-256 `4f6f944a5d12178a96fb817386c064a158bc6edc2cc67692024678a0de4412c8` |
| `nuttx.bin` | 228072 bytes；SHA-256 `48f7198e2c371387920bf6e8a9dfb47390679aa89059f7c87c2ce09f9ed0f2a4` |
| `nuttx.hex` | 439335 bytes；SHA-256 `3cde8d89ca9507648d4636321eab6f6a567ceb36f9a04b11820b8394ed145150` |

本轮只检查上述既有构建证据，没有重新构建、烧录或执行新的硬件测试。检查清单已说明该固件此前在同一块 ESP32-P4X Function EV Board 上通过 USB Serial/JTAG 进入 `NuttX-13.0.0` 的 `nsh>`。

## 4. 为什么不能复制目录或 cherry-pick

### 4.1 `cda4af9f002` 不是自包含补丁

该提交修改 98 个文件，统计为 9668 insertions、46 deletions。它同时包含：

- P4 SoC 入口。
- Function EV Board 及 26 个外设/测试配置。
- ADC、I2C、I2S、MCPWM、PCNT、RMT、SPI、TWAI、WDT 等与最小 USB NSH 无关的内容。
- 对 C3、C6、H2 和 RISC-V 公共异常代码的修改。
- 镜像生成和烧录工具修改。

初始 P4 SoC 目录本身只有少量入口文件：

```text
arch/risc-v/include/esp32p4/chip.h
arch/risc-v/src/esp32p4/Kconfig
arch/risc-v/src/esp32p4/Make.defs
arch/risc-v/src/esp32p4/esp_chip_rev.c
arch/risc-v/src/esp32p4/hal_esp32p4.mk
```

复位、时钟、cache/MMU、中断、timer、UART 和 USB Serial/JTAG 的主要实现来自 Apache 在该提交之前已经演进的 `arch/risc-v/src/common/espressif` 和外部 HAL。仅复制 P4 目录会同时缺少头文件、符号、链接脚本和镜像规则。

### 4.2 两个 Git 历史已经长期分叉

openvela 基线和 Apache 初始 P4 提交的 merge-base 是：

```text
9bbacc44ffa9846dc52c08f772cfdea4c6cda255
2018-08-22 fs/hostfs: Add support for open() append mode
```

从该 merge-base 计算：

- openvela 一侧有 39676 个独有提交。
- Apache P4 一侧有 28492 个独有提交。

这说明 Apache master 不能被视为比赛分支的线性升级源。

### 4.3 公共 Espressif 层差异过大

快照统计：

| 快照 | `arch/risc-v/src/common/espressif` 文件数 |
| --- | ---: |
| openvela `dd92bcf4257` | 79 |
| `cda4af9f002` 的父提交 | 104 |
| Apache 参考 `31fbb9921899` | 130 |

openvela `dd92bcf4257` 到 `cda4af9f002` 父提交的相关快照差异涉及 170 个文件，约 25026 insertions、7648 deletions。这里包含大量与 P4 无关的 Wi-Fi、外设和公共 API 改造，不能整体覆盖。

### 4.4 外部 HAL 不是兼容的小版本升级

| 使用方 | `esp-hal-3rdparty` commit | P4 路径情况 |
| --- | --- | --- |
| openvela `dd92bcf4257` | `9fc713a95b1ff150dd0b0647e465d3c624056bb1` | 只有一个早期 `esp_cpu_intr.c` 路径 |
| Apache 初始 P4 | `a85ce2f1bad9f745090146eb30a18d91b8ddd309` | 约 722 个 P4 路径 |
| Apache 已验证参考 | `8d0a898910084206721a0892ab093021bca1496a` | 约 722 个 P4 路径 |

`9fc713a...` 与 `a85ce2f...` 不是线性祖先关系；两者快照差异超过一万个文件。因此不能为了 P4 直接替换 openvela 的全局 HAL pin，否则可能破坏现有 ESP32-C3/C6/H2。

建议在 P4 `Make.defs` 进入公共 Espressif `Make.defs` 之前设置 P4 专用的 `ESP_HAL_3RDPARTY_VERSION`。公共文件使用 `ifndef` 设置默认版本，允许这种按芯片覆盖。具体 pin 在实现前仍需确认许可证、下载缓存行为及既有芯片的回归策略。

Apache 已验证 usbconsole 的 `compile_commands.json` 中约有 227 个 `esp-hal-3rdparty` 源文件参与编译；链接 map 的 archive dependency 部分按对象名交叉核对约有 112 个 HAL 对象进入最小镜像依赖闭包。即使只做 USB NSH，HAL 依赖仍然不可简化成少数寄存器头文件。

## 5. 依赖台账

### 5.1 最小 USB NSH 所需层次

```text
usbconsole defconfig
├── Function EV Board：boot、bringup、reset
├── P4 SoC：Kconfig、chip.h、revision、HAL source manifest
├── Espressif 公共层
│   ├── reset/start、clock、BSS、MMU/cache
│   ├── CLIC IRQ、vectors、exception
│   ├── system timer
│   └── USB Serial/JTAG、低层日志、serial registration
├── esp-hal-3rdparty 精确版本
├── P4 rev3 linker scripts
└── esptool image：simple boot offset 0x2000
```

### 5.2 文件/API 对照

| Apache 文件或功能 | openvela 对应位置 | 主要差异 | 处理原则 |
| --- | --- | --- | --- |
| `arch/risc-v/Kconfig` P4 entry | 同路径 | openvela 仍保留 `ARCH_CHIP_ESPRESSIF` 和旧 Espressif choice 模型 | 只加入 P4 选择和必要 `select`，不顺带重构 C3/C6/H2 |
| `arch/risc-v/src/esp32p4/Kconfig` | 当前不存在 | P4 双核、400 MHz、rev 3.x、cache line 和 workaround | 首版只保留 rev3.1、CPU、cache、最小 boot 所需项 |
| `arch/risc-v/include/esp32p4/chip.h` | 当前不存在 | 芯片能力由外部 HAL 的 `irq.h`、GPIO signal headers 补全 | 保留公共接口，生成/下载头文件不提交到仓库 |
| `arch/risc-v/src/esp32p4/hal_esp32p4.mk` | 当前不存在 | 初始提交有 164 条 source entry，参考版本有 213 条 | 先建立可解释的最小 source manifest；外设源后续按配置加入 |
| `esp_start.c` | 同路径 | Apache 新增 CLIC MTVT、chip revision、BSS clear、SPI flash state、MMU map；cache HAL 函数签名不同 | 以 openvela 文件为底稿按功能移植，不能覆盖 |
| `esp_irq.c/.h` | 同路径 | openvela 使用旧三参数 `esp_setup_irq`；Apache 新 API传 handler/arg，并使用 HAL interrupt handle、CLIC、SMP map | 保留旧芯片 ABI；为 P4增加兼容层或条件实现 |
| `esp_vectors.S` | 同路径 | openvela 没有 P4 CLIC `_mtvt_table`；Apache 同时支持 PLIC/CLIC | 只引入经 P4 宏保护的 CLIC vector table |
| `riscv_exception_common.S` | 同路径 | P4 需要对异常 cause 做 0..63 mask | 作为独立 RISC-V 公共修复审查 |
| `esp_timerisr.c` | 同路径 | P4 使用 `SYSTIMER_TARGET0_INTR_SOURCE`，旧芯片使用 edge source | 用芯片宏隔离差异 |
| `esp_lowputc.c` | 同路径 | 新 HAL 使用 `uart_periph_signal`、新的 RCC/clock 使能和 generic UART context | USB console 首版避免迁入 LP UART/RS485 等无关重构 |
| `esp_serial.c` | 同路径 | P4 TX empty raw bit 名称不同 | 迁入小型 P4 条件分支 |
| `esp_usbserial.c` | 同路径 | LL interrupt mask 名称变化；P4 需显式 bus clock、PHY defaults、source-to-IRQ；IRQ attach 流程变化 | 依据 P4 HAL做条件适配，不能照搬新版 IRQ API到所有芯片 |
| P4 common linker scripts | Apache `boards/risc-v/esp32p4/common/scripts` | rev <3 和 rev3 内存布局不兼容 | 当前实板只采用 rev3.1 路径；通用 SoC layout 归公共 `nuttx` |
| board `scripts/Make.defs` | 团队 `board/contest_board` | Apache 会选择 `sections.rev3.ld` | 团队仓仅保留板级 wrapper；通用 layout 不藏进团队目录 |
| `tools/espressif/Config.mk` | 同路径 | P4 simple boot/MCUBoot 的 app/bootloader offset 是 `0x2000` | 形成独立、P4 条件化的 image generation 修改 |
| Apache board bringup | 团队 `board/contest_board/src` | Apache bringup 同时注册大量外设 | 首版只做最小 boot/reset/USB console，不移植外设注册 |
| Apache usbconsole defconfig | 团队 `board/contest_board/configs/nsh` | Apache 新版 symbol 包含部分 openvela 不存在或语义不同的配置 | 以 openvela Kconfig 解析结果为准逐项添加 |

## 6. 后续 Apache 提交分类

### 6.1 必须吸收语义，不直接 cherry-pick

| Commit | 作用 | 采用方式 |
| --- | --- | --- |
| `cda4af9f002` | 初始 P4、Function EV Board、公共层条件和 image offset | 只选取最小启动所需文件和 hunk |
| `81602e16a20` | 恢复受宏保护的 P4 revision check | 纳入启动/revision 逻辑；不得绕过实板 revision 检查 |
| `4047b9f42b8` | RISC-V Espressif 启动时清除 BSS | 纳入 reset/start 最小闭环 |
| `2c8dc175102` | P4 L1/L2 cache line 配置 | 纳入 cache 基础配置，避免后续 cache/DMA 假设错误 |
| `612aca73ce0` 的 HAL pin 结果 | Apache 参考更新到 `8d0a8989...` | 作为 P4 专用可复现 pin 评估，不全局应用该提交 |

### 6.2 仅 CMake 路径需要，第一轮可推迟

openvela `build.sh` 默认走 Make；只有显式 `--cmake` 才走 CMake。因此第一轮先保证默认比赛构建入口，CMake 作为独立逻辑单元处理：

| Commit | 作用 |
| --- | --- |
| `ed217f8f3ff` | Espressif RISC-V CMake 基础支持 |
| `e59604bbe61` | CMake flashing target |
| `4f1a3356f90` | CMake 与 Make source set 对齐 |
| `19c1d86a16` | board CMake linker script 操作修复 |
| `5adbddf52ac` | P4/C3/C6/H2 HAL CMake linker script 修复 |
| `5053734b503` | CMake 生成 `irq.h` 的依赖顺序修复 |

### 6.3 当前最小里程碑不需要

- `b606180da92`：只为 rev <3 增加 `sram_high` heap；当前板卡配置为 rev3.1。
- `8c42fda257f` 及后续 RTC GPIO。
- `dedf9045c13`、`6480bb231dd`、`7e321e0aba3`：LP core/LPUART/LPI2C。
- `d6a55824b38`、`92a0b18ca11`：PSRAM。
- `5c4c60f9d26`、`c63c061a52c`：Ethernet。
- touch、analog comparator、LP mailbox、TWAI2、MIPI-DSI、LCD、touchscreen 等后续驱动。
- `73e243584eb`、`51f77ee111f`：Apache 板级初始化和 `NSH_ARCHINIT` API 清理；应适配 openvela 当前 API，而不是反向升级 openvela。
- `e6b08b2a1e`、`eb0834cd27`：只影响非 usbconsole 的外设配置。

## 7. 建议的最小移植和提交顺序

芯片层修改归 `nuttx` 项目；Function EV Board 修改归团队项目。两条轨道必须分开提交和 PR。

### N1：配置、工具链和 HAL 边界

内容：

- `ARCH_CHIP_ESP32P4` 和必要 Kconfig source。
- P4 `Kconfig`、`Make.defs`、`chip.h` 骨架。
- P4 专用 `esp-hal-3rdparty` pin 和许可证/来源记录。
- 首轮只支持 Make，不加入 Apache 全套 CMake。

验收：P4 defconfig 能通过配置解析，无 unknown/stale symbol；既有 C3/C6/H2 defconfig 不因 P4 选择而改变。

### N2：memory map、linker 和 image generation

内容：

- rev3.1 memory layout、aliases、flat memory 和 sections linker scripts。
- P4 simple boot 的 `0x2000` image offset。
- ROM linker fragments 的引用方式。

验收：能链接最小 ELF，并从本次产物生成可由 esptool 识别的 BIN；记录完整 chip、flash mode/frequency/size 和 offset。

### N3：reset/start、clock、BSS、cache/MMU 和 revision

内容：

- 复位入口和 `__esp_start`。
- `bootloader_clear_bss_section()` 对应语义。
- clock、SPI flash state、MMU/cache 初始化。
- rev3.1 检查和 cache line 配置。

验收：编译和链接成功，且能加入不依赖 syslog 的早期 progress markers。

### N4：CLIC、exception 和 timer

内容：

- CLIC `_mtvt_table`。
- IRQ source/CPU interrupt 映射和动态 vector table。
- exception cause mask。
- system timer interrupt source。

验收：timer tick 和中断路径可编译；硬件上无重复异常/复位后再进入下一步。

### N5：USB Serial/JTAG console

内容：

- USB Serial/JTAG clock、PHY 和 LL interrupt mask。
- console lowputc、serial registration 和 P4 IRQ attach 适配。
- 不启用 UART0、USB OTG 或无关串口功能。

验收：早期日志可见，随后稳定进入 console。

### B1：团队板级最小 USB NSH

内容放在：

```text
contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/
  board/contest_board/
```

只加入：

- team 345 board symbol 和正确板名。
- 最小 boot、reset、bringup。
- `configs/nsh/defconfig`。
- 对公共 NuttX P4 integration commit 的精确 SHA 记录。

不加入 LCD、Camera、Audio、Ethernet、PSRAM 或其他外设。

验收构建命令预期为：

```bash
cd ~/openvela-contest
./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j8
```

配置实际落地前需要先确认 `build.sh` 能识别该路径。第一轮优先默认 Make；CMake 验证单独执行：

```bash
./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh --cmake -j8
```

## 8. 风险和阻塞点

1. **HAL 版本隔离**：P4 所需 HAL 与 openvela 现有 pin 非线性兼容，必须证明按芯片覆盖不会污染其他 Espressif 构建。
2. **IRQ API 冲突**：Apache 新版 `esp_setup_irq` 签名和 handler ownership 与 openvela 不同。直接替换会使现有公共驱动编译失败或改变 ISR 行为。
3. **CLIC/SMP**：P4 是双核并使用 CLIC；不能假设 C3/C6/H2 的 PLIC/单核路径可以直接复用。
4. **启动和 cache/MMU**：HAL API 签名、cache level 和 internal-memory-via-L1 行为不同，错误通常表现为上电早期无输出或随机 exception。
5. **revision/linker 耦合**：rev <3 与 rev3.x 硬件差异大；当前只面向 rev3.1，不应同时支持两套 layout。
6. **镜像 offset**：P4 simple boot 使用 `0x2000`；烧录时必须读取本次构建生成的参数，不能沿用旧命令。
7. **板级模板占位风险（已解决）**：`board/contest_board` 已改为 team 345 P4X symbol、最小 boot/app/reset 和 USB NSH 配置。
8. **许可证与来源**：NuttX 文件保留 Apache-2.0/SPDX 和原作者；外部 HAL 使用精确 commit 拉取，不把下载目录、生成头文件或 SDK blob 提交进仓库。

## 9. 已落地的最小实现

### 9.1 SoC 和依赖边界

- `nuttx` 已创建具名分支 `feat/esp32p4-soc-contest2026`，基点仍为 `dd92bcf4257`。
- 增量加入 `arch/risc-v/include/esp32p4` 和 `arch/risc-v/src/esp32p4` 的最小入口文件，没有复制 Apache 整个目录。
- P4 单独固定 `esp-hal-3rdparty` 为 `8d0a898910084206721a0892ab093021bca1496a`；C3/C6/H2 继续使用 openvela 原有默认 pin。
- 对公共 Espressif 层只加入 P4 所需的 startup、CLIC IRQ、GPIO、lowputc、serial、timer、USB Serial/JTAG 和 vector 变体；旧芯片仍选择原实现。
- HAL OS adapter 通过单文件 force-include 兼容头适配 openvela 的 `nxtask_init`、`nxsched_usleep`、IRQ 和 `fcntl` API，没有修改下载的 HAL checkout。
- P4 选择 openvela 的 IRQ 保护型 64-bit atomic fallback，以适配 GCC 13.4.0 不提供 RV32 `libatomic.a` 的情况。
- HAL 清单补入 rev3 启动实际依赖的 `pmu_pvt.c`。
- P4 不编译未使用且 ABI 不匹配的通用 `riscv_mtimer.c`，scheduler tick 继续由 Espressif system timer 提供。

### 9.2 团队板级

团队板已从占位模板改为 ESP32-P4X Function EV Board 最小 USB NSH 配置，落在 manifest 映射后的：

```text
vendor/openvela/boards/contest2026_345_board/
```

板级只包含 boot、app initialize、reset、USB console 配置和 rev3 linker scripts。链接脚本额外保留 `esp_start_p4` 的 SRAM 启动段，并采用 Apache 后续修正后的 rev3 MSPI workaround/LP RAM 非重叠布局。

## 10. 构建验证

配置和构建命令：

```bash
cd /home/uleemos/openvela-contest
./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh olddefconfig
PATH=/tmp/openvela-esptool-venv/bin:$PATH \
  ./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j16
```

结果：

| 项目 | 值 |
| --- | --- |
| 配置 | `olddefconfig` 和 `savedefconfig` 成功 |
| 编译器 | openvela prebuilt `riscv-none-elf-gcc` 13.4.0 |
| HAL | `8d0a898910084206721a0892ab093021bca1496a` |
| 镜像工具 | 隔离安装在 `/tmp/openvela-esptool-venv` 的 esptool 4.12.0；项目最低要求 4.8.0 |
| Flash 参数 | ESP32-P4、4 MiB、DIO、80 MHz、simple boot |
| Flash offset | `0x2000`，来自本次 `tools/espressif/Config.mk` 和 `.config` |
| `nuttx` | 381096 bytes；SHA-256 `c51a445778e62409a871e04dde722490ee5dcd9a51471711408833d205fa4ad9` |
| `nuttx.hex` | 376307 bytes；SHA-256 `41f0f83258926651729cb0fb6b1b348f846281bddfa2fa71f249179c77149d73` |
| `nuttx.bin` | 221316 bytes；SHA-256 `b785f31fafb90ceadca331d7469a999b108b5dfde7e8d62c4d0dcb4b4c61fe36` |

`esptool.py image_info nuttx.bin` 识别为 ESP32-P4 image v1，入口 `0x4ff4447a`，3 个 segment，checksum `0xe9` 有效。最终链接内存占用为：SRAM 26836 bytes、IROM 98434 bytes、DROM 146084 bytes；rev3 MSPI workaround 保留 256 bytes，LP RAM 可用区在修正后为 32488 bytes。

完整重编译出现一条既有公共层告警：`esp_libc_stubs.c` 中 `__assert_func` 被声明为 `noreturn` 但编译器认为可能返回。该告警不影响链接或镜像生成，后续应单独核对 panic/assert 路径，不在最小 bring-up 中扩大修改范围。

## 11. 硬件测试状态

### 11.1 板卡识别和烧录

WSL2 内没有映射 `/dev/ttyACM*`，随后通过 Windows 侧只读枚举确认：

| 项目 | 值 |
| --- | --- |
| Windows port | `COM3` |
| PNP ID | `USB\VID_303A&PID_1001&MI_00` |
| 芯片 | ESP32-P4 revision v3.2 |
| 特性 | Dual Core + LP Core、400 MHz、40 MHz crystal |
| USB mode | USB-Serial/JTAG |
| MAC | `e8:f6:0a:e3:a6:5e` |
| Windows esptool | 5.3.1 |

由于 Windows esptool 不能直接读取 WSL UNC 路径，`nuttx.bin` 临时复制为 `C:\Users\uleem\AppData\Local\Temp\openvela-esp32p4-nuttx.bin`。Windows 侧 SHA-256 为 `B785F31FAFB90CEADCA331D7469A999B108B5DFDE7E8D62C4D0DCB4B4C61FE36`，与 WSL 构建产物一致后才执行烧录：

```text
esptool.exe -c esp32p4 -p COM3 -b 921600 write-flash \
  --flash-size 4MB --flash-mode dio --flash-freq 80m \
  0x2000 C:\Users\uleem\AppData\Local\Temp\openvela-esp32p4-nuttx.bin
```

烧录结果：擦除范围 `0x00002000–0x00038fff`，写入 221316 bytes，写后 hash 校验通过，并通过 RTS 硬复位。

### 11.2 NSH smoke test

首次运行发现 `free` 和 `ps` 因 procfs 未挂载而失败。配置本身已有 `CONFIG_FS_PROCFS=y` 和 `CONFIG_NSH_PROC_MOUNTPOINT="/proc"`，因此只在团队板 `board_app_initialize()` 增加 `nx_mount()`，重建、重刷后最终结果如下：

| 命令/检查 | 结果 |
| --- | --- |
| 冷启动 | 出现 `NuttX-13.0.0` 和 `nsh>`；USB 重新枚举会截断最前面的 0–2 个字符 |
| `help` | 正常，列出 NSH 命令和 `dumpstack/hello/nsh/sh` builtin apps |
| `uname -a` | `NuttX 13.0.0 dd92bcf4257-dirty ... risc-v esp32p4x-function-ev-board` |
| `free` | total 494296、used 6912、free 487384 bytes |
| `ps` | CPU0 IDLE 和 `nsh_main` 两个任务正常 |
| `hello` | `Hello, World!!` |
| `ls /dev` | `console null random ttyACM0 zero` |

完整串口日志：[esp32p4-nsh-smoke-2026-08-09.log](hardware-logs/esp32p4-nsh-smoke-2026-08-09.log)，SHA-256 `ea842146e1497343019c0b355369da9bc642d17bfa8c8cdb316f525eab3c33f8`。

### 11.3 连续复位稳定性

通过 Windows COM3 连续执行 10 次 RTS 硬复位。每一轮都重新运行 `uname -a`，验收条件为返回 `NuttX 13.0.0` 且命令后再次出现 `nsh>`。最终结果为 **10/10 PASS**，未观察到 exception、异常复位、死机或命令通道失联。

循环日志：[esp32p4-reset-stability-10x-2026-08-09.log](hardware-logs/esp32p4-reset-stability-10x-2026-08-09.log)，SHA-256 `4280174843fe97e146053892a97e9ba1392bd09c5a08f62ffa8e693b82e37b65`。

### 11.4 GPIO 阶段（构建和硬件验证通过）

GPIO 阶段没有照搬参考实现中的 GPIO1。V1.8 板原理图表明 GPIO0/1
默认通过 R61/R59 连接 32.768 kHz 晶振，通往 J1 的 R199/R197 标为 NC；
GPIO7/8 也分别通过 0 Ω 电阻连接板载共享 I2C SCL/SDA，不能用跳线短接。
因此改用原理图确认通过 R33/R39 只接 J1 排针的 GPIO20/21：

| 设备 | 板级引脚 | J1 | 用途 |
| --- | ---: | ---: | --- |
| `/dev/gpio0` | GPIO20 | pin 13 | 推挽输出 |
| `/dev/gpio1` | GPIO21 | pin 11 | 下拉输入，与 GPIO20 跳线回环 |
| `/dev/gpio2` | GPIO35 | BOOT | 上拉、下降沿按键中断 |

配置已启用 `CONFIG_DEV_GPIO`、`CONFIG_ESPRESSIF_GPIO_IRQ` 和
`CONFIG_EXAMPLES_GPIO`。2026-08-11 在 NuttX commit `b9f8442fa73` 和
团队板 commit `f3fccc570fe` 上完成干净重链接。WSL 当前 Python 环境缺少
`esptool` 包，因此 Make 在最终 MKIMAGE 检查处停止；随后使用已安装的
Windows esptool 5.3.1，按同一构建规则执行 `elf2image --ram-only-header
-fs 4MB -fm dio -ff 80m` 生成最终镜像：

| 产物 | 大小 | SHA-256 |
| --- | ---: | --- |
| `nuttx` | 393876 bytes | `b2b373b8942290d19d90337e2d6e96714c5d2adfc6223158a39eb65fd136e012` |
| `nuttx.hex` | 403610 bytes | `08f20f717aedc64b049162a1ded715acaf7a8fbbbf588b53731e636e7caaafff` |
| `nuttx.bin` | 228172 bytes | `f4c87dcf2c1b34ab22b932c50d7856ac57d380b14b23a5e1adab8d310f61fd91` |

`esptool image-info` 将 `nuttx.bin` 识别为 ESP32-P4、4 MiB、DIO、
80 MHz 镜像，入口为 `0x4ff44638`，checksum `0x3e` 有效。

硬件回环验收步骤：

```text
# 断电后用杜邦线连接 J1 pin 13 (GPIO20) 与 J1 pin 11 (GPIO21)
gpio -o 0 /dev/gpio0
gpio /dev/gpio1              # 期望 Value=0
gpio -o 1 /dev/gpio0
gpio /dev/gpio1              # 期望 Value=1
gpio /dev/gpio2              # 按下并松开 BOOT，期望 poll 返回
```

镜像通过 Windows COM3 写入地址 `0x2000`，esptool 写后 hash 校验成功。
串口 `uname -a` 返回 `b9f8442fa73`，并确认 `/dev/gpio0`、`gpio1`、
`gpio2` 均已注册。GPIO20 输出 0/1 时 GPIO21 分别读取 0/1；恢复 GPIO20
为低电平后，GPIO35 的阻塞 poll 被一次物理 BOOT 按键下降沿唤醒并返回。

完整日志：[esp32p4-gpio-smoke-2026-08-11.log](hardware-logs/esp32p4-gpio-smoke-2026-08-11.log)，
SHA-256 `c575f5e5c3b3dc8a557cd613fabd93ab6b38c3b9cd53b47e88d15cc627b67d41`。
该文件在 2026-08-12 仅纠正了板型元数据，原始烧录与串口输出未改。

### 11.5 I2C/ES8311 阶段（构建和硬件验证通过）

主板 V1.8 原理图确认 GPIO7 经 R194（0 Ω）连接 `ESP_I2C_SDA`，GPIO8
经 R190（0 Ω）连接 `ESP_I2C_SCL`；同一总线经 R62/R52（0 Ω）连接
板载 ES8311，也路由至 CSI/DSI 连接器。V1.8 主板 sheet 3 的 R109/R98
分别给 `ESP_I2C_SCL`/`ESP_I2C_SDA` 提供 2.2 kΩ 到 `ESP_3V3` 的外部
上拉；当前驱动仍按 I2C 要求将两脚配置为开漏输出。早期记录曾漏看这两个
主板上拉，现已纠正。

首次硬件诊断出现 NACK 中断位 `0x400`。对构建配置复核后发现活动
`.config` 仍使用 Kconfig 默认 GPIO6/GPIO5，而不是板级 defconfig 中的
GPIO8/GPIO7。强制清除旧配置并重新生成后，活动配置固定为：

```text
CONFIG_ESPRESSIF_I2C0=y
CONFIG_ESPRESSIF_I2C0_SCLPIN=8
CONFIG_ESPRESSIF_I2C0_SDAPIN=7
# CONFIG_I2C_POLLED is not set
```

正常中断模式固件完成编译、链接和镜像生成，构建产物为：

| 产物 | 大小 | SHA-256 |
| --- | ---: | --- |
| `nuttx` | 405204 bytes | `369f4ee37723542d5872f1822019215aa96ffb0addfd554617bd668e8bdd130a` |
| `nuttx.hex` | 433997 bytes | `3ead75859d5253db7a8ab11eb30a58927627a30455b6b9d05ce1588a55729edd` |
| `nuttx.bin` | 236280 bytes | `123ffbfd0ea2f1ae121c1a90ef61daeb965ecb2b05faa6ea75d93528230ae2f0` |

`nuttx.bin` 通过 Windows esptool 5.3.1 写入 ESP32-P4 revision v3.2 的
`0x2000`，写后 hash 校验通过。未连接 Camera/LCD 子板，也未加装板外
I2C 上拉电阻；主板自身 R109/R98 上拉已在后续原理图复核中确认。最终硬件结果：

| 检查 | 结果 |
| --- | --- |
| `/dev/i2c0`、Bus 0 | 注册成功 |
| ES8311 地址 | `0x18` ACK |
| ES8311 ID | `FD=0x83`、`FE=0x11`、`FF=0x01` |
| 10 kHz repeated-start read | PASS |
| 100 kHz repeated-start read | PASS |
| 100 kHz separate-transfer read | PASS |

因此当前板载 ES8311 bring-up 不需要 Camera 子板或额外上拉；Camera/SC2336
留作后续 CSI 阶段的独立目标，不用来替代本次 I2C 验收。

完整日志：[esp32p4-i2c-es8311-smoke-2026-08-12.log](hardware-logs/esp32p4-i2c-es8311-smoke-2026-08-12.log)，
SHA-256 `3ee5114e6c1933c611c77748984d0d43ad17370158f0232406a0503044a8c68f`。
该文件在提交前纠正了板型元数据，原始烧录与串口输出未改。

### 11.6 C6 V1.8 原理图纠偏审计

2026-08-12 在提交 I2C 阶段改动前，以 V1.8 原理图 6 个 sheet 重新审计
已经实现的板级配置。GPIO7/8、GPIO20/21、GPIO35、J1 针脚和 ES8311
总线连接与实测配置一致，因此无需修改 GPIO/I2C 代码或 defconfig。
技能中原先引用的 C5 V2.0 原理图已经替换；其中 GPIO20/21 串联电阻位号
也由错误的 R40/R33 修正为 V1.8 的 R33/R39，BOOT 网络名由旧记录的
`ESP_BOOT` 修正为 `GPIO35_BOOTMODE`。GPIO35 还经 R135 连接
`RMII_TXD1`；当前 GPIO 按键测试在 Ethernet 未启用时有效，后续 Ethernet
阶段必须取消或隔离该 GPIO 中断设备。

C6 后续开发必须使用 V1.8 sheet 5：U1 为 `ESP32-C6-MINI-1`，P4
GPIO14–19 连接 `SD2_D0–D3/CLK/CMD`，GPIO54 为 `C6_EN`，GPIO6 为
`C6_WAKEUP`。这些资源在 SPI、SD、无线协处理器阶段不能作为空闲 GPIO
重复分配。

纠偏后使用同一 I2C 源码和 defconfig 重新构建成功；`nuttx`、`nuttx.hex`
和 `nuttx.bin` 分别为 405204、433997 和 236280 bytes，SHA-256 分别为
`c877a0afbffce8fd827faa964acd713cfcf5232199a8b4482b7e359a639818d3`、
`864aca0016b82f0f3eb7143f69fcc4ac55bfe535fb219226cc1c773cc2a6cee2`、
`629cbf790eb3b39e4078d0d9086995bd577e87087852c2be9c03087b35cdf6aa`。
本次审计未改变可执行代码或配置；新镜像因构建时间变化未重复烧录，硬件
验收仍对应 11.5 中已经写入同一块 V1.8/C6 实板的镜像及原始串口记录。

### 11.7 Gate G1 基础稳定性补测

2026-08-15 在 ESP32-P4 revision v3.2、NuttX commit `d49dc5e5a9c` 上新增
内置命令 `g1_smoke`，同时给出 system timer、定时器中断后的 POSIX
signal 递送、monotonic clock、PSRAM 用户堆和 Flash 重启保持证据。

`g1_smoke 1800` 使用 10 Hz POSIX timer，完成 18000 次 signal wait 和
18000 轮变长堆块的分配、全量写入、全量校验与释放。30 分钟结果：

| 检查 | 结果 |
| --- | --- |
| Timer/Interrupt | 18000/18000，elapsed 1800030 ms |
| PSRAM user heap | arena 33554432 bytes |
| Heap used | before 8952、after 8952、drift 0 |
| Largest free block | 33545480 bytes，所有分钟采样不变 |
| Crash/reset | 未观察到 |

最终复现构建命令为：

```bash
PATH=/tmp/openvela-esptool-venv/bin:$PATH \
  ./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j16
```

使用 `riscv-none-elf-gcc 13.4.0` 和隔离安装的 esptool 4.12.0，命令退出 0。
最终产物为：`nuttx` 592232 bytes、SHA-256
`85b463cc8532613965bfd7f479c8df2bb403d5ca64f16aa77aeb3fd36d2ff8db`；
`nuttx.hex` 623521 bytes、SHA-256
`36e63edc48246f075f2428a991a62b26988c822802081d84bb1dbce59c83cf74`；
`nuttx.bin` 279240 bytes、SHA-256
`681512daff4fb25cb898153b9530705e6eb288945bce820019980eb6910d7725`。
`image_info` 将 BIN 识别为 ESP32-P4、16 MB、DIO、80 MHz，入口
`0x4ff4811c`，checksum `0x7b` 有效。该最终 BIN 经 COM3 写入 `0x2000`，
写后 hash 校验通过；随后 `g1_smoke 2` 和 `sc2336_probe` 均再次 PASS。

`/data/g1-persist.bin` 在首次运行写入 sequence 1，执行 NSH `reboot` 后读回
校验通过并写入 sequence 2；30 分钟测试启动时继续读回 sequence 2 并写入
sequence 3。10 次 NSH 软件热重启均重新挂载 `/data`、进入 `nsh>` 并成功
执行 `uname -a`，结果为 10/10 PASS。

原始日志：

- [30 分钟稳定性](hardware-logs/esp32p4-g1-stability-30min-2026-08-15.log)，
  SHA-256 `c16b69136616e37c1bd6eccec63f7a23afdf4ed678ec19bef9b7e27d7f64cd19`。
- [10 次热重启](hardware-logs/esp32p4-g1-warm-reboot-10x-2026-08-15.log)，
  SHA-256 `d1c35c76c4ee292ebad8b7f52182deeb4e869458754dd173165cd1ba1ec06a64`。
- [Flash 重启保持](hardware-logs/esp32p4-g1-flash-persistence-2026-08-15.log)，
  SHA-256 `e828348946953a70d4eaef6f8fcbcb2c0e701bd9ea865592cd34676684530429`。
- [最终镜像联合 smoke](hardware-logs/esp32p4-g1-camera-final-smoke-2026-08-15.log)，
  SHA-256 `21c600db5a5fbfa6654ef5c7d6c8471a5143baa37e730eeeaf88807da1a8abe4`。

随后使用 `tools/hardware/reset_cycles.py --mode manual-power`，通过人工切断并
恢复开发板供电完成 20 次有效冷启动。首批 20 个测试槽位为 19/20 PASS；其中
第 3 个槽位因 60 秒内未检测到 COM3 消失而判为“未执行到断电”，不是固件启动
失败。追加 1 次真实断电补测为 1/1 PASS，因此有效冷启动累计 20/20。每次有效
测试均观察到 Flash MTD `/data` 挂载、`NuttShell (NSH) NuttX-13.0.0`、`nsh>`，
并成功执行 `uname -a`，固件 commit 均为 `d49dc5e5a9c`。

断电测试后再次执行 `uname -a`、`free`、`ps`、`ls /dev`、`g1_smoke 2` 和
`sc2336_probe`，测试器退出 0。PSRAM 用户堆为 33554432 bytes，短测完成
20/20 timer signals、20 轮 heap 校验且 drift 为 0；Flash persistence sequence
从 5 递增到 6；SC2336 ID 仍为 `0xcb3a`。

补充原始日志：

- [首批 20 个断电测试槽位](hardware-logs/esp32p4-g1-cold-boot-20x-2026-08-15.log)，
  SHA-256 `c9ebc7a176f057cf403387d8d2d952e3f7254fdb0fb808fd14ec23f0b8e07df2`。
- [1 次断电补测](hardware-logs/esp32p4-g1-cold-boot-supplement-1x-2026-08-15.log)，
  SHA-256 `a2e9cbe4ad1b69f36804955c3b7a3c58c86716263257f4e3f0c25f9021f7de57`。
- [断电后的联合 smoke](hardware-logs/esp32p4-g1-post-cold-smoke-2026-08-15.log)，
  SHA-256 `889a51ecedb017ec36ed72bdf7f4cf47c525d590855f3ba2c9f329f5740e7ced`。

### 11.8 AG638A32M2 / SC2336 最小风险识别

硬件依据为主板 `SCH_ESP32-P4X_FUNCTION_EV_BOARD` V1.8 sheet 3 和 Camera
子板 sheet 1。主板 GPIO8/GPIO7 分别承载共享 `ESP_I2C_SCL/SDA`；Camera
子板 Q3 完成 3.3 V 到 1.8 V 双向电平转换，R20/R23 提供传感器侧 2.2 kΩ
上拉，U1/U2 产生 1.8 V/2.8 V，Y1 提供 24 MHz `XVCLK`。模块资料
`camera_datasheet.pdf` 标识内部传感器为 SC2336。

新增 `sc2336_probe` 只通过 `/dev/i2c0` 在 100 kHz 下执行两次 16-bit
register-address、8-bit-value 的 repeated-start 读取；不写 sensor register，
不启动 stream、MIPI CSI、DMA 或 frame buffer。真机结果：7-bit 地址 `0x30`
ACK，`0x3107=0xcb`、`0x3108=0x3a`，组合 ID `0xcb3a`，PASS。

原始日志：[SC2336 ID probe](hardware-logs/esp32p4-sc2336-id-probe-2026-08-15.log)，
SHA-256 `9ef17f2c8a1e7ff3f97763288001966001123fc986a2e0e771005ef2bc7f85e4`。

## 12. 下一最小步骤

1. G1 测试入口、SC2336 只读探测、硬件日志和文档已通过专属仓 PR #4
   Rebase and merge，赛事分支 commit 为
   `2f32a81cc8845d8801936fcbddbe9202711502cd`。
2. 后续 AI 开始前先完整阅读 `ESP32P4_AI_HANDOFF.md`，按其中的双仓边界、
   硬件文档门禁、构建/烧录/验证和 PR 工作流执行。
3. 固定官方 SC2336 初始化表的精确来源 commit 和许可证，再分析 openvela
   MIPI CSI controller、DMA/cache、video device 接口差距。
4. 下一次 Camera 真机增量先写入最小 sensor 初始化表并确认 stream control，
   再单独开启 CSI/DMA；不要一次合入完整视频链路。
5. 补充既有 `esp_libc_stubs.c::__assert_func` noreturn 告警的独立分析，
   不阻塞 Camera 依赖差异整理。

## 13. CAM-002：SC2336 官方来源与 Camera 依赖台账审计

### 13.1 本轮目标与环境状态

- 本轮目标：完成 CAM-002 依赖审计与台账落盘，不写寄存器、不写驱动代码、不启动 DMA。
- 基线状态：
  - 公共 NuttX：`feat/esp32p4-soc-contest2026`（HEAD: `d49dc5e5a9c`，对应公共 PR #340）。
  - 团队专属仓：基线 `2f32a81`（PR #1~#4 已合并）。
- 硬件文档预检：已通过 `hardware-docs-preflight.sh` 验证（V1.8 主板原理图、P4 Datasheet、TRM、Errata 四份权威 PDF 均正常解析）。

### 13.2 权威硬件证据台账

| 项目 | 证据来源 | 关键技术参数与连接事实 |
| --- | --- | --- |
| **主板 CSI 接口** | `SCH_ESP32-P4X_FUNCTION_EV_BOARD_V1.8_20260805.pdf` Sheet 3 | J5 (15-pin FPC `CSI_1-1734248-5`)：Pin 2/3 `CSI_A_DATA0N/P`、Pin 5/6 `CSI_A_DATA1N/P`、Pin 8/9 `CSI_A_CLKN/P`、Pin 11 `CAM_IO0`（经 R106 0 Ω 到 `CSI_IO0`，R123 10 kΩ 上拉到 3.3 V）、Pin 12 `CAM_IO1`（经 R101 0 Ω 到 `CSI_IO1`）、Pin 13 `ESP_I2C_SCL`（GPIO8，R109 2.2 kΩ 上拉）、Pin 14 `ESP_I2C_SDA`（GPIO7，R98 2.2 kΩ 上拉）、Pin 15 `ESP_3V3`。 |
| **Camera 子板** | `esp32-p4-function-ev-board-camera-subboard-schematics.pdf` Sheet 1 | J1 (15-pin FPC 到主板) ↔ J2 (24-pin FPC 到 Sensor)；U1 (`ME6211C18M5G-N`) 产生 1.8 V `DOVDD_1V8`；U2 (`ME6211C28M5G-N`) 产生 2.8 V `AVDD_2V8`；Y1 为 24 MHz 有源晶振（由 `DOVDD_1V8` 供电），向 Sensor Pin 14 `MCLK` 提供 24 MHz `XVCLK`；Q3 (`DMN63DLDW-7`) 双 NMOS 完成 3.3 V ↔ 1.8 V I2C 双向电平转换，R20/R23 为传感器侧 2.2 kΩ 上拉；复位网络含 SW1、R10、C3。 |
| **Sensor 模组** | `camera_datasheet.pdf` (`AS-AG638A32M2-50`) | 内部 Sensor 为 SmartSens SC2336；1/3" 2MP CMOS；最大 1920×1080@30 fps；RAW10/RAW8 格式；2-lane MIPI CSI-2 D-PHY；输入时钟 24 MHz；7-bit SCCB/I2C 地址 `0x30`；Sensor ID 寄存器 `0x3107=0xcb`、`0x3108=0x3a`（真机已实测验证）。 |
| **SoC 与 Errata** | `esp32-p4_datasheet_cn.pdf` / `esp-chip-errata-zh_CN-master-esp32p4.pdf` | SoC 为 ESP32-P4NRW32X revision v3.2；封装内 32 MB PSRAM；早期 v3.0 勘误 MSPI-750/751/DMA-767 已在 v3.1 硬件修复，当前 v3.2 不受影响；但 DMA 描述符 4 字节对齐、帧缓冲区 64 字节 Cache 行对齐及 CPU/DMA 间 Cache Invalidate/Clean 同步仍为硬性约束。 |

### 13.3 SC2336 官方初始化表来源与许可证台账

1. **官方上游代码仓**：`https://github.com/espressif/esp-video-components`
2. **组件路径**：`esp_cam_sensor/sensors/sc2336/`
3. **许可证**：`Apache-2.0`（`SPDX-License-Identifier: Apache-2.0`，`SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD`）。
4. **审计确认 Commit SHA**：`2e924b614d7095898ac88c7ba34d43a6c98261ea`（2026-06-17）及 underlying `3620887638419f7afbe4aa3a909422c640b14061`（寄存器表头文件分离）。
5. **源文件列表与审计结论**：
   - `esp_cam_sensor/sensors/sc2336/sc2336.c`：包含传感器增益映射表、曝光控制算法及 V4L2 参数换算。
   - `esp_cam_sensor/sensors/sc2336/include/sc2336.h`：公共头文件与 Sensor ID 定义。
   - `esp_cam_sensor/sensors/sc2336/private_include/sc2336_regs.h`：寄存器宏定义。
   - `esp_cam_sensor/sensors/sc2336/private_include/sc2336_settings.h`：模式表索引入口。
   - `sc2336_mipi_2lane_24Minput_1920x1080_raw10_30fps.h`：1080p 30 fps RAW10（2-lane MIPI, 24 MHz XVCLK）。
   - `sc2336_mipi_2lane_24Minput_1920x1080_raw10_25fps.h`：1080p 25 fps RAW10（2-lane MIPI, 24 MHz XVCLK）。
   - `sc2336_mipi_2lane_24Minput_1280x720_raw10_30fps.h`：720p 30 fps RAW10（2-lane MIPI, 24 MHz XVCLK）。
   - `sc2336_mipi_2lane_24Minput_640x480_raw10_50fps.h`：VGA 50 fps RAW10（2-lane MIPI, 24 MHz XVCLK）。
6. **首发带起模式推荐**：
   - 第一阶段（CAM-003/004/005）：推荐选用 `24Minput_1280x720_raw10_30fps` 或 `24Minput_1920x1080_raw10_25fps` 作为基准测试模式，匹配板载 24 MHz Y1 晶振与 2-lane 物理布线。

### 13.4 底层 HAL 与 NuttX 驱动适配差异分析

1. **底层 HAL 现状（`esp-hal-3rdparty` commit `8d0a898910084...`）**：
   - `components/esp_hal_cam/mipi_csi_hal.c`（78 行）：包含 PHY PLL 频率选择、PHY 与 Host 复位、Active Lane 配置、Bridge 宽高与 FIFO 阈值配置。**代码轻量且无 OS 依赖，可直接作为 NuttX CSI lower-half 的底层调用**。
   - `components/esp_hal_cam/esp32p4/mipi_csi_periph.c`：包含外设寄存器基地址与中断号定义。
   - `components/upper_hal_cam/csi/src/esp_cam_ctlr_csi.c`（Upper HAL，641 行）：大量依赖 FreeRTOS 队列、IDF 堆分配、电源管理锁等。**严禁整文件复制**，必须在 NuttX 中实现原生 `struct imgdata_s` 接口。
2. **DMA 与内存/Cache 一致性**：
   - ESP32-P4 的 MIPI CSI Bridge 数据由 DW-GDMA 搬运至内存。
   - DMA Descriptor 需 4 字节对齐，PSRAM 帧缓冲区需 64 字节 Cache-line 对齐。
   - 在 CPU 读取 PSRAM 帧数据前，必须调用 Cache Invalidate（如 `up_invalidate_dcache`），严禁通过关闭 Cache 绕过一致性问题。
3. **NuttX Video 框架对齐**：
   - **Upper-half**：NuttX 已有 `drivers/video/v4l2_cap.c`，通过 `capture_register()` 注册 `/dev/video0`，提供标准 V4L2 ioctl 支持。
   - **Lower-half (`struct imgdata_s`)**：由 ESP32-P4 MIPI CSI 控制器驱动实现 `init`、`uninit`、`set_buf`、`start_capture`、`stop_capture`。
   - **Sensor Subdevice (`struct imgsensor_s`)**：由 SC2336 驱动实现 `init`、`uninit`、`set_fmt`、`stream_on`、`stream_off`。

### 13.5 仓库边界与后续 Commit 拆分计划

- **公共 NuttX 仓 (`nuttx/`)**：
  - `arch/risc-v/src/esp32p4/esp32p4_mipi_csi.c`：ESP32-P4 MIPI CSI 控制器 lower-half 实现。
  - `arch/risc-v/src/esp32p4/hal_esp32p4.mk` 与 `Kconfig`：引入 CSI HAL 源码与配置项。
  - `arch/risc-v/src/esp32p4/esp32p4_dma.c` / DW-GDMA 支持。
- **团队专属仓 (`contest2026_345_.../`)**：
  - `board/contest_board/src/esp32p4_camera.c`：板级 Camera 上电时序、复位控制、I2C 绑定与 `capture_register()`。
  - `board/contest_board/configs/nsh/defconfig`：开启 Camera/Video 相关 Kconfig。
  - `app/sc2336_stream/` 与 `app/camera_smoke/`：Sensor 控制测试与视频帧 Smoke 验证 App。
  - `PORTING_NOTES.md` 与 `hardware-logs/`：阶段记录与真机日志。

- **后续增量提交拆分路线**：
  1. `团队仓 Commit A (CAM-002)`: `docs: record SC2336 source and camera dependency ledger`
  2. `团队仓 Commit B (CAM-003, 当前)`: `camera: add SC2336 minimal initialization and stream control`
  3. `NuttX 仓 Commit C (CAM-004)`: `risc-v: add ESP32-P4 MIPI CSI low-level controller support`
  4. `NuttX 仓 Commit D (CAM-005)`: `video: add ESP32-P4 CSI DMA and PSRAM frame capture support`
  5. `团队仓 Commit E (CAM-006)`: `boards: register ESP32-P4X camera and add frame smoke app`
  6. `团队仓 Commit F (CAM-007/008)`: `test: add camera stability evidence and documentation for Gate G2`

## 14. CAM-003：SC2336 最小初始化与 Stream Control 控制面实现

### 14.1 本轮目标与范围边界

- **唯一可观察目标**：在团队专属仓实现 SC2336 传感器控制面最小驱动与测试入口：
  1. 保持并增强只读 ID 探测（`0x3107/0x3108 = 0xcb3a`）；
  2. 实现传感器软件复位（`0x0103 = 0x01`）与复位后恢复验证；
  3. 导入官方已审计的 2-lane 24 MHz XVCLK 模式初始化表（720p 30 fps / 1080p 25 fps RAW10），完成寄存器写入与关键寄存器读回校验；
  4. 实现流控制：`stream-on`（`0x0100 = 0x01`）与 `stream-off`（`0x0100 = 0x00` 待机模式）；
  5. 支持多轮流状态切换压力测试（`cycle <N>`）。
- **边界纪律**：本阶段只操作 I2C/SCCB 控制面，不启用 MIPI CSI 接收、不分配 DMA 描述符与帧缓冲区，不宣称“已获得图像帧”。

### 14.2 修改与新增文件

- 新增：[`app/sc2336_probe/sc2336_tables.h`](file:///home/uleemos/openvela-contest/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/sc2336_probe/sc2336_tables.h)（包含 720p 30 fps 及 1080p 25 fps 2-lane 24 MHz 初始化寄存器表与宏定义）。
- 修改：[`app/sc2336_probe/sc2336_probe_main.c`](file:///home/uleemos/openvela-contest/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/sc2336_probe/sc2336_probe_main.c)（重构为支持 `probe`、`reset`、`init`、`stream-on`、`stream-off`、`test` 与 `cycle` 子命令的传感器控制面测试工具）。
- 修改：[`app/sc2336_probe/Makefile`](file:///home/uleemos/openvela-contest/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/sc2336_probe/Makefile)（栈大小调整为 4096 字节）。
- 修改：[`app/sc2336_probe/README.md`](file:///home/uleemos/openvela-contest/contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/sc2336_probe/README.md)（更新命令用法与子命令说明）。

### 14.3 构建与固件产物

- **构建命令**：
  ```bash
  cd /home/uleemos/openvela-contest
  PATH=/tmp/openvela-esptool-venv/bin:$PATH \
    ./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j16
  ```
- **编译状态**：退出码 0，无警告。
- **固件产物**：
  - `nuttx`：596888 bytes，SHA-256 `181b4894ee0f978b391ab70c6e267d3f912db6f228bf041811f096f3bc244c71`
  - `nuttx.hex`：640793 bytes，SHA-256 `193f76d175ff9a96fa093382752f325cc1086b66612c00184abd6b6573aff7b2`
  - `nuttx.bin`：281476 bytes，SHA-256 `a402dfccc58491c0236b9d6eeceef403d4129bf04488fba63ec94e2a8c575ce3`
  - `image_info`：ESP32-P4、16 MB、DIO、80 MHz、Entry `0x4ff4811c`，校验和 `0x67` 有效。

### 14.4 真机串口验证与测试结果

- **烧录命令**：
  ```powershell
  python.exe -m esptool -c esp32p4 -p COM3 -b 921600 write-flash `
    --flash-size 16MB --flash-mode dio --flash-freq 80m `
    0x2000 openvela-esp32p4-nuttx.bin
  ```
  Hash of data verified，成功写入 `0x2000` 并完成硬复位。

- **验证项目与结果**：
  1. **系统基础环境**：`uname -a` 返回 `NuttX 13.0.0 d49dc5e5a9c`；`free` 显示 Umem 33554432 bytes (PSRAM)；`ls /dev` 确认 `/dev/i2c0` 正常注册挂载。
  2. **只读 ID 探测 (`sc2336_probe probe`)**：SCCB 地址 `0x30`，读回 `0x3107=0xcb`、`0x3108=0x3a`，组合 ID `0xcb3a`，**PASS**。
  3. **软件复位恢复 (`sc2336_probe reset`)**：写入 `0x0103 = 0x01` 触发软复位，延时 20 ms 后重新探测，ID `0xcb3a` 正常恢复，**PASS**。
  4. **720p 30 fps 全流程测试 (`sc2336_probe test 720p`)**：
     - Step 1: Probe ID (0xcb3a) MATCH
     - Step 2: Soft Reset PASS
     - Step 3: Write Mode Table 165 registers PASS
     - Step 4: Verify Key Registers (CLK_CTRL=0x05, ANA_INIT_1=0x80, ANA_INIT_2=0x80, VTS=1500) PASS
     - Step 5: Stream ON (`0x0100=0x01`, I2C ACK OK) PASS，保持 200 ms
     - Step 6: Stream OFF (`0x0100=0x00`, I2C ACK OK) PASS
     - 结论：**ALL PASS**。
  5. **720p 30 fps 流切换循环压力测试 (`sc2336_probe cycle 10 720p`)**：连续 10 轮 Stream-ON / Stream-OFF 切换，全部获得 I2C ACK 响应，**10/10 PASS**。
  6. **1080p 30 fps 全流程与循环测试 (`sc2336_probe test 1080p` & `cycle 10 1080p`)**：下发 149 个模式寄存器，关键寄存器校验与 10 轮流切换压力测试全部 **PASS**。
  7. **1080p 25 fps 全流程与循环测试 (`sc2336_probe test 1080p25` & `cycle 10 1080p25`)**：下发 130 个模式寄存器，关键寄存器校验与 10 轮流切换压力测试全部 **PASS**。

### 14.5 证据文件归档

- 原始串口日志：[`hardware-logs/esp32p4-sc2336-control-smoke-2026-08-19.log`](hardware-logs/esp32p4-sc2336-control-smoke-2026-08-19.log)
- 日志 SHA-256：`ef5306745452f58755b65bd013dfaee855ba9cb08790815b7d347cba37c82934`
- 标记状态：**peripheral-tested (SC2336 Control Plane & Stream Control Verified on Hardware)**。

### 14.6 下一步行动

1. **CAM-004（公共 NuttX 仓）：ESP32-P4 MIPI CSI 最小控制器与 D-PHY 接收链路**：已完成。
2. **CAM-005（公共 NuttX 仓）：CSI DMA 描述符与 PSRAM frame capture 支持**。
3. **CAM-006（团队仓）：板级 Camera 驱动注册 (`/dev/video0`) 与取帧 Smoke App**。

## 15. CAM-004：ESP32-P4 MIPI CSI 控制器与 D-PHY 接收链路实现与硬件验证

### 15.1 本轮目标与双仓边界

- **本轮目标**：在公共 `nuttx` 仓库中实现 ESP32-P4 MIPI CSI-2 Host 控制器、D-PHY 物理层接收器和 CSI Bridge 子系统的最小驱动与寄存器诊断接口，并在硬件上打通传感器流控与 D-PHY 状态联动验证。
- **双仓边界纪律**：
  - **公共 `nuttx` 仓**：添加公共 SoC 驱动 [`esp32p4_mipi_csi.h`](file:///home/uleemos/openvela-contest/nuttx/arch/risc-v/include/esp32p4/esp32p4_mipi_csi.h)、[`esp32p4_mipi_csi.c`](file:///home/uleemos/openvela-contest/nuttx/arch/risc-v/src/esp32p4/esp32p4_mipi_csi.c)、[`Kconfig`](file:///home/uleemos/openvela-contest/nuttx/arch/risc-v/src/esp32p4/Kconfig)、[`Make.defs`](file:///home/uleemos/openvela-contest/nuttx/arch/risc-v/src/esp32p4/Make.defs) 与 [`hal_esp32p4.mk`](file:///home/uleemos/openvela-contest/nuttx/arch/risc-v/src/esp32p4/hal_esp32p4.mk)；严格通过 `nxstyle` 代码风格检查。
  - **团队专属仓**：启用 `CONFIG_ESP32P4_MIPI_CSI=y`，在 `sc2336_probe` 中新增 `csi-init`、`csi-status`、`csi-test` 与 `csi-deinit` 命令，并完成真机串口日志归档。

### 15.2 实现与架构设计

1. **时钟与复位管理**：
   - 使用 `HP_SYS_CLKRST` 配置 D-PHY 时钟源为 20 MHz PLL（`MIPI_CSI_PHY_CLK_SRC_PLL_F20M`）；
   - 使能 D-PHY 配置时钟、CSI Host 总线时钟和 Bridge 模块时钟，并执行硬件复位释放。
2. **HAL 与寄存器接入**：
   - 接入底层 `mipi_csi_hal.c` 与 `mipi_csi_periph.c`；
   - 根据工作模式动态配置 Active Data Lanes（2-lane）、RAW10 数据类型过滤（`0x2b`）、帧尺寸（1280×720 / 1920×1080）和 D-PHY PLL 频段（480 Mbps）；
   - 桥接器 FIFO 阈值配置为 960 字节，防止数据突发溢出。
3. **D-PHY 状态与中断诊断**：
   - 导出 `esp32p4_mipi_csi_get_status()` 与 `esp32p4_mipi_csi_dump()` 接口；
   - 实时读取 Host `phy_rx`（`phy_rxclkactivehs`, `phy_rxulpsclknot`, `phy_rxulpsesc`）、`phy_stopstate`（`phy_stopstateclk`, `phy_stopstatedata_0/1`）、主中断状态 `int_st_main`、PHY 致命错误状态 `int_st_phy_fatal` 以及 Bridge 缓冲区深度。

### 15.3 构建与固件产物

- **构建命令**：
  ```bash
  cd /home/uleemos/openvela-contest
  PATH=/tmp/openvela-esptool-venv/bin:$PATH \
    ./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j16
  ```
- **代码规范检查**：
  - `arch/risc-v/src/esp32p4/esp32p4_mipi_csi.h`: **PASSED nxstyle check**
  - `arch/risc-v/src/esp32p4/esp32p4_mipi_csi.c`: **PASSED nxstyle check**
  - `arch/risc-v/include/esp32p4/esp32p4_mipi_csi.h`: **PASSED nxstyle check**
- **固件产物**：
  - `nuttx`：605828 bytes，SHA-256 `215cfd5c0fb42e3029f7c136459797bb06c9c500acfef1ec3af8d8bb3a5eb6f2`
  - `nuttx.hex`：654579 bytes，SHA-256 `118424e83a6c1b2e029829dfe8bf4221c6f04d17415f165c66618aac7478dd8d`
  - `nuttx.bin`：284184 bytes，SHA-256 `275c3924c3b7f860b9c099d29248f3a04cdade517fa053e6ce80c99be1875e7f`
  - `image_info`：ESP32-P4、16 MB、DIO、80 MHz、Entry `0x4ff4811c`，校验和 `0x3d` 有效。

### 15.4 真机串口验证与测试结果

- **烧录命令**：
  ```powershell
  python.exe -m esptool -c esp32p4 -p COM3 -b 921600 write-flash `
    --flash-size 16MB --flash-mode dio --flash-freq 80m `
    0x2000 openvela-esp32p4-nuttx.bin
  ```
  Hash of data verified，成功写入 `0x2000` 并完成硬复位。

- **验证项目与结果**：
  1. **系统环境**：`uname -a` 返回 `NuttX 13.0.0 d49dc5e5a9c-dirty`；`free` 显示 Umem 33549848 bytes 正常。
  2. **CSI 控制器初始化 (`sc2336_probe csi-init 720p`)**：
     - CSI Host / D-PHY / Bridge 初始化成功，`Initialized: YES, Bridge Enabled: YES`。
  3. **CSI 状态转储 (`sc2336_probe csi-status`)**：
     - 准确读取 D-PHY 初始状态，所有致命中断寄存器均为 `0x00000000`，FIFO 深度 `0 bytes`。
  4. **720p 30 fps CSI D-PHY 联动测试 (`sc2336_probe csi-test 720p`)**：
     - Step 1: CSI 控制器初始化 PASS
     - Step 2: D-PHY 待机状态采样 PASS
     - Step 3: SC2336 软复位并下发 165 个模式寄存器，关键寄存器校验 PASS
     - Step 4: 激活 Stream ON (`0x0100=0x01`) PASS
     - Step 5: D-PHY 高速流接收状态验证，零 PHY 致命错误（`int_st_phy_fatal = 0x00000000`），零包错误（`int_st_pkt_fatal = 0x00000000`），保持 200 ms PASS
     - Step 6: 停止 Stream OFF (`0x0100=0x00`) PASS
     - Step 7: D-PHY 返回待机状态 PASS
     - 结论：**ALL PASS**。
  5. **1080p 30 fps CSI D-PHY 联动测试 (`sc2336_probe csi-test 1080p`)**：149 寄存器模式表，D-PHY 高速接收与流切换全部 **ALL PASS**。
  6. **1080p 25 fps CSI D-PHY 联动测试 (`sc2336_probe csi-test 1080p25`)**：130 寄存器模式表，D-PHY 高速接收与流切换全部 **ALL PASS**。
  7. **CSI 控制器去初始化 (`sc2336_probe csi-deinit`)**：成功关闭 Bridge 并对 CSI/PHY 门控时钟，去初始化后 `csi-status` 报告 `Initialized: NO`，PASS。

### 15.5 证据文件归档

- 原始串口日志：[`hardware-logs/esp32p4-csi-dphy-smoke-2026-08-19.log`](hardware-logs/esp32p4-csi-dphy-smoke-2026-08-19.log)
- 日志 SHA-256：`7e80b78aedcd4a47a1612d093adaececd1f49ba491d6a284fc744a8181b964eb`
- 标记状态：**peripheral-tested (ESP32-P4 MIPI CSI Controller & D-PHY Link Verified on Hardware)**。

### 15.6 下一步行动

1. **CAM-005（公共 NuttX 仓）：ESP32-P4 CSI DMA 描述符与 PSRAM 帧缓冲区捕获驱动**。
2. **CAM-006（团队仓）：板级 Camera 驱动注册 (`/dev/video0`) 与取帧 Smoke App**。
3. **CAM-007/CAM-008（团队仓）：Gate G2 视频流稳定性测试与全量日志归档**。

---

## 16. ES8311 + I2S0 音频子系统移植与验证 (2026-08-22)

### 16.1 硬件引脚与架构映射

针对 **ESP32-P4X Function EV Board V1.8** 实板电路：
- **控制总线 (I2C0)**：
  - `SDA = GPIO7`, `SCL = GPIO8`，I2C0 挂载，从机地址 `0x18`，经 I2C 探测已确认芯片 ID 寄存器 `0xFD=0x83, 0xFE=0x11, 0xFF=0x01`（Everest Semiconductor ES8311）。
- **音频数据总线 (I2S0)**：
  - `MCLK`: **GPIO13** (`I2S0_MCLK_PAD_OUT_IDX`, `LP_AON_CLKRST.hp_clk_ctrl.hp_pad_i2s0_mclk_en = 1`)
  - `BCLK / SCLK`: **GPIO12** (`I2S0_O_BCK_PAD_OUT_IDX`, Master 模式)
  - `WS / LRCK`: **GPIO10** (`I2S0_O_WS_PAD_OUT_IDX`, Master 模式)
  - `DOUT (TX / Playback)`: **GPIO9** (`I2S0_O_SD_PAD_OUT_IDX` -> ES8311 SDIN)
  - `DIN (RX / Record)`: **GPIO11** (`I2S0_I_SD_PAD_IN_IDX` <- ES8311 SDOUT)
- **功放控制 (Power Amplifier)**：
  - `PA_EN / SPK_EN`: **GPIO53**（高电平使能板载功放驱动扬声器）。

### 16.2 驱动分层与架构实现

1. **ESP32-P4 I2S Lower-Half 架构驱动 (`arch/risc-v/src/esp32p4/esp32p4_i2s.c`)**：
   - 适配 NuttX 标准 `i2s_dev_s` 与 `i2s_ops_s` 操作集：`i2s_txchannels`, `i2s_txsamplerate`, `i2s_txdatawidth`, `i2s_send`, `i2s_rxchannels`, `i2s_rxsamplerate`, `i2s_rxdatawidth`, `i2s_receive`, `i2s_getmclkfrequency`, `i2s_setmclkfrequency`。
   - **AHB-GDMA 双向通道**：绑定 `SOC_GDMA_TRIG_PERIPH_I2S0` (3)，为 TX/RX 分配链表描述符队列（支持 `CONFIG_ESP32P4_I2S_MAXINFLIGHT` 并发缓冲）。
   - **中断与工作队列调度**：挂载 `ETS_AHB_PDMA_OUT_CH0_INTR_SOURCE`（TX EOF）与 `ETS_AHB_PDMA_IN_CH0_INTR_SOURCE`（RX SUC EOF），在 ISR 中更新传输队列并交由高优先级工作队列 `HPWORK` 触发 `es8311_processdone` 完成回调。
   - **精确时钟分频**：采用 160 MHz PLL 时钟源，结合 `i2s_hal_calc_mclk_precise_division` 动态计算 MCLK 与 BCLK 整数/小数分频比，标准 44.1 kHz/48 kHz/16 kHz 采样率零偏差输出。
2. **ES8311 编解码器驱动适配 (`drivers/audio/es8311.c`)**：
   - 修复 missing `<nuttx/mutex.h>` 引起的 `nxmutex_lock`/`nxmutex_unlock` 未定义链接错误。
3. **板级初始化与设备注册 (`board/contest_board/src/board_audio.c`)**：
   - 自动初始化 I2C0 与 I2S0 实例，配置 GPIO53 功放引脚输出高电平；
   - 注册 `/dev/audio/pcm0`（播放设备）与 `/dev/audio/pcm_in0`（录音设备）。
4. **CLI 测试工具 (`app/es8311_audio/es8311_audio_main.c`)**：
   - `probe`: 探测 ES8311 芯片 ID 与音频节点就绪状态；
   - `dump`: 打印 ES8311 0x00 .. 0x47 全部寄存器配置；
   - `tone <freq> <duration_sec>`: 生成 44.1 kHz 16-bit 双声道正弦波并通过 `/dev/audio/pcm0` 驱动扬声器播放；
   - `record <duration_sec> [file]`: 从板载麦克风录制 PCM 音频，分析峰值幅度与 RMS 能量指标，并支持存储至 SmartFS 分区。

### 16.3 规范与构建验证

- **代码规范**：所有新增/修改文件（`esp32p4_i2s.h`, `esp32p4_i2s.c`, `board_audio.h`, `board_audio.c`, `board_boot.c`, `es8311_audio_main.c`）经 `nxstyle` 工具全量检查，**0 errors, 0 warnings (100% PASS)**。
- **符号核验**：`riscv-none-elf-nm` 证实 `board_audio_initialize`, `es8311_audio_main`, `es8311_initialize`, `esp32p4_i2sbus_initialize` 全部成功编入固件 ELF。
- **构建输出**：`nuttx.bin` 成功生成，Exit Code 0。

---

## 17. MIPI DSI 显示屏驱动 + Framebuffer + LVGL 图形子系统 (2026-08-23)

### 17.1 硬件引脚与架构映射

针对 **ESP32-P4X Function EV Board V1.8** 实板电路与 MIPI DSI 显示屏接口：
- **MIPI DSI 接口**：
  - 2 组高速差分数据通道（`DSI_D0P/N`, `DSI_D1P/N`）与 1 组差分时钟通道（`DSI_CLKP/N`）。
  - 单通道 Lane 速率达 1000 Mbps，总带宽 2000 Mbps。
- **背光控制 (`LCD_BL`)**：
  - **GPIO26**：推挽输出，输出高电平使能显示屏背光驱动。
- **硬件复位 (`LCD_RST`)**：
  - **GPIO27**：推挽输出，上电拉低复位脉冲（10ms），拉高释放复位（120ms）等待屏幕内部初始化完毕。
- **共享控制总线 (I2C0)**：
  - `SDA = GPIO7`, `SCL = GPIO8`（共享用于 GT911 电容触摸 IC `0x5D`/`0x14`）。

### 17.2 驱动分层与架构实现

1. **ESP32-P4 MIPI DSI Lower-Half 驱动 (`arch/risc-v/src/esp32p4/esp32p4_mipi_dsi.c`)**：
   - **时钟树配置**：开启 DSI 系统时钟 `HP_SYS_CLKRST.soc_clk_ctrl1.reg_dsi_sys_clk_en = 1`，使能 D-PHY 配置时钟与 PLL 参考时钟 `HP_SYS_CLKRST.peri_clk_ctrl03.reg_mipi_dsi_dphy_cfg_clk_en = 1`, `reg_mipi_dsi_dphy_pll_refclk_en = 1`；
   - **DPI 像素时钟**：选通 PLL_F240M 时钟源，按目标像素时钟动态分频配置 `HP_SYS_CLKRST.peri_clk_ctrl03.reg_mipi_dsi_dpiclk_div_num` 并使能 `reg_mipi_dsi_dpiclk_en`；
   - **D-PHY TX PLL 初始化**：基于 40 MHz 晶振基准配置 D-PHY TX PLL，设置 1000 Mbps Lane 速率并校准 HS/LP 转换时序；
   - **DCS 指令引擎**：封装 `esp32p4_mipi_dsi_write_dcs` 与 `esp32p4_mipi_dsi_read_dcs`，支持向面板发送 Short/Long DCS 封包（Soft Reset `0x01`、Sleep Out `0x11`、Display On `0x29`、Set Pixel Format `0x3A` 等）；
   - **Video Mode DPI 时序引擎**：配置 DSI Host DPI 水平（HBP, HFP, HSYNC, HACT）与垂直（VBP, VFP, VSYNC, VACT）时序，支持 Non-Burst Pulse / Non-Burst Event / Burst 模式；
   - **Bridge 格式转换与流控**：针对 ESP32-P4 v3.x 配置 Bridge 输入/输出色彩空间映射（RGB565/RGB888）与内部 FIFO 阈值。
2. **NuttX Framebuffer 驱动接口 (`arch/risc-v/src/esp32p4/esp32p4_mipi_dsi.c`)**：
   - 完整实现 NuttX 标准 `fb_vtable_s` 操作集：`getvideoinfo`, `getplaneinfo`, `pan_display`, `setpower`；
   - 在 PSRAM 中分配 64-byte 字节对齐的图形帧缓冲区，在 `pan_display` 中结合 `esp_cache_msync(..., ESP_CACHE_MSYNC_FLAG_DIR_C2M)` 保证 CPU 绘制数据即时刷入物理内存；
   - 导出标准架构函数 `up_fbinitialize(int display)`、`up_fbgetvplane(int display, int plane)`、`up_fbuninitialize(int display)`。
3. **板级显示子系统集成 (`board/contest_board/src/board_display.c`)**：
   - 在 `board_app_initialize()` 中自动执行：GPIO26/27 初始化、复位脉序、D-PHY PLL 配置、面板 DCS 唤醒时序、`/dev/fb0` 设备节点注册、启动连续 Video 模式视频流推送。
4. **DSI Display CLI 测试套件 (`app/dsi_display/dsi_display_main.c`)**：
   - `info`: 查看 Framebuffer 驱动信息、分辨率、位深、行步长与显存地址；
   - `bars`: 在屏幕上绘制 SMPTE 标准 8 色条测试图形；
   - `color <r> <g> <b>`: 全屏纯色填充测试（校验色彩映射与坏点）；
   - `grid`: 绘制 32x32 像素定位网格与中央瞄准框（校验分辨率与边界对齐）；
   - `fps [duration_sec]`: 动态跳跃色块帧率压测，统计真实刷屏 FPS 与显存吞吐率（MB/s）。
5. **LVGL 9.1.0 图形子系统支持 (`apps/graphics/lvgl`)**：
   - 启用 `CONFIG_GRAPHICS_LVGL=y`、`CONFIG_LV_USE_NUTTX=y`、`CONFIG_LV_USE_DEMO_WIDGETS=y`、`CONFIG_EXAMPLES_LVGLDEMO=y`；
   - 支持通过 NSH 直接运行 `lvgldemo` 启动图形控件演示。

### 17.3 规范与构建验证

- **代码规范**：所有新增/修改的 7 个文件（`esp32p4_mipi_dsi.h`, `esp32p4_mipi_dsi.c`, `board_display.h`, `board_display.c`, `board_boot.c`, `dsi_display_main.c`）经 `nxstyle` 全量检查，**0 errors, 0 warnings (100% PASS)**。
- **符号核验**：`riscv-none-elf-nm` 证实 `board_display_initialize`, `esp32p4_mipi_dsi_initialize`, `esp32p4_mipi_dsi_start_video`, `dsi_display_main`, `lvgldemo_main` 全部正确链接至固件。
- **固件产物**：`nuttx.bin` 大小 643 KB (657,556 字节)，SHA-256 `771712d51c5a00ffcd8b38001582ec17703ef6b0699b2828aa0edce2d6844184`。

