# 构建、打包与平台集成

<!-- module-review
{
  "id": "build",
  "source_fingerprint": "sha256:08cdeb35ff5abc9790d741e205a2ad94f4d154d3f6cbb6c0f71d6902e6b6e0ab",
  "reason": "在linux/release2_3_2合并bc6684f180，保留发布分支工具链绑定依赖缓存、3G ccache、source_ref、并发控制、180分钟预算和无PCH诊断构建；叠加MeshLib ON静态ZLIB、运行库闭包及包内回归，未恢复旧宽泛缓存。YAML与12段Bash语法、53项工具测试中的50项通过/3项Linux专属跳过，实际远端构建和最终包验收另列。"
}
-->

## 职责与边界

本模块负责构建、打包与平台集成。归属以[模块清单](../modules.json)的 `build` 为准。

## 入口与调用

- [CMakeLists.txt](../../../CMakeLists.txt)：根构建选项、平台依赖与子目标装配；由配置阶段进入。
- [CMakeLists.txt](../../../src/CMakeLists.txt)：装配应用可执行目标、链接依赖和平台资源。
- [version.inc](../../../version.inc)：定义应用名称、标识及版本；由 CMake 引入，产品版本与资源包版本分开维护。
- [build_release_macos.sh](../../../build_release_macos.sh)：macOS 的依赖、架构、构建配置和打包命令入口；按[验证规范](../validation.md)选择必要目标并确认脚本的依赖／打包范围。
- [build_macos_zstd.sh](../../../scripts/build_macos_zstd.sh) 与 [bundle_macos_zstd.sh](../../../scripts/bundle_macos_zstd.sh)：固定运行库构建、架构/最低系统验证与包内重定位；构建入口和独立 DMG 入口共用。

## 核心流程

`SliceAnalysis.cpp/.hpp` 加入现有 libslic3r，`test_slice_analysis.cpp` 加入现有 fff_print_tests；复用 JSON/Boost 与现有切片数据，不新增 SDK、独立进程、依赖或打包选项。本次仅最小应用/测试目标验证，平台和 GUI 状态见[实施记录](../validation/2026-10-18-slice-analysis.md)。

统一名义连通集合测量的 `SliceAnalysisGeometry.cpp/.hpp` 编入现有 `libslic3r_cgal`，复用项目已链接的 CGAL/GMP/MPFR。CGAL 类型与浮点舍入编译选项不进入普通 SliceAnalysis 目标，对外提供隐藏几何实现的Region及Boost有理数；整数输入不依赖平台long宽度或先转double。无新依赖下载或平台分支；Mac实际构建与Windows/Linux待验证状态分别记录。

`ModelStageReport.cpp/.hpp` 加入既有libslic3r目标，复用已链接的CGAL、OpenSSL与Boost，不新增程序或SDK依赖。只在显式CLI报告中调用；默认SDK OFF/ON及GUI链接边界不变。

`src/meshlib` 是根CMake管理的内部静态适配库，不再构建独立MeshLib程序。默认 `ORCA_MESHLIB_LOCAL_EVALUATION=OFF` 编译C++17无SDK桩；ON只将适配实现编成C++20并私有包含SDK头，链接固定3.1.3.429的MRMesh/MRVoxels，主Orca业务仍C++17。关闭该目标LTO、隐藏内联符号，避免SDK头污染旧TBB/Eigen/OpenVDB；实际macOS两级命名空间链接探针通过不代表其他平台ABI兼容。

ON要求64位和Orca静态依赖：macOS arm64显式最低14.0；Windows MSVC x64使用匹配工具集的官方非IteratorDebug SDK、Release系配置及/MD，拒绝/MT、/MDd和非零iterator ABI；Linux x64/arm64支持官方include/MeshLib及lib/MeshLib布局，针对已知静态依赖archive隐藏导出，防止旧符号干扰SDK，但此配置不证明ELF绑定安全。Windows/Linux输出实验提示，需目标平台编译运行验收，不能将配置测试写成原生通过。OFF保留原平台/最低版本。

