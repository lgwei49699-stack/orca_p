# Linux GitHub 打包与 MeshLib 接入核对

## 当前验证结果（2026-10-19，完整应用与搬迁 AppDir 回归通过）

- 最新完整构建/重链/封装退出 0，日志 `full-build/orca-appimage-build-v2.log`。交付测试产物为宿主 `/tmp/orca-meshlib-linux-leXC1x/full-build/OrcaSlicer_Linux_V2.3.2.2-Release-meshlib-40d8be288d-v2.AppImage`，133941752 bytes，SHA256 `0e855633231e941573bb4cb117add84ad699acfdf1031744a8d176ebd8131127`。包内 ELF SHA256 `edc52d0defc6ccfd74d9cef2836f32937fd43414135a1172e348d90f39b367c5`。这是 `codex/model-feature-analysis-20261018` / `40d8be288d` 加本轮未提交源码的快照，不冒充干净提交或生产发布。
- 最终原字节 AppImage 经手工解包、搬迁后，在 SDK 隐藏且清除 LD 注入环境的条件下：运行库/哈希验证、MeshLib 42/42、阶段报告 34/34、切片分析 10/10、AppRun help 与真实重建全部通过，候选独立网格检查及导出回读通过。新证据根目录 `/tmp/orca-meshlib-linux-leXC1x/full-build/final-appdir-v2-xyAnjI`，对应容器 `/probe/full-build/final-appdir-v2-xyAnjI`。CLI 专项共 86 项，不与原适配探针、工具回归或第一轮旧包结果重复计数。
- TBB 隔离复测：主程序不再导出已确认的 TBB 自有函数/类型信息。`LD_BIND_NOW=1 --help` 与开启立即绑定的真实 MeshLib 重建中，原先 14 个冲突名字全部解析到 SDK 的 OpenVDB 10.1 自身，SDK→主程序的 TBB 绑定计数均为 0；`tbb-binding-isolation-summary.json`、`loader-bindnow-bindings.txt` 和 `loader-reconstruction-bindings.txt` 保存精确结果。没有用 `libz.so` 的正常 GUI/TIFF 依赖或 Slic3r 参数含 TBB 名字作误判。
- 修前/修后完整 Linux 包的普通 default-off 切片产物对照通过：全部可执行 G-code、完整有效配置、90 层、耗材密度 1.24、耗材 2.79 g 以及排除执行耗时后的原 result 完全一致；指令 SHA256 为 `b064168e19ba7f3d26b75dc248c303c53f4a8963959d55bd8a60fd8283b508bc`。证据 `pre-post-symbol-isolation-slice-comparison.json`，复用 `gcode_facts` / `stable_legacy`，不是只比较同新包的报告开关。覆盖为固定 cube/EP7 自动摆盘，不推导所有模型/多盘或固定位置业务都已通过。
- 工具回归：Linux 配置/真实合成 ELF/打包 53/53 通过，无跳过；Mac 配置 25 通过、2 个 Linux 原生用例跳过。独立补丁复核及 `git diff --check` 通过。最终源码三文件与 `/repo` 逐字节匹配，源码、旧包、新包和新 ELF 的 SHA256 复核稳定；SDK 已恢复，无活动构建/测试（仅保留之前人为中断的 4 个 cc1plus zombie）。容器、缓存、旧包、初次失败与新证据全部保留，未提交/推送/触发远端 CI，也未修改服务器。
- 未验证边界仍保留：本机是 ARM 上的 amd64 仿真；原 AppImage 包装头在此环境不能原样执行，因此使用原字节 SquashFS 手工解包验证 AppDir，**原生 x86_64 Linux 的 AppImage 启动仍待测**，不能写成原始整包脚本通过。Ubuntu24 桌面 GUI 启动/切片/预览/工程重开、高级修复及默认 Alpha Wrap 的手测清单见下文；实际服务器联调、实印质量及用户明确暂缓的固定位置空盘问题没有在本轮验收。

### 符号修正与第一轮包验证过程记录

- 已获主任务及独立复核批准，当前在同一私有 `/work/orca` 执行 `ORCA_EXTRA_BUILD_ARGS='-DORCA_MESHLIB_LOCAL_EVALUATION=ON -DORCA_MESHLIB_SDK_ROOT=/probe/sdk -DZLIB_USE_STATIC_LIBS=ON' MAKEFLAGS=--load-average=8 ./build_linux.sh -s -i -j6`，日志 `full-build/orca-appimage-build-v2.log`。启动前仅有历史 cc1plus zombie、无活动构建；旧包单独保留。当前同步 CMake/README/测试 SHA256 分别为 `c236ef8246114120cbf5009675fb70cd46ac422af36bbb3174bbe2628e5c3bd9`、`cf9d384cd24a877d1cf4d0f8cecf1a7174c149ac5a0151d5bf965968dfd4fb82`、`0c47f139f46439325f72351a2ba22c6cda22f9637eeccb1b1367eba62e13d351`（`tbb-rebuild-source-sha256.txt`）。新包另存 `-v2`，将复跑整包 CLI、实际/立即绑定，并与旧包 default-off 的 G-code、有效配置、层数、耗材及去耗时原结果对照。

