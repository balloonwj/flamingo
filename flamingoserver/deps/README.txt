flamingoserver 的第三方依赖库（编译产物）
==========================================

这里放的是**编译好的**第三方库，源码不在这里：

    libsodium          源码在 <仓库根>\third-party\libsodium-1.0.22-RELEASE

MySQL 客户端（MariaDB Connector/C 3.4.9）此前也以预编译库的形式放在这里（deps\mariadb-connector），
2026-10-09 起改为随仓库提交源码（flamingoserver\mariadb-connector-c-3.4.9）、编译时一并编出，
那个目录已删除，见 flamingoserver\mariadbclient\README.txt 与 docs\服务端依赖库源码编译方案.md。

libsodium 在 Windows 上以**静态库**形式链接，运行期不需要携带任何第三方 DLL。
静态库与我们的工程一样使用**静态 CRT**（Debug 为 /MTd，Release 为 /MT）——
静态库的 CRT 必须与主程序一致，否则一个进程里会出现两份 CRT，各有各的堆。

目录层级刻意与 MSBuild 的宏同名：

    lib\$(Platform)\$(Configuration)\xxx.lib

于是两个 Windows 配置（x64 的 Debug / Release）在 vcxproj 里共用同一行库目录设置，
不会出现某个配置漏配、编到一半才发现的情况。Linux 侧只有一份，放在 lib\linux 下。

Linux 侧同样以静态库形式链接（lib/linux/libsodium.a），CMakeLists.txt 按完整路径链接、
不写库名，避免链接器在系统目录里找到另一份版本不同的库。

来源版本、编译参数与重编方法见 libsodium\README.txt。
产物随仓库一起提交，克隆下来即可编译，不必先自己编依赖库。
注意仓库根的 .gitignore 有一条 *.a，会把 Linux 静态库一并忽略掉，因此那里为
libsodium 的路径写了显式的放行规则；新增 Linux 依赖库时别忘了照做，否则产物提交不上去
（libsodium 的 Linux 静态库此前就因此一直没进版本库）。
