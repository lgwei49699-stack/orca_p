# 下发打印后界面无响应：日志调查

## 当前执行状态

- 目标／边界：分析日志与转储后，用户已授权开始编码。修复 Windows 设备选择框隐藏后模态循环残留；不下发真实打印。用户提供 Mac 手测日志，并授权日志无阻塞问题后更新为 2.3.2.2-Release 并提交。本轮分析日志、更新版本、完成检查并提交；不推送、不操作虚拟机。
- 源码基准：`master` / `563f30abc97a5add8a73f1e7b1a01b82aa769d22`。开始时已有 `result.json` 修改及未跟踪 `2026-10-04-study-textbook.md`，均不改动。
- 输入：原日志包、10 月 9 日窗口 JSON、目标 PID 19568 日志及 copy-info、10:21:40 的 `orca-slicer.DMP`。日志和转储 PID／主线程／启动时间对应；转储包含关键二进制映像，已核对相关函数实际机器码。故障在同事 Windows，当前 Mac 仅分析端；精确构建提交和私有 PDB 尚缺。
- 已确认：打印机选择框 HWND 0x55131c 已隐藏，returnCode=wxID_OK、m_print_submitting=false，但自身 ShowModal 未返回；活动 wxModalEventLoop 仍 insideRun=true、shouldExit=false，wxWindowDisabler 仍存活并生效。主窗口 HWND 0x505CE 的 wx enabled=false 与先前原生 disabled 快照吻合。模态循环残留导致输入持续禁用的机制已确认；无法从事后快照证明具体哪一次隐藏／活动循环变化触发了退出遗漏。
- 当前步骤：Mac 日志确认取消与成功下发两次模态窗口均正常返回、parent_enabled=true；运行包包含本次日志，Mach-O UUID 与已验证构建一致。SoftFever_VERSION 已更新为 2.3.2.2-Release，Mac arm64 Release 构建和程序版本检查通过；本提交包含源码、回归测试、诊断工具、版本与相关文档。
- 工具：会话未暴露 CodeGraph MCP，CLI 不在 PATH 但绝对路径可用。旧索引声称最新，实际 Plater、bbs_3mf 哈希过期；执行一次增量 sync（新增 5、修改 16，退出码 0），随后 8 个目标文件 SHA256 全部匹配；使用 context/callers 辅助定位，动态事件绑定回读源码。
- 未验证：Windows 原生运行与同事现场尚未回归；当前 Mac 可以构建及运行公共 wx 循环测试，但不能代替 Windows 验收。CLI 没有这组模态窗口，共享导出／上传业务不变。
- 下一步：Windows 同步本提交后构建并完成原生回归，按下文清单验证嵌套弹窗、取消、切换应用与下发收尾。result.json、教材记录、原始日志与转储不在本提交范围；本轮不推送。


## 证据与判断

原始日志保留用户目录；记录不复制认证信息、设备标识及完整云文件地址。以下时间均直接取自日志，未换算执行主机时间。

### 目标会话时间线

文件：`debug_Thu_Oct_08_16_29_07_29848.log.0`，2,998,008 字节，SHA256 `2d03864226542594274aee1b51b0a17efcb9b1ea1843b3d74261bdd53bcb3e3b`。

| 时间 | 日志行 | 已确认事实 |
| --- | --- | --- |
| 16:29:07.319 | 2–5 | Windows GUI，版本 `2.3.2.1-Release`，简体中文；主线程 `0x00006d64` |
| 16:30:03.876–03.952 | 1947–1953 | 确认选中 1 台 EP3Plus；走普通 G-code，`use_3mf_file=false` |
| 16:30:03.953–03.956 | 1960–1989 | `apply_status=0`，复用已切片结果并完成临时 G-code 导出，无重新切片等待 |
| 16:30:03.982 | 1998–1999 | 导出完成，开始异步下发 |
| 16:30:04.257 | 2035–2036 | 上传文件 516,113 字节；OBS 返回 HTTP 200 |
| 16:30:04.526 | 2038–2039 | 打印命令返回 HTTP 200，`{"code":0,"msg":"success","data":true}`；表示服务接受命令，不能据此证明设备实际开始／完成打印 |
| 16:30:05.964 | 2040–2041 | worker 记录 `process 477ms + finalize 1077ms`，并删除临时打印文件 |
| 16:30:09.480–16:32:49.475 | 2042–2075 | 17 对后台 `process_task` 与主线程 `process_ui_task`；主线程响应延迟 0–1.013ms，后续完整日志全为这类事件 |
| 16:32:59.477… | 2076 | 文件最后一行不完整；没有正常 shutdown 记录。不能区分强制结束、采集时截断或其他终止方式 |

### 对照与无关错误

- 16:34:37 新启动会话在 16:35:12.724 再次收到成功响应，16:35:13.881 完成回调；到 16:37:39 仍有完整主线程备份记录。现象相似，但用户只确认 16:30 卡住，不能将第二次也断言为复现。
- 上午会话正常结束。下午启动阶段的耗材预设父项缺失、历史工程路径不存在、平台排除区域三角化错误均发生在下发之前，后续导出与上传成功；没有直接证据把它们列为本次触发原因。
- 浏览了全部 30 份日志的下发／正常退出摘要；详细因果分析集中于用户确认的会话。附件中未找到 `.dmp`／`.wer`／堆栈转储文件。

## 源码核对与解释边界

1. [MainFrame.cpp](../../../src/slic3r/GUI/MainFrame.cpp) 的 GFD 打印按钮投递 `EVT_GLTOOLBAR_PRINT_PLATE`；[Plater.cpp](../../../src/slic3r/GUI/Plater.cpp) 绑定事件并调用 `show_gfd_device_selection_dialog()`，以 `ShowModal()` 打开设备选择窗口。
2. `gfd_queue_print_upload()` 的成功 finalize（当前源码 11551–11556 行）先对“上传成功”消息调用 `ShowModal()`，返回后调用 `complete_gfd_print_submission()`。后者校验窗口／submission 身份并调用设备窗口 `complete_print_submission(true)`。
3. [GFDDeviceSelectionDialog.cpp](../../../src/slic3r/GUI/GFDDeviceSelectionDialog.cpp) 的 `complete_print_submission()`（775–783 行）清除 submitting 标记并 `EndModal(wxID_OK)`。外层 `show_gfd_device_selection_dialog()` 返回后取消残余任务并销毁设备窗口。
4. [PlaterWorker.hpp](../../../src/slic3r/GUI/Jobs/PlaterWorker.hpp) 在 `m_job->finalize()` **返回之后**才写耗时日志。因此 2040 行支持“完成回调已返回”；在匹配当前成功路径的前提下，也支持成功消息的 `ShowModal()` 已返回。日志未记录两个窗口的实际可见／启用／销毁状态，不能据此证明所有模态状态已恢复。
5. [MainFrame.cpp](../../../src/slic3r/GUI/MainFrame.cpp) 的 `EVT_BACKUP_POST` 调用 `run_backup_ui_tasks()`，再进入 [bbs_3mf.cpp](../../../src/libslic3r/Format/bbs_3mf.cpp) 的 `process_ui_task()`。线程 ID 与启动主线程相同，并非只有后台线程还活着。
6. 检查了消息对话框、worker 调度、按钮鼠标捕获及本地 wxWidgets Windows 模态实现。正常的嵌套 `ShowModal()`／`EndModal()` 序列本身不能证明缺陷；未发现足以仅靠静态阅读确认本次必现卡住的语句。不能将“先析构消息框再结束设备框”或强行 `Enable(true)` 宣称为已验证修复。

本地 wxWidgets 快照 HEAD 为 `139e4f2a62a9d1c40bdcf36523d94a517b14ca79`；只用于理解当前依赖实现，没有证据证明用户 Windows 包使用完全相同依赖。运行日志的版本号及相同日志字符串也不足以绑定当前仓库 HEAD。

## 10 月 8 日阶段性判断（后续现场见下文）

| 判断 | 证据／限制 |
| --- | --- |
| 已确认：服务端已接受本次下发 | HTTP 200 与业务 success；不等价于设备执行完成 |
| 已确认：该时间段并非主线程持续不再处理事件 | 主线程连续 17 次及时处理自定义 GUI 事件；不能排除间歇性阻塞或仅某类输入／渲染异常 |
| 优先候选：发送完成后的窗口模态／启用状态残留 | 用户描述为“点不动”，主事件循环继续运行，时点紧随嵌套对话框收尾；缺少 `IsEnabled/IsModal/IsShown` 现场数据 |
| 次要候选：其他隐藏模态窗、输入捕获或渲染交互状态异常 | 同样可与事件循环存活共存；日志没有窗口清单与捕获状态 |
| 暂无支持：上传卡住、一直重新切片、完成回调永久阻塞 | 各阶段都有完成证据；不能以此排除其他时间段的独立问题 |

用户后续补充：“好像”未点击成功确认，切回 Orca 时看不到确认弹窗。这是存在不确定性的操作回忆，不能改写为已确定未点击。它提高了弹窗显示／焦点／窗口状态路径的排查优先级；同时日志中的 finalize 返回限制了“成功弹窗一直藏在后面等待确认”的解释，需现场窗口清单或关闭路径日志进一步区分。

