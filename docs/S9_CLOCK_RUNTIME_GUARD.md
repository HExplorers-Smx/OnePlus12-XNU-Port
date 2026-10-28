# S9 | PJD110 GPU 时钟索引和 XNU 双 SMMU 运行时只读门控

**阶段状态**：本地解析、原生 C 编译、双侧 SMMU 观察模拟和故障注入通过；
**真实 XNU 电源驱动、SMMU 设备编程、GMU 运行、Metal 均未完成，也没有任何可刷写镜像。**

## 1. 来自用户实机的证据

当前 Android 构建：`PJD110_16.0.10.501(CN01)`；运行 FDT SHA256：
`949c54944853d5e4f75ed75d7cfe40e0ec52fe5b4e5db866fd97a96c8f9c131a`。
**只信任这个版本、实际运行 FDT**，不是向任意 SM8650 发时钟投票的依据。

`tools/s9_clock_audit.py` 对私人 ZIP 执行版本和 FDT 哈希校验，逐字节
抽取设备树 clock-names、clocks、#clock-cells、phandle 和提供者路径；
又与已有 S8 资源图交叉检查。生成公开元数据：
`boards/gpu/waffle_s9_clock_index.json`。

| 名称索引 | clock-names | 提供者 | 参数 | 解释 |
|---|---|---|---|---|
| 0 | gcc_gpu_memnoc_gfx | `/soc/clock-controller@100000` | 37 | 仅提供者引用 |
| 1 | gcc_gpu_snoc_dvm_gfx | `/soc/clock-controller@100000` | 38 | 仅提供者引用 |
| 2 | gpu_cc_ahb | `/soc/clock-controller@3d90000` | 0 | 仅提供者引用 |
| 3 | apb_pclk | `/soc/power-controller@c300000` | 空 | **提供者类型需复核**，不能直接当成有频率的普通时钟 |

原 `clocks` 属性有一个**尾部 phandle=0**，位于四个名字之后。
这个哨兵不对应第五个具名时钟；中间 0、额外非零数据、索引漂移或
提供者不符，一律拒绝。**这既不是时钟电源所有权，也不能据此发出 RPMh/AOP 投票**。

对照上游 Linux SM8650 DTS 可以发现不同来源的 GMU 寄存器布局：
上游用过 `0x03d6a000`，而本设备 ColorOS 的 `reg` 是 `0x03d68000`。
驱动必须依据本机运行 FDT 与匹配的 OEM 版本，不复制其他板子的常量。

- Linux SM8650 DTS：https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/b3438e5ca785565a65ef231bc03cd5a05c3be5c7/arch/arm64/boot/dts/qcom/sm8650.dtsi
- GPUCC 时钟 ID：https://code.googlesource.com/linux/torvalds/linux/+/ea0b1a4c5a6ff1338ccffb8aad1ababf42d7bb0d/include/dt-bindings/clock/qcom,sm8650-gpucc.h

## 2. 新增原生 C 接入层

`gpu/waffle_s9_runtime_guard.{h,c}` 使用实际 `wa_s8_preboot` 和
`wa_s6_runtime`，提供用户态不可伪装为 ready 的**协议门控**；调用者必须
自行提供独立的 **XNU 运行时硬件观察者**，不能把设备树、Android 的 Root
读数、UEFI 交接记录或合成测试回调视作当前设备控制权。

在每一次 HFI 接收/发送前后核对：

1. 同一次启动 epoch、power/SMMU/runtime token 和独占 AOP/时钟、电源租约；
2. GPU SID0 与 GMU SID5 两个不同 context bank、不同 ASID 与独立 TTBR 根；
3. 4 KiB 翻译粒度、TLBI 已完成、所有页表已发布、未匹配流故障开启；
4. secure SID2 仍由安全控制者保留，记录序号不倒退；
5. S8 页表仍封存且 audit 成功，HFI 应答仍然走 S8 实际接口而不是另写协议。

如果发送后读回失效，**可能已经向 GMU 暴露 DMA 内存**：进入 quarantine，
不释放 backing，不触发未经证实的重置。观察者未安装时默认拒绝。
整个模块没有映射寄存器、发起电源投票、安装 context bank 或执行 GPU 命令。

注意：测试中的观察者是 CPU 模拟回调；它只能证明入口条件和失败生命周期
符合预期，**不能证明真实硬件拥有独占租约**。继续推进需要提供一套真正
由 XNU GPUCC/AOP、SMMU 和 Gunyah 所有者实现的观察/授权后端。

## 3. 离线复现

```bash
make s9-test
ONEPLUS12_PRIVATE_EVIDENCE=/path/outside/git/Evidence.zip \
  WAFFLE_SANITIZE=1 python3 -m unittest discover -s tests -v
python3 tools/s9_clock_audit.py /path/outside/git/Evidence.zip \
  --out /tmp/waffle_s9_clock_index.json
python3 tools/verify_source_manifest.py
```

使用真实用户资料完成的本地回归：**90 项通过，0 失败，0 跳过**。
S9 C 运行时测试通过 AddressSanitizer 和 UndefinedBehaviorSanitizer。
原始 `fdt.dtb`、`dtbo.img`、四份 GPU 固件都留在本地证据包外，不进源码 ZIP。
没有运行 GitHub Actions 或刷机。

