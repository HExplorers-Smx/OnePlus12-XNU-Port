# S3：waffle Adreno 750 —— 资源依赖图与 GMU 固件规划器

2026-10-28，本地离线研发阶段。**未启动 Adreno GPU、没有 XNU DMA/GPU 驱动、没有 Metal 性能数据，不提供刷机镜像。**

## 本轮代码（在 S2 基础上增加）

- `tools/dt_graph.py`：把 FDT 的 `phandle`、`#iommu-cells`、`#clock-cells`、`#power-domain-cells`、`#reset-cells`、`#interconnect-cells`、`#interrupt-cells` 和原始 `reg` 转换为**只读资源关系图**。即使一个 SID 的 phandle 指向 GPU SMMU，也只是解析结果，不能算作 context bank 租约。DTBO 如果需要外部 overlay fixups，则没有应用原始 base FDT 之前不能取得完全解析的关系图。
- 明确区分 `reg` 的 **bus address** 与 CPU physical address。只在父 bus 提供可解释的 `ranges` 时完成转换；缺少 `ranges` 不会擅自采用恒等映射。无论地址是否已转换，`safe_to_access_mmio` 都为 `false`。
- 支持 `kgsl-3d0` 节点，除兼容 `gpu@...` / `gmu@...` 之外；与 S2 GPU 探针共用结构信息，报告输出 `resource_graphs`，不写寄存器。
- `gpu/waffle_gmu_fw_plan.[ch]`：针对 Gen7 格式、**仅规划** GMU 固件的 16 字节分段头，区分元数据、预分配区、持久预分配区和固件数据；验证截断、重叠、非法虚拟内存、段顺序及容量；两趟解析防止失败时返回部分可执行计划；SQE/AQE 支持跳过首个四字节固件头。地址范围来自同架构的参考实现，**尚未由 PJD110 同版本固件确认**。
- `tests/test_s3_graph.py` 和 `gpu/host_fw_plan_test.c`：合成设备树和假固件，验证完整成功路径和错误路径，不包含 OEM 固件，也没有硬件操作。
- `boards/gpu/stock_release_16_501.json`：记录精确 ColorOS 发布标签和 4 个 ZIP 的公开元数据。`sha256=null` 明确表示**尚未下载与验证**，不是可信固件哈希。

## 实际进度和外部限制

根据用户的 Root 报告，PJD110 运行 SM8650 / Adreno750v2、内核 6.1.141、DSI 1440×3168。现在已能从**用户提供的真实 FDT**解出大部分 GPU 总线和资源依赖；但这次工作环境无法取得 Release 二进制，不能宣布一加 12 的真实 MMIO、IOMMU SID、GMU 时钟/电源依赖和完整原厂固件已经验证。

公开档案：

- [PJD110 精确版本固件](https://github.com/snowwolf725/OnePlus12-fw-repos/releases/tag/PJD110_16.0.10.501(CN01))；只验证了资产名称/大小/发布时间，没有本地固件摘要
- [UniXNU Ace 5 Gen7 固件解析格式](https://github.com/tycykp/UniXNU-OnePlus-Ace5/blob/main/ios16/gpu/giulia_gpu_firmware.h)；来源为 BSD-2-Clause-Patent，非已验证的 waffle 专用规则
- [UniXNU Adreno740](https://github.com/UniXNU/Adreno740) 和 [AdrenoMetal](https://github.com/UniXNU/AdrenoMetal)；需要专门的 A750 SM8650 backend，不能直接复制 A740 初始化和寄存器

## 本地复现，无需 Actions

```sh
python3 -m unittest discover -s tests -v
python3 tools/gpu_probe.py --dt-image /local/stock/dtbo.zip \
  --build 'PJD110_16.0.10.501(CN01)' --out out/gpu/dtbo-inventory.json
```

**要做真正硬件适配而不是继续堆接口，下一道严格门槛是 G0/G1：**
1. 核对该系统版本的 `dtbo` 和 `vendor_boot` 中的 DTB，处理 overlay / `__fixups__`，再结合运行时 `/sys/firmware/fdt` 交叉验证 GPU MMIO、电源域、时钟及 GPU SMMU 路由。
2. 从完整且匹配的原厂 `vendor` 固件确定 GMU/SQE/AQE/ZAP 的真实 SHA-256、装载方式与验证签名；公开的 `vendor_dlkm.zip` 未必包含这些固件，不得因压缩包名字推断。
3. 只有完成以上验证后，再对接 Gen7-9-1 的时钟/RPMh/SMMU 租约、GMU HFI、命令提交及真实 Fence。没有验证前，内核加载功能保持关闭。

`waffle_gmu_fw_plan` 是数据格式验证器，绝非可运行的 GPU 驱动或 DMA 映射器。源码编译通过不代表设备安全、界面流畅、iOS 可用。
