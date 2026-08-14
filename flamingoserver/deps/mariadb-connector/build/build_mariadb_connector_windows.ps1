# 编译 MariaDB Connector/C 的四个 Windows 静态库变体（Win32/x64 × Debug/Release）
# 并复制到 deps 下。静态 CRT，与 flamingoserver 各工程保持一致。
$ErrorActionPreference = "Stop"

$version  = "3.4.9"
$destRoot = (Resolve-Path "$PSScriptRoot\..").Path
$srcRoot  = (Resolve-Path "$PSScriptRoot\..\..\..").Path       # flamingoserver
$src      = Join-Path $srcRoot "mariadb-connector-c-$version"
$workRoot = Join-Path $env:TEMP "mariadb-connector-build"

if (-not (Test-Path (Join-Path $src "CMakeLists.txt"))) { throw "找不到连接器源码：$src" }

$options = @(
    "-DWITH_SSL=SCHANNEL",            # 用 Windows 自带 TLS，不引入 OpenSSL
    "-DWITH_UNIT_TESTS=OFF",
    "-DWITH_MSI=OFF",
    "-DWITH_SIGNCODE=OFF",
    "-DWITH_CURL=OFF",
    "-DWITH_EXTERNAL_ZLIB=OFF",
    "-DWITH_MYSQLCOMPAT=OFF",
    "-DINSTALL_INCLUDEDIR=include/mysql",
    # 认证插件编成静态，免得运行期还要带插件 DLL；MySQL 8 的默认认证靠 caching_sha2_password
    "-DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC",
    "-DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC",
    "-DCLIENT_PLUGIN_DIALOG=STATIC",
    "-DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC",
    # 静态 CRT。CMP0091 要显式置 NEW，否则连接器声明的 cmake_minimum_required 会让这个变量被忽略
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>"
)

foreach ($plat in @("Win32", "x64")) {
    $build = Join-Path $workRoot $plat
    if (Test-Path $build) { Remove-Item $build -Recurse -Force }

    Write-Host "配置 $plat ..."
    & cmake -S $src -B $build -G "Visual Studio 18 2026" -A $plat @options
    if ($LASTEXITCODE -ne 0) { throw "$plat 配置失败" }

    foreach ($cfg in @("Debug", "Release")) {
        Write-Host "编译 $plat|$cfg ..."
        & cmake --build $build --config $cfg --target mariadbclient
        if ($LASTEXITCODE -ne 0) { throw "$plat|$cfg 编译失败" }

        $dest = Join-Path $destRoot "lib\$plat\$cfg"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item (Join-Path $build "libmariadb\$cfg\mariadbclient.lib") $dest -Force
        Write-Host "  -> $dest\mariadbclient.lib"
    }
}

# 头文件与平台无关，取 x64 那次的安装结果即可
$incBuild = Join-Path $workRoot "x64"
& cmake --install $incBuild --config Release --prefix (Join-Path $workRoot "install") | Out-Null
$incSrc = Join-Path $workRoot "install\include\mysql"
if (Test-Path $incSrc) {
    Remove-Item (Join-Path $destRoot "include") -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Join-Path $destRoot "include") | Out-Null
    Copy-Item $incSrc (Join-Path $destRoot "include") -Recurse -Force
    Write-Host "头文件已更新到 $destRoot\include\mysql"
}
