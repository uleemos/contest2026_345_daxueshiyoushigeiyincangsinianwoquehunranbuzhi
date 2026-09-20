# 开发暂停与恢复交接 — 2026-09-15

## 0. 暂停边界

用户明确要求现在暂停开发，等下次启动。已停止启动新的开发、编译、
烧录和测试。已查询本轮相关长命令：构建、烧录、串口测试、训练代码
下载、uv/Python 安装均已退出，无这些任务在后台继续运行。
暂停收尾仅整理状态、检查日志、保存构建产物和本文档。

所有原有和新增任务保留。没有 commit/push，没有播放或新录音，没有
格式化、分区或删除用户文件。已有跨仓库未提交修改全部保留。

## 1. 当前三份状态必须区分

### 板端正在运行的版本

- 最后烧录：`hardware-logs/outbox-p4-flash-20260915.log`，烧录数据哈希校验通过。
- 串口固件时间：Sep 15 2026 01:34:23，NuttX `f620a900906-dirty`。
- 此次镜像 SHA256：`6a6e2a0fd0668604c68ffc1804bf2195d25a41c026f4067e1de43cf0a0e5d585`。
- 最后板测：`outbox-exfat-p4-20260915.log`，队列测试返回
  **-36 / ENAMETOOLONG，sender_calls=0，失败**，不是队列验收通过。
- 命令退出到 NSH，清理路径执行卸载、关闭写入门；slot0 的 sticky owner
  仍保留到重启。下次测试 C6 前必须考虑这个状态，不能认为当前已支持共存。
- 最后 free：Umem used=1,236,640；Kmem used=4,760。

### 已编译、但未烧录的修复版本

- `hardware-logs/outbox-p4-build-namefix-20260915.log`：构建成功。
- 修复目录/状态文件名超过 CONFIG_NAME_MAX=32；采用16位十六进制
  FNV64索引名，最长状态文件名29字符；原 session_id/摘要逐字比较，
  碰撞返回冲突，不允许覆盖或把不同记录当成同一条。
- BIN SHA256：`cf7ff811f50ba81a3627f73eab510011295c634558fb9f2b8d90951e31035445`。
- ELF SHA256：`6ef85c33ce20e911204de8dabd599a035ce28a0aad021bc2432776913fd51aa6`。
- config SHA256：`e1c4f1e7284b83bfdb671ed3003efcf980162d5a46719469fd3d35841f3fe2eb`。
- 私有副本：`.secrets/pause-20260915-unflashed/{nuttx,nuttx.bin,.config}`，
  目录700、文件600、Git忽略。**这是未实测候选，不是已验收基线。**

### 比上述构建更晚的源码

最后添加的 `velafit_render_session_status()` 和管线状态行调用，
**尚未重新编译或测试**。不要用上面的构建成功记录覆盖这些新改动。
因此下次完整构建前，源码不应被描述为全部已编译通过。

## 2. 本轮完成的局部功能及证据

### 摄像头/模型/显示坐标

- 新增 `app/sc2336_probe/sc2336_transform.h`；共享整数 fit/cover 计算，
  RAW10 模型 letterbox 和预览 center-cover 已改用它。
- 像素边界坐标约定：sensor→view CCW90，(x,y)→(y,raw_w-x)。
  1280×720 sensor 对应720×1280 view；192方形输入内容108×192，
  x偏移42；1024×600原生FB center-cover裁剪720×421，y偏移429。
- 新增17个已知坐标、padding/crop外点、NaN检查；修复旧测试仍断言
  旋转前横向letterbox的过期预期。
- `tools/hardware/delivery_regression.py --repeat 100`，
  `hardware-logs/delivery-host-20260915.log`：实际C源码主机测试通过。
- 限制：转换工具尚未接到真实预览的骨架叠加消费者；物理竖屏与模型
  人体正立语义仍需端到端固定图检查，不能据此宣称真人骨架已对齐。

### 深蹲回归

- 新增 `test_squat_replay.c`；修复无效/低置信度/非有限值时保留半次
  动作的问题；新增 interrupt 保留已完成计数但丢弃中断动作。
- 必须先观察站立才开始计数；超过1000ms的采样间隔取消动作连续性，
  重复/倒序时间戳忽略；修复尚在下降时过早转入上升状态的条件。
- 5/10/15/30 FPS、浅蹲、阈值抖动、离开/重新进入、暂停中断、
  不规则采样、时间戳回绕等合成测试通过100轮；见同一主机日志。
- 1000ms连续性门槛是保守软件策略：约50秒推理基线无法满足它，
  应加速而不是放宽门槛伪造可靠计数。真人提示合理性未验收。

### 训练流程与界面基础

- 新增 `pipeline/velafit_session.[ch]`，统一业务事件：
  idle→countdown(3s)→running→paused/resume→finished→restart。
- 独立测试覆盖重复事件、非法事件、倒序时钟、暂停排除计时、重开；
  100轮通过。明确属于事件注入，不是语音识别。
- 管线加入 event/tick 入口；不再使用帧数/30计算时长或显示固定30FPS。
- 新增界面状态行：训练状态/时间/唤醒/网络，沿用现有绘制，无新框架。
- 限制：尚未完成真实UI/语音调用、独立调度和摘要全链路回归；最后
  状态行改动未编译。暂停时非深蹲FSM处理、真实模型数据时序与业务
  clock的对齐、计时独立呈现和边界布局仍需检查。

### P4 microWakeWord 控制模型与前端

- 新增 `models/velafit_mww_probe.cc`、`velafit_mww_control.S`、
  `velafit_mww_frontend.cc`；NSH入口 `velafit_ai mww_control 1000` /
  `velafit_ai mww_frontend`，不访问麦克风或喇叭。
