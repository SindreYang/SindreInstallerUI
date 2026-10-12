<##
.SYNOPSIS
    在隔离目录中验证原生 NSIS/MUI2 安装器的安装、升级和卸载闭环。

.DESCRIPTION
    测试副本使用普通用户权限和 HKCU，避免修改真实系统目录或触发 UAC。
    测试仍执行由 NSIS 编译出的真实安装器、真实卸载器和真实 payload 提取逻辑。
    生产脚本继续使用管理员权限和 HKLM；本脚本只验证流程本身。

.PARAMETER Root
    测试工作目录。默认使用 F:\tmp\SindreInstallerUI-closure。
#>

[CmdletBinding()]
param(
    [string]$Root = 'F:\tmp\SindreInstallerUI-closure'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$nsis = Join-Path $repo 'dist\thirds\nsis\makensis.exe'
if (-not (Test-Path -LiteralPath $nsis -PathType Leaf)) {
    throw "找不到 makensis.exe：$nsis"
}

function Write-Utf8NoBom {
    param([string]$Path, [string]$Content)
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Assert-Closure {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "验证失败：$Message"
    }
}

function Invoke-Installer {
    param([string]$Executable, [string]$InstallDirectory)
    $arguments = @('/S', "/D=$InstallDirectory")
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    Assert-Closure ($process.ExitCode -eq 0) "安装器退出码为 $($process.ExitCode)：$Executable"
}

function Invoke-Uninstaller {
    param([string]$Executable)
    $process = Start-Process -FilePath $Executable -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
    Assert-Closure ($process.ExitCode -eq 0) "卸载器退出码为 $($process.ExitCode)：$Executable"
}

if (Test-Path -LiteralPath $Root) {
    Remove-Item -LiteralPath $Root -Recurse -Force
}
New-Item -ItemType Directory -Path $Root | Out-Null

$themes = @(
    @{ Name = 'native'; Source = 'preview\native'; Script = 'native.nsi'; Product = 'SindreNativeClosure'; Installer = 'SindreNativeClosure.exe' },
    @{ Name = 'simple-mui2'; Source = 'preview\native_simple'; Script = 'native_simple.nsi'; Product = 'SindreSimpleMUIClosure'; Installer = 'SindreSimpleMUIClosure.exe' }
)

$results = [System.Collections.Generic.List[object]]::new()
foreach ($theme in $themes) {
    $build = Join-Path $Root $theme.Name
    $themeSource = Join-Path $repo $theme.Source
    $themeBuild = Join-Path $build $theme.Name
    $installDirectory = Join-Path $build 'installed'
    New-Item -ItemType Directory -Path $themeBuild -Force | Out-Null
    Copy-Item (Join-Path $themeSource '*') $themeBuild -Recurse -Force

    $scriptPath = Join-Path $themeBuild $theme.Script
    $script = Get-Content -LiteralPath $scriptPath -Raw
    # 闭环测试使用隔离目录，避免测试权限依赖真实的 Program Files。
    $script = $script.Replace('InstallDir "$PROGRAMFILES\${INSTALL_DIR_NAME}"', "InstallDir `"$installDirectory`"")
    $script = $script.Replace('InstallDir "$PROGRAMFILES\${PRODUCT_NAME}"', "InstallDir `"$installDirectory`"")
    $script = $script.Replace('RequestExecutionLevel admin', 'RequestExecutionLevel user')
    $script = $script.Replace('HKLM', 'HKCU')
    $script = $script.Replace('!define PRODUCT_NAME "SindrePy"', "!define PRODUCT_NAME `"$($theme.Product)`"")
    $script = $script.Replace('!define PRODUCT_KEY "${PRODUCT_NAME}"', "!define PRODUCT_KEY `"$($theme.Product)`"")
    $script = $script.Replace('!define PRODUCT_KEY "SindrePy"', "!define PRODUCT_KEY `"$($theme.Product)`"")
    $script = $script.Replace('!define PRODUCT_KEY "SindrePySimpleMUI"', "!define PRODUCT_KEY `"$($theme.Product)`"")
    $script = $script.Replace('!define INSTALL_DIR_NAME "${PRODUCT_NAME}"', "!define INSTALL_DIR_NAME `"$($theme.Product)`"")
    $script = $script.Replace('!define INSTALL_DIR_NAME "SindrePy"', "!define INSTALL_DIR_NAME `"$($theme.Product)`"")
    $script = $script.Replace('!define INSTALLER_NAME "SindrePy_native.exe"', "!define INSTALLER_NAME `"$($theme.Installer)`"")
    $script = $script.Replace('!define INSTALLER_NAME "SindrePy_simple_mui2.exe"', "!define INSTALLER_NAME `"$($theme.Installer)`"")
    Write-Utf8NoBom $scriptPath $script

    # 清理同名测试项，避免上一次中断留下的安装目录被当作升级目标。
    Remove-Item -LiteralPath "Registry::HKEY_CURRENT_USER\Software\$($theme.Product)" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\$($theme.Product)" -Recurse -Force -ErrorAction SilentlyContinue

    $payload = Join-Path $themeBuild 'payload\README.txt'
    Write-Utf8NoBom $payload "closure-v1-$($theme.Name)`r`n"

    Push-Location $themeBuild
    try {
        & $nsis /V0 /NOCD $theme.Script | Out-Host
        Assert-Closure ($LASTEXITCODE -eq 0) "主题编译失败：$($theme.Name)"
    }
    finally {
        Pop-Location
    }

    $installer = Join-Path $themeBuild $theme.Installer
    Invoke-Installer $installer $installDirectory
    $installedPayload = Join-Path $installDirectory 'README.txt'
    $uninstaller = Join-Path $installDirectory 'uninst.exe'
    $registryPath = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\$($theme.Product)"
    Assert-Closure (Test-Path -LiteralPath $installedPayload) "$($theme.Name) 安装后 payload 不存在"
    Assert-Closure (Test-Path -LiteralPath $uninstaller) "$($theme.Name) 安装后卸载器不存在"
    Assert-Closure ((Get-Content -LiteralPath $installedPayload -Raw).Trim() -eq "closure-v1-$($theme.Name)") "$($theme.Name) 安装内容不正确"
    Assert-Closure (Test-Path -LiteralPath $registryPath) "$($theme.Name) 安装后卸载注册表项不存在"

    $script = $script.Replace('0.1.0.0', '0.2.0.0')
    Write-Utf8NoBom $scriptPath $script
    Write-Utf8NoBom $payload "closure-v2-$($theme.Name)`r`n"
    Push-Location $themeBuild
    try {
        & $nsis /V0 /NOCD $theme.Script | Out-Host
        Assert-Closure ($LASTEXITCODE -eq 0) "$($theme.Name) 升级包编译失败"
    }
    finally {
        Pop-Location
    }
    Invoke-Installer $installer $installDirectory
    Assert-Closure ((Get-Content -LiteralPath $installedPayload -Raw).Trim() -eq "closure-v2-$($theme.Name)") "$($theme.Name) 升级后 payload 未替换"
    $version = (Get-ItemProperty -LiteralPath $registryPath).DisplayVersion
    Assert-Closure ($version -eq '0.2.0.0') "$($theme.Name) 升级后版本号不正确：$version"

    Invoke-Uninstaller $uninstaller
    # NSIS 卸载器退出后还需要一点时间删除自身文件。
    Start-Sleep -Milliseconds 1000
    Assert-Closure (-not (Test-Path -LiteralPath $installDirectory)) "$($theme.Name) 卸载后安装目录仍存在"
    Assert-Closure (-not (Test-Path -LiteralPath $registryPath)) "$($theme.Name) 卸载后注册表项仍存在"
    $results.Add([pscustomobject]@{ Theme = $theme.Name; Install = 'PASS'; Upgrade = 'PASS'; Uninstall = 'PASS' })
}

$results | Format-Table -AutoSize
Write-Output "闭环验证通过：$Root"