- 完整 Ubuntu24 amd64 / MeshLib ON Orca 已构建并封装成功。第一轮原 AppImage 保留，SHA256 `38ef077b5e3834c033ee5452082e66c866960867825b9d785997b9c3d25b0bbe`；从原字节解包、搬迁并隐藏 SDK 后，包内真实 ELF 的 MeshLib 42/42、阶段报告 34/34、普通切片分析 10/10 通过，AppRun help 与实际重建也通过。**这些功能通过不等于最终验收完成**：随后真实动态绑定核查确认 TBB 头定义仍会跨库抢占，正在最小修正 Linux ON 链接边界，旧包不作为最终交付包。
- 第一轮证据根目录：宿主 `/tmp/orca-meshlib-linux-leXC1x/full-build/final-appdir-ZVzZoh`（容器 `/probe/full-build/final-appdir-ZVzZoh`）。`meshlib/summary.json`、`model-stage/summary.json`、`slice-analysis/summary.json` 分别记录上述完整专项，源文件/二进制完整性均通过；不要与此前 7 项最小适配探针相加。真实 ELF SHA256 `96adf26b298b42f235e2addfaab3a985b401d950036dc244f642fe27d0331ed2`。
- 普通切片对照使用固定 cube + EP7 0.4 / 0.20 / PLA 配置，`--arrange 1 --ensure-on-bed --slice 0`；报告默认关、显式关、开启均 90 层，全部可执行 G-code 指令 SHA256 均为 `b064168e19ba7f3d26b75dc248c303c53f4a8963959d55bd8a60fd8283b508bc`，完整 CONFIG_BLOCK 及原结果（排除耗时）一致。它不是用户明确暂缓的 `--arrange 0` 固定位置空盘问题验收，也未覆盖 GUI 或所有模型/多盘场景。
- 实际隔离缺口：主程序导出 10 个 `tbb::detail` 头函数及 4 个 task/task_traits RTTI。SDK 的 OpenVDB 10.1 对这些名字有 `WEAK DEFAULT` 定义和可抢占重定位；普通重建已出现 4 个 RTTI 绑定到主程序，`LD_BIND_NOW=1 --help` 进一步确认 10 个函数也解析到主程序。证据为 `tbb-bindnow-sdk-to-main.txt`、`tbb-overlap-dynsym.txt`、`tbb-sdk-relocations.txt` 与完整 `loader-bindnow-bindings.txt`。函数包括 spin_mutex、spin_rw_mutex、rw_scoped_lock、range_vector、fold_tree 和 atomic_do_once；不是 Slic3r 函数参数中出现 TBB 的误判，也不能用小案例未崩溃覆盖此缺口。未观察到现有专项的实际打印指令变化。
- 修正范围：仅 Linux MeshLib ON 生成带 VERSION 的局部链接脚本，隐藏所属 TBB namespace 的头定义/类型信息/相关 thunk，保留既有静态 archive 排除。脚本作为 INTERFACE 链接文件穿过 PRIVATE STATIC，兼容 CMP0099 OLD 且改变时触发重链；不采用全局 ALL，不替换依赖、不改业务/OFF/Mac/Windows。真实合成 ELF 回归同时覆盖隔离、Slic3r 签名包含 TBB 但不误隐藏、OFF、文件依赖与路径空格。首次该回归另复现既有 Linux SDK 空格路径被 RPATH shell 参数拆分，已在同一 Linux 行补路径引用；不是挪走夹具绕过失败。
- 补丁工具回归已通过：Linux 配置 27 项 + 打包 26 项共 53/53，无跳过（`full-build/tbb-final-linux-tools.log`）；Mac 配置 25 项通过、2 项 Linux 原生用例跳过（`full-build/tbb-final-macos-config.log`）。真实 ELF 实测普通/虚继承/协变返回三类 thunk 均隐藏，保留 Slic3r 参数/模板中含 TBB 的符号；脚本变更确实重链、内容未变的重新配置不改 mtime。初稿 quoted C++ thunk 通配未隐藏的失败已转为精确 mangled 前缀并补真实断言，历史失败日志保留为 `tbb-symbol-config-tests*.log`。独立源码复核无阻塞，`git diff --check` 通过；完整应用旧包仍不能证明此补丁，待主任务重新链接/封装后验收。
- 原 AppImage 启动边界：本机 ARM/Rosetta 的首次 `--appimage-extract` 在包装头返回 `Exec format error`；失败证据 `full-build/final-package-15L6F2/package-validation/extract.txt` 保留。仅在本机容器安装 squashfs-tools，`unsquashfs -s -o 944632` 验证原包的 SquashFS 4.0/zstd，随后原字节解包至新目录再搬迁；没有修改 AppImage magic，也没有改验收脚本绕过失败。已有 Docker 构建模板对 appimagetool 的 `AI\\x02` 处理仅作为仿真环境限制的佐证；原 AppImage 的原生 x86_64 Linux 启动仍待验证，不能把人工解包路径写成原脚本通过。
- 所有第一轮包运行时 SDK 均隐藏，`LD_LIBRARY_PATH/LD_PRELOAD/LD_AUDIT` 清空，结束后 trap 已恢复 `/probe/sdk`。38 个依赖库及两 MR 原字节身份验证通过；直接 bin 的 RPATH 为 `$ORIGIN/../lib/meshlib`。OpenVDB/TBB/z-ng 等实际从包内加载；系统 GUI/TIFF 引入 libz.so 不作为冲突判据。下一步：局部补丁测试和独立审核后由主任务重新链接/封装为新文件，再复跑最终包和动态绑定；旧包和首次失败证据保留，未提交/推送/修改服务器。

