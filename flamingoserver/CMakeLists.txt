cmake_minimum_required(VERSION 2.6)

project (FLAMGINGO_SERVER)

# _FILE_OFFSET_BITS=64 让 off_t / fseeko / ftello 在 32 位 Linux 上也是 64 位，
# 使 fileserver 能正确收发超过 2 GiB 的文件（聊天文件与群文件均不限大小）。
# 64 位平台上 off_t 本就是 64 位，这里定义与否行为一致，加上只为跨平台一致。
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -g -Wall -O0 -Wno-unused-variable -pthread -D_FILE_OFFSET_BITS=64")

link_directories(
    ${PROJECT_SOURCE_DIR}/lib
	/usr/lib64/mysql/
)

# MySQL 后端的编译开关。默认编进去，运行时由 etc/chatserver.conf 的 dbtype 决定用哪种后端，
# 一个二进制同时支持 mysql 与 sqlite 两种模式。
# 关掉它（cmake -DWITH_MYSQL=OFF .）可以编出一份不依赖 libmysqlclient 的纯本地版本，
# 适合只跑 dbtype=sqlite、且机器上不想装 mysql-devel / mariadb-devel 的部署。
#
# 没有对应的 WITH_SQLITE 开关：SQLite 是随仓库自带的单文件 amalgamation，不依赖任何外部库，
# 关掉它省不下依赖、只省一点二进制体积，却要在 DbFactory / SqlDialect / main 等处
# 到处加条件编译，不划算。
option(WITH_MYSQL "编译 MySQL 数据库后端" ON)

# 协议加密依赖 libsodium（net/FrameCrypto.cpp），三个服务与单元测试都要链它。
# 头文件与静态库都随仓库提供，放在 deps/libsodium 下：源码包在 ../third-party，
# 编译产物统一收在这里，Windows 四个变体与 Linux 各一份，见 deps/libsodium/README.txt。
# 用完整路径链静态库而不是写库名，是为了避免链接器在系统目录里找到另一份 libsodium。
set(LIBSODIUM_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/deps/libsodium/include)
set(LIBSODIUM_LIB ${PROJECT_SOURCE_DIR}/deps/libsodium/lib/linux/libsodium.a)
include_directories(${LIBSODIUM_INCLUDE_DIR})

if (WITH_MYSQL)
    # MySQL 客户端的头文件用随仓库自带的那份（MariaDB Connector/C 3.4.9），
    # 与 Windows 侧四个 vcxproj 指向的是同一份，两个平台的声明因此不会走岔。
    # 这样 Linux 上不装 mariadb-devel / mysql-devel 也能编译 —— 那两个包在不同发行版
    # 里的头文件路径各不相同（/usr/include/mysql、/usr/include/mariadb 都有），
    # 交给系统去找反而是构建失败的常见原因。来源与重建方式见 deps/README.txt。
    set(MARIADB_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/deps/mariadb-connector/include)
    include_directories(${MARIADB_INCLUDE_DIR})

    # 客户端库也用随仓库自带的那份静态库，不再依赖系统装没装 mysql-devel /
    # mariadb-devel。做法与上面的 libsodium 一致：用完整路径链接，不写库名，
    # 免得链接器在系统目录里找到另一份版本不同的 libmysqlclient。
    # 该库由 deps/mariadb-connector/build/build_mariadb_connector_linux.sh 编出。
    set(MARIADB_LIB ${PROJECT_SOURCE_DIR}/deps/mariadb-connector/lib/linux/libmariadbclient.a)
    IF (NOT EXISTS ${MARIADB_LIB})
        MESSAGE(FATAL_ERROR "${MARIADB_LIB} not found. Run deps/mariadb-connector/build/build_mariadb_connector_linux.sh to build it, or configure with -DWITH_MYSQL=OFF for the sqlite-only variant.")
    ENDIF()

    # 静态库自身不含这些实现，链接可执行文件时要一并带上：
    #   ssl / crypto  连接器编译时选的 TLS 后端是 OPENSSL
    #   z             连接器自带的 zlib 用于压缩协议
    #   dl            认证插件按 dlopen 的方式加载（本仓库全部编成静态，仍需该符号）
    # 顺序有讲究：依赖方在前、被依赖方在后，GNU ld 按此顺序解析静态库。
    set(MYSQL_LIB ${MARIADB_LIB} ssl crypto dl)
