# TinyPose 实时接入与自动验收（2026-09-19）

## 已完成的软件路径

- `velafit_ai camera_tiny_pose`：SC2336 RAW10 → RGB192 → 顺时针旋转 →
  96×96 area resize → INT8 量化 → TFLM Invoke → 8 点解码 → 深蹲 FSM。
- `sc2336_probe preview_tiny [sec]`：LCD 实时画面、TinyPose 骨架、置信度状态、
  深蹲计数与质量 HUD；推理在线程中异步执行，待处理邮箱上限为 1。
- 8 点按 `{左/右肩、左/右髋、左/右膝、左/右踝}` 映射到 COCO 17 槽位。
- 置信度阈值为 0.30。至少 6 点可显示部分骨架；只有 8 个必需点全部通过时
  FSM 才接收该帧，否则中断未完成动作并显示 `LOW CONFIDENCE`。
- 保留原 `camera_pose`、`preview_pose`、`pose_bench` 和 MoveNet 验收路径。

## 自动测试结果

固件运行于真实 ESP32-P4 v3.2，模型 SHA256：
`d911523cf3ec66dc9a22a9e98e706265be8917c1dcc826679b88652d5bc99662`。

| 检查 | 结果 |
|---|---|
| 固定真实图像预处理 | C 实现与冻结 INT8 fixture 27,648 字节逐字节一致 |
| TFLM Invoke 100 轮 | min 10 ms，avg 11.6 ms，P50 10 ms，P95 20 ms，max 20 ms |
| 数值一致性 | host mismatch 0，repeat mismatch 0 |
| 延迟门限 | `gate_150ms=PASS`，`gate_200ms=PASS` |
| 单帧摄像头整链 | 约 330 ms；capture 100 ms，convert 40 ms，preprocess 20 ms，Invoke 10 ms |
| LCD 60 秒稳定性 | 179 帧、179 次推理完成、0 error、0 replaced、0 stale |
| LCD 可见结果延迟 | capture-to-result mean 636 ms，max 650 ms（含预览渲染和帧调度） |
| FSM 自动模拟 | 9/9 次触发；标准、浅蹲、膝内扣分支均通过 |
| 释放后内存 | Umem/Kmem 与测试前一致：1,236,640 / 4,704 bytes used |

Invoke latency 仍明确排除 camera capture、RGB conversion、rotation/resize、render、
FSM 和 cold model init。LCD 可见结果统计包含摄像头、转换、异步调度和渲染，因此不能
与 Invoke-only 的 150/200 ms 门限混用。

当前静态画面为 `renderable=yes`，但右踝约 0.23，得到 `fsm_points=7/8`，所以 FSM
正确拒绝该帧。这是下一步真人站位和动作验收需要确认的模型质量问题。

## 固件与证据

- BIN SHA256：`5658e75f7d012b18add6c3d5078f07ab6ecc0758f9c1f57740b8956d3afb6ef5`
- ELF SHA256：`9de3c59bfb12eb1b6994a0007d9f0ee6545b05b33f695a27da525037b666d284`
- config SHA256：`9f376dafa50c84a02bf4e8b7902b5444e7ceecd9a5980552dbc2550b5598ada3`
- 固件包：`.secrets/tinypose-live-integration-v5-20260919`
- 构建：`hardware-logs/tinypose-live-integration-v5-build-20260919.log`
- 烧录：`hardware-logs/tinypose-live-integration-v5-flash-20260919.log`
- 60 秒 LCD：`hardware-logs/tinypose-live-integration-v5-60s-20260919.log`
- 最终自动回归：`hardware-logs/tinypose-live-integration-v5-final-auto-20260919.log`

## 下一步真人验收

运行 `sc2336_probe preview_tiny 60`，由真人在站立区完成站立、下蹲、最低点和起身。
需要肉眼确认骨架方向/左右/关节位置，并确认 HUD 从 `LOW CONFIDENCE` 进入
`TRACKING`，一次完整深蹲只增加一次计数。该步骤尚未执行，不能由静态场景自动测试替代。

## V10 LCD 引导与自适应验收更新（2026-09-20）

- TinyPose FSM 置信度阈值调整为 0.20；MoveNet 默认 0.30 保持不变。
- 深蹲状态机根据倒计时期间的站立膝角建立本轮基线，按相对角度差计数。
- `preview_tiny [sec]` 先显示实时画面、`STAND READY` 和 5 秒倒计时；倒计时
  结束后计数清零，显示红色录制点、`CAPTURING`、剩余秒数和
  `GO - DO SQUATS`；结束后保留 `CAPTURE DONE`。
- 质量日志分别报告 `shallow`、`valgus` 和 `lean`，并且倒计时帧不进入
  真人验收统计。
- 所有串口汇总均移到传感器停流和 CSI/DMA 关闭之后，避免 CSI/DSI 持续工作时
  USB Serial/JTAG 发送阻塞。

V10 实机 5 秒准备 + 10 秒采集冒烟测试：45 帧，其中有效采集阶段 30 帧；
推理提交 45、完成 44、error 0、replaced 0、stale 0，命令正常返回 NSH。
该测试验证了屏幕阶段控制和采集链路，没有真人动作，所以 `reps=0` 符合预期。

- V10 BIN SHA256：`a93fdaefed7fcb372bf6945669cc02d8e5736d21f11ea86d2bf65b05715bd85b`
- V10 ELF SHA256：`6000da7fcaaf1b306d8f52b242257b8b581b782a963e6d10b4f870dbf9730aa2`
- V10 固件包：`.secrets/tinypose-live-integration-v10-20260920`
- V10 构建日志：`hardware-logs/tinypose-live-integration-v10-build-20260920.log`
- V10 LCD 实机日志：`hardware-logs/tinypose-lcd-countdown-v10-smoke-20260920.log`

V7 真人验收实际完成约 6–7 次动作（约 2–3 次深蹲），系统只计数 2 次且有效数为
0，因此 V7 未通过。V10 自适应计数仍需完成第四次真人验收后才能判定通过。

## V10 第四次真人验收与 V11 修复（2026-09-20）

第四次验收按 LCD 引导完成 90 秒正式采集。链路完成 284 帧、283 次推理，错误、
替换和过期结果均为 0；正式阶段 269 帧全部通过关键点置信度门限。系统只计数 2 次，
其中有效 1 次；状态帧为 `22,3,243,0`，说明状态机在一次大幅角度变化后长期停留
在最低点。实际膝角范围为 50.4°–162.7°，固定的初始站姿回程门限不适合该漂移。

V11 在动作开始后改用“本次最低角度到初始基线的恢复比例”：恢复 40% 进入起身态，
恢复 70% 完成动作，然后以恢复姿态重新建立下一次基线。新增“166° 跌至 50° 后
连续完成 3 次动作”的回归序列，连同全部既有测试连续 5 轮通过。

- V11 BIN SHA256：`210bbc49fa4eda5d792e0a6e7be9abb1c6da8c2829eeb6abb3820f81c421fe2e`
- V11 ELF SHA256：`8542ddfe172c21e91679ad3150d6fcb01920f50962e2f379ef8a7f0ccae94aa4`
- V11 固件包：`.secrets/tinypose-live-integration-v11-20260920`
- 第四次验收日志：`hardware-logs/tinypose-live-human-acceptance-v10-fourth-20260920.log`
- V11 烧录后实机日志：`hardware-logs/tinypose-v11-postflash-smoke-20260920.log`

V11 已烧录，摄像头、LCD、推理和命令收尾再次通过实机检查。新的计数恢复逻辑仍需
下一轮受控真人动作验证，不能用自动回归代替。
