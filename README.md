# VelaFit AI 智能运动体态教练

> **参赛编号**：345  
> **赛道方向**：AI 硬件产品创新 & 新硬件适配（ESP32-P4 + openvela）  
> **核心硬件**：ESP32-P4X Function EV Board V1.8 (SoC: ESP32-P4NRW32X Dual-Core RISC-V @ 400MHz, 32MB PSRAM + 16MB Flash) + ESP32-C6-MINI-1 (Wi-Fi 6) + SC2336 MIPI Camera + ES8311 I2S Audio Codec

---

## 一、 作品简介

**VelaFit AI** 是一款基于 **openvela**（Apache NuttX 架构）与 **ESP32-P4** 高性能 RISC-V SoC 深度定制的**端云协同边缘智能运动体态教练**。

传统智能健身产品多依赖“云端视频上传”，存在高延迟、无法实时计数、用户室内隐私泄露和弱网失效等痛点。VelaFit 创新性地采用**深度端云协同（Edge-Cloud Synergy）架构**：
- **端侧（ESP32-P4）**：利用 RISC-V PIE/QACC SIMD 指令加速的 **TFLM + ESP-NN 边缘 AI 推理引擎**，本地实时运行 INT8 人体姿态关键点检测（MoveNet/YOLO-Pose），配合一欧元滤波与有限状态机（FSM），在视频数据不出芯片的前提下实现 **<50ms 极低延迟的深蹲/开合跳动作计数、体态质检与实时语音反馈**。
- **云侧（ESP32-C6 + AI Agent）**：通过 Wi-Fi 仅上传动作结构化统计日志（JSON），由 openvela `ai_agent` 云端大模型生成个性化“课后运动复盘报告”与长期健康处方。

---

## 二、 系统架构与目录结构

```text
contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/
├── docs/                                  # 核心设计与技术规格文档
│   └── VELAFIT_AI_SYSTEM_DESIGN.md        # VelaFit 边缘 AI 系统与端云协同设计方案
├── board/contest_board/                   # ESP32-P4X Function EV Board V1.8 板级适配
│   ├── configs/nsh/defconfig              # 生产 defconfig
│   ├── src/                               # 板级引脚、I2C/I2S/PSRAM/Flash 初始化
│   └── scripts/                           # 固件烧录与自动化脚本
├── app/                                   # 团队专属应用与测试套件
│   ├── velafit_engine/                    # 边缘 AI 推理引擎与姿态算法核心
│   ├── es8311_audio/                      # ES8311 音频子系统与 CLI 测试
│   ├── sc2336_probe/                      # SC2336 摄像头探测与流控制测试
│   └── g1_smoke/                          # Gate G1 平台稳定性自动化测试
├── hardware-logs/                         # 真机硬件测试原始日志与 SHA-256 证据
├── ai/skills/openvela-esp32p4-porting/    # ESP32-P4 专用移植技能与规则库
├── logs/                                  # AI Coding 交互记录归集
├── ESP32P4_AI_HANDOFF.md                  # 跨会话开发接力文档
└── PORTING_NOTES.md                       # 硬件与驱动移植台账
```

---

## 三、 运行与编译方式

### 1. 编译环境要求
- Linux x86_64 或 WSL2 环境；
- 工具链由 openvela 构建脚本自动管理。

### 2. 编译固件
在工作区根目录下执行：
```bash
./build.sh vendor/openvela/boards/contest2026_345_board/configs/nsh -j16
```
产物生成于 `nuttx/nuttx.bin`。

### 3. 真机烧录与测试
通过串口或 Windows esptool 烧录：
```bash
python -m esptool --chip esp32p4 -b 921600 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x2000 nuttx.bin
```

---

## 四、 阶段规划与演进路线

| 阶段 | 里程碑 | 核心内容与验收方式 | 当前状态 |
| :--- | :--- | :--- | :--- |
| **G1** | 平台基线 | RISC-V 核心、32MB PSRAM、16MB Flash、I2C/GPIO、20/20 冷启动 | **已通过** |
| **G2** | 摄像头取流 | SC2336 探测、流控制、MIPI CSI DW-GDMA PSRAM 帧捕获 | **核心取帧及 5 分钟稳定性已通过；30 分钟整机 soak 待执行** |
| **Stage 1** | 推理引擎移植 | TFLM + ESP-NN SIMD 汇编算子集成与 Benchmark | **当前进行中** |
| **Stage 2** | 姿态检测验证 | 载入单张 160x160 INT8 测试图，解析 17 关键点 | **待开展** |
| **Stage 3** | 动作与质检仿真 | 注入连续关键点时序流，验证深蹲/开合跳 FSM 与防抖 | **待开展** |
| **Stage 4** | 端到端与多媒体联调 | 摄像头输入 ➔ AI 推理 ➔ DSI 骨骼叠加 ➔ ES8311 播报 | **待开展** |

---

## 五、 AI Coding 使用说明

本项目严格按照大赛规范采用 AI 辅助开发：
- **方案架构与设计**：通过与 AI 深度对齐，确立了“端云协同”架构与 RISC-V PIE SIMD 算子加速路径；
- **底层驱动与规范审计**：利用 AI 自动校验 `nxstyle` 代码风格与 ESP32-P4 原理图引脚约束；
- **测试与证据归档**：每次硬件联调均生成完整的日志并计算 SHA-256 存入 `hardware-logs/`。

完整 AI 交互日志记录在 `logs/` 目录中。
