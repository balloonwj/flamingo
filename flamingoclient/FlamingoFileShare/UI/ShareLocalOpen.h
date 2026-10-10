/**
 *  在本机打开本人发起的共享，ShareLocalOpen.h
 *
 *  「文件共享」窗口的「在本机打开」与点本人发出的共享卡片（2026-10-03 起）共用这一份实现：按本机记录里的共享根，
 *  磁盘与文件夹用资源管理器直接打开，单个文件打开所在文件夹并选中它。打不开时返回原因，由调用方决定怎样提示。
 *
 *  典型用法：
 *      const int result = fileshare::OpenShareLocally(hwnd, &service, shareId);
 *      if (result != fileshare::share_local_open_ok)
 *          ::MessageBoxW(hwnd, fileshare::ShareLocalOpenFailText(result, service.GetLocalStatus(shareId)).c_str(), ...);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>

namespace fileshare {

class CShareService;

//在本机打开的结果
enum share_local_open_result
{
    share_local_open_ok           = 0,   //已交给资源管理器
    share_local_open_no_record    = 1,   //本机没有这个共享的记录：共享不是在本机发起的，或本机记录已被清理
    share_local_open_root_missing = 2,   //本机有记录，但共享的磁盘、文件夹或文件已不存在（U 盘被拔出、被删除或改名）
    share_local_open_shell_failed = 3,   //资源管理器没有打开（ShellExecute 失败）
};

/**
 *  在本机打开本人发起的共享。
 *  @param owner   交给 ShellExecute 的父窗口，可为 NULL
 *  @param service 业务总控（不拥有），为 NULL 时按没有记录处理
 *  @param shareId 共享 id
 *  @return share_local_open_result 之一
 */
int OpenShareLocally(HWND owner, CShareService* service, int64_t shareId);

/**
 *  打不开时给用户看的原因。
 *  @param result      OpenShareLocally 的结果（不是 share_local_open_ok）
 *  @param localStatus 该共享在本机的提供状态（CShareService::GetLocalStatus，share_local_status），用来区分
 *                     「在另一台电脑上发起的」与「本机记录已被清理」；不知道时传 0
 *  @return 提示文字
 */
std::wstring ShareLocalOpenFailText(int result, int localStatus);

/**
 *  用系统默认程序打开本机的一个文件或文件夹（第 4 期起，完成弹窗的「打开」）。
 *  @param path 完整路径
 *  @return 已交给外壳返回 true；路径为空、已不存在或外壳失败返回 false
 */
bool ShareShellOpenPath(const std::wstring& path);

/**
 *  在资源管理器里打开所在文件夹并选中这一项（第 4 期起，完成弹窗的「打开所在文件夹」「在文件夹中显示」）。
 *  @param path 完整路径
 *  @return 已交给资源管理器返回 true；路径为空、已不存在或外壳失败返回 false
 */
bool ShareShellRevealPath(const std::wstring& path);

} // namespace fileshare
