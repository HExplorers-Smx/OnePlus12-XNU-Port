# Stage S1: 离线固件结构分析与设备树验证

这是一套**只读固件解析工具**，不修改任何安卓分区，也不生产可刷入/可引导镜像。

## 已确认的设备基线

来自 2026-10-27 的 PJD110 ColorOS 16.0.10.501(CN01) Root 采集报告：设备 `waffle`，SM8650，屏幕 1440×3168，DSI-1/DSI-2，触控名称 `touchpanel`，Adreno750v2，UFS 控制器 `ufshcd`，147 个列名分区，A/B 双槽，Linux 6.1.141。**未确认的仍为屏幕 IC 型号、触摸 IC 型号、SM8650 专属 MMIO 地址、XNU 兼容性。**

- 原报告不得提交到公有 GitHub。允许提交经过白名单转换的 `boards/observations/coloros16_501.json`（不含序列号、IMEI、账号或密钥）。
- `tools/ingest_report.py` 针对 **DeviceArchive v1.0 成功 Root 报告**解析本地 TXT，仅输出指定的普通硬件字段。
- `tools/check_evidence.py` 比对 `boards/waffle.toml` 和快照，发现型号、版本或分区大小冲突时显式失败。未来 ColorOS 17/18 的数据要单独建新快照，不能覆盖已验证基线。

## 离线解析步骤

Python 3.11+，无需额外依赖，不要求 GitHub Actions。

```bash
python3 -m unittest discover -s tests -v
python3 tools/check_evidence.py --board boards/waffle.toml --snapshot boards/observations/coloros16_501.json
python3 tools/ingest_report.py /private/OnePlus12-report_1.txt --out output/coloros16_local.json
python3 tools/inspect_firmware.py /private/boot.img /private/vendor_boot.img /private/dtbo.img --out output/firmware-metadata.json
```

`inspect_firmware` 当前支持：

- Android GKI `boot.img` header v3/v4：检查内核与 ramdisk 布局边界，不解压数据；
- `vendor_boot.img` header v3/v4：读取设备树偏移、大小、ramdisk 布局，若是合法单个 FDT 则解析；
- 原始 `.dtb`：FDT 版本、root-compatible、model、节点总数，部分 Qualcomm 子系统和 reserved-memory 的 `reg` 信息；
- Android `.dtbo` 表：每个条目的索引、ID、revision、哈希、FDT 根兼容字符串及限定的节点信息。

如果 `vendor_boot.img` 包含拼接的多个 DTB，当前**只解析第一个 FDT**；实际硬件选型需要进一步结合匹配的 DTBO 与厂商启动链，不可直接用于编写 UEFI 寄存器驱动。

脚本既不会抽取固件载荷，也不扫描个人文件；文件只从你传入的本地路径读取。对损坏和不认识的镜像主动报错。不假设所有 OEM 镜像都采用 AOSP 规范，也不绕过设备安全启动校验。

## 下一步真正的移植关口

1. **严格匹配版本**：获取 `PJD110_16.0.10.501(CN01)` 的官方全量包，记录精确 URL、发布版本和 SHA-256；不同机型和版本不得混用。
2. **硬件驱动契约**：从匹配的 DTB/DTBO 补全触屏、面板、UFS、IOMMU、内存预留区、电源域与中断资源；每条记录标注来源和状态。
3. **受控 UEFI 骨架**：单独管理上游版本及补丁，不使用另一机型的 MMIO 值，先让主机上的编译与静态检测通过。
4. **真机调试预案**：事先确认一加 12 的 `fastboot boot` 是否被允许、回退镜像是否匹配。**不假设 `fastboot boot` 一定可用，不写入 `uefi_a/b` 等引导分区**。
5. XNU 内核入口/日志测试通过后才按 UFS、显示、触控顺序推进；iOS GUI 不在现阶段交付范围。

AOSP 规范：
- <https://source.android.com/docs/core/architecture/dto/partitions>
- <https://source.android.com/docs/core/architecture/bootloader/boot-image-header>
- <https://source.android.com/docs/core/architecture/partitions/vendor-boot-partitions>
