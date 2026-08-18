/**
 *  ChatSendMode.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "ChatSendMode.h"
#include "../../Common/Util/IniFile.h"
#include "../../Session/UserSessionData.h"   // g_szHomePath（exe 所在目录）
#include <tchar.h>

namespace balloonchatwnd {

namespace {

//flamingo.ini 里发送键模式的段名 / 键名（ASCII，遵守该文件须保持 ASCII 的约束）。
const TCHAR* const kIniSection = _T("chat");
const TCHAR* const kIniKey     = _T("sendmsgkey");
//键值（ASCII）：enter = Enter 发送；ctrlenter = Ctrl+Enter 发送。
const TCHAR* const kValEnter     = _T("enter");
const TCHAR* const kValCtrlEnter = _T("ctrlenter");

//进程内缓存的发送模式；-1 表示尚未从 ini 读入。
int g_cachedMode = -1;

//拼出 flamingo.ini 的完整路径（与 AboutDlg / 网络设置窗一致：g_szHomePath + config\flamingo.ini）。
CString IniPath()
{
    CString path(g_szHomePath);
    path += _T("config\\flamingo.ini");
    return path;
}

} // namespace

SendKeyMode GetChatSendMode()
{
    if (g_cachedMode < 0)
    {
        CIniFile ini;
        TCHAR buf[32] = { 0 };
        ini.ReadString(kIniSection, kIniKey, kValEnter, buf, ARRAYSIZE(buf), IniPath());
        if (_tcsicmp(buf, kValCtrlEnter) == 0)
            g_cachedMode = kSendOnCtrlEnter;
        else
            g_cachedMode = kSendOnEnter;
    }
    return static_cast<SendKeyMode>(g_cachedMode);
}

void SetChatSendMode(SendKeyMode mode)
{
    g_cachedMode = static_cast<int>(mode);
    CIniFile ini;
    ini.WriteString(kIniSection, kIniKey,
                    mode == kSendOnCtrlEnter ? kValCtrlEnter : kValEnter,
                    IniPath());
}

LPCTSTR GetChatSendHintText(SendKeyMode mode)
{
    if (mode == kSendOnCtrlEnter)
        return _T("提示：按 Enter 换行");
    return _T("提示：按 Ctrl+Enter 换行");
}

LPCTSTR GetChatSendPlaceholder(SendKeyMode mode)
{
    if (mode == kSendOnCtrlEnter)
        return _T("输入消息，Ctrl+Enter 发送，Enter 换行");
    return _T("输入消息，Enter 发送，Ctrl+Enter 换行");
}

} // namespace balloonchatwnd
