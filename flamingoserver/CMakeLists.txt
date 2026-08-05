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

if (WITH_MYSQL)
    # 推荐使用 CentOS 7.0 及以上系统
    # CentOS 7.0 及以上系统默认安装的是mariadb，安装方法
    # yum install mariadb-server mariadb mariadb-devel
    # CentOS 7.0 以下系统默认安装mysql，安装方法：
    # yum install mysql-server mysql mysql-devel
    find_library(MYSQL_LIB libmysqlclient.so /usr/lib64/mysql/)
    #如果你的数据库是mariadb，你还需要安装mariadb-devel库；如果你的数据库是mysql，你需要安装mysql-devel，否则会出现如下错误提示。
    IF (NOT MYSQL_LIB)
        MESSAGE(FATAL_ERROR "mysqlclient not found. If your mysql is mariadb, please install mariadb-devel otherwise install mysql-devel.")
    ENDIF(NOT MYSQL_LIB)
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

set(utils_srcs
utils/StringUtil.cpp
utils/URLEncodeUtil.cpp
utils/MD5.cpp
utils/FileToken.cpp
utils/RelayToken.cpp
utils/DaemonRun.cpp
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
fileserversrc/FileManager.cpp
fileserversrc/TcpSession.cpp)

set(imgserver_srcs
imgserversrc/main.cpp
imgserversrc/RelayServer.cpp
imgserversrc/RelaySession.cpp
imgserversrc/RelayHub.cpp
fileserversrc/FileServer.cpp
fileserversrc/FileSession.cpp
fileserversrc/FileManager.cpp
fileserversrc/TcpSession.cpp)


add_executable(chatserver ${net_srcs} ${json_srcs}  ${chatserver_srcs} ${dbapi_srcs} ${mysqlapi_srcs} ${sqliteapi_srcs} ${mysqlmgr_srcs} ${zlib_srcs} ${utils_srcs} chatserversrc/LoginProtocol.cpp utils/PasswordHasher.cpp chatserversrc/ContactsProtocol.cpp chatserversrc/FavoriteProtocol.cpp chatserversrc/FavoriteManager.cpp chatserversrc/ProfileProtocol.cpp chatserversrc/RegisterProtocol.cpp chatserversrc/RegisterService.cpp chatserversrc/AccountName.cpp chatserversrc/SmsCodeManager.cpp chatserversrc/FriendProtocol.cpp chatserversrc/MessageProtocol.cpp chatserversrc/GroupProtocol.cpp chatserversrc/GroupPermission.cpp chatserversrc/CardProtocol.cpp chatserversrc/FileProtocol.cpp chatserversrc/FileStoreManager.cpp chatserversrc/GroupFileProtocol.cpp chatserversrc/GroupFileManager.cpp chatserversrc/RemoteDesktopProtocol.cpp chatserversrc/RemoteDesktopManager.cpp chatserversrc/CallProtocol.cpp chatserversrc/CallManager.cpp)
#光包含库目录是没用的，还必须使用TARGET_LINK_LIBRARIES链接该库
#dl / pthread 是 sqlite3 amalgamation 在 Linux 上的依赖：前者用于动态加载扩展，
#后者用于 SQLITE_THREADSAFE 的互斥实现。sqlite3.c 走 C 编译器，拿不到 CMAKE_CXX_FLAGS
#里的 -pthread，所以这里显式链上，不依赖链接阶段的传递
if (WITH_MYSQL)
    TARGET_LINK_LIBRARIES(chatserver mysqlclient dl pthread)
else()
    TARGET_LINK_LIBRARIES(chatserver dl pthread)
endif()

# fileserver / imgserver 自 v2 起需 jsoncpp 解析附件描述符
add_executable(fileserver ${net_srcs} ${json_srcs} ${fileserver_srcs} ${utils_srcs})
TARGET_LINK_LIBRARIES(fileserver)

add_executable(imgserver ${net_srcs} ${json_srcs} ${imgserver_srcs} ${utils_srcs})
TARGET_LINK_LIBRARIES(imgserver)
# ===== unit tests (doctest) =====
# Standalone test runner for the enterprise org-structure pure-logic modules.
# Independent of the chatserver / fileserver / imgserver binaries.
set(test_srcs
tests/test_main.cpp
tests/test_department_path.cpp
tests/test_department_msg.cpp
tests/test_framecodec.cpp
tests/test_passwordhasher.cpp
tests/test_loginprotocol.cpp
tests/test_contactsprotocol.cpp
tests/test_favoriteprotocol.cpp
tests/test_profileprotocol.cpp
tests/test_registerprotocol.cpp
tests/test_registerservice.cpp
tests/test_accountname.cpp
tests/test_friendprotocol.cpp
tests/test_messageprotocol.cpp
tests/test_messagepinmanager.cpp
tests/test_groupprotocol.cpp
tests/test_grouppermission.cpp
tests/test_cardprotocol.cpp
tests/test_filetoken.cpp
tests/test_relaytoken.cpp
tests/test_remotedesktopprotocol.cpp
tests/test_callprotocol.cpp
tests/test_callmanager.cpp
tests/test_fileprotocol.cpp
tests/test_groupfileprotocol.cpp
tests/test_sqlite_translator.cpp
tests/test_sqlite_database.cpp
tests/test_sqlite_schema.cpp
tests/test_sqldialect.cpp
tests/test_announcementprotocol.cpp
chatserversrc/DepartmentPath.cpp
chatserversrc/DepartmentMsg.cpp
chatserversrc/LoginProtocol.cpp
chatserversrc/ContactsProtocol.cpp
chatserversrc/ProfileProtocol.cpp
chatserversrc/RegisterProtocol.cpp
chatserversrc/RegisterService.cpp
chatserversrc/AccountName.cpp
chatserversrc/SmsCodeManager.cpp
chatserversrc/FriendProtocol.cpp
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
chatserversrc/AnnouncementProtocol.cpp
net/FrameCodec.cpp
utils/PasswordHasher.cpp
utils/MD5.cpp
utils/FileToken.cpp
utils/RelayToken.cpp
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
add_executable(flamingo_tests ${test_srcs} ${json_srcs})
# 测试程序只跑 SQLite（MPM 套用临时 .db 文件），不需要也不该拖上 libmysqlclient。
# 这里单独给测试目标定义 FLAMINGO_NO_MYSQL，使 DbFactory 编出不含 MySQL 分支的版本 ——
# 与全局的 WITH_MYSQL 开关互不影响，主程序仍照常带 MySQL 后端。
target_compile_definitions(flamingo_tests PRIVATE FLAMINGO_NO_MYSQL)
#理由同 chatserver：测试程序里编进了 sqlite3 amalgamation
TARGET_LINK_LIBRARIES(flamingo_tests dl pthread)