SDK版本校验：Unix读固定config_cmake.h，Windows读完整发行目录app/Release/mr.version；MRMesh/MRVoxels运行库内容哈希进入私有模板，不再固化构建机绝对路径。Windows运行期使用宽字符WinAPI按函数地址定位DLL；Unix使用dladdr，适配层启用PIC。哈希不覆盖所有传递依赖，也不是加载沙箱。直接 CMake 的 Unix 评估构建使用构建树 SDK RPATH，Windows运行需要在受控DLL搜索路径提供配套依赖；缺库仍可能早于CLI解析失败。macOS 发布脚本现在支持用户已配置 SDK 的本地测试包：先处理库副本，再链接及记录最终哈希，不修改原始 SDK、不下载/安装 SDK、不创建服务端镜像。打包成功不代表商业授权或公证完成。具体构建/迁移边界见[适配层说明](../../../src/meshlib/README.md)。仍由同一Orca调用SDK，无额外修复程序；默认Alpha Wrap不变。

Linux ON 的 RPATH/静态 archive 符号隐藏通过 INTERFACE 链接条目穿过 PRIVATE STATIC 依赖，兼容既有 CMP0099 行为；`--disable-new-dtags` 使构建树间接 SDK 库也能解析。仅将 SDK 前缀和库目录传给 Linux 打包模板，OFF 提前返回。`package_meshlib_linux.py` 从两份 MR 库递归收集真实依赖至 `lib/meshlib`，保留 SONAME 与许可；glibc/加载器/编译器基础库仍由目标系统提供。仅主程序及带路径的非 MR 副本重定位，两份 MR 内容不变；清单校验哈希及实际加载路径。支持服务器直接执行 `bin/orca-slicer`，不只依赖 AppRun；ON 不再对 ELF 做全局 `/usr` 字节替换，OFF 保留旧处理。封装失败直接退出，不继续发布残缺包。

完整 Linux 包的真实绑定核查发现，TBB 头文件在 Orca 自身对象中生成的弱定义不属于上述 archive，SDK OpenVDB 会把同名函数/RTTI 绑定到主程序。因此 Linux ON 另生成局部 VERSION 链接脚本，按 Itanium 名字的所属 TBB namespace 隐藏函数、类型信息、vtable 和相应 thunk，不按签名中任意位置出现 `tbb` 来隐藏业务导出。脚本作为实际链接文件经 INTERFACE 传递，既保留 CMP0099 OLD 的 PRIVATE STATIC 链路，也让内容变化触发重链；file(GENERATE) 避免无变化的 configure 重写文件。只对 Linux 的手工 RPATH 条目引用目录，支持 SDK 路径空格；OFF/Mac/Windows 不增加脚本或改变链接语义。真实合成 ELF 回归覆盖跨库函数/RTTI 抢占的前后对照、普通/虚继承/协变返回 thunk 隐藏、Slic3r 含 TBB 参数/模板的导出保留、OFF 无脚本及改脚本重链；Linux 配置 27 项及打包 26 项通过，不能代替最终完整 Orca 的动态绑定和运行验证。

Linux GitHub 工作流固定 Ubuntu24 x64 SDK 3.1.3.429，下载验 SHA 后私有提取，通过原 `ORCA_EXTRA_BUILD_ARGS` 显式 ON，并传入 `-DZLIB_USE_STATIC_LIBS=ON`，避免完整应用的 `find_package(ZLIB)` 默认选择共享库后被静态依赖校验拒绝；OFF 构建默认值不变。手动运行只上传产物，既有标签发布须先通过隐藏 SDK、搬迁最终 AppImage 后的 CLI 测试。本轮未触发远程工作流；本机 Docker 已完成完整应用及 AppImage 构建，并验证隐藏 SDK 后搬迁 AppDir 的 42 项 MeshLib、34 项阶段报告、10 项切片报告以及 AppRun 实际重建。局部 TBB 链接修正前后普通 cube 自动摆盘切片的全部打印指令、有效配置、层数、耗材及排除执行耗时后的原结果一致；立即绑定与真实重建均无已确认的 SDK→主程序 TBB 符号抢占。上述范围不覆盖原生 x86_64 的 AppImage 包装头启动（本机仿真使用原字节手工解包）、GUI、全部模型/多盘及用户暂缓的固定位置切片问题；具体身份与证据见[Linux记录](../validation/2026-10-19-linux-github-packaging-review.md)。部署须保留整个解包目录，不能只替换主程序，也不承诺在 Ubuntu22 用户态直接运行。

