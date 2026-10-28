# S7｜Adreno 750 SMMU 页表与 HFI SCRATCH 主机侧闭环

## 完成边界

本轮在用户一加 12 PJD110、ColorOS `PJD110_16.0.10.501(CN01)` 的
S5 硬件证据与 S6 HFI 队列协议基础上，新增两个可编译的 C11 核心：

- `gpu/waffle_smmu_pt.{h,c}` — ARMv8 LPAE stage-1、4 KiB 粒度、32 位
  IOVA / 36 位 PA 的 **宿主内存页表构建器**。调用者传入每个受其独占
  所有的 4 KiB 页表物理页地址与 CPU 指针；实现 L1/L2/L3 table 和
  page descriptor，权限、合法范围、只读禁止写、重叠拒绝、容量检查、
  独立页表遍历审计以及 seal 后不可修改。**不访问 SMMU 寄存器**。
- `gpu/waffle_s7_scratch.{h,c}` — 将 Gen7 `kind=2` 的 4 KiB SCRATCH
  分配接到相互独立的 GPU / GMU 页表根，并连接已有 S4 的 HFI
  `F2H_MSG_MEM_ALLOC` 应答器。物理页与两个 IOVA 必须由外部所有者
  明确提供，不会拿虚拟指针猜测物理地址。缓存发布需回调成功；
  任一不确定的 DMA 所有权/发布失败进入隔离。

主机集成测试模拟：Gen7 q1 接收 → S6 pump → S7 内存分配 → GPU 和 GMU
两个软件页表都能查到同一物理页 → q0 输出 HFI ACK。**这仅表示
宿主软件完成协议封装，绝不代表 GMU 收到消息或 GPU 能执行任务。**

## 位宽与地址含义

`wa_pt_map_page` 中的地址是 **GPU/GMU 设备 IOVA** 和**已验证的 RAM
物理地址**；不是 CPU 虚拟指针，也不是从 DT 里抄来的 MMIO 地址。
页表页由外部内存所有者提供 4 KiB 对齐的 CPU 映射与真实物理地址，
并保证生存期。页描述符采用 Normal Non-cacheable（预留 MAIR index 0）、
outer-shareable、AF、EL1 级权限及 XN 防执行位。**只有在未来运行时
提供者显式设置相容 TCR/MAIR/context，并校验硬件要求后，这些表才
可能被安装。** 本阶段不会触碰 CPU/XNU 自己的页表。

禁止直接用这些软件描述符设置 SMMU：真实硬件的 stream/context bank
租约、36 位 PA 适配、cache coherence、ACTLR、安全世界权限、fault
handler 和 TLBI 仍未实现。SID 5 是设备树中的 GMU stream ID，**不是**
context-bank 编号；secure SID 2 不可被普通 SCRATCH 复用。

## 完整运行时接入仍需要的实现

1. oneplus/waffle XNU 所有权提供者：原厂 AOP/RPMh/QMP/GDSC 的正确
   电源和时钟调用、读回、失败撤销，不能复用 UEFI 启动时的临时租约。
2. GPU 专用 SMMU 的上下文与 stream 独占授权、4 KiB 翻译 TCR/MAIR
   设置、表页 cache clean、TTBR 发布、读回和 TLBI；需要独立 fault
   处理和 DMA 停止确认。不能让本项目自己猜 context-bank 编号。
3. GMU 运行时的安全固件/ZAP 认证、TCM/普通内存装载、HFI 17 队列
   与中断，以及 START 后真实 `SCRATCH` 请求回读和 ACK。
4. GPU CP 初始化、真实 IB 命令和硬件 fence 完成，再尝试
   IOSurface、scanout、Metal 对接。

缺少上述提供者时，不得让启动状态越过 `HardwareReady=false`。
本仓库没有刷写程序或系统镜像，不修改 A/B 分区。

## 本地验证

```sh
make s7-test
WAFFLE_SANITIZE=1 python3 -m unittest discover -s tests -v
# 可选：私人 ZIP 路径，仅供本地测试，不会被放入压缩包
ONEPLUS12_PRIVATE_EVIDENCE=/outside/repo/OnePlus12-XNU-Evidence.zip \
  WAFFLE_SANITIZE=1 python3 -m unittest discover -s tests -v
```

S7 主机测试包含：缺页表、分支映射、4 KiB 对齐、36 位物理范围、
可执行属性被修改时的审计、重复映射、seal 禁写、双独立地址空间、
缓存发布失败、已占用 IOVA 无副作用拒绝、启动世代变化拒绝、
S6 队列与 S7 提供者协同。所有回调均使用主机模拟值。

## 下一步 S8

以真实 SM8650 平台后端为重点。先在 XNU 构建环境中实现最小化、
**默认禁用硬件写入**的专属 GPU SMMU provider 接口，加入只读
context/stream/PA/时钟状态验证及日志，确认本次启动硬件所有权后，
再考虑通过专用开发启动载荷做 GMU 首次 SCRATCH ACK 验证。
在这之前不分发任何可刷机镜像。
