# EPONE 实时视频接入 Orca 分析

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前执行状态

- 目标：核对 Flutter、后端和本分支 Orca 的实时视频协议，评估直接接入条件；本轮只分析，不修改业务代码、不操作真实设备。
- Orca：`codex/model-feature-analysis-20261018`，HEAD `c8c45c8557fba8a75de0056a82ff93202f37c7b9`。工作区已有模型检测/修复、脚本、模块文档等改动及 result.json，均保留。
- Flutter：`projects/phone/flutter_module_3d`，`feature/2.0.0`，HEAD `87eb605866ba65a61380d52527ab36e2af0fae33`，开始时工作区干净。
- 主服务：`projects/threeDService/Print3DService`，`feature/f_模型修复测试集成_20261029`，HEAD `aa03ddc266d0b4e34447d52bc71b730262792138`，开始时工作区干净。
- Camera：用户补充克隆 `projects/threeDService/Print3dCameraService`，`master`，HEAD `03ccde3faddd01e89cd06527585ded7f52241067`，工作区干净；已补查会话、凭据和多观看者源码。
- PMC：工作目录为 `feature/f_模型打印对比-master` / `1c01426046a91583192c68bfb60de145191afdf9`，干净，当前分支没有 WAN 实现。只读检查本地远端跟踪引用 `origin/feature/dev_epone优化` / `ebfe6d240e788ab6c5cb0c317fb06a8bdcf98ab5`，该版本有 WAN 命令和事件桥；未切分支、未合并，不能把该能力记到当前 PMC 分支。
- H5 工作目录仍为 `master` / `fb2bd646ca9fd2f96f3e4c4983d53f6caea560f2`，干净。用户要求继续分析后已 `git fetch --no-tags origin`，最新 `origin/master` 为 `85d5a0bce504609a524fa90bf79f80a14905a325`（2026-10-27），已有 `public/ep-one-camera/{index.html,config.js,player.js}`。本轮分析该远端快照，未 checkout、merge 或修改 H5 业务源码；新版源码已找到，无需用户另行提供。
- 当前步骤：H5 补充分析完成。已确认三个静态文件可复用，桌面必须补消息桥、会话控制和隐藏/恢复管理；临时补齐测试桩后完成原断言及 6 个兼容性场景。原有测试本身因 mock 过期失败，未修改仓库测试或业务代码。后续可按本记录进入实现，不再缺 H5 源码。
- 工具状态：本会话未暴露 CodeGraph MCP；CLI 不在 PATH，但 `~/.local/bin/codegraph` v0.9.6 可运行。Orca 索引存在、status 报 7 项变化；常规 Python SQLite 只读打开失败，使用 immutable 只读模式成功，抽查 MediaPlayCtrl、Monitor、StatusPanel、DeviceManager、GFDDeviceSelectionDialog、wxMediaCtrl2.cpp/.mm 共 7 文件均与源码 SHA256 一致。CLI query 成功；callers 对 SetMachineObject 未返回关系，但源码 Monitor 有调用，故调用关系以源码复核。未证明全库新鲜，无初始化或重建。Flutter、主服务、Camera 当前目录未发现 .codegraph，使用文档与源码。
- 验证边界：源码与官方文档复核、Node VM 模拟 DOM/TCPlayer 验证；没有运行 Orca/App、请求真实视频、构建、部署或操作真实设备，尚不能宣称 Orca 播放成功。源码默认配置不代表线上配置。

## 调查记录

- Flutter 业务入口为 `lib/features/ep_one/ep_one_camera_live_controller.dart`，同时包含 LAN 和 WAN，会话默认云直播。
- Orca `MediaPlayCtrl::Play` 的现有云链路要求 `bambu:///` URL，不能据已有播放区域推导支持 EPONE 的 WebRTC 会话。

## 结论与适用机型

可以把 EPONE 视频接入 Orca 的 GUI；现有版本不能仅粘贴视频 URL 完成接入。建议优先实现 WAN：复用现有 WebRTC 播放页，Orca 负责设备选择、授权请求和会话生命周期，后端复用现有 Camera 推流与观看者管理。

Flutter `ep_one_model_features.dart:17` 和 Print3D `DeviceCameraCapabilityResolver` 当前仅向 EPONEPro 提供摄像头产品入口；WAN 主服务和 PMC 协议接受整个 EPONE 系列，Camera 再检查固件 WAN v1、固定视频 profile、cameraEnabled 和 captureReady。因此“系列协议可用”不能推导基础款/Plus 具备摄像头或 App 入口。

