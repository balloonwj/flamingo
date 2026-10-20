# 3.12 是随仓库提交的 MariaDB Connector/C 源码（mariadb-connector-c-3.4.9，以 add_subdirectory 引入）
# 要求的最低版本；CentOS 7 一类老系统自带的 cmake 是 2.8，需要另装 cmake3
cmake_minimum_required(VERSION 3.12)

project (FLAMGINGO_SERVER)

# _FILE_OFFSET_BITS=64 让 off_t / fseeko / ftello 在 32 位 Linux 上也是 64 位，
# 使 fileserver 能正确收发超过 2 GiB 的文件（聊天文件与群文件均不限大小）。
# 64 位平台上 off_t 本就是 64 位，这里定义与否行为一致，加上只为跨平台一致。
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -g -Wall -Wno-unused-variable -pthread -D_FILE_OFFSET_BITS=64")

# 优化级别按构建类型区分（2026-10-04）：
#   不指定 CMAKE_BUILD_TYPE（默认）：-O0，单步调试准确，开发时使用，行为与改动前完全一致；
#   -DCMAKE_BUILD_TYPE=Release：C 与 C++ 都用 -O2。仍保留 -g（便于分析崩溃转储），也不定义 NDEBUG ——
#   assert 照常生效，除了速度之外与默认构建没有任何行为差别。
# 为什么要有 Release：未优化时 C 文件（sqlite3.c、zlib）同样是 -O0，SQLite 查询本身就慢。
# 实测（Windows 上同一个拉一页 50 条历史的请求）：Debug 版 175~202 毫秒，Release 版 18~23 毫秒；
# 客户端打开聊天窗到收到历史约 300 毫秒 → 约 155 毫秒。
# 用法：cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release（构建类型记在 CMakeCache 里，之后 make 沿用）
set(CMAKE_C_FLAGS_RELEASE "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
endif()

link_directories(
    ${PROJECT_SOURCE_DIR}/lib
	/usr/lib64/mysql/
)

# MySQL 后端的编译开关。默认编进去，运行时由 etc/chatserver.conf 的 dbtype 决定用哪种后端，
# 一个二进制同时支持 mysql 与 sqlite 两种模式。
# 关掉它（cmake -DWITH_MYSQL=OFF .）可以编出一份不含 MySQL 客户端的纯本地版本，适合只跑
# dbtype=sqlite 的部署：不编译 MariaDB 连接器的源码。OpenSSL 与它无关（机器人的 HTTPS 也要用），见下文。
#
# 没有对应的 WITH_SQLITE 开关：SQLite 是随仓库自带的单文件 amalgamation，不依赖任何外部库，
# 关掉它省不下依赖、只省一点二进制体积，却要在 DbFactory / SqlDialect / main 等处
# 到处加条件编译，不划算。
option(WITH_MYSQL "编译 MySQL 数据库后端" ON)

# gcc 5 以前默认按 gnu90 编译 C 代码，CentOS 7 自带的 gcc 4.8.5 就是这样，要显式指定 C99（CMake 换算成 -std=gnu99）：
# 连接器源码用了 C99 的写法（如在 for 语句里声明循环变量，libmariadb/ma_default.c），按 gnu90 编译直接报错；
# zlib 1.3.2 在 C89 模式下认为没有 vsnprintf，gzprintf 只编成返回错误的空实现，并打出两条 #warning（gzwrite.c）。
# gcc 5 起默认就是 gnu11 或更高，不设置这个变量，编译参数保持原样。作用于之后定义的全部目标里的 C 源文件
# （连接器、flamingo_zlib、sqlite3.c），三者都能按 C99 编译（2026-10-12 在 CentOS 7 容器里实测）
if (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 5.0)
    set(CMAKE_C_STANDARD 99)
endif()

# 协议加密依赖 libsodium（net/FrameCrypto.cpp），三个服务与单元测试都要链它。
# 2026-10-10 起在 cmake 配置阶段从随仓库提交的源码（libsodium-1.0.22，官方发布包裁剪而来，未做改动）
# 编出静态库，不再链接 deps/libsodium 下的预编译库：那份库在 Ubuntu 24.04 上编出，要求 glibc 2.25 以上，
# CentOS 7（glibc 2.17）上链接失败。Windows 仍用 deps/libsodium 下的预编译库与头文件。
# 编译方式见 cmake/FlamingoDeps.cmake 与 docs/服务端依赖库源码编译方案.md 第 11 节。
# 用完整路径链静态库而不是写库名，是为了避免链接器在系统目录里找到另一份 libsodium。
include(${PROJECT_SOURCE_DIR}/cmake/FlamingoDeps.cmake)
flamingo_build_libsodium(${PROJECT_SOURCE_DIR}/libsodium-1.0.22 1.0.22)
include_directories(${LIBSODIUM_INCLUDE_DIR})

# OpenSSL 有两个用户：机器人呼叫外部服务的 HTTPS（utils/HttpsClient.cpp，docs/机器人方案.md 第 10 节 1k）与
# MariaDB 连接器的 TLS。2026-10-10 起默认在 cmake 配置阶段从随仓库提交的源码（openssl-3.5.9，官方发布包裁剪而来，
# 未做改动）编出静态库，编译机不再需要 OpenSSL 的开发包，老系统（CentOS 7 只有 1.0.2k）上也是同一个版本；
# 编译 OpenSSL 需要 Perl 及几个模块，缺失时 cmake 报错并给出安装命令（cmake/FlamingoDeps.cmake）。
# 这一段必须放在 WITH_MYSQL 一段之前：连接器在 OPENSSL_FOUND 已为真时不再自己查找（上游 CMakeLists.txt:306）。
# 需要跟随发行版的安全更新时用 -DWITH_SYSTEM_OPENSSL=ON 改用系统 OpenSSL：行为与 2026-10-10 之前相同，找得到就
# 启用 https，找不到时只支持 http。方案见 docs/服务端依赖库源码编译方案.md 第 10、11 节。
option(WITH_SYSTEM_OPENSSL "使用系统的 OpenSSL，而不是随仓库源码编出的 OpenSSL 3.5.9" OFF)
if (WITH_SYSTEM_OPENSSL)
    # 之前用自带 OpenSSL 配置过的构建目录，缓存里还留着指向 deps-install 的路径，FindOpenSSL 会直接沿用；
    # 先清掉这些缓存，让它重新到系统目录里查找
    string(FIND "${OPENSSL_INCLUDE_DIR}" "${FLAMINGO_DEPS_INSTALL_ROOT}" bundled_pos)
    if (bundled_pos EQUAL 0)
        unset(OPENSSL_INCLUDE_DIR CACHE)
        unset(OPENSSL_SSL_LIBRARY CACHE)
        unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    endif()
    find_package(OpenSSL QUIET)
else()
    flamingo_check_perl_for_openssl()
    flamingo_build_openssl(${PROJECT_SOURCE_DIR}/openssl-3.5.9 3.5.9)
    # 预先把 FindOpenSSL 的三个结果变量写进缓存：find_path / find_library 发现缓存变量已有值就不再查找，
    # 于是 find_package 直接采用自带的这份，不会找到系统里的 OpenSSL（CentOS 7 装了 openssl-devel 时就有一份 1.0.2k）
    set(OPENSSL_INCLUDE_DIR ${FLAMINGO_OPENSSL_INCLUDE_DIR} CACHE PATH "OpenSSL include directory" FORCE)
    set(OPENSSL_SSL_LIBRARY ${FLAMINGO_OPENSSL_SSL_LIB} CACHE FILEPATH "OpenSSL ssl library" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY ${FLAMINGO_OPENSSL_CRYPTO_LIB} CACHE FILEPATH "OpenSSL crypto library" FORCE)
    find_package(OpenSSL REQUIRED)
    # FLAMINGO_BUNDLED_OPENSSL：HttpsClient 据此探测系统根证书包（utils/CaBundleLocator.h）；
    # FLAMINGO_OPENSSL_DIR：编进库里的 OPENSSLDIR，守卫用例 OSL2 用它核对（tests/test_bundleddeps.cpp）
    add_compile_definitions(FLAMINGO_BUNDLED_OPENSSL FLAMINGO_OPENSSL_DIR="${FLAMINGO_OPENSSL_DIR}")
endif()
if (OPENSSL_FOUND)
    add_definitions(-DFLAMINGO_HAS_OPENSSL)
    include_directories(${OPENSSL_INCLUDE_DIR})
    set(HTTPS_LIB ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
    MESSAGE(STATUS "OpenSSL ${OPENSSL_VERSION} (${OPENSSL_SSL_LIBRARY}): bot external calls support https")
else()
    set(HTTPS_LIB "")
    MESSAGE(STATUS "OpenSSL not found: bot external calls support http only")
endif()

if (WITH_MYSQL)
    # MySQL 客户端用随仓库提交的 MariaDB Connector/C 3.4.9 源码现场编译（2026-10-09 起；此前链接的是
    # deps/mariadb-connector 下的预编译库），与 Windows 侧的 windowsproj/mariadbclient.vcxproj 是同一份源码。
    # 方案见 docs/服务端依赖库源码编译方案.md。上游 CMake 以子目录方式引入，EXCLUDE_FROM_ALL 使它只在
    # 被链接时编出 mariadbclient 静态库，动态库 libmariadb、工具 mariadb_config 等其余目标都不编。
    #
    # 编译开关与原预编译库相同：TLS 后端用 OpenSSL（连接器不接受 WITH_SSL=OFF；默认用上文自带的 OpenSSL，
    # WITH_SYSTEM_OPENSSL=ON 时编译机要装 libssl-dev 或 openssl-devel），四个认证插件编成静态，不编单元测试、不用 curl。开关写进缓存并用 FORCE 固定，
    # 避免沿用旧构建目录里残留的其它取值（上游自己把策略 CMP0077 设成了新行为，普通变量其实也能生效，
    # 用缓存只是为了在 CMakeCache.txt 里看得到实际取值）。
    set(WITH_SSL OPENSSL CACHE STRING "MariaDB Connector/C TLS backend" FORCE)
    set(WITH_UNIT_TESTS OFF CACHE BOOL "MariaDB Connector/C unit tests" FORCE)
    set(WITH_CURL OFF CACHE BOOL "MariaDB Connector/C remote io" FORCE)
    set(WITH_MYSQLCOMPAT OFF CACHE BOOL "MariaDB Connector/C mysqlclient compat links" FORCE)
    set(CLIENT_PLUGIN_CACHING_SHA2_PASSWORD STATIC CACHE STRING "" FORCE)
    set(CLIENT_PLUGIN_SHA256_PASSWORD STATIC CACHE STRING "" FORCE)
    set(CLIENT_PLUGIN_DIALOG STATIC CACHE STRING "" FORCE)
    set(CLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD STATIC CACHE STRING "" FORCE)

    # 只有 zlib 一项与原预编译库不同：连接器不编译自带的 zlib，改用下面的 flamingo_zlib（服务端的
    # zlib1.3.2），整个程序里只有一份 zlib。上游的写法是 IF(NOT ZLIB_FOUND) FIND_PACKAGE(ZLIB REQUIRED)，
    # 预先设好这几个变量后不会再去系统里找 zlib；ZLIB_LIBRARY 写的是目标名，上游拿它去链接 mariadbclient。
    set(WITH_EXTERNAL_ZLIB ON CACHE BOOL "MariaDB Connector/C uses an external zlib" FORCE)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/zlib1.3.2)
    set(ZLIB_LIBRARY flamingo_zlib)
    set(ZLIB_LIBRARIES flamingo_zlib)

    # 上游作为子项目被引入时不设置插件的安装目录（上游 cmake/install.cmake 中 IF(NOT IS_SUBPROJECT) 一段），
    # 而它为动态插件（client_ed25519、parsec、zstd；我们不使用，EXCLUDE_FROM_ALL 下也不会编译）生成安装规则时
    # 要用这个目录，配置阶段会报「INSTALL TARGETS given no LIBRARY DESTINATION」。这里给出与上游独立构建时
    # 相同的默认值（上游 install.cmake 的 IF(INSTALL_PLUGINDIR) 分支会采用它），写进 mariadb_version.h 的
    # MARIADB_PLUGINDIR 也因此与独立构建时一致
    set(INSTALL_PLUGINDIR "lib/mariadb/plugin")

    add_subdirectory(mariadb-connector-c-3.4.9 EXCLUDE_FROM_ALL)

    # 我们自己的代码（mysqlapi/）用到的连接器头文件：公共头文件在上游源码的 include 下，
    # 其中 mariadb_version.h 由上游在配置阶段生成到构建目录对应的 include 下
    include_directories(${PROJECT_SOURCE_DIR}/mariadb-connector-c-3.4.9/include
                        ${PROJECT_BINARY_DIR}/mariadb-connector-c-3.4.9/include)

    # mariadbclient 目标自带链接依赖（OpenSSL 的 ssl / crypto、dl、pthread 与 flamingo_zlib，见上游
    # libmariadb/CMakeLists.txt 的 TARGET_LINK_LIBRARIES(mariadbclient ${SYSTEM_LIBS} ...)），
    # 链接它的目标会自动带上，不必再手写
    set(MYSQL_LIB mariadbclient)
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
dbapi/DbHandlePool.cpp
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
zlib1.3.2/zutil.c
zlib1.3.2/uncompr.c
zlib1.3.2/trees.c
zlib1.3.2/inftrees.c
zlib1.3.2/inflate.c
zlib1.3.2/inffast.c
zlib1.3.2/infback.c
zlib1.3.2/gzwrite.c
zlib1.3.2/gzread.c
zlib1.3.2/gzlib.c
zlib1.3.2/gzclose.c
zlib1.3.2/deflate.c
zlib1.3.2/crc32.c
zlib1.3.2/compress.c
zlib1.3.2/adler32.c
zlib1.3.2/ZlibUtil.cpp
)

