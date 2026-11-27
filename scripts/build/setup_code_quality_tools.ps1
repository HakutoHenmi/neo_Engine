[CmdletBinding()]
param([string]$PythonPath = 'python')

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$toolsRoot = Join-Path $repoRoot 'artifacts/code-quality/tools'
$manifest = Get-Content -LiteralPath "$PSScriptRoot/code_quality_tools.json" -Raw | ConvertFrom-Json

function Get-VerifiedArchive {
    param([string]$Url, [string]$Sha256, [string]$Name)
    $archive = Join-Path $toolsRoot "downloads/$Name.zip"
    New-Item -ItemType Directory -Force (Split-Path $archive -Parent) | Out-Null
    if ((Test-Path -LiteralPath $archive) -and (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -eq $Sha256) {
        return $archive
    }
    Write-Host "Downloading $Name..."
    $partial = "$archive.part"
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $partial
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Sha256) {
        throw "SHA256 mismatch for $Name. The archive was not extracted."
    }
    Move-Item -LiteralPath $partial -Destination $archive -Force
    return $archive
}

try {
    New-Item -ItemType Directory -Force $toolsRoot | Out-Null
    $python = Join-Path $toolsRoot 'python/Scripts/python.exe'
    if (-not (Test-Path -LiteralPath $python)) {
        & $PythonPath -m venv (Join-Path $toolsRoot 'python')
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the code quality Python environment.' }
    }
    & $python -m pip install --disable-pip-version-check --requirement "$PSScriptRoot/requirements-code-quality.txt"
    if ($LASTEXITCODE -ne 0) { throw 'Lizard installation failed.' }

    $pmdHome = Join-Path $toolsRoot $manifest.pmd.directory
    if (-not (Test-Path -LiteralPath (Join-Path $pmdHome 'bin/pmd.bat'))) {
        $archive = Get-VerifiedArchive -Url $manifest.pmd.url -Sha256 $manifest.pmd.sha256 -Name "pmd-$($manifest.pmd.version)"
        Expand-Archive -LiteralPath $archive -DestinationPath $toolsRoot -Force
    }
    if (-not (Test-Path -LiteralPath (Join-Path $pmdHome 'bin/pmd.bat'))) { throw 'PMD distribution was not extracted correctly.' }

    $javaRoot = Join-Path $toolsRoot $manifest.java.directory
    $java = @(Get-ChildItem -LiteralPath $javaRoot -Filter java.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'bin' } | Select-Object -ExpandProperty FullName)
    if ($java.Count -eq 0) {
        $archive = Get-VerifiedArchive -Url $manifest.java.url -Sha256 $manifest.java.sha256 -Name "java-$($manifest.java.version)"
        Expand-Archive -LiteralPath $archive -DestinationPath $javaRoot -Force
        $java = @(Get-ChildItem -LiteralPath $javaRoot -Filter java.exe -Recurse |
            Where-Object { $_.Directory.Name -eq 'bin' } | Select-Object -ExpandProperty FullName)
    }
    if ($java.Count -ne 1) { throw 'Expected one project-local Java runtime.' }

    & $python -m lizard --version
    if ($LASTEXITCODE -ne 0) { throw 'Lizard verification failed.' }
    & $java[0] --version
    if ($LASTEXITCODE -ne 0) { throw 'Java verification failed.' }
    & $java[0] -cp (Join-Path $pmdHome 'lib/*') net.sourceforge.pmd.cli.PmdCli --version
    if ($LASTEXITCODE -ne 0) { throw 'PMD verification failed.' }
    Write-Host "Code quality tools installed in $toolsRoot"
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
