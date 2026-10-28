# S5｜PJD110 实机 GPU 证据与可复现校验

本轮输入为机主自行导出的 `OnePlus12-XNU-Evidence-1793121535483.zip`，来源为 Root 只读采集、当前已启动的 **PJD110 / ColorOS `PJD110_16.0.10.501(CN01)` / A 槽**。**原始 ZIP、设备树和专有固件不在本仓库，也不得公开提交。**

## 经过验证的事实

- ZIP 中 `fdt.dtb`、完整 `dtbo.img` 和四份 GPU 固件的 SHA-256 与采集报告相符。此为复制完整性校验，**不代表 OEM 数字签名验证**。
- 运行中 FDT 大小 919,577 字节，5,009 个节点，型号 `Qualcomm Technologies, Inc. Pineapple MTP,waffle`；GPU 节点为 `qcom,adreno-gpu-gen7-9-1`，`qcom,chipid = 0x43051401`（A750v2）。
- GPU `reg` CPU 物理基址 `0x03D00000`，容量 `0x40000`；GIC SPI 300。
- GMU `reg` 基址 **`0x03D68000`，长度 `0x37000`**，GIC SPI 304、305。节点**名字**包含 `3d69000`，不能把节点名误当成 `reg` 地址。
- 独立 GPU SMMU `0x03DA0000` / `0x40000`，位于 `/soc/kgsl-smmu@3da0000`。通过设备树 phandle 得出的 stream ID 为 user=0、LPAC=1、secure=2、GMU=5。SID 不是 context bank 号码，也不是能够直接接管的证明。
- GMU 通过 `/soc/qcom,qmp-aop` 邮箱、`/soc/clock-controller@3d90000` 等时钟控制节点，以及单独的 `vdd`、`vddcx` 供电节点相连。**从 phandle 关系推导不出可以直接使用的电源投票值。**
- `dtbo.img` 读取了 24 MiB 分区容量，其中 DTBO 表使用 6,683,846 字节，包含 12 个 overlay 条目。**不能凭表顺序猜测实际应用的是哪一个**；此处以运行中 FDT 为准。
- 用户提供的 `gmu_gen70900.bin` (78,588 字节) 使用 S4 原生 C 规划器验证，包含 **15 块（11 元数据 / 4 数据）**；两块持久 GMU 虚拟预分配地址是 `0x60000000`、`0x60001000`，各 4 KiB。这不是 CPU 物理地址。
- SQE、AQE、ZAP 文件均取得并比对摘要。它们的文件存在及 SHA-256 校验**不证明安全世界已验证 ZAP、GMU 已执行或 HFI 已成功通信**。

本轮结构化结果：[`boards/gpu/waffle_a750_verified_16_501.json`](../boards/gpu/waffle_a750_verified_16_501.json)。里面只有地址、版本、结构、大小和摘要，不含固件或完整设备树字节。

## 代码改动

1. `tools/qualify_evidence_s5.py`：严格校验私人 ZIP 白名单、文件尺寸、报告哈希、型号/版本/槽位、运行中 FDT 的 GPU/GMU/SMMU 身份与资源、DTBO 表、原厂 GMU 格式。缺证据**失败而非回退到其他手机**。
2. `gpu/waffle_a750_hw_s5.{h,c}`：编译时绑定的 PJD110 地址与版本校验；硬件 `ready` 固定为 false，不能把只读设备树当 MMIO 访问许可。
3. `tests/test_s5_evidence.py`：错误构件、错误固件块、重复 ZIP 成员、恶意额外文件、错误系统版本、真实/合成证据和原生 C 门控测试。

## 离线复现

不消耗 GitHub Actions；完整私有 ZIP 请置于 Git 工作区**外部**：

```bash
python3 tools/qualify_evidence_s5.py /private/OnePlus12-XNU-Evidence.zip --out /tmp/waffle_s5.json
ONEPLUS12_PRIVATE_EVIDENCE=/private/OnePlus12-XNU-Evidence.zip python3 -m unittest discover -s tests -v
```

`/tmp/waffle_s5.json` 应和仓库中 S5 公共结构记录关键字段一致。无私有固件的公开测试仍可执行，私有集成测试会自动跳过。

## 仍缺什么

- 设备电源、时钟、RPMh/AOP/Gunyah 的**运行时所有权与有界失败回滚后端**。
- 原生 4 KiB GPU SMMU 页表、独立 context bank 租约及硬件翻译故障与 TLBI 读回验证。
- GMU/ZAP **安全认证**、真实 GMU TCM/RAM 初始化与 HFI `SCRATCH` 请求的 **设备侧** 应答验证。
- 一条真实 GPU 提交、Fence 完成与显存读回；继而是显示/IOSurface/Metal。未完成这些之前严禁显示“GPU 驱动已适配”。

**严禁：** 提供可刷机镜像；根据 DTB 地址直接写 MMIO；刷写 `uefi`、`dtbo`、`boot`、`super`；公开原厂固件或用户整份 FDT；将 iPhone X IPSW 当作通用驱动。