else()
    # 用「关掉时才定义宏」的写法而不是「打开时定义」：这样 Windows 的 vcxproj 不用改，
    # 默认路径上也不存在「忘了定义宏导致两个后端都没编进去」这种失败模式
    add_definitions(-DFLAMINGO_NO_MYSQL)
    MESSAGE(STATUS "WITH_MYSQL=OFF: chatserver 将只支持 dbtype=sqlite")
endif()

set(net_srcs 
base/AsyncLog.cpp
base/ConfigFileReader.cpp
base/Platform.cpp
base/Timestamp.cpp

net/Acceptor.cpp
net/ByteBuffer.cpp
net/Channel.cpp
net/Connector.cpp
net/EpollPoller.cpp
net/EventLoop.cpp
net/EventLoopThread.cpp
net/EventLoopThreadPool.cpp
net/InetAddress.cpp
net/Poller.cpp
net/PollPoller.cpp
net/ProtocolStream.cpp
net/FrameCodec.cpp
net/FrameCrypto.cpp
net/FramePayload.cpp
net/SelectPoller.cpp
net/Sockets.cpp
net/TcpClient.cpp
net/TcpConnection.cpp
net/TcpServer.cpp
net/Timer.cpp
net/TimerQueue.cpp
)

# 数据库抽象层：业务层只依赖这一层，不直接依赖 MySQL 或 SQLite 的 API
set(dbapi_srcs
dbapi/Field.cpp
dbapi/QueryResult.cpp
dbapi/DbConfig.cpp
dbapi/DbFactory.cpp
dbapi/SqlDialect.cpp
)

set(mysqlapi_srcs
mysqlapi/DatabaseMysql.cpp
mysqlapi/MysqlQueryResult.cpp
)

# 本地 SQLite 后端。sqlite3/ 下是官方 amalgamation（3.53.1），单文件编译
set(sqliteapi_srcs
sqlite3/sqlite3.c
sqliteapi/DatabaseSqlite.cpp
sqliteapi/SqliteQueryResult.cpp
sqliteapi/SqliteFunctions.cpp
sqliteapi/SqliteSqlTranslator.cpp
sqliteapi/SqliteSchema.cpp
sqliteapi/SqliteManager.cpp
)

set(mysqlmgr_srcs
mysqlmgr/MysqlManager.cpp
mysqlmgr/MysqlThrd.cpp
mysqlmgr/MysqlThrdMgr.cpp
mysqlmgr/TaskList.cpp
)

# 关掉 MySQL 后端时，这两组源码连同 libmysqlclient 一起从构建里摘掉
if (NOT WITH_MYSQL)
    set(mysqlapi_srcs "")
    set(mysqlmgr_srcs "")
endif()

set(json_srcs
jsoncpp1.9.0/json_reader.cpp
jsoncpp1.9.0/json_value.cpp
jsoncpp1.9.0/json_writer.cpp
)

set(zlib_srcs
zlib1.2.11/zutil.c
zlib1.2.11/uncompr.c
zlib1.2.11/trees.c
zlib1.2.11/inftrees.c
zlib1.2.11/inflate.c
zlib1.2.11/inffast.c
zlib1.2.11/infback.c
zlib1.2.11/gzwrite.c
zlib1.2.11/gzread.c
zlib1.2.11/gzlib.c
zlib1.2.11/gzclose.c
zlib1.2.11/deflate.c
zlib1.2.11/crc32.c
zlib1.2.11/compress.c
zlib1.2.11/adler32.c
zlib1.2.11/ZlibUtil.cpp
)

# zlib 的 gz* 实现要由 configure 定义 HAVE_UNISTD_H，才会去 include <unistd.h>。
# 仓库里的 zlib 是直接放进来的源码、没有跑过 configure，于是 read / write / close / lseek
# 在 Linux 下全是隐式声明，编译器一律按返回 int 处理 —— lseek 返回的 off_t 会被截断成
# 32 位，与本文件开头定义 _FILE_OFFSET_BITS=64 的意图正好相反。
# 这里只给 zlib 的源文件补上这个宏，不去改第三方源码本身；Windows 下 gzguts.h 走的是
# <io.h> 分支，不受影响。
set_source_files_properties(${zlib_srcs} PROPERTIES COMPILE_DEFINITIONS HAVE_UNISTD_H)

