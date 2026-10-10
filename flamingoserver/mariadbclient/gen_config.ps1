<#
    重新生成 MariaDB Connector/C 在 Windows x64 上的配置期生成文件，并记录上游 mariadbclient 目标的编译清单。

    用途：Windows 侧用自己的 windowsproj\mariadbclient.vcxproj 从源码编译连接器，不运行上游 CMake。
    上游 CMake 在配置阶段生成的三个文件（include\ma_config.h、include\mariadb_version.h、
    libmariadb\ma_client_plugin.c）由本脚本生成一次后随仓库提交，放在 config\x64\ 下；同时把上游
    mariadbclient 目标实际使用的源文件、宏、包含目录与编译选项写成 config\x64\mariadbclient_manifest.txt，
    编写或核对 mariadbclient.vcxproj 时以它为准。只在升级连接器版本、或修改下面的编译参数时需要重跑。
    方案见 docs\服务端依赖库源码编译方案.md。

    典型用法（在任意目录执行，需要 cmake 与 Visual Studio 2022）：
        powershell -ExecutionPolicy Bypass -File flamingoserver\mariadbclient\gen_config.ps1
        powershell -ExecutionPolicy Bypass -File gen_config.ps1 -SourceDir D:\src\mariadb-connector-c-3.4.10 -KeepWorkDir

    balloonwj@qq.com   2026-10-09
#>

[CmdletBinding()]
param
(
    # 上游源码目录。为空时取随仓库提交的那份（与本脚本同在 flamingoserver 下）。
    # Windows PowerShell 5.1 在 param 块的默认值里取不到 $PSScriptRoot，所以默认值在正文里补
    [string]$SourceDir = "",

    # 临时构建目录，只用于运行上游 CMake 的配置阶段。默认放在系统临时目录下，脚本结束时删除
    [string]$WorkDir = (Join-Path $env:TEMP "flamingo-mariadbclient-config"),

    # CMake 生成器。与服务端工程的工具集 v143 对应
    [string]$Generator = "Visual Studio 17 2022",

    # 为真时保留临时构建目录，便于排查
    [switch]$KeepWorkDir
)

$ErrorActionPreference = "Stop"

$serverRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ($SourceDir -eq "")
{
    $SourceDir = Join-Path $serverRoot "mariadb-connector-c-3.4.9"
}
$SourceDir = (Resolve-Path $SourceDir).Path
$zlibDir = Join-Path $serverRoot "zlib1.3.2"
$outDir = Join-Path $PSScriptRoot "config\x64"

if (-not (Test-Path (Join-Path $SourceDir "CMakeLists.txt")))
{
    throw "找不到连接器源码：$SourceDir"
}
if (-not (Test-Path (Join-Path $zlibDir "zlib.h")))
{
    throw "找不到服务端的 zlib：$zlibDir"
}

# 编译参数与原先 deps\mariadb-connector 预编译库保持一致（见该目录下的 README.txt），
# 只有 zlib 一项不同：连接器不再编译自带的 zlib，改用服务端的 zlib1.3.2。
# 预先设好 ZLIB_FOUND 与几个 ZLIB_* 变量后，上游 CMakeLists.txt 的 IF(NOT ZLIB_FOUND) 分支不会执行，
# 也就不会去系统里查找 zlib；ZLIB_LIBRARY 只是占位，配置阶段不会用到实际的库文件。
$options = @(
    "-DWITH_SSL=SCHANNEL",            # 用 Windows 自带的 TLS，不引入 OpenSSL
    "-DWITH_UNIT_TESTS=OFF",
    "-DWITH_MSI=OFF",
    "-DWITH_SIGNCODE=OFF",
    "-DWITH_CURL=OFF",
    "-DWITH_MYSQLCOMPAT=OFF",
    "-DWITH_EXTERNAL_ZLIB=ON",
    "-DZLIB_FOUND=TRUE",
    "-DZLIB_INCLUDE_DIR=$zlibDir",
    "-DZLIB_LIBRARY=flamingo_zlib",
    "-DZLIB_LIBRARIES=flamingo_zlib",
    # 认证插件全部编成静态，连 MySQL 8 也不需要额外携带插件 DLL
    "-DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC",
    "-DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC",
    "-DCLIENT_PLUGIN_DIALOG=STATIC",
    "-DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC",
    # 静态 CRT。CMP0091 要显式置 NEW，否则连接器声明的 cmake_minimum_required 会让这个变量被忽略
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>"
)

if (Test-Path $WorkDir)
{
    Remove-Item $WorkDir -Recurse -Force
}

