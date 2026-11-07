# 物差しの画面の流れを実際のキー入力で確かめる: タイトル → クレジット → セレクト → 戻る → 出発 → ロード → ポーズ → 映像設定 → 時間切れ → もう一度 → タイトル。
# 窓を前面に出してキーを送り、tests/out/flow-*.png へ撮影する。動かしている間は操作しないこと
param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $repoPath '../Generated/Outputs/Development/DirectXGameApp.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (Get-Process DirectXGameApp -ErrorAction SilentlyContinue) { throw 'Close the running game first.' }
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class Flow {
  [StructLayout(LayoutKind.Sequential)] public struct RECT{public int L,T,R,B;}
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref RECT p);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte scan,uint flags,UIntPtr extra);
}
'@
$save = Join-Path $env:LOCALAPPDATA 'LiquidTime/slime-lab-v1.json'
$backup = Join-Path $PSScriptRoot 'out/flow-save-backup.json'
if (Test-Path $save) { Copy-Item -LiteralPath $save -Destination $backup -Force }
$process = Start-Process $Executable -ArgumentList '--windowed-title --devour-time=16' -WorkingDirectory $repoPath -PassThru
function Shot([string]$name) {
    [Flow+RECT]$r = New-Object 'Flow+RECT'; [void][Flow]::GetClientRect($process.MainWindowHandle, [ref]$r)
    [Flow+RECT]$o = New-Object 'Flow+RECT'; [void][Flow]::ClientToScreen($process.MainWindowHandle, [ref]$o)
    $bitmap = New-Object System.Drawing.Bitmap $r.R, $r.B
    $g = [System.Drawing.Graphics]::FromImage($bitmap); $g.CopyFromScreen($o.L, $o.T, 0, 0, $bitmap.Size); $g.Dispose()
    $bitmap.Save((Join-Path $PSScriptRoot "out/flow-$name.png")); $bitmap.Dispose()
}
function Key([byte]$vk, [byte]$scan, [uint32]$extended = 0) {
    [void][Flow]::SetForegroundWindow($process.MainWindowHandle)
    [Flow]::keybd_event($vk, $scan, $extended, [UIntPtr]::Zero); Start-Sleep -Milliseconds 90
    [Flow]::keybd_event($vk, $scan, 2 + $extended, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120
}
try {
    for ($i = 0; $i -lt 200 -and $process.MainWindowHandle -eq 0; ++$i) { Start-Sleep -Milliseconds 100; $process.Refresh() }
    [void][Flow]::SetForegroundWindow($process.MainWindowHandle)
    Start-Sleep -Milliseconds 300; Shot 'title-intro'
    Start-Sleep -Seconds 4; Shot 'title'
    Key 0x28 0x50 1; Start-Sleep -Milliseconds 300; Shot 'title-down'
    Key 0x26 0x48 1; Start-Sleep -Milliseconds 200
    Key 0x28 0x50 1; Start-Sleep -Milliseconds 200; Key 0x0D 0x1C; Start-Sleep -Milliseconds 400; Shot 'credit-1'
    Key 0x27 0x4D 1; Start-Sleep -Milliseconds 700; Shot 'credit-2'
    Key 0x1B 0x01; Start-Sleep -Milliseconds 300; Key 0x26 0x48 1; Start-Sleep -Milliseconds 200
    Key 0x0D 0x1C; Start-Sleep -Milliseconds 500; Shot 'leave-1'; Start-Sleep -Milliseconds 420; Shot 'leave-2'; Start-Sleep -Milliseconds 300; Shot 'leave-3'; Start-Sleep -Milliseconds 200; Shot 'title-leaving'
    Start-Sleep -Seconds 2; Shot 'select'
    Key 0x27 0x4D 1; Start-Sleep -Milliseconds 700; Shot 'select-right'
    Key 0x25 0x4B 1; Start-Sleep -Milliseconds 400
    Key 0x1B 0x01; Start-Sleep -Seconds 2; Shot 'back-title'
    Key 0x0D 0x1C; Start-Sleep -Seconds 3
    Key 0x0D 0x1C; Start-Sleep -Milliseconds 250; Shot 'select-leaving'
    Start-Sleep -Milliseconds 1500; Shot 'load-1'
    Start-Sleep -Seconds 2; Shot 'load-2'; Start-Sleep -Seconds 24
    Shot 'game'
    Key 0x1B 0x01; Start-Sleep -Milliseconds 600; Shot 'pause'
    Key 0x28 0x50 1; Start-Sleep -Milliseconds 200; Key 0x0D 0x1C; Start-Sleep -Milliseconds 500; Shot 'graphics'
    Key 0x1B 0x01; Start-Sleep -Milliseconds 300; Shot 'pause-back'
    Key 0x26 0x48 1; Start-Sleep -Milliseconds 200; Key 0x0D 0x1C; Start-Sleep -Milliseconds 500; Shot 'resumed'
    Start-Sleep -Seconds 12; Shot 'result-timeup'
    Key 0x52 0x13; Start-Sleep -Seconds 1; Shot 'result-retry'
    Start-Sleep -Seconds 20; Shot 'result-again'
    Key 0x09 0x0F; Start-Sleep -Seconds 4; Shot 'result-title'
    if ($process.HasExited) { throw "The game stopped: $($process.ExitCode)" }
    'PASS flow captured'
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
    if (Test-Path $backup) { Copy-Item -LiteralPath $backup -Destination $save -Force }
}
