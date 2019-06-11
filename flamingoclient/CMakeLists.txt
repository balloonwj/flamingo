cmake_minimum_required(VERSION 3.0)
project(FlamingGoClient)
include_directories(./Source/wtl9.0/)
include_directories(./Source/)
ADD_DEFINITIONS(-D_AFXDLL)
 
# 在共享库中使用MFC
SET(CMAKE_MFC_FLAG 2)
add_link_options("/SUBSYSTEM:WINDOWS")
add_definitions(-D_UNICODE)
add_definitions(-DUNICODE)
link_directories(./Source/lib/)
SET(FLAMING_GO_CLIENT_SOURCE 
./Source/AboutDlg.h ./Source/AboutDlg.cpp
./Source/AddFriendConfirmDlg.h ./Source/AddFriendConfirmDlg.cpp
./Source/BuddyChatDlg.h ./Source/BuddyChatDlg.cpp
./Source/BuddyInfoDlg.h ./Source/BuddyInfoDlg.cpp
./Source/BuddyInfoFloatWnd.h ./Source/BuddyInfoFloatWnd.cpp
./Source/BuddyListCtrl.h ./Source/BuddyListCtrl.cpp
#./Source/BuddyListCtrlEx.h ./Source/BuddyListCtrlEx.cpp
./Source/Buffer.h ./Source/Buffer.cpp
./Source/CascadeWinManager.h ./Source/CascadeWinManager.cpp
./Source/ChangePicHead.h ./Source/ChangePicHead.cpp
./Source/ChangePicHeadDlg.h ./Source/ChangePicHeadDlg.cpp
./Source/ChatDlgCommon.h ./Source/ChatDlgCommon.cpp
./Source/ChatTabMgr.h ./Source/ChatTabMgr.cpp
./Source/CheckNetworkStatusTask.h ./Source/CheckNetworkStatusTask.cpp
./Source/ClosePromptDlg.h ./Source/ClosePromptDlg.cpp
./Source/Crash.cpp
./Source/CreateNewGroupDlg.h ./Source/CreateNewGroupDlg.cpp
#./Source/CRegisterSkinHyperLink.h ./Source/CRegisterSkinHyperLink.cpp
./Source/CriticalSection.h ./Source/CriticalSection.cpp
./Source/CriticalSection.h ./Source/ChangePicHeadDlg.cpp
./Source/CustomMsgDef.h
./Source/DstVersion.h
./Source/EncodingUtil.h ./Source/EncodingUtil.cpp
./Source/ChangePicHeadDlg.h ./Source/ChangePicHeadDlg.cpp
./Source/FaceCtrl.h ./Source/FaceCtrl.cpp
./Source/FaceList.h ./Source/FaceList.cpp
./Source/FaceSelDlg.h ./Source/FaceSelDlg.cpp
./Source/File.h ./Source/File.cpp
./Source/File2.h ./Source/File2.cpp
./Source/FileTaskThread.h ./Source/FileTaskThread.cpp
./Source/FileTransferCtrl.h ./Source/FileTransferCtrl.cpp
./Source/FindFriendDlg.h ./Source/FindFriendDlg.cpp
./Source/FlamingoClient.h ./Source/FlamingoClient.cpp
./Source/FontSelDlg.h ./Source/FontSelDlg.cpp
./Source/GDIFactory.h ./Source/GDIFactory.cpp
./Source/GifImage.h ./Source/GifImage.cpp
./Source/GroupChatDlg.h ./Source/GroupChatDlg.cpp
./Source/GroupInfoDlg.h ./Source/GroupInfoDlg.cpp
#./Source/GroupListCtrl.h ./Source/GroupListCtrl.cpp
./Source/HeartbeatTask.h ./Source/HeartbeatTask.cpp
./Source/HideWindow.h ./Source/HideWindow.cpp
./Source/IImageOle.h
./Source/ImageEx.h ./Source/ImageEx.cpp
./Source/ImageTaskThread.h ./Source/ImageTaskThread.cpp
./Source/IniFile.h ./Source/IniFile.cpp
./Source/IULog.h ./Source/IULog.cpp
./Source/LoginAccountList.h ./Source/LoginAccountList.cpp 
./Source/LoginDlg.h ./Source/LoginDlg.cpp 
./Source/LoginSettingsDlg.h ./Source/LoginSettingsDlg.cpp 
./Source/LogonUserInfoDlg.h ./Source/LogonUserInfoDlg.cpp 
./Source/MainDlg.h ./Source/MainDlg.cpp
./Source/MD5.h ./Source/MD5.cpp 
./Source/MD5Sum.h ./Source/MD5Sum.cpp  
./Source/MessageLogger.h ./Source/MessageLogger.cpp
./Source/MiniBuffer.h ./Source/MinuBuffer.cpp 
./Source/MobileStatusDlg.h ./Source/MobileStatusDlg.cpp 
./Source/ModifyMarkNameDlg.h ./Source/ModifyMarkNameDlg.cpp
./Source/ModifyPasswordDlg.h ./Source/ModifyPasswordDlg.cpp
./Source/MsgTipDlg.h ./Source/MsgTipDlg.cpp
./Source/MultiChatDlg.h ./Source/MultiChatDlg.cpp
./Source/MultiChatMemberSelectionDlg.h ./Source/MultiChatMemberSelectionDlg.cpp
./Source/NetRecvHandleTask.h ./Source/NetRecvHandleTask.cpp  
./Source/NetSendTask.h ./Source/NetSendTask.cpp  
./Source/Path.h ./Source/Path.cpp  
./Source/PerformanceCounter.h ./Source/PerformanceCounter.cpp  
./Source/PicBarDlg.h ./Source/PicBarDlg.cpp  
./Source/RecentListCtrl.h ./Source/RecentListCtrl.cpp  
./Source/RecvMsgThread.h ./Source/RecvMsgThread.cpp  
./Source/RegisterDialog.h ./Source/RegisterDialog.cpp
./Source/RemoteDesktopDlg.h ./Source/RemoteDesktopDlg.cpp  
./Source/resource.h ./Source/Result.h 
./Source/RichEditUtil.h ./Source/RichEditUtil.cpp  
./Source/SendMsgThread.h ./Source/SendMsgThread.cpp
./Source/SessChatDlg.h  ./Source/SessChatDlg.cpp    
./Source/SplitterCtrl.h  ./Source/SplitterCtrl.cpp   
./Source/SrcVersion.h  ./Source/   
./Source/Startup.h  ./Source/Startup.cpp   
./Source/stdafx.h  ./Source/stdafx.cpp   
./Source/SystemSettingDlg.h  ./Source/SystemSettingDlg.cpp  
./Source/TeamDlg.h  ./Source/TeamDlg.cpp 
./Source/Thread.h  ./Source/Thread.cpp 
./Source/ThreadPool.h  ./Source/ThreadPool.cpp 
./Source/UtilTime.h  ./Source/UtilTime.cpp  
./Source/TrayIcon.h  ./Source/TrayIcon.cpp  
./Source/UIText.h    
./Source/UpdateDlg.h  ./Source/UpdateDlg.cpp  
./Source/Updater.h  ./Source/Updater.cpp 
./Source/UserConfig.h  ./Source/UserConfig.cpp
./Source/UserMgr.h  ./Source/UserMgr.cpp
./Source/UserSessionData.h  ./Source/UserSessionData.cpp
./Source/UserSnapInfoDlg.h  ./Source/UserSnapInfoDlg.cpp
./Source/Utils.h  ./Source/Utils.cpp
./Source/XmlDocument.h  ./Source/XmlDocument.cpp
./Source/ZlibUtil.h  ./Source/ZlibUtil.cpp
./Source/  ./Source/ 
./Source/flamingo.rc)