## 完整 Docker 第一轮构建过程（2026-10-19，历史步骤）

- 最新结果：6 并发续编与 AppImage 封装已完成，主任务取得 exit0。原字节产物已复制至宿主 `/tmp/orca-meshlib-linux-leXC1x/full-build/OrcaSlicer_Linux_V2.3.2.2-Release-meshlib-40d8be288d.AppImage`，大小 134093304 bytes，源/副本 SHA256 均为 `38ef077b5e3834c033ee5452082e66c866960867825b9d785997b9c3d25b0bbe`。首次最终包脚本在 `--appimage-extract` 即返回 `Exec format error`，还未运行包内 CLI；证据 `full-build/final-package-15L6F2/package-validation/extract.txt`，SDK 已由 trap 恢复。ELF 确认 x86-64，最终 AppImage 头带 `AI\x02` 标记，而既有 Docker 构建脚本清除了 appimagetool 副本的同标记；正在区分本机 Rosetta/AppImage 启动限制与包内运行问题，不以封装成功宣称运行通过。
- 当前执行：`libslic3r_cgal` 已完成 25/25；完整应用先以 `./build_linux.sh -s -i -j 4` 构建至 346/545，无编译错误。用户此前已允许调整资源提速；Mac 构建退出后，实读容器 8 核仅用约 4 核、MemAvailable 约 15.54 GiB、swap 未用，4 个编译进程各约 1.95–2.28 GiB RSS，因此选择 6 并发而非 8 并发，给大 GUI 单元留内存余量。主任务仅向原 Ninja 发 SIGINT；原构建 exit2 是人为中断，不是编译失败，原日志 `full-build/orca-appimage-build.log` 保留。确认旧 Ninja 及活动编译进程退出后，在同一 `/work/orca`、相同配置增量执行 `./build_linux.sh -s -i -j 6`，新日志 `full-build/orca-appimage-build-j6.log`；始终只运行一个构建。完成后隐藏 `/probe/sdk` 并用 trap 恢复，在新证据目录执行最终 AppImage 验证，再检查完整应用的动态符号及普通切片报告开关兼容性。当前尚未取得最终 AppImage，不能标为包验收通过。
- 完整应用当前追加源码 SHA256：`ModelReconstruction.cpp=f4ae8e232b6aa8468c7ae4f1ea5cc7bb21cd6e03e7356b5baeda2d313fc9d0f0`，`.hpp=88358d2a4deb242bbc4de09db26143f54711b5e0a96f4b325c3b3d01f42306b8`；对应测试 `test_model_reconstruction.cpp=46344532b8e16bf7ca6d4a4e8ea49a9a33c6de625443b9a60f98d0754191ea39`。以下步骤记录保留为历史，最新执行状态以本段为准。
- 当前续接：第三方依赖已完成（dependencies.log 最后为185/185、原进程exit0）；用户要求修正复审问题，固定位置切片问题明确暂缓。之前50项配置/包测试和7项适配探针没有运行完整根CMake的FindZLIB，不能当作完整Orca包通过。现已在完整 `/work/orca` 配置中实测：默认找到libz.so并被拒绝（`full-build/orca-configure-default.log`，exit1），启用`ZLIB_USE_STATIC_LIBS=ON`并清理该失败配置缓存的ZLIB_LIBRARY_RELEASE/DEBUG后，根CMake生成成功（`full-build/orca-configure-static.log`，exit0）。CI及README仅ON命令已补静态选择，OFF不变；Linux真实FindZLIB新增回归26/26通过。当前先构建`libslic3r_cgal --parallel 2`，日志`full-build/orca-build-cgal.log`，随后完整应用/包；不把配置生成成功写成链接/运行成功。
- 本轮快照追加源文件：`ModelReconstruction.cpp/.hpp`及对应C++测试、ZLIB参数/配置测试/README已复制到容器私有副本。原源码快照仍保留，最终程序需记录追加文件哈希；仓库挂载仍只读，不修改远程服务器。Mac新程序并行构建使用独立本机构建目录，证据`/tmp/orca-20261019-fixes-fNQ04T/`。
- 用户明确要求用本机 Docker 打包完整 Orca；本轮目标为 Ubuntu24 x86_64 Release / MeshLib ON AppImage，不触发远程 CI、不改只读服务器、不自动提交/推送。当前分支/HEAD 为 `codex/model-feature-analysis-20261018` / `40d8be288d`，使用包含本轮 Linux 打包改动的工作区快照。
- 复用本机容器 `orca-meshlib-linux-40d8-probe`，源码复制到容器私有 `/work/orca`，不在 Mac 的 build/deps 构建目录运行；仅复制既有下载缓存，不复用 macOS 已编译库。容器内安装完整 Linux 构建依赖，原仓库挂载只读。
- Docker VM 约 8 GiB 内存、16 CPU；本机 ARM 上执行 amd64 仿真。限制容器 CPU affinity 为 2 核，第三方顶层串行/子构建限负载，主应用低并行，避免沿用 CI 大并行导致内存耗尽。日志输出 `/tmp/orca-meshlib-linux-leXC1x/full-build/`。
- 计划：系统依赖→全量第三方库→当前 Orca→AppImage→隐藏 SDK 后最终包脚本；独立记录普通切片/分盘及 GUI 未验/已知问题，不能以打包成功推导业务通过。先前“完整包未构建”的记录保留为历史状态，当前结果随执行更新。
- 快照：`full-build/source.tar` SHA256 `fefda9c7b431c7d20b9f89ab056779c0fa76075d06115f24283c240165328724`，按 Git 跟踪及未忽略文件生成，不含 `.git` 和 Mac 构建产物；解包的 macOS xattr 提示不影响文件内容。构建版本另以快照/关键文件哈希记录，不冒充干净 HEAD。`build_linux.sh -u` 正在容器副本执行，日志 `full-build/system-dependencies.log`；仅本机容器内安装，服务器无操作。
- 用户随后允许调整配置提速。本机实读为64 GiB/16 CPU、内存free percentage 61%；计划待apt安装完成后将Docker内存从约8 GiB增至24 GiB、容器CPU affinity放宽到8核，第三方仍顶层串行，应用低于全部CPU并发。需要重启本机Docker，已确认没有其他运行容器；服务器只读约束不变。
- 资源调整已生效：停止Docker后只给用户settings-store.json增加`MemoryMiB=24576`，原配置备份为`full-build/docker-settings-before.json`；重启实读MemTotal=25160732672 bytes（约23.43 GiB可见），容器`nproc=8`。未改其他Docker偏好/镜像/容器，不动服务器。系统依赖安装退出0，现以`MAKEFLAGS=--load-average=8 ./build_linux.sh -d -j 1`编译第三方库，日志`full-build/dependencies.log`；GMP/MPFR裸`make -j`用负载上限约束，不以顶层-j1冒充子任务串行。
- GitHub耗时对照：再次只读查询 Run `35806823414`，job自2026-09-23 01:34:15Z至02:59:55Z，共85分40秒；第三方依赖33分15秒，应用及AppImage47分41秒。该历史代码未包含本轮MeshLib，不同缓存/机器/代码不能用于推算本机固定倍数；本机尚无完整耗时。