Write-Host "配置上游 CMake（只做配置，不编译）..."
& cmake -S $SourceDir -B $WorkDir -G $Generator -A x64 @options
if ($LASTEXITCODE -ne 0)
{
    throw "CMake 配置失败"
}

# 三个配置期生成文件：源路径（相对构建目录）与拷贝后的文件名一致
$generated = @("include\ma_config.h", "include\mariadb_version.h", "libmariadb\ma_client_plugin.c")
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
foreach ($rel in $generated)
{
    $src = Join-Path $WorkDir $rel
    if (-not (Test-Path $src))
    {
        throw "CMake 没有生成 $rel"
    }
    Copy-Item $src (Join-Path $outDir (Split-Path $rel -Leaf)) -Force
    Write-Host "已更新 config\x64\$(Split-Path $rel -Leaf)"
}

<#
    把一段路径列表里的绝对路径换成占位符，便于比较不同机器、不同版本生成的清单。

    参数 text：分号分隔的路径列表或单个路径。
    返回值：把上游源码目录换成 <src>、临时构建目录换成 <build>、服务端根目录换成 <server> 之后的文本。
    副作用：无。
#>
function Convert-ToPlaceholder([string]$text)
{
    $result = $text
    foreach ($pair in @(@($SourceDir, "<src>"), @($WorkDir, "<build>"), @($serverRoot, "<server>")))
    {
        $result = $result.Replace($pair[0], $pair[1]).Replace($pair[0].Replace("\", "/"), $pair[1])
    }
    return $result
}

# 从上游生成的 mariadb_obj.vcxproj 中取出编译清单。上游的 mariadbclient 静态库本身不含源文件，
# 它由 OBJECT 库 mariadb_obj 编出的目标文件打包而成（上游 libmariadb\CMakeLists.txt 的
# ADD_LIBRARY(mariadb_obj OBJECT ...) 与 ADD_LIBRARY(mariadbclient STATIC ...)），源文件、宏与包含目录都在前者里
$projPath = Join-Path $WorkDir "libmariadb\mariadb_obj.vcxproj"
[xml]$proj = Get-Content $projPath -Encoding UTF8
$ns = New-Object System.Xml.XmlNamespaceManager($proj.NameTable)
$ns.AddNamespace("m", "http://schemas.microsoft.com/developer/msbuild/2003")

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# 由 gen_config.ps1 生成，记录上游 mariadb_obj 目标（Windows x64；mariadbclient 静态库由它的目标文件组成）的编译清单。请勿手工修改。")
$lines.Add("# <src> = 上游源码目录，<build> = 临时构建目录，<server> = flamingoserver 目录")
$lines.Add("")
$lines.Add("[cmake]")
$lines.Add((& cmake --version | Select-Object -First 1))
$lines.Add("generator=$Generator")
foreach ($opt in $options)
{
    $lines.Add((Convert-ToPlaceholder $opt))
}

foreach ($cfg in @("Debug", "Release"))
{
    $cond = "'`$(Configuration)|`$(Platform)'=='$cfg|x64'"
    $group = $proj.SelectSingleNode("//m:ItemDefinitionGroup[@Condition=`"$cond`"]/m:ClCompile", $ns)
    $lines.Add("")
    $lines.Add("[$cfg|x64]")
    foreach ($name in @("PreprocessorDefinitions", "AdditionalIncludeDirectories", "AdditionalOptions", "RuntimeLibrary", "WarningLevel", "DisableSpecificWarnings", "CompileAs"))
    {
        $node = $group.SelectSingleNode("m:$name", $ns)
        if ($null -ne $node)
        {
            $lines.Add("$name=" + (Convert-ToPlaceholder $node.InnerText))
        }
    }
}

$lines.Add("")
$lines.Add("[sources]")
foreach ($item in $proj.SelectNodes("//m:ClCompile[@Include]", $ns))
{
    $entry = Convert-ToPlaceholder $item.GetAttribute("Include")
    # 个别源文件有自己的编译选项（如自带 zlib 的 -W0），一并记下
    foreach ($child in $item.ChildNodes)
    {
        $entry += "  {" + $child.LocalName + "=" + (Convert-ToPlaceholder $child.InnerText) + "}"
    }
    $lines.Add($entry)
}

$manifest = Join-Path $outDir "mariadbclient_manifest.txt"
[System.IO.File]::WriteAllLines($manifest, $lines, (New-Object System.Text.UTF8Encoding($true)))
Write-Host "已更新 config\x64\mariadbclient_manifest.txt"

if (-not $KeepWorkDir)
{
    Remove-Item $WorkDir -Recurse -Force
}
