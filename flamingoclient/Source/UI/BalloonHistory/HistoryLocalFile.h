/**
 *  按一条本地库消息记录找出它在本机的可用文件或文件夹路径。
 *
 *  消息记录窗（行右键菜单的「打开」「打开所在文件夹」、导出时附带图片与文件）与主面板搜索
 *  下拉（「文件」标签结果行悬停时的「打开文件」「打开所在文件夹」按钮）共用这一份查找规则：
 *    · 图片：按内容 MD5 换算本地缓存路径；
 *    · 文件：先查文件落点登记表（CFileLocalStore），登记的路径失效再回退下载缓存路径，
 *      优先级交给 balloonchatwnd::ResolveLocalFilePath（单测 Suite CFL）；
 *    · 文件夹传输记录：查文件夹落点登记表（CFolderLocalStore）按会话 id 记下的根目录；
 *    · 其余类型没有本地文件。
 *  每次都实测磁盘：用户随时会把文件移走、改名或删除，登记表里有记录不代表文件还在。
 *
 *  典型用法：
 *      std::wstring path;
 *      if (balloonhistory::ResolveRecordLocalPath(rec, path))
 *          ::ShellExecute(hwnd, _T("open"), path.c_str(), NULL, NULL, SW_SHOWNORMAL);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <string>

#include "V2MsgQuery.h"   // balloonhistory::HistoryRecord

namespace balloonhistory {

/**
 *  取一条消息记录在本机的可用路径。缓存与下载目录按当前登录账号区分（net::CurrentUserAccount），
 *  调用方须在登录之后调用。
 *    rec：消息记录（只读）。
 *    outPath：[出参] 本机完整路径：图片与文件为文件路径，文件夹传输记录为那个文件夹的路径；
 *             返回 false 时被清空。
 *  @return 本机确实有这个文件（或文件夹）时返回 true；消息类型没有本地文件、登记表里查不到、
 *          或登记的路径已经不存在时返回 false。
 */
bool ResolveRecordLocalPath(const HistoryRecord& rec, std::wstring& outPath);

} // namespace balloonhistory
