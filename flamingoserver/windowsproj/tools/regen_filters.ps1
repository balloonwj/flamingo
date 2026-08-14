# 重新生成四个服务端工程的 .vcxproj.filters（解决方案资源管理器里的分组）。
#
# 为什么要脚本：vcxproj 与 .filters 是两份各自维护的清单，往工程里加文件时很容易只加前者，
# 于是文件平铺在工程根节点下。2026-08-13 整理时，四个工程合计有 186 个文件没有归类。
# 以后加完文件跑一次本脚本即可，不会再漏。
#
# 归类规则，三条，优先级从上到下：
#   1. 第三方源码目录（jsoncpp1.9.0 / zlib1.2.11 / sqlite3）归到 third-party\<目录名> 下，
#      使自家代码与外来代码在树里一眼分开；
#   2. chatserver 工程的 chatserversrc、以及 Tests 工程的 tests，文件太多，按下面两张
#      映射表再分一层。表里查不到的文件会被报出来提醒归类，不会悄悄丢到根节点；
#   3. 其余一律按文件所在目录名归类，filter 名与磁盘目录一一对应。
#
# 本脚本只影响 IDE 里的显示，不参与编译，重跑不会改变任何产物。
#
# balloonwj@qq.com   2026-08-13

$ErrorActionPreference = "Stop"
$projDir = (Resolve-Path "$PSScriptRoot\..").Path
$xmlns = "http://schemas.microsoft.com/developer/msbuild/2003"

#随仓库携带的第三方源码目录，统一收进 third-party 分组
$thirdPartyDirs = @("jsoncpp1.9.0", "zlib1.2.11", "sqlite3")

# chatserversrc 的二级分组。core = 进程骨架与连接层，admin = HTTP 控制台及其后台数据，
# app = 面向 IM 客户端的协议与业务管理器。键是不带扩展名的文件名，.cpp 与 .h 共用一条
$chatserversrcGroup = @{
    #—— core：进程入口、监听与连接层 ——
    "main" = "core"; "ChatServer" = "core"; "ChatSession" = "core"; "TcpSession" = "core"
    "MonitorServer" = "core"; "MonitorSession" = "core"; "BussinessLogic" = "core"
    "Protocol" = "core"; "Msg" = "core"

    #—— admin：控制台 HTTP 接口与其后台数据 ——
    "HttpServer" = "admin"; "HttpSession" = "admin"; "HttpMsg" = "admin"; "AdminManager" = "admin"
    "DepartmentManager" = "admin"; "DepartmentMsg" = "admin"; "DepartmentPath" = "admin"
    "EnterpriseManager" = "admin"; "EmployeeManager" = "admin"
    "AnnouncementManager" = "admin"; "ServiceAccountManager" = "admin"
    "ClientVersionManager" = "admin"; "UpdatePackageScanner" = "admin"

    #—— app：客户端功能 ——
    "LoginProtocol" = "app"; "ContactsProtocol" = "app"; "ProfileProtocol" = "app"
    "RegisterProtocol" = "app"; "RegisterService" = "app"; "AccountName" = "app"
    "SmsCodeManager" = "app"; "FriendProtocol" = "app"; "UserManager" = "app"
    "MessageProtocol" = "app"; "MessageManager" = "app"; "MessagePinManager" = "app"
    "GroupProtocol" = "app"; "GroupManager" = "app"; "GroupPermission" = "app"
    "CardProtocol" = "app"; "CardManager" = "app"
    "FileProtocol" = "app"; "FileStoreManager" = "app"
    "GroupFileProtocol" = "app"; "GroupFileManager" = "app"
    "RemoteDesktopProtocol" = "app"; "RemoteDesktopManager" = "app"
    "CallProtocol" = "app"; "CallManager" = "app"
    "FavoriteProtocol" = "app"; "FavoriteManager" = "app"
    "StarredProtocol" = "app"; "StarredManager" = "app"
    "ContactRemarkManager" = "app"; "AnnouncementProtocol" = "app"; "UpdateProtocol" = "app"
}

