#requires -Version 5.1
<#
Copy a user-selected active OrcaSlicer log without changing the source or process.
The companion CMD opens a file picker; optional CLI parameters allow an explicit path.
Read sharing cannot override a writer that denies read access or file permissions.
#>
[CmdletBinding()]
param(
    [string]$SourcePath,
    [string]$OutputDirectory = [Environment]::GetFolderPath('Desktop')
)

$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This tool requires Windows.'
}

if ([string]::IsNullOrWhiteSpace($SourcePath)) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    try {
        $picker.Title = '选择提示正在使用的那份 OrcaSlicer 日志'
        $picker.Filter = 'Orca 日志 (*.log;*.log.*)|*.log;*.log.*'
        # Let our shared-read FileStream handle access errors, not the picker.
        $picker.ValidateNames = $false
        $picker.CheckFileExists = $true
        $picker.Multiselect = $false
        $defaultLogDirectory = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'OrcaSlicer\log'
        if (Test-Path -LiteralPath $defaultLogDirectory -PathType Container) {
            $picker.InitialDirectory = $defaultLogDirectory
        }
        if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
            Write-Host '已取消，没有复制文件。'
            return
        }
        $SourcePath = $picker.FileName
    } finally {
        $picker.Dispose()
    }
}

$sourceFile = Get-Item -LiteralPath $SourcePath
if ($sourceFile.PSIsContainer -or $sourceFile.Name -notmatch '\.log(?:\.\d+)?$') {
    throw '请选择日志文件，例如 debug_..._19568.log.0。'
}
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    throw '保存位置不存在，请确认桌面可用。'
}

$folderName = 'Orca-log-copy-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N'))
$destinationFolder = Join-Path $OutputDirectory $folderName
[void][IO.Directory]::CreateDirectory($destinationFolder)
$destinationPath = Join-Path $destinationFolder $sourceFile.Name
$partialPath = $destinationPath + '.partial'
$infoPath = Join-Path $destinationFolder 'copy-info.json'
$info = [ordered]@{
    schemaVersion = 1
    startedAt = [DateTimeOffset]::Now.ToString('o')
    sourcePath = $sourceFile.FullName
    sourceLengthAtOpen = $null
    sourceLengthAfterRead = $null
    bytesCopied = 0
    success = $false
    method = 'FileAccess.Read; FileShare.ReadWrite | FileShare.Delete; bounded initial-length snapshot'
    notes = 'A live log may change during the copy; this is not an atomic snapshot and does not flush the writer.'
}
if ($sourceFile.Name -match '_(\d+)\.log(?:\.\d+)?$') {
    $info['processIdFromFilename'] = $Matches[1]
}

$sourceStream = $null
$destinationStream = $null
$copyFailure = $null
try {
    Write-Host "正在复制：$($sourceFile.Name)"
    $sharing = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    $sourceStream = [IO.File]::Open($sourceFile.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, $sharing)
    [long]$initialLength = $sourceStream.Length
    $info['sourceLengthAtOpen'] = $initialLength
    # CreateNew avoids replacing any existing file. Keep failures marked .partial.
    $destinationStream = [IO.File]::Open($partialPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $buffer = New-Object byte[] (1024 * 1024)
    [long]$copied = 0
    while ($copied -lt $initialLength) {
        $requested = [int][Math]::Min([long]$buffer.Length, $initialLength - $copied)
        $read = $sourceStream.Read($buffer, 0, $requested)
        if ($read -eq 0) {
            throw '复制时源日志被截短，已保留 partial 文件，不能当作完整日志。'
        }
        $destinationStream.Write($buffer, 0, $read)
        $copied += $read
        $info['bytesCopied'] = $copied
    }
    $info['sourceLengthAfterRead'] = $sourceStream.Length
    $destinationStream.Flush()
    $destinationStream.Dispose()
    $destinationStream = $null
    $sourceStream.Dispose()
    $sourceStream = $null
    [IO.File]::Move($partialPath, $destinationPath)
    $info['outputPath'] = $destinationPath
    $info['sha256'] = (Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash
    $info['success'] = $true
} catch {
    $copyFailure = $_.Exception.GetBaseException()
    $info['error'] = $copyFailure.Message
    $info['exceptionType'] = $copyFailure.GetType().FullName
    $info['hresult'] = $copyFailure.HResult
    $info['errorCodeLowWord'] = $copyFailure.HResult -band 65535
} finally {
    if ($null -ne $destinationStream) { $destinationStream.Dispose() }
    if ($null -ne $sourceStream) { $sourceStream.Dispose() }
    $info['finishedAt'] = [DateTimeOffset]::Now.ToString('o')
    $info | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $infoPath -Encoding UTF8
}

if ($null -ne $copyFailure) {
    Write-Host "复制失败，错误信息已保存到：$infoPath"
    Write-Host '请保留卡住的 Orca 进程，把错误信息发回；工具不会强行解除文件占用。'
    throw $copyFailure
}

Write-Host "复制成功：$destinationPath"
Write-Host '请发送这个复制后的日志；copy-info.json 也可一起发送。'
try {
    Start-Process -FilePath (Join-Path $env:SystemRoot 'explorer.exe') -ArgumentList ('/select,"{0}"' -f $destinationPath)
} catch {
    Write-Warning "文件已保存，请在桌面打开此文件夹：$destinationFolder"
}
