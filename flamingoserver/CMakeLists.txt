cmake_minimum_required(VERSION 2.6)

project (FLAMGINGO_SERVER)

#FXN_VERSION是为网友FXN定制的版本，增加了一些特殊功能，如果您不需要，可以去掉-DFXN_VERSION选项(默认不开启)
#-DFXN_VERSION
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -g -Wall -O0 -Wno-unused-variable -pthread")

link_directories(
    ${PROJECT_SOURCE_DIR}/lib
	/usr/lib64/mysql/
)

find_library(MYSQL_LIB libmysqlclient.so /usr/lib64/mysql/)
IF (NOT MYSQL_LIB)
    MESSAGE(FATAL_ERROR "mysqlclient not found. If your mysql is mariadb, please install mariadb-devel otherwise install mysql-devel.")
ENDIF(NOT MYSQL_LIB)

set(net_srcs 
base/Timestamp.cpp
base/CountDownLatch.cpp
base/FileUtil.cpp
base/Logging.cpp
base/LogFile.cpp
base/LogStream.cpp
base/AsyncLogging.cpp
base/ConfigFileReader.cpp

net/Acceptor.cpp
net/Buffer.cpp
net/Channel.cpp
net/Connector.cpp
net/EpollPoller.cpp
net/EventLoop.cpp
net/InetAddress.cpp
net/Sockets.cpp
net/TcpClient.cpp
net/TcpConnection.cpp
net/TcpServer.cpp
net/EventLoopThread.cpp
net/EventLoopThreadPool.cpp
net/ProtocolStream.cpp
net/Timer.cpp
net/TimerQueue.cpp
)

set(database_srcs
database/DatabaseMysql.cpp
database/Field.cpp
database/QueryResult.cpp
)

set(mysql_srcs
mysql/MysqlManager.cpp
mysql/MysqlThrd.cpp
mysql/MysqlThrdMgr.cpp
mysql/TaskList.cpp
)

set(json_srcs
jsoncpp-0.5.0/json_reader.cpp
jsoncpp-0.5.0/json_value.cpp
jsoncpp-0.5.0/json_writer.cpp
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
utils/DaemonRun.cpp
)

set(chatserver_srcs
chatserversrc/main.cpp
chatserversrc/IMServer.cpp
chatserversrc/ChatSession.cpp
chatserversrc/UserManager.cpp
chatserversrc/MsgCacheManager.cpp
chatserversrc/TcpSession.cpp
chatserversrc/MonitorSession.cpp
chatserversrc/MonitorServer.cpp
chatserversrc/HttpSession.cpp
chatserversrc/HttpServer.cpp
chatserversrc/BussinessLogic.cpp)

set(fileserver_srcs
fileserversrc/main.cpp
fileserversrc/FileServer.cpp
fileserversrc/FileSession.cpp
fileserversrc/FileManager.cpp
fileserversrc/TcpSession.cpp)

set(imgserver_srcs
imgserversrc/main.cpp
fileserversrc/FileServer.cpp
fileserversrc/FileSession.cpp
fileserversrc/FileManager.cpp
fileserversrc/TcpSession.cpp)


add_executable(chatserver ${net_srcs} ${json_srcs}  ${chatserver_srcs} ${mysql_srcs} ${database_srcs} ${zlib_srcs} ${utils_srcs})
#光包含库目录是没用的，还必须使用TARGET_LINK_LIBRARIES链接该库
TARGET_LINK_LIBRARIES(chatserver mysqlclient)

add_executable(fileserver ${net_srcs}  ${fileserver_srcs} ${utils_srcs})
TARGET_LINK_LIBRARIES(fileserver)

add_executable(imgserver ${net_srcs}  ${imgserver_srcs} ${utils_srcs})
TARGET_LINK_LIBRARIES(imgserver)





