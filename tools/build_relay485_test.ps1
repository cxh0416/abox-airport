[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$outputDir = Join-Path $repoRoot 'dist\diagnostics\relay485_test'
$configurationSuffix = "Relay485Test$Configuration"
$bootBase = 0x08000000
$appBase = 0x08008000
$bootLimit = 0x8000
$appLimit = 0x37800
$imageSize = 0x40000

function Invoke-Checked {
    param([scriptblock]$Command, [string]$Description)

    & $Command | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Build-Target {
    param(
        [string]$Project,
        [string]$Target,
        [switch]$RelayTest
    )

    $projectRoot = Join-Path $repoRoot $Project
    $buildDir = Join-Path $projectRoot "build\$configurationSuffix"
    $toolchain = Join-Path $projectRoot 'cmake\gcc-arm-none-eabi.cmake'
    $arguments = @(
        '-S', $projectRoot,
        '-B', $buildDir,
        '-G', 'Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DCMAKE_BUILD_TYPE=$Configuration"
    )
    if ($RelayTest) { $arguments += '-DABOX_RELAY485_TEST=ON' }

    Invoke-Checked { & cmake @arguments } "Configure $Project"
    Invoke-Checked { & cmake --build $buildDir --target $Target --parallel 4 } "Build $Project"

    $bin = Join-Path $buildDir "$Target.bin"
    if (-not (Test-Path -LiteralPath $bin -PathType Leaf)) {
        throw "Missing $Project artifact: $bin"
    }
    return (Resolve-Path -LiteralPath $bin).Path
}

function Get-GitValue {
    param([string[]]$Arguments)
    $value = (& git -C $repoRoot @Arguments 2>$null | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($value)) { return 'unknown' }
    return $value
}

function Get-GitDirty {
    $status = (& git -C $repoRoot status --porcelain 2>$null | Out-String).Trim()
    return -not [string]::IsNullOrWhiteSpace($status)
}

function Get-UInt32LE {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToUInt32($Data, $Offset)
}

function Assert-ImageVector {
    param(
        [byte[]]$Image,
        [int]$Offset,
        [uint32]$StartAddress,
        [uint32]$EndAddress,
        [string]$Name
    )

    if (($Image.Length - $Offset) -lt 8) { throw "$Name image has no vector table." }
    $stackPointer = Get-UInt32LE $Image $Offset
    $resetVector = Get-UInt32LE $Image ($Offset + 4)
    $codeAddress = $resetVector -band 0xFFFFFFFE
    if (($stackPointer -band 0x2FFE0000) -ne 0x20000000) {
        throw "$Name stack pointer is invalid: 0x$('{0:X8}' -f $stackPointer)."
    }
    if (($codeAddress -lt $StartAddress) -or ($codeAddress -gt $EndAddress)) {
        throw "$Name reset vector is outside its Flash range: 0x$('{0:X8}' -f $resetVector)."
    }
}

& (Join-Path $repoRoot 'platform\tools\validate_flash_layout.ps1') `
    -BootLinker (Join-Path $repoRoot 'boot\STM32F105XX_FLASH.ld') `
    -AppLinker (Join-Path $repoRoot 'app\STM32F105XX_FLASH.ld') | Out-Host

$bootBin = Build-Target 'boot' 'ATbox_BootLoader'
$appBin = Build-Target 'app' 'ATbox_V1_0' -RelayTest

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$bootOut = Join-Path $outputDir 'ABox_Relay485_Test_Boot.bin'
$appOut = Join-Path $outputDir 'ABox_Relay485_Test_App.bin'
$fullOut = Join-Path $outputDir 'ABox_Relay485_Test_Full.bin'
$manifestPath = Join-Path $outputDir 'ABox_Relay485_Test.manifest.json'

Copy-Item -LiteralPath $bootBin -Destination $bootOut -Force
Copy-Item -LiteralPath $appBin -Destination $appOut -Force

$bootData = [System.IO.File]::ReadAllBytes($bootOut)
$appData = [System.IO.File]::ReadAllBytes($appOut)
if ($bootData.Length -gt $bootLimit) { throw "Boot image exceeds 32 KiB: $($bootData.Length) bytes." }
if ($appData.Length -gt $appLimit) { throw "App image exceeds 0x37800 bytes: $($appData.Length) bytes." }

$fullData = [byte[]]::new($imageSize)
for ($index = 0; $index -lt $fullData.Length; $index++) { $fullData[$index] = 0xFF }
[Array]::Copy($bootData, 0, $fullData, 0, $bootData.Length)
[Array]::Copy($appData, 0, $fullData, ($appBase - $bootBase), $appData.Length)
[System.IO.File]::WriteAllBytes($fullOut, $fullData)

Assert-ImageVector $bootData 0 $bootBase ($appBase - 1) 'Boot'
Assert-ImageVector $appData 0 $appBase ($appBase + $appLimit - 1) 'App'
Assert-ImageVector $fullData 0 $bootBase ($appBase - 1) 'Full Boot'
Assert-ImageVector $fullData ($appBase - $bootBase) $appBase ($appBase + $appLimit - 1) 'Full App'

$reservedOffset = 0x3F800
for ($index = $reservedOffset; $index -lt $fullData.Length; $index++) {
    if ($fullData[$index] -ne 0xFF) {
        throw "Flash reserved page is not erased at offset 0x$('{0:X}' -f $index)."
    }
}

$artifacts = @(
    [ordered]@{
        name = Split-Path -Leaf $appOut
        path = $appOut
        bytes = (Get-Item -LiteralPath $appOut).Length
        flash_base = '0x08008000'
        sha256 = (Get-FileHash -LiteralPath $appOut -Algorithm SHA256).Hash
    },
    [ordered]@{
        name = Split-Path -Leaf $bootOut
        path = $bootOut
        bytes = (Get-Item -LiteralPath $bootOut).Length
        flash_base = '0x08000000'
        sha256 = (Get-FileHash -LiteralPath $bootOut -Algorithm SHA256).Hash
    },
    [ordered]@{
        name = Split-Path -Leaf $fullOut
        path = $fullOut
        bytes = (Get-Item -LiteralPath $fullOut).Length
        flash_base = '0x08000000'
        sha256 = (Get-FileHash -LiteralPath $fullOut -Algorithm SHA256).Hash
    }
)

$manifest = [ordered]@{
    manifest_version = 1
    product = 'Airport_vehicle'
    configuration = $Configuration
    diagnostic = 'ABOX_RELAY485_TEST'
    source_commit = Get-GitValue @('rev-parse', 'HEAD')
    source_dirty = Get-GitDirty
    flash_layout = [ordered]@{
        boot_base = '0x08000000'
        boot_bytes = $bootLimit
        app_base = '0x08008000'
        app_limit_bytes = $appLimit
        reserved_page = '0x0803F800'
    }
    artifacts = $artifacts
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Relay485 diagnostic artifacts written to $outputDir"
$artifacts | ForEach-Object { Write-Host ("{0}: {1} bytes SHA-256 {2}" -f $_.name, $_.bytes, $_.sha256) }