## 当前实施状态（2026-10-19）

- 用户已要求直接补齐 Linux MeshLib 构建/打包/验证；当前分支仍为 `codex/model-feature-analysis-20261018` / `40d8be288d`。不修改算法、业务接口、普通切片/分盘逻辑，不自动提交或触发发布。
- 实现已完成：Linux 链接参数的静态依赖传播、运行库闭包打包和包内加载、固定 SDK 的 CI 构建及最终包测试入口。MR 两库原字节保留，只有必要的第三方副本重定位；复用现有 CMake 开关与构建入口，不扩展为独立服务。
- 最终验证：Linux 容器配置/打包测试 50/50 通过无跳过；Mac 同组 49 通过、1 Linux 用例跳过。真实适配重建探针及隐藏 SDK 的迁移运行各 7 项通过；ON/OFF 模板展开后 shell 语法通过，工作流静态核对通过。模块检查 35 个模块问题 0，`git diff --check` 通过。
- 既有工作区文档、result.json、下载工具改动保留；并行实现与独立复查完成，修正已发现的主程序绝对 RPATH 提前拒绝及间接库 RUNPATH 问题，未放宽原 CLI 验收断言。已登记模块归属及更新相关正文/指纹。
- 本机最初 Docker daemon 未启动；本轮启动本机 Docker 并创建隔离 amd64 Ubuntu 24.04 容器做最小 SDK 探针，验证后已停止该容器，证据及容器保留可复查。服务器保持只读，不能把仿真容器或脚本测试视为完整 Orca/服务器验收。
- 下一步：提交/远程 CI 需另获用户授权。完整 Orca AppImage 尚未构建验收；本分支原有空盘判断问题不在本轮修改范围，不能据此上线替换 v2.0.21。