## 4. S10 实际硬件落地的前提

先有可复现的 **UEFI/XNU 内核引导与可靠 USB 日志**，其次才能在已经确认
独占资源的 XNU 会话内建立只读 GPUCC/AOP 时钟快照、SMMU 两个 context
及 fault-on-unmatched 证明。之后再考虑带明确授权的短时电源/时钟
状态切换、Gunyah/secure-world 协作、GMU/ZAP 固件验证及 HFI 启动。

不得拿 Android 运行中的 KernelSU 权限直接修改已被 Android 驱动占用的
GPUCC/SMMU 或 GMU 寄存器来验证 XNU 代码；这可能造成显卡、文件系统或
设备失去响应。先补齐独立实验启动路径、日志与恢复计划。

**当前未实现的关键项**：真实 XNU 平台后端、寄存器读回及所有权合同、
真实 GMU HFI 首个 SCRATCH 应答、可观察 GPU 命令完成、Fence、Metal。

## S9 扩展：具名 GPU/GMU/SMMU 时钟、实际租约门控（本地增强）

在原先 S9 四项 GPU 时钟的单模块审计基础上，新增：

- `tools/s9_clock_inventory.py`：从已认证 **SHA-256 对应当前运行 FDT** 的私人证据 ZIP，严格解析三组 `clocks` 和 `clock-names`。在 FDT 声明 `#clock-cells=0` 时正确识别 `apb_pclk`，仅接受 GPU 时钟属性末尾**一个零填充单元**；任何额外非零数据、重复 phandle、丢失具名提供者或版本不匹配均拒绝。原始 FDT 不进入产物。
- `boards/gpu/waffle_a750_s9_named_clocks.json`：可公开的已解析时钟资源，共 GPU 4、GMU 6、GPU SMMU 4 条具名引用，去重为 **9 项唯一提供者/索引资源**。多个角色引用同一资源，不能重复宣称互不相关的硬件租约。
- `tools/generate_s9_clock_header.py`：根据 JSON 决定性生成 `gpu/waffle_s9_clock_ids.h`；`--check` 检查防止源码和设备树索引漂移。FDT 参数是 OEM 专有索引，不能当作 Linux 主线时钟频率/地址。
- `gpu/waffle_s9_clock_runtime.{h,c}`：**只通过用户显式提供的独占 XNU 运行时提供者**取得 9 个时钟租约，并逐一读回。若存在确定未申请的时钟，按反序释放已确定取得的时钟；申请成功但读回不确定、归还失败或运行时 epoch 漂移时永久隔离，禁止继续访问 HFI、释放可能与 DMA 相关的资源。
- `wa_s9_runtime_ready()`：在由平台驱动保持的同一把 XNU 资源锁内，重新核对全部时钟租约和独立 GPU/GMU SMMU 的 context/stream、TTBR、4KiB、TLBI、缓存发布、secure SID2 未被占用等读回快照。

**限制**：本扩展的所有时钟 claim/readback 和 SMMU 快照由测试模拟的“硬件所有者”提供；真实 XNU 平台中的 `GPUCC/GCC/AOP/GDSC/Gunyah` 驱动、SMMUv2 寄存器所有权、缓存一致性、硬件 HFI/Fence 还没有实现，也尚未在手机上跑通。回调不构成权限证明；实际硬件阶段必须提供同一次 XNU 启动、来自独立可验证来源的日志。不得在 Android/KernelSU 进程内尝试 GPUCC/SMMU 写入。

S9 时钟运行时模块已使用 `clang --target=arm64-apple-darwin` 编译成 Mach-O arm64 **目标文件**；这不是 XNU 内核链接、可装载 kext、可启动 UEFI 或设备端验证。

实机参照注意：上游 Linux 主线 SM8650 DTS 的 GMU MMIO 窗口与机主 `ColorOS 16` FDT 并不相同；本工程始终使用 `waffle` 已验证 MMIO，而非自动替换。

## 5. OEM 全量时钟表与受控的模拟电源租约

`tools/s9_clock_inventory.py` 和 `gpu/waffle_s9_clock_ids.h` 进一步对实际运行
FDT 的 GPU、GMU、GPU SMMU 时钟列表按名称和 phandle 解码、去重，当前捕获
对应 9 个**不同的提供者标识**。`gpu/waffle_s9_clock_runtime.{h,c}`
只通过由将来的 XNU 真正硬件所有者提供的 claim/readback/release 回调管理
顺序、epoch 与失败隔离；没有 MMIO writer。`apb_pclk` 被列在标识表，
但其提供者不是经验证的普通时钟控制器，实际 XNU 适配必须单独判明语义，
不得在未核实的情况下将其当成可投票时钟。

时钟租约的 SMMU 启动前只读检查与每个 HFI 请求的 S9 guard 是两个独立用途，
**不能把它们的模拟 token 当成实际设备权利**；二者头文件现已同时编译
并通过主机 ASan/UBSan 的兼容性测试。