ZLIB 配置回归单独覆盖 Linux 系统 `FindZLIB` 的真实导入目标：ON 默认选共享库须拒绝，ON 显式选静态库可配置，OFF 仍允许默认共享库；同时检查工作流实际传入的开关。2026-10-19 补丁后配置专项在 Ubuntu24 amd64 上 26/26 通过；macOS 同专项 25 项通过、1 项 Linux 原生测试跳过。此前只创建 OpenSSL/TBB 目标的配置夹具与最小适配运行探针未覆盖完整应用的 ZLIB 选择，不能将这些通过结果视为完整 Orca 构建通过。

只读 `ModelRepairEvidence.cpp/.hpp` 编入现有 libslic3r；`test_model_repair_evidence.cpp` 和 `test_mesh_slice_diagnostics.cpp` 注册到现有 libslic3r_tests。证据由默认关闭的非动作选项 `--repair-evidence` 接入：仅检测组合高级修复的残缺结果可能追加单 Z 切层和同轮有效区域观察；普通修复／重建只投影已有报告。默认协议与扫描路径保留，不新增自动编排、GUI 菜单、依赖或 worker。现有高级修复、重建和构建目标保持，应用与测试必须使用同一源码身份；macOS 构建通过不代表 Windows/Linux 已运行验证。

根 CMake 配置依赖与目标，src 子目标链接 libslic3r、GUI 和开发工具；平台脚本组织依赖、应用与打包，dev-utils/platform 提供图标、manifest、plist 和桌面集成。deps/deps_src 是供应链边界。

独立重建的 ModelReconstruction、MeshReconstruction、ReconstructionProcess 注册到 `src/libslic3r/CMakeLists.txt` 的现有库目标，候选参数／对比窗口注册到 `src/slic3r/CMakeLists.txt`。worker 直接启动当前应用并走内部命令分流，没有另一个常驻服务或新插件，也不复用 `open`／detached GUI 启动。Alpha Wrap 使用现有 CGAL 头文件依赖；CGAL 版本未变，GMP 平台定向修正如下。

10-06 缺陷优先重建压力测试复现 GMP 6.2.1/macOS arm64 汇编 x18 兼容异常。获用户授权后，[GMP 配方](../../../deps/GMP/GMP.cmake)仅针对 macOS arm64 目标固定使用官方 GMP 6.3.0 源码，SHA-256 `a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898`。按 `CMAKE_OSX_ARCHITECTURES` 选择，未指定时才取 `CMAKE_SYSTEM_PROCESSOR`，因此 Intel→arm64 也升级、arm64→Intel 不升级。Linux 和 macOS x86_64 仍为原 6.2.1 URL/hash/GCC15 补丁，Windows MSVC 仍复制原 `.lib/.dll`；不升级系统 Homebrew、不复制其库到项目，也不改变几何策略。6.3.0 在当前 Apple Clang/static 配置下使用未打补丁的上游源码；未泛化到其他编译器或 universal 构建。复现、正式构建和回归状态见[主记录](../validation/2026-10-04-smart-print-engine.md)最新节。

## 配置与约束