# zlib 的 gz* 实现要由 configure 定义 HAVE_UNISTD_H，才会去 include <unistd.h>。
# 仓库里的 zlib 是直接放进来的源码、没有跑过 configure，于是 read / write / close / lseek
# 在 Linux 下全是隐式声明，编译器一律按返回 int 处理 —— lseek 返回的 off_t 会被截断成
# 32 位，与本文件开头定义 _FILE_OFFSET_BITS=64 的意图正好相反。
# 这里只给 zlib 的源文件补上这个宏，不去改第三方源码本身；Windows 下 gzguts.h 走的是
# <io.h> 分支，不受影响。
set_source_files_properties(${zlib_srcs} PROPERTIES COMPILE_DEFINITIONS HAVE_UNISTD_H)

# zlib（zlib1.3.2，官方原样文件，外加我们的 ZlibUtil 封装）编成静态库，三个服务与两个测试程序都链接它。
# MariaDB 连接器也用这一份（见上面 WITH_MYSQL 一段），所以它必须是一个目标，而不是各程序各编一遍的源文件。
add_library(flamingo_zlib STATIC ${zlib_srcs})

set(utils_srcs
utils/StringUtil.cpp
utils/URLEncodeUtil.cpp
utils/MD5.cpp
utils/FileToken.cpp
utils/RelayToken.cpp
# 云端录制控制连接的消息格式，chatserver 与 imgserver 共用
utils/CloudRecordCtlMsg.cpp
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
# ChatSession 的备忘与提醒部分（2401~2403 与群成员变化时的推送），声明在 ChatSession.h
chatserversrc/ChatSessionMemo.cpp
# ChatSession 的单聊密聊部分（2601~2609 的分发、发帧与收尾，docs/密聊方案.md），声明在 ChatSession.h
chatserversrc/ChatSessionSecret.cpp
# 单聊密聊的内存会话管理（纯逻辑，测试程序也编进去）
chatserversrc/SecretChatManager.cpp
# ChatSession 的帖子群部分（2701~2720，docs/帖子群方案.md），声明在 ChatSession.h
chatserversrc/ChatSessionThread.cpp
# 帖子群的协议与规则、存储、「正在查看」登记表（测试程序也编进去）
chatserversrc/ThreadProtocol.cpp
chatserversrc/ThreadManager.cpp
chatserversrc/ThreadWatchRegistry.cpp
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
chatserversrc/MessageSearchText.cpp
chatserversrc/GlobalSearchProtocol.cpp
chatserversrc/MessagePinManager.cpp
chatserversrc/MessageDeleteRange.cpp
chatserversrc/GroupManager.cpp
chatserversrc/GroupFollowManager.cpp
chatserversrc/GroupQaProtocol.cpp
chatserversrc/GroupQaManager.cpp
chatserversrc/GroupLinkProtocol.cpp
chatserversrc/GroupLinkManager.cpp
chatserversrc/QaAdminProtocol.cpp
chatserversrc/BotRules.cpp
chatserversrc/BotManager.cpp
chatserversrc/BotEngine.cpp
chatserversrc/BotProtocol.cpp
chatserversrc/BotServerEnv.cpp
chatserversrc/ChatSessionBot.cpp
chatserversrc/HttpRequestParse.cpp
utils/HttpsClient.cpp
utils/CaBundleLocator.cpp
chatserversrc/GroupInviteProtocol.cpp
chatserversrc/GroupInviteManager.cpp
chatserversrc/GroupAutoName.cpp
chatserversrc/SensitiveWordMatcher.cpp
chatserversrc/SensitiveWordManager.cpp
chatserversrc/SensitiveAdminProtocol.cpp
chatserversrc/CardManager.cpp
# 备忘与提醒（2401~2404，见 docs/备忘与提醒方案.md）
chatserversrc/MemoProtocol.cpp
chatserversrc/MemoManager.cpp
# 管理控制台长连接（2501~2504，管理端口，见 docs/FlamingoLink方案.md）
chatserversrc/AdminLinkProtocol.cpp
chatserversrc/AdminLinkSession.cpp
chatserversrc/AdminLinkServer.cpp
# 启动时检查实际下发给客户端的主机地址是否只有本机才能访问（见 utils/ConfigHostCheck.h）
utils/ConfigHostCheck.cpp)

