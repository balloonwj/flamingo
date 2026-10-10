/**
 *  删除历史登录账号时的确认与本地数据清理实现，AccountRemoval.cpp
 *
 *  balloonwj@qq.com   2026-08-11
 */

#include "stdafx.h"
#include "AccountRemoval.h"

#include <commctrl.h>       // TaskDialogIndirect
#include <shellapi.h>       // SHFileOperation
#include <vector>

#include "../../Session/UserSessionData.h"   // g_szHomePath
#include "../../net/UserCachePath.h" // net::UserCacheDir / SanitizeAccountForPath
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找

namespace balloonauth {

namespace {

//确认框里「删除」按钮的自定义命令 ID。取一个不与 IDOK / IDCANCEL 撞的值。
const int kBtnIdDelete = 1001;

} // anonymous namespace

bool ConfirmRemoveAccount(HWND hParent, const CString& account, bool& outDeleteLocal)
{
    outDeleteLocal = false;

    std::wstring content = TRF(L"账号「{0}」将从本机的登录记录中移除，下次打开登录窗口不再出现在下拉列表里。\n\n"
                               L"这不会影响服务器上的账号本身，你仍然可以重新输入它来登录。",
                               (LPCTSTR)account);

    //自定义按钮：把危险动作写在按钮上（"删除账号"），比笼统的"确定"更难误点。
    TASKDIALOG_BUTTON buttons[1];
    buttons[0].nButtonID     = kBtnIdDelete;
    buttons[0].pszButtonText = TR(L"删除账号");

    TASKDIALOGCONFIG cfg;
    ::ZeroMemory(&cfg, sizeof(cfg));
    cfg.cbSize             = sizeof(cfg);
    cfg.hwndParent         = hParent;
    cfg.dwFlags            = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.dwCommonButtons    = TDCBF_CANCEL_BUTTON;
    cfg.pszWindowTitle     = TR(L"删除登录账号");
    cfg.pszMainIcon        = TD_WARNING_ICON;
    cfg.pszMainInstruction = TR(L"确定要删除这个账号吗？");
    cfg.pszContent         = content.c_str();
    cfg.pButtons           = buttons;
    cfg.cButtons           = ARRAYSIZE(buttons);
    cfg.nDefaultButton     = IDCANCEL;   //默认落在"取消"上，回车不会误删
    //勾选项默认不勾：多数人只想让下拉列表干净点，不是要清历史。
    cfg.pszVerificationText = TR(L"同时删除该账号在本机的聊天记录与缓存（不可恢复）");

    int pressed = 0;
    BOOL verified = FALSE;
    const HRESULT hr = ::TaskDialogIndirect(&cfg, &pressed, NULL, &verified);
    if (FAILED(hr))
    {
        //确认不了就不删 —— 宁可让用户再点一次，也不能在没确认的情况下删东西。
        return false;
    }

    if (pressed != kBtnIdDelete)
    {
        return false;
    }

    outDeleteLocal = (verified != FALSE);
    return true;
}

bool DeleteAccountLocalData(const CString& account)
{
    if (account.IsEmpty())
    {
        return false;
    }

    //路径统一由 UserCacheDir 拼（子目录传空 → 拿到 <主目录>Users\<账号>\）。
    //账号净化、空账号返回空串这些边界都在那一层处理过了。
    const std::wstring dir =
        net::UserCacheDir(g_szDataRoot, std::wstring(account.GetString()), NULL);
    if (dir.empty())
    {
        return false;
    }

    //安全边界：拼出来的路径必须真的落在 <主目录>Users\ 之下。UserCacheDir 已经
    //净化过账号，这里再核一次是"删目录"这种不可逆动作应有的谨慎 —— 万一将来
    //有人改了那边的实现，这道检查还能拦住。
    std::wstring usersRoot = g_szDataRoot;
    if (!usersRoot.empty() && usersRoot[usersRoot.size() - 1] != L'\\')
    {
        usersRoot += L'\\';
    }
    usersRoot += L"Users\\";
    if (dir.size() <= usersRoot.size() || dir.compare(0, usersRoot.size(), usersRoot) != 0)
    {
        return false;
    }

    //目录本来就不存在：当作已经删干净，返回成功。
    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return true;
    }
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return false;   //同名的不是目录，不碰它
    }

    //SHFileOperation 要求路径以两个 '\0' 结尾（它收的是路径列表）。
    //末尾那个反斜杠也要去掉，否则部分系统上会拒绝。
    std::wstring target = dir;
    if (!target.empty() && target[target.size() - 1] == L'\\')
    {
        target.erase(target.size() - 1);
    }
    std::vector<wchar_t> buf(target.begin(), target.end());
    buf.push_back(L'\0');
    buf.push_back(L'\0');

    SHFILEOPSTRUCTW op;
    ::ZeroMemory(&op, sizeof(op));
    op.wFunc  = FO_DELETE;
    op.pFrom  = &buf[0];
    //直接删、不进回收站、不再弹系统确认框（本端已经确认过一次了），
    //也不弹系统错误框 —— 失败与否由返回值告诉调用方，让它自己记日志。
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_NOCONFIRMMKDIR;

    return ::SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

} // namespace balloonauth
