# 构建、诊断固件包与恢复

这些工具服务当前ESP32-P4 V1.8工作区，不意味着全部比赛功能已验收。
不提交、不推送；模型、真人素材、固件和配置留在Git忽略的`.secrets/`。

## 构建（工作区根目录执行）

```sh
bash contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/tools/build_delivery.sh 16
```

脚本校验三份持久输入的SHA256：MoveNet、RGB192固定素材、okay_nabu控制模型，
然后调用现有build.sh。没有使用临时目录中的模型/图片；工具运行时的可丢弃编译目录
不是构建依赖。当前依赖已在工作区准备，缺少模型时脚本明确失败，不下载未知文件替代。

若测试**未通过唤醒质量验收**的专用词开发原型，显式设置
`VELAFIT_MWW_DEV_PATH`为私有`development.tflite`绝对路径后构建。
Make已强制每次重新装配模型/固定素材`.S`，避免`.incbin`文件或环境路径变化没有
触发重编译；已在后续构建日志确认重新装配。仍需核对最终固件/model hash。
CMake路径已补齐，但本次实际执行的是Make流程，不将CMake标记为验证通过。

当前P4短PIE探针使用隔离官方工具链，仅该汇编文件使用GCC14.2，其余仍用GCC13.4。
先下载官方ESP-IDF v5.5.2 `tools/tools.json`列出的
`riscv32-esp-elf-14.2.0_20251107-x86_64-linux-gnu.tar.xz`到`.secrets/`，
再运行`tools/hardware/verify_pie_toolchain.py <压缩包> .secrets/pie-toolchain-14.2.0`。
工具先验证锁定SHA256再安全解包，拒绝覆盖目录。build_delivery.sh传入对应编译器路径；
不修改全局PATH。不具备该依赖时明确失败，不悄悄使用不支持PIE的汇编器。

## 私有固件包

`tools/hardware/firmware_bundle.py`提供三个操作：

- `snapshot <.secrets下的新目录> --evidence <已有日志>`：保存当前nuttx.bin/ELF/config，
  拒绝覆盖目录，写入哈希及证据引用。快照不能替代功能验收。
- `verify <包目录>`：检查三份产物哈希、Espressif BIN/ELF标识、P4平台及0x2000布局。
- `flash <包目录> --python <Python解释器> --port COM3`：默认仅dry-run。
  加`--execute`才实际调用esptool；校验失败不会使用其他镜像作为替代。

WSL侧调用Windows Python可使用：
`/mnt/c/Users/uleem/AppData/Local/Programs/Python/Python312/python.exe`。
工具通过wslpath转换BIN路径；默认不访问串口，不整片擦除Flash，不格式化SD。
已有授权下才执行实际烧录。ROM esptool下载复位及写入0x2000已反复实测。

## 恢复顺序

1. 停止占用COM3的主机串口程序；不要同时启动两个烧录/诊断客户端。
2. 选择有相应板端日志的包并verify。原始候选名称不代表完整系统通过。
3. 使用flash --execute，等待写入哈希验证及硬复位完成。
4. 运行只读状态、对应控制模型及SD ACK回放回归。旧包SD/C6仍互斥；
   outbox-watchdog及更新候选启用了共享轮询调度，不能混用两种固件的验收结论。
5. 根据包引用的测试范围恢复工作，不能把另一个版本的日志归到当前固件。

当前已保留：

- `.secrets/resume-development-baseline-20260915/`：控制/开发模型及SD ACK实测。
- `.secrets/mww-chain-candidate-20260915/`：初版整链输出一致但速度未实时。
- `.secrets/mww-q20-candidate-20260915/`：Q20整链260–290ms/秒音频，输出/内存/SD回归。
- `.secrets/preview-worker-candidate-20260915/`：预览后台推理候选，详见对应板测日志。
- `.secrets/outbox-watchdog-candidate-20260915/`：加速前云端/持久补传恢复基线；
  BIN `a49a81ddb746b8e92b6ef50ab952fe93eab405aaa063eb10f8c34c71e5a1ffb3`。
- `.secrets/hwlp-context-candidate-20260915/`：短指令/双任务HWLP、SD只读、真实MiMo
  回归通过；尚无PIE上下文或优化推理。详见`hardware-logs/p4-extension-ledger-20260915.md`。

卡上`/sdcard/velafit-outbox`由专用后台任务持有挂载，保留已ACK的01/02会话；
不可删除测试目录来重跑。再次离线→重启→补传验收使用新的会话ID。
后台凭证仅驻留RAM，重启后需既有私密配置通过主机程序重新提供；停止任务清除凭证。
当前32条总记录容量（包含ACK）还没有回收政策，不能宣称长期无限补传存储。

嵌套TFLM已有五份未提交的ESP-NN内核修改已原样保存为
`tools/patches/tflm-esp-nn-existing.patch`，基础commit及hash见扩展台账。
该补丁仍待集成验证，不应仅因存在而启用。普通构建目前未启用CONFIG_TFLITEMICRO_ESP_NN。

## 主机回归与训练

使用Python3执行`tools/hardware/delivery_regression.py --repeat 10`、
`test_outbox.py`、`test_firmware_bundle.py`、`test_mww_resample.py`。
这些是主机证据；慢推理回归使用明确标注的FAKE callback。

训练环境采用Python3.12.11，依赖见`tools/hardware/mww-training-requirements.lock`。
官方microWakeWord源码固定commit见恢复工作日志，源码Apache-2.0；不把源码许可证
当成所有外部模型/语料的通用授权。已有用户录音不提交，不上传。
`train_mww_development.py --output <.secrets下的新目录>`只生成开发实验：原录音先划分，
最多200步，不用留出集调阈值，不覆盖既有模型。当前需要更多负语音、边界标注和独立
现场数据才能评估专用词实际效果；不能报告稳定误唤醒率。