GUI 是本功能入口；CLI 当前没有视频观看命令，实时显示、窗口生命周期和网页消息桥不适用于无界面切片 CLI。本轮不涉及切片算法、配置覆盖或 GUI/CLI 切片行为变化。

## 已核对的端到端链路

### Flutter 与公开 API

- `lib/config/base.dart:73`：`EP_ONE_CAMERA_USE_LAN` 默认 false，直接 WAN；打开开关后只用 LAN，不自动互切。
- `lib/features/ep_one/ep_one_gateway.dart:150`：公开请求沿用 `/app/print3d/api/v2/camera/lan-sessions/{prepare,heartbeat,stop}`。路径虽含 lan，`transport=WAN` 实际进入云直播。
- `ep_one_camera_live_controller.dart:159`：请求包含 schemaVersion、UUID requestId、deviceSn、UUID viewerId；WAN 心跳/停止还带 sessionId 和 generation。网络超时重试复用同一次请求身份。
- prepare 成功不等于视频可播；等待 `wan.playbackReady=true`、`protocol=WEBRTC`、有效 `playUrl` 和租约后挂载播放页。独立维护观看者租约与签名 URL 有效期；同流签名刷新不重建正在播放的 WebView。
- `lib/pages/device_detail/ep_one/ep_one_camera_web_preview.dart:84`：现有 Widget 只支持 Android/iOS；不能把 Dart Widget 原样嵌到 C++ Orca。可复用的是 H5 页面和协议。
- Flutter 调用 `window.epOneCamera.start({url,revision})`、pause/stop；接收 `EpOneCamera.postMessage` 的 ready、firstFrame、error、stalled 等事件。App 登录 token 不交给网页。

### Print3D、Camera、PMC 与媒体流

控制流程：客户端 → Print3D 用户鉴权与设备权限 → Camera 会话/签名/租约 → PMC MQTT 下发 → 打印机。

WAN 媒体流程：打印机通过 WHIP 推送 H.264 → 腾讯云 CSS → WebRTC 播放页 → App 或 Orca。Java 服务不转发视频帧，Camera Worker 的延时摄影合成不参与这条实时媒体链路。

- Print3D `CameraSessionFacade.java:25`：统一路由 LAN/WAN，WAN prepare/heartbeat 就绪时补齐播放 URL。
- `CameraWanSessionFacade.java:109`：校验当前用户存在、状态有效、设备未删、绑定角色 admin/user/invite、EPONE 系列等；每次相关操作继续鉴权。
- Camera `CameraWanSessionService.java:35`：已有有效 WAN 推流时加入观看者；否则检查占用、设备快照、在线/能力后创建会话。
- `CameraWanSessionService.java:193`：只有有效观看者和就绪推流才能取得签名 WEBRTC URL；签名生命周期不等于观看租约。
- `CameraWanSessionService.java:208`：固件凭短期 grant 领取 WHIP endpoint 与推流授权；桌面客户端不应调用此设备接口或接触推流密钥。
- `CameraWanProperties.java:23` 当前源码默认心跳 30 秒、租约 300 秒、播放 URL TTL 7200 秒、maxViewers 16，实际受服务配置及设备/会话截止约束；客户端使用响应字段，不硬编码默认。
- 固定 profile 为 `P4_H264_720P10_V1`，固件接入文档对应 H.264、1280×720、10 fps；实际固件采集和线上播放未测试。
- PMC WAN 源码在上述远端跟踪快照中：`CameraWanCommandController` 接收 `/pmc/internal/device/camera/commands`，service 按登记 MAC 定位设备，通过现有 IoT 下发；`DISPATCHED` 只表示 IoT 接受，不表示出帧。设备事件转发 Camera `/print3dCamera/internal/camera/device-events`。

### App 与 Orca 同时观看

WAN 已实现一路推流多个观看者，每个客户端生成独立 viewerId、分别续租。Camera `stop` 只将本观看者标为 LEFT；`activeViewers==0` 时才停止共享流。源码默认每设备 16 人，可配 1–64。这为 App 与 Orca 并看提供协议基础，仍需实际联调。

LAN 为 HTTP MJPEG：`http://<局域网IP>:8099/camera/live.mjpg`，状态地址 `/camera/status`，必须附 `X-Live-Session-Id` 和 `Authorization: Bearer <viewToken>`。Redis LAN 脚本限制一个有效观看者，不能按 WAN 多端能力推导 LAN 也可并看。LAN 媒体直连设备，但准备/续租仍调用云端业务 API，不是脱离服务端的离线观看。

LAN/WAN 之间存在占用协调；Camera 对固件明确声明 `cameraWanLanPreemption` 的场景支持接管，否则可能返回 DEVICE_BUSY。第一版不建议自行引入自动切换策略。

