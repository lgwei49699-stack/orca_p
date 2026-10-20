# MeshLib 内部评估适配层

## 范围

MeshLib 3.1.3.429 作为 Orca 的内部库接入，不再构建 `OrcaMeshLibWorker`，没有第二套程序版本、跨进程参数或中间报告文件。当前只开放显式 CLI 后端；默认 Alpha Wrap、原生 GUI、高级修复及只读证据流程不自动改用 MeshLib。

本目录仅包含第一方适配代码，CMake 不包含、下载或安装 SDK。macOS 发布脚本可为用户已配置的本地 SDK 制作 arm64 / macOS 14.0+ 测试包；Linux GitHub 工作流会下载并校验固定版本 SDK，私有解压后构建 Ubuntu 24.04 x86_64 测试包。构建验证不代表商业再分发授权或公证通过。

## 调用与构建

`ModelReconstruction.cpp` → 同进程后台任务 → `MeshLibAdapter` → `MR::rebuildMesh` → Orca 独立网格复检、几何比较及导出。

- Orca 主体保持 C++17，适配层是同一次 CMake 构建中的 C++20 静态目标。
- 公开头只包含标准库数组、向量、字符串和回调；MR、Eigen、TBB、OpenVDB 及 JSON 类型不穿过边界。
- SDK 头目录、宏、隐藏可见性和禁用 LTO 仅作用于适配目标，避免改变普通切片代码的编译环境。
- `ORCA_MESHLIB_LOCAL_EVALUATION=OFF` 是默认值，构建不依赖 SDK；选择 MeshLib 时明确返回未编入后端。
- ON 要求 64 位及 `SLIC3R_STATIC=ON`。平台代码覆盖 **macOS arm64 ≥14.0、Windows MSVC x64、Linux x64/arm64**；Windows/Linux 仍属实验适配，配置通过不等于已经完成原生编译、依赖 ABI 或运行验收。

本机已有完整 SDK 时，在既有 Orca 构建配置中追加以下选项，再使用原来的 Orca 构建目标；不另建修复程序：

```sh
cmake -S . -B build/arm64 \
  -DORCA_MESHLIB_LOCAL_EVALUATION=ON \
  -DORCA_MESHLIB_SDK_ROOT="/absolute/path/to/MeshLib.framework/Versions/3.1.3.429" \
  -DORCA_MESHLIB_EXTRA_INCLUDE_DIRS="/absolute/path/to/sdk-thirdparty-headers" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0

cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests
```

`ORCA_MESHLIB_EXTRA_INCLUDE_DIRS` 可为分号分隔列表，用于 SDK 没有附带的头文件。本机探针使用固定来源 `tl/expected.hpp` 以及已有第三方头目录；正式依赖交付应进一步固定完整头文件与运行库集合，不能把其他机器的偶然 PATH/头文件搜索结果视为已验证环境。

### Windows / Linux 构建条件

在已经配置好 Orca 原有依赖的对应构建目录追加 `-DORCA_MESHLIB_LOCAL_EVALUATION=ON -DSLIC3R_STATIC=ON -DORCA_MESHLIB_SDK_ROOT=<SDK绝对路径>`，继续构建原有 `OrcaSlicer` 目标，不产生第二个修复程序。

| 平台 | 固定 3.1.3.429 SDK 根目录及条件 |
| --- | --- |
| Windows x64 | 官方匹配 MSVC 工具集的非 IteratorDebug 压缩包解压目录或其中的 `install`；需 `include/MRMesh`、`app/Release/{mr.version,MRMesh.dll,MRVoxels.dll}`、`lib/Release/{MRMesh.lib,MRVoxels.lib}`。多配置生成器另加 `-DCMAKE_CONFIGURATION_TYPES=Release`，所有参与模块保持 `/MD`、`_ITERATOR_DEBUG_LEVEL=0`；不混用 Debug 或 `/MT` 依赖。 |
| Linux x64 / arm64 | 匹配发行版/架构的官方 SDK 前缀（或带 `usr/local` 的解压根），需 `include/MeshLib/MRMesh/config_cmake.h`、`lib/MeshLib/libMRMesh.so` 和 `libMRVoxels.so`。使用 GCC/Clang，Orca 的已知旧运行库依赖须为静态库；不能把 macOS SDK 放入 Linux 构建。 |

