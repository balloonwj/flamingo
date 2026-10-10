/**
 *  帖子群窗口的工厂 CThreadUiFactory 的实现，见 ThreadUiFactory.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadUiFactory.h"

#include "ThreadWindow.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"           // i18n::InitFromHost：DLL 的界面文字经宿主的语言模块翻译
#include "../../Source/Common/I18n/BalloonUiText.h"  // ResolveBalloonUiText：balloonui 内置文字的译文
#include "../../../third-party/balloonui/balloonui/DuiTheme.h"   // 默认字体（本 DLL 自己那份 balloonui）
#include "../../../third-party/balloonui/balloonui/DuiTextResolver.h"   // SetTextResolver：内置文字经宿主翻译

namespace threadgroup {

namespace {

//语言代码缓冲区的容量（字符），足够放下 "zh-CN"、"qps-ploc" 一类的代码
const uint32_t kLangCodeChars = 32;

//DLL 里 TR 查找时转交的宿主；InitUi 时登记，ShutdownUi 时清空。可能在任意线程读取
IThreadGroupHost* volatile g_translateHost = NULL;

//交给 i18n::InitFromHost 的查找函数：转给宿主的 Translate；宿主已解除登记时返回原文
const wchar_t* HostTranslate(const wchar_t* context, const wchar_t* source)
{
    IThreadGroupHost* host = g_translateHost;
    return (host != NULL) ? host->Translate(context, source) : source;
}

} // namespace

CThreadUiFactory& CThreadUiFactory::Instance()
{
    static CThreadUiFactory s_factory;
    return s_factory;
}

CThreadUiFactory::CThreadUiFactory()
{
}

bool CThreadUiFactory::InitUi(IThreadGroupHost* host)
{
    if (host == NULL)
        return false;
    //多语言：界面文字经宿主的语言模块翻译，界面字体取宿主当前生效的字体；须在创建任何窗口之前
    wchar_t language[kLangCodeChars] = { 0 };
    wchar_t uiFont[LF_FACESIZE] = { 0 };
    g_translateHost = host;
    if (host->GetUiLanguage(language, kLangCodeChars, uiFont, LF_FACESIZE))
    {
        i18n::InitFromHost(&HostTranslate, language, uiFont);
        balloonwjui::DuiTheme::Inst().SetDefaultFontFace(i18n::UiFontName());
        balloonwjui::SetTextResolver(&i18n::ResolveBalloonUiText);
        LOG_INFO(L"FlamingoThreadGroup ui language %s, ui font %s.", language, uiFont);
    }
    else
    {
        LOG_WARNING(L"FlamingoThreadGroup: host did not report the ui language, using built-in source text.");
    }
    return true;
}

void CThreadUiFactory::ShutdownUi()
{
    g_translateHost = NULL;
}

IThreadWindow* CThreadUiFactory::CreateThreadWindow(int64_t groupId, CThreadSession* session, IThreadWindowOwner* owner)
{
    if (groupId <= 0 || session == NULL || owner == NULL)
        return NULL;
    return new CThreadWindow(groupId, session, owner);
}

} // namespace threadgroup