SET(NET_SOURCE_FILE ./Source/net/Msg.h ./Source/net/FileMsg.h
./Source/net/IUProtocolData.h ./Source/net/IUProtocolData.cpp
./Source/net/IUSocket.h ./Source/net/IUSocket.cpp
./Source/net/protocolstream.h ./Source/net/protocolstream.cpp
)



SET(REMOTE_DESKTOP_SOURCE_FILE ./Source/RemoteDesktop/CursorInfo.h 
./Source/RemoteDesktop/ScreenManager.h 
./Source/RemoteDesktop/ScreenManager.cpp
./Source/RemoteDesktop/ScreenSpy.h
./Source/RemoteDesktop/ScreenSpy.cpp
./Source/RemoteDesktop/until.h
./Source/RemoteDesktop/until.cpp)

add_subdirectory(./Source/jsoncpp-1.9.0)
add_subdirectory(./Source/SkinLib)
add_subdirectory(./Source/zlib1.2.11)
link_libraries(jsoncpp SkinLib zlib RichEd20)
if(CMAKE_CL_64)
    #设置64位的程序目标(可执行程序)输出路径
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/bin/Debug64/)     
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/bin/Release64/) 
else()
    #设置32位的程序目标(可执行程序)输出路径
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/Bin/)     
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/Bin/) 
endif()
add_executable(flamingGoClient ${FLAMING_GO_CLIENT_SOURCE} ${NET_SOURCE_FILE} ${SKIN_LIB_SOURCE_FILE})

add_subdirectory(./CatchScreen)
add_subdirectory(./iAutoUpdate)