完整布局来源为[固定官方版本](https://github.com/MeshInspector/MeshLib/releases/tag/v3.1.3.429)。Windows 使用其 `mr.version`、Unix 使用版本头验证 SDK 版本；`MR::GetMRVersionString()` 可能返回构建日期，仅记录诊断，不用它代替版本验收。版本文件不证明第三方头文件或所有传递依赖兼容。

开启后动态运行库随主程序启动加载。Windows 需要在程序目录或受控 DLL 搜索路径中提供 SDK 及其传递依赖；Unix 构建树通过显式 SDK RPATH 查找。校验实际加载 MRMesh/MRVoxels 的 SHA256，不再要求构建机器上的绝对位置，允许保持内容不变的文件迁移。更改运行库内容（包括重写加载命令、重新签名）后，需要重新确定身份并验证，不能绕过校验。若库缺失，系统加载器可能在 CLI 启动前终止，无法输出结构化报告。

macOS 本地打包使用 `./build_release_macos.sh -sxa arm64 -c Release`：读取既有 MeshLib ON 缓存，将默认目标设为至少 14.0（保留缓存中更高的版本），在构建目录准备完整动态库闭包，独立放入 `Contents/Frameworks/MeshLib`。库副本先重定位和 ad-hoc 签名，再通过 `ORCA_MESHLIB_RUNTIME_DIR` 供 CMake 链接并记录最终 SHA256；原 SDK 及其头文件不变。App / DMG 封装保留这些库的字节，并验证所有非系统依赖可在包内解析、架构及最低系统兼容。清单位于 `Contents/Resources/meshlib-runtime.json`。

已有使用原 SDK 的旧 Mac 包须重新配置和编译，不能只复制 dylib 或仅重新生成 DMG；`-b` 遇到未准备的缓存会拒绝。当前 MeshLib Mac 包仅支持 ad-hoc 本地签名，`--sign` Developer ID 流程会明确拒绝，须另行实现匹配签名身份的编译流程。普通 SDK OFF 包保留现有签名参数；Mac 验证及 GUI 交接见[打包记录](../../docs/ai/validation/2026-10-17-macos-meshlib-packaging.md)。

### Linux 测试包

[Linux 工作流](../../.github/workflows/build-linux.yml) 固定 Ubuntu 24.04 x86_64 和 SDK `3.1.3.429`，校验发行包 SHA256 后解压到私有目录，使用既有 `ORCA_EXTRA_BUILD_ARGS` 启用后端。手动运行仅上传构建/验证产物；既有 `manual-build-linux-*` 标签推送仍走发布路径，不能用推送标签代替本地检查。没有自动触发远程 CI 或修改服务器的步骤。

本地 Linux 已准备好 SDK 和所需依赖时，沿用原构建命令：

```sh
ORCA_EXTRA_BUILD_ARGS="-DORCA_MESHLIB_LOCAL_EVALUATION=ON -DORCA_MESHLIB_SDK_ROOT=/absolute/path/to/sdk -DZLIB_USE_STATIC_LIBS=ON" ./build_linux.sh -si
```

普通 CMake 配置仍默认 OFF；不启用时不需要 SDK。Linux ON 显式选择静态 ZLIB，避免 Ubuntu 默认找到 `libz.so` 后被静态依赖要求拒绝；该参数只随上述 ON 构建传入，不改变 OFF 默认值。Linux ON 的库路径和静态依赖符号隐藏以链接条目穿过 PRIVATE 静态库依赖，构建树使用可传递的 RPATH。适配层以外的代码仍为 C++17，不替换 Orca 自身的静态依赖。

仅隐藏依赖归档还不够：TBB 的头文件内联定义和类型信息也会被编入 Orca 自身对象文件。Linux ON 另生成局部链接脚本，隐藏归属 TBB 的这些符号，避免 SDK 的 OpenVDB 绑定到 Orca 自带的旧版本；不会因普通 Orca 函数的参数包含 TBB 类型就隐藏该函数。脚本作为链接输入经 PRIVATE 静态库链传递，内容不变不更新时间，变化时须重新链接。验收同时检查实际动态绑定与功能回归，不能只用小模型成功证明依赖隔离。

若复用的构建目录已缓存 `libz.so`，首次重配时在上述 `ORCA_EXTRA_BUILD_ARGS` 追加 `-UZLIB_LIBRARY_RELEASE -UZLIB_LIBRARY_DEBUG`，仅清除两项 ZLIB 查找缓存；不需要删除已编译的第三方依赖。全新构建无需追加。

打包脚本收集 MRMesh/MRVoxels 所需的非系统动态库，放在 `lib/meshlib`；glibc、加载器和编译器基础运行库仍由目标系统提供。主程序的包内相对 RPATH 支持直接执行 `bin/orca-slicer`，不要求通过 AppRun。两份 MR 库保持与配置时相同的字节；必要的第三方库路径只在副本中调整，不改原 SDK。`resources/meshlib-runtime.json` 记录依赖和哈希；缺库、加载到包外 SDK 或内容变化均使打包验证失败。

工作流对最终 AppImage 解包搬迁，并暂时隐藏构建 SDK：直接调用真实二进制跑 MeshLib/阶段报告测试，再独立检查 AppRun 启动及重建。`scripts/test_linux_meshlib_package.sh` 可手动复用，参数为 AppImage 与尚不存在的证据目录；调用者须确保原 SDK 不可访问。不把仅文件存在、脚本单测或最小适配探针当作完整安装包通过。

部署必须保留完整解包目录，不能只复制二进制。当前构建面向 Ubuntu 24.04 用户态（含 24.04 容器），不保证可直接在 Ubuntu 22.04 宿主运行；服务器只读核对、实际测试范围和 GUI 待验清单见 [Linux 打包记录](../../docs/ai/validation/2026-10-19-linux-github-packaging-review.md)。

### 服务端调用

公共接口保持 `--reconstruct-model --reconstruction-engine meshlib --reconstruction-voxel-mm <毫米>`，可选 `--reconstruction-memory-mb <128至4096的整数>` 显式覆盖内存预算。默认行为不变，不需要旧调用方增加参数。对 MeshLib 这是整个 Orca 进程 RSS 的协作停止阈值，不是 SDK 增量内存或硬上限；对 Alpha Wrap 仍是其 worker 预算。资源配置由调用方结合服务器额度设置并留余量，本轮不自动读取 cgroup/Job 上限、不新增任务调度器或信号处理。

重建在 GUI 初始化前分流，但应用可能仍链接图形运行库。部署验收须在目标系统无 DISPLAY、普通用户权限下验证依赖加载、中文路径、连续作业和失败恢复；内部取消回调测试不代表 SIGINT/SIGTERM 保证输出报告。

## 性能和安全边界

1. 同进程移除进程启动和 IPC 文件读写，但不保证重建算法本身更快。适配层将有界三角网格编码到内存 STL，保留已验证的 SDK 焊接/复制非流形顶点行为；临时缓冲为 `84 + 50 × 输入面数` 字节，SDK 加载完成后释放，没有落盘。
2. 回调返回 false 请求协作取消；回调抛出的异常在进入 SDK 线程池边界内被捕获，不向 SDK 展开。调用方负责资源准入估算、同进程 RSS 观察和截止时间；已有进程级 `RunningGuard` 在后端分流前限制同时只执行一个重建任务，不增加第二套排队机制。
3. 同进程没有硬内存上限、任意代码硬超时或崩溃隔离；SDK 或 CGAL 不可中断阶段可能延迟取消，不能安全强杀工作线程。SDK 缓存和动态库也不保证任务完成后立即卸载。
4. 固定 SDK 版本及实际加载 MRMesh/MRVoxels 的 SHA256 用于身份核对，实际位置记录在报告中；不验证所有传递依赖，也不构成对不可信动态库的沙箱，因为加载器可能在校验前执行原生初始化代码。
5. 输入面被 SDK 跳过、输出超限、非有限坐标、回调失败或引擎异常均不交付候选。SDK 成功只表示候选生成；Orca 的完整检测通过后才允许导出，不代表形状、间隙、内部结构或打印质量已保证。

## 依赖审查与实际探针

此前 macOS 探针的 Orca 本地依赖为静态 TBB 2021.5（接口 12050）、OpenVDB 8.2、Boost 1.84、OpenSSL 1.1.1w；该平台 SDK 使用自己的 TBB 12.18、OpenVDB 13 等动态库。本轮 Ubuntu 24.04 SDK 实际使用 OpenVDB 10.1，依赖版本不能跨平台套用。macOS Mach-O 两级符号命名空间和私有适配边界不能直接推广到 Linux ELF 符号抢占或 Windows CRT/DLL 环境。

Linux 配置针对已知的 Orca 静态依赖 archive 使用 `--exclude-libs`，减少旧运行库符号从最终程序导出；不对全部应用符号做隐藏，不把该选项视为完整 ABI 证明。目标平台仍需核对实际 `.dynsym` / `DT_NEEDED` / 运行期符号绑定，回归普通切片和 MeshLib。Windows 配置拒绝已知 `/MT` / Debug STL 混用，但不能自动证明任意预编译依赖都使用正确 CRT；必须与选定 SDK 配套验证。

此前 macOS 独立链接探针实际验证：

| 探针 | 结果 |
| --- | --- |
| C++17 host + 原静态 TBB + C++20 SDK 桥接，方块重建 | 12 面 → 218 面；host 的 TBB 接口仍为 12050 |
| 同进程真实 Q 模型，完整适配代码 | 59,924 面 → 79,494 面、39,679 顶点；跳面 0 |
| Q 适配层单次计时 | 加载约 23 ms、重建约 5,104 ms、适配层合计约 5,160 ms |
| false 回调及抛异常回调 | 分别返回 cancelled / callback_error，不交付候选；实际 SDK 正进度回调内抛错也已验证 |
| 默认 OFF CMake 目标 + C++17 host，最低系统 11.3 | 构建与调用通过；仅依赖系统 libc++/libSystem，无 SDK 动态库 |

这些是局部真实链接/接口探针，不是完整应用、全平台、性能分位数或打印验收。完整 Orca 测试和 Computer Use 结果由任务主记录另行登记。

2026-10-19 本机 Ubuntu 24.04 amd64 Docker（macOS/Rosetta）实际编译当前适配目标，7 项 SDK 探针通过；将真实依赖闭包打包、搬迁并隐藏原 SDK 后，直接执行包内 ELF 的同样 7 项通过，MR 库身份校验通过。它证明适配编译及包内依赖加载，不代表完整 Orca 的旧 TBB/OpenVDB 共存、最终 AppImage、普通切片/分盘或 GUI 已验收。详见 [Linux 实测记录](../../docs/ai/validation/2026-10-19-linux-github-packaging-review.md)。
