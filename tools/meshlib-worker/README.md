# MeshLib 本机评估 worker

这是 Orca 显式 CLI 重建的独立 C++20 后端，只生成候选；**不是正式发布组件，不是自动修复菜单，也不执行切片**。默认不构建、不下载 SDK、不安装或拷贝 SDK 到 Orca，不包含打包目标。正式产品使用及再分发必须另行确认 SDK 授权和许可证兼容性；MeshInspector 应用订阅不能替代 SDK 授权。

当前固定 MeshLib **3.1.3.429**。只验证 macOS arm64 本机评估；Windows/Linux 尚未验证。现有 GUI 和默认 Alpha Wrap 不依赖本目录。

## 构建与版本来源

使用官方固定版本 [arm64 SDK pkg](https://github.com/MeshInspector/MeshLib/releases/download/v3.1.3.429/meshlib_v3.1.3.429_arm64.pkg)，下载到仓库忽略的 `planning/cache/meshlib-sdk/` 后校验：

```text
size:   76500475 bytes
sha256: bd141ecb8a8034fadb4b81d50ea2b51c55bbd45dc52308d5ebce5bebb300d47e
```

使用 `pkgutil --expand-full` 解包而非运行安装器。SDK prefix 是展开目录内 `MeshLib.pkg/Payload/Frameworks/MeshLib.framework/Versions/3.1.3.429`，包含 `include/` 和 `lib/`。不要将这些 SDK 文件加入 Git。

官方 pkg 未包含所有依赖开发头文件。本机复用了已有 Boost、TBB、Eigen、OpenSSL 头文件；唯一缺少的 `tl/expected.hpp` 来自该 MeshLib tag 指向的 [固定子模块提交](https://github.com/Developer-Ecosystem-Engineering/expected/blob/d6e1fcc766b725f196d7f58097d72f8cfab4d56a/include/tl/expected.hpp)，保存在 `planning/cache/meshlib-sdk/thirdparty-headers/tl/expected.hpp`。其 SHA256 为 `80c227d7e2f4f7408dcb6a2428d594890d6d2c82b7c2f1bf28acd17838d22ce4`。CMake 不会自动取得这些依赖。

本机已验证的构建示例（先准备上述文件）：

```bash
meshlib_eval_root="$PWD/planning/cache/meshlib-sdk/expanded-3.1.3.429-arm64/MeshLib.pkg/Payload/Frameworks/MeshLib.framework/Versions/3.1.3.429"
cmake -S tools/meshlib-worker -B planning/cache/meshlib-worker-build \
  -DORCA_MESHLIB_LOCAL_EVALUATION=ON \
  -DORCA_MESHLIB_SDK_ROOT="$meshlib_eval_root" \
  "-DORCA_MESHLIB_EXTRA_INCLUDE_DIRS=$PWD/planning/cache/meshlib-sdk/thirdparty-headers;/opt/homebrew/include;/opt/homebrew/opt/eigen/include/eigen3" \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  -DOPENSSL_CRYPTO_LIBRARY="$meshlib_eval_root/lib/libcrypto.3.dylib" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build planning/cache/meshlib-worker-build -j2
python3 tools/meshlib-worker/test_worker.py \
  "$PWD/planning/cache/meshlib-worker-build/OrcaMeshLibWorker" -v
```

SDK 自身 MRMesh/MRVoxels 的 Mach-O 最低版本是 12.7，但其附带 TBB、OpenVDB、Boost、fmt、jsoncpp、OpenSSL 等实际依赖最低版本是 **14.0**，因此上述独立构建使用 14.0。这不改变主 Orca 的系统版本要求，也不代表已在 macOS 14 真机验证。不要误用本机 Homebrew 的最低 macOS 26 动态库；这里 OpenSSL 仅复用开发头文件，运行库指定 SDK 内附版本。评估二进制依赖当前 SDK 解包路径，不能直接搬到其他电脑发布。

CMake 检查 SDK 头文件 `MESHLIB_VERSION == v3.1.3.429`，将所选 MRMesh/MRVoxels 的真实路径与 SHA256 写入独立构建目录。worker 在资源监督生效后，用系统动态库定位及 OpenSSL EVP 核验实际加载的这两个库，与构建时身份一致才处理模型。**`GetMRVersionString()` 读取应用资源 `mr.version`，缺失时只返回库编译日期，不是可靠的 SDK 版本 API**；报告保留其原值作为诊断，版本身份依据是固定发行包校验、头文件版本与运行库哈希。此检查不是整个依赖供应链审计。

Windows 需要显式指定对应实际 DLL（`ORCA_MESHLIB_MESH_RUNTIME`、`ORCA_MESHLIB_VOXELS_RUNTIME`），而不是只提供 `.lib`；该路径未完成构建和运行验证。Windows worker 必须由 Orca 的受限 Job 运行器启动，独立 Python 协议测试会跳过，不得据此标记 Windows 通过。

## 通过 Orca CLI 调用

在仓库根目录运行，输入必须为不带颜色的 STL，单位为毫米；输出目录必须尚不存在。下面 `0.0803` 是 Q 样本的回归精度，不是所有模型的推荐值。

```bash
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  --reconstruct-model /absolute/path/input.stl \
  --reconstruction-engine meshlib \
  --reconstruction-voxel-mm 0.0803 \
  --reconstruction-worker-path "$PWD/planning/cache/meshlib-worker-build/OrcaMeshLibWorker" \
  --outputdir "$PWD/planning/repair/meshlib-evaluation-01" \
  --accept-reconstruction-risk
```

通过独立几何验收后返回 0，生成 `candidate.stl` 和 `reconstruction.json`；失败返回既有重建退出码 2，不生成可用候选，不覆盖输入。执行前参数错误可能尚未创建输出目录，此时查看标准错误中的 JSON。`--repair-evidence` 可附加既有只读边界投影，不会自动修复、采用或切片。原生 GUI 无 MeshLib 入口，未指定引擎时仍使用 Alpha Wrap。

报告的 `candidate_ready` 只表示网格候选可供检查：`shape_confirmation=not_confirmed`、`print_ready=false`。采样偏差超过阈值仍保留候选并告警，阈值不是硬性保真验收；结构匹配、活动间隙和打印验证尚未自动完成。默认内存/超时沿用原重建预算，显式超时可用 `--reconstruction-timeout 60`；超限不自动降低精度或换引擎。

## 内部调用协议

七个参数均必填，未知或重复参数拒绝：

```text
OrcaMeshLibWorker --task-directory ABS --parent-pid UINT \
  --timeout-ms UINT --memory-limit-mb UINT --voxel-mm DOUBLE \
  --max-input-faces UINT --max-output-faces UINT
```

- `task-directory` 是父进程创建的独占临时目录，必须绝对路径、非符号链接。父 PID 必须是直接启动该程序的真实父进程。
- timeout 为父进程剩余预算，最多 300000 ms；内存 128–4096 MiB；输入最多 300 万面，输出最多 100 万面。Linux 地址空间上限、跨平台 RSS/父亡监督复用 `ReconstructionProcess.cpp`。macOS RSS 是采样预算，不是严格分配上限；父层还需监督两层 worker 的合计 RSS、超时和取消。
- 输入固定 `meshlib-input.stl`：普通非符号链接文件、精确二进制 STL、大小不超过 256 MiB、有限坐标。此为内部协议，不接受 ASCII STL；由 Orca 读取用户模型后写入工作副本。
- 输出固定 `meshlib-candidate.mesh`：小端 `ORCARC01` 八字节标识、`uint64` 顶点数/面数、`float32 xyz`、`int32` 三角面索引。只序列化有效面及其引用顶点并压紧索引，不删任何壳体。超过输出限额时不写候选。
- `meshlib-report.json` 最大 256 KiB，临时文件关闭并重命名原子发布；已存在的输出或临时文件不覆盖。成功报告最后发布；父端不得仅凭候选文件存在判成功。

SDK loader 设置 `telemetrySignal=false`，记录漏面和为非流形拓扑复制的顶点数。`skipped_face_count != 0` 或加载面数与原始记录数不符时拒绝重建；复制顶点不等于漏面，但必须报告。

## 固定算法与报告

`MR::rebuildMesh` 参数：显式 voxel、Auto 符号判断、Standard offset、预细分、封闭 HoleWindingNumber 结果、绕数阈值 0.5、beta 1.5、减面、角度优化 3 轮、极短边 0.005 mm。不启用 sample 专属碎片删除，不硬编码 7 个部件。报告保存实际 float 值和 Auto 最终选用模式。

关键 JSON 字段：

| 字段 | 含义 |
| --- | --- |
| `schema`, `engine`, `sdk_version` | `meshlib_worker_v1`、`meshlib`、`3.1.3.429` |
| `runtime_identity_verified`, `runtime_libraries` | 实际加载库身份核验及路径、SHA256 |
| `sdk_runtime_version` | SDK API 原始诊断字符串，可能仅为构建日期 |
| `parameters` | voxel、全部固定重建参数、输入输出面数上限；float 参数保存实际精度 |
| `input.faces`, `input.loaded_faces` | 二进制 STL 原始记录数、SDK 实际加载数；两者必须相等 |
| `skipped_face_count`, `duplicated_vertex_count` | 漏面数、SDK 为修正拓扑复制的顶点数 |
| `output.faces`, `output.vertices`, `output.bytes` | 候选序列化后的实际计数，父端必须对照二进制再校验 |
| `selected_sign_mode`, `selected_sign_mode_name` | SDK 回调报告的实际符号检测枚举及名称 |
| `elapsed_ms` | runtime_identity、input_validation、sdk_load、rebuild、candidate_export、total，按实际完成阶段出现 |
| `candidate_validated`, `print_ready` | 始终 false；worker 不冒充独立几何验收或打印验证 |

退出码 0 只表示已发布完整协议报告；须查看 `status`。只有 `candidate_generated` 表示生成了待验候选。已处理的失败包括 `invalid_input`、`input_faces_skipped`、`resource_limit`、`timed_out`、`candidate_rejected`、`engine_error`、`output_error`、`sdk_version_mismatch`、`failed`。参数解析/报告无法发布返回 2；硬监督保留 124（父亡/超时）、125（内存）、126（监督失败），可能没有报告。父端统一转换业务状态，不新增用户自动流程。

候选必须再经 Orca 独立检测、双向几何偏差检查及业务边界验收，原件始终不覆盖。`fragment_removal=none` 仅表示没有额外的碎片筛除步骤；体素重建本身仍可能使小结构消失或合并，不承诺部件无损保留。零自交和闭合也不等于设计意图恢复、关节可动或保证打印。

## 本机回归记录

- 2026-10-15：macOS arm64、C++20、SDK 固定包，独立协议测试 **12/12 通过**（约 1 秒），覆盖有效候选/压紧索引、输入缺失/链接/截断/NaN、输入输出限额、SDK 漏面、输出不覆盖、超时、父 PID 错误、无效 voxel。
- Q 样本（原 SHA256 `776db5822a1e3afcdda6fd094e8ca724f9ccf778b6d36c0023951a244fd58ce0`）：voxel 0.0803，59,924 → 79,494 面，39,679 顶点，漏面 0，复制顶点 2,402；实际模式 HoleWindingRule。一次总耗时约 6.627 秒，其中重建 6.593 秒、加载 21.8 ms、身份核验 6.9 ms。该值不是多次中位数，也不是完整 Orca 验收时间。
- 历史 79,400 面结果删除过 3 个微小壳体的 94 个面；本 worker 明确保留，所以候选面数不同。不得以通过协议测试替代独立几何检测，也不得为凑历史面数删小部件。
- 同一次 Q 候选已按原有独立评测脚本复检：10 个闭合壳，开放边、非流形边/顶点、零面积面、重复面均 0；libigl 自交 0 对，MeshLab 自交 0 面。78 面和 12 面微壳有负有向体积，包围盒中心在主壳内部；4 面微壳正体积，中心在主壳外。前两者可能表达微小内腔，不能仅凭负体积删掉；单点内部观察也不是设计意图证明。Orca 正式 CLI 的独立验收结果以父流程报告为准。

SDK 授权、正式打包/签名、其他平台、旧系统和实机打印均未完成，本目录不宣称发布就绪。