## Orca 现状与接入位置

| 位置 | 当前行为 | 需要补齐 |
| --- | --- | --- |
| `MediaPlayCtrl.cpp:248` | 使用 MachineObject/NetworkAgent，云 URL 要求 bambu:///，本地也采用 Bambu 封装 | 不直接作为 EPONE WebRTC 播放接口 |
| `StatusPanel.cpp:1056,4192` | 已有自定义摄像头 wxWebView；只 LoadURL 并切换可见窗口 | EPONE 会话控制、JS 启停与首帧/错误回调 |
| `MainFrame.cpp:4098` | BBL 设备进入 MonitorPanel，其余进入 PrinterWebView | EPONE 页面路由不能假定自动进入 BBL 状态页 |
| `GFDDeviceSelectionDialog.hpp:25` | 云打印选择结构已有 device_sn、MAC、类型等 | 增加明确的观看入口和当前设备绑定 |
| `GFDConfig.hpp:73` | 当前 GFD 登录是 manager/auth；设备查询也是管理端接口 | 管理主体与 App 用户主体的观看授权对齐 |
| `Widgets/WebView.cpp:238` | Windows 使用 Edge，macOS 使用 WebKit，Linux 使用平台 WebView；消息通道名 wx | 与 H5 的 EpOneCamera 通道适配并验证目标平台 WebRTC |

“自定义摄像头”是网页容器，但当前没有 prepare/heartbeat/stop、签名注入或 viewerId 管理。仅填写 H5 index.html 也不会自动取得可播放会话。LAN URL 同样缺请求头及租约，不应靠固定明文 URL 接入。

## 建议实现范围

1. 先按当前管理账号用途提供后端管理端观看入口：复用 Camera 内部会话服务，按管理账号的设备访问范围鉴权，并给观看主体稳定、不会与 App 用户混淆的身份。不能把 manager token 直接当 App token，也不能只凭 MAC/SN 放行。若改为 App 用户登录，则复用既有 App API，但需要相应账号入口。
2. Orca 增加 EPONE 摄像头会话类，持有 deviceSn、viewerId、sessionId、generation、租约、最新签名与请求代次；异步申请、心跳、停止，切设备/退出登录/关闭窗口清理旧会话并丢弃迟到响应。
3. 在 EPONE 设备页面或独立观看窗口使用 wxWebView，加载同一可信 HTTPS 播放页；通过 JS 传签名 URL，通过消息桥确认首帧和失败。仅显示视频无需为此重做温控、AMS 等整个 Bambu Monitor。
4. Orca 可注册与 Flutter 同名的 `EpOneCamera` JS 通道，或在 H5 执行前注入转发到 wx 的适配器；页面本身也可增加兼容分支。复用 TCPlayer、start/pause/stop 和真实首帧检测，无需复制整套 Vue 应用。签名 URL 不放导航参数或持久配置；Orca 的通用 `WebView::RunScript` 在 internal_developer_mode 会记录脚本文本，携凭据调用需避免进入该日志路径。
5. 优先 WAN，LAN 后续按需要增加带头 HTTP MJPEG 拉流与解码；不将 LAN 的单观看者/占用语义隐藏为普通播放失败。

这属于“复用现有媒体平台，新增桌面接入”，不是一行播放地址配置。设备已经满足现有 WAN 协议时，没有证据要求为桌面观看新增固件媒体协议或 Java 转码服务。

## H5 需要哪些代码

新版代码现已从 origin/master 找到并分析，无需用户再提供。接入所需仅为 `public/ep-one-camera/` 的三个文件和外部 TCPlayer SDK/样式，无需整个 H5 工程，也无需实际播放凭据或服务端密钥。当前本地工作目录仍是旧 master；查看源码应使用本记录的 Git 快照或明确更新到相应版本，不能误认旧目录已更新。

Flutter 默认 QA 页面为 `https://qa-mobile3dh5.wisebeginner3d.com/ep-one-camera/index.html`，正式为 `https://mobile3dh5.gongfudou.com/ep-one-camera/index.html`，可用 dart-define 覆盖。这里是源码推导的默认地址，未验证线上页面是否存在或其部署版本。

