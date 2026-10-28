# OnePlus 12 (waffle/PJD110) GPU 路线 — S2 离线接入基线

**阶段状态：仅离线资料解析和驱动接口骨架，未接通任何 GPU 硬件。**

## 为什么 GPU 是一项必须单独完成的驱动工作

一加 12 根报告确认 SM8650 + `Adreno750v2`，1440×3168，DSI-1/DSI-2，最高 120 Hz。iPhone X 使用 Apple A11 GPU；iPhone X iOS 16 用户态/根文件系统可作为 ABI 研究对象，不能作为高通 Adreno 驱动，也不代表 Apple 完整系统镜像能直接启动或激活。Apple 的用户态、信任缓存、板级设备树、固件及 XNU 构建必须匹配。这套工程不提供/分发 Apple IPSW 或任何 OEM 固件。

目标图形链：

```
Darwin 22/iOS 16 GUI --> Metal/QuartzCore/IOSurface
                        | 原生 Metal → AIR/SPIR-V/NIR/IR3 / metal2vulkan + Turnip
NativeAdreno XNU user client --> Adreno750 device/backend --> BO / 4KiB GPU SMMU
                        | GMU power, signed GMU FW, HFI, Gen7-9-1 HWSCHED
                        | 真实 queue + fence --> display scanout + IOSurface lifetime
```

**禁止误读**：GOP/简单 framebuffer 成功显示 Apple 标志，不代表 3D 加速。`gpu_probe.py` 读取 `reg` 并不授权内核去访问该 MMIO；`waffle_gpu_gate.c` 里的阶段前进来自调用方证明，尚无实际可用的证明提供者。

## 已经写入本工程

1. `boards/gpu/waffle_a750.json`：区分你在 PJD110 上已经观测的事实、Linux A750 Gen3 参考值、尚未完成的板级采集；**没有把 Ace 5 寄存器地址复制为一加 12 的已验证数据**。
2. `tools/gpu_probe.py`：从本地原始 DTB、DTBO 表、`vendor_boot`（v3/v4）或包含单一镜像的原厂分区 ZIP识别 GPU / GMU / GPU SMMU 节点；记录 `reg`、`reg-names`、`interrupt-names`、`iommus` 原始 phandle 单元，保留未解析状态及 root compatible 是否匹配 waffle 的布尔结果。支持同版本原厂固件目录中四种候选固件的 SHA-256、长度、缺失项清单；不会写分区或输出原厂固件内容。
3. `tools/inspect_firmware.py`：新增 GPU/GMU/SMMU 节点筛选和原始 phandle 辅助字段；**SID 数值 ≠ context bank 编号，也不能把 phandle 当 IRQ 号**。
4. `gpu/waffle_gpu_gate.[ch]`：可移植原生 C 失败闭锁状态机，驱动未验证时禁止标记 Metal ready。失败/状态不确定时保持隔离，需要独立确认冷重启才能重置，不会自动释放可能仍被 DMA 引用的地址空间。
5. `gpu/waffle_gpu_platform.[ch]`：面向未来 XNU GPU provider 的分层回调接口，要求真实固件、时钟、SMMU、HFI、fence、scanout、Metal 证据。**当前不包含回调的真机驱动实现，不会访问任何真实 MMIO。**
6. `tests/test_gpu.py` 及 `gpu/host_*`：合成 DTB、模拟固件、C 端异常/成功链测试，不调用设备，也不需要 Actions。

## A740 与 A750 的差别

Linux `drm/msm` A750 补丁：Adreno 750 在 SM8650 上使用 `ADRENO_7XX_GEN3`，参考 chip ID `0x43051401`；与 A740 不同，A750 没有 A740 的 HWCFG block，而是不同的寄存器路径。A750 参考 GMEM 为 3 MiB，候选固件包括 `gmu_gen70900.bin`、`gen70900_sqe.fw`、`gen70900_aqe.fw` 与 `gen70900_zap.mbn`，固件身份/名称需要以匹配 PJD110 原厂版本复核。

