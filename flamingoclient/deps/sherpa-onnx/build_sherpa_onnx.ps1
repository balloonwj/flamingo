# 编译语音字幕用的 sherpa-onnx 静态库（docs/语音字幕方案.md 4.1、4.2、第 13 节）。
#
# sherpa-onnx 与它链接的 onnxruntime 静态库太大（x64 的 onnxruntime.lib 922 MB），不进版本库。本脚本：
#   1. 按固定标签 v1.13.8 浅克隆源码到本目录的 src\；
#   2. 打上 patches\ 里的补丁：内存文件登记表（让模型能直接从 FlamingoCaptionModel.dll 的资源里加载），
#      以及 C 接口里创建识别器、创建识别流、解码三处的异常捕获（异常穿过 extern "C" 函数会终止整个进程）；
#   3. 用 CMake + VS 2022 编出 Release、静态 CRT（/MT）、只含 C 接口、不含语音合成等的静态库，
#      onnxruntime 1.28.2 的静态库由 sherpa-onnx 的 CMake 按固定 SHA256 下载；
#   4. 把要链接的 .lib 复制到 lib\<平台>\（FlamingoCaption.vcxproj 从这里找，找不到时不链接识别引擎）。
# src\、build-*\、lib\ 都在 .gitignore 里。首次构建每个平台约 20～30 分钟，需要 git、CMake 与外网。
#
# 用法（在 PowerShell 里）：
#   flamingoclient\deps\sherpa-onnx\build_sherpa_onnx.ps1              # 两个平台
#   flamingoclient\deps\sherpa-onnx\build_sherpa_onnx.ps1 -Platform x64
#
# balloonwj@qq.com   2026-10-06
param(
    [ValidateSet('x64', 'Win32', 'all')]
    [string]$Platform = 'all'
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$tag = 'v1.13.8'
$src = Join-Path $here 'src'
$patch = Join-Path $here 'patches\sherpa-onnx-v1.13.8-flamingo.patch'
# 要链接的静态库（onnxruntime.lib 另从 _deps 目录复制）
$libs = @('sherpa-onnx-c-api.lib', 'sherpa-onnx-core.lib', 'sherpa-onnx-fstfar.lib', 'sherpa-onnx-fst.lib',
          'sherpa-onnx-kaldifst-core.lib', 'kaldi-decoder-core.lib', 'kaldi-native-fbank-core.lib',
          'ssentencepiece_core.lib', 'kissfft-float.lib')

# 1. 源码
if (-not (Test-Path (Join-Path $src 'CMakeLists.txt'))) {
    git clone --depth 1 --branch $tag https://github.com/k2-fsa/sherpa-onnx.git $src
    if ($LASTEXITCODE -ne 0) { throw 'git clone failed' }
}

# 2. 补丁：以补丁新增的 memory-file-registry.h 是否存在判断是否已打过
if (-not (Test-Path (Join-Path $src 'sherpa-onnx\csrc\memory-file-registry.h'))) {
    git -C $src apply --whitespace=nowarn $patch
    if ($LASTEXITCODE -ne 0) { throw 'git apply failed' }
}

# 打过补丁的 c-api.h 须与 include\ 里入库的那份一致（FlamingoCaption 编译时包含的是入库的那份）
$a = Get-FileHash (Join-Path $src 'sherpa-onnx\c-api\c-api.h')
$b = Get-FileHash (Join-Path $here 'include\sherpa-onnx\c-api\c-api.h')
if ($a.Hash -ne $b.Hash) { throw 'include\sherpa-onnx\c-api\c-api.h differs from the patched source' }

$platforms = if ($Platform -eq 'all') { @('x64', 'Win32') } else { @($Platform) }
foreach ($p in $platforms) {
    # 3. 构建
    $build = Join-Path $here "build-$p"
    cmake -S $src -B $build -G 'Visual Studio 17 2022' -A $p `
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
        -DBUILD_SHARED_LIBS=OFF -DSHERPA_ONNX_USE_STATIC_CRT=ON `
        -DSHERPA_ONNX_ENABLE_TTS=OFF -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF `
        -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF `
        -DSHERPA_ONNX_ENABLE_BINARY=OFF -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF `
        -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_TESTS=OFF -DSHERPA_ONNX_ENABLE_C_API=ON
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($p)" }
    cmake --build $build --config Release --target sherpa-onnx-c-api -- /m:4 /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($p)" }

    # 4. 复制静态库
    $out = Join-Path $here "lib\$p"
    New-Item -ItemType Directory -Force $out | Out-Null
    foreach ($l in $libs) { Copy-Item (Join-Path $build "lib\Release\$l") $out -Force }
    Copy-Item (Join-Path $build '_deps\onnxruntime-src\lib\onnxruntime.lib') $out -Force
    "sherpa-onnx static libs ready: $out"
}
