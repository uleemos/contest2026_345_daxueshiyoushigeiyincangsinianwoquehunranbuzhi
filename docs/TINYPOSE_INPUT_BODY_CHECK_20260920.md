# TinyPose 输入方向与 BODY_CHECK 证据（2026-09-20）

## 0. 当前有效结论：摄像头固定顺时针 90° 后的 V18 链路

本节覆盖下文 V13–V17 的旧安装方向结论。旧证据保留用于追踪迁移原因。

摄像头现已按产品安装方式物理顺时针旋转 90° 并固定。实机导出的最终
`96×96` 图像证明：软件已有的第一处 CCW90 已把人物恢复为头上脚下的竖直
方向。因此 V18 删除了 RGB192→RGB96 阶段原有的第二处 CW90；该阶段现在只做
`2×2` area resize 和 INT8 quantization。

当前真实推理链为：

```text
SC2336 1280×720 RAW10（物理 CW90 安装）
  -> BGGR demosaic + portrait_sample CCW90
  -> RGB888 192×192 letterbox（有效人物区域约 108×192，左右黑边）
  -> 2×2 area resize（不旋转）
  -> RGB/INT8 96×96（有效人物区域约 54×96，左右黑边）
  -> TinyPose
```

在软件坐标中，`S -> M` 由
`x_p = y_s, y_p = 1279 - x_s`、portrait fit/letterbox 和 2×2 resize 组成。
近似归一化关系为：

```text
x_m ≈ 0.21875 + 0.5625 * y_s / 720
y_m ≈ 1 - x_s / 1280
```

`preview_model` 是独立的调试显示支路。它把 M 额外 CCW90 后写入横屏 framebuffer：
`(x_m, y_m) -> (y_m, 95-x_m)`，再按整数倍居中显示。这个 `M -> D` 变换不参与
推理，也没有修改全局 LCD rotation 或文字方向。

V18 实机人物证据中，原始 M 图像的人物正面站立、头在上、双脚在下且全身入框；
两侧为预期黑边。方向结论来自导出的模型输入本身，不依赖 skeleton overlay 或
LCD 物理方向：

- PPM：`.secrets/tinypose-portrait-input-v18-20260920/tiny-input96-person-upright.ppm`
- PPM SHA256：`b37fdb71b692d9fcf9052eca95cb22fe210adbcd2514faabc441147a8dfcbb78`
- PNG SHA256：`7323f79289ccf70e8fc6a142440e29f79800f1d0e66aee63f0a3dbc37757650f`
- 单帧串口日志：`hardware-logs/tinypose-portrait-v18-person-input96-20260920.log`
- 12 秒 LCD 预览日志：`hardware-logs/tinypose-portrait-v18-person-direction-20260920.log`
- LCD 预览结果：56 帧，active 56，guard PASS，正常返回 NSH。

现有关键点模型按旧的横向 M 输入训练，不能用它判断 V18 方向下的关键点质量或
BODY_CHECK。下一步必须生成与当前竖直 M 输入一致的训练/标注数据并重新训练；
之后才能采集 5–10 帧关键点并重新校准距离阈值。

## 1. 真实调用链

推理支路按实际 buffer 顺序为：

1. SC2336 输出 `1280×720` packed RAW10 BGGR，CSI DMA 将一帧写入 PSRAM。
2. `sc2336_raw10_bggr_letterbox()` 读取 RAW10，在 sensor 坐标中完成 BGGR
   demosaic；`portrait_sample()` 对每个目标像素使用
   `sensor_x = 1279 - view_y, sensor_y = view_x`，生成 CCW90 的
   `720×1280` view。
3. 该 view 以 fit/letterbox 方式写入 `model_rgb`：RGB888 `192×192×3`，
   内容为 `108×192`，左右各约 42 像素黑边。推理支路没有 center crop。
4. `vf_pose_worker_publish()` 将完整 RGB192 复制到异步 worker 的固定 mailbox。
5. `tiny_preview_infer()` 调用 `velafit_tiny_pose_infer_rgb192()`。
6. `velafit_tiny_pose_preprocess_rgb192()` 对 RGB192 做 CW90；每个输出像素
   对旋转后的 `2×2` 区域求均值，得到 `96×96×3` 的浮点像素值。
7. 每个均值执行 `(pixel / 127.5 - 1) / scale + zero_point`，使用
   `nearbyintf()` 和 INT8 饱和，直接写入 TFLM input tensor。
8. `MicroInterpreter::Invoke()` 执行 TinyPose。

