. "$PSScriptRoot/code_quality_common.ps1"
$script:ReadabilityToolsRoot = Join-Path $script:CodeQualityRoot 'artifacts/code-quality/tools'

function Get-ReadabilityFiles {
    param([string[]]$Files, [string]$Project = 'All', [switch]$IncludeTests)
    if ($Files) {
        Get-CodeQualityFiles -Files $Files
        return
    }
    foreach ($file in @(Get-CodeQualityFiles -All)) {
        $relative = $file.Substring($script:CodeQualityRoot.Length + 1).Replace('\', '/')
        if (($Project -eq 'All' -and $relative -match '^(Engine|Game)/') -or
            ($Project -ne 'All' -and $relative.StartsWith("$Project/", [StringComparison]::OrdinalIgnoreCase)) -or
            ($IncludeTests -and $relative.StartsWith('tests/'))) {
            $file
        }
    }
}

function Get-LizardPython {
    $python = Join-Path $script:ReadabilityToolsRoot 'python/Scripts/python.exe'
    if (-not (Test-Path -LiteralPath $python)) {
        throw 'Lizard is not installed. Run scripts/build/setup_code_quality_tools.ps1 first.'
    }
    return $python
}

function Get-CpdRuntime {
    $manifest = Get-Content -LiteralPath "$PSScriptRoot/code_quality_tools.json" -Raw | ConvertFrom-Json
    $javaRoot = Join-Path $script:ReadabilityToolsRoot $manifest.java.directory
    $java = @(Get-ChildItem -LiteralPath $javaRoot -Filter java.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'bin' } | Select-Object -ExpandProperty FullName)
    $pmdHome = Join-Path $script:ReadabilityToolsRoot $manifest.pmd.directory
    if ($java.Count -ne 1 -or -not (Test-Path -LiteralPath (Join-Path $pmdHome 'lib'))) {
        throw 'PMD CPD / Java is not installed. Run scripts/build/setup_code_quality_tools.ps1 first.'
    }
    return [pscustomobject]@{ Java = $java[0]; ClassPath = (Join-Path $pmdHome 'lib/*'); Version = $manifest.pmd.version }
}

function Write-ReadabilityFileList {
    param([string[]]$Files, [string]$Path)
    if ($Files.Count -eq 0) { throw 'No first-party C/C++ files selected.' }
    New-Item -ItemType Directory -Force (Split-Path $Path -Parent) | Out-Null
    [IO.File]::WriteAllLines($Path, $Files, [Text.UTF8Encoding]::new($false))
}
