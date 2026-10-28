# S11 · waffle UEFI 设备树验证与只读诊断入口

**本阶段不是 XNU、iOS、UEFI 固件或可刷写 boot.img。所有测试均在 Linux 主机环境进行。**

## 解决的实质问题

S10 的探针只能检查 UEFI SerialIo 句柄，完全不知道正在运行的是否是一加 12；
新 S11 先用 UEFI `EFI_CONFIGURATION_TABLE` 中的 `gFdtTableGuid` 寻找当前引导会话的 FDT，
并拒绝未证明为 `waffle` 的环境。明确拒绝将其他 SM8650 设备（如 Ace 5）视为同板。

UEFI 入口 `uefi/WaffleS11Probe/WaffleS11Probe.c` 依次：

1. 在配置表搜索 FDT；找不到即失败。
2. 使用 `GetMemoryMap`，确认 FDT 头和 `totalsize` 声明的整个区域都落在一个允许的 UEFI 内存描述符内；
   过大、缺失、映射不一致或 MemoryMap 调用失败即拒绝，不尝试地址扫描。
3. 用 `boot/waffle_s11_fdt.{h,c}` 的零堆内存解析器严格验证 FDT v17 头、保留区、
   结构/字符串块边界、节点嵌套、属性和 END 标记，并且要求根节点 `model`
   以 `,waffle` 结尾、`compatible` 含 `qcom,pineapple`。
4. 在 UEFI **控制台**打印只含统计量、CRC32 和明确未验证声明的 WFLG-v1 十六进制日志行。
   可将这一行从已经可用的 UEFI 控制台记录复制出来，再用 S10 解码器处理。
   **这不是 USB-C CDC/ACM、UART 线、fastboot 或真正 XNU 日志。**
5. 可选地枚举 SerialIo 句柄数量，但不使用这些句柄进行传输。
6. 直接返回 UEFI：没有 `ExitBootServices`、磁盘写入、MMIO、EFI 变量更新、
   GPU 上电、SMMU 接管、XNU 引导或持久修改。

CRC32 只用于发现意外损坏，**不能证明固件真实性**。机主原始 FDT 的 SHA-256
通过已有 S5 私人证据流程核验；本仓库绝不上传该 FDT 或任何原厂固件正文。

## EDK2 构建输入：已准备但未完成真实 EDK2 构建

本阶段新增 `WaffleS11Probe.inf`、`WaffleS11Pkg.dec`、`WaffleS11Pkg.dsc`。
仓库本身没有 EDK2 SDK；已对纯 C 核心和 UEFI 源码做 AArch64 COFF 编译检查，
但是在此环境没有执行真实 EDK2 `build`、没有链接成 `.efi`，更没有在一加 12 引导。

准备独立 EDK2 包（不会包含专有固件）：

```sh
python3 tools/s11_stage_edk2.py --out /tmp/waffle-edk2-packages
```

在另行安装的匹配版本 EDK2 工作区中，可将生成的 `/tmp/.../WaffleS11Pkg`
加入 `PACKAGES_PATH`，然后尝试 EDK2 standalone UEFI Application 编译：

```sh
export PACKAGES_PATH=/tmp/waffle-edk2-packages
# 在 edk2 工作区内，并已 source edksetup.sh / 已生成 BaseTools 时：
build -a AARCH64 -p WaffleS11Pkg/WaffleS11Pkg.dsc -t CLANGDWARF
```

这里的 DSC 是一个 **UEFI application 编译平台**，不是能启动
waffle 硬件的固件平台 FDF、UEFI 驱动集、UEFI 设备引导程序或完整 XNU 镜像。
`CLANGDWARF` 是建议尝试的工具链标识，实际依赖本地 EDK2/LLVM 配置，
**上述 build 命令尚未经过真实工具链验证，可能需要补库依赖或调整 DSC。**

绝对不要把本阶段独立 `.efi` 应用转换成 `boot.img` 并刷入分区。
运行此程序的前提是**已具备单独验证、可恢复的 waffle UEFI 启动环境**。
没有这个环境就只运行宿主测试，不对手机进行引导试验。

## 本地可复现验证

```sh
# 只读、无网络、无 GitHub Actions
make s11-test
ONEPLUS12_PRIVATE_EVIDENCE=/private/location/OnePlus12-XNU-Evidence.zip \
  python3 -m unittest discover -s tests -v
python3 tools/verify_source_manifest.py
python3 tools/s11_stage_edk2.py --out /tmp/waffle-edk2-packages
```

`test_s11_native.py` 使用合成 FDT、伪造 UEFI memory-map 和真实 UEFI 应用入口，
覆盖匹配/错误机型、异常内存映射、截断、溢出、WFLG 可解码等；
有 `ONEPLUS12_PRIVATE_EVIDENCE` 时，还会只读校验已提供的 PJD110 私人 FDT。
`tests/fixtures/s11_edk2/` 仅用于 **host mock 与 Clang 语法测试**；
它并不是 EDK2 头文件的替代品，也不能参与正式构建。

## 下一阶段 S12（必要硬件前置）

1. 在经过验证的 waffle UEFI 引导环境执行 S11 探针，实际获取控制台或串口日志；
   **未验证可恢复启动前不得提议写入 boot/UEFI 分区**。
2. 证实真实日志物理通道（UART / USB gadget）并为 WFLG 建立传输层；
   不得把 SerialIo 句柄等同于 USB-C。
3. 取得本次启动的内存所有权、ExitBootServices 前后的设备树交接；
   只在真机日志证实时推进 XNU CPU0 与 pmap。
4. 最后才允许研究真正的 GPUCC / AOP / SMMU 运行时资源所有者、GMU HFI 和 GPU Fence。

### 不能宣称完成的状态

- EDK2 完整构建：**未验证**；手机 UEFI 已启动：**未验证**。
- USB-C / UART 真正日志：**未验证**；XNU 引导：**未验证**。
- GPUCC 电源控制、SMMU 运行时映射、GMU 启动：**未实现于真实设备**。
- Metal：**未实现**；可刷入 iOS：**不存在**。

Actions 额度保护：本地 `.github/workflows/board-ci.yml` 只有 `workflow_dispatch`；
**不因源码提交自动构建，也没有运行新的 Actions。**