LCD preview 是独立支路：同一 RAW frame 直接进入
`sc2336_raw10_bggr_preview_rgb565()`。它也通过 `portrait_sample()` 做 CCW90，
随后在 `720×1280` view 中做 center-cover crop（约 `720×421`）并缩放到
framebuffer `1024×600 RGB565`。preview buffer 不会送入 TinyPose，TinyPose 的
第二次 CW90 也不会作用于 LCD framebuffer。

因此：推理支路中的 CCW90 和 CW90 确实先后作用于同一图像内容，中间的显式
buffer 是 RGB192；preview 只包含第一处 CCW90，属于另一条 branch。

## 2. S、M、D 坐标空间

### S：sensor/camera coordinates

- `x_s ∈ [0,1280)`，`y_s ∈ [0,720)`。
- 原点为 RAW frame 左上角。

### M：TinyPose model coordinates

- `x_m,y_m ∈ [0,1]`，对应 INT8 `[1,96,96,3]`。
- S→portrait：`x_p = y_s`，`y_p = 1279 - x_s`（CCW90）。
- portrait→RGB192：宽度从 720 fit 到 108 并居中，
  `x_c ≈ 42 + x_p×108/720`；高度 1280→192，`y_c ≈ y_p×192/1280`。
- RGB192→M：`x_m ≈ 1-y_c/192`，`y_m ≈ x_c/192`（CW90 + 2×2 resize）。

合并后，方向近似为：

```text
x_m ≈ x_s / 1280
y_m ≈ 0.21875 + 0.5625 × y_s / 720
```

两次 90° 旋转在方向上抵消。由于中间先执行 portrait letterbox，最终模型内容
等价于把原始 landscape sensor 画面缩放为约 `96×54`，在 96×96 输入上下留边。

### D：LCD framebuffer coordinates

- `x_d ∈ [0,1024)`，`y_d ∈ [0,600)`，软件坐标始终是横屏。
- S→D 为 CCW90 后的 `720×1280` view，执行 center-cover crop，再缩放到
  `1024×600`。
- 产品屏幕的物理旋转发生在 framebuffer 之外，所以文字看起来旋转不会改变 M。

当前 `velafit_render_sc2336_skeleton()` 的 M→D 实现把模型坐标直接当作第二次
CW90 之前的 RGB192 坐标，尚未先做逆变换
`x_c = y_m, y_c = 1-x_m`。因此 TinyPose skeleton overlay 目前不能作为输入方向
的唯一证据。本任务没有修改该显示变换。

## 3. 最终 RGB96 输入证据

新增 `velafit_ai tiny_input_dump`：在板端执行实际 capture、CCW90 letterbox 和
CW90/2×2 area resize，导出量化前 RGB96 PPM。可视图的四像素均值取最近整数；
实际 INT8 路径仍保留浮点四像素均值，未改变 preprocessing。

- 冻结真实摄像头 fixture：INT8 27,648 字节逐字节一致；RGB96 debug 图逐像素
  等于训练正立图的 2×2 area resize。
- ESP32-P4 实机导出：门框和墙体为正立方向，上下存在预期 letterbox 黑边。
- 实机 PPM SHA256：
  `cc4fd8ff45e3811887b8b222e87427a258d7dd1692f360694a25bfcd54d69c9b`。
- 私有证据：`.secrets/tinypose-body-check-v13-20260920/tiny-input96-board.ppm`。
- 串口日志：`hardware-logs/tinypose-input96-v13-20260920.log`。

该帧没有人物，因此它证明实际图像轴向和 letterbox，人物关键点方向仍需真人站立
帧确认。

## 4. 独立 BODY_CHECK

实现位于 `app/velafit_ai/algo/velafit_body_check.{h,c}`，没有接入 Wake Word、
MiMo、TTS 或正式训练状态机。默认配置：

- 必需的肩、髋、膝、踝 8 点 confidence 均不低于 0.30；
- 主轴满足肩→髋→膝→踝的 y 顺序，且 `abs(body_dy)` 至少为
  `abs(body_dx)` 的 1.5 倍；
- 左右同级关节的通用 y 差不超过 0.15，膝部使用更严格的 0.10；
- frame margin 为 0.04；
- shoulder-center 到 ankle-center 距离小于 0.375 为 `TOO_FAR`，大于
  0.90 为 `TOO_CLOSE`；
- 连续 5 帧全部满足后才返回 `BODY_CHECK_READY`。

所有阈值均位于 `velafit_body_check_config_t`，不是散落在 render 中的常量。

新增 `sc2336_probe preview_body [sec]`。它只执行 camera、TinyPose 和
BODY_CHECK，在 LCD 映射为 `STEP INTO FRAME`、`MOVE BACK`、`MOVE CLOSER`、
`HOLD STILL`、`STAND READY`。命令结束并关闭 CSI/DMA 后，输出前 10 个连续
结果的 8 点坐标、置信度、四个中心点、body_dx/body_dy 和 orientation。