## 第二轮：复测补充与关闭按钮分析

用户重新操作确认：成功弹框不点击时会一直存在；原故障中主窗口右上角关闭按钮也无法点击。本轮未收到新复测日志，不能把原 16:30 日志当作新复测的运行证据。

### 源码确认的机制

- 设备选择窗口本身以 `ShowModal()` 打开；成功提示又在其内部 `ShowModal()`。本地 wxWidgets Windows 实现通过 `wxModalEventLoop` 创建 `wxWindowDisabler`，暂时禁用其他顶层窗口。因此主窗口关闭按钮点不了与模态限制一致，单凭这一点不能判断线程死锁。
- `GFDDeviceSelectionDialog::accept_selection()` 第 748 行将 `m_print_submitting` 置 true；服务器成功不会立即清除它。`Plater.cpp` 第 11555 行先等待成功提示返回，第 11556 行才调用完成函数，最终在设备窗口第 780 行清 submitting、第 782 行结束外层模态。
- 设备窗口第 440–458 行同时规定：submitting 为 true 时，关闭请求会被 veto，Esc 会被拦截。这意味着用户仍在等待确认成功消息时，设备窗口也不允许自行关闭。
- 这是明确的状态耦合：业务下发已经完成，但“正在下发”的界面限制仍依赖成功提示退出。若提示不可见／无法操作，主窗口与设备窗口都会缺少正常的可操作退出入口，而备份等事件仍可运行。
- 正常模态恢复按层退回：成功提示退出时恢复设备窗口，设备窗口退出时再恢复主窗口。嵌套本身是合法机制；没有现场窗口状态，不能认定恢复漏掉的具体位置。

### 对原故障的结论限制

正常复测中“不点提示，主窗口关闭不了”有明确代码解释；原故障“提示看不到，主窗口也点不了”更支持窗口模态／启用状态问题。但旧日志在 16:30:05.964 已有 finalize 返回，因此在运行代码与当前成功路径一致的前提下，不能同时声称该成功提示此后仍一直在 `ShowModal()` 中等待。

尚需区分：成功提示以未记录的方式退出后模态状态未正确恢复，其他模态窗口仍持有禁用状态，或运行包实现与当前源码存在差异。没有证据选择其中一个作为已证实根因；主线程仍处理事件这一结论不变。

### 修正方向（方案，未实施）

将“业务完成”和“用户读完成功提示”分开：在确认下发结果后完成任务收尾，正常退出设备选择窗口及其模态循环，再通过主窗口的非模态通知显示结果。即使提示被遮挡，用户仍能操作和关闭主窗口。避免简单给主窗口强行 `Enable(true)`，因为此时可能还有有效模态窗口。

如果保留成功对话框，应在外层设备选择窗口完整退出后、下一轮事件中用主窗口作为父窗口展示，显式记录显示／关闭和主窗口状态；这仍会有等待确认时的正常模态限制，不能保证彻底避免隐藏提示带来的操作阻断。以上仅为后续实现方向，不是已验证修复。

## 第三轮：只追查故障原因

用户明确需要故障原因分析，前述交互改进方案不构成根因结论。本轮继续检查事件入口、回调重入、模态退出、备份导出及历史日志，未修改业务代码。

- **关闭路径未进入**：当前 `MainFrame.cpp` 的 close handler 首条语句就记录 `mainframe received close_widow event`，随后才检查导出、gizmo、保存确认等退出条件。目标日志没有该入口，也没有其 veto 分支日志。因此，在运行包实现一致、日志时间范围覆盖点击的前提下，不能解释为关闭时卡在保存工程或退出清理；输入未进入正常 close handler 更符合现有证据。
- **完成回调计时不是网络耗时**：再次核对队列弹出后才调用 finalize，计时日志在 finish 回调返回后写出。历史 `debug_Wed_Sep_30_11_44_28_31536.log.0` 中相同两组 lambda 类型标识的任务，9 月 30 日 18:29:59 响应成功、10 月 8 日 09:23:40 才记 finalize 返回，finalize 为 658,432,989ms。这与 finalize 可包含漫长模态等待相符，但不能单凭它断言历史会话期间用户做了什么。目标会话 finalize 仅 1,077ms，原故障持续期间没有仍在该成功 finalize 中等待的证据。
- **备份导出未显示与故障收尾重叠**：目标最后一次实际备份写文件发生在 16:29:49；16:30:05 后只有备份检查，没有新的 `export_3mf` 记录。当前 Backup 策略包含 Silence，无进度回调时不会创建导出进度框或在导出轮询中泵全局 UI。没有证据把本次归为导出进度框覆盖模态事件循环。
- **未证实队列锁死**：`ThreadSafeQueueSPSC::consume_one()` 在调用消息处理前释放锁并弹出队列项，不是持队列锁打开成功模态框。worker 的等待／join 若永久阻塞主线程，也与故障后继续及时处理 wx 备份事件不符。不能因此保证所有重入路径正确，但没有建立该队列死锁的因果链。
- **正常关闭顺序本身未证明错误**：当前 wx Windows 模态退出释放该层 disabler；成功框返回后才调用设备框 EndModal，正常情况下会逐层恢复。查到嵌套调用不等于查到此次恢复失败的触发条件。

现有证据能够排除或降低上传未完成、该 finalize 长期等待、关闭保存阻塞、主线程持续死锁等解释；不能决定是主窗口仍 disabled、其他模态循环残留、输入捕获／分发异常还是运行包差异。缺失的决定性数据是故障现场主线程栈、主窗口原生 enabled 状态及所属／可见模态窗口。根因状态应明确写为**尚未定位**，不能为了给出确定答案把推测升级为事实。

## 第四轮：重启恢复与现场采集

用户确认“重启后可以正常下发”。这说明故障不是每次下发都必现；重启会重建窗口与任务状态，正常恢复不能选择性证明模态残留、鼠标捕获或任一具体根因，也不能证明原问题已修复。

继续核对 Windows 顶层窗口析构、模态 Show/Hide/EndModal、主循环异常处理：正常 EndModal 应先退出该层循环并恢复禁用窗口，再隐藏窗口；没有找到能由本次日志证明被触发的异常收尾分支。原成功框 finalize 已返回这一约束仍成立，不能倒退为“同一个成功框一直等待确认”。

已准备 [capture_windows_gui_state.ps1](../../../scripts/capture_windows_gui_state.ps1) 和配套 [双击启动器](../../../scripts/capture_windows_gui_state.cmd)。这是独立取证工具，不改 OrcaSlicer 程序。优先将一键采集 ZIP 复制到 Windows，完整解压后双击 `双击采集.cmd`；无需手输命令。启动器通过自身目录定位 PS1，以当前用户权限运行；在 32 位调用环境可用时选择 Sysnative 的 64 位 PowerShell。出错或结束会保留控制台文字，方便查看。

