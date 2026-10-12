libsodium 1.0.22（编译产物，只供 Windows 使用）
==============================================

用途：net/FrameCrypto.cpp 的协议加密（Ed25519 身份密钥、密钥协商、帧体加解密）。

来源：<仓库根>\third-party\libsodium-1.0.22-RELEASE，官方 release 包，未做任何改动。

产物清单
--------

    include\sodium.h、include\sodium\*.h    公共头文件（取自 Linux 安装结果，含生成的 version.h）
    lib\x64\Debug\libsodium.lib             静态库，/MTd
    lib\x64\Release\libsodium.lib           静态库，/MT

使用方
------

四个 vcxproj（chatserver / fileserver / imgserver / Tests）都链接它，
且**必须定义 SODIUM_STATIC 宏**：sodium\export.h 里没有这个宏就会把接口声明成
__declspec(dllimport)，链接静态库时符号对不上。

Linux 不使用这里的库：2026-10-10 起 CMakeLists.txt 在配置阶段从 flamingoserver/libsodium-1.0.22
（同一个官方发布包裁剪而来）编出静态库并按完整路径链接，见 flamingoserver/cmake/FlamingoDeps.cmake。
此前这里的 lib/linux/libsodium.a 与 build/build_libsodium_linux.sh 已删除。

重新编译
--------

Windows（用 libsodium 自带的 VS 工程，v143 工具集，与我们的工程一致）：

    build\build_libsodium_windows.ps1

脚本会把产物复制回本目录。换 libsodium 版本时，改脚本顶部的版本号；头文件在 Linux 上对新版本源码
运行一次 cmake 后，从构建目录下的 deps-install/libsodium/include 复制过来。Linux 侧换版本时，
替换 flamingoserver/libsodium-<版本>/ 并修改 CMakeLists.txt 里 flamingo_build_libsodium 的两个参数。