set(fileserver_srcs
fileserversrc/main.cpp
fileserversrc/FileServer.cpp
fileserversrc/FileSession.cpp
fileserversrc/UploadPartPolicy.cpp
fileserversrc/CloudRecordScene.cpp
fileserversrc/FolderRelayEndpoint.cpp
fileserversrc/FileManager.cpp
fileserversrc/UpdatePackageIndex.cpp
fileserversrc/TcpSession.cpp)

set(imgserver_srcs
imgserversrc/main.cpp
imgserversrc/RelayServer.cpp
imgserversrc/RelaySession.cpp
imgserversrc/RelayHub.cpp
imgserversrc/RoomRelayFrame.cpp
imgserversrc/RoomForwardPolicy.cpp
imgserversrc/ScreenShareRoomRelay.cpp
imgserversrc/CloudRecordFile.cpp
imgserversrc/CloudRecorder.cpp
imgserversrc/CloudRecordControl.cpp
imgserversrc/P2PProbeCore.cpp
imgserversrc/P2PAddressProbe.cpp
imgserversrc/RelayHelloPolicy.cpp
fileserversrc/FileServer.cpp
fileserversrc/FileSession.cpp
fileserversrc/UploadPartPolicy.cpp
fileserversrc/CloudRecordScene.cpp
fileserversrc/FolderRelayEndpoint.cpp
fileserversrc/FileManager.cpp
fileserversrc/UpdatePackageIndex.cpp
fileserversrc/TcpSession.cpp)


