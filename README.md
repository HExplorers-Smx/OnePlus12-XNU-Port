> **S13 新增（2026-10-31）：** 可执行的 AArch64 EFI 中暂存 Mach-O 段装载、BSS 清零与 xnu-12377 `boot_args` 草稿。
> UEFI AllocatePages / FreePages 已通过宿主模拟完整验证。**没有 XNU 跳转、没有真实 UEFI/XNU 启动，更不是刷机镜像。**
> 代码与测试见 [S13 实现](docs/S13_HAKIOS_MACHO_STAGING.md)。GitHub Actions 仅手动触发。

> **S12 新增（2026-10-31）：** 已生成真正的 ARM64 UEFI PE/COFF 预检程序 `WaffleS12Preflight.efi`，
> 并加入 HakiOS XNUP 格式兼容解析和实机设备树校验；**尚不能启动 XNU/iOS**，不可刷写。
> 代码及运行界限见 [S12 源码与测试](docs/S12_HAKIOS_XNUP_PREFLIGHT.md)。
> **Actions 仅手动触发**，不会因提交自动运行。

## S11 离线验证进度（2026-10-28）

新增 **UEFI 当前启动 FDT 的安全边界与 waffle 校验、WFLG 控制台诊断、EDK2 源码包**；AArch64 COFF 与 UEFI Mock 测试通过，**未构建 EFI 可执行文件，未在一加 12 实机启动，不能刷机**。详见 [S11 文档](docs/S11_UEFI_WAFFLE_PROBE.md)。GitHub Actions 仅手动触发。

## S10 本地里程碑（2026-10-28）

新增 UEFI `.inf` 只读枚举入口、WFLG-v1 帧协议/CRC 校验及解码工具、XNU 运行时只读观察者 fail-closed 契约。**仅源码与本地测试，尚未完成实际 EDK2 平台编译、手机 USB-C 传输或 XNU 引导**。详见 [S10 文档](docs/S10_UEFI_BOOTLOG_OBSERVER.md)。本阶段未运行 Actions 或刷写手机。

## S8 本地成果（2026-10-28）

增加 GPU/GMU 双页表静态 SCRATCH 预发布与 HFI 应答时序，以及对应 ColorOS 16 的真实 FDT 时钟关系证据。**尚无 XNU 实机 GPU 驱动，不可刷入。**详见 [S8 文档](docs/S8_POWER_SMMU_PREBOOT.md)。

# OnePlus 12 (waffle / PJD110) · XNU / UEFI Port

**S7 原生 C 4 KiB GPU SMMU 主机页表、独立 GPU/GMU 映射和 Gen7 SCRATCH 内存提供者（仅主机模拟；没有实际 GPU 上电或可刷机 iOS 镜像）。**

目标：在 OnePlus 12（骁龙 8 Gen 3 / SM8650，Qualcomm pineapple）上研究 UEFI → XNU 引导、设备树、UFS、显示、触控和驱动兼容。设备专属数值必须由真机和匹配固件验证；禁止直接使用一加 Ace 5 的分区表/镜像/MMIO 地址。

## 当前 S4 重点

- **S4 GPU/GMU**：[`docs/GPU_ADRENO750_S4.md`](docs/GPU_ADRENO750_S4.md)，Gen7 HFI 首次 SCRATCH 请求应答器、GPU/GMU IOVA 校验、失败隔离、电源／SMMU 资源租约、CPU host unit tests。**没有任何一加 12 实机 MMIO 或 GPU 命令**。
- 不运行 Actions 时，运行 `python3 -m unittest discover -s tests -v`；开发期间保持 Actions **仅手动触发**。

## 当前已经存在的工程内容

- **S3 GPU**：[`docs/GPU_ADRENO750_S3.md`](docs/GPU_ADRENO750_S3.md)，补齐 phandle 资源图、bus `ranges` 地址转换、原生 GMU 固件布局规划器（仅离线，不驱动硬件）。