set(utils_srcs
utils/StringUtil.cpp
utils/URLEncodeUtil.cpp
utils/MD5.cpp
utils/FileToken.cpp
utils/RelayToken.cpp
utils/DaemonRun.cpp
# 把配置里的相对 dbpath 解析到可执行文件所在目录，见 chatserversrc/main.cpp
utils/AppPath.cpp
# 配置文件定位与必需配置项校验，三个服务的 main.cpp 共用
utils/ServerConfigLoader.cpp
# 升级包目录的枚举与 MD5 计算，fileserver 的索引与 chatserver 的扫描器共用
utils/FileEnum.cpp
)

set(chatserver_srcs
chatserversrc/main.cpp
chatserversrc/ChatServer.cpp
chatserversrc/ChatSession.cpp
chatserversrc/UserManager.cpp
chatserversrc/TcpSession.cpp
chatserversrc/MonitorSession.cpp
chatserversrc/MonitorServer.cpp
chatserversrc/HttpSession.cpp
chatserversrc/HttpServer.cpp
chatserversrc/BussinessLogic.cpp
chatserversrc/DepartmentPath.cpp
chatserversrc/DepartmentMsg.cpp
chatserversrc/DepartmentManager.cpp
chatserversrc/AdminManager.cpp
chatserversrc/EnterpriseManager.cpp
chatserversrc/EmployeeManager.cpp
chatserversrc/AnnouncementManager.cpp
chatserversrc/AnnouncementProtocol.cpp
chatserversrc/ServiceAccountManager.cpp
chatserversrc/MessageManager.cpp
chatserversrc/MessagePinManager.cpp
chatserversrc/GroupManager.cpp
chatserversrc/CardManager.cpp)

set(fileserver_srcs
fileserversrc/main.cpp
fileserversrc/FileServer.cpp
fileserversrc/FileSession.cpp
fileserversrc/FolderRelayEndpoint.cpp
fileserversrc/FileManager.cpp
fileserversrc/UpdatePackageIndex.cpp
fileserversrc/TcpSession.cpp)

set(imgserver_srcs
imgserversrc/main.cpp
imgserversrc/RelayServer.cpp
imgserversrc/RelaySession.cpp
imgserversrc/RelayHub.cpp
fileserversrc/FileServer.cpp
fileserversrc/FileSession.cpp
fileserversrc/FolderRelayEndpoint.cpp
fileserversrc/FileManager.cpp
fileserversrc/UpdatePackageIndex.cpp
fileserversrc/TcpSession.cpp)


add_executable(chatserver ${net_srcs} ${json_srcs}  ${chatserver_srcs} ${dbapi_srcs} ${mysqlapi_srcs} ${sqliteapi_srcs} ${mysqlmgr_srcs} ${zlib_srcs} ${utils_srcs} chatserversrc/LoginProtocol.cpp utils/PasswordHasher.cpp utils/Sha256.cpp chatserversrc/ContactsProtocol.cpp chatserversrc/FavoriteProtocol.cpp chatserversrc/FavoriteManager.cpp chatserversrc/StarredProtocol.cpp chatserversrc/StarredManager.cpp chatserversrc/ContactRemarkManager.cpp chatserversrc/ContactTagProtocol.cpp chatserversrc/ContactTagManager.cpp chatserversrc/UpdateProtocol.cpp chatserversrc/ClientVersionManager.cpp chatserversrc/UpdatePackageScanner.cpp chatserversrc/ProfileProtocol.cpp chatserversrc/RegisterProtocol.cpp chatserversrc/RegisterService.cpp chatserversrc/AccountName.cpp chatserversrc/SmsCodeManager.cpp chatserversrc/FriendProtocol.cpp chatserversrc/MessageProtocol.cpp chatserversrc/GroupProtocol.cpp chatserversrc/GroupPermission.cpp chatserversrc/CardProtocol.cpp chatserversrc/FileProtocol.cpp chatserversrc/FileStoreManager.cpp chatserversrc/GroupFileProtocol.cpp chatserversrc/GroupFileManager.cpp chatserversrc/RemoteDesktopProtocol.cpp chatserversrc/RemoteDesktopManager.cpp chatserversrc/CallProtocol.cpp chatserversrc/CallManager.cpp chatserversrc/FolderTransferManager.cpp chatserversrc/FolderTransferProtocol.cpp)
#光包含库目录是没用的，还必须使用TARGET_LINK_LIBRARIES链接该库
#dl / pthread 是 sqlite3 amalgamation 在 Linux 上的依赖：前者用于动态加载扩展，
#后者用于 SQLITE_THREADSAFE 的互斥实现。sqlite3.c 走 C 编译器，拿不到 CMAKE_CXX_FLAGS
#里的 -pthread，所以这里显式链上，不依赖链接阶段的传递
if (WITH_MYSQL)
    TARGET_LINK_LIBRARIES(chatserver ${MYSQL_LIB} dl pthread ${LIBSODIUM_LIB})
