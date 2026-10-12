# 编译 libsodium 的两个 Windows 静态库变体（x64 的 Debug / Release）并复制到 deps 下。
# 服务端自 2026-10-09 起只保留 x64，不再编 Win32。
# 用的是 libsodium 自带的 VS 工程，工具集 v143，与 flamingoserver 四个工程保持一致。
# DebugLIB / ReleaseLIB 这两个配置本身就是「静态库 + 静态 CRT」，正是我们要的组合。
$ErrorActionPreference = "Stop"

$version   = "1.0.22-RELEASE"
$repoRoot  = (Resolve-Path "$PSScriptRoot\..\..\..\..").Path
$sodiumSrc = Join-Path $repoRoot "third-party\libsodium-$version"
$proj      = Join-Path $sodiumSrc "builds\msvc\vs2022\libsodium\libsodium.vcxproj"
$destRoot  = (Resolve-Path "$PSScriptRoot\..").Path

# MSBuild 的位置按需修改；也可以先跑 VS 的开发者命令提示符再直接调用 msbuild
$msbuild = "d:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe"

if (-not (Test-Path $proj)) { throw "找不到 libsodium 工程：$proj" }

foreach ($cfg in @("DebugLIB", "ReleaseLIB")) {
    foreach ($plat in @("x64")) {
        Write-Host "编译 $cfg|$plat ..."
        & $msbuild $proj /p:Configuration=$cfg /p:Platform=$plat /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { throw "$cfg|$plat 编译失败" }

        # libsodium 的输出路径固定为 bin\<平台>\<Debug|Release>\<工具集>\static\
        $debugOrRelease = if ($cfg -eq "DebugLIB") { "Debug" } else { "Release" }
        $src  = Join-Path $sodiumSrc "bin\$plat\$debugOrRelease\v143\static\libsodium.lib"
        $dest = Join-Path $destRoot  "lib\$plat\$debugOrRelease"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item $src $dest -Force
        Write-Host "  -> $dest\libsodium.lib"
    }
}

Write-Host "两个变体已更新完毕。头文件不随平台变化，通常不用重新复制；"
Write-Host "换版本时，在 Linux 上对新版本源码运行一次 cmake，用构建目录下 deps-install/libsodium/include 覆盖 deps\libsodium\include 即可。"
