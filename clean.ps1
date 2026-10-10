<#
    Flamingo 仓库一键清理脚本。

    用途：删除仓库里可再生的垃圾文件与构建产物，释放磁盘空间。所有被删除的目标
    都满足"删掉之后可以通过重新编译或重新运行得到"这一条件，不会触及任何源代码、
    资源文件或第三方依赖源码。

    典型用法（在仓库根目录下执行）：
        powershell -ExecutionPolicy Bypass -File .\clean.ps1              # 清理垃圾文件 + 构建产物
        powershell -ExecutionPolicy Bypass -File .\clean.ps1 -DryRun      # 只列出会删什么，不真的删
        powershell -ExecutionPolicy Bypass -File .\clean.ps1 -IncludeRuntimeData   # 连运行期数据一起清

    注意：执行前请先关闭 Visual Studio，否则 .vs 目录里的 IntelliSense 数据库文件
    会被占用而删除失败。

    balloonwj@qq.com   2026-07-31
#>

[CmdletBinding()]
param
(
    # 是否连同运行期数据一起清理。运行期数据包括本地测试账号的聊天记录、下载的
    # 文件、服务端收到的文件缓存等，删掉之后这些本地数据不可恢复，因此默认不清理。
    [switch]$IncludeRuntimeData,

    # 是否只做预演。为真时只打印将要删除的目标和占用空间，不执行任何删除动作。
    [switch]$DryRun
)

# 字节到 MB 的换算基数，用于把统计出来的字节数格式化成人类可读的大小。
$script:BytesPerMegabyte = 1MB

# 打印大小时保留的小数位数。
$script:SizeDecimalDigits = 1

# ---------------------------------------------------------------------------
# 第一类：垃圾文件。构建日志、编码转换脚本留下的中间清单、调试时抓下来的位图、
# 服务端跑起来时重定向出去的运行日志。这些文件对开发没有任何参考价值。
# 每一项都是相对仓库根目录的路径，支持通配符。
# ---------------------------------------------------------------------------
$script:JunkFilePatterns = @(
    # 客户端历次编译时重定向出来的编译日志
    'build_*.log',
    # 摄像头自测的日志与抓下来的第一帧图像
    'cameratest.log',
    'cameratest_first.bmp',
    # 源码编码普查脚本（GBK/UTF-8 转换）生成的文件清单
    '_files_ascii.txt',
    '_files_gbk.txt',
    '_files_utf16le.txt',
    '_files_utf8bom.txt',
    '_files_utf8nobom.txt',
    '_flamingo_src_list.txt',
    # DXGI 抓屏改造期间为了比对画面 dump 出来的位图（只有 Debug 版的调试入口会生成，落在程序目录 Debug\）
    'flamingoclient\Debug\dxgicap.bmp',
    'flamingoclient\Debug\dxgipipe.bmp',
    'flamingoclient\Debug\dxgipipegdi.bmp',
    # 联系人列表树形控件的调试输出
    'flamingoclient\Debug\tree_diag.log',
    # 三个服务在 Windows 下就地运行时落在工程目录里的日志与手工重定向出来的
    # stdout / stderr。异步日志自己按"服务名.时间戳.进程号.log"命名。
    # 服务端正式的日志目录（flamingoserver\logs 等）归第三类管，此处不涉及。
    'flamingoserver\windowsproj\*.log',
    'flamingoserver\windowsproj\*.out',
    'flamingoserver\windowsproj\*.err',
    # 跑 SQLite 集成用例时落在各处的临时库文件（含 WAL 模式的 -wal / -shm 附属文件）。
    # 每个用例开头都会重建它们，纯粹是测试中间产物。测试程序的工作目录不固定，
    # 所以仓库根、客户端目录、服务端的源码根 / chatserversrc / tools / windowsproj /
    # WSL 构建目录下都可能留下一份。服务端用例的库名形如 flamingo_sqlite_xxx_test.db、
    # flamingo_fileshare_schema_test.db，故服务端一侧按 flamingo_*_test.db* 匹配。
    'flamingo_sqlite_*.db*',
    'flamingoclient\flamingo_sqlite_*.db*',
    'flamingoserver\flamingo_*_test.db*',
    'flamingoserver\chatserversrc\flamingo_*_test.db*',
    'flamingoserver\tools\flamingo_sqlite_*.db*',
    'flamingoserver\windowsproj\flamingo_*_test.db*',
    'flamingoserver\windowsproj\x64\*\flamingo_*_test.db*',
    'flamingoserver\build-linux\flamingo_*_test.db*',
    # 服务端升级包用例（test_updatepackageindex / test_updatepackagescanner）在工作目录下
    # 建的临时目录，落点同上
    'flamingoserver\flamingo_update*_test',
    'flamingoserver\chatserversrc\flamingo_update*_test',
    'flamingoserver\windowsproj\flamingo_update*_test',
    'flamingoserver\windowsproj\x64\*\flamingo_update*_test',
    'flamingoserver\build-linux\flamingo_update*_test',
    # 服务端 Python 脚本跑过之后生成的字节码缓存
    'flamingoserver\tools\__pycache__'
)

