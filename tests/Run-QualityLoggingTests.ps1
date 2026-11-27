$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../scripts/build/readability_common.ps1"
$python = Get-LizardPython
& $python "$PSScriptRoot/quality_logging_tests.py"
if ($LASTEXITCODE -ne 0) { throw 'Quality logging tests failed.' }
