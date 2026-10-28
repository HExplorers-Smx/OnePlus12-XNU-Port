# S6｜Gen7 HFI 队列与内存应答的主机侧闭环（2026-10-28）

## 结果与证据等级

本阶段在 S5 的 PJD110/ColorOS `PJD110_16.0.10.501(CN01)` 实机
GPU/GMU/SMMU 资源快照基础上，实现了可本地编译的 **Gen7 HWSCHED
17 队列共享内存传输**和 **F2H_MSG_MEM_ALLOC → H2F ACK** 协调器。

**仅通过主机内存模型验证**，不是 GMU/XNU 真机通信成功、不是 GPU
提交完成、不是 Metal 加速，更不是可刷机镜像。当前没有 XNU 的 AOP
电源服务、运行时 SMMU 寄存器读回或真实 GMU 驱动。

| 组件 | 代码 | 结果 |
|---|---|---|
| Gen7 HFI 17 队列页表及缓冲区初始化 | `gpu/waffle_hfi_ring.{h,c}` | 本地可编译，可拒绝错位地址和短缓冲区 |
| CPU → GMU TX 队列（q0） | 同上 | 4-word 对齐、容量检查、队列回绕、缓存同步回调 |
| GMU → CPU RX 队列（q1 / q2） | 同上 | 错误消息不推进消费者游标；同步失败报错 |
| F2H `SCRATCH` 分配应答 | `gpu/waffle_hfi_alloc.{h,c}`（S4） | 复用已拥有映射、失败保留 DMA 资源 |
| runtime 协调器 | `gpu/waffle_hfi_runtime.{h,c}` | 要求同一次启动的电源/SMMU 令牌、GMU transport attestation，缺任何一项拒绝 |
| 本地故障注入 | `gpu/host_s6_ring_runtime_test.c` | 令牌变化、租约失效、缓存发布失败、队列损坏、内存提供者失败时隔离 |

### 精确依赖

参考源的 GitHub 文件路径（仓库内容与许可证请遵守各自上游版本）：

- `tycykp/UniXNU-OnePlus-Ace5/ios16/gpu/giulia_gpu_hfi.h`
  — 17 队列布局、队列头、4-word padding、q0/q1/q2 的协议方向；
- `tycykp/UniXNU-OnePlus-Ace5/ios16/gpu/giulia_gpu_hfi_alloc.h`
  — `F2H_MSG_MEM_ALLOC` 版本/flags/kind、`SCRATCH` 内存应答；
- `tycykp/UniXNU-OnePlus-Ace5/docs/GPU_STACK_FRAMEWORK.md`
  — 电源、SMMU、GMU 与 HFI 的依赖顺序和故障隔离；
- 本项目 `boards/gpu/waffle_a750_verified_16_501.json`
  — 从机主**当前运行的 FDT 与固件哈希**核实的 PJD110 资源信息。

上游 Ace 5 的内存布局、HFI 地址采用作为**协议参考**，并未被当成
OnePlus 12 的设备侧可用映射证明。`0x60010000` 是参考 HFI 的 **GMU IOVA**，
不是 CPU 物理地址，不能直接用于 `mmap` 或 CPU MMIO 访问。

## 主要安全约束

1. 只在硬件所有权提供者回报**同次启动**的 `power_token`、
   `smmu_token`、`GMU HFI transport_token` 时允许绑定 RX/TX。
   令牌只是 API 约定；真正取得硬件所有权的提供者还**未实现**。
2. 缓存回调必须在真实平台上实现 CPU↔GMU 一致性、适当的屏障及映射属性。
   本地测试的回调只模拟完成，不验证实际 DMA 一致性。
3. 接收到类型不支持的设备消息、GMU ACK 写入失败或映射提供者状态
   不确定时停止处理。已发布的资源不能在不证明 DMA 停止时释放。
4. 有关 secure SID2、firmware ZAP 安全认证、GPU SMMU context bank、
   AOP 时钟与电源投票值，目前没有真机运行证据；不能使用参考地址猜测写入。
5. 本项目只处理队列中 **F2H_MSG_MEM_ALLOC**，
   尚不支持完整的 START/FEATURE/PERF/BW/AQE/NOP/HWSCHED 协议集合。
   单次软件 ACK 入列**不等于** GMU 已收到或接受。

## 本地复现

```sh
# 需要 C11 编译器；不需要 SDK、不需要手机、不需要专有固件
make s6-test

# 加入用户提供的私人 ZIP 做 S5 硬件版本交叉校验
ONEPLUS12_PRIVATE_EVIDENCE=/safe/private/OnePlus12-XNU-Evidence.zip \
  WAFFLE_SANITIZE=1 python3 -m unittest discover -s tests -v
```

不要把私人 ZIP 放入 git 工作树；本地回归不会自动上传任何数据。

## 接下来 S7 真正需要实现的硬件后端

- 完成 **waffle XNU runtime** 电源/时钟所有权服务和超时读回，
  并确认不会与仍活跃的固件、Gunyah、Android 资源所有者冲突。
- 专属 GPU SMMU 的 4 KiB 表、context bank 和 SID5 租约，
  页表发布/TLBI、translation fault 处理及真实硬件撤销证明。
- GMU 正版固件的安全认证、TCM/普通 RAM 装载、HFI 队列实际 ACK 和
  首个 `SCRATCH` 分配（实机 USB/串口日志）；只有这一步真机证据成立，
  才能进入 GPU 指令与真实 fence 实验。
- 修复 CP/secure init 及 HWSCHED 完成机制，
  然后继续 IOSurface/scanout/Metal。没有真实 fence 就不宣称硬件加速。

**当前没有可启动的 iOS 镜像，不提供任何分区写入命令。**