出现中文切回提示后，在 8 秒内切回 OrcaSlicer，保留卡住现场。完成后会打开资源管理器并选中桌面 JSON。多个进程时直接在提示中输入进程 Id。若需要手动调用，仍可在脚本所在目录打开 PowerShell 执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\capture_windows_gui_state.ps1
```

该参数只对这次 PowerShell 进程设置执行策略。8 秒后采集 3 次，输出桌面 `orca-gui-state-日期时间-随机后缀.json`。手动调用遇到多个进程时可用 `-OrcaProcessId 实际进程号` 指定；双击启动器已启用交互选择。默认权限查询失败会保留错误，不自动提权。也可在正常状态采一次作对照，但正常快照不能替代故障现场。脚本不会尝试启用 Orca 窗口、结束进程或重新打印；只在采集全部完成后打开资源管理器，避免影响采样时焦点。窗口标题可能包含工程名。

| 现场结果 | 能推进的判断 | 仍不能证明 |
| --- | --- | --- |
| 主窗口 disabled，消息探测成功 | 主窗口被禁用且仍处理消息；沿 owner/lastActivePopup 核对是否存在有效对话框 | 不能仅凭 disabled 判定 wx 恢复出错，正常模态也会禁用主窗口 |
| 主窗口 disabled，但未发现可操作的对话框 | 重点检查隐藏／屏幕外／cloaked 窗口及模态收尾 | 外部快照不能识别所有 wx 内部 disabler，需栈或生命周期日志定位具体语句 |
| 主窗口 enabled，capture/menuOwner 指向其他句柄 | 转查捕获／菜单循环／焦点分发 | 单次捕获信息可能是正常瞬态，需连续样本和操作对照 |
| WM_NULL 多次失败 | 结合错误码、窗口是否还存在及同时段日志，决定是否主线程转储 | 失败并不统一等于死锁；权限／销毁竞态也可失败 |

API 依据：[GetGUIThreadInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo) 可查询其他 GUI 线程的活动／焦点／捕获状态；[IsWindowEnabled](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindowenabled) 查询原生输入启用状态；[SendMessageTimeoutW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw) 的返回零包括超时或失败，脚本按文档清零并保留 last-error，不直接标记“死锁”。采样是顺序快照，有观察时序影响，不能取代线程栈。

脚本验证状态：完成静态签名／结构／只读操作审阅；本机没有 PowerShell、dotnet/csc/mcs 或 Windows 执行环境，未执行 PowerShell 解析、C# 编译、Windows API 运行，不能记为脚本运行通过。未改应用业务代码，不需要用本机 macOS 应用构建证明 Windows 取证工具有效。新增脚本后模块检查通过：35 个模块、12,871 个归属文件、2,485 个明确排除、问题 0；`git diff --check` 通过。未暂存或提交。

## 第五轮：成功弹窗消失的解释边界

用户追问“那弹窗为什么看不到了”。必须把“此时窗口已经不在”和“具体什么事件导致退出”分开：在当前源码成功分支中，`msg_dlg` 是 finalize lambda 的局部对象，`ShowModal()` 返回、设备完成函数执行、局部对象析构之后，外层 `PlaterJob::finalize()` 才写出 16:30:05.964 那条耗时。因此若运行包匹配该成功分支，用户随后切回时看不到该成功框，与它已经退出／析构相符；单纯被其他窗口遮挡不能解释该调用已经返回。日志仍未直接记录运行时实际进入哪个 UI 分支，源码与 Windows 二进制身份限制仍适用。

退出入口核对：

- `MsgDialog.cpp:174` 的确认按钮事件调用 `EndModal(btn_id)`；没有成功提示自动超时关闭的定时器。
- `GUI_Utils.hpp:193–203` 的 DPIDialog 基类把 Esc 转成 Close；本地 wx `dlgcmn.cpp:511` 的默认 close handler 最终可调用 EndDialog；标题栏关闭也可走这个入口。没有证据表示用户当时按了这些键或按钮。
- `Button.cpp:436–442` 将空格／回车按下和松开转为鼠标按下／松开处理。成功框 OK 按钮创建时请求焦点，但实际焦点状态没有日志。
- **具体可疑实现**：`Button.cpp:430–434` 的 `mouseCaptureLost()` 创建默认 `wxMouseEvent` 并调用 `mouseReleased()`；本地 wx 的默认坐标为 `(0,0)`。当该按钮此前 `pressedDown == true` 且尺寸非空时，`mouseReleased()` 判断坐标位于按钮内，会发送点击事件，进而使成功框 EndModal。丢失捕获在这里可能被转成“确认”，而不是仅取消按压。前提不能省略：按钮必须已处于按下状态；`Button.hpp` 默认初始化为 false，因此单纯切换应用并不必然触发此路径。这是源码层面的可疑行为，未进行 Windows 复现。

本次没有按钮按下／捕获丢失／Esc／窗口关闭事件日志，调用处也没有保存 `ShowModal()` 返回值，故不能选择上述任一入口作为事实。即使确认了意外 EndModal，正常模态恢复仍应恢复父窗口；还需要独立解释后续主窗口不能操作，不能把这条按钮路径直接称为完整卡死根因。

## 10 月 9 日：同事 Windows 故障现场已收到

用户先确认这次“点过成功提示的确定，随后点不动了”，后补充“可能是切到其他的界面操作，回到 orca 就卡住了”。确切发现时点和切换顺序仍有不确定性；不能改写为点击立即必现或 Alt+Tab 必然触发。故障发生在同事电脑，不能要求在用户当前 Mac 执行 Windows 操作，也不把当前工作区程序身份当成同事 EXE 的构建身份。

输入 JSON：`orca-gui-state-20261009-093847-9508dbb8.json`，557,824 字节，SHA256 `605bfd8f8d0b7d65c4680845675ccd16238596652458e2616882c807cd4c4007`。原文件留用户微信目录，不复制原始诊断内容入仓库。

- 运行进程：PID 19568，启动于 `2026-10-09T08:55:39.8721857+08:00`，路径 `D:\kfd\OrcaSlicer\orca-slicer.exe`。
- EXE SHA256：`6938C9E941A3543C6C094346A401A83DBD821124F89367D08B61B04FAC5AEA41`；fileVersion 为空，不能仅凭空版本判断文件损坏，也不能与旧包／当前源码自动等同。
- 系统报告：Windows NT `10.0.26300.0`，采集器 64 位，PowerShell `5.1.26100.9549`。
- 采样时段：09:38:44.836–45.038、09:38:46.055–46.238、09:38:47.256–47.441（均 +08:00）；每次枚举 83 个窗口，83 次 WM_NULL 探测均成功，总计 249/249。主窗口与打印机选择窗口每次探测耗时均显示为 0ms（毫秒取整，非严格零耗时）。

### 三次一致的决定性状态

| 对象 | 原生状态 | 解释 |
| --- | --- | --- |
| 主窗口 `0x505CE`，`未命名 - OrcaSlicer` | `exists=true`，`visible=true`，`enabled=false`，`disabledStyle=true`，style=`0x9F0F0000`，`minimized=false`，`cloaked=0` | 窗口存在、显示，但被明确禁止接收输入；这直接解释点击／右上角关闭无效 |
| 打印机选择 `0x55131C` | `exists=true`，`visible=false`，`enabled=true`，style=`0x86CC0084`，owner/rootOwner=`0x505CE`，`minimized=false`，`cloaked=0` | 原生窗口还活着，已经隐藏；它属于主窗口，既非最小化，也不是仅被其他窗口遮挡 |
| 打印机选择位置 | `(720,404)–(1840,1054)`，`intersectsMonitor=true` | 没有移到屏幕外的证据；不显示的直接状态是 hidden |
| 成功提示 | 三次枚举均没有“上传成功”窗口，也没有 owner 指向打印机选择的顶层窗口 | 与用户已点击确定、成功提示退出一致；当前无需用“成功提示藏在后面等待确认”解释 |
| GUI 线程 19572（`0x4c74`） | GUI 查询成功，capture/menuOwner/moveSize 全为 `0x0`，主窗口消息探测成功 | 采样时不是鼠标捕获／菜单循环占用，窗口线程仍能处理探测消息；不能据此证明所有任务正常 |

三个样本里只有主窗口 `visible=true`，其余 82 个均隐藏。禁用的其他窗口属于 IME／ComboLBox 等辅助控件，不能把它们当作锁住主窗口的业务弹窗。前两次 foreground 为零，第三次为 Windows Terminal 窗口；所有 Orca GUI 线程 active/focus 为零不能独立认定为焦点 bug，也不能声称采样时 Orca 确实取得前台。

Win32 依据：[IsWindowEnabled](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindowenabled) 查询是否启用鼠标／键盘输入；[IsWindowVisible](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindowvisible) 检查 WS_VISIBLE，而被遮挡的窗口仍可能返回 true。因此本次不是单靠用户主观“看不到”判断 hidden。

### 源码故障点与仍缺的最后一环

当前正常调用顺序是 `Plater.cpp:11555` 成功提示 ShowModal 返回 → `complete_gfd_print_submission()` → `GFDDeviceSelectionDialog.cpp:782` EndModal → 外层 `Plater.cpp:11620` ShowModal 返回并销毁局部选择窗口。现场仍存在隐藏选择窗口且主窗口 disabled，使排查重点从成功提示自身转到**外层设备选择窗口的退出／恢复过程**。但仅有 HWND 不能直接读取 wx 的 IsModal、活动事件循环或栈上局部对象状态。

本地 wx Windows 实现有一条与现场精确相容的失败路径：

1. `src/msw/dialog.cpp:195` 的 EndModal 调用 Hide，进入 `Show(false)`。
2. `Show(false)` 第 141 行先调用模态循环 ExitLoop，之后第 155 行仍会隐藏原生窗口。
3. ExitLoop 调用 `wxEventLoopBase::Exit()`；`src/common/evtloopcmn.cpp:92` 要求目标循环为当前 active，否则 `wxCHECK_RET` 提前返回。
4. 若该条件失败，ScheduleExit／OnExit 不执行，`wxModalEventLoop::OnExit()` 中的 `wxWindowDisabler` 释放就未发生，但第 2 步仍可能完成 Hide。这样可以出现“选择窗口已隐藏、ShowModal 尚未正常退回、主窗口仍 disabled、主线程继续处理事件”。

这是一条**已核实代码存在、尚未核实现场触发**的候选因果链。上游 [wx 事件循环说明](https://github.com/wxWidgets/wxWidgets/blob/master/interface/wx/evtloop.h) 同样约束 Exit 只用于当前活动循环；不能据此认定同事二进制一定执行了该拒绝分支。外层窗口退出后又被其他机制禁用、wx 与原生状态不一致等仍需通过线程栈／生命周期日志区分。没有证据将正常的嵌套 ShowModal 本身认定为必然错误。

复核切回入口：当前 `MainFrame.cpp:3176` 激活事件进入 `Plater::on_activate()`，后者只在有延迟错误时显示错误提示；没有查到该入口直接隐藏设备选择或禁用主窗口的语句。现场也没有可见错误提示。因此“切走／切回造成状态异常”仍只是待验证时序，不能由用户回忆自动选定此分支。上轮按钮 capture-lost 合成点击的候选不再是解释本次提示消失所必需的假设，因为本次用户明确点过确定。

当前所缺为同事此次进程（08:55:39 启动、09:38 左右故障）的应用日志和故障主线程栈。旧 10 月 8 日日志可作同类现象对照，不能充当今天 finalize 已返回的直接证据。后续诊断应记录成功框返回值、设备 EndModal 前后、外层 ShowModal 返回及析构，并将确认下发按钮入口时的活动事件循环身份与 EndModal 时身份作比较；不是直接强制 Enable 主窗口来掩盖未退出的模态状态。

## 10 月 9 日：补充日志包缺失目标会话

收到 `/Users/liwei/Downloads/OrcaSlicer 2/`，目录中 1,270 个文件、29 份日志，未找到 dump／WER。用户说明导出时有一份日志“正在使用”，已跳过。核对结果如下：

| 包内当天日志 | 时间范围 | 主线程 | 判断 |
| --- | --- | --- | --- |
| `debug_Fri_Oct_09_09_32_17_30788.log.0` | 09:32:17.582–09:33:33.368 | `0x7854` | 09:33:31 进入正常关闭，最后 GUI_App 析构退出；不是 PID 19568 |
| `debug_Fri_Oct_09_09_32_19_31040.log.0` | 09:32:19.337–09:33:15.069 | `0x7984` | 09:33:11 进入正常关闭，最后 GUI_App 析构退出；不是 PID 19568 |

第一份为 362,231 字节、1,693 行，SHA256 `a75c554f557de1eba29b80cab9b7f223d8f697a230e8ffab05199bd7f5ad7440`；第二份为 400,294 字节、1,903 行，SHA256 `3b315fb5e5b808d14f67161469ec74c4e8884983f7f42b7ed553bffe7b87cc2c`。两份均未发现 GFD 下发入口／选择设备记录，不可把其中预设／启动错误解释成故障进程的下发卡住原因。两份启动日志都写明未找到其他实例或未启用单实例，不能断言它们只向故障实例转发了消息。

故障快照对应 PID **19568**，启动 **08:55:39**、主线程 **0x4c74**，在 **09:38:44–47** 仍存在。包内无 `19568` 文件名，也无 08:55 启动的当天日志，因此当前被使用而跳过的文件很可能正是目标日志；但用户尚未给出被跳过的具体文件名，不能把“很可能”写成已核对同一文件。

独立新增对照证据：`debug_Thu_Oct_08_16_34_37_3948.log.0` 在新包中由此前截取的 500,988 字节扩为 1,354,080 字节、7,489 行，SHA256 `fd5d9cfe23eade3df8737b9c20da8bfd867d9a0619c150a6cce47c7e1548c62d`，覆盖至 10 月 8 日 20:04:03。16:35 之后还在 16:42、17:41、17:47、18:21、19:12、19:56 再次进入下发，最后正常关闭。因此旧 16:34 会话并没有在 16:35 成功后持续不可操作；这也说明旧副本末行截断本身不能证明进程被杀。其他同名旧日志内容哈希未变化；不重复分析完整历史包。

下一步只需补充同事电脑上 PID 19568／08:55 左右启动对应的日志（先按文件名筛选），可先尝试单独复制到桌面再发送；若仍被拒绝读取，应保留具体占用错误并确认读取方式，不能为了复制默认结束仍可取证的故障进程。本轮没有操作同事电脑，也没有新增恢复／解锁动作。

本轮仅核对输入身份及必要日志里程碑、更新调查记录；模块检查和差异空白检查通过，应用源码未改，未构建、暂存或提交。

## 10 月 9 日：正在使用的日志复制交接

用户反馈文件提示正在使用，询问处理方式，随后澄清目前直接压缩 AppData 下整个 OrcaSlicer 文件夹。因此已确认的是压缩时占用，尚未确认普通单文件复制失败；首先单独复制目标日志到桌面再发送或压缩副本，保持故障进程。

备用方式：新增 [copy_windows_active_log.cmd](../../../scripts/copy_windows_active_log.cmd) 和 [copy_windows_active_log.ps1](../../../scripts/copy_windows_active_log.ps1)，提供本地交付物 `build/diagnostics/orca-active-log-copy-one-click.zip`。同事完整解压、双击 `双击复制正在使用的日志.cmd`，在选择窗口中选被占用的日志（本次优先找文件名含 `19568`／`08_55` 的目标文件）。默认打开 `%APPDATA%\OrcaSlicer\log`，自定义数据目录可在窗口中导航选择。

工具以 `FileAccess.Read` 加 `FileShare.ReadWrite | FileShare.Delete` 打开日志，不更改源文件或结束源进程。按打开时文件长度复制到桌面的独立 `Orca-log-copy-日期时间-GUID` 目录；追加内容不追读，复制中截短则失败且副本保留 `.partial` 后缀。成功后自动定位副本，发送该日志及可选 `copy-info.json` 即可。失败会保存异常类型／错误码／已复制字节数到 `copy-info.json`，控制台保留错误信息。

边界：这种方式解决的是打开方式与现有写句柄不兼容的一类情况，不能强行覆盖其他句柄拒绝读取或 ACL 权限限制。[FileShare 定义](https://learn.microsoft.com/en-us/dotnet/api/system.io.fileshare?view=netframework-4.8.1) 和 [CreateFileW 共享规则](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew) 要求现有访问与新的共享模式兼容；设置更宽的共享范围不等于解锁。文件选择器 `ValidateNames=false` 避免在实际共享读取前先被选择窗口的共享冲突检查挡住；路径存在、文件类型由脚本继续检查，实际权限仍由操作系统验证（[文件选择窗口选项](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-_fileopendialogoptions)）。

本次检查：ZIP CRC／逐字节内容与源码一致、PS1 UTF-8 BOM、CMD CRLF 均通过；新工具尚未在 Windows 实测，没有声称已经复制出目标日志。源码已登记 test-tools 模块，未改应用代码；不默认结束进程、关闭文件句柄或强制启用 Orca 窗口。

## 10 月 9 日：目标进程日志补齐后的收敛

新输入 `debug_Fri_Oct_09_08_55_40_19568.log.0`，578,681 字节，3,005 行（末行只有 `[`，最后完整记录在第 3,004 行），SHA256 `fdd917fa71d01ca6a6b1413ce079fdaa61288367388ee09ace497f5f5b975ad1`。

`copy-info.json` 显示 10:09:10.745–10:09:10.813 共享只读复制成功，源打开长度／读后长度／复制字节均为 578,681；实测文件 SHA256 为 `fdd917fa71d01ca6a6b1413ce079fdaa61288367388ee09ace497f5f5b975ad1`，与元数据完全一致。文件名 PID 19568、启动日志 08:55:40.474、主线程 0x4c74 与既有快照对应。此结果证明复制核心路径在同事 Windows 上成功；不证明普通复制此前一定失败，也不把活动文件读取当作原子快照。最后完整日志为 10:07:44，末行不完整不能单独认定崩溃或停止写入。

| 时间 | 日志行 | 本次目标进程的直接证据 |
| --- | --- | --- |
| 08:56:33.528 | 1996、1999 | 点击 GFD 打印，进入设备选择；该进程只有一次此下发入口 |
| 08:56:34.710 | 2005 | 设备查询作业 finalize 已返回，process 729ms + finalize 383ms |
| 08:56:34.711–34.717 | 2007–2053 | 备份 3MF 导出发生在确认下发之前，并已有完成记录 |
| 08:56:38.704 | 2058、2061 | 选择 1 台 EP3Plus，普通 G-code 路径，开始导出 |
| 08:56:38.736 | 2107 | 开始 submission_id=1 的异步下发 |
| 08:56:39.022 | 2144 | 上传 HTTP 200，文件大小 542,603 字节 |
| 08:56:39.317 | 2147 | 打印命令 HTTP 200，code=0、msg=success、data=true；只证明服务接受，不替代实机打印确认 |
| 08:56:44.516 | 2148–2149 | 打印作业 process 521ms + finalize 4,847ms 已返回，随后临时文件清理 |
| 08:56:44.689–10:07:44.690 | 2150–3004 | 427 对周期备份队列消息持续转到主线程处理，后台投递到主线程记录延迟 0–21.917ms；不等于执行了 427 次工程文件保存 |
| 09:38:44.695 | 2655–2656 | 窗口采样同一秒主线程仍处理队列消息；与原生探测成功互相印证 |

第 2149 行之后，除周期消息，仅有第 2318 行 09:10:40.504 的 `studio is inactive, stop to subscribe`；没有再次打印入口或主窗口关闭入口。全日志 17 条 error 均在启动阶段，最后在 08:55:46，类型为预设父项／启动加载问题；下发收尾附近无 error。不能把这些启动错误当作当前窗口失去输入的证据。

### 当前能定位到哪里

1. 此次日志补齐了“当天 finalize 已返回”的直接证据。按当前源码 `PlaterWorker.hpp:82–90`，耗时日志在回调返回后写出；结合用户明确点过成功确定，不能再解释为成功提示仍在等待确认、上传仍在等待或 finalize 永久阻塞。
2. 快照主窗口原生 disabled 直接解释菜单／右上角关闭都点不了；周期消息和 249/249 原生探测成功反对“主线程在故障期间持续阻塞”的解释。打印机选择原生 HWND 存活但 hidden，使最可疑范围仍为外层设备选择的模态退出和输入恢复。
3. 当前成功路径为 `Plater.cpp:11555` 成功框 ShowModal 返回 → `complete_gfd_print_submission()` → `GFDDeviceSelectionDialog.cpp:782` EndModal → 设备选择 ShowModal 返回／析构。日志缺少后三者的独立标记，finalize 返回不能单独证明外层 ShowModal 已返回。
4. 此前查到的 wx `Exit()` 检查失败后 Hide 仍继续的路径依然符合现场，但还没有活动循环指针／调用栈证据证明实际走过。新增核对：`deps/wxWidgets/wxWidgets.cmake:34` 设置 `wxBUILD_DEBUG_LEVEL=0`；wx `debug.h` 明确 wxCHECK 仍执行条件并提前返回，只是不输出断言。故没有 `Use ScheduleExit() on not running loop` 报错不能排除该路径；同事依赖的实际构建参数仍未验证。
5. `GUI_App.cpp:4269–4279` 的 is_studio_active 检查 last_active_point 与 15 分钟阈值（`GUI_App.hpp:33`），09:10:40 的 inactive 是活动超时／订阅状态，不能当作 Windows 焦点变化、切换应用时刻或触发卡住的原因。当前日志没有证明 Alt+Tab 导致失效。

结论口径：**已确认主窗口遗留禁用状态造成无法操作；高度怀疑设备选择模态退出／恢复不完整；具体内部失败分支尚未证明。** 普通日志能够支持这一层结论，不能单凭新日志升级为特定源码语句根因已确定。

### 当前现场的下一份有效证据

若同事的这一个故障进程仍在，使用 Windows 自带任务管理器即可：

1. 按 Ctrl+Shift+Esc，进入“详细信息”，找到 `orca-slicer.exe`，确认 PID **19568**；不要选择此前正常退出的其他实例。若已重启，旧现场不能补采，新 PID 需与新窗口快照对应。
2. 右键该进程，选择“创建内存转储文件”／“创建转储文件”，等待完成后按提示打开保存位置，保留 `.dmp`。不要结束进程、强制 Enable 窗口或再次下发打印来取证。微软的 [用户态进程转储步骤](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/task-manager-live-dump#create-a-memory-dump-for-a-user-mode-process) 支持该入口。
3. 将转储作为私有诊断文件保留，它可能含登录凭据及工程内存；分析时同时需要该运行版本的 EXE 与匹配 PDB（若已有）以可靠解析项目函数，不用当前 Mac 构建替代。

转储分析目标是主线程 **19572 / 0x4c74**：区分仍停留在外层 ShowModal、另一个嵌套循环／析构阶段，或已返回主循环却保留 native disabled；结合活动循环和 wx 与 native 启用状态才能选择修复位置。转储未收到前，不承诺一定可从无符号转储还原具体源码行。

本轮没有修改应用逻辑或新增采集工具；不需要为日志分析构建应用。GUI 故障修复未完成，CLI 没有对应模态窗口场景。模块检查通过：35 个模块、12,874 个归属文件、2,485 个明确排除、问题 0；差异空白检查通过。未暂存或提交。

## 10 月 9 日：DMP 确认隐藏的设备选择模态循环仍存活

输入为用户本地 `orca-slicer.DMP`，1,235,664,962 字节，SHA256 `b703b8aa48e1cc1b5e2473846cf1aa4b825f9d66f92ddb0268334a38741526a4`。Minidump 时间为 UTC 02:21:40，即北京时间 **10:21:40**；MiscInfo 的 PID **19568**、启动秒数对应 08:55:39，主线程 **19572 / 0x4c74**。包含 43 个线程及 144 个模块，未包含异常流；这是一份运行现场转储，不因 `file` 工具的通用 “crash report” 文案判定发生了崩溃。

### 解析方法和身份边界

本机 LLDB 能加载 Windows x64 minidump，但最初没有映像，只显示栈顶。随后直接从该转储的 Memory64 映射恢复相关 PE 头、代码与展开元数据，用 LLDB PE 展开器获得 28 帧主线程栈。关键代码和字段再以原始转储严格读取、RTTI 和反汇编逐项交叉核验，不使用其他版本的 Windows 程序套符号。

- 主业务在 `OrcaSlicer.dll`，加载基址 `0x7ff85bab0000`，映像大小 `0x4e0d000`，CodeView PDB GUID **1F9B1DE7-E075-4CE0-AD89-F5F606DABFD8**、Age **54 (0x36)**，PDB 名为 `OrcaSlicer.pdb`。记录的 PE 时间戳为 2026-10-08 04:05:56 UTC，但不把时间戳等同于 Git 提交身份。
- 小启动器 `orca-slicer.exe` 的 PDB GUID 为 **E299E543-E833-4743-B7D8-DF56D2FCB832**、Age **9**，与主业务 DLL 的符号不是同一份。
- 本机 Downloads 中已有 Windows 安装目录的 EXE/DLL 指纹及 CodeView 均不匹配，未作为故障版本使用。
- 没有私有 PDB；LLDB 为私有代码显示的 `hid_write + 大偏移`、`BRepExtrema... + 大偏移` 等只是最近导出符号，**不能据此归因 USB／几何模块**。下面 wx 函数名来自实际机器码／虚表布局与源码对应，不声称已通过 PDB 定位到项目源文件行号。
- 临时分析脚本、重建 PE、寄存器帧、脱敏字段摘要均留在忽略的 `build/diagnostics/dump-analysis/`；原始 DMP 留用户目录。未上传转储或其中内存到外部服务，未扫描／展示登录令牌。重建的 PE 是运行时内存副本，不是可发布或用于原始文件哈希比对的构建产物。

### 主线程真实调用关系

由顶层入口向当前等待位置，相关部分如下（省略正常事件分发中间帧）：

```text
主应用 wxEventLoop（0x1f9ff5f3440）
  → 打印事件处理
    → show_gfd_device_selection_dialog
      → wxDialog::ShowModal（当前对象为 GFDDeviceSelectionDialog）
        → wxEventLoopBase::Run（wxModalEventLoop 0x1f9ffa804e0）
          → wxEventLoopManual::DoRun
            → Windows 消息分发／等待
              → win32u!NtUserMsgWaitForMultipleObjectsEx
```

关键帧：`OrcaSlicer.dll+0x18db088` 为设备选择 ShowModal 调用之后尚未执行的位置；`+0x2e1da32` 在 wxDialog::ShowModal 内等待 Run 返回；`+0x2ff579a` 为 Run 等待 DoRun 返回。RTTI 明确识别栈上对话框为 `Slic3r::GUI::GFDDeviceSelectionDialog`，并非成功消息框。没有等待上传、worker join 或互斥锁的调用链。

### 对象内存给出的决定性证据

| 对象／字段 | 转储实值 | 结论 |
| --- | --- | --- |
| 设备选择对象 | `0x9fad516730`，HWND=`0x55131c` | 与 09:38 窗口 JSON 是同一原生窗口；对象仍在 ShowModal 调用者栈上 |
| `IsShown()` | false | wx 自己也认为已隐藏，与先前 Windows 原生 hidden 一致 |
| `m_returnCode` | 5100，即 `wxID_OK` | 结束结果已设为确定；构造默认值为 0 |
| `m_print_submitting`／lifetime | false／true | 下发中标志已清除，对象仍存活 |
| `m_modalData` | `0x1f9ffa804e0`，RTTI=`wxModalEventLoop` | 对话框的模态上下文尚未释放 |
| `wxEventLoopBase::ms_activeLoop` | 同为 `0x1f9ffa804e0` | 拍转储这一刻仍运行该设备选择循环，不是另一个可见消息框 |
| `m_isInsideRun`／`m_shouldExit` | true／false | 循环仍在运行，没有有效退出标记；不是正常退出中等待清空事件的阶段 |
| `m_windowDisabler` | `0x1f9963d4110`，active=true | 这一层用于禁用其他顶层窗口的对象未释放 |
| 主窗口对象 | `0x1f9f3712540`，HWND=`0x505ce`，shown=true、enabled=false | 与原生快照的禁用状态互相印证 |
| Plater 私有状态 | active_device_dialog=null，submission_id=1 | 成功收尾已清除业务活动指针；外层 ShowModal 返回后应执行的 submission_id++ 尚未发生 |

这些偏移以故障二进制指令核对：HWND +0x218、shown/enabled 位于 +0x190、returnCode +0x328、modalData +0x360、submitting +0x513；循环 shouldExit +0x8、insideRun +0x14、disabler +0x40。父链为设备选择 → Plater → Notebook → MainFrame；disabler 的既有禁用／隐藏窗口排除表包含 6 个辅助对话框，不包含 MainFrame。该禁用对象正常释放时应恢复主窗口。

### 实际二进制中的结束路径与故障机制

进一步核对了故障 DLL 的成功提示字符串和直接调用，确认不是仅凭当前工作区假设其行为：

1. 成功 finalize 分支在消息框 ShowModal 返回后，调用 `complete_gfd_print_submission(..., true)`（DLL RVA 0x185eee0）。
2. 该函数清除 active_device_dialog，再调用设备框 `complete_print_submission`（RVA 0x1d22d20）。
3. 设备框清除 submitting，调用虚表中的 EndModal(wxID_OK)。实际 EndModal 位于 RVA 0x2e1d340，先写 returnCode，再调用 Show(false)。
4. 实际 Show(false)（RVA 0x2e1d7b0）若已隐藏会直接返回；否则先调用循环 Exit，再隐藏窗口。Exit（RVA 0x2ff5630）若对象不是当时活动循环会直接返回，没有错误日志。
5. 成功 ScheduleExit 应把 shouldExit 设为 1 并调用 OnExit；实际 OnExit（RVA 0x2e1d5e0）释放 disabler 并把指针清零。转储中 shouldExit=0、disabler 非空且有效，证明正常模态退出清理没有完成。

**已确认的故障机制：设备选择窗口已经隐藏、成功结束结果已设置，但所属模态事件循环没有退出，窗口禁用对象持续存活，主窗口因此一直禁止鼠标／键盘输入，连右上角关闭也无效。** 程序在这个隐藏循环里仍可处理备份事件和消息探测，解释了日志继续增长及“画面在、点不动”的组合。

**仍未确认的是历史触发事件**：这份事后转储不能区分当时先发生隐藏导致 Show(false) 提前返回，还是 Exit 调用时活动循环不匹配。当前 activeLoop 正确不能反推之前也正确；同样不能据此断言 Alt+Tab、显卡或系统版本就是触发者。未发现内存破坏证据，也不以“没有找到”排除所有可能。故障机制已从“高度怀疑”升级为内部状态证实，具体最早触发分支仍须窄范围事件记录／复现。

### 修复边界与验证状态

后续修改应确保**结束设备选择框时真正结束所属模态循环并完成禁用恢复**，同时避免在其他模态层仍运行时错误恢复输入；只调用主窗口 Enable 会留下隐藏循环与栈上对象，不能算完整修复。该结论为修改定位，不是已实现补丁。若做诊断增强，记录 EndModal 前后 IsShown／IsModal／当前活动循环与本层循环身份，并覆盖成功确定、切换应用、关闭／取消和重复下发的本地模拟流程。

本轮只做本地转储／二进制分析和文档更新，没有修改应用代码、没有下发打印、没有重启同事进程。CLI 无等价模态交互，应用构建不适用；不将转储解析成功写成修复或 GUI 回归通过。

## 后续定位与 GUI 交接

以下为下次出现问题时的取证步骤，不要求为了诊断再次真实下发打印。当前只核对了日志，没有 Windows 实机复现。

| 场景 | 操作与预期 | 当前状态 |
| --- | --- | --- |
| 识别卡住层级 | 记录“上传成功”是否点过确定、设备选择窗口是否消失；观察窗口标题有无“未响应”。分别尝试主窗口拖动、菜单点击及键盘导航，记录哪些仍有效 | 用户确认约 16:30 点不动，回忆好像未点确认、切回后看不到确认框；准确窗口状态仍缺失 |
| 查隐藏窗口 | 卡住现场用 Alt+Tab 检查是否有 OrcaSlicer 弹窗；若找到，记录标题并正常关闭后观察是否恢复 | 待验证；这是区分隐藏模态窗的手段，不保证恢复 |
| 保留现场 | 记录精确时间和运行程序路径；在结束进程前保留日志、窗口截图，必要时保存该进程转储 | 已收到 10 月 8 日日志、10 月 9 日同事窗口 JSON 及目标 PID 19568 应用日志；10:21 转储已收到，确认设备选择模态循环残留 |
| 开发诊断 | 后续修改时在成功消息显示／返回、设备 EndModal 前后、外层 ShowModal 返回与析构处记录 submission_id、线程 ID、mainframe/device/message 的 IsEnabled/IsShown/IsModal，以及 mouse capture；回调下一事件轮再核对主窗口 | 建议，未实施 |
| 隔离复现 | 后续使用本地模拟成功响应驱动同一窗口生命周期，检查成功后主窗口恢复；同时覆盖失败重试、取消和连续打开关闭，避免依赖真实打印机 | 建议，未实施 |

自动化／CLI：本轮仅做日志提取、时间／线程对照和源码阅读；不宣称应用测试通过。CLI 无模态设备选择窗口，此 GUI 问题无等价 CLI 验证入口。文档检查 `python3 scripts/check_ai_module_docs.py --all` 通过：35 个模块、12,870 个归属文件、问题 0；`git diff --check` 通过。没有暂存或提交。

## 步骤记录

| 步骤 | 结果 |
| --- | --- |
| 初始基准与文件清单 | 核对仓库、master、HEAD 与已有用户改动；读取模块与维护规范；发现 30 份日志 |
| 用户确认 | 明确故障为约 16:30 下发后点不动，将 16:35 会话仅作对照；补充好像未点击确认、切回后确认框不可见 |
| 图谱与源码 | 修正目标索引过期，8 文件哈希匹配；动态绑定与 wx 平台机制仍以源码核对 |
| 证据收敛 | 完成上传／回调与 17 对主线程事件时间线；将窗口状态恢复列为候选，未越过证据认定具体根因 |
| 文档交付 | 保存调查及 GUI 取证清单，cloud 模块新增入口；模块检查与差异空白检查通过，保留已有用户改动 |
| 第二轮补充 | 复核成功提示无自动结束路径及 submitting 解除顺序，确认设备窗口关闭／Esc 拦截；区分正常模态限制、原故障残留候选和旧 finalize 日志约束，保存改进方案但未修改业务代码 |
| 第三轮追因 | 补查 close handler 未进入、备份时序、队列解锁与 finalize 历史对照；仍缺现场窗口状态／栈，将根因明确标为尚未定位，不以交互改进代替根因 |
| 第四轮追因 | 用户确认重启恢复；继续核对 Windows 析构和异常处理未建立因果链。准备独立窗口状态采集脚本并登记 test-tools 归属；Windows 实测仍缺失，不能将取证准备写成根因已确认 |
| 第五轮弹窗退出 | 核对局部弹窗析构先于 finalize 耗时日志、无自动超时；确认按压期间丢失捕获可合成按钮事件的源码路径。记录严格前提及缺少现场事件，未把候选关闭触发升级为卡死根因；仅更新调查文档 |
| 一键取证 | 用户要求免输 PowerShell 命令。新增同目录 `.cmd` 启动器，PowerShell 加交互选进程和采集后打开结果文件夹；中文 PS1 使用 UTF-8 BOM，CMD 使用 CRLF，打包提供双击入口。Windows 平台运行验证仍缺失 |
| 10 月 9 日现场 | 同事 JSON 三次均证实主窗口 disabled、设备选择 hidden 但存活、成功框无 HWND、全部探测通过、无 capture。用户确认点过成功提示，可能切走再切回才发现卡住。直接输入限制已定位；外层模态恢复失败的具体内部触发仍待今天日志／栈 |
| 补充日志包核对 | 收到 OrcaSlicer 2，用户说明跳过正在使用日志。当天两份均为其他 PID 且正常退出，缺 19568／08:55 目标会话；昨日下午会话的新副本补全至 20:04 正常退出。需补目标日志，不能拿其他实例替代 |
| 正在使用日志复制 | 新增双击选文件的共享只读复制工具；bounded copy 保留源进程，失败保存错误及 partial；打包检查通过，Windows 现场执行结果待同事返回 |

一键交付产物：本地 `build/diagnostics/orca-gui-capture-one-click.zip`，包含 `双击采集.cmd`、`capture_windows_gui_state.ps1`、`使用说明.txt`。ZIP CRC 校验、解包内容与当前脚本逐字节一致、PS1 UTF-8 BOM、CMD CRLF 检查通过；这只是产物完整性检查，不是 Windows 运行通过。脚本为源文件，ZIP 为本地生成交付物，不提交日志或诊断结果。

本批 `python3 scripts/check_ai_module_docs.py --all` 通过：35 个模块、12,872 个归属文件、问题 0；`git diff --check` 通过。没有应用构建或 Windows 运行验证，没有暂存或提交。


## 10 月 9 日：按转储机制实施修复

### 实现与范围

用户询问现有信息是否足以编码后，已开始针对已确认机制修改。Windows 的 GFDDeviceSelectionDialog 改用项目内 GFDModalDialog；它拥有自己的 wxGUIEventLoop 与 wxWindowDisabler。EndModal 直接对所属循环 ScheduleExit，不依赖 Show(false) 的可见性判断，也不要求该循环恰好处于活动栈顶。作用域等待 Run 真正返回再恢复窗口，并先恢复后隐藏，保持 Windows 激活顺序；嵌套窗口仍在时不会提前解禁主窗口。正常、已隐藏、另一循环活动、初始化提前结束、重复结束均明确处理。

不修改 wxWidgets 第三方源码／私有字段，不硬编码主窗口 Enable(true)。非 Windows 的实际设备选择仍使用 wxDialog。GFDDeviceSelectionDialog 增加模态 begin / end requested / returned 日志及结束期间拒绝再次提交的保护。日志只写返回值与窗口状态；导出、上传、成功提示文案、后端协议及失败后的重试逻辑保持原有路径，CLI 无本项界面状态，因此没有 CLI 业务改动或实机下发。

关于 Exit 与 ScheduleExit 的接口语义，已对照本地依赖源文件及 [wxWidgets 官方接口说明](https://github.com/wxWidgets/wxWidgets/blob/master/interface/wx/evtloop.h)；实际修复依据仍是本次转储和所用依赖实现，不以最新上游实现推定故障包版本。

### 自动化与构建结果

- 源码基准：master / 563f30abc97a5add8a73f1e7b1a01b82aa769d22 加本次未提交修改；目录为当前仓库。用户原有 result.json 与教材记录未动。
- 现有构建配置：build/arm64、Ninja Multi-Config、Release、AppleClang、项目本地 wx 3.1.5 静态库。最终增量构建 `cmake --build build/arm64 --config Release --target gfd_modal_dialog_tests OrcaSlicer --parallel 4` 退出 0；相关文件已按 .clang-format 格式化。应用构建日志见 ignored 的 build/diagnostics/gfd-modal-app-build.log 与 gfd-modal-final-build.log。
- 独立目标 gfd_modal_dialog_tests 已在本机编译并运行；7 组、54 项检查通过。覆盖正常结束及重复结束保留首个结果、先隐藏再结束、Hide 结束、内层模态期间结束外层且不提前解禁、关闭取消、OnEventLoopEnter 早期结束、同窗口连续 5 次 ShowModal；其他原本已禁用窗口保持禁用。
- 初轮测试超时最后定位为 Mac 本地 wx 的 Show 路径没有调用测试用 InitDialog 钩子，并非窗口关闭失败；隔离跟踪确认 close 的 Run 已返回。该路径保留为 Windows 专属测试，本机使用 OnEventLoopEnter 验证 Run 分发前结束。不能把 Windows 专属用例写为已运行。
- 负向验证通过：两份仅存在于 ignored build/diagnostics/modal-mutants 的变异实现，分别恢复“隐藏即忽略结束”和“非当前循环使用 Exit”的错误条件；测试准确在 already hidden 与 outer completion while nested dialog active 处触发 5 秒看门狗，均退出 2。正式源码不引入测试开关。
- Windows 环境未提供编译器／运行桌面，没有 Windows 构建、原生 InitDialog 或同事整条业务流程的通过结论。Mac 的实际 Orca 设备选择未改用新循环，本机公共循环测试与 Windows 集成验收分开。

### Windows 验证交接（均待执行）

先在 Windows 的本仓库现有构建目录重新构建本次源码；不能沿用转储中的 10 月 8 日 DLL。保留构建提交／未提交差异、OrcaSlicer.dll 的 SHA256 与该次日志起止时间。原现场工程的文件身份未随转储提供；整条业务复测使用同事先前成功下发的工程、机型、耗材和工艺，先记录工程名及参数，避免把另一套工程的结果当同配置复现。中英文显示没有新增文案，本次不涉及翻译调整。

可独立先运行无云服务／无实机打印的回归：在已有构建目录配置 BUILD_TESTS=ON 和 SLIC3R_GUI_TESTS=ON，构建 gfd_modal_dialog_tests，使用 `ctest --test-dir <构建目录> -C Release -R "^gui:gfd_modal_dialog$" --output-on-failure`。程序需有交互桌面，自动开关测试窗口；Windows 还会执行 InitDialog 中结束一例，预期所有检查通过，无 5 秒退出超时。

| 场景 | 操作 | 预期／证据 | 状态 |
| --- | --- | --- | --- |
| 未下发时取消 | 从当前盘打印入口打开打印机选择，直接点右上角关闭；再打开，重复 5 次 | 每次设备框关闭后，主界面可点击、可再次打开；日志每次 begin 对应 returned | 待 Windows |
| 正常成功收尾 | 经允许的测试设备下发同一工程，成功提示点确定 | 设备框关闭，主窗口输入恢复；end requested 的 return_code=5100，随后 returned 且 modal=0 | 待 Windows |
| 切换应用 | 成功提示出现后 Alt+Tab 到另一程序，再回 Orca 点确定；另一次点确定后立即切走再回来 | 都能继续点击菜单、切换页签和操作主窗口关闭；不出现 hidden+disabled 长期残留 | 待 Windows |
| 嵌套期间等待 | 成功提示暂不点确定，切换应用并回来 | 等待期间主窗口不能操作属于正常模态；提示应可找到并确认，确认后应恢复 | 待 Windows |
| 失败与重试 | 使用已知可控失败条件，使此次下发失败，读完错误提示 | 选择框保持可操作，可重试或关闭；不能误记成功，也不恢复到隐藏残留状态 | 待 Windows |

若仍出现点不动，先保留进程并采集窗口 JSON 与新日志；这时重点核对 end requested 和 returned 是否配对，再决定是否需要新的 DMP。当前不要求继续补同类普通日志才开始修复。


最终复核（本机 11:00）：

- 最终源码重新编译后，`ctest --test-dir build/arm64 -C Release -R '^gui:gfd_modal_dialog$' --output-on-failure` 退出 0，54 checks / 0 failures；完整输出保存 build/diagnostics/gfd-modal-test-final.log 及 build/arm64/Testing/Temporary/LastTest.log。
- 新应用路径 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA256 `6af06adc0e6b5332685584b1c501a1d9f88d951804d0747dc69199456fa752d7`；`--help` 冒烟退出 0，不代表 GUI 整条下发流程通过。
- 测试程序 SHA256 `dd7dd405b259e6c232f253aa12d498b9f0521867975696f4399a5f07d1b9fff8`；源码／测试／CMake 逐文件身份保存 build/diagnostics/gfd-modal-build-identity.json。该 Mac 应用用于编译集成核对，不能交给 Windows 同事当修复包。
- 35 个模块文档归属／指纹检查通过，git diff --check 通过；未暂存、未提交。Windows 编译和 GUI 回归仍待执行，不把公共循环通过等同于原故障已在 Windows 验收。


## 新旧逻辑受控对照与旧包现场复测（进行中）

用户确认可编译并提供 Mac 上 Windows 虚拟机环境。本轮目标是在 Windows 用真实 wxDialog 原生旧路径与修复路径分别触发同样的隐藏／嵌套时序，验证缺陷与修正；不把程序注入故障状态称为同事原始操作已还原。旧安装包目前没有已知必现的纯点击步骤，Alt+Tab 仅作为现场对照维度，不保证复现。上一轮截图标题 Init completion 确认来自独立测试夹具；当前无该测试或追踪／变异进程存活。


用户随后要求“你告诉我怎么做，我来验证”：改为交付手测与已有测试目标命令，不继续虚拟机操作。Parallels Windows 11 已从 suspended 启动；仅尝试只读环境查询，无安装／配置修改。尚未新增 Windows 旧路径对照程序，不宣称已能让旧安装包必现。

手测两包同机、同工程／设备／配置、一次只运行一个包，先正常确认，再分别覆盖确认前 Alt+Tab、点确认后立即 Alt+Tab、确认前最小化／恢复。每轮确认主窗口菜单、页签与设备选择重开可操作；最后再检查关闭主程序，避免每轮重启掩盖累积状态。旧包异常时保留进程，采集现有窗口 JSON、日志与准确操作时间，再换新包。未复现只能记为本轮未复现。

源码验证在 Windows 已有构建目录上开启 BUILD_TESTS=ON、SLIC3R_GUI_TESTS=ON，构建 gfd_modal_dialog_tests 并用 ctest 精确筛选 gui:gfd_modal_dialog；新测试自动制造隐藏／嵌套退出条件，不需要账号、工程或实机下发。该目标不是旧安装包注入工具，不能将其通过写为原始偶现已复现并消失。


## Mac 用户手测通过后再提交

用户安排“Mac 测试没问题提交，然后 Windows 同步测试”，并明确目前还没测，要等其测完再提交。本轮不提交、不推送、不操作 Windows 虚拟机。

本轮复核 `gfd-modal-build-identity.json` 的 10 项身份全部一致，未出现源码或程序变化；复用已通过的 7 组／54 检查，不重复运行 GUI 测试打断用户。`check_ai_module_docs.py --all`：35 个模块、12,878 个归属文件、2,485 个排除、0 问题；`git diff --check` 通过。暂存区为空，result.json 仍为用户原有两项新增，教材文档未触碰。

当时可见进程路径为 `/Applications/OrcaSlicer.app` 及当前仓库 `build/arm64/OrcaSlicer/OrcaSlicer.app`；本次实际构建是 `build/arm64/src/Release/OrcaSlicer.app`，不要用旧安装／打包路径代替。用户先保存并退出旧进程，再启动本次路径，保持其常用工程／设备／配置。Mac 设备选择仍使用原生 wxDialog，本轮手测检查现有功能无回归；不能以其通过替代 Windows 自有模态循环的系统验收。

| Mac 手测场景 | 操作 | 预期 | 状态 |
| --- | --- | --- | --- |
| 取消与重开 | 从当前盘打印入口打开打印机选择，点右上角关闭，再打开；连续 5 次 | 每次主界面恢复操作，设备框可再次打开 | 待用户 |
| 正常成功与切换 | 按原有获准的测试流程下发常用工程，成功提示点确定；切到其他应用再回来 | 设备框关闭，菜单／页签可操作，能够再次打开设备选择 | 待用户 |
| 应用收尾 | 保存工程，再关闭主窗口 | 正常退出，不遗留点不动的窗口 | 待用户 |

收到用户通过结果后，选择性提交本次修复、回归、相关诊断工具与模块／验证文档，排除 result.json、教材记录及 ignored 诊断产物。Windows 之后同步同一提交进行构建与前节场景验收。


## 代码审查：不以强行复现偶现为前提

用户要求确认当前代码处理是否存在问题。本轮核对以下路径，未发现本次修复的阻塞性逻辑缺陷，未改生产代码：

| 审查点 | 核对结果 |
| --- | --- |
| 已隐藏后结束 | EndModal 对所属 ModalSession.finish 发出请求，不经过 wxDialog::Show(false) 的相同可见性提前返回；隐藏不会跳过退出 |
| 非当前活动循环 | IsInsideRun 确认所属循环尚在调用栈内，ScheduleExit 允许内层运行期间标记外层退出；不对活动内层误发退出 |
| 禁用状态与顺序 | wxWindowDisabler 在所属 Run 返回后的作用域析构中释放，随后才隐藏选择框；不会在内层提示仍活动时提前恢复主窗口；原先已禁用窗口保留状态 |
| 正常成功 | Plater 等成功提示 ShowModal 返回后清当前活动设备指针并调用 complete(true)，再 EndModal；外层返回后递增 submission_id 并取消残余任务 |
| 失败及迟到打印回调 | 失败 complete(false) 清 submitting 并恢复加载状态供重试，不置 modal_ending；回调保持 alive／活动窗口／submission_id 过滤，本修复未变更打印协议与业务结果 |
| 重复结束和重复提交 | 首次 finish 设置 ending 并保留返回值；后续结束不覆盖；GFD 选择框置 m_modal_ending，确认入口拒绝再次提交 |
| 进入循环前结束 | InitDialog 中结束时跳过 Run；OnEventLoopEnter 阶段结束由 DoRun 的 ending 检查收尾，不在尚未 insideRun 时调用 ScheduleExit |
| 平台与 API | Windows wxDialog::IsModal/ShowModal/EndModal/Show 均为虚函数，项目基类覆盖有效，未访问 private m_modalData；Mac 生产选择框仍使用原生 wxDialog。CLI 无同类界面，本轮业务核心无改动 |

审查发现测试覆盖缺口：此前异常嵌套场景不能单独证明正常“内层确认结束 → 选择框仍可用且主窗口仍禁用 → 外层结束”顺序。因此只增加该独立用例。测试目标最小重建退出 0，CTest 退出 0，8 组／65 checks／0 failures；新增 11 项检查，旧结果不重复计数。运行会短暂创建测试窗口，已在运行前告知用户。

证据：`build/diagnostics/gfd-modal-review-build.log`、`gfd-modal-review-test.log`、`gfd-modal-review-identity.json`。与前次 10 项身份对照，只有 test_gfd_modal_dialog.cpp 和测试程序变化；应用及所有生产源码身份一致，复用应用构建，不重复重编无变化的业务目标。模块文档与 diff 检查另行完成。

结论边界：本轮是源码审查与真实 wx 公共循环回归通过；Windows 原生 InitDialog／消息分发、Alt+Tab 激活表现与真实下发流程尚无运行结论。具体历史触发操作也仍未还原。无需用户为了获得本轮代码审查结论而无限重复尝试偶现；审查通过不能改写为 Windows 现场已验收。


## Mac 实际日志复核与 2.3.2.2-Release 提交

用户提供 `planning/debug/orca-user-gui-run-20261009-112801-581027.log`，命令运行的是 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。本次包已由用户更新：文件 mtime 11:27:51，包含新增模态日志，Mach-O UUID `FDFB0010-5AD8-3103-AACE-92C161D859F2` 与 11:00 已验证构建相同。包内程序 SHA256 `32e51203bcd3ecd9e5f2ecfa613a1480d41eee021a18d6cd24f298950c9fa933`，直接构建程序 SHA256 `6af06adc0e6b5332685584b1c501a1d9f88d951804d0747dc69199456fa752d7`；打包可改变字节，UUID 一致加本次日志能支持同次链接身份，不只凭版本显示推定。此前“该打包路径不是本次新构建”的提醒仅适用于当时状态，不沿用于已更新后的包。

日志仍由运行进程追加。分析快照截至 11:36:26.990241，331,505 字节、1,709 行、SHA256 `00ca2bcc75fb4024cf6a99a045973821e0c3bde00aca031d854b1a3d708acb1d`，包含 1 个 NUL，解析时跳过该字符但不改原日志。脱敏事件摘要存于 ignored 的 build/diagnostics/mac-user-log-20261009-summary.json；不提交原日志或任何认证／设备信息。

| 时间／快照行号 | 事件 | 判断 |
| --- | --- | --- |
| 11:28:52.612／1486 | 设备选择 modal begin | 第一次打开 |
| 11:30:07.783–.808／1516–1517 | CANCEL=5101，end requested 后 returned，shown=false、modal=false、parent_enabled=true | 取消收尾完成，主窗口启用 |
| 11:31:34.136／1548 | 再次 modal begin | 可以重新进入打印入口 |
| 11:31:48.569／1646 | 上传 HTTP 200 | 上传成功 |
| 11:31:49.043／1649 | 打印命令 HTTP 200、code=0、data=true | 服务接受下发，不等于设备已打印完成 |
| 11:31:55.292–.323／1650–1653 | OK=5100，end requested 后 returned，shown=false、modal=false、parent_enabled=true；临时文件清理完成 | 成功路径正常退出，未出现本次 Windows 故障的模态残留 |
| 至 11:36:26 | 主线程持续处理周期备份消息 | 提交分析时进程仍运行；没有主窗口正常退出证据，不能把退出场景记为通过 |

两条 error 出现在下发前：11:28:06 可选 3Dconnexion 驱动未安装，11:28:14 calc_exclude_triangles 无法生成排除区域三角形。其他 warning 包括首页导航／消息及未加载网络插件的版本查询；本轮后续切片导出、上传、下发及窗口恢复均完成，未显示模态修复的阻塞问题，不称整份日志零错误。这次实际 Mac 取消／成功路径已有日志证据，Windows 原生路径仍不能由 Mac 替代。

用户据此授权更新公开产品版本 SoftFever_VERSION：2.3.2.1-Release → 2.3.2.2-Release。内部兼容／预设 SLIC3R_VERSION 保持 01.10.01.50，不随公开补丁版本迁移。没有覆盖用户正在运行的打包副本，新程序位于 build/arm64/src/Release/OrcaSlicer.app。

提交验证：

- CMake 在现有 arm64 Release 目录构建 OrcaSlicer 和 gfd_modal_dialog_tests，退出 0。先前 parallel=4 构建为提高并行度主动中止，随后 parallel=8 增量续建成功；不是编译失败。最终日志：build/diagnostics/gfd-modal-2322-build-final.log。
- 新程序 `--help` 退出 0，输出 2.3.2.2-Release；生成版本头与 Info.plist 的公开版本一致。程序 SHA256 `8fbc678bc0125af3143b90c13329309958ac671c942d8709b2648a059d6294f7`，arm64 UUID `4599A72B-1B78-39D7-89A5-C229E27AEDCE`。证据：build/diagnostics/gfd-modal-2322-final-identity.json 与 gfd-modal-2322-help.log。
- 对照 gfd-modal-review-identity.json，8 项相关源码／测试／CMake 文件以及 GUI 测试程序的 SHA256 均未变化；复用已通过的 8 组／65 checks。公开版本更新不改模态逻辑，不重复弹出测试窗口。
- 两个 Windows CMD 启动器加入路径级 eol=crlf 属性；使用 core.autocrlf=false 从 Git 暂存区检出后仍为 CRLF，PS1 的 UTF-8 BOM 已核对。只验证了脚本格式与既有 Windows 采集证据，不将全部脚本分支写为运行通过。
- 按本次 19 个文件选择性暂存；工作区与暂存区模块文档检查、暂存差异空白检查通过。CLI 没有这组模态交互，共享导出／下发逻辑未修改；Mac 实际取消和成功收尾有证据，主窗口退出与 Windows 原生场景仍待验证。
