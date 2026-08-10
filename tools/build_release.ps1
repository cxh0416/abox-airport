[CmdletBinding()]
param([ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release')
& "$PSScriptRoot\..\platform\tools\build_product_release.ps1" -ProductRoot "$PSScriptRoot\.." -AppTarget ATbox_V1_0 -BootTarget ATbox_BootLoader -Configuration $Configuration -AppLinker app/STM32F105XX_FLASH.ld -BootLinker boot/STM32F105XX_FLASH.ld
exit $LASTEXITCODE
