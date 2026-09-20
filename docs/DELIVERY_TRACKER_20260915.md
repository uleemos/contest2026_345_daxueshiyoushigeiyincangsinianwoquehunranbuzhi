# Unified delivery tracker — 2026-09-15

> **用户已明确要求恢复，当前进行中。** 历史断点见
> [暂停交接记录](DEVELOPMENT_PAUSE_20260915.md)；本次执行见
> [恢复工作日志](../hardware-logs/resume-progress-20260915.md)。下表起始状态由后文更新覆盖。

Scope merges prior storage/KWS/cloud/acceleration work and the user's P0-1–7,
P1-1 additions. No commits/pushes, audio playback/new recording, formatting or
replacement of user data. Stage completion is a checkpoint, not a stop request.

States: 待开发 / 进行中 / 自动测试通过 / 板端实测通过 / 待真人验收 /
被外部依赖阻塞. A local test pass never promotes the entire task to accepted.

| Task | Current state / evidence | Next executable work |
| --- | --- | --- |
| SD/C6 shared scheduling | 进行中; sticky single owner, isolated slot tests only | Per-slot state and shared controller lifecycle; integration regression |
| microSD queue storage | 进行中; exFAT exclusive file + reboot readback, storage-kws-progress-20260915.md | Production mount/namespace, bounded outbox persistence |
| P4 microWakeWord | 待开发; host control 1000/reset pass only | Board runner + frontend timing/memory |
| Target phrase training | 进行中; existing recordings retained | Audit corpus, reproducible train/convert/evaluate; no new recordings now |
| Real sender / offline resend | 进行中; host outbox pass, separate P4 MiMo summary pass | Actual sender, scheduling, retry/recovery with SD/C6 |
| PIE/HWLP / ESP-NN | 待开发; MoveNet ~50 s baseline | After cloud/storage recovery baseline: instructions, context, kernels, fixed-image comparison |
| P0-1 coordinates | 进行中 | Shared transform description, roundtrip and actual RAW10 tests |
| P0-2 squat replay | 进行中 | Confidence/loss/time/pause edge tests and fixes |
| P0-3 training flow | 待开发 | One business event entry, countdown/pause/end/restart tests |
| P0-5 minimum UI | 待开发; existing dashboard foundation | Synchronize skeleton/count/time/hints/wake/network/errors |
| P0-4 decoupling | 待开发 | Bounded latest frame and nonblocking service workers/fault tests |
| P0-6 integration endurance | 待开发 | Fixed firmware identity, bounded runs, actual metrics and recovery |
| P0-7 delivery/recovery | 进行中; private accepted firmware retained | Remove /tmp build inputs, pin/hash/licensing, executable scripts |
| P1-1 display refresh | 待开发; staged memcpy ~3.63 FPS only | Frame-boundary switch investigation after available P0 work |
| Ethernet adaptation | 待开发; retained prior scope | Driver/document audit; physical network acceptance conditional on cable |
| MoveNet 1000 P4 invokes | 待开发; user stopped after ~20+, exact logs not yet recounted | Retain deferred night test; optimize before expensive repeat |

## Execution notes

- Repository and skill preflight read. repo status default multiprocessing
  fails sandbox socket creation; `repo status -j1` succeeds. Existing changes
  across projects preserved. Toolchain GCC13.4.0.
- Begin independent coordinate/FSM work while retaining storage as first
  hardware integration dependency: removing the SDMMC owner check without
  per-slot state would corrupt an active client's controller configuration.
- Existing preview uses rotated center-cover; model uses rotated letterbox.
  These are distinct coordinate spaces, not interchangeable normalized points.
- Squat inspection: invalid poses currently leave in-progress repetitions
  alive; confidence/time continuity need regression coverage.

## Deferred human acceptance (not software prerequisites)

Body/bone alignment; manual squat count and hint comparison; complete training
flow; real target-word wakeup; subjective latency; screen readability/refresh;
continuous real exercise; contest submission form and final demo flow.

## 暂停时状态更新（覆盖上方起始状态）

