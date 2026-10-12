<##
.SYNOPSIS
    从任意当前目录编译原生 NSIS/MUI2 安装器。

.PARAMETER Theme
    编译 native、simple-mui2 或 all。
#>

[CmdletBinding()]
param(
    [ValidateSet('native', 'simple-mui2', 'all')]
    [string]$Theme = 'all'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$makensis = Join-Path $repo 'dist\thirds\nsis\makensis.exe'
if (-not (Test-Path -LiteralPath $makensis -PathType Leaf)) {
    throw "找不到 makensis.exe：$makensis"
}

$targets = if ($Theme -eq 'all') { @('native', 'simple-mui2') } else { @($Theme) }
$definitions = @{
    native = @{ Directory = 'preview\native'; Script = 'native.nsi' }
    'simple-mui2' = @{ Directory = 'preview\native_simple'; Script = 'native_simple.nsi' }
}
foreach ($name in $targets) {
    $directory = Join-Path $repo $definitions[$name].Directory
    $script = $definitions[$name].Script
    if (-not (Test-Path -LiteralPath (Join-Path $directory $script) -PathType Leaf)) {
        throw "找不到主题脚本：$directory\$script"
    }
    Push-Location $directory
    try {
        & $makensis /V1 /NOCD $script
        if ($LASTEXITCODE -ne 0) {
            throw "主题编译失败：$name，退出码 $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

Write-Output "原生安装器编译完成：$($targets -join ', ')"
