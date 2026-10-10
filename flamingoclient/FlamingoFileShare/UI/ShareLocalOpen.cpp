/**
 *  在本机打开本人发起的共享的实现，见 ShareLocalOpen.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareLocalOpen.h"

#include <shellapi.h>   //ShellExecuteW

#include "../Engine/ShareService.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace fileshare {

namespace {

//ShellExecute 的返回值大于这个数表示成功（Win32 约定）
const INT_PTR kShellExecuteOk = 32;

} // namespace

int OpenShareLocally(HWND owner, CShareService* service, int64_t shareId)
{
    ShareLocalRecord rec;
    if (service == NULL || !service->GetLocalRecord(shareId, rec) || rec.m_rootPath.empty())
    {
        LOG_INFO("open share locally, shareid: %lld, no local record.", static_cast<long long>(shareId));
        return share_local_open_no_record;
    }
    if (::GetFileAttributesW(ShareToLongPath(rec.m_rootPath).c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LOG_INFO("open share locally, shareid: %lld, root missing.", static_cast<long long>(shareId));
        return share_local_open_root_missing;
    }
    //单个文件：打开所在文件夹并选中它；磁盘与文件夹：直接打开
    HINSTANCE result = NULL;
    if (rec.m_kind == share_kind_file)
    {
        const std::wstring args = L"/select,\"" + rec.m_rootPath + L"\"";
        result = ::ShellExecuteW(owner, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
    }
    else
    {
        result = ::ShellExecuteW(owner, L"open", rec.m_rootPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    const bool ok = reinterpret_cast<INT_PTR>(result) > kShellExecuteOk;
    LOG_INFO("open share locally, shareid: %lld, kind: %d, ok: %d.", static_cast<long long>(shareId), rec.m_kind, ok ? 1 : 0);
    return ok ? share_local_open_ok : share_local_open_shell_failed;
}

bool ShareShellOpenPath(const std::wstring& path)
{
    if (path.empty() || ::GetFileAttributesW(ShareToLongPath(path).c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LOG_INFO(L"share shell open skipped, path missing: %s.", path.c_str());
        return false;
    }
    HINSTANCE result = ::ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<INT_PTR>(result) > kShellExecuteOk;
    LOG_INFO(L"share shell open, path: %s, ok: %d.", path.c_str(), ok ? 1 : 0);
    return ok;
}

bool ShareShellRevealPath(const std::wstring& path)
{
    if (path.empty() || ::GetFileAttributesW(ShareToLongPath(path).c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LOG_INFO(L"share shell reveal skipped, path missing: %s.", path.c_str());
        return false;
    }
    //与「在本机打开」单个文件的写法相同：explorer.exe /select,"路径"
    const std::wstring args = L"/select,\"" + path + L"\"";
    HINSTANCE result = ::ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<INT_PTR>(result) > kShellExecuteOk;
    LOG_INFO(L"share shell reveal, path: %s, ok: %d.", path.c_str(), ok ? 1 : 0);
    return ok;
}

std::wstring ShareLocalOpenFailText(int result, int localStatus)
{
    switch (result)
    {
    //本机没有记录：上线比对时知道它绑定的是别的设备，就直说在另一台电脑上
    case share_local_open_no_record:
        if (localStatus == share_local_status_other_device)
            return TR(L"这个共享是在另一台电脑上发起的，请在那台电脑上打开。");
        return TR(L"本机没有这个共享的记录，无法在本机打开。");

    //共享根已不存在
    case share_local_open_root_missing:
        return TR(L"共享的内容在本机上已不存在：磁盘可能已被拔出，或文件夹、文件已被删除、改名。");

    //资源管理器没有打开
    case share_local_open_shell_failed:
        return TR(L"资源管理器没有打开共享的内容，请稍后重试。");

    //已经打开（调用方不应在成功时取提示文字）
    default:
        return std::wstring();
    }
}

} // namespace fileshare