- `mww-p4-control-20260915.log`：1000调用4.52秒；
  `mww-frontend-p4-20260915.log`回归1000调用4.51秒；arena均20,164字节。
- 两次均100帧×2复位回放一致；命令退出堆使用回到测试前值。
- 前端16kHz合成PCM共16000采样→98帧，160/441分块均hash=8534426c；
  板端每轮30ms，主机实际同源前端哈希完全相同。
- `mww-frontend-contract-host-20260915.log`：6项通过，包含
  pymicro-features2.0.2训练特征对照。
- 计时粒度10ms，不宣称精确单次最大延迟。控制模型是okay_nabu，
  **不是专用词模型**；采音/重采样/特征量化/流式模型仍未串成实时链路。

### 队列持久化准备

- `sync/velafit_outbox.c`已加入固件构建；修改为不可变初始记录+
  最多10个尝试状态文件，避免依赖FatFs不支持的覆盖rename语义。
- 保留失败文件；损坏状态报错，不删除、不静默确认、不覆盖原摘要。
- 主机9项通过：失败恢复、去重、冲突、永久错误、32记录容量、
  原记录不变、损坏状态、10次尝试上限、NAME_MAX。
  证据 `outbox-generations-host-fixed-20260915.log`。
- 新命令 `c6_wifi sd-queue-test` / `sd-queue-replay`：真实SD存储+
  **模拟sender**；不是MiMo验证。首次失败与修复后未实测状态见第1节。
- 修复后专用测试目录 `/sdcard/vf-queue-test-20260915`；mkdir排他创建，
  已存在时报错，不能删目录/换标志覆盖来“跑过测试”。先读状态定位。
- 尚无SD/C6共存、生产挂载所有权、后台真实sender、ACK回收策略及
  断电容错验收。损坏记录当前会阻止本次tick，其他记录公平性需改进。

### 训练准备和交付工具

- `audit_kws_corpus.py`审计已有3段正样本、2段负样本，均5秒、
  44.1kHz单声道PCM16。技术指标/哈希见 `kws-corpus-audit-20260915.json`。
  未上传音频；按原始录音划分训练/开发留出，全部标记非独立验收集。
- 官方训练代码已下载到 `.secrets/micro-wake-word-training`，
  commit `4665173cd35f1cff9a61e06fc427f124766c488e`，Apache-2.0。
  来源 https://github.com/OHF-Voice/micro-wake-word 。框架许可证不自动
  代表任何新增语料/声音模型的使用许可。
- uv0.8.22已安装在临时工具venv；Python3.12.11已下载至
  `.secrets/python/cpython-3.12.11-linux-x86_64-gnu`。
  TensorFlow/完整训练环境尚未安装，没有启动训练或生成目标词模型。
- `check_build_inputs.py`验证3个持久素材哈希；
  `bash tools/build_delivery.sh 16`已实际成功，Make路径通过显式变量
  使用`.secrets/build-inputs`，不读取/tmp中的模型/固定图。
- 限制：原Kconfig默认路径仍是/tmp，绕过该脚本的构建及CMake路径
  还要完善；尚未完成干净环境重建或一键烧录/恢复工具。

## 3. 下次启动的明确恢复点

1. 先读本文及 `DELIVERY_TRACKER_20260915.md`、
   `hardware-logs/storage-kws-progress-20260915.md`。检查三仓库状态，
   保留全部现有改动，不提交/推送，不重新索要已提供凭证或板卡参数。
2. **第一个实现检查点**：检查并编译最后的状态行改动；继续完善
   主机界面/业务管线测试。若只验证queue修复，也可用保存的未烧录候选，
   但必须明确它不包含最后的界面改动。
3. **第一个板端检查点**：烧录NAME_MAX修复候选后运行
   `c6_wifi sd-queue-test`。成功后重启执行`sd-queue-replay`确认ACK持久化，
   再回归原16KiB文件只读回读和C6（当前仍需重启切槽）。
   所有过程区分真实SD/模拟sender/真实MiMo。
4. 推进SD/C6共享控制器：仍是全局g_sdiodev和sticky owner，
   不可直接删除owner检查；必须设计每槽状态/统一传输锁/中断与
   时钟恢复，并核实C6和SD调用方的完整事务锁边界。
5. 穿插KWS完整流式链路、专用词训练和转换评估；复用已有录音。
   Python3.12可继续建持久venv，不必因为缺独立真人测试暂停其他软件任务。
6. 新增主线继续：坐标消费者接入→管线级深蹲回放→业务流程/最小UI→
   预览推理网络解耦→真实队列补传/整机稳定性→交付恢复。
7. 保存云端/存储已验收可恢复基线之后，继续PIE/HWLP与ESP-NN，
   固定图片对比、性能门槛、成功后的云端回归；失败回退已验收固件。
8. Ethernet、真正帧边界显示切换、原MoveNet1000次夜测等旧任务不取消。
   旧夜测暂停的精确已运行次数还须从原始日志核实，不能把“20多次”填成确数。

恢复基线：原 `.secrets/microsd-exclusive-baseline-20260915/`仍保留，
BIN SHA256 `5d63c47f92583de23fab8f50819af880b792b92f01d00ccd6a33bc2125c9f9fa`，
烧录地址0x2000。它仅验收了当时存储能力，不包含本轮新增功能。
不要在此次暂停期间自行烧录恢复；下次根据测试需要选择。

## 4. 集中留给用户的事项

现在没有需要立即补充的信息或操作，开发按用户请求暂停。
之后集中验收：真人骨架对齐、深蹲人工计数/提示、完整训练流程、
真实专用词唤醒、交互延迟、屏幕清晰度/刷新、真人连续训练、
比赛提交形式和最终演示流程。独立唤醒验收语料/背景声时长不足，
后续再安排补录，不把已有开发录音当独立测试集。
