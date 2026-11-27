$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../scripts/build/readability_common.ps1"
$python = Get-LizardPython
& $python "$PSScriptRoot/readability_tools_tests.py"
if ($LASTEXITCODE -ne 0) { throw 'Readability tool integration tests failed.' }
