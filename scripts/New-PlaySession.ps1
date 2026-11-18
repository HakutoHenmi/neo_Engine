# New-PlaySession.ps1
# プレイ会で渡すフォルダを作る (ZIP にはしない)。中身はリリース版のゲームと水のデモの 2 つ。
#   ゲームを遊ぶ.bat     … タイトルから始まるリリース版 (全画面。F11 / Alt+Enter で窓に戻せる)
#   水のデモを見る.bat   … 箱庭の水のデモ (--water-demo。窓で始まる)
# 先に Release|x64 をビルドしておく。初めて起動したときのシェーダーのコンパイルを省くため、
# ゲーム (New-SlimeRelease.ps1) と水のデモの両方を一度ずつ動かして Resources/ShaderCache を作る。
param([string]$Executable = '', [string]$Destination = '')
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bundle = if ($Destination) { [IO.Path]::GetFullPath($Destination) } else { Join-Path $repoRoot ('dist/PlaySession_' + (Get-Date -Format 'yyyyMMdd')) }
if (Test-Path -LiteralPath $bundle) { Remove-Item -LiteralPath $bundle -Recurse -Force }
& (Join-Path $PSScriptRoot 'New-SlimeRelease.ps1') -Executable $Executable -Bundle $bundle -NoZip | Out-Null
$exe = Join-Path $bundle 'SlimeAssault.exe'

# 水のデモのシェーダーも配布用のキャッシュへ足す (撮影モードは約 1 分で自分で終わる)
$demo = Start-Process -FilePath $exe -ArgumentList '--water-demo --water-demo-capture --write-shader-cache' -WorkingDirectory $bundle -PassThru
if (!$demo.WaitForExit(300000)) { Stop-Process -Id $demo.Id -Force; throw 'Water demo warm-up timed out.' }
if ($demo.ExitCode -ne 0) { throw "Water demo warm-up exited with code $($demo.ExitCode)." }
# 撮影の画像とログは配らない
foreach ($name in @('tests', 'imgui.ini', 'load-game.png', 'load-profile.txt', 'error_log.txt', 'run_log.txt', 'fluid_debug.txt', 'd3d12_validation_log.txt', 'black_screen_log.txt')) {
    $path = Join-Path $bundle $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}

# 起動用のバッチ (中身は ASCII だけ。どのコードページの cmd でも読める)
$ascii = New-Object System.Text.ASCIIEncoding
[IO.File]::WriteAllText((Join-Path $bundle 'ゲームを遊ぶ.bat'), "@echo off`r`ncd /d `"%~dp0`"`r`nstart `"`" `"%~dp0SlimeAssault.exe`"`r`n", $ascii)
[IO.File]::WriteAllText((Join-Path $bundle '水のデモを見る.bat'), "@echo off`r`ncd /d `"%~dp0`"`r`nstart `"`" `"%~dp0SlimeAssault.exe`" --water-demo`r`n", $ascii)

$readme = @"
チリツモ  プレイ会用  (作成 $(Get-Date -Format 'yyyy/MM/dd'))

■ 起動のしかた
  ・ゲームを遊ぶ.bat     … ゲーム本編 (タイトルから始まります)
  ・水のデモを見る.bat   … 水のデモ (箱庭に積んだ岩から水が流れ、たまっていきます)
  フォルダの中身は動かさず、このフォルダごとコピーして使ってください。

■ ゲーム本編
  ・全画面で始まります。F11 または Alt+Enter で窓に切り替えられます。
  ・Esc / START でポーズ。操作はゲームの中の案内に出ます (キーボード・マウスとゲームパッドの両方で遊べます)。

■ 水のデモ
  ・窓で始まります。F11 または Alt+Enter で全画面にできます。
  ・右ドラッグ / A・D / ←→ : まわりを回る
  ・ホイール / W・S        : 近づく・離れる
  ・R     : 水を空にする
  ・Space : 水を止める・流す
  ・終わるときは窓を閉じてください (× / Alt+F4)。

■ 動く環境
  ・Windows 10 / 11 (64bit)、DirectX 12 に対応した GPU
  ・NVIDIA の RTX の GPU では DLSS を使います (無くても動きます)
"@
[IO.File]::WriteAllText((Join-Path $bundle 'はじめにお読みください.txt'), $readme, (New-Object System.Text.UTF8Encoding $true))
Write-Output $bundle