add_executable(chatserver ${net_srcs} ${json_srcs}  ${chatserver_srcs} ${dbapi_srcs} ${mysqlapi_srcs} ${sqliteapi_srcs} ${mysqlmgr_srcs} ${utils_srcs} chatserversrc/LoginProtocol.cpp utils/PasswordHasher.cpp utils/Sha256.cpp chatserversrc/ContactsProtocol.cpp chatserversrc/FavoriteProtocol.cpp chatserversrc/FavoriteManager.cpp chatserversrc/ContactRemarkManager.cpp chatserversrc/ContactTagProtocol.cpp chatserversrc/ContactTagManager.cpp chatserversrc/UpdateProtocol.cpp chatserversrc/ClientVersionManager.cpp chatserversrc/UpdatePackageScanner.cpp chatserversrc/ProfileProtocol.cpp chatserversrc/RegisterProtocol.cpp chatserversrc/RegisterService.cpp chatserversrc/AccountName.cpp chatserversrc/SmsCodeManager.cpp chatserversrc/FriendProtocol.cpp chatserversrc/MessageProtocol.cpp chatserversrc/GroupProtocol.cpp chatserversrc/GroupPermission.cpp chatserversrc/CardProtocol.cpp chatserversrc/FileProtocol.cpp chatserversrc/FileStoreManager.cpp chatserversrc/GroupFileProtocol.cpp chatserversrc/GroupFileManager.cpp chatserversrc/RemoteDesktopProtocol.cpp chatserversrc/RemoteDesktopManager.cpp chatserversrc/CallProtocol.cpp chatserversrc/CallManager.cpp chatserversrc/CallCardWriter.cpp chatserversrc/FolderTransferManager.cpp chatserversrc/FolderTransferProtocol.cpp chatserversrc/FileShareProtocol.cpp chatserversrc/FileShareSessionManager.cpp chatserversrc/FileShareManager.cpp chatserversrc/FileSharePolicy.cpp chatserversrc/FileShareAdminService.cpp chatserversrc/FileShareAdminProtocol.cpp chatserversrc/FeatureSwitchManager.cpp chatserversrc/ScreenShareProtocol.cpp chatserversrc/ScreenShareManager.cpp chatserversrc/ScreenShareRules.cpp chatserversrc/ScreenShareLinkTable.cpp chatserversrc/CloudRecordLink.cpp chatserversrc/CloudRecordRules.cpp chatserversrc/CloudRecordProtocol.cpp chatserversrc/CloudRecordStore.cpp chatserversrc/CloudRecordService.cpp chatserversrc/CloudRecordServerEnv.cpp chatserversrc/ScreenShareService.cpp chatserversrc/GroupCallManager.cpp chatserversrc/GroupCallProtocol.cpp chatserversrc/GroupCallService.cpp chatserversrc/GroupCallServerEnv.cpp chatserversrc/P2PProtocol.cpp chatserversrc/P2PSessionManager.cpp chatserversrc/P2PSignalService.cpp chatserversrc/P2PServerSetup.cpp)
#光包含库目录是没用的，还必须使用TARGET_LINK_LIBRARIES链接该库
#dl / pthread 是 sqlite3 amalgamation 在 Linux 上的依赖：前者用于动态加载扩展，
#后者用于 SQLITE_THREADSAFE 的互斥实现。sqlite3.c 走 C 编译器，拿不到 CMAKE_CXX_FLAGS
#里的 -pthread，所以这里显式链上，不依赖链接阶段的传递
if (WITH_MYSQL)
    TARGET_LINK_LIBRARIES(chatserver ${MYSQL_LIB} flamingo_zlib ${HTTPS_LIB} dl pthread ${LIBSODIUM_LIB})
