/**
 *  ChatSendMode.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "ChatSendMode.h"
#include "../../Common/Util/IniFile.h"
#include "../../Session/UserSessionData.h"   // g_szHomePath（exe 所在目录）
#include <tchar.h>
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//flamingo.ini 里发送键模式的段名 / 键名（ASCII，遵守该文件须保持 ASCII 的约束）。
const TCHAR* const kIniSection = _T("chat");
const TCHAR* const kIniKey     = _T("sendmsgkey");
//键值（ASCII）：enter = Enter 发送；ctrlenter = Ctrl+Enter 发送。
const TCHAR* const kValEnter     = _T("enter");
const TCHAR* const kValCtrlEnter = _T("ctrlenter");

//进程内缓存的发送模式；-1 表示尚未读入。
int g_cachedMode = -1;

//绑定的账号配置（BUG-119）；NULL 表示未绑定，读写 flamingo.ini。所有权归调用方。
IChatSendModeStore* g_store = NULL;

//拼出 flamingo.ini 的完整路径（与 AboutDlg / 网络设置窗一致：g_szHomePath + config\flamingo.ini）。
CString IniPath()
{
    CString path(g_szHomePath);
    path += _T("config\\flamingo.ini");
    return path;
}

//读 flamingo.ini 里的发送键（未绑定账号配置时用，以及账号配置迁移时取旧值）
SendKeyMode ReadIniSendMode()
{
    CIniFile ini;
    TCHAR buf[32] = { 0 };
    ini.ReadString(kIniSection, kIniKey, kValEnter, buf, ARRAYSIZE(buf), IniPath());
    if (_tcsicmp(buf, kValCtrlEnter) == 0)
        return kSendOnCtrlEnter;
    return kSendOnEnter;
}

} // namespace

void BindChatSendModeStore(IChatSendModeStore* store)
{
    g_store = store;
    g_cachedMode = -1;
    //账号配置里还没有发送键：把 flamingo.ini 的旧值迁移过去，老用户升级后发送键不变
    if (g_store != NULL && !g_store->HasSendMode())
    {
        g_store->SaveSendMode(ReadIniSendMode());
    }
}

SendKeyMode GetChatSendMode()
{
    if (g_cachedMode < 0)
    {
        g_cachedMode = (g_store != NULL) ? static_cast<int>(g_store->LoadSendMode())
                                         : static_cast<int>(ReadIniSendMode());
    }
    return static_cast<SendKeyMode>(g_cachedMode);
}

void SetChatSendMode(SendKeyMode mode)
{
    g_cachedMode = static_cast<int>(mode);
    if (g_store != NULL)
    {
        g_store->SaveSendMode(mode);
        return;
    }
    CIniFile ini;
    ini.WriteString(kIniSection, kIniKey,
                    mode == kSendOnCtrlEnter ? kValCtrlEnter : kValEnter,
                    IniPath());
}

LPCTSTR GetChatSendHintText(SendKeyMode mode)
{
    if (mode == kSendOnCtrlEnter)
        return TR(L"提示：按 Enter 换行");
    return TR(L"提示：按 Ctrl+Enter 换行");
}

LPCTSTR GetChatSendPlaceholder(SendKeyMode mode)
{
    if (mode == kSendOnCtrlEnter)
        return TR(L"输入消息，Ctrl+Enter 发送，Enter 换行");
    return TR(L"输入消息，Enter 发送，Ctrl+Enter 换行");
}

} // namespace balloonchatwnd