- **S2 GPU**：[`docs/GPU_ADRENO750_S2.md`](docs/GPU_ADRENO750_S2.md)，SM8650/Adreno750v2 的 DT/GMU/SMMU 固件清单工具、宿主 C 门控及驱动 provider 契约；当前没有任何实机 GPU MMIO 或 Metal 加速。

- `tools/inspect_firmware.py`：支持 boot/vendor_boot、DTB、DTBO 离线只读解析（主机测试）。
- `tools/ingest_report.py`、`tools/check_evidence.py`：带白名单的诊断报告归一化以及硬件基线交叉校验。
- `boards/observations/coloros16_501.json`：来自实机 v1.0 Root 报告的非敏感硬件基线。
- [`docs/OFFLINE_FIRMWARE_S1.md`](docs/OFFLINE_FIRMWARE_S1.md)：固件解析、测试与下一步接入说明。

- `boards/waffle.toml`：来自 2026-10-27 PJD110 / ColorOS 16 的已观察硬件**事实清单**，同时记录不确定项。
- `tools/validate_board.py`：标准库校验，拒绝将未知地址冒充已验证移植数据。
- `tools/make_board_bundle.py`：根据已知板级资料生成可溯源的 JSON 基线包，**并非 UEFI、XNU 或可刷镜像**。
- `tests/`：输入合法性、A/B 分区及未知硬件值检查。
- `.github/workflows/board-ci.yml`：**仅手动触发**检查并打包板级资料，手机可以从 Actions 下载。
- `docs/`：依赖资料、系统升级版本矩阵、严格的验收关口和可复现计划。

### GitHub Actions（当前额度不足，请不要触发）

仓库 Actions → **OnePlus 12 Board CI** → **Run workflow**。成功后下载 `OnePlus12-waffle-board-baseline` ZIP；这是配置和报告包，不是可刷固件。

### 本地构建（Python 3.11+）

```bash
python3 -m unittest discover -s tests -v
python3 tools/validate_board.py boards/waffle.toml
make gpu-test
python3 tools/make_board_bundle.py boards/waffle.toml output
```

## 阶段和禁止事项

1. S0：板级信息、设备档案、原厂固件来源与版本归档（已完成最小基线）。
2. S1：离线解析 boot/vendor_boot/DTB/DTBO 工具已实现；下一步获取匹配原厂镜像并实证解析 MMIO、reserved-memory、显示面板驱动；分区内容、用户数据和专有固件不进 Git。
3. S2：专用 UEFI + 可恢复引导验证（优先内存临时启动；仅在设备明确支持且有回退方案时测试）。
4. S3：XNU CPU0、内存映射、串口/USB 日志，随后 UFS、显示与触控。
5. S4：GPU/音频/Wi-Fi/电源等驱动，最后才是符合法律授权的 iOS 用户态兼容性探索。

**不会把编译成功当成开机成功，也不会把原生 iOS 桌面、蜂窝、Apple ID 或 Metal 支持当成现有成果。**

源代码和官方镜像各自受原作者许可证约束。此仓库**不分发 Apple IPSW / 用户镜像 / 高通或 OEM 专有固件**。

参考与证据见 [`docs/UPSTREAMS.md`](docs/UPSTREAMS.md) 和 [`docs/VALIDATION.md`](docs/VALIDATION.md)。

## S5：一加 12 实机 GPU 固件/设备树资料（2026-10-28）

已在机主提供的 PJD110 ColorOS 16.0.10.501(CN01) 私人只读 ZIP 上校验 SHA-256、GPU/GMU/SMMU 寄存器、SID、中断，以及 15 个 GMU 固件块。详见 [S5 实机证据及未完成项](docs/S5_REAL_HARDWARE_EVIDENCE.md)。

`boards/gpu/waffle_a750_verified_16_501.json` 为公开**元数据**，不是原厂签名证明或可刷包。原始设备树/固件不随源代码分发。未完成 GPU 上电、SMMU runtime、HFI、Fence 和 Metal；不要刷机。