else()
    TARGET_LINK_LIBRARIES(chatserver flamingo_zlib ${HTTPS_LIB} dl pthread ${LIBSODIUM_LIB})
endif()

# fileserver / imgserver 自 v2 起需 jsoncpp 解析附件描述符
add_executable(fileserver ${net_srcs} ${json_srcs} ${fileserver_srcs} ${utils_srcs})
TARGET_LINK_LIBRARIES(fileserver flamingo_zlib ${LIBSODIUM_LIB})

add_executable(imgserver ${net_srcs} ${json_srcs} ${imgserver_srcs} ${utils_srcs})
TARGET_LINK_LIBRARIES(imgserver flamingo_zlib ${LIBSODIUM_LIB})
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
# 它依赖的实现（net/FrameCodec.cpp 在清单里，zlib1.3.2/ZlibUtil.cpp 在测试程序链接的 flamingo_zlib 里），
# 补上这一行即可，不需要额外添加实现文件。
tests/test_framecompress.cpp
tests/test_passwordhasher.cpp
tests/test_loginprotocol.cpp
tests/test_contactsprotocol.cpp
tests/test_favoriteprotocol.cpp
tests/test_conversationbench.cpp
tests/test_conversationunread.cpp
tests/test_conversationbriefs.cpp
tests/test_notifymode.cpp
tests/test_markunread.cpp
tests/test_groupfollow.cpp
tests/test_mustread.cpp
tests/test_groupqa.cpp
tests/test_grouplink.cpp
tests/test_bot.cpp
tests/test_botengine.cpp
# 测试用的数据库后端切换（默认 SQLite，FLAMINGO_TEST_MYSQL=1 时跑 MySQL）
tests/test_dbbackend.cpp
tests/test_profileprotocol.cpp
tests/test_registerprotocol.cpp
tests/test_registerservice.cpp
tests/test_accountname.cpp
tests/test_friendprotocol.cpp
tests/test_sha256.cpp
tests/test_contactremarkmanager.cpp
tests/test_userprofilefields.cpp
tests/test_employeegender.cpp
tests/test_adminresetpassword.cpp
tests/test_loginaccount.cpp
tests/test_dashboardstats.cpp
tests/test_announcementrepush.cpp
tests/test_enterpriselistfilter.cpp
tests/test_servicekind.cpp
tests/test_contacttagprotocol.cpp
tests/test_contacttagmanager.cpp
tests/test_adminbootstrap.cpp
tests/test_defaultenterprise.cpp
# 组织架构成员的账号状态 accountstatus（Suite OAS）：同样对着临时 .db 文件跑
# DepartmentManager 与 EmployeeManager，两者的实现已在下方清单中
tests/test_orgaccountstatus.cpp
tests/test_apppath.cpp
# 系统根证书包探测（Suite CAB，utils/CaBundleLocator.cpp 在下方的实现清单中）与随仓库源码编译的
# libsodium / OpenSSL 的守卫用例（Suite SOD / OSL），docs/服务端依赖库源码编译方案.md 第 12 节
tests/test_cabundle.cpp
tests/test_bundleddeps.cpp
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
tests/test_messagedeleterange.cpp
# 转发取源消息的过滤口径（Suite FWD）。被测的 MessageManager 已在下方的实现清单中
tests/test_forwardsource.cpp
# 消息可见范围（Suite AUD）：saveMessage 写范围、各读取路径按范围过滤。被测的 MessageManager 已在下方的实现清单中
tests/test_messageaudience.cpp
tests/test_messagesearchtext.cpp
tests/test_globalsearch.cpp
tests/test_groupprotocol.cpp
tests/test_grouppermission.cpp
tests/test_groupautoname.cpp
# 敏感词匹配引擎：归一化、Aho-Corasick 匹配与内容摘要（Suite SW）
tests/test_sensitiveword.cpp
# 敏感词库管理器：词库增删改查、导入导出、总开关、缓存失效、拦截记录（Suite SWD），对着临时库跑
tests/test_sensitivewordmanager.cpp
# 敏感词管理后台接口的报文编解码：错误码换算、企业范围、数组解析、应答拼装（Suite SWA）
tests/test_sensitiveadminprotocol.cpp
tests/test_pinnedgroups.cpp
# 退群、解散群、转让群主的存储层行为（Suite GX）。被测的 GroupManager 已在下方的实现清单中
tests/test_groupexit.cpp
tests/test_groupjoin.cpp
tests/test_joinrequest.cpp
tests/test_grouphistory.cpp
tests/test_groupinvite.cpp
tests/test_cardprotocol.cpp
# 卡片互动存储层行为（Suite KM）。CardManager 经 DbFactory 对着临时 .db 文件读写 t_card_interaction
tests/test_cardmanager.cpp
tests/test_filetoken.cpp
tests/test_relaytoken.cpp
tests/test_filesessionremoval.cpp
# 上传分片的命名与收尾策略（Suite UPP）
tests/test_uploadpart.cpp
fileserversrc/UploadPartPolicy.cpp
fileserversrc/CloudRecordScene.cpp
tests/test_folderrelayhub.cpp
tests/test_relayrolecheck.cpp
tests/test_remotedesktopprotocol.cpp
tests/test_callprotocol.cpp
tests/test_callmanager.cpp
tests/test_screenshareprotocol.cpp
tests/test_screensharemanager.cpp
tests/test_screensharelink.cpp
tests/test_cloudrecord.cpp
# 云端录制对转发的开销：计时用例（Suite CRB，默认跳过）
tests/test_cloudrecordbench.cpp
tests/test_cloudrecordlink.cpp
tests/test_cloudrecordserver.cpp
tests/test_cloudrecordservice.cpp
tests/test_screenshare_multi.cpp
tests/test_screensharerules.cpp
tests/test_groupcallmanager.cpp
tests/test_groupcallprotocol.cpp
tests/test_groupcallservice.cpp
tests/test_groupcallrelay.cpp
tests/test_relayroomhub.cpp
tests/test_roomrelayframe.cpp
tests/test_roomforwardpolicy.cpp
tests/test_fileprotocol.cpp
tests/test_filestoremanager.cpp
tests/test_groupfileprotocol.cpp
tests/test_groupfilesearch.cpp
tests/test_sqlite_translator.cpp
tests/test_sqlite_database.cpp
tests/test_sqlite_schema.cpp
tests/test_fileshare_schema.cpp
tests/test_sqldialect.cpp
# 事务接口（Suite DBT）与断线重试策略（Suite DRP）的用例。
# DRP 那一套只依赖 dbapi/DbRetryPolicy.h 里的纯函数，不需要链接 MySQL 客户端库，
# 所以带 FLAMINGO_NO_MYSQL 编出来的默认测试目标也能跑
tests/test_dbtransaction.cpp
tests/test_dbhandlepool.cpp
tests/test_mysqlpool.cpp
tests/test_sqlitepool.cpp
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
# 连接关闭 Nagle 的用例（Suite NDL）：一条本机回环连接交给 TcpConnection，用的也是下面这批 reactor 实现文件
tests/test_tcpnodelay.cpp
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
chatserversrc/ProfileProtocol.cpp
chatserversrc/RegisterProtocol.cpp
chatserversrc/RegisterService.cpp
chatserversrc/AccountName.cpp
chatserversrc/UserManager.cpp
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
# tests/test_announcementrepush.cpp 用到的公告管理器：ARP 套用例对着临时 .db 文件发布公告、筛未读受众
chatserversrc/AnnouncementManager.cpp
# tests/test_defaultenterprise.cpp 用到的企业与部门管理器：DE 套用例对着临时 .db 文件
# 建默认企业及其公司根部门
chatserversrc/EnterpriseManager.cpp
chatserversrc/DepartmentManager.cpp
# tests/test_employeegender.cpp 用到的员工管理器：EMG 套用例对着临时 .db 文件新建、编辑员工
chatserversrc/EmployeeManager.cpp
# MessageManager 走 DbFactory 取连接，CVB 套计时用例对着临时 .db 文件真的建会话、灌消息
chatserversrc/MessageManager.cpp
# MessageManager 入库与搜索用到的搜索文本提取，tests/test_messagesearchtext.cpp 也直接测它
chatserversrc/MessageSearchText.cpp
# 全局搜索（cmd_global_search）的报文编解码，tests/test_globalsearch.cpp 直接测它
chatserversrc/GlobalSearchProtocol.cpp
chatserversrc/MessageProtocol.cpp
chatserversrc/MessagePinManager.cpp
# MessageManager / MessagePinManager 读取时按清理区间过滤，test_messagedeleterange.cpp 也直接测它
chatserversrc/MessageDeleteRange.cpp
chatserversrc/GroupProtocol.cpp
chatserversrc/GroupPermission.cpp
# tests/test_groupautoname.cpp 的 GN 组用到的自动群名生成规则
chatserversrc/GroupAutoName.cpp
# tests/test_sensitiveword.cpp 用到的敏感词匹配引擎（纯函数，不碰数据库）
chatserversrc/SensitiveWordMatcher.cpp
# tests/test_sensitivewordmanager.cpp 用到的敏感词库管理器：经 DbFactory 对着临时库读写
chatserversrc/SensitiveWordManager.cpp
# tests/test_sensitiveadminprotocol.cpp 用到的敏感词管理后台接口报文编解码
chatserversrc/SensitiveAdminProtocol.cpp
# tests/test_groupautoname.cpp 的 GM 组用到的群组存储管理器：经 DbFactory 对着临时库跑建群与重算
chatserversrc/GroupManager.cpp
# tests/test_groupfollow.cpp 用到的群内关注名单读写（t_group_follow，docs/消息提醒模式与消息盒子方案.md §12）
chatserversrc/GroupFollowManager.cpp
# tests/test_groupqa.cpp 用到的群问答纯逻辑、读写与管理接口字段校验（docs/群问答方案.md）
chatserversrc/GroupQaProtocol.cpp
chatserversrc/GroupQaManager.cpp
chatserversrc/QaAdminProtocol.cpp
# tests/test_grouplink.cpp 用到的加群链接纯逻辑与 t_group_link 读写（docs/邀请入群方案.md §12）
chatserversrc/GroupLinkProtocol.cpp
chatserversrc/GroupLinkManager.cpp
# tests/test_bot.cpp 用到的机器人纯逻辑与读写（docs/机器人方案.md）
chatserversrc/BotRules.cpp
chatserversrc/BotManager.cpp
chatserversrc/BotEngine.cpp
chatserversrc/BotProtocol.cpp
chatserversrc/HttpRequestParse.cpp
utils/HttpsClient.cpp
utils/CaBundleLocator.cpp
# tests/test_groupinvite.cpp 用到的入群邀请卡片纯逻辑与 t_group_invite 读写（docs/邀请入群方案.md 第 3 期）
chatserversrc/GroupInviteProtocol.cpp
chatserversrc/GroupInviteManager.cpp
chatserversrc/CardProtocol.cpp
# tests/test_cardmanager.cpp 用到的卡片互动存储管理器：经 DbFactory 对着临时库读写 t_card_interaction
chatserversrc/CardManager.cpp
# 备忘与提醒（Suite MEM-S）：报文与规则是纯函数，MemoManager 经 DbFactory 对着临时库读写 t_memo / t_memo_target
tests/test_memo.cpp
chatserversrc/MemoProtocol.cpp
chatserversrc/MemoManager.cpp
# 群备忘卡片（Suite MC-S）与群公告卡片（Suite AC-S）：卡片内容构造是纯函数，存取经 MessageManager 对着临时库跑
tests/test_memocard.cpp
tests/test_announcecard.cpp
# 消息改写离线同步（Suite RW）：改写序号的分配与按序号拉取，经 MessageManager 对着临时库跑
tests/test_rewrittensync.cpp
# 建群与批量加人的写库（Suite GB，2026-10-08 建群卡顿）：成员行写入的正确性与耗时，经 GroupManager 对着临时库跑
tests/test_groupbulk.cpp
# 管理控制台长连接（Suite ADL）：报文与规则是纯函数
tests/test_adminlink.cpp
chatserversrc/AdminLinkProtocol.cpp
chatserversrc/FileProtocol.cpp
# tests/test_filestoremanager.cpp 用到的实现：经 DbFactory 对着临时 .db 文件跑秒传查询
chatserversrc/FileStoreManager.cpp
chatserversrc/GroupFileProtocol.cpp
# tests/test_groupfilesearch.cpp 用到的群文件库管理器：GFS 套用例经 DbFactory 对着临时 .db 文件按文件名搜群文件
chatserversrc/GroupFileManager.cpp
chatserversrc/RemoteDesktopProtocol.cpp
chatserversrc/RemoteDesktopManager.cpp
chatserversrc/CallProtocol.cpp
chatserversrc/CallManager.cpp
chatserversrc/ScreenShareProtocol.cpp
chatserversrc/ScreenShareManager.cpp
chatserversrc/ScreenShareRules.cpp
chatserversrc/ScreenShareLinkTable.cpp
chatserversrc/CloudRecordRules.cpp
chatserversrc/CloudRecordProtocol.cpp
chatserversrc/CloudRecordStore.cpp
chatserversrc/CloudRecordService.cpp
chatserversrc/CloudRecordLink.cpp
chatserversrc/GroupCallManager.cpp
chatserversrc/GroupCallProtocol.cpp
chatserversrc/GroupCallService.cpp
imgserversrc/RoomRelayFrame.cpp
imgserversrc/RoomForwardPolicy.cpp
imgserversrc/CloudRecordFile.cpp
imgserversrc/CloudRecorder.cpp
imgserversrc/CloudRecordControl.cpp
tests/test_foldertransfermanager.cpp
chatserversrc/FolderTransferManager.cpp
tests/test_foldertransferprotocol.cpp
chatserversrc/FolderTransferProtocol.cpp
tests/test_fileshareprotocol.cpp
tests/test_p2pprotocol.cpp
tests/test_p2psessionmanager.cpp
tests/test_p2psignalservice.cpp
# 单聊密聊的会话管理（Suite SC，docs/密聊方案.md 第 8 节）
tests/test_secretchat.cpp
chatserversrc/SecretChatManager.cpp
# 帖子群（Suite TG，docs/帖子群方案.md 第 8 节）
tests/test_threadgroup.cpp
chatserversrc/ThreadProtocol.cpp
chatserversrc/ThreadManager.cpp
chatserversrc/ThreadWatchRegistry.cpp
tests/test_p2pserversetup.cpp
tests/test_udpsockets.cpp
tests/test_p2paddressprobe.cpp
imgserversrc/P2PProbeCore.cpp
tests/test_relayhellopolicy.cpp
imgserversrc/RelayHelloPolicy.cpp
chatserversrc/P2PServerSetup.cpp
# 配置里的主机地址是否只有本机才能访问（Suite HAC）
tests/test_confighostcheck.cpp
utils/ConfigHostCheck.cpp
chatserversrc/P2PSignalService.cpp
chatserversrc/P2PSessionManager.cpp
chatserversrc/P2PProtocol.cpp
chatserversrc/FileShareProtocol.cpp
tests/test_filesharesessionmanager.cpp
chatserversrc/FileShareSessionManager.cpp
tests/test_filesharemanager.cpp
chatserversrc/FileShareManager.cpp
tests/test_filesharepolicy.cpp
chatserversrc/FileSharePolicy.cpp
tests/test_fileshareadmin.cpp
chatserversrc/FileShareAdminProtocol.cpp
tests/test_featureswitch.cpp
chatserversrc/FeatureSwitchManager.cpp
tests/test_filesharecard.cpp
tests/test_fileshareaccess.cpp
tests/test_filesharegroup.cpp
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
utils/CloudRecordCtlMsg.cpp
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
dbapi/DbHandlePool.cpp
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
TARGET_LINK_LIBRARIES(flamingo_tests flamingo_zlib ${HTTPS_LIB} dl pthread ${LIBSODIUM_LIB})

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
    # tests/test_mariadbclient.cpp 是 MariaDB 连接器的守卫用例（Suite MDB），只有这个链接了连接器的测试程序才编它
    add_executable(flamingo_tests_mysql ${test_srcs} ${json_srcs} ${mysqlapi_srcs}
                   mysqlmgr/MysqlManager.cpp tests/test_mariadbclient.cpp)
    TARGET_LINK_LIBRARIES(flamingo_tests_mysql dl pthread ${MYSQL_LIB} flamingo_zlib ${HTTPS_LIB} ${LIBSODIUM_LIB})
endif()