### 最终模型输入实时 LCD 预览

新增 `sc2336_probe preview_model [sec] [scale]`。V18 以后该命令每帧执行真实的
RAW10→CCW90 letterbox RGB192→无旋转 2×2 area resize，显示量化前最终 RGB96；
它不使用普通 preview branch，也不执行推理。LCD 显示支路额外将 RGB96 相对
文字坐标逆时针旋转 90°，以整数最近邻倍数居中显示，保持 1:1 比例，不拉伸、
不 cover crop、不填满 framebuffer。`scale` 支持 1–6，默认 4：

```text
sc2336_probe preview_model 60 4   # 384×384 居中，便于肉眼检查
sc2336_probe preview_model 60 1   # 原始 96×96 像素大小
```

V17 在 ESP32-P4 上完成 60 秒 4× 实时预览；随后 1× 完整测试显示 24 帧并正常
返回 NSH。首帧和最终汇总记录于
`hardware-logs/tinypose-model-preview-v17-live-60s-20260920.log` 和
`hardware-logs/tinypose-model-preview-v17-live-5s-scale1-20260920.log`。
预览 blit 的 CCW90、居中、黑边和整数缩放主机测试连续 5 次通过。

## 5. 当前实机结论与待验收项

空场景首次实测中，模型对背景产生 8 个超过 0.30 的伪关键点，说明 confidence
不能单独证明人物存在。伪点主轴被判为 `UPRIGHT`，body height 约 0.323–0.331，
但左右膝 y 差约 0.13。BODY_CHECK 增加可配置的双侧一致性 Gate 后，V14 实机
空场景 13 帧中 12 帧为 `NOT_FULLY_VISIBLE`、1 帧为 `STABILIZING`，0 帧
`READY`；单帧稳定计数为 1/5，不会进入 READY。背景偶发单帧通过几何 Gate
仍需结合真人数据继续校准。

V14 构建和实机证据：

- BIN SHA256：`7822ba8b090f7802ed24e62d8771e3b99c03bc09667ad62eb768104d3cd469a2`
- ELF SHA256：`5769afbcff2727af76244278c9317328c7e76b2cf9abd1e33c3616c9742ec2e1`
- 构建日志：`hardware-logs/tinypose-body-check-v14-build-20260920.log`
- 空场景日志：`hardware-logs/body-check-v14-empty-scene-20260920.log`
- 私有固件快照：`.secrets/tinypose-body-check-v14-20260920`

主机自动测试执行 5 次，BODY_CHECK 的 confidence、方向、边界、双侧一致性、
距离和连续 5 帧稳定性用例全部通过。冻结 preprocessing fixture 的 INT8 字节
和 RGB96 debug 像素比较也通过。V14 已烧录到真实 ESP32-P4。

真人 A–D、F 场景尚未执行。需要先采集正立人物的至少 10 个连续模型帧，确认
`shoulder_y < hip_y < knee_y < ankle_y` 和 `abs(body_dy) > abs(body_dx)`，然后
用实测 body height 调整 TOO_CLOSE/TOO_FAR 阈值。完成这些测试前，BODY_CHECK
不能接入正式 READY→COUNTDOWN 状态机。

## 6. Portrait model v1 A/F 实机验收 — 2026-09-20

新的 portrait TinyPose v1 已烧录至 ESP32-P4 rev3.2。正常距离自然站立 20 秒
产生 58 个有效姿态结果：最初 4 帧为 `STABILIZING`，之后 54 帧全部为
`BODY_CHECK_READY`；最终方向为 `UPRIGHT`。低置信度、边界、过近、过远和
方向错误计数均为 0，因此 A（正常全身入镜）和 F（连续稳定后 READY）通过。

前 10 帧的四级中心持续满足 shoulder < hip < knee < ankle；示例 body_dx 为
0.008–0.015，body_dy 为 0.580–0.596，竖直主轴证据充分。8 个关键点的置信度
持续高于 0.30。推理 Invoke 均值为 10 ms，异步预览链路 capture-to-result
均值为 634 ms、最大 650 ms；后者包括摄像头、渲染和异步调度，不属于
Invoke-only 延迟门限。

证据保存在
`hardware-logs/tinypose-portrait-model-v1-body-check-af-20260920.log` 和
`hardware-logs/tinypose-portrait-model-v1-body-check-af-20260920.json`。B、C、D、
E 场景仍需分别进行真人实机验收；通过前仍不接正式 READY→COUNTDOWN。
