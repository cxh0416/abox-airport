[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("abox-relay485-test-" + [guid]::NewGuid().ToString('N'))
$exe = Join-Path $tempRoot 'relay485_protocol_test.exe'

New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
try {
    & gcc -std=c11 -Wall -Wextra -Werror `
        "-I$(Join-Path $repoRoot 'app\Core\User\Inc')" `
        (Join-Path $repoRoot 'app\Core\User\Src\relay485_protocol.c') `
        (Join-Path $repoRoot 'tools\relay485_protocol_test.c') `
        '-o' $exe
    if ($LASTEXITCODE -ne 0) { throw "gcc failed with exit code $LASTEXITCODE." }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "relay485 protocol tests failed with exit code $LASTEXITCODE." }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
