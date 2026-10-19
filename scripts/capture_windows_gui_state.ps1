#requires -Version 5.1
<#
Captures OrcaSlicer window/input state without changing window state or printing.
Run on Windows while the affected OrcaSlicer process is still alive:
  Double-click the companion capture_windows_gui_state.cmd launcher, or run:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\capture_windows_gui_state.ps1
Switch back to OrcaSlicer during the initial delay. The JSON is saved on Desktop.
Use -OrcaProcessId 1234 when multiple OrcaSlicer processes are running.
Window titles may contain project names. No configuration or credentials are read.
#>
[CmdletBinding()]
param(
    [ValidateRange(0, 2147483647)][int]$OrcaProcessId = 0,
    [ValidateRange(0, 60)][int]$DelaySeconds = 8,
    [ValidateRange(1, 10)][int]$SampleCount = 3,
    [string]$OutputDirectory = [Environment]::GetFolderPath('Desktop'),
    [switch]$Interactive,
    [switch]$OpenOutputFolder
)

$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This diagnostic requires Windows.'
}
if ($OrcaProcessId -eq 0) {
    $candidates = @(Get-Process | Where-Object { $_.ProcessName -match '^orca[-_]?slicer$' })
    if ($candidates.Count -eq 0) {
        throw '没有找到正在运行的 OrcaSlicer，请保留故障程序，再双击采集。'
    }
    if ($candidates.Count -gt 1) {
        $candidates | Select-Object Id, ProcessName, MainWindowTitle | Format-Table | Out-Host
        if (-not $Interactive) {
            throw 'Expected one OrcaSlicer process. Specify -OrcaProcessId with its process ID.'
        }
        $selection = Read-Host '找到多个 OrcaSlicer，请输入上表中卡住进程的 Id'
        $selectedProcessId = 0
        if (-not [int]::TryParse($selection, [ref]$selectedProcessId) -or
            $selectedProcessId -notin @($candidates.Id)) {
            throw '输入的进程 Id 不在列表中，请重新运行采集。'
        }
        $OrcaProcessId = $selectedProcessId
    } else {
        $OrcaProcessId = $candidates[0].Id
    }
}
$targetProcess = Get-Process -Id $OrcaProcessId
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    throw 'OutputDirectory must be an existing directory.'
}