# tests 的二级分组。按被测对象分，测试用例与被测实现在树里分开
$testsGroup = @{
    #—— protocol：协议编解码，纯逻辑 ——
    "test_loginprotocol" = "protocol"; "test_contactsprotocol" = "protocol"
    "test_profileprotocol" = "protocol"; "test_registerprotocol" = "protocol"
    "test_friendprotocol" = "protocol"; "test_favoriteprotocol" = "protocol"
    "test_starredprotocol" = "protocol"; "test_messageprotocol" = "protocol"
    "test_groupprotocol" = "protocol"; "test_cardprotocol" = "protocol"
    "test_fileprotocol" = "protocol"; "test_groupfileprotocol" = "protocol"
    "test_remotedesktopprotocol" = "protocol"; "test_callprotocol" = "protocol"
    "test_announcementprotocol" = "protocol"; "test_updateprotocol" = "protocol"
    "test_department_msg" = "protocol"

    #—— manager：业务管理器，多数对着临时 .db 真读真写 ——
    "test_starredmanager" = "manager"; "test_contactremarkmanager" = "manager"
    "test_messagepinmanager" = "manager"; "test_callmanager" = "manager"
    "test_clientversionmanager" = "manager"; "test_adminbootstrap" = "manager"
    "test_registerservice" = "manager"; "test_updatepackagescanner" = "manager"
    "test_updatepackageindex" = "manager"; "test_conversationbriefs" = "manager"
    "test_conversationunread" = "manager"; "test_conversationbench" = "manager"

    #—— db：数据库层本身 ——
    "test_sqlite_translator" = "db"; "test_sqlite_database" = "db"
    "test_sqlite_schema" = "db"; "test_sqldialect" = "db"; "test_dbbackend" = "db"

    #—— net：帧编解码 ——
    "test_framecodec" = "net"; "test_framecompress" = "net"; "test_framecrypto" = "net"
    "test_framepayload" = "net"

    #—— utils：utils 目录下各模块 ——
    "test_apppath" = "utils"; "test_serverconfigloader" = "utils"; "test_asynclog" = "utils"
    "test_filetoken" = "utils"; "test_relaytoken" = "utils"; "test_passwordhasher" = "utils"
    "test_sha256" = "utils"

    #—— logic：不碰文件系统与数据库的纯逻辑模块 ——
    "test_accountname" = "logic"; "test_department_path" = "logic"
    "test_grouppermission" = "logic"

    #—— 测试框架本身留在 tests 根下 ——
    "test_main" = ""; "doctest" = ""
}

#未在映射表里找到的文件，收集起来最后统一提示
$script:unmapped = @()

<#
    算出一个文件该进哪个 filter。
    $include  ：vcxproj 里的相对路径，形如 ..\net\Channel.cpp
    $projName ：工程名，只有 chatserver 才对 chatserversrc 再分组
    返回 filter 名；返回空串表示放在树的根下
#>
function Get-FilterName([string]$include, [string]$projName)
{
    $parts = $include -split "\\"
    if ($parts.Count -lt 2) { return "" }

    $dir = $parts[$parts.Count - 2]
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($parts[$parts.Count - 1])

    if ($thirdPartyDirs -contains $dir) { return "third-party\$dir" }

    if ($dir -eq "chatserversrc" -and $projName -eq "chatserver") {
        if ($chatserversrcGroup.ContainsKey($baseName)) {
            return "chatserversrc\" + $chatserversrcGroup[$baseName]
        }
        $script:unmapped += "$projName : $include（chatserversrc 未归类，请在脚本的映射表里补一条）"
        return "chatserversrc"
    }

    if ($dir -eq "tests") {
        if ($testsGroup.ContainsKey($baseName)) {
            $g = $testsGroup[$baseName]
            if ($g -eq "") { return "tests" }
            return "tests\$g"
        }
        $script:unmapped += "$projName : $include（tests 未归类，请在脚本的映射表里补一条）"
        return "tests"
    }

    return $dir
}

<#
    读出 vcxproj 里的文件清单。
    返回哈希表：条目类型（ClCompile / ClInclude / None / Text）-> 该类型下的路径数组，保持原有顺序
#>
function Read-ProjectItems([string]$vcxprojPath)
{
    [xml]$doc = Get-Content -LiteralPath $vcxprojPath
    $result = [ordered]@{}
    foreach ($kind in @("ClCompile", "ClInclude", "None", "Text")) {
        $list = @()
        foreach ($ig in $doc.Project.ItemGroup) {
            foreach ($node in $ig.ChildNodes) {
                if ($node.LocalName -eq $kind -and $node.Include) { $list += [string]$node.Include }
            }
        }
        if ($list.Count -gt 0) { $result[$kind] = $list }
    }
    return $result
}

