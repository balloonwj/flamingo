flamingoserver 的第三方依赖库（编译产物，只供 Windows 使用）
==============================================================

这里放的是 Windows 服务端链接的**编译好的**第三方库，源码不在这里：

    libsodium          源码在 <仓库根>\third-party\libsodium-1.0.22-RELEASE

Linux 上不使用这里的库。2026-10-10 起 Linux 的 libsodium 与 OpenSSL 都在 cmake 配置阶段从随仓库提交的源码
（flamingoserver\libsodium-1.0.22、flamingoserver\openssl-3.5.9）编出，见 flamingoserver\cmake\FlamingoDeps.cmake
与 docs\服务端依赖库源码编译方案.md 第二期。此前这里还有一份 Linux 的 libsodium 静态库（lib\linux\libsodium.a），
它在 Ubuntu 24.04 上编出，要求 glibc 2.25 以上，CentOS 7（glibc 2.17）上链接失败，已删除。

MySQL 客户端（MariaDB Connector/C 3.4.9）此前也以预编译库的形式放在这里（deps\mariadb-connector），
2026-10-09 起改为随仓库提交源码（flamingoserver\mariadb-connector-c-3.4.9）、编译时一并编出，
那个目录已删除，见 flamingoserver\mariadbclient\README.txt 与 docs\服务端依赖库源码编译方案.md。

libsodium 在 Windows 上以**静态库**形式链接，运行期不需要携带任何第三方 DLL。
静态库与我们的工程一样使用**静态 CRT**（Debug 为 /MTd，Release 为 /MT）——
静态库的 CRT 必须与主程序一致，否则一个进程里会出现两份 CRT，各有各的堆。

目录层级刻意与 MSBuild 的宏同名：

    lib\$(Platform)\$(Configuration)\xxx.lib

于是两个 Windows 配置（x64 的 Debug / Release）在 vcxproj 里共用同一行库目录设置，
不会出现某个配置漏配、编到一半才发现的情况。

来源版本、编译参数与重编方法见 libsodium\README.txt。
产物随仓库一起提交，克隆下来即可编译，不必先自己编依赖库。