# ---------------------------------------------------------------------------
# 第二类：构建产物目录。包含 Visual Studio 的解决方案缓存（.vs）以及各子项目的
# 中间文件与输出目录。删除之后重新编译一次即可完全恢复。
# ---------------------------------------------------------------------------
$script:BuildOutputDirs = @(
    # Visual Studio 解决方案缓存：IntelliSense 数据库、浏览信息等，占用空间最大
    'flamingoclient\.vs',
    'flamingoAdmin\.vs',
    'flamingoserver\.vs',
    # PC 客户端主工程的四个配置输出
    'flamingoclient\Source\Debug',
    'flamingoclient\Source\Release',
    'flamingoclient\Source\Debug_x64',
    'flamingoclient\Source\Release_x64',
    # PC 客户端单元测试工程的中间文件与测试可执行文件
    'flamingoclient\Source\Tests\Unit_Debug_Win32',
    'flamingoclient\Source\Tests\Unit_Debug_x64',
    'flamingoclient\Source\Tests\Unit_Release_x64',
    # PC 客户端的输出目录（2026-10-09 起取代原来的 Bin\）。Debug_Win32\、Release_Win32\ 只有 32 位版本的
    # 编译产物（含单元测试程序），整个删除；Debug\、Release\ 是 x64 版本的运行目录，其中的资源与配置
    # （config、Face、Image、Sound）入库，不能删，只删编译产物与单元测试程序。
    'flamingoclient\Debug_Win32',
    'flamingoclient\Release_Win32',
    'flamingoclient\Debug\Tests',
    'flamingoclient\Release\Tests',
    'flamingoclient\Debug\*.exe',
    'flamingoclient\Debug\*.dll',
    'flamingoclient\Debug\*.pdb',
    'flamingoclient\Debug\*.ilk',
    'flamingoclient\Debug\*.lib',
    'flamingoclient\Debug\*.exp',
    'flamingoclient\Release\*.exe',
    'flamingoclient\Release\*.dll',
    'flamingoclient\Release\*.pdb',
    'flamingoclient\Release\*.lib',
    'flamingoclient\Release\*.exp',
    'flamingoclient\Release\*.iobj',
    'flamingoclient\Release\*.ipdb',
    # 管理端控制台的中间文件、输出目录及其单元测试输出
    'flamingoAdmin\Debug',
    'flamingoAdmin\Release',
    'flamingoAdmin\Debug_x64',
    'flamingoAdmin\Release_x64',
    'flamingoAdmin\Bin',
    'flamingoAdmin\Tests\Debug',
    'flamingoAdmin\Tests\Release',
    'flamingoAdmin\Tests\Debug_x64',
    'flamingoAdmin\Tests\Release_x64',
    # 服务端三个服务、单元测试工程与 MariaDB 连接器静态库工程在 Windows 下的中间文件目录
    'flamingoserver\windowsproj\chatserver',
    'flamingoserver\windowsproj\fileserver',
    'flamingoserver\windowsproj\imgserver',
    'flamingoserver\windowsproj\Tests',
    'flamingoserver\windowsproj\mariadbclient',
    # 服务端在 Windows 下的可执行文件、静态库与调试符号。x64 两个配置输出到 x64\Debug、x64\Release，
    # 只删 exe / lib / pdb / ilk，同目录下的 data、logs 等运行期数据归第三类管。
    # windowsproj 根目录下的那几份是已停用的 32 位配置留下的旧产物。
    'flamingoserver\windowsproj\*.exe',
    'flamingoserver\windowsproj\*.pdb',
    'flamingoserver\windowsproj\*.ilk',
    'flamingoserver\windowsproj\x64\*\*.exe',
    'flamingoserver\windowsproj\x64\*\*.lib',
    'flamingoserver\windowsproj\x64\*\*.pdb',
    'flamingoserver\windowsproj\x64\*\*.ilk',
    # Linux（含 WSL）下在源码根目录就地 cmake 的生成物与编出的可执行文件。
    # 注意 flamingoserver\chatserver 是可执行文件本身，不是目录（见 .gitignore 的说明）。
    'flamingoserver\CMakeCache.txt',
    'flamingoserver\CMakeFiles',
    'flamingoserver\Makefile',
    'flamingoserver\cmake_install.cmake',
    'flamingoserver\chatserver',
    'flamingoserver\fileserver',
    'flamingoserver\imgserver',
    'flamingoserver\flamingo_tests',
    'flamingoserver\flamingo_tests_mysql',
    # 带 MySQL 后端的旧 Linux 构建目录。WSL 开发服务器所在的 build-linux 不在此列：
    # 其中有开发服务器正在使用的数据库与云端录像，重新编译它也要花较长时间。
    'flamingoserver\build-mysql',
    # 第三方音频库的中间文件目录。生成的静态库放在各自的 Bin 子目录里，
    # 删掉 obj 不会导致这两个库需要重新编译。
    'third-party\opus-1.5.2\obj',
    'third-party\speexdsp-1.2.1\obj'
)

