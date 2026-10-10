MariaDB Connector/C 源码编译的配套文件
========================================

用途：chatserver 在 dbtype=mysql 时用 MariaDB Connector/C 连接 MySQL / MariaDB。自 2026-10-09 起，
连接器以源码形式随仓库提交（..\mariadb-connector-c-3.4.9，官方 3.4.9 发布包裁剪而来，没有改动任何文件），
编译时从源码编出，不再使用预编译的静态库。方案与实施记录见 docs\服务端依赖库源码编译方案.md。

本目录的内容
------------

    gen_config.ps1                         对上游只做 CMake 配置，生成下面 config\x64 里的文件
    config\x64\ma_config.h                上游配置阶段生成的平台探测结果
    config\x64\mariadb_version.h          上游配置阶段生成的公共头文件（版本号、默认字符集、插件目录）
    config\x64\ma_client_plugin.c         上游配置阶段生成的静态插件登记表
    config\x64\mariadbclient_manifest.txt 上游 mariadb_obj 目标的源文件、宏、包含目录与编译选项

两个平台怎么编
--------------

Windows：windowsproj\mariadbclient.vcxproj 是纯 C 的静态库工程（只有 Debug|x64、Release|x64），
不运行上游 CMake，源文件、宏与包含目录照抄 config\x64\mariadbclient_manifest.txt，并使用
config\x64 下的三个生成文件。chatserver 与 Tests 以工程引用的方式链接它。

Linux：CMakeLists.txt 用 add_subdirectory 引入上游 CMake，由它在配置阶段现场生成文件，
不使用本目录下的任何文件。

两边都让连接器使用服务端的 zlib1.3.2，不编译连接器自带的那份 zlib；编译参数与此前的预编译库相同：
Windows 用 SChannel、Linux 用 OpenSSL 作 TLS 后端，认证插件全部编成静态。

升级连接器版本
--------------

1. 用新版官方源码替换 ..\mariadb-connector-c-3.4.9（目录名随版本号改），按方案文档第 3 节的清单删去
   unittest、external、benchmark、client 等不需要的目录；
2. 运行 powershell -ExecutionPolicy Bypass -File gen_config.ps1（需要 cmake 与 Visual Studio 2022）；
3. 用 git diff 对照 config\x64\mariadbclient_manifest.txt 的变化，同步修改 windowsproj\mariadbclient.vcxproj
   与 .filters；
4. Windows 与 Linux 都重新编译，跑服务端测试中的 MDB1～MDB3（MDB1 里的版本号要跟着改）。