[腾讯 TCPlayer 官方接入说明](https://intl.cloud.tencent.com/document/product/266/33977?lang=en&pg=) 明确 PC/手机浏览器支持 webrtc:// 直播，5.x 需要域名 License。桌面 wxWebView 的具体运行时仍需实测，不能把 PC 浏览器支持等同于所有 Orca 安装包已支持。

## 文档与分支差异

- Camera README、WAN/App 接入文档仍有“LAN 失败后再 WAN”的叙述，但当前 Flutter 代码默认直接 WAN、两模式不自动互切；当前行为以 Dart 源码为准。
- Camera App 文档部分 LAN 示例写 8080 和 /camera/live；当前 Camera 默认和 Flutter 校验均为 8099 和 /camera/live.mjpg。
- Camera 文档示例部分租约/签名 TTL 与当前 Java 默认不同。接入以响应字段和实际部署配置为准。
- PMC WAN 代码只在另一个已核对的 Git 快照中；H5 新页面已找到于最新 origin/master，仍缺于未切换的旧本地工作目录。不能据多个 Git 快照能读取就认定当前工作目录组成可运行的配套版本。

## H5 补充分析（2026-10-31）

### 源码身份与组成

分析版本：H5 `origin/master`，`85d5a0bce504609a524fa90bf79f80a14905a325`，2026-10-27 合并提交。AGENTS.md 与当前工作目录相同，无 docs/ai 模块文档或 CodeGraph 索引。只更新 Git 远端引用，业务源码仍保持原工作目录状态。

| 文件 | 职责 | 复用判断 |
| --- | --- | --- |
| `public/ep-one-camera/index.html` | 全屏 video 容器、保留帧 canvas、继续播放按钮；加载 TCPlayer 5.1.0 CSS/JS | 独立静态页面，不依赖 Vue 路由、store 或 App 登录页 |
| `public/ep-one-camera/config.js` | 按页面 hostname 选择 qa/production 与 Web License URL | 当前两个环境用同一许可 URL；配置存在不代表任意新域名都被许可 |
| `public/ep-one-camera/player.js` | 152 行播放器控制、消息回调、首帧/停滞检测、隐藏停止与末帧保留 | 主要播放逻辑可复用；会话、授权、续租和重连决策都在宿主 |

Git blob 身份：index.html=`9bcd5dd67df484464e0158c11b72373bad17b227`；config.js=`e7978e3966b81d8a94eec9179a1087c3b3a0ed70`；player.js=`ed53b86bc512064377e6bbd6eedf72f0afefd5b6`。

### 已有播放能力

- player.js:51 接收 `start({url, revision})`，只接受 webrtc://，检查 TCPlayer、licenseUrl 和 RTCPeerConnection。
- :67 初始化 TCPlayer：静音、仅收视频、隐藏控件、禁用播放器上报/调试日志，`fallback=false`、`connectRetryCount=0`。网页不自行回退 HLS，也不承担自动重连策略。
- :92 / :123 首帧优先采用 requestVideoFrameCallback；旧内核以有视频尺寸且时间推进的 timeupdate 兜底。单纯 ready/HTTP 成功不算首帧。
- :98–103 发送 error、fallback、stalled；出首帧后连续 10 秒无帧报告 stalled。首帧前的超时由 Flutter 当前 12 秒计时器负责，Orca 也必须提供。
- :17–49 正常播放时缓存 canvas 画面，pause 会保留最近画面但 dispose 当前媒体连接；stop 清空画面与播放器。pause 不是暂停后直接调用旧 player.play()，恢复必须重新 start。
- :148–151 暴露 start/stop/pause；pagehide 或 document.hidden 时 pause，变为 visible 没有自动恢复逻辑。
- :31 回调仅发送事件和 revision，不回传播放 URL 或登录 token；宿主需按 revision 丢弃旧连接的迟到事件。

### 桌面接入的具体缺口

1. **消息通道名称与时机。** 页面仅调用 `window.EpOneCamera.postMessage`，Orca 当前通道是 `window.wx.postMessage`。不适配时 ready/firstFrame/error 全部丢失；页面仍可被 JS 调用，但宿主无法可靠管理状态。建议为专用视频 WebView 注册 EpOneCamera 并处理对应事件。若采用别名转发，应在 player.js 执行前完成；ready 只在末尾发送一次，加载完成后才注入可能错过 ready，需另外握手。
2. **完整会话控制。** 三个文件没有业务 API、设备选择、登录、heartbeat、stop 请求、签名刷新和权限管理。Orca 必须沿用上文控制流程，不能只 LoadURL。H5 start 每次都会 dispose 重建，所以同一路流只更新签名时，Orca 应像 Flutter 一样缓存新 URL，不反复调用 start。
3. **切页与恢复。** wx 窗口隐藏/重新显示、应用挂起、设备切换、退出账号都要显式处理；不能只依赖浏览器 visibility 事件必然跟随原生窗口。暂停媒体与停止观看租约是两个动作，应按产品策略控制。关窗口需调用媒体 stop，并停止/回收本端观看资格。
4. **桌面呈现。** index.html 将 video/canvas 设置为 object-fit:cover，窗口比例变化会裁掉画面。打印监控建议采用 contain 保留完整画面；“继续播放”等当前写死中文，正式桌面接入需覆盖英文。
5. **平台与许可。** 继续加载已授权的 HTTPS 页面更容易复用现有配置；Windows WebView2/macOS WebKit/Linux 目标运行时需分别确认 RTCPeerConnection、H.264、自动播放和许可验证。config.js 会按域名选配置，不会自动替新域名取得授权。
6. **现有 StatusPanel 辅助脚本不能直接套用。** `on_webview_navigating` 异步调用 remove_controls，后者假定已有 video 元素；此 H5 的 video 要等 start 才创建，且由 TCPlayer 自己管理控件。建议专用视频 WebView 避开这套通用 video 操作，保留明确的宿主控制。

这些缺口不要求重写 H5 播放器；最小可行实现为“Orca 会话控制器 + 专用 wxWebView + 同名消息通道”，再补桌面布局和英文。权限问题仍按前述管理登录与 App 用户登录的边界解决。

### 隔离验证结果

从上述 Git 提交导出三个生产文件和现有测试到仓库外临时目录，保存 SHA256/提交清单：`/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/epone-h5-analysis-20261031-q7c1ds8g/`。没有修改生产 player.js。

- Node v22.23.2 执行原测试 `node tests/ep-one-camera-web-player.test.cjs`，退出码 1：player.js:115 调用 controls(false)，测试 TCPlayer mock 没有 controls 方法。该失败是测试桩与当前源码不匹配，不证明真实 TCPlayer 不支持该方法。
- 临时副本补齐 controls、DOM 属性、querySelectorAll、MutationObserver 和 canvas 桩后，原测试的首帧、流替换、迟到事件、fallback、stop 与消息不含 URL 的断言通过。此结果不冒充仓库原测试通过。
- 额外 6 个 Node VM 场景通过：仅 wx 通道收不到 ready；执行前适配能接收 ready/firstFrame；pause 保留帧/释放播放器且新帧替换；隐藏后停止而重新可见不自启；缺 WebRTC 时先报错；SDK fallback 被拒绝并释放播放器。
- 全部是模拟 DOM/TCPlayer 的协议测试，不加载腾讯 SDK、不连接直播，不验证实际解码、跨平台 WebView、线上许可或视觉布局。原仓库测试应在进入实施时同步修复测试桩。

## 验证与后续验收

本轮仅文档/分析，无需编译应用或执行 Gradle；补充执行的 H5 模拟测试详见上节。H5 补充说明后，`python3 scripts/check_ai_module_docs.py --all` 再次通过：35 模块、归属文件 12963、明确排除 2525、问题 0；相关文档 `git diff --check` 通过。导出快照的三个生产文件及原测试 SHA256 均保持不变，H5 工作目录仍干净且停留于原 master。未执行视频 CLI/GUI 或真实流测试，未暂存或提交。

后续实施采用已核对的 H5 版本，并记录对应安装包与部署身份，再进行以下验收；本表不是当前已存在功能的立即手测要求：

| 场景 | 操作与预期 | 当前状态 |
| --- | --- | --- |
| 单端首帧 | 选择有权限且固件支持的 EPONEPro，点击观看；prepare 后等待真实 firstFrame 才显示播放成功 | 待实现/实机验证 |
| App/Orca 并看 | App 开始观看，再开 Orca；共享推流、viewerId 不同；关 Orca，App 继续播放 | 待实现/实机验证 |
| 退出/断线 | 分别关闭窗口、退出账号、断网；本端停止续租，其他观看者不受影响；最后观看者退出后按服务状态停流 | 待实现/实机验证 |
| 切设备与旧回包 | A 视频申请中切换 B；A 的迟到响应不得显示在 B 或停止 B | 待实现/实机验证 |
| 签名刷新 | 同一流播放中刷新签名不黑屏；断线后使用最新 URL 重连 | 待实现/实机验证 |
| 权限/占用/容量 | 无设备权限、设备离线、LAN 占用、达到实际观看人数上限时，分别明确提示，不误报播放成功 | 待实现/实机验证 |
| 平台与语言 | Windows/macOS 的实际 WebView 验证 TCPlayer、自动播放、首帧和关闭；Linux 若在发布范围内单独验证；中英文文案完整 | 待实现/实机验证 |

回传证据应包括各服务/客户端版本、设备固件、QA/生产环境、操作时间段、已脱敏日志和首帧/关闭结果，不回传 token、签名 URL 或 grant。
