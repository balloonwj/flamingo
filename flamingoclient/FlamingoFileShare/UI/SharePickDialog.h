/**
 *  文件共享 DLL 里弹系统选择框的公共函数，SharePickDialog.h
 *
 *  浏览窗口（下载选目标目录、上传选文件或文件夹）与发起共享对话框（选要共享的文件夹或文件）共用这一份，
 *  基于 IFileOpenDialog。调用线程须已初始化 COM（DLL 的窗口都在 exe 的 UI 线程上，exe 启动时已初始化）。
 *
 *  典型用法：
 *      std::vector<std::wstring> picked;
 *      if (SharePickItems(m_hWnd, true, false, false, L"选择文件夹", picked))
 *          Use(picked[0]);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace fileshare {

/**
 *  弹系统的选文件 / 选文件夹对话框（模态于 owner）。
 *  @param owner            所有者窗口，可为 NULL
 *  @param folders          为真时选文件夹（只选一个），否则选文件
 *  @param multi            为真时可多选文件（folders 为真时忽略）
 *  @param defaultDownloads 为真时初始位置取「下载」文件夹
 *  @param title            对话框标题
 *  @param out              [出参] 选中项的完整路径
 *  @return 用户选中了至少一项返回 true；取消或失败返回 false
 */
bool SharePickItems(HWND owner, bool folders, bool multi, bool defaultDownloads, const wchar_t* title,
                    std::vector<std::wstring>& out);

} // namespace fileshare