### 服务器只读核对

用户 2026-10-19 明确允许查看服务器配置，禁止修改。仅通过现有 Termius 会话执行 `cat`、`getconf`、`nproc`、`free`、`df`、`lscpu`、`docker ps/inspect` 与容器内读取系统文件/路径；没有上传、安装、修改文件、启停服务或容器。

- 宿主机：Ubuntu 22.04、glibc 2.35、x86_64、8 vCPU（AMD EPYC，含 AVX2），内存总计约 15 GiB、可用约 13 GiB，无 swap；磁盘 `/` 约 99 G、可用约 12 G、使用率 89%（以实时 df 为准，不采用登录横幅旧值）。
- 现有 `orcaslice-build-v21` 容器：镜像 `bigdata-orcaslice:v2.0.20`，Ubuntu 24.04.2、glibc 2.39、x86_64；inspect 的 Memory/NanoCpus/CpuQuota/CpuPeriod 均为 0，表示这些字段未设容器额度，不等于不限所有资源。
- 启动方式：`/usr/local/bin/orca-slicer` 为指向 `/squashfs-root/bin/orca-slicer` 的软链接，直接执行二进制，绕过 AppRun。因此包必须支持二进制自身相对库路径与传递依赖，不能只依赖 AppRun 的 LD_LIBRARY_PATH。
- 选择：保留 Ubuntu 24.04 构建与同版本 SDK；仅用于 24.04 容器，不承诺在 22.04 宿主直接运行。固定 SDK 实际两库引用 GLIBC_2.38/GLIBCXX_3.4.32，不能以共享内核推导宿主用户态兼容。
- 打包策略补充：仅对包内主程序设置相对、可传递的 ELF RPATH；MR SDK 库保持原字节，确保 configure 时的 SHA256 不被打包改写。
- 用户随后澄清目标镜像为 `v2.0.21`，其有此前的切片/分盘问题，`v2.0.20` 没有。这里实读的容器名与镜像来源不相同，不能将 `orcaslice-build-v21` 的来源镜像字段解释为已经核验了 `v2.0.21` 镜像本身；本轮不修改或重新部署任一服务器版本。

### 本机最小 Linux 探针与搬迁测试（已执行）

- 固定 SDK `meshlib_v3.1.3.429_ubuntu24-dev.deb` 104854184 bytes，实下载 SHA256 `e592cf7d9a670d2f5b1fc01ddabb2574044749180f492864c62f37805686d727` 与官方元数据一致。临时目录 `/tmp/orca-meshlib-linux-leXC1x`，本机 Docker 容器 `orca-meshlib-linux-40d8-probe`，仓库只读挂载。
- Ubuntu 24.04 amd64（macOS/Rosetta 仿真）、GCC 13.3、CMake 3.28 实际编译当前适配库，C++17 静态 host 通过 PRIVATE 依赖 C++20 adapter，使用静态 OpenSSL。除固定 SDK 和工具链，适配编译新增头依赖仅 `libexpected-dev`。
- 初次裸跑因 DT_RUNPATH 不传递而缺 `libz-ng.so.2`；Linux ON 局部增加 `--disable-new-dtags` 后，readelf 确认可传递 DT_RPATH，清空 LD 路径直接运行成功。24 项配置/生成测试在 Linux 容器通过。
- 适配探针 7 项通过：方块 12 面生成 128 面/66 顶点，实际 MR 库身份校验通过；入口取消/回调抛错、SDK 正进度回调取消/抛错、非法体素、空输入拒绝。不是完整 Orca 独立网格验收，也未覆盖旧 TBB/OpenVDB 与应用共存。
- 日志：上述目录下 `adapter-configure.log`、`adapter-build-rpath.log`、`adapter-run-without-env.log`（初次失败）、`adapter-run-rpath.log`、`adapter-elf-identity.log`、`platform-config-tests-linux.log`。探针 ELF SHA256 `aaae218a4de8990b21ef31f64edd5f75e7bfbd7c5330a03a6269f0b18742155e`；实际无 EVP/OPENSSL/CRYPTO 动态符号命中。
- 成包小 ELF 回归进一步发现：第三方子库有 RUNPATH 时会绕过主程序路径，仍加载原 SDK。已对有路径的非 MR 副本设置 `$ORIGIN`，不修改两份 MR 库；真实 SDK 的 OpenVDB 原路径为 `/home/MeshLib/lib`，仅该库副本的内容发生改变。
- 真实适配探针封装得到 38 个库、28 份声明文件；搬迁到 `relocated-package` 并隐藏 SDK 后，清除 LD 路径的依赖/哈希验证通过，`env -i` 直接执行包内二进制的 7 项探针均通过。MR 实际加载路径都在包内，运行身份校验通过，SDK 已恢复。包内主程序 SHA256 `36538fcbd7219825b47e52f90c14bf8715721c8595089f0bb499deea59e38daa`。
- Linux 容器打包工具 26/26 测试通过（含真实 ELF 编译、子库 RUNPATH、搬迁及隐藏 SDK）；macOS 25 项通过、1 项 Linux ELF 测试按平台跳过。它们不是完整 Orca / AppImage / GUI 或服务器业务验收。

