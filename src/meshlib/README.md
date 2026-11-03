# MeshLib 内部评估适配层

在需要补洞的显式重建中，每个距离场分块再按每边至多 8 个采样点划分。适配层对固定 SDK 的绕数树计算整个小块可能的绕数范围。叶子范围使用三角形有向立体角，内部节点覆盖 SDK 可能选择的近场与远场结果。范围严格处在阈值同一侧时共享符号，距离仍逐点用 SDK 投影。范围跨阈值、数值无法包住或遇到不连续处时使用原 SDK 逐点求值。已验证闭合表面的近表面采样沿用 SDK 路径。SDK 升级时须用 `meshlib_winding_tile_parity` 及真实候选对照复核角度、归一化和树遍历规则。见[Mac 性能验证](../../docs/ai/validation/2026-10-31-cgal-621-upgrade.md)。

## 范围

MeshLib 3.1.3.429 是 GUI 和 CLI 唯一的重建引擎。Alpha Wrap 算法、预封口/粗化、专用 worker 和参数已删除。高级修复及其他几何算法仍使用 CGAL，普通导入/切片不会自动重建。

本目录仅包含第一方适配代码，CMake 不包含、下载或安装 SDK。macOS 发布脚本可为用户已配置的本地 SDK 制作 arm64 / macOS 14.0+ 测试包；Linux GitHub 工作流会下载并校验固定版本 SDK，私有解压后构建 Ubuntu 24.04 x86_64 测试包。构建验证不代表商业再分发授权或公证通过。

## 调用与构建

宿主复用已有完整检查：边界、非流形边、面方向冲突、重复面和退化面均为0的闭合输入传入 `close_holes=false`，SDK省去补开口需要的远场计算，绕数、体素和简化策略保持。其他输入及直接适配器调用默认仍补开口；不增加检查扫描、公开参数或重试。闭合输入中的相交和封闭内腔仍用绕数处理。

`ModelReconstruction.cpp` → 同进程后台任务 → `MeshLibAdapter` → 单次 SDK 距离采样/`MarchingCubesByParts`/预留容量的串行拓扑构造 → 四区域串行简化及全网格交界处理 → Orca 独立网格复检、几何比较及导出。

宿主仅在显式重建的私有副本中复用高级修复的精确重复面整理及邻面定向，避免相反重复孔壁抵消绕数；不填孔、不按距离焊接、不移动顶点。原件保留用于比较和撤销，报告记录清理面数。SDK严格装载检查针对已整理的输入，仍不允许静默漏面。

显式重建固定使用 HoleWindingNumber，避免 Auto 将存在重合/非流形结构的输入判为可安全使用 ProjectionNormal。保留输入体素和 0.5 绕数阈值；关闭预细分，直接按输入三角面计算。radio 与程序化开放穹盖实测中，SDK 预细分后的近似绕数会产生原件没有的空腔/通道，仅禁止翻边形变仍不能解决。

绕数近似系数使用共享常量 3，报告和 SDK 设置取同一值。它是计算近似系数，不是体素分辨率，也不是严格误差上界；更大的值不保证每个阈值分类都单调改善。单因素对照中，2 和 3.5 均因新增空腔被否决；3 通过了完整网格、孔槽及扩展穹盖对照，并消除了原系数 4 在密集穹盖上新增的微小空腔。正式 Mac 构建与 GUI 的当前验收状态见[本批记录](../../docs/ai/validation/2026-10-31-cgal-621-upgrade.md)。不按模型切换系数，不自动重试；既有协作预算和人工确认保持。

SDK marching-cubes 提取后不做角度翻边；随后用 SDK 简化保留已有顶点位置、边两端度数均为 3 且四顶点坐标互异时禁止塌缩，避免最小闭合块被压为两张重合面；四顶点中存在坐标精确重合时，允许已有 SDK 简化清理塌陷壳，不按尺寸或体积删除真实小件；简化前局部检查新面之间及新面与固定邻面之间的共面覆盖、非共面穿越，复用SDK三角相交谓词；对5顶点闭壳缩成四面体额外保持有向体积的符号和非零性，防止极小内腔翻转。仅阻止不安全折叠，不删除组件、移动输入或修改SDK；不代替最终全模型自交及实体检查。沿用 SDK 的 `0.25 × voxel` 误差指标与 `1e-5` stabilizer，串行执行，不强制折叠小边。这是简化的误差指标，不是表面距离的严格上界。同一完整网格按初始面ID分四区，先保留区域边界进行内部简化，再全网格处理交界；全部阶段共用同一初始QEM及累计误差，减少同时存活的折叠队列，不切开/拼接或重复重建。候选三角划分可能变化，仍需完整检查与孔槽回归。简化后不再调用SDK的全网格pack；导出时已有的有效面遍历统一生成紧凑索引，输出上限及预留按有效顶点计数，已删除的存储槽不计为输出。该清理不降低体素或放宽面数上限，也不宣称降低简化早期的内存峰值。