`linux/release2_3_2` 合并功能分支时保留既有构建加速：依赖安装目录缓存键绑定工具链与源码，精确命中才跳过依赖编译；下载缓存独立保留前缀回退，ccache 为 3G。应用通过 `-sip` 一次构建并封装、关闭预编译头，`ORCA_NINJA_KEEP_GOING=1` 只收集更多编译错误，不吞掉失败。手动 `source_ref` 只选择检出源码，不更换本次运行的工作流；测试本次合并时选择此发布分支并将该输入留空。既有全局并发组会取消旧任务（含标签任务），避免同时预热和发布。手动运行只上传产物，匹配标签的 push 才创建 Release。冲突处理及当前验证边界见[Linux记录](../validation/2026-10-19-linux-github-packaging-review.md)。

本次公开版本为 `2.3.2.3-Release`，用于识别固定位置切片修复；内部兼容版本不变。
`.github/workflows/build-linux.yml` 在上传 AppImage／创建 Release 前解压刚生成的包，清除显示服务环境变量，
调用包内 AppRun 执行 `test_cli_fixed_position_slice.py`，再复用其自带参数执行 `test_cli_oversized_arrange.py` 验证单/多盘JSON及实际3MF一致性。没有 G-code、分盘返回错误或任何回归失败即停止发布，
另以 always 步骤保存测试证据、AppImage 和包内可执行文件 SHA256。原有文件存在检查只代表文件生成，
不再宣称可部署。该门槛针对此工作流；本地编译通过不代表 Linux 镜像或业务配置已验证，状态见
[固定位置切片修复记录](../validation/2026-10-19-cli-fixed-position-slicing.md)。

此前 Windows GFD 模态退出修复曾将 `version.inc` 的 `SoftFever_VERSION` 更新为 `2.3.2.2-Release`；当前版本以上述 `2.3.2.3-Release` 为准，内部兼容版本 `SLIC3R_VERSION=01.10.01.50` 保持不变。版本更新需重新生成版本头及应用元数据，旧运行／打包副本不会自动变为新版本。Mac 用户日志已确认取消及下发成功后的模态收尾；Windows 系统验收仍待同步新源码后执行。

架构、生成器、Release 配置和依赖前缀必须匹配。应用版本与资源包版本独立。Windows 动态库、macOS rpath/zstd 和 Linux 分发标识是不同打包责任，不能只改一个文件名推定完成。

若既有 `build/arm64` 缓存启用了 `ORCA_MESHLIB_LOCAL_EVALUATION=ON`，发布脚本自动选择至少 macOS 14.0，保留缓存中更高版本；显式 `-t` 低于 14.0 提前拒绝，不关闭后端。普通 SDK OFF 构建仍默认 11.3。本地维护台的应用构建和 CI 命令显示同一 `-t` 参数，第三方依赖任务不改变默认。此前默认 11.3 覆盖 ON 缓存的失败原因见[构建恢复记录](../validation/2026-10-17-cli-oversized-arrangement.md)。

[scripts/macos_runtime_bundle.py](../../../scripts/macos_runtime_bundle.py) 从 MRMesh/MRVoxels 递归收集非系统依赖，校验 arm64 和目标最低版本，在 `build/arm64/meshlib-runtime/MeshLib` 准备副本：ID 使用 `@rpath/MeshLib/`、内部依赖使用 `@loader_path/`，清除 SDK 自带开发机 RPATH，再逐库 ad-hoc 签名。CMake 的 `ORCA_MESHLIB_RUNTIME_DIR` 指向这些最终文件，原有运行时 SHA256 检查保留。`-b` 只接受已配置的准备目录和匹配最低版本，否则要求重新配置。

构建完成后复制闭包至 `Contents/Frameworks/MeshLib`，避开应用自己的同名 zstd；清单存于 Resources。App/DMG 收尾清理主程序构建 RPATH，递归验证每个非系统加载依赖的包内路径、架构、最低版本及清单哈希，缺库/逃逸路径/旧 SDK 引用均失败。MeshLib 包封装只签外层 App，不以 `--deep` 重签已哈希的子库，再严格验证完整签名和库哈希。当前 MeshLib 仅支持 ad-hoc 本地包，Developer ID 参数会明确拒绝，不能对库重签后沿用旧编译身份；普通 OFF 包保留原签名流程。实际验证与维护台操作见[打包记录](../validation/2026-10-17-macos-meshlib-packaging.md)。

