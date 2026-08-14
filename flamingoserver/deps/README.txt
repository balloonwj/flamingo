flamingoserver 的第三方依赖库（编译产物）
==========================================

这里放的是**编译好的**第三方库，源码不在这里：

    libsodium          源码在 <仓库根>\third-party\libsodium-1.0.22-RELEASE
    mariadb-connector  源码在 flamingoserver\mariadb-connector-c-3.4.9

两个库在 Windows 上一律以**静态库**形式链接，运行期不需要携带任何第三方 DLL。
静态库与我们四个工程一样使用**静态 CRT**（Debug 为 /MTd，Release 为 /MT）——
静态库的 CRT 必须与主程序一致，否则一个进程里会出现两份 CRT，各有各的堆。

目录层级刻意与 MSBuild 的宏同名：

    lib\$(Platform)\$(Configuration)\xxx.lib

于是四个 Windows 配置（Win32/x64 × Debug/Release）在 vcxproj 里共用同一行库目录设置，
不会出现某个配置漏配、编到一半才发现的情况。Linux 侧只有一份，放在 lib\linux 下。

Linux 侧两个库同样以静态库形式链接（lib/linux/libsodium.a 与
lib/linux/libmariadbclient.a），CMakeLists.txt 按完整路径链接它们、不写库名，
既避免链接器在系统目录里找到另一份版本不同的库，也让机器上不装
mysql-devel / mariadb-devel 就能编译。

各库的来源版本、编译参数与重编方法，见各自目录下的 README.txt。
产物随仓库一起提交，克隆下来即可编译，不必先自己编依赖库。
注意仓库根的 .gitignore 有一条 *.a，会把 Linux 静态库一并忽略掉，因此那里为
这两个路径写了显式的放行规则；新增 Linux 依赖库时别忘了照做，否则产物提交不上去
（libsodium 的 Linux 静态库此前就因此一直没进版本库）。