# ---------------------------------------------------------------------------
# 第三类：运行期数据。只有显式指定 -IncludeRuntimeData 时才会被清理。
# 这些目录里是程序跑起来之后落盘的本地数据，删掉不影响编译，但本地测试账号的
# 聊天记录、收发过的文件等会一并丢失。
# ---------------------------------------------------------------------------
$script:RuntimeDataDirs = @(
    # 服务端在 Windows 下运行时落盘的数据：文件服务器收到的文件、图片服务器的图片、
    # dbtype=sqlite 时的数据库、升级包投放目录、异步日志。windowsproj 根目录下的那几份
    # 属于已停用的 32 位配置，x64\Debug、x64\Release 下的属于当前在用的 x64 配置。
    #
    # 不在此列的是 WSL 开发服务器的数据：build-linux 下的 data、cloudrecords，以及源码根
    # 目录下的 filecache、imgcache（filecachedir / imgcachedir 按进程工作目录解析，开发服务器
    # 在 flamingoserver 下启动，文件就落在这里）。删掉它们，开发服务器库里的文件与图片消息
    # 就找不到对应文件了，需要时请手工处理。
    'flamingoserver\windowsproj\filecache',
    'flamingoserver\windowsproj\imgcache',
    'flamingoserver\windowsproj\data',
    'flamingoserver\windowsproj\updatecache',
    'flamingoserver\windowsproj\logs',
    'flamingoserver\windowsproj\x64\*\data',
    'flamingoserver\windowsproj\x64\*\filecache',
    'flamingoserver\windowsproj\x64\*\imgcache',
    'flamingoserver\windowsproj\x64\*\logs',
    # 早年在源码根目录就地运行 Linux 服务端时留下的数据库
    'flamingoserver\data',
    # 服务端异步日志的落盘目录（源码根目录就地运行，以及 WSL 开发服务器）
    'flamingoserver\logs',
    'flamingoserver\build-linux\logs',
    # PC 客户端按账号存放的本地数据：聊天记录、头像、下载的文件、崩溃转储（x64 Debug、Release 两个运行目录）
    'flamingoclient\Debug\Users',
    'flamingoclient\Release\Users',
    # PC 客户端的图片缓存
    'flamingoclient\Debug\cache',
    'flamingoclient\Release\cache',
    # PC 客户端的日志目录
    'flamingoclient\Debug\Logs',
    'flamingoclient\Release\Logs',
    # PC 客户端下载的升级包与更新器日志
    'flamingoclient\Debug\update',
    'flamingoclient\Release\update'
)

<#
    统计一个文件或目录占用的字节数。

    参数 path：待统计的绝对路径，可以是文件也可以是目录；路径不存在时返回 0。
    返回值：占用的字节数。目录会递归统计其下所有文件（含隐藏文件）。
    副作用：无。
#>
function Get-TargetSizeInBytes([string]$path)
{
    if (-not (Test-Path -LiteralPath $path))
    {
        return 0
    }

    $item = Get-Item -LiteralPath $path -Force
    if (-not $item.PSIsContainer)
    {
        return $item.Length
    }

    $measured = Get-ChildItem -LiteralPath $path -Recurse -Force -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum
    if ($null -eq $measured.Sum)
    {
        return 0
    }

    return $measured.Sum
}

