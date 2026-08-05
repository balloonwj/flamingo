/**
 *  ChatImageSaveAs.h
 *
 *  聊天图片「另存为」的执行部分：弹保存对话框、把本地图片文件按原字节拷到用户
 *  选定位置。单聊窗与群聊窗都要用，故抽成自由函数放在这里，不让两边各抄一份。
 *
 *  与 ChatMessageMenu.h 的分工：那边是纯模型（菜单该有哪几行、默认文件名叫什么），
 *  编进 FlamingoUnit 测试工程跑断言；本文件碰 Win32 对话框与文件系统，不进测试
 *  工程。两者合起来才是完整的「图片另存为」。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#pragma once

#include <string>

namespace balloonchatwnd {

/**
 *  弹保存对话框，把一张本地图片另存到用户选定位置。
 *
 *  按原文件字节整体拷贝 —— 无损、保持原格式（下载缓存里存的就是发送方上传的
 *  原始字节）。拷贝失败时在本函数内弹提示告知用户，调用方只需按返回值记日志。
 *
 *    owner：       模态对话框的属主窗口，可为 NULL。
 *    imagePath：   图片本地文件完整路径；空串直接返回 false，不弹对话框。
 *    defaultName： 对话框里的默认文件名（不含目录），一般由
 *                  MakeImageSaveAsDefaultName 算出；默认扩展名取它的后缀，
 *                  没有后缀时按 png 处理。
 *
 *  @return 确实拷贝成功返回 true；用户取消、路径为空、拷贝失败都返回 false
 *          （三者的区别调用方一般不关心，要区分请看日志）。
 */
bool SaveChatImageAs(HWND owner, const std::wstring& imagePath,
                     const std::wstring& defaultName);

} // namespace balloonchatwnd