else()
    TARGET_LINK_LIBRARIES(chatserver dl pthread ${LIBSODIUM_LIB})
endif()

# fileserver / imgserver 自 v2 起需 jsoncpp 解析附件描述符
add_executable(fileserver ${net_srcs} ${json_srcs} ${fileserver_srcs} ${utils_srcs} ${zlib_srcs})
TARGET_LINK_LIBRARIES(fileserver ${LIBSODIUM_LIB})

add_executable(imgserver ${net_srcs} ${json_srcs} ${imgserver_srcs} ${utils_srcs} ${zlib_srcs})
TARGET_LINK_LIBRARIES(imgserver ${LIBSODIUM_LIB})
# ===== unit tests (doctest) =====
# Standalone test runner for the enterprise org-structure pure-logic modules.
# Independent of the chatserver / fileserver / imgserver binaries.
set(test_srcs
tests/test_main.cpp
tests/test_department_path.cpp
tests/test_department_msg.cpp
tests/test_framecodec.cpp
# 帧压缩往返用例（Suite FC）。windowsproj/Tests.vcxproj 一直列着它，只有这里漏了，
# 于是 FC1 / FC2 两条用例在 Linux 上从未被编译进测试程序、也就从未运行过。
# 它依赖的实现（net/FrameCodec.cpp 与 zlib1.2.11/ZlibUtil.cpp）本来就在清单里，
# 补上这一行即可，不需要额外添加实现文件。
tests/test_framecompress.cpp
tests/test_passwordhasher.cpp
tests/test_loginprotocol.cpp
tests/test_contactsprotocol.cpp
tests/test_favoriteprotocol.cpp
tests/test_starredprotocol.cpp
tests/test_starredmanager.cpp
tests/test_conversationbench.cpp
tests/test_conversationunread.cpp
tests/test_conversationbriefs.cpp
# 测试用的数据库后端切换（默认 SQLite，FLAMINGO_TEST_MYSQL=1 时跑 MySQL）
tests/test_dbbackend.cpp
tests/test_profileprotocol.cpp
tests/test_registerprotocol.cpp
tests/test_registerservice.cpp
tests/test_accountname.cpp
tests/test_friendprotocol.cpp
tests/test_sha256.cpp
tests/test_contactremarkmanager.cpp
tests/test_contacttagprotocol.cpp
tests/test_contacttagmanager.cpp
tests/test_adminbootstrap.cpp
tests/test_apppath.cpp
# 两份 v2 协议头（服务端 chatserversrc/Protocol.h 与客户端 Source/net/Protocol.h）
# 的一致性守卫（Suite PP）。它把两个头文件当纯文本读进来比对，不链接任何实现文件，
# 只用到 utils/AppPath.cpp 里的路径工具，而后者已在下方的实现清单中
tests/test_protocolparity.cpp
tests/test_serverconfigloader.cpp
# 网络 I/O 工作线程数配置项的取值判定用例（Suite IOT）。被测的两个函数就在
# utils/ServerConfigLoader.cpp 里，而该文件已在下方的实现清单中，不需要再补实现文件
tests/test_iothreadcount.cpp
tests/test_asynclog.cpp
tests/test_messageprotocol.cpp
tests/test_messagepinmanager.cpp
tests/test_groupprotocol.cpp
tests/test_grouppermission.cpp
tests/test_cardprotocol.cpp
tests/test_filetoken.cpp
tests/test_relaytoken.cpp
tests/test_filesessionremoval.cpp
tests/test_folderrelayhub.cpp
tests/test_remotedesktopprotocol.cpp
tests/test_callprotocol.cpp
tests/test_callmanager.cpp
tests/test_fileprotocol.cpp
tests/test_groupfileprotocol.cpp
tests/test_sqlite_translator.cpp
tests/test_sqlite_database.cpp
tests/test_sqlite_schema.cpp
tests/test_sqldialect.cpp
# 事务接口（Suite DBT）与断线重试策略（Suite DRP）的用例。
# DRP 那一套只依赖 dbapi/DbRetryPolicy.h 里的纯函数，不需要链接 MySQL 客户端库，
# 所以带 FLAMINGO_NO_MYSQL 编出来的默认测试目标也能跑
tests/test_dbtransaction.cpp
tests/test_announcementprotocol.cpp
tests/test_updateprotocol.cpp
tests/test_clientversionmanager.cpp
tests/test_updatepackageindex.cpp
tests/test_updatepackagescanner.cpp
# 会话发送路径的线程归属用例（Suite SSA）。它是端到端复现：真的起一个 EventLoop 线程、
# 真的建一条 127.0.0.1 上的 TCP 连接、真的走完三帧协议加密握手，故要把 reactor 与被测的
# 发送漏斗 chatserversrc/TcpSession.cpp 一并编进测试程序（都在下方的实现清单里）。
# 连接由用例自己 accept 出来后交给 TcpConnection，不经 TcpServer / Acceptor /
# EventLoopThreadPool，那三个因此不必编进来
tests/test_sendthreadaffinity.cpp
# Poller 事件位口径的用例（Suite PRV）。同样是端到端复现，用的是下面这批 reactor 实现文件，
# 不需要再额外补实现文件。缺陷本身只在 Windows 的 SelectPoller 上成立，Linux 走 epoll
# 不会复现，这里编进来是为了守住「两个平台的判定口径一致」
tests/test_pollerrevents.cpp
chatserversrc/TcpSession.cpp
net/ByteBuffer.cpp
net/Channel.cpp
net/EpollPoller.cpp
net/EventLoop.cpp
net/EventLoopThread.cpp
net/InetAddress.cpp
net/Poller.cpp
net/PollPoller.cpp
net/SelectPoller.cpp
net/Sockets.cpp
net/TcpConnection.cpp
net/Timer.cpp
net/TimerQueue.cpp
base/Timestamp.cpp
chatserversrc/DepartmentPath.cpp
chatserversrc/DepartmentMsg.cpp
chatserversrc/LoginProtocol.cpp
chatserversrc/ContactsProtocol.cpp
# tests/test_favoriteprotocol.cpp 用到的实现。Windows 的 windowsproj/Tests.vcxproj 一直列着它，
# 只有这里漏了，导致 Linux 下单独构建 flamingo_tests 目标时报
# parseSetFavoriteRequest / buildSetFavoriteResponse 未定义
chatserversrc/FavoriteProtocol.cpp
# tests/test_starredprotocol.cpp 与 tests/test_starredmanager.cpp 用到的实现。
# StarredManager 走 DbFactory 取连接，测试里对着临时 .db 文件真的读写 t_starred
chatserversrc/StarredProtocol.cpp
chatserversrc/StarredManager.cpp
chatserversrc/ProfileProtocol.cpp
chatserversrc/RegisterProtocol.cpp
chatserversrc/RegisterService.cpp
chatserversrc/AccountName.cpp
chatserversrc/SmsCodeManager.cpp
chatserversrc/FriendProtocol.cpp
# ContactRemarkManager 走 DbFactory 取连接，测试里对着临时 .db 文件真的读写 t_contact_remark
chatserversrc/ContactRemarkManager.cpp
# tests/test_contacttagprotocol.cpp 与 tests/test_contacttagmanager.cpp 用到的实现。
# ContactTagManager 同样走 DbFactory，测试里对着临时 .db 文件真的读写 t_contact_tag
chatserversrc/ContactTagProtocol.cpp
chatserversrc/ContactTagManager.cpp
# AdminManager 同样走 DbFactory，ADB 套用例对着临时 .db 文件真的读写 t_admin
chatserversrc/AdminManager.cpp
# MessageManager 走 DbFactory 取连接，CVB 套计时用例对着临时 .db 文件真的建会话、灌消息
chatserversrc/MessageManager.cpp
chatserversrc/MessageProtocol.cpp
chatserversrc/MessagePinManager.cpp
chatserversrc/GroupProtocol.cpp
chatserversrc/GroupPermission.cpp
chatserversrc/CardProtocol.cpp
chatserversrc/FileProtocol.cpp
chatserversrc/GroupFileProtocol.cpp
chatserversrc/RemoteDesktopProtocol.cpp
chatserversrc/RemoteDesktopManager.cpp
chatserversrc/CallProtocol.cpp
chatserversrc/CallManager.cpp
tests/test_foldertransfermanager.cpp
chatserversrc/FolderTransferManager.cpp
tests/test_foldertransferprotocol.cpp
chatserversrc/FolderTransferProtocol.cpp
chatserversrc/AnnouncementProtocol.cpp
chatserversrc/UpdateProtocol.cpp
# ClientVersionManager 经 DbFactory 取连接，CVM 套用例对着临时 .db 文件真的读 t_client_version
chatserversrc/ClientVersionManager.cpp
# UPI 套用例验的是 fileserver 侧的升级包目录索引
fileserversrc/UpdatePackageIndex.cpp
chatserversrc/UpdatePackageScanner.cpp
utils/FileEnum.cpp
net/FrameCodec.cpp
# ServerConfigLoader 的 InitProtocolEncryption 依赖 FrameCrypto；
# FramePayload 与 FrameCodec 同属帧处理模块，一并编进来（它依赖下面的 zlib）
net/FrameCrypto.cpp
net/FramePayload.cpp
utils/PasswordHasher.cpp
utils/MD5.cpp
# 管理端建员工 / 重置口令时由服务端自己算 SHA256(明文)，故测试程序也要编进来
utils/Sha256.cpp
utils/FileToken.cpp
utils/RelayToken.cpp
# tests/test_apppath.cpp 用到的实现
utils/AppPath.cpp
# tests/test_serverconfigloader.cpp 用到的实现。ServerConfigLoader 依赖 CConfigFileReader，
# 后者原先没进测试程序，故一并加进来
utils/ServerConfigLoader.cpp
base/ConfigFileReader.cpp
# SQLite 驱动的集成测试要真的打开一个临时 .db 文件，因此要把驱动本体与 amalgamation 一起编进来。
# 驱动内部会调用 LOGI / LOGE，所以还要带上 AsyncLog（未 init 时日志只是进内存队列，不影响测试）
sqlite3/sqlite3.c
sqliteapi/DatabaseSqlite.cpp
sqliteapi/SqliteQueryResult.cpp
sqliteapi/SqliteFunctions.cpp
sqliteapi/SqliteSqlTranslator.cpp
sqliteapi/SqliteSchema.cpp
sqliteapi/SqliteManager.cpp
dbapi/Field.cpp
dbapi/QueryResult.cpp
dbapi/DbConfig.cpp
# MessagePinManager 经 DbFactory 取连接，故测试程序里也要编进工厂本体
dbapi/DbFactory.cpp
dbapi/SqlDialect.cpp
base/AsyncLog.cpp
base/Platform.cpp
)
add_executable(flamingo_tests ${test_srcs} ${json_srcs} ${zlib_srcs})
# 测试程序只跑 SQLite（MPM 套用临时 .db 文件），不需要也不该拖上 libmysqlclient。
# 这里单独给测试目标定义 FLAMINGO_NO_MYSQL，使 DbFactory 编出不含 MySQL 分支的版本 ——
# 与全局的 WITH_MYSQL 开关互不影响，主程序仍照常带 MySQL 后端。
target_compile_definitions(flamingo_tests PRIVATE FLAMINGO_NO_MYSQL)
#理由同 chatserver：测试程序里编进了 sqlite3 amalgamation
TARGET_LINK_LIBRARIES(flamingo_tests dl pthread ${LIBSODIUM_LIB})

# 第二个测试目标：带 MySQL 后端的同一套用例。
#
# 为什么要单独一个目标：上面那个 flamingo_tests 特意定义了 FLAMINGO_NO_MYSQL，
# DbFactory 里连 MySQL 分支都没编进去，无论怎么设环境变量都连不上 MySQL。而产品是
# 两种后端都支持的，两边的差异又不只是语法（InnoDB 的二级索引自带主键值、SQLite 不带，
# 索引需求因此完全不同），只测一边等于没测另一边。
#
# 跑法（需要一个可连的 MySQL，参数见 tests/test_dbbackend.h）：
#     FLAMINGO_TEST_MYSQL=1 ./build-mysql/flamingo_tests_mysql --test-case="CVU*,CVE*"
# 不设 FLAMINGO_TEST_MYSQL 时它跑的仍是 SQLite，与 flamingo_tests 等价。
if (WITH_MYSQL)
    add_executable(flamingo_tests_mysql ${test_srcs} ${json_srcs} ${mysqlapi_srcs}
                   ${zlib_srcs} mysqlmgr/MysqlManager.cpp)
    TARGET_LINK_LIBRARIES(flamingo_tests_mysql dl pthread ${MYSQL_LIB} ${LIBSODIUM_LIB})
endif()