<#
    把字节数格式化成以 MB 为单位的可读字符串。

    参数 bytes：待格式化的字节数，取值范围为非负整数。
    返回值：形如 "268.3 MB" 的字符串。
    副作用：无。
#>
function Format-SizeInMegabytes([double]$bytes)
{
    $megabytes = [Math]::Round($bytes / $script:BytesPerMegabyte, $script:SizeDecimalDigits)
    return "$megabytes MB"
}

<#
    删除一批清理目标，并统计释放的空间。

    参数 categoryName：这批目标的类别名称，仅用于在控制台上分组显示。
    参数 patterns：相对仓库根目录的路径数组，元素可以带通配符；不存在的路径会被静默跳过。
    参数 repoRoot：仓库根目录的绝对路径。
    返回值：本批目标实际释放（预演模式下为"将要释放"）的字节数。
    副作用：非预演模式下会真实删除磁盘上的文件与目录，并向控制台输出每一项的处理结果。
#>
function Invoke-CleanupCategory([string]$categoryName, [string[]]$patterns, [string]$repoRoot)
{
    Write-Host ""
    Write-Host "── $categoryName ──" -ForegroundColor Cyan

    $categoryBytes = 0
    $hitCount = 0

    foreach ($pattern in $patterns)
    {
        $fullPattern = Join-Path $repoRoot $pattern
        # 这里必须用 Get-Item 而不是 Get-ChildItem：后者拿到一个目录路径时返回的是
        # 目录内部的条目，会把整个构建产物目录展开成成千上万个子项。
        $matchedItems = @(Get-Item -Path $fullPattern -Force -ErrorAction SilentlyContinue)
        foreach ($matched in $matchedItems)
        {
            $targetPath = $matched.FullName
            $targetBytes = Get-TargetSizeInBytes $targetPath
            $relativePath = $targetPath.Substring($repoRoot.Length).TrimStart('\')
            $sizeText = Format-SizeInMegabytes $targetBytes

            if ($DryRun)
            {
                Write-Host ("  [预演] {0,-58} {1,10}" -f $relativePath, $sizeText)
                $categoryBytes += $targetBytes
                $hitCount++
                continue
            }

            try
            {
                Remove-Item -LiteralPath $targetPath -Recurse -Force -Confirm:$false -ErrorAction Stop
                Write-Host ("  [已删] {0,-58} {1,10}" -f $relativePath, $sizeText) -ForegroundColor Green
                $categoryBytes += $targetBytes
                $hitCount++
            }
            catch
            {
                Write-Host ("  [失败] {0,-58} {1}" -f $relativePath, $_.Exception.Message) -ForegroundColor Red
            }
        }
    }

    if ($hitCount -eq 0)
    {
        Write-Host "  （没有找到需要清理的目标，此前应该已经清理过了）" -ForegroundColor DarkGray
    }

    return $categoryBytes
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

# 脚本所在目录即仓库根目录，这样从任何工作目录调用都能定位正确的清理目标。
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition

Write-Host ""
Write-Host "Flamingo 仓库清理" -ForegroundColor Yellow
Write-Host "仓库根目录：$repoRoot"
if ($DryRun)
{
    Write-Host "当前是预演模式，不会真的删除任何文件。" -ForegroundColor Yellow
}

$totalBytes = 0
$totalBytes += Invoke-CleanupCategory "第一类：垃圾文件（日志、中间清单、调试位图）" $script:JunkFilePatterns $repoRoot
$totalBytes += Invoke-CleanupCategory "第二类：构建产物与 Visual Studio 缓存" $script:BuildOutputDirs $repoRoot

if ($IncludeRuntimeData)
{
    $totalBytes += Invoke-CleanupCategory "第三类：运行期数据（本地聊天记录、文件与图片缓存）" $script:RuntimeDataDirs $repoRoot
}
else
{
    Write-Host ""
    Write-Host "── 第三类：运行期数据 ──" -ForegroundColor Cyan
    Write-Host "  已跳过。如需一并清理，请加上 -IncludeRuntimeData 参数。" -ForegroundColor DarkGray
    Write-Host "  注意该类会删除本地测试账号的聊天记录与收发过的文件，且不可恢复。" -ForegroundColor DarkGray
}

Write-Host ""
if ($DryRun)
{
    Write-Host ("预演完成，实际执行可释放约 {0}。" -f (Format-SizeInMegabytes $totalBytes)) -ForegroundColor Yellow
}
else
{
    Write-Host ("清理完成，共释放约 {0}。" -f (Format-SizeInMegabytes $totalBytes)) -ForegroundColor Green
}
Write-Host ""