<#
    读出现有 .filters 里每个 filter 的 GUID，重新生成时沿用。
    GUID 变了不影响功能，但会让 diff 里多出无谓的改动
#>
function Read-ExistingGuids([string]$filtersPath)
{
    $map = @{}
    if (-not (Test-Path -LiteralPath $filtersPath)) { return $map }
    [xml]$doc = Get-Content -LiteralPath $filtersPath
    foreach ($ig in $doc.Project.ItemGroup) {
        foreach ($node in $ig.ChildNodes) {
            if ($node.LocalName -eq "Filter" -and $node.Include) {
                $map[[string]$node.Include] = [string]$node.UniqueIdentifier
            }
        }
    }
    return $map
}

foreach ($projName in @("chatserver", "fileserver", "imgserver", "Tests")) {
    $vcxproj = Join-Path $projDir "$projName.vcxproj"
    $filters = "$vcxproj.filters"

    $items = Read-ProjectItems $vcxproj
    $guids = Read-ExistingGuids $filters

    #先算出每个文件的 filter，同时把用到的 filter（含各级父节点）收集齐
    $assign = [ordered]@{}
    $usedFilters = New-Object System.Collections.Generic.HashSet[string]
    foreach ($kind in $items.Keys) {
        foreach ($include in $items[$kind]) {
            $f = Get-FilterName $include $projName
            $assign["$kind|$include"] = $f
            if ($f -ne "") {
                #父节点也要声明，否则 VS 不认这个层级
                $segs = $f -split "\\"
                for ($i = 1; $i -le $segs.Count; $i++) {
                    [void]$usedFilters.Add(($segs[0..($i - 1)] -join "\"))
                }
            }
        }
    }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("<?xml version=`"1.0`" encoding=`"utf-8`"?>`r`n")
    [void]$sb.Append("<Project ToolsVersion=`"4.0`" xmlns=`"$xmlns`">`r`n")

    #第一段：filter 定义
    [void]$sb.Append("  <ItemGroup>`r`n")
    foreach ($f in ($usedFilters | Sort-Object)) {
        $guid = if ($guids.ContainsKey($f) -and $guids[$f]) { $guids[$f] } else { "{" + [guid]::NewGuid().ToString() + "}" }
        [void]$sb.Append("    <Filter Include=`"$f`">`r`n")
        [void]$sb.Append("      <UniqueIdentifier>$guid</UniqueIdentifier>`r`n")
        [void]$sb.Append("    </Filter>`r`n")
    }
    [void]$sb.Append("  </ItemGroup>`r`n")

    #后面几段：每种条目类型一段，顺序与 vcxproj 里一致，便于两份清单对照着看
    foreach ($kind in $items.Keys) {
        [void]$sb.Append("  <ItemGroup>`r`n")
        foreach ($include in $items[$kind]) {
            $f = $assign["$kind|$include"]
            if ($f -eq "") {
                [void]$sb.Append("    <$kind Include=`"$include`" />`r`n")
            }
            else {
                [void]$sb.Append("    <$kind Include=`"$include`">`r`n")
                [void]$sb.Append("      <Filter>$f</Filter>`r`n")
                [void]$sb.Append("    </$kind>`r`n")
            }
        }
        [void]$sb.Append("  </ItemGroup>`r`n")
    }

    [void]$sb.Append("</Project>")

    #vcxproj 与 .filters 都是带 BOM 的 UTF-8，保持一致
    [System.IO.File]::WriteAllText($filters, $sb.ToString(), (New-Object System.Text.UTF8Encoding($true)))

    $total = 0
    foreach ($kind in $items.Keys) { $total += $items[$kind].Count }
    Write-Host ("{0,-12} 共 {1,3} 个文件，{2,2} 个分组" -f "$projName :", $total, $usedFilters.Count)
}

if ($script:unmapped.Count -gt 0) {
    Write-Host ""
    Write-Host "以下文件没有在映射表里找到，已暂放到上一级分组，请补进脚本顶部的表："
    $script:unmapped | ForEach-Object { Write-Host "  $_" }
}
