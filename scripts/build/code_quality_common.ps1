$script:CodeQualityRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))

function Find-CodeQualityTool {
    param([Parameter(Mandatory)][string]$Name, [string]$ToolPath)

    if ($ToolPath) {
        return (Resolve-Path -LiteralPath $ToolPath -ErrorAction Stop).Path
    }

    # The complete LLVM distribution includes builtin headers needed by DirectXMath.
    $programDirectory = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
    $llvmTool = Join-Path $programDirectory "LLVM/bin/$Name"
    if (Test-Path -LiteralPath $llvmTool) { return $llvmTool }

    # Prefer the VS 2022 LLVM toolchain used by this solution (LLVM 19).
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installations = @(& $vswhere -products '*' -version '[17.0,18.0)' -property installationPath)
        $installations += @(& $vswhere -products '*' -version '[18.0,)' -property installationPath)
        foreach ($installation in $installations) {
            foreach ($relative in @("VC/Tools/Llvm/x64/bin/$Name", "VC/Tools/Llvm/bin/$Name")) {
                $candidate = Join-Path $installation $relative
                if (Test-Path -LiteralPath $candidate) { return $candidate }
            }
        }
    }

    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    throw "$Name was not found. Install the Visual Studio C++ workload / LLVM tools, or supply -ToolPath."
}

function Get-CodeQualityFiles {
    param([string[]]$Files, [switch]$All)

    Push-Location $script:CodeQualityRoot
    try {
        if ($Files) {
            $candidates = foreach ($file in $Files) {
                $resolved = (Resolve-Path -LiteralPath $file -ErrorAction Stop).Path
                $prefix = $script:CodeQualityRoot + [System.IO.Path]::DirectorySeparatorChar
                if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "File is outside the repository: $file"
                }
                $resolved.Substring($prefix.Length).Replace('\', '/')
            }
        } else {
            if ($All) {
                $candidates = @(& git -c core.quotepath=false ls-files --cached --others --exclude-standard)
            } else {
                $candidates = @(& git -c core.quotepath=false diff --name-only --diff-filter=ACMR HEAD --)
                if ($LASTEXITCODE -ne 0) { throw 'git diff failed.' }
                $candidates += @(& git -c core.quotepath=false ls-files --others --exclude-standard)
            }
            if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }
        }

        foreach ($candidate in ($candidates | Sort-Object -Unique)) {
            $eligible = $candidate -match '^(Engine|Game|tests)/.+\.(c|cc|cpp|cxx|h|hpp|hxx|inl)$' -and
                $candidate -notmatch '^Engine/ThirdParty/' -and
                (Test-Path -LiteralPath $candidate -PathType Leaf)
            if ($eligible) {
                Join-Path $script:CodeQualityRoot $candidate
            } elseif ($Files) {
                throw "Only first-party C/C++ files under Engine, Game or tests are supported: $candidate"
            }
        }
    } finally {
        Pop-Location
    }
}