### 最终复核与交接

最终配置/打包复跑命令：

```sh
docker exec -w /repo orca-meshlib-linux-40d8-probe \
  python3 -B -m unittest scripts.tests.test_meshlib_platform_config scripts.tests.test_package_meshlib_linux -v
```

50 项通过，13.219 秒，实际执行 Linux ELF 用例。容器现已停止；重跑需明确启动本机此测试容器，不在远程业务容器执行。日志位于上述临时目录的 `final-linux-tests.log`、`final-linux-shell-syntax.log`、`final-linux-source-identity.log`；当前打包工具 SHA256 `0f32837e8c99fe645caffc8f614c0d457d740a908c06dba1e635c09661b1143d`。CMake 实际展开两份模板的 ON/OFF 配置后均 `bash -n` 通过，最终 AppImage 测试脚本语法通过。

后续只在独立测试环境执行（不自动操作用户指定的只读服务器）：

1. 构建当前开发分支完整 Ubuntu24 x64 AppImage。可使用新增 `workflow_dispatch` 仅生成测试产物；不要为试包直接推发布标签。保存提交/脏工作区身份、AppImage 哈希及工作流证据。
2. 使构建 SDK 不可访问后执行 `bash scripts/test_linux_meshlib_package.sh <AppImage绝对路径> <新的证据目录>`。要求直接 ELF 与 AppRun 均重建成功，MR 哈希正确，所有所需运行库在迁移包内。CI 已接好此步骤，但本轮未获得完整 AppImage，未执行该全应用验证。
3. 单独核对并修正当前分支既有空盘判断，再复跑用户失败的原命令 `--slice 0 --auto-plate 0 --arrange 0`（相同模型与完整配置），确认实际生成 G-code，不能只看退出码；另测自动摆盘、多盘、空盘/禁用盘、越界 JSON。此既有业务问题不是本轮依赖打包改动的验收通过项。
4. 使用相同模型/机器/工艺/耗材，在新包对照普通切片以及显式报告开关，核对实际打印指令和原结果；可复用 `scripts/test_cli_slice_analysis.py` 并显式提供全部资源路径，不依赖本机 planning 目录。旧 TBB/OpenVDB 与新动态依赖在完整应用内共存尚未验证。
5. GUI 手测交接：在 Ubuntu24 桌面的独立用户配置中运行最终包 AppRun，导入闭合方块→选择已知有效机型/耗材/工艺→切片→打开预览→保存并重新打开工程，核对无缺库/崩溃且普通结果一致；另执行现有高级修复及默认 Alpha Wrap 入口。保留日志、包身份和截图。GUI 没有 MeshLib 选项，不能要求用户从不存在的入口测试；但 ON 应用启动共享这些库，所以 GUI 启动/原业务仍是待验项。

边界：本轮没有更改重建算法、切片/分盘逻辑、客户端业务接口或 Mac/Windows 打包；CMake 默认仍 OFF，Linux 工作流显式 ON。未提交、推送、触发 CI、发布或修改服务器。

## 实施前分析：开发分支 Linux MeshLib 测试包（2026-10-19 历史快照）

本节保留实施前的缺口与授权边界；当前进展以文首为准，下述“未修改/待补齐”不表示本轮最终状态。