| Task | 实际状态 | 未覆盖部分 |
| --- | --- | --- |
| P0-1 coordinates | 局部自动测试通过，重复100轮；预览/模型共用尺寸计算 | 实际骨架叠加接入、方向语义与真人对齐未验收 |
| P0-2 squat replay | 自动测试通过，重复100轮；已修复连续性/置信度/提前转向问题 | 管线级回放、实机计数和真人准确性未验收 |
| P0-3 training flow | 独立事件控制器自动测试通过100轮；管线接入代码已编译 | 真实语音/触摸业务调用、完整事件到摘要集成未验收 |
| P0-5 minimum UI | 进行中；新增计时/状态行代码已保存 | 最后界面改动尚未编译，布局/帧同步/真人评价待做 |
| P4 microWakeWord | 控制模型1000调用/复位回放板端通过；前端合成PCM板端与主机一致 | 采音→重采样→特征→量化→模型连续运行、专用词和真实唤醒未通过 |
| Target phrase training | 原语料审计通过；官方训练代码和Python3.12已准备 | TensorFlow训练环境、训练/转换/评估尚未完成 |
| microSD queue storage | 新不可变记录方案主机9项通过；修复版已编译 | 初次板测ENAMETOOLONG失败；修复版尚未烧录复测、重启ACK未验收 |
| SD/C6 / real resend | 进行中，未作共享驱动修改 | 仍单slot owner；真实sender及并发调度未接通 |
| P0-7 delivery/recovery | 持久输入哈希校验及构建脚本已运行通过；未烧录产物已私有保存 | CMake持久路径、干净环境复现、统一烧录恢复脚本及最终交付待做 |
| P0-4 / P0-6 / P1-1 / acceleration / Ethernet | 保留，待开发或待集成 | 不因本次暂停取消；不得标为验收通过 |

## 恢复执行更新（优先于上方历史状态）

证据详情及固件hash统一见`hardware-logs/resume-progress-20260915.md`。

| 任务 | 当前确切进展 | 剩余 |
| --- | --- | --- |
| microSD不可变队列 | 实卡诊断队列/真实sender/显式ACK重启重放通过；14项主机通过；独立后台服务实际断网落盘→重启联网自动补传通过 | 实际训练摘要接入、AP物理断网/断电长测、容量回收政策 |
| P4 microWakeWord整链 | 合成44.1kPCM→Q20 FIR→特征→INT8→TFLM板测/主机一致；1秒PCM需260–290ms | 持续MIC调度、目标词质量、真实唤醒和整机并行CPU预算 |
| 专用词训练 | 私有训练/流式INT8导出/主机TFLM/P4 1000调用及复位通过 | 开发原型判别质量不通过；负语音不足、5秒窗口、缺独立验收集 |
| 坐标/骨架 | 共用fit/cover，实际绘制像素回归通过；后台预览中已接映射入口 | 及时的模型结果、固定图完整叠加、真人对齐 |
| 深蹲/训练管线 | 主机回放、NaN恢复、暂停旧帧门控、摘要保存失败/幂等测试通过 | 真实业务入口、完整流程板测与真人计数 |
| 最小UI | 状态行/计时/绘制边界主机通过；摄像头骨架消费入口已接 | 全流程事件、实际计数提示/唤醒网络状态统一绑定 |
| 预览推理解耦 | 一运行+一pending后台worker；低优先级推理时60秒193帧/退出堆恢复 | 推理结果仍过期、退出等待长；网络worker、超时恢复和完整训练事件解耦 |
| 交付恢复 | 持久构建输入，重装配模型/素材；私有快照、校验、烧录工具6项主机和实际烧录通过 | 干净环境/CMake全构建、模型/语料许可总账、最终验收包 |
| 云TLS/MiMo | P4 wrong-name flags0x04、正确TLS、真实摘要HTTP200+schema；后台SD pending恢复/ACK去重和KWS并行回归通过 | 请求总deadline强化、实际训练业务提交、物理AP掉线/整机长测 |
| SD/C6共享 | 两种初始化顺序、真实队列写入、后台补传/KWS并行通过；修复抢占后的SD完成事件误判 | 更长并发压力；真实DMA故障fail-closed需重启，IRQ路由未支持 |
| 加速/以太网/P1显示 | 保留，待实现 | 不因KWS或阶段通过取消；PIE/ESP-NN/以太网/帧边界切换尚未完成 |

