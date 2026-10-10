<#
    把 docs/diagrams/ 下 README 用的示意图（flamingo-*.svg）渲染成同名 PNG。
    用 Edge（找不到时用 Chrome）的无头模式截图，按 SVG 根元素的 width / height 设窗口尺寸，
    默认 2 倍像素密度，这样 README 里缩小显示时文字依然清晰。

    典型用法（在仓库根目录）：
        powershell -ExecutionPolicy Bypass -File tools/render_diagrams.ps1
        powershell -ExecutionPolicy Bypass -File tools/render_diagrams.ps1 -Name flamingo-architecture
    balloonwj@qq.com   2026-10-10
#>
param(
    # 只渲染这一张（不带扩展名）；为空时渲染全部 flamingo-*.svg
    [string]$Name = "",
    # 像素密度倍数：2 表示输出 PNG 的宽高是 SVG 尺寸的两倍
    [int]$Scale = 2
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$snapDir = Join-Path $repoRoot "docs\diagrams"

# 依次查找可用的浏览器
$browserCandidates = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
)
$browser = $null
foreach ($candidate in $browserCandidates)
{
    if (Test-Path $candidate)
    {
        $browser = $candidate
        break
    }
}
if ($null -eq $browser)
{
    throw "Neither Edge nor Chrome was found."
}

# 单独的用户数据目录，避免与正在使用的浏览器实例冲突
$profileDir = Join-Path $env:TEMP "flamingo-render-diagrams"

if ($Name -ne "")
{
    $svgFiles = @(Get-Item (Join-Path $snapDir "$Name.svg"))
}
else
{
    $svgFiles = @(Get-ChildItem -Path $snapDir -Filter "flamingo-*.svg")
}

foreach ($svg in $svgFiles)
{
    [xml]$doc = Get-Content -Path $svg.FullName -Encoding UTF8 -Raw
    $width = [int]$doc.svg.width
    $height = [int]$doc.svg.height
    $png = [System.IO.Path]::ChangeExtension($svg.FullName, ".png")
    if (Test-Path $png)
    {
        Remove-Item $png -Force
    }

    $url = "file:///" + ($svg.FullName -replace '\\', '/')
    $browserArgs = @(
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        "--no-first-run",
        "--user-data-dir=$profileDir",
        "--force-device-scale-factor=$Scale",
        "--window-size=$width,$height",
        "--screenshot=$png",
        $url
    )
    $proc = Start-Process -FilePath $browser -ArgumentList $browserArgs -PassThru -Wait -WindowStyle Hidden
    if (-not (Test-Path $png))
    {
        throw "Rendering failed for $($svg.Name) (exit code $($proc.ExitCode))."
    }
    Write-Output ("{0} -> {1} ({2}x{3} @{4}x)" -f $svg.Name, (Split-Path -Leaf $png), $width, $height, $Scale)
}