Mac 应用构建先在 `build/<arch>/macos-zstd/<最低系统版本>` 构建既有固定版本 zstd 1.5.7，
每次校验源码压缩包 SHA256，保留已解压源码时间戳以支持增量编译；未改 Homebrew 或第三方快照。
配置显式传 `zstd_DIR:PATH`，避免 Boost iostreams 的传递依赖再发现高系统版本 Homebrew 库。
离线仍可用 `WISEBEGINNER_ZSTD_ARCHIVE`。`-b` 只接受已选择对应兼容库的缓存，否则要求去掉
`-b` 重新配置。直接调用 CMake 的新构建目录仍需显式选择兼容 zstd 前缀，不自动下载。

生成的 `build/<arch>/OrcaSlicer/*.app` 将 zstd 复制到 Frameworks，改用包内引用并带许可；
先把 Resources 软链接变为副本，资源根目录为 0755 以便其他用户读取，避免修改仓库资源，
再对完整包做 ad-hoc 签名及严格完整性检查。
universal 的两次应用编译共用包含双架构的库，lipo 合并后重新验证和签名。独立 DMG 打包仍可
处理旧的外部绝对路径依赖，并验证已经包内化的库；临时签名不等于 Developer ID 签名或公证。

DMG 的“安装与首次启动说明.txt”由 `scripts/macos-install-guide.txt` 按应用名替换生成，按用户要求仅保留中文简短三步。
第一步为全局“任何来源”，后两步为单应用“仍要打开”和有条件的隔离标记处理；保留用户在系统设置确认的步骤。
按用户要求删除首步版本括注及恢复保护段落，未修改命令或系统行为。
打包过程不执行这些命令，不自动改变系统安全设置；说明更新只需重新打包，不需重编译应用，旧 DMG 不会自动更新。
普通用户包默认只有主应用、Applications 链接和说明；`--include-validator` 仅在需要开发预设校验工具时显式传入。

`CompilerWarningCompatibility.cmake` 先探测枚举诊断支持，支持才给 C++ 添加降级选项；
Apple ld 的重复库告警通过链接器探测后才关闭，不重排或删除传递静态库，其他警告继续保留。
旧 CMake（低于 3.18）不尝试该链接选项；非 Apple 平台不施加 Apple 链接器选项。

旧 arm64 依赖缓存不会因 `build_release_macos.sh -s`（仅应用）自动升级，须先在对应依赖构建目录执行 `cmake --build deps/build/arm64 --target dep_GMP`，再重新链接应用/测试，或走包含依赖的完整构建。现有 universal 脚本按 arm64/x86_64 分开构建后合并；本次只验证 arm64 产物，没有验证 universal 打包。

## 失败与取消

编译成功、链接成功、打包成功和目标系统可运行分开记录；警告不能冒充构建失败，旧二进制也不能证明新源码通过。用户已有新构建时复用；缺少当前验证所需产物时由 AI 补齐必要目标，详见[验证规范](../validation.md)。

Windows GFD 设备选择的循环辅助实现注册在 `src/slic3r/CMakeLists.txt`，仍编入现有 GUI 库。`SLIC3R_GUI_TESTS=ON` 额外注册独立 `gfd_modal_dialog_tests`，仅编译辅助实现并链接 wx core/base；不为测试链接整套切片／云端业务。该开关默认关闭，运行需交互桌面。`.gitattributes` 仅为本次两个 Windows 诊断 CMD 固定 `eol=crlf`，保证同步代码后的启动器换行格式，不改变其他文件的检出规则。

## 修改与验证

