libsodium 1.0.22（编译产物）
============================

用途：net/FrameCrypto.cpp 的协议加密（Ed25519 身份密钥、密钥协商、帧体加解密）。

来源：<仓库根>\third-party\libsodium-1.0.22-RELEASE，官方 release 包，未做任何改动。

产物清单
--------

    include\sodium.h、include\sodium\*.h    公共头文件（取自 Linux 安装结果，含生成的 version.h）
    lib\x64\Debug\libsodium.lib             静态库，/MTd
    lib\x64\Release\libsodium.lib           静态库，/MT
    lib\linux\libsodium.a                   静态库，-fPIC

使用方
------

四个 vcxproj（chatserver / fileserver / imgserver / Tests）都链接它，
且**必须定义 SODIUM_STATIC 宏**：sodium\export.h 里没有这个宏就会把接口声明成
__declspec(dllimport)，链接静态库时符号对不上。

Linux 侧由 CMakeLists.txt 用完整路径链接 lib/linux/libsodium.a，不写库名，
免得链接器在系统目录里找到另一份 libsodium。

重新编译
--------

Windows（用 libsodium 自带的 VS 工程，v143 工具集，与我们的工程一致）：

    build\build_libsodium_windows.ps1

Linux / WSL：

    build/build_libsodium_linux.sh

两个脚本都会把产物复制回本目录。换 libsodium 版本时，改脚本顶部的版本号即可。