- 用户澄清本轮目标为当前开发分支 `codex/model-feature-analysis-20261018`，HEAD `40d8be288d7d0299c7886867821499f094974c35`，不是 master，也不是此前的 Linux 发布分支。下文旧基准结论仅作历史记录。
- 本轮只分析所需调整；未修改业务代码、工作流或打包脚本，未下载 SDK、构建 Linux 包、触发 CI、推送或发布。已有工作区改动保留，仅补充本记录。
- 已有 MeshLib C++/CLI 接口及 Linux SDK 配置，不需另写重建服务。StageReport / SliceAnalysis 复用既有 CGAL、OpenSSL、Boost，无新增外部 SDK。
- 最小调整范围：固定 `3.1.3.429` Linux SDK，通过既有 `ORCA_EXTRA_BUILD_ARGS` 启用；补全 Linux 链接参数传播；将 MeshLib 非系统动态依赖随 TAR/AppImage 一起打包并设置包内加载路径；对最终包做真实 CLI 回归。
- 当前 Linux RPATH / `--exclude-libs` 使用 adapter 的 `INTERFACE_LINK_OPTIONS`，但 `libslic3r` 以 PRIVATE STATIC 链路依赖 adapter；项目最低 CMake 3.13，未设置 CMP0099 NEW。应局部保证这些参数到达最终链接，不以配置成功代替链接/符号检查，不全局替换 Orca 的依赖版本。
- 优先原字节复制 SDK 运行库并使用包内路径，避免不必要的二进制改写。若原生 ELF 检查证明必须改 RPATH 或 strip，应先处理 SDK 副本、再由 CMake 计算哈希；当前 Linux 尚无 macOS 已有的 prepared-runtime 入口，不能在配置之后改写受校验的 MR 库。
- 现有打包脚本仅复制主程序与 resources，AppRun 仅设置 bin 搜索路径，CI 仅检查 AppImage 文件存在/类型。启用 SDK 后，运行库缺失可能使程序在解析参数前无法启动，普通切片也会受影响；部署必须保留完整运行目录，不能只替换主程序。
- 独立阻塞项：当前分支 `src/OrcaSlicer.cpp` 仍在盘成员注册前使用 `empty()` 跳过盘（当前行 7124 / 7196）。前序 master 工作目录的修正和回归脚本不能视为本分支已有内容；应针对性处理后测试 `--slice 0 --auto-plate 0 --arrange 0`，不合并其他目录未审核改动。
- 待执行验收：最终 Linux 包脱离 SDK 目录启动、实际加载路径/哈希与 ELF 符号核查、MeshLib 重建及失败返回、固定位置/自动分盘/多盘普通切片、模型阶段报告与特征/切片分析报告。普通 GUI 启动及切片尚未验证；GUI 无直接 MeshLib 重建入口，不能将 CLI 成功等同于 GUI 已测。
- 回归入口需适配 Linux：MeshLib 测试有 PID/子进程检查，应对独立解包目录中的最终二进制运行并提供仅包内库路径，另测 AppRun 启动，避免将 AppImage 包装进程误判为重建子进程；特征/切片报告测试需显式固定模型和配置，不依赖本机未跟踪的 planning 资源。
- 下一步：按用户确认实施上述最小构建/打包改动，再在 Linux 原生环境和实际服务镜像验证；本机 macOS 既有结果不作为 Linux 通过证据。
- 本轮文档检查：`python3 scripts/check_ai_module_docs.py --all` 复核 35 个模块，问题 0；`git diff --check` 通过。它们不验证 Linux 编译或运行。

## 此前审查记录（历史基准）