拓扑构造先释放 marching-cubes 临时对象，再调用 SDK `MeshBuilder::addTriangles`，按闭合三角面预计的 `3×面数` 预留半边容量，避免并行构造的临时副本与反复扩容。它只是容量预留，不是删面或数量上限；实际添加数必须等于采样面数，否则拒绝候选。串行编号可改变后续简化结果，不能承诺与旧并行候选字节一致。SDK 构造调用期间仍是协作取消，不是硬中断。完成后释放私有输入及缓存，再执行原有受约束简化；GUI/CLI共用此实现，体素、绕数、预算与完整检查不变。当前诊断包的252175 GUI两次完成，但首轮峰值接近2GiB，正式包GUI及更广资源边界以本批记录为准。

重建的毫米坐标包含原模型旋转/缩放，不叠加摆放平移，避免 float 顶点精度随打印床位置变差。候选仍以原完整变换检查实际摆放坐标，原对象位置不改；报告记录排除的平移向量，实际最小 Z/抬升提示仍在打印床坐标系。

两阶段共用协作取消/资源预算，分别记录耗时；没有重试重建、增大体素、按模型 ID 分支或删除小件。六个真实样例及已知孔道/薄壁对照结果见[核心优化记录](../../docs/ai/validation/2026-10-31-cgal-621-upgrade.md)。网格通过仍不表示原设计结构完全保留，采样偏差及人工确认规则不变。

显式重建按 Z 分块供给 SDK `MarchingCubesByParts` 距离场。采样调用原 SDK `meshToDistanceFunctionVolume`，保留体素中心的浮点运算顺序；相邻块重叠一层，复用上一块共享采样而不重复计算，由 SDK 连接网格。主浮点块以 32MiB 为目标，至少包含两层 XY 平面，另保存共享层；这不是进程内存上限。闭合输入也使用分块，避免 SDK 每个网格线程分别缓存完整 XY 平面的内存开销；仍保留 `close_holes=false` 的窄表面带计算，并未重新开启补洞远场。体素、绕数精度、阈值、原三角面、受约束简化及最终检查不变；分块可能改变网格编号和简化结果，不承诺候选 STL 逐字节一致。较细体素仍可能超预算，SDK 网格提取还保留全网格位集；内存降低也可能有耗时代价。GUI 与 CLI 共用此实现，普通切片不进入该路径。当前 Mac 回归和细节边界见同一验证记录。

宿主准入估算同时计入全网格两份逐层位集、至少两层XY距离块及共享层；只按表面积会低估较细体素的大包围盒请求。部分层可为空，估算仍非峰值保证，运行中继续监测整应用内存。显式超预算请求在进入SDK前拒绝并保留原件，仍由用户选择精度，不自动调参。

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

公共接口为 `--reconstruct-model --reconstruction-voxel-mm <毫米>`，仍兼容服务端的显式 `--reconstruction-engine meshlib`。CLI 必须给出体素尺寸；GUI 显示可编辑的 0.1 mm 初始值，不代表所有模型都适合。旧 Alpha 引擎/参数按不支持拒绝。SDK-OFF 的 GUI 提示不可用，CLI 返回失败；不回退其他算法。可选 `--reconstruction-memory-mb <128至4096的整数>` 覆盖整个 Orca 进程内存的协作预算（macOS footprint、Windows working set、Linux RSS），不是 SDK 增量内存或硬上限。

重建在 GUI 初始化前分流，但应用可能仍链接图形运行库。部署验收须在目标系统无 DISPLAY、普通用户权限下验证依赖加载、中文路径、连续作业和失败恢复；内部取消回调测试不代表 SIGINT/SIGTERM 保证输出报告。

## 性能和安全边界

1. 同进程移除进程启动和 IPC 文件读写，但不保证重建算法本身更快。适配层用 `MeshBuilder::addTriangles` 串行构造私有 SDK 输入，每个三角面使用独立的三个顶点，按每面6条半边预留，避免自动并行构造的临时分块/拼接副本。逐面保持输入坐标和绕序，避免 SDK STL 加载器再次焊接非流形顶点扇时跳面。不再编码、解码内存 STL；原模型的拓扑、属性和坐标不变。报告 `input_mode=independent_triangles`，`loaded_vertices` 为输入面数的三倍，仍核对实际载入面数与跳面数。输入复制循环检查取消，SDK构造返回后再次检查；该调用仍不可硬中断。约199万面的输入阶段对照峰值约819→522MB、构造约65→129ms，不代表完整重建峰值降低同样数值或整体更快。这个表示仍比共享顶点占用更多空间；沿用原有准入、整个进程的协作预算和取消机制。
2. 回调返回 false 请求协作取消；回调抛出的异常在进入 SDK 线程池边界内被捕获，不向 SDK 展开。`MarchingCubesParams.cb` 接入同一个 Progress，使 addPart 和 finalize 内部的取消点也能收到停止请求；采样仍报告原有进度，finalize 使用 0.72–0.8 段。阶段标志只在同步 addPart 全部返回后切换，不与 SDK 工作线程并发修改。调用方负责资源准入估算、同进程内存观察和截止时间；已有进程级 `RunningGuard` 在后端分流前限制同时只执行一个重建任务，不增加第二套排队机制。拓扑构造等不可中断调用仍需等待返回。
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
