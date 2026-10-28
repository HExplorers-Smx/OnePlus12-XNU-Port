## S8 → S9 优先工作

1. 追踪 ColorOS 16 GPU `clocks` 尾部 phandle `0x0` 和四个命名时钟的真实调用索引。
2. 实现具备硬件所有权证明的 GPUCC/AOP 和独立 GPU SMMU 后端，默认只读。
3. GMU 首个 SCRATCH 请求必须取得真实 GMU ACK 观察，而不是主机队列模拟。
4. 验证 GPU 命令与硬件 fence，最后再连接 Metal。

# 下一步升级方向

1. 维护匹配版本固件 manifest（下载链接、构建标识、SHA256、设备型号、boot/dtbo/vendor_boot 的头和 dtbo 索引）。原始镜像存放于维护者私有环境，不纳入 Git。
2. 让 DeviceArchive v1.1 可显式导出 DTB/设备树**非敏感结构化元数据**和面板/触控驱动的 `compatible`，不碰分区写入。
3. 增加 `waffle` 专用 UEFI 源码/补丁目录、锁定上游 commit，并为 UEFI 编译创建单独手动 Actions；**在完成引导协议审计前只编译不刷写**。
4. 确认 Bootloader 是否支持无持久写入的临时启动与可恢复调试，准备日志、断电及 Android 回退流程。不可默认支持 `fastboot boot`。
5. XNU CPU0/内存映射、UFS、显示触控顺序补齐，每一步分别记录主机测试和真机测试。


## S4 后更新：真正的 GPU 接入关口

- 优先在已 Root 的一加 12 上通过**下一版 DeviceArchive 的可选导出功能**读取当前运行 DTB 非敏感元数据与原厂 GPU 固件摘要；不要求用户使用电脑，也不把原始固件或密钥上传 Git。
- 只有拿到与 `PJD110_16.0.10.501(CN01)` 对应的 FDT、GPU firmware、可信来源和受控的 SMMU/context-bank ownership，才能实现真实硬件 provider；S4 的 C 模块不能独立驱动 GPU。
- 先取得真实首个 SCRATCH 供需闭环与 GMU ACK，再接 CP/HFI 命令、fence、display scanout 和 Metal；没有实机日志不能标记为完成。
- 继续只在本地编译；Actions 配置保持 workflow_dispatch，不消耗额度。

## S6 → S7 重点（2026-10-28）

S6 已完成 **主机侧的 HFI ring + SCRATCH 分配应答闭环**，详见 `docs/S6_HFI_QUEUE_RUNTIME.md`。S7 不应重复实现环形缓冲区，应先将 A750 XNU 真正缺失的硬件提供者补齐：

1. PJD110 当前启动固件下的电源/时钟/AOP/Gunyah 所有权，不能复用 Ace 5 的投票值；
2. 独立 GPU SMMU context/SID5 所有权、4 KiB 页表和失效/撤销证据；
3. 原厂签名固件的正确认证流程，GMU TCM/RAM 装载及 HFI 17 队列的实机传输；
4. 首个 SCRATCH 请求的真实 GMU 地址、提交应答和设备侧 ACK；
5. 进一步支持 Gen7 HWSCHED 的其它消息、第一条 GPU 命令及真实 Fence；最后才考虑 Metal。

**没有实际 XNU 启动和同一次冷启动证据，不可以宣称任何设备阶段完成。**


## S8 (after S7) — provider-only XNU bring-up

1. Implement an actual OnePlus 12 runtime GPU power owner that positively proves
   exclusive GDSC, clocks, AOP/RPMh transport, firmware retention and clean unwind.
2. Port the independent GPU SMMU 4K stage-1 provider with real table-owner PA,
   TTBR/TCR/MAIR, SID-5 stream ownership and fault handler. The S7 software
   descriptors are NOT ready to activate until published and audited on device.
3. Add bounded dynamic map updates with correct publish/TLBI and confirmed
   ownership transitions; S7 deliberately forbids writes after seal and cannot
   be reused as a live HFI allocator without this stage.
4. Verify the real GMU firmware/ZAP and boot handshake before requesting a live
   HFI SCRATCH ACK; check it on device using UART/USB logs and no partition writes.
5. Only then add command submission, real fence completion and Metal scanout.

Do not run Actions until usage is available. No OEM firmware or private captures
should be committed to public GitHub.

## S9 完成后 S10 的关键技术债

1. GPUCC/GCC 时钟表的四个名字已匹配实际 FDT，但 `apb_pclk` 关联的是 `/soc/power-controller@c300000`，需拿对应原厂驱动的 `clk_get` / `devm_clk_get` 序号/可选项语义进行核对，**禁止硬编码未知投票**。
2. 首先集成能真正开机并输出可靠 USB 诊断日志的 `waffle` XNU/UEFI 平台；目前仓库仅 GPU 子模块，真实 GPUCC/AOP/RPMh/Gunyah 所有者还不存在。
3. 由真 XNU 驱动提供 S9 只读快照和独占资源声明，核对两个 SMMU context、4 KiB TTBR、TLBI、fault handler，之后才考虑带回滚的受控写操作。
4. 读取同一次实机启动中的 GMU 首个 SCRATCH 请求及主机应答可观察结果，杜绝把主机测试的 ACK 当设备侧完成；最后再做 GPU 提交、Fence、Metal。
5. 测试场景应具备可靠回退和日志；不能在 Android 正在运行时借 KernelSU 直接更改 GPUCC 或 SMMU 硬件寄存器。

## S9 本地增强后：接下来优先构建真正的 XNU 启动与硬件观察者

1. 复用同平台 XNU 项目的合法开源代码建立 `waffle` CPU0/USB 日志启动工程，不要直接复用 Ace5 的 board memory/GPT 值；首先需要可恢复的仅调试启动方案。
2. 原生 XNU GPUCC/AOP/RPMh 控制器提供者必须在同次启动中审计真实所有权、lease token 与读回；当前 9 项唯一时钟关系并**不是**投票值或硬件访问许可。
3. 独立 GPU SMMU provider 需记录 SID0/SID5/secure SID2、合法 context bank、真实 CPU/4K GPU 粒度桥接、TTBR/TCR/MAIR、TLBI 与 fault 读回，在切换硬件前先证明 DMA 已停止。
4. GMU/ZAP 固件使用当前机器版本的私有固件，不将固件正文纳入公开源码，先观察原生 GMU 启动以及真正的 SCRATCH 请求和 ACK；后续才能推进 command/fence 与 Metal。
5. Android 运行时已有 KGSL 持有 GPU，不得通过已 Root 的手机直接写 GPUCC/GMU/SMMU 寄存器进行“模拟 XNU”验证。

## S11 后续跟进（2026-10-28）

- 已有 S11 只读 UEFI 启动探针：对当前启动配置表内 FDT 进行内存边界与 `waffle` 型号识别，并通过 UEFI 控制台打印可被 S10 解码的 WFLG_HEX 日志。
- **没有设备端构建**：尚无经验证可恢复的 waffle EDK2 平台、USB-C DWC3 传输、完整 XNU boot chain 或 GPU 驱动上电。
- 下一步优先取得独立、可恢复、经过实机验证的 waffle UEFI 进入路径；然后在该路径下读取同一引导会话的设备树/时钟所有权并收集日志，最后推进 XNU CPU0 与 GPUCC/SMMU。
- CI 工作流只允许 `workflow_dispatch`，不在源码更新时运行；不可为了尝试启动随意写入 A/B `uefi`、`abl`、`boot` 或 `vendor_boot` 分区。
