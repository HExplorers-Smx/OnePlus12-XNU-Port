# S8 | Adreno 750 Static-SCRATCH / SMMU runtime bridge

## S7 -> S8 关键更正

真实 Gen7 GMU 在 START 后才会通过 HFI 发送 kind=2 SCRATCH 内存请求。
S7 的主机测试在收到请求时建立 GPU/GMU 页表；但在已经启用的 SMMU 上修改
页表要求正确的 cache publish / TLBI / break-before-make，S7 尚未实现。

S8 改为启动前从受信任的 4 KiB backing 提供者取得一页物理 RAM，清零、
检查、cache 发布，写入 **GPU SID0 与 GMU SID5 两棵独立软件页表**，审计并
封存两个根，发布表页。之后收到 HFI SCRATCH 才把已经拥有的 IOVA
回给固件，绝不在 HFI 回调里修改页表或在设备启动后再次清零。

核心：`gpu/waffle_s8_preboot.{h,c}`，对接 S4 的租约、S7 的页表构建、
S6 的 Gen7 HFI 17 队列。不同 GMU handle 不会复用唯一 SCRATCH backing。
S6 对完全相同的请求有内部 ACK 缓存，所以新增 `wa_s8_attach_s6()` / 
`wa_s8_pump_s6()`，让重复应答也重新核对当前电源和 GPU/GMU SMMU 所有权。
这两个入口仍依赖外部 XNU 同步锁，不能在所有权并发撤销中无锁使用。

S8 callback 契约要求已验证的设备树哈希、本次启动 epoch、表页和 backing
物理内存所有权、缓存发布、独立 GPU/GMU context bank、TTBR/TCR/MAIR、
TLBI 完成、secure SID2 保护以及 fault-on-unmatched。
**回调接口并不等于这些真实硬件驱动已经实现**。当前没有 XNU MMIO
或电源编程实现，默认没有任何可写硬件的权限。

## 实机 FDT 时钟依赖

`tools/s8_power_inventory.py` 读取 v1.1 APP 导出的私人 ZIP，验证 FDT SHA-256
和 ColorOS 编译版本，并生成无专有固件、无原始 DTB 的板级依赖清单：
`boards/gpu/waffle_a750_power_deps_s8.json`。

- GMU 时钟来自 GPUCC `0x03d90000` 和 GCC `0x00100000` 两个提供者，
  AOP mailbox `/soc/qcom,qmp-aop`，供应电源由 GDSC 节点引用。
- GPU SMMU 的时钟和 `vdd-supply` phandle 已解码，但仅为结构信息。
- GPU 的 `clock-names` 有四个名称：`gcc_gpu_memnoc_gfx`、
  `gcc_gpu_snoc_dvm_gfx`、`gpu_cc_ahb`、`apb_pclk`。
  对应的 `clocks` 属性最后还有一个 **phandle 0x0**；可能是空占位项。
  因为完整 `clocks` 仍包含这个无法解码项，清单保守记为未全部解析；
  不能直接断言缺少真实时钟控制器，也不能擅自执行未知时钟 vote。

## 确认范围与风险

本轮主机侧确认：S8 预分配/双表/封存/HFI ACK 可执行；错误的地址、
SMMU SID、不同 handle、多次请求、旧 epoch、缓存发布失败、租约拒绝和
运行时读回失效均有对应测试。所有模拟回调均不控制手机硬件。

**没有**在手机上运行 XNU GPU 驱动、读取真实 GMU HFI ACK、通电/断电、
配置任何 context bank、发布 Metal、操作 UEFI 分区或运行 GPU 命令。
XNU 常用 16 KiB CPU 物理页与 GPU SMMU 使用的 4 KiB 页表有不同粒度；
实际 XNU backing provider 必须保证 4 KiB 子页所有权与相邻内存安全，
不能仅将 CPU 页直接四等分而不核对宿主映射。

## 下一步 S9

1. 对照匹配的 OnePlus SM8650 GPU 驱动调查 `clocks` 尾部空 phandle
   和 4 个 `clock-names` 的调用索引；核实运行时电源/AOP/RPMh 所有者。
2. 在独立启动实验工程中实现**默认只读**的 GPUCC/电源/独立 GPU SMMU
   平台后端，先做到身份、context bank/stream、TTBR/4K 属性、TLBI 读回。
3. 完成 ZAP 安全固件认证、真实 GMU/HFI 首个 SCRATCH 应答，确认设备
   端收到了 ACK，再推进 GPU 命令及硬件 fence。
4. 才能继续做 IOSurface、scanout 和 Metal。

不提供任何可刷写镜像或写分区命令，直到真机实验路径与恢复机制通过验证。

## 离线复现

```bash
make s8-test
WAFFLE_SANITIZE=1 python3 -m unittest discover -s tests -v
python3 tools/s8_power_inventory.py /path/outside/repo/PRIVATE.zip --out /tmp/power.json
python3 tools/verify_source_manifest.py
```
