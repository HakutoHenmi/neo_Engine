param([ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Development')

$flags = switch ($Configuration) {
    'Debug' { '/MTd /Od /D_DEBUG /DUSE_IMGUI' }
    'Development' { '/MT /Od /DUSE_IMGUI' }
    'Release' { '/MT /O1 /DNDEBUG /DUSE_IMGUI' }
}
$output = "tests/out/$Configuration"
New-Item -ItemType Directory -Force -Path (Join-Path (Split-Path $PSScriptRoot -Parent) $output) | Out-Null
@{ Flags = $flags; Output = $output }