- 2026-10-19 只读审查完成；用户要求确认既有 GitHub 打包流程，未授权本轮合并分支、推送 Tag、触发 CI 或发布。本轮只保存审查记录，不修改业务或工作流。
- 当前工作区为 `codex/model-feature-analysis-20261018` / `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，有切片分析、文档和其他未提交改动，均保留。本轮比较的引擎基准为 `master` / `ba979559b06c8da78ffbbb76597c68a9c51d28cc`，不能把当前工作区当作 master。
- 用户部署路径是 GitHub 生成 Linux x86_64 安装包 → Linux 安装 → 制作新镜像，基础镜像为 `swr.cn-east-2.myhuaweicloud.com/gongfudou/bigdata-orcaslice:v2.0.20`；无需更改此顺序。
- 结论：旧打包流程有真实成功记录；指南所指发布分支尚无本轮越界分盘修正和 MeshLib 接入，不能将旧成功作为新版本验收。
- 未构建、运行或下载 Linux 二进制；本机 Docker daemon 未运行。新 master 的 Linux 编译、安装包运行库及最终镜像业务验收仍待执行。

## 发布入口与实际身份

`planning/packaging/manual-tag-build-guide.html`（更新日期 2026-08-24）指定另一个目录 `../orca-slicer` 的 `linux/release2_3_2` 分支，通过 `github` remote 推送 `manual-build-linux-*` Tag。这里的 remote 名称与当前 `orca-slicer-soft` 不同，不能直接照抄到当前目录。

2026-10-19 使用 `git ls-remote` 核对 GitHub：该分支及最新 Linux Tag `manual-build-linux-20260923-01` 都指向 `e5640e1a5cc044f27aaae635802d6a9a8036d59a`，与 `../orca-slicer` 干净工作区一致。源码检查显示该提交没有 `src/meshlib`、没有 `ORCA_MESHLIB_LOCAL_EVALUATION` 选项；变换 JSON 仍通过 `contain_instance` 返回旧归属，未包含 `export_separately` / `next_export_plate`。两个仓库对象库不完整共享，不能将 `merge-base` 的对象不存在错误当作祖先关系证明；上述缺失结论来自实际源码。

GitHub API 实读 [Run 35806823414](https://github.com/lgwei49699-stack/orca_p/actions/runs/35806823414)：2026-09-23 创建，`Build Linux AppImage for x86_64` / `success`；job `107009433354` 的构建、AppImage、上传和 Release 步骤全部成功。Artifact `10730720932` 为 `OrcaSlicer-Ubuntu24.04-x86_64-2.3.2.0-Release`，118381246 bytes，API 报告的 artifact digest 为 `sha256:6e38df1b559182b6709729e1b7560f94e2c6d7d834835232c9349a9a321ab6d1`；这是服务器元数据，并非本轮下载校验的 AppImage 哈希。

## 发现与影响

1. **发布源码未同步。** 按指南直接给当前 Linux 分支再打 Tag，不会带上 10 月 17 日的越界模型独立返回盘/XY 居中/Z0 修正，也没有 MeshLib。应先明确并同步所需已提交代码，再以新 Tag 构建；不能覆盖旧发布 Tag。
2. **MeshLib 编译与打包尚未接入该工作流。** 当前 master 默认 OFF，CI 没有下载 SDK、显式 ON 或提供 SDK 路径；合入 master 后照旧构建仍默认无 MeshLib。若要 ON，需要匹配 Linux x86_64 的固定 SDK、完整动态依赖及加载路径，并验证最终包内实际加载身份。
3. **AppImage 成功不等于运行通过。** 发布分支 `.github/workflows/build-linux.yml:204` 的 Verify AppImage 仅检查文件存在、大小及 `file` 类型。`build_linux_image.sh.in` 只显式复制资源和主程序，`build_appimage.sh.in` 使用 appimagetool 封装现有目录，没有显式收集 MeshLib 依赖。不能据此保证 ON 包安装后能启动。
4. **目录内失败日志是旧问题证据。** `planning/packaging/1.txt` 结尾为 `jpeg_stdio_dest` 重复定义导致链接失败；当前发布分支 OpenCV 配置已在 Linux 关闭 bundled JPEG 并复用项目 JPEG，且后续最新 Tag 构建成功。不能把旧日志当作最新构建失败。

## 后续验证范围

用户准备同步 master 后进一步核对：`master/ba979559b0` 的 MeshLib 默认 OFF，关闭时只编译 SDK 占位实现，普通分盘发布无需为此修改业务代码。建议发布工作流显式设置 `ORCA_EXTRA_BUILD_ARGS=-DORCA_MESHLIB_LOCAL_EVALUATION=OFF` 固定本次构建意图；这是建议，用户尚未要求实施或选择永久关闭。Linux 分支的工具链指纹缓存、并发控制和 `build_linux.sh` 的 Ninja 错误收集参数比 master 更新，同步时保留这些平台改动。两分支的 OpenCV JPEG 修复和两个 Linux 打包脚本内容相同，无需重复修复旧 JPEG 问题。若目标包括可用的 MeshLib，则仍须补齐 ON 的 SDK/运行库打包与原生回归，不以合并源码替代。

仅发布分盘修正可保留 MeshLib OFF，并对新 Tag 的最终 AppImage 做启动、普通分盘/切片和越界 JSON 回归。启用 MeshLib 则增加 SDK 准备与依赖打包、脱离构建机 SDK 路径的运行校验，以及 `scripts/test_cli_meshlib_reconstruction.py` 的真实 Linux 回归；最终镜像继续核对普通业务、架构、依赖和资源限额。不将当前配置级平台测试或 macOS 通过记录当作 Linux 运行证明。

本轮 CodeGraph MCP 未暴露；跨仓库/分支的历史索引不作为发布身份依据，工作流、Git refs 和实际源码直接核对。没有初始化或重建索引。

文档检查：`python3 scripts/check_ai_module_docs.py --all` 复核35模块，报告1项已有工作区问题：`gcode-preview.md` 的源码变化需复核并记录指纹；本轮未改该模块或其源码，不覆盖其他任务的审核记录。构建模块文档的差异空白检查通过；未提交。

关联：[构建模块](../modules/build.md)、[CLI 分盘验证](2026-10-17-cli-oversized-arrangement.md)。