if (-not ('OrcaGuiState.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace OrcaGuiState {
    public static class Native {
        [StructLayout(LayoutKind.Sequential)]
        public struct Rect { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)]
        struct GuiInfo {
            public uint Size, Flags;
            public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
            public Rect CaretRect;
        }
        delegate bool EnumProc(IntPtr window, IntPtr parameter);
        [DllImport("user32.dll", SetLastError = true)]
        static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
        [DllImport("user32.dll")]
        static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern int GetClassName(IntPtr window, StringBuilder text, int size);
        [DllImport("user32.dll")]
        static extern bool IsWindow(IntPtr window);
        [DllImport("user32.dll")]
        static extern bool IsWindowEnabled(IntPtr window);
        [DllImport("user32.dll")]
        static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")]
        static extern bool IsIconic(IntPtr window);
        [DllImport("user32.dll")]
        static extern bool GetWindowRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
        static extern int GetWindowLong(IntPtr window, int index);
        [DllImport("user32.dll")]
        static extern IntPtr GetWindow(IntPtr window, uint command);
        [DllImport("user32.dll")]
        static extern IntPtr GetAncestor(IntPtr window, uint flags);
        [DllImport("user32.dll")]
        static extern IntPtr GetLastActivePopup(IntPtr window);
        [DllImport("user32.dll")]
        static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")]
        static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
        [DllImport("user32.dll", SetLastError = true)]
        static extern bool GetGUIThreadInfo(uint thread, ref GuiInfo info);
        [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW", SetLastError = true)]
        static extern IntPtr SendMessageTimeout(IntPtr window, uint message,
            UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
        [DllImport("kernel32.dll")]
        static extern void SetLastError(uint error);
        [DllImport("dwmapi.dll")]
        static extern int DwmGetWindowAttribute(IntPtr window, uint attribute, out uint value, uint size);

        static string Hex(IntPtr value) { return "0x" + value.ToInt64().ToString("X"); }
        static Dictionary<string, object> Identify(IntPtr window) {
            uint process;
            uint thread = GetWindowThreadProcessId(window, out process);
            StringBuilder name = new StringBuilder(256);
            GetClassName(window, name, name.Capacity);
            return new Dictionary<string, object> {
                { "handle", Hex(window) }, { "exists", IsWindow(window) },
                { "processId", process }, { "threadId", thread },
                { "class", name.ToString() }, { "root", Hex(GetAncestor(window, 2)) }
            };
        }
        public static Dictionary<string, object> Snapshot(uint process) {
            var windows = new List<IntPtr>();
            var threads = new HashSet<uint>();
            bool enumerated = EnumWindows(delegate(IntPtr window, IntPtr unused) {
                uint ownerProcess;
                uint thread = GetWindowThreadProcessId(window, out ownerProcess);
                if (ownerProcess == process) { windows.Add(window); threads.Add(thread); }
                return true;
            }, IntPtr.Zero);
            int enumerationError = enumerated ? 0 : Marshal.GetLastWin32Error();
            var snapshot = new Dictionary<string, object> {
                { "startedAt", DateTimeOffset.Now.ToString("o") },
                { "foreground", Identify(GetForegroundWindow()) },
                { "enumerationSucceeded", enumerated }, { "enumerationError", enumerationError }
            };
            var guiThreads = new List<Dictionary<string, object>>();
            foreach (uint thread in threads) {
                GuiInfo info = new GuiInfo();
                info.Size = (uint)Marshal.SizeOf(typeof(GuiInfo));
                bool success = GetGUIThreadInfo(thread, ref info);
                int error = success ? 0 : Marshal.GetLastWin32Error();
                var row = new Dictionary<string, object> {
                    { "threadId", thread }, { "success", success }, { "error", error }
                };
                if (success) {
                    row["flags"] = info.Flags;
                    row["active"] = Identify(info.Active);
                    row["focus"] = Identify(info.Focus);
                    row["capture"] = Identify(info.Capture);
                    row["menuOwner"] = Identify(info.MenuOwner);
                    row["moveSize"] = Identify(info.MoveSize);
                }
                guiThreads.Add(row);
            }
            snapshot["guiThreads"] = guiThreads;
            var rows = new List<Dictionary<string, object>>();
            // Capture state before sending any WM_NULL probes.
            foreach (IntPtr window in windows) {
                var row = Identify(window);
                StringBuilder title = new StringBuilder(1024);
                GetWindowText(window, title, title.Capacity);
                Rect rect;
                bool rectValid = GetWindowRect(window, out rect);
                uint style = unchecked((uint)GetWindowLong(window, -16));
                uint cloaked;
                int cloakResult = DwmGetWindowAttribute(window, 14, out cloaked, 4);
                row["title"] = title.ToString();
                row["enabled"] = IsWindowEnabled(window);
                row["visible"] = IsWindowVisible(window);
                row["minimized"] = IsIconic(window);
                row["style"] = "0x" + style.ToString("X8");
                row["disabledStyle"] = (style & 0x08000000u) != 0;
                row["owner"] = Hex(GetWindow(window, 4));
                row["rootOwner"] = Hex(GetAncestor(window, 3));
                row["lastActivePopup"] = Hex(GetLastActivePopup(window));
                row["rectangleValid"] = rectValid;
                row["rectangle"] = rect;
                row["intersectsMonitor"] = MonitorFromWindow(window, 0) != IntPtr.Zero;
                row["cloakQueryHresult"] = cloakResult;
                row["cloaked"] = cloakResult == 0 ? (object)cloaked : null;
                rows.Add(row);
            }
            for (int index = 0; index < windows.Count; ++index) {
                UIntPtr ignored;
                Stopwatch elapsed = Stopwatch.StartNew();
                SetLastError(0);
                // WM_NULL; BLOCK | ABORTIFHUNG | ERRORONEXIT; never broadcast.
                bool success = SendMessageTimeout(windows[index], 0, UIntPtr.Zero,
                    IntPtr.Zero, 0x23, 250, out ignored) != IntPtr.Zero;
                int error = success ? 0 : Marshal.GetLastWin32Error();
                elapsed.Stop();
                rows[index]["probeSucceeded"] = success;
                rows[index]["probeError"] = error;
                rows[index]["probeElapsedMs"] = elapsed.ElapsedMilliseconds;
                rows[index]["existsAfterProbe"] = IsWindow(windows[index]);
            }
            snapshot["windows"] = rows;
            snapshot["finishedAt"] = DateTimeOffset.Now.ToString("o");
            return snapshot;
        }
    }
}
'@
}

$identity = [ordered]@{ processId = $OrcaProcessId; name = $targetProcess.ProcessName }
try {
    $identity['startedAt'] = $targetProcess.StartTime.ToString('o')
    $binaryPath = $targetProcess.MainModule.FileName
    $identity['binaryPath'] = $binaryPath
    $identity['fileVersion'] = [Diagnostics.FileVersionInfo]::GetVersionInfo($binaryPath).FileVersion
    $identity['sha256'] = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash
} catch {
    $identity['identityError'] = $_.Exception.Message
}

Write-Host "请在 $DelaySeconds 秒内切回 OrcaSlicer，保持卡住现场，暂时不要关闭或重启程序。"
Start-Sleep -Seconds $DelaySeconds
$samples = @()
for ($sampleIndex = 0; $sampleIndex -lt $SampleCount; ++$sampleIndex) {
    $targetProcess.Refresh()
    if ($targetProcess.HasExited) { break }
    $samples += [OrcaGuiState.Native]::Snapshot([uint32]$OrcaProcessId)
    if ($sampleIndex + 1 -lt $SampleCount) { Start-Sleep -Seconds 1 }
}
$report = [ordered]@{
    schemaVersion = 1
    capturedAt = [DateTimeOffset]::Now.ToString('o')
    osVersion = [Environment]::OSVersion.VersionString
    powershellVersion = $PSVersionTable.PSVersion.ToString()
    collectorBits = [IntPtr]::Size * 8
    process = $identity
    samples = $samples
    notes = @(
        'Snapshots are not atomic. Window destruction and activation can race with collection.',
        'A failed WM_NULL probe alone does not prove a hang; inspect error and window existence.',
        'Thread input state is collected only for threads owning enumerated top-level windows.',
        'This report does not contain a thread stack or wxWidgets modal-loop state.'
    )
}
$outputName = 'orca-gui-state-{0}-{1}.json' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
$outputPath = Join-Path $OutputDirectory $outputName
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputPath -Encoding UTF8
Write-Host "采集完成，已保存 $($samples.Count) 份窗口快照：$outputPath"
if ($OpenOutputFolder) {
    try {
        # Open Explorer only after all snapshots have been captured and saved.
        Start-Process -FilePath (Join-Path $env:SystemRoot 'explorer.exe') -ArgumentList ('/select,"{0}"' -f $outputPath)
    } catch {
        Write-Warning "结果已保存，但无法打开所在文件夹：$outputPath"
    }
}
