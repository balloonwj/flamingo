cmake_minimum_required(VERSION 3.0)
project(CatchScreen)

ADD_DEFINITIONS(-D_AFXDLL)
 
# 在共享库中使用MFC
SET(CMAKE_MFC_FLAG 2)
add_link_options("/SUBSYSTEM:WINDOWS")
add_definitions(-D_UNICODE)
add_definitions(-DUNICODE)
SET(CATCH_SCREEN_SOURCE_FILE 
./CatchScreen.h
./CatchScreen.cpp
CatchScreen.rc
CatchScreenDlg.cpp
CatchScreenDlg.h
EditEx.cpp
EditEx.h
resource.h
StdAfx.cpp
StdAfx.h
TrackerEx.cpp
TrackerEx.h
)

SET(SOURCE_FILE ./res/arrow_m.cur
./res/back_dd.bmp
./res/back.bmp
./res/CatchScreen.ico
./res/CatchScreen.rc2
./res/move_r.cur
./res/size1_r.cur
./res/size2_r.cur
./res/size3_r.cur
./res/size4_r.cur)
add_executable(CatchScreen WIN32 ${CATCH_SCREEN_SOURCE_FILE} ${SOURCE_FILE})