`test_macos_build_compatibility.py` 以小型原生 Mach-O 测构建入口、缓存、校验工具、双架构与签名，
并实际探测 CMake 告警选项；替换昂贵的源码/应用构建，不把夹具通过视为完整应用各架构验收。
实际工具链编译与配置阶段的版本/能力模拟分开：覆盖 AppleClang 14、15 的门槛、15.0.1/16、
Clang 支持/不支持及非 Clang 拒绝分支；模拟选项不用于编译，不冒充旧工具链真机验证。
真实 arm64 构建、包内路径/最低系统、CLI 和剩余限制见 [Mac 验证记录](../validation/2026-10-08-macos-runtime.md)。

修改 CMake／打包脚本先静态检查，再按当前分工构建受影响目标；打包改动需要验证实际打包流程，普通业务改动不默认执行全平台打包。交给用户 GUI 验证的应用路径与资源必须匹配本次构建；跨平台 Python 脚本测试不等于 Windows 安装包验收。

现有测试/工具范围：`scripts/tests/test_bundle_macos_zstd.py`、`scripts/tests/test_gmp_platform_selection.py`。GMP 测试调用真实配方的条件分支，只替换构建注册函数，覆盖 native/cross Apple、Linux 和 MSVC，不下载或编译依赖；运行时稳定性另由项目库自检和重新链接后的几何/修复回归证明。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

重建当前使用本仓库 `build/arm64` 的 Release 配置；交接以 `build/arm64/src/Release/OrcaSlicer.app` 的实际新链接产物和匹配资源为准，不能启动尚未更新的 `build/arm64/OrcaSlicer/OrcaSlicer.app` 打包副本。编译／测试状态和 hash 在[主记录](../validation/2026-10-04-smart-print-engine.md)，GUI 启动命令在[交接清单](../validation/2026-10-05-reconstruction-gui.md)。本轮不因为增加 worker 就宣称 Windows/Linux 打包、进程限制、中文路径或商业发行已经验收。

## 分支现状

2026-10-19 核对既有 GitHub Linux 打包入口：指南指向另一工作目录的 `linux/release2_3_2`，远端最新 Linux Tag 为 `manual-build-linux-20260923-01` / `e5640e1a5`，构建与发布成功，但尚无本轮越界 JSON 修正和 MeshLib 接入。该历史工作流不启用 SDK、只检查 AppImage 文件；当前开发分支已补齐上述 ON 构建/打包验证入口，尚未远程执行。不能将旧成功记录当作当前 master 或 MeshLib ON 的 Linux 运行验收。源码、Tag、Run 和后续范围见[核对记录](../validation/2026-10-19-linux-github-packaging-review.md)。

本分支保留 OrcaSlicer 品牌；Windows 动态库/CGAL 处理与用户版并不全同。此前 Mac zstd 调整未修改其他平台；本轮 Linux MeshLib 打包独立记录，不改变 Windows 运行库及打包脚本。

`gfd_cloud_config_tests` 注册在 tests/slic3rutils，只包含纯JSON协议及Catch入口、链接 test_common/nlohmann_json，无需链接GUI库或启动桌面。应用增加头文件GFDCloudConfig.hpp，域名、版本号、打包配置不变。构建和界面待验见[云方案记录](../validation/2026-10-09-cloud-config-quality-analysis.md)。

## 关联模块

[公共运行时与平台辅助](platform.md)、[测试基础设施与维护工具](test-tools.md)、[中英文与翻译资源](localization.md)、[打印机、耗材与工艺资源](resource-profiles.md)。

`GFDCloudConfigDialog.cpp/.hpp` 编入现有GUI库，复用保存/上传进度和模态收尾。`SLIC3R_GUI_TESTS=ON` 新增独立 `gfd_cloud_config_dialog_tests`，仅链接wx core/base；需要桌面，不连接云端。第六批Mac arm64 Release应用及辅助测试构建通过，第六批36项生命周期检查与CLI --help通过；提交前重登边界修改另按云方案记录重建验证；实际表单/云写入及Windows待验，见[云方案记录](../validation/2026-10-09-cloud-config-quality-analysis.md)。