参考代码与文档：
- https://github.com/UniXNU/Adreno740
- https://github.com/UniXNU/AdrenoMetal
- https://unixnu.com/porting/gpu
- https://github.com/tycykp/UniXNU-OnePlus-Ace5/blob/main/docs/GPU_BRINGUP.md
- https://www.mail-archive.com/freedreno@lists.freedesktop.org/msg29878.html

采用上游接口时必须保留原许可证与固定来源版本。不能直接把 `ShengGmu*`、SM8550/A740 配置、`a740_sqe.fw`、`a740_zap.mbn`、A740 CP ring 或 Ace 5 的硬件地址照搬到 waffle。

## 仍然缺失的真实驱动代码（按优先级）

| 门槛 | 必须实现和实证 | 现状 |
|---|---|---|
| G0 | PJD110 本机 DTB 对应的 GPU/GMU/SMMU `reg`、interrupts、clocks、power-domains、iommus、reserved-memory、版本 SHA；解开 phandle 关联 | 未采集完整 DTB |
| G1 | 与当前 ColorOS 16.0.10.501 版完全匹配的原厂 GMU/SQE/AQE/ZAP 校验、签名装载与 provenance | 未取得固件 |
| G2 | Gen7-9-1 GPU 电源/时钟/RPMh 投票与状态读回，失败时硬件复位和租约规则 | 未实现 |
| G3 | 独立 GPU 4KiB SMMU 页表、stream/context bank 租约及 fault/TLBI 证明；严禁篡改非本设备所有者的 context | 未实现 |
| G4 | GMU 固件解析、认证、TCM/RAM 载入、实际 HFI ready/ACK 以及 HWSCHED 队列 | 未实现 |
| G5 | A750 PM4/IB 提交、真实 GPU fence 和 DMA 完成，再安全回收 BO | 未实现 |
| G6 | 校验颜色/像素一致的 1440×3168 scanout、原生 DSI 与双缓冲同步 | 未实现 |
| G7 | iOS 16 Metal/IOSurface 用户态 ABI、QuartzCore/backboardd 和 shader ABI 实测 | 未实现 |

**禁用替代证据**：Android 的 KGSL 运行正常，不代表 XNU GPU 正常；源代码通过 GCC 不代表 GPU 上电；读取 framebuffer 不代表 Metal 加速；没有真实 `fence` 回读禁止标记渲染完成。禁止通过猜测 `sid`、`bank` 或 GPIO/IRQ 写寄存器。

## 复现（电脑/本地构建；不是要求手机输入命令）

```bash
make test
make gpu-test
python3 tools/gpu_probe.py \
  --dt-image /path/to/waffle-verified.dtb \
  --firmware-dir /path/to/local-stock-firmware \
  --build 'PJD110_16.0.10.501(CN01)' \
  --out out/gpu/waffle-firmware-qualification.json
```

## 一加 12 精确匹配固件资源索引（仅索引，没有下载入工程）

第三方 GitHub 发布页已经列出 `PJD110_16.0.10.501(CN01)`，其中包括 `dtbo.zip`、`vendor_boot.zip`、`vendor_dlkm.zip`：
https://github.com/snowwolf725/OnePlus12-fw-repos/releases/tag/PJD110_16.0.10.501(CN01)

仓库不包含这些二进制：本地执行环境无法通过网络下载完整原厂资源，也没有对实际 ZIP 内容做检验。`gpu_probe.py` 已允许直接读取 **单镜像 ZIP**，但 `vendor_dlkm` 模块压缩包仍不支持作为 DTB 直接读入。须核实 OTA 与底层固件版本一致，不能因文件名相同跳过 SHA/签名核对。

系统更新到 ColorOS 17/18 时先生成**独立版本 pin 和 SHA**，不能复用 ColorOS 16 的寄存器或固件假设。下一次应对照真机 firmware DTS 和 Qualcomm DT binding/原厂 KGSL 源码，生成明确的 `waffle_a750_verified.json`，等可安全驱动的 G0/G1 完成后再进入硬件接入。