## S6：HFI 队列与内存应答的本地闭环（2026-10-28）

在已经锁定的 S5 PJD110 FDT/固件哈希基础上新增 `gpu/waffle_hfi_ring.{h,c}`、`gpu/waffle_hfi_runtime.{h,c}`，实现 CPU/GMU 队列数据结构、队列回绕、缓存同步提供者及与 S4 HFI 内存分配器的联动。包括原生 C 故障注入测试。详见 [S6 详细记录](docs/S6_HFI_QUEUE_RUNTIME.md)。

```sh
make s6-test
# 私人固件严格保留在仓库外部，按需通过 ONEPLUS12_PRIVATE_EVIDENCE 校验
python3 -m unittest discover -s tests -v
```

**尚未验证**：XNU 下的 GPU 电源、硬件 SMMU 配置、GMU HFI 设备侧握手、真实 GPU 命令/Fence、Metal 加速。S6 不包含可刷写文件；工作流仅手动触发。

## S7：SMMU 页表和 HFI SCRATCH 内存接入（2026-10-28）

- [S7 详细记录](docs/S7_SMMU_SCRATCH_BRINGUP.md)：ARMv8 LPAE 三层 4 KiB 描述符、
  32 位 IOVA/36 位 PA 软件校验；GPU/GMU 两个独立页表根的 4 KiB SCRATCH
  绑定；完整性审计、seal、重叠拒绝、故障隔离。
- `gpu/host_s7_smmu_scratch_test.c` 包含 S6 HFI 队列到 S7 内存映射提供者
  的 CPU-only 集成测试。`WAFFLE_SANITIZE=1 make s7-test` 可运行 ASan/UBSan。
- `boards/gpu/waffle_s7_software_capabilities.json` 明确区分软件实现与
  **没有进行过的 XNU GPU SMMU 配置、GMU 启动、Fence、Metal**。

注意：**这套页表尚未由真实 XNU 运行时安装，也没有 TLBI、fault handler 或
GPU DMA 验证，不能作为已经能在硬件上运行的驱动**；不是镜像，不提供刷写。

## S9：时钟索引审计与运行时双 SMMU 门控（2026-10-28）

- [S9 详细实现、验证与剩余硬件前提](docs/S9_CLOCK_RUNTIME_GUARD.md)：对真实 PJD110 FDT 的四项具名 GPU 时钟和末尾 phandle `0x0` 逐项核对，强调 `apb_pclk` 当前指向 power-controller 而非已证实的时钟电源租约。
- `gpu/waffle_s9_runtime_guard.{h,c}`：通过外部 XNU 提供者回报的独占租约和双 SMMU context 的快照，在 GMU HFI 接收/应答前后重复核验。无提供者默认拒绝，状态不确定隔离且不释放 DMA-facing 内存。
- **90 项本地主机测试通过**，含私人 FDT 的只读校验以及 C ASan/UBSan。没有真实 XNU GPU 硬件接管或 Metal，加速仍不可用。无刷机镜像，Actions 仅手动触发。

可用 `make s9-test` 单独复现 S9 代码测试。所有私人固件材料应放在源码目录外部。

### S9 扩展：9 项独立物理时钟资源的归并与运行时读回

在真实 ColorOS 16 FDT 上定位 GPU(4)、GMU(6)、GPU SMMU(4) 共 14 条时钟引用，合并重复引用后为 9 个唯一资源。`tools/s9_clock_inventory.py` 严格校验索引、具名时钟和厂商额外的尾部零填充；`gpu/waffle_s9_clock_runtime.{h,c}` 在模拟 XNU 提供者下执行带 epoch 的申请、读回、反序回收、隔离及独立 SMMU 快照检查。详见 [S9 扩展记录](docs/S9_CLOCK_RUNTIME_GUARD.md)。本地全量 90 项测试通过（使用机主私人 ZIP 校验，私人原始资料保留在仓库外）；**不是可启动或加速的 GPU 驱动**。
