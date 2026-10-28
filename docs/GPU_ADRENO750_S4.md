# S4：一加 12（waffle）Gen7 HFI 分配与 GPU 电源／SMMU 租约

版本：2026-10-28。**此包不含 UEFI/XNU 可启动镜像，也不含可以访问 GPU MMIO 的驱动后端。**

本阶段针对一个具体、已有同平台参考证据的启动阻塞点推进：在另一台 SM8650 设备（一加 Ace 5）的 Gen7 GMU 启动实验中，固件在 `START` 之后提出首次 `F2H_MSG_MEM_ALLOC` 内存请求。该请求需要返回由宿主真正拥有、同时能被 GPU 与 GMU 访问的 SCRATCH 内存；此前相关实验无法完成对应内存提供者的闭环。**不能假定在 PJD110 上已复现该行为**，更不能把测试中的地址写到一加 12 的 SMMU。

## 1. 已实现：Gen7 HFI F2H 内存请求处理

- 源码：`gpu/waffle_hfi_alloc.[ch]`。
- 支持 Gen7 HWSCHED `F2H_MSG_MEM_ALLOC`（消息 id 20）版本 0/非零描述符格式（10/11 个 dword）。
- 精确核对数据包 id、type、dword 长度、对齐编码、已知 flags、kind 和请求大小上限。
- 未授权的 secure、IPC_CORE、memstore、硬件 fence 种类立即拒绝，不能拿普通内存伪装。
- 无 GFX 访问权限的普通 SYSRAM 请求按该 Gen7 原厂协议候选规则补入 GMU `ACC/PRIV/WRITE`，但不会猜出虚拟地址。
- 只有**独立 provider 已拥有并映射**的 backing 才能回填 GPU、GMU IOVA。校验 4 KiB 粒度、GPU/GMU 访问方向、GMU IOVA 32 位协议长度、容量和 VA 对齐。
- 仅在 `publish()` 明确报告初始化、回读和缓存发布成功后构建 ACK。ACK 回填实际长度，`host_handle` 使用请求的 `gmu_handle`，并保留 kind、flags、align。
- 相同 `(kind, gmu_handle)` 且关键属性相同的请求复用已拥有的块；冲突请求拒绝。
- 不明确的保留、发布或释放结果进入 quarantine。**已发布内存绝不由这段代码回收**；设备可能正在 DMA。
- 测试中的标识：请求头 `0x00700b14`，`kind=2`，`flags=0x33f`，`gmu_handle=2`，`size=4096`，全部出自同平台的上游案例，**不是一加 12 的实测包**。

本代码刻意没有真正的 `reserve/publish/release` XNU 平台提供者，没有 GMU 门铃、消息队列或 SMMU 页表写入，因而目前属于**可复用的协议处理核心，尚未集成真实 GPU**。

## 2. 已实现：电源／SMMU 有限租约事务核心

- 源码：`gpu/waffle_gpu_lease.[ch]`。
- Power claim → Power readback → SMMU claim → SMMU readback 的顺序必须全部成立；读回失败与不确定结果会 quarantine。
- SMMU 配置必须显式提供独立 GPU SMMU、GMU SID、secure SID、GPU SID、context bank、ASID、4 KiB 表粒度以及**已拥有的实际页表物理内存**。
- SID 不是 context bank。拒绝把 secure SID 或 GPU SID 代入 GMU SID；拒绝 TTBR 越界和 16 KiB 冒充 4 KiB 页表。
- SMMU 确定拒绝且尚未发生写入，可尝试释放 power；无法确认释放成功则保留并 quarantine。
- 正常 shutdown 必须先有独立的真实 DMA 停止证明，然后先释放 SMMU、再释放 power。回收不确定时禁止继续释放下一项。
- 只有可信平台确认了**真正硬件冷复位**才能清空 quarantine；普通用户态 API 或热重启没有这样的证明。

租约提供者还没有实现在 XNU、UEFI 或 Android 内核中，**没有执行 GPU 时钟投票、RPMh/AOP 通讯、SMMU TLBI、固件认证或 GMU 上电**。测试中的物理地址与 token 是合成样本，绝对不能当作 waffle 的真实 MMIO、SID 或 context bank。

## 3. 固件和来源绑定

参考实现是 `tycykp/UniXNU-OnePlus-Ace5` 的 Gen7 HFI 协议和实际同平台问题记录，准确路径与 Git blob 锁定在 `boards/gpu/hfi_source_pins_s4.json`。协议行为借鉴自公开 BSD-2-Clause-Patent 实现，本项目的 `waffle_*` 代码为独立适配层。

已经确认公开的 `PJD110_16.0.10.501(CN01)` Release 含 `dtbo.zip`、`vendor_boot.zip` 等资产，**但本地二进制下载仍不可用**。`boards/gpu/stock_release_16_501.json` 的 SHA-256 为 `null`：没有得到二进制验证，不能据此宣布 GPU MMIO、SID、GMU 固件和原厂电源时序已针对 waffle 固定。

## 4. 验证

在仓库根目录运行：

```bash
python3 -m unittest discover -s tests -v
# 可选：需要主机支持 ASan/UBSan
echo '在支持 ASan/UBSan 的 Linux 主机执行：'
WAFFLE_SANITIZE=1 python3 -m unittest discover -s tests -p 'test_s4_native.py' -v
```

主机测试实现了一个**只存在于测试中的假 backing provider**，返回的 IOVA/Token 为合成值；它测量协议拒绝、复用、故障回滚逻辑，不测 GPU 性能或 GMU 执行。

## 5. 下一道真正的硬件门槛（S5）

1. **真实匹配 DTB**：使用 root 一加 12 当前运行系统导出的只读 FDT，对照同版本 DTBO、vendor_boot、原厂 GPU 驱动，固定 GPIO/时钟/RPMh/GPUSMMU IRQ，以及 GPU/GMU SID、bank 所有权。禁止把 Ace5 的地址移用。
2. **匹配的 GPU 固件**：获取原厂对应版本的 `gmu_gen70900.bin`、SQE/AQE、ZAP，并固定每个 SHA-256、认证方式和 GMU 地址。固件文件绝不入仓库。
3. **XNU 真实 runtime provider**：实现 clock/AOP、GPU 独立 SMMU 页表/TLBI/ASID 独占、GPU/GMU 双地址 backed memory reserve/publish；接入能真实收发 HFI 的 17 队列 transport。不能用 UEFI 的临时状态冒充 XNU 所有权。
4. **证明第一个 SCRATCH 响应**：由真正的 GMU 发起请求，并取得它的 ACK/状态推进；发生不能确认的 DMA 或 CP/GMU 错误时停止自动重试。
5. **后续性能闭环**：完成 CP/IB 最小工作、真实 fence、scanout 与 IOSurface/Metal 发布，才能判定是否具有流畅的 iOS 图形界面。

无论当前测试数量多少，本阶段 GPU 驱动状态始终为 **未在 PJD110 实机启动**。