无人值守执行持续进行；不提交、不推送、不录音、不播放。最终人工事项仍集中在前述清单。

### 加速扩展检查点

HWLP 隔离短指令板测通过（1/2/17/100循环数值正确，堆不变），
`hardware-logs/hwlp-isolated-p4-20260915.log`。已实现 opt-in 七寄存器
上下文保存和双线程主动切换测试已板测通过：各10轮，错误0；云端与KWS回归通过。
官方 ESP-NN v1.3.0 固定commit d8866fa3762ee9caf56712b4019d004d86e0f3f8，
Apache-2.0源码私有保存。现有GCC13.4不能汇编PIE卷积指令（工具链探针日志），
已隔离官方14.2工具链，不替换全局编译器。PIE短点积16种对齐位置板测通过；
完整PIE上下文候选正在编译，优化算子和模型速度目标均未通过。

### 10:25 检查点（继续执行）

- 当前板上44d6eb03固件：PIE隔离指令、HWLP任务切换、合成MWW链、SD显式ACK读取通过。
- 同固件真实MiMo固定摘要2840ms、HTTP200且schema通过；错误主机名拒绝、正常TLS、
  原SD文件16KiB前后读取通过。证据`pie-isolated-cloud-regression-20260915.log`。
- 待验证候选增加400字节异常帧与224字节PIE负载，双线程各25轮寄存器模式测试。
  仅任务级CFG=2，不虚拟化读清饱和标志/PERF，不允许ISR使用PIE。
- 固定图基线50850ms（profiler开启）：Conv2D45990ms，Depthwise3980ms；
  不满足2秒/200ms。证据`hwlp-fixed-pose-baseline-20260915.log`。

### 11:00 加速检查点（覆盖先前状态）

PIE寄存器/HWLP切换通过；官方Conv+有界Depthwise、PSRAM权重副本固定图3.70秒，
参考51个浮点位值完全相同。真实MiMo3390ms/200/schema、TLS/SD原文件回归通过。
6c7fb470私有候选为当前恢复点。仍未达到2秒/200ms，默认生产推理尚未切换诊断后端。
已证实上游两处边界缺陷（小窗口scratch、DW>256丢bias），失败日志及自动恢复保留。
下一候选测试通道打包和QACC32本地补丁，状态见最新恢复日志。


### 诊断推理提速检查点（覆盖先前加速数值，200ms仍未通过）

- 参考ESP-NN1.3.2及ESP-DL开源内核，加入P4 FPU上下文、QACC权重打包、
  精确重量化、单/双输入查表及32像素分块。固定模型五轮1.44–1.45秒，
  平均1.448秒，相比本轮2.39秒基线减少约39%。
- 当前候选重新执行原参考，51个输出浮点位值完全一致；83个卷积/深度卷积
  探针、FPU/HWLP/PIE切换、固定MWW1000调用及复位回放通过。
- 同固件真实固定MiMo摘要、TLS主机名正反验证、SD原文件只读回归通过。
- 当前BIN `a7fe058d1bcc2f7b983b34fcc35edc36e943bca56e720160877a7208bb7f6756`。
  私有恢复包 `.secrets/pose-speed-tile32-20260915`。
- **200ms未完成。** Conv2D约850ms、Depthwise约340ms；诊断arena12MiB，
  冷初始化另需1.19秒。此结果不代表实时摄像头/真人准确率验收，生产后端未切换。
- 构建与证据总账：`docs/POSE_SPEED_OPTIMIZATION_20260915.md`；
  一键构建 `bash tools/build_pose_diagnostic.sh 16`（不自动烧录）。
