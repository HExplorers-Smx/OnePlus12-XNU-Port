# S10 — UEFI 启动信息、日志格式与 GPUCC/SMMU 只读观察边界

## 做到了什么（本地工程，不是实机启动）

S10 从 S9 主机模拟层增加三种可以单独验收的实际代码：

1. **`uefi/WaffleS10Probe/`**：一个 EDK2 UEFI_APPLICATION `.inf` + C 入口。
   启动时只输出 UEFI 控制台说明、枚举 `EFI_SERIAL_IO_PROTOCOL` 句柄数量、
   显示枚举错误/完成提示；不调用任何 GPUCC/SMMU MMIO、时钟投票、分区操作、
   EFI 变量写入或 XNU 跳转。**没有完成 EDK2 平台 DSC 接入或真机运行。**
2. **`boot/waffle_s10_log.{h,c}`**：无堆内存的版本化 WFLG-v1 诊断帧编码/解码、
   长度界限、CRC32、保留位和类型校验。该纯 C 模块在主机和 Darwin ARM64
   Mach-O 目标编译阶段均通过。**日志帧当前尚未接入手机 USB 控制器。**
3. **`boot/waffle_s10_observer.{h,c}`**：XNU 运行时观察结果的*接收契约*；
   对当前 boot epoch、独立 power/SMMU owner、实测时钟供电、映射、读数完成、
   单调序列号做 fail-closed 检查。不含真实 register read，也不取得任何租约。
   “观察者模拟通过”不代表已读取 SM8650 寄存器。
4. **`tools/s10_decode_log.py`**：离线读取二进制帧，严格拒绝截断、坏 CRC、
   无效版本/标记；支持以 `--recover-prefix` 跳过文件前的无关字节，
   输出 JSON；无设备/网络访问，也不会把“帧有效”当成“USB/XNU 运行成功”。

## WFLG v1 数据格式

| 字节偏移 | 字段 | 值 |
|---|---|---|
| 0–3 | Magic | ASCII `WFLG` |
| 4 | 版本 | `1` |
| 5 | 事件类型 | 1 boot、2 observation、3 error、4 handoff |
| 6–7 | flags | 必须为 0 |
| 8–11 | sequence | Little-endian uint32 |
| 12–13 | payload_size | Little-endian uint16，最大 512 字节 |
| 14–15 | reserved | 必须为 0 |
| 16–19 | CRC32 | ISO-HDLC / IEEE，覆盖头部前 16 字节 + payload |
| 20… | payload | 原样数据，工具只尝试 UTF-8 展示 |

目前的 UEFI 程序仍使用标准 UEFI Console，而不是 WFLG 传输。
这两个模块已按字节级协议建立独立可测试基础，后续才能连接实际硬件日志后端。

## EDK2 接入要求

1. 使用独立、能够针对 SM8650 `waffle` 启动的、经验证的 EDK2 平台 DSC/FDF，
   通过 `MdePkg/MdePkg.dec` 和原始设备启动环境构建
   `uefi/WaffleS10Probe/WaffleS10Probe.inf`。
2. 需验证 UEFI 固件 Boot Services 及 SerialIo 是否有可用的控制器驱动；
   即使枚举到 SerialIo，也可能只是 UART，不代表 USB-C CDC/ACM 工作。
3. USB 日志后端必须单独具备 DWC3/USB PHY 初始化、端点配置、主机枚举、
   传输错误处理和重置恢复。此仓库 **S10 不含这些硬件后端**。
4. 若将来需要 boot.img 形式的试验性调试镜像，必须先匹配 Android boot
   header、活动分区/ABI、fastboot 能力、防回滚策略及可恢复条件。
   **当前 S10 不提供可刷写或建议在手机运行的引导镜像**。

## GPUCC / SMMU 的安全边界

已核实的 FDT 物理窗口是 GPU `0x03d00000`、GMU `0x03d68000`、
GPU SMMU `0x03da0000`。这些只是地址，**既不证明 clock/gdsc 已开启，也不授予 MMIO 访问权**。

S10 默认只输出配置级信息。`wa_s10_observer_accept` 需要未来一个持有真正
XNU 运行时独占租约的提供者提供观察快照；同一次 boot epoch、真实访问完成、
供电与读回以及独立 SMMU 租约全部一致才会接受。缺失/模糊则拒绝或者隔离。
这个模块当前无任何触发 MMIO 的方法，更不可能改变 GPU、SMMU 的状态。

绝对不要在仍运行 Android/KernelSU 的环境中使用 `/dev/mem` 或随机
写入 GPUCC、SMMU、GMU 寄存器来模拟 XNU 观察，否则可能破坏运行中的
Android 驱动、DMA 状态及系统稳定性。

## 重现命令（普通电脑/Linux 开发环境，无需手机）

```sh
# 目前只有源码和离线测试；不依赖 GitHub Actions
make s10-test
ONEPLUS12_PRIVATE_EVIDENCE=/path/outside/repo/OnePlus12-XNU-Evidence.zip \
  WAFFLE_SANITIZE=1 python3 -m unittest discover -s tests -v
python3 tools/validate_board.py boards/waffle.toml
python3 tools/verify_source_manifest.py

# 当真正采集到 WFLG 原始二进制之后：
python3 tools/s10_decode_log.py /path/to/bootlog.wflg --out /tmp/bootlog.json
```

## 后续 S11 先做什么

- 把已经完成的 UEFI .inf 连接真实的 *waffle* 平台 DSC，并做完整 EDK2 构建。
- 先建立不会永久写入分区且有回退路径的串口/USB 观察方案；
  特别先区分 SerialIo/UART 与 USB-C 日志，得到真实主机抓包或枚举记录。
- 真正得到 XNU 会话之后，才实现真实 GPUCC/AOP/SMMU Owner 并接入 S9/S10 只读观察。
- 之后才能继续验证 GMU 固件、HFI SCRATCH 回答、GPU 命令及 Fence；
  Metal 的流畅度属于更后面的验收阶段。

**验收状态**：UEFI 源码建立、WFLG 格式和接收器、S10 read-only observation contract
通过本地测试；UEFI 二进制/手机 USB-C/XNU 启动/GPU 硬件加速均未完成。
