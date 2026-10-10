/**
 *  退出、登出、电脑关机或注销前的「云端录像」对话框（标签 [cloudexit]），CloudExitDialogTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.4 节（2026-10-07 用户确认）。覆盖 FlamingoMedia/Record/CloudExitDialog：
 *    - CEXD1 各场合的按钮文字：退出、登出、注销三个场合的按钮分别写「退出 / 登出 / 注销」，没有勾选项；
 *    - CEXD2 关机场合：按钮为「关机」「重启」「取消关机」，有「同时放弃这些录像」勾选项；
 *    - CEXD3 按钮与结果的对应：退出类场合为继续、放弃、不退出，关窗等于不退出；关机场合按勾选项分为关机或重启的继续与放弃，
 *            第三个按钮与关窗为取消关机；哪些结果要放弃录像；
 *    - CEXD4 录像清单的写法：每行「· 标题」，没有标题写「（未命名的录像）」，超过 5 条时写「等 N 条」；
 *    - CEXD5 对话框窗口：关机场合有勾选项与三个按钮；勾上后点「重启」，结果为重启并放弃；关窗为取消关机；
 *    - CEXD6 源文件守卫（exe 侧）：退出与登出经 ConfirmCloudRecordExit 询问，选「不退出」时不退出；主面板处理
 *            WM_QUERYENDSESSION：强制关机、已确认过、没有未完成的录像时不拦，否则登记拦截原因并返回 FALSE，再投消息弹对话框；
 *            对话框之后按选择重新发起关机、重启或注销，并先记下已确认。
 *    - CEXD7 按钮放得下文字：四种场合打开对话框，每个按钮的实际宽度不小于「文字宽度 + 按钮留白」，
 *            三个按钮都在窗口之内；简体中文界面同样要满足（原先简体中文下按钮恒为 96 像素，「登出并放弃这些录像」被截断）。
 *  CEXD5 会建一个置顶的窗口，测完即销毁；CEXD6 在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独
 *  加了 /FC，编译期即为绝对路径）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"
#include "../../FlamingoMedia/Record/CloudExitDialog.h"
#include "../Common/I18n/I18n.h"
#include "../Common/I18n/I18nLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenrecord;
using namespace flamingomedia;

namespace {

//清单里的标题个数：比上限多两条
const int kCexdManyTitles = kCloudExitMaxListed + 2;

//按 id 找控件树里的按钮
balloonwjui::DuiButton* CexdButton(CCloudExitDialog& dlg, UINT id)
{
    balloonwjui::DuiControl* content = dlg.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

//模拟点一下按钮
void CexdClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//读文件时每次读的字节数、本文件在 flamingoclient 下的层数（Source\Tests\本文件）
const size_t kCexdReadChunk = 4096;
const int kCexdDepth = 3;

std::string CexdReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kCexdReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string CexdClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kCexdDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//取一个函数的函数体：从定义开始到下一个以 nextPrefix 开头的行（或文件末尾）
std::string CexdFunctionBody(const std::string& source, const std::string& signature, const std::string& nextPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + nextPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

} // namespace

TEST_CASE("CEXD1 button texts for exit, logout and logoff", "[cloudexit]")
{
    CloudExitDialogSpec s = MakeCloudExitDialogSpec(media_cloud_exit_app);
    CHECK(s.m_buttons[kCloudExitButtonFirst] == std::wstring(TR(L"退出，下次继续")));
    CHECK(s.m_buttons[kCloudExitButtonSecond] == std::wstring(TR(L"退出并放弃这些录像")));
    CHECK(s.m_buttons[kCloudExitButtonThird] == std::wstring(TR(L"不退出")));
    CHECK_FALSE(s.m_hasAbandonCheck);
    CHECK_FALSE(s.m_intro.empty());
    CHECK_FALSE(s.m_note.empty());
    s = MakeCloudExitDialogSpec(media_cloud_exit_logout);
    CHECK(s.m_buttons[kCloudExitButtonFirst] == std::wstring(TR(L"登出，下次继续")));
    CHECK(s.m_buttons[kCloudExitButtonThird] == std::wstring(TR(L"不登出")));
    CHECK_FALSE(s.m_hasAbandonCheck);
    s = MakeCloudExitDialogSpec(media_cloud_exit_logoff);
    CHECK(s.m_buttons[kCloudExitButtonSecond] == std::wstring(TR(L"注销并放弃这些录像")));
    CHECK_FALSE(s.m_hasAbandonCheck);
}

TEST_CASE("CEXD2 shutdown offers shutdown, restart and an abandon checkbox", "[cloudexit]")
{
    const CloudExitDialogSpec s = MakeCloudExitDialogSpec(media_cloud_exit_shutdown);
    CHECK(s.m_buttons[kCloudExitButtonFirst] == std::wstring(TR(L"关机")));
    CHECK(s.m_buttons[kCloudExitButtonSecond] == std::wstring(TR(L"重启")));
    CHECK(s.m_buttons[kCloudExitButtonThird] == std::wstring(TR(L"取消关机")));
    CHECK(s.m_hasAbandonCheck);
    CHECK_FALSE(s.m_checkText.empty());
}

TEST_CASE("CEXD3 buttons map to choices", "[cloudexit]")
{
    const int kinds[3] = { media_cloud_exit_app, media_cloud_exit_logout, media_cloud_exit_logoff };
    for (int i = 0; i < 3; ++i)
    {
        CHECK(CloudExitChoiceFor(kinds[i], kCloudExitButtonFirst, false) == media_cloud_exit_keep);
        CHECK(CloudExitChoiceFor(kinds[i], kCloudExitButtonSecond, false) == media_cloud_exit_abandon);
        CHECK(CloudExitChoiceFor(kinds[i], kCloudExitButtonThird, false) == media_cloud_exit_cancel);
        CHECK(CloudExitChoiceFor(kinds[i], kCloudExitClosed, false) == media_cloud_exit_cancel);
    }
    CHECK(CloudExitChoiceFor(media_cloud_exit_shutdown, kCloudExitButtonFirst, false) == media_cloud_exit_shutdown_keep);
    CHECK(CloudExitChoiceFor(media_cloud_exit_shutdown, kCloudExitButtonFirst, true) == media_cloud_exit_shutdown_abandon);
    CHECK(CloudExitChoiceFor(media_cloud_exit_shutdown, kCloudExitButtonSecond, false) == media_cloud_exit_restart_keep);
    CHECK(CloudExitChoiceFor(media_cloud_exit_shutdown, kCloudExitButtonSecond, true) == media_cloud_exit_restart_abandon);
    CHECK(CloudExitChoiceFor(media_cloud_exit_shutdown, kCloudExitButtonThird, true) == media_cloud_exit_cancel);
    CHECK(CloudExitChoiceFor(media_cloud_exit_shutdown, kCloudExitClosed, true) == media_cloud_exit_cancel);
    //要放弃录像的结果
    CHECK(CloudExitChoiceAbandons(media_cloud_exit_abandon));
    CHECK(CloudExitChoiceAbandons(media_cloud_exit_shutdown_abandon));
    CHECK(CloudExitChoiceAbandons(media_cloud_exit_restart_abandon));
    CHECK_FALSE(CloudExitChoiceAbandons(media_cloud_exit_keep));
    CHECK_FALSE(CloudExitChoiceAbandons(media_cloud_exit_cancel));
    CHECK_FALSE(CloudExitChoiceAbandons(media_cloud_exit_restart_keep));
}

TEST_CASE("CEXD4 list text of unfinished records", "[cloudexit]")
{
    std::vector<std::wstring> titles;
    titles.push_back(L"周会演示");
    titles.push_back(L"");
    const std::wstring two = MakeCloudExitListText(titles);
    CHECK(two == std::wstring(L"· 周会演示\n· ") + TR(L"（未命名的录像）"));
    titles.clear();
    for (int i = 0; i < kCexdManyTitles; ++i)
        titles.push_back(L"T" + std::to_wstring(static_cast<long long>(i)));
    const std::wstring many = MakeCloudExitListText(titles);
    //列出前 5 条，最后一行写全部条数
    CHECK(many.find(L"T4") != std::wstring::npos);
    CHECK(many.find(L"T5") == std::wstring::npos);
    CHECK(many.find(TRF(L"等 {0} 条", kCexdManyTitles)) != std::wstring::npos);
    CHECK(MakeCloudExitListText(std::vector<std::wstring>()).empty());
}

TEST_CASE("CEXD5 the shutdown dialog records restart and abandon", "[cloudexit]")
{
    std::vector<std::wstring> titles(1, L"周会演示");
    CCloudExitDialog dlg;
    REQUIRE(dlg.Open(media_cloud_exit_shutdown, titles));
    REQUIRE(dlg.IsWindow());
    CHECK(CexdButton(dlg, CCloudExitDialog::kIdButtonFirst) != NULL);
    CHECK(CexdButton(dlg, CCloudExitDialog::kIdButtonSecond) != NULL);
    CHECK(CexdButton(dlg, CCloudExitDialog::kIdButtonThird) != NULL);
    balloonwjui::DuiButton* check = CexdButton(dlg, CCloudExitDialog::kIdAbandonCheck);
    REQUIRE(check != NULL);
    CHECK_FALSE(dlg.IsDecided());
    check->SetCheck(true, false);
    CexdClick(dlg.m_hWnd, CCloudExitDialog::kIdButtonSecond);
    CHECK(dlg.IsDecided());
    CHECK(dlg.Result() == media_cloud_exit_restart_abandon);
    dlg.Close();
    CHECK_FALSE(dlg.IsWindow());

    //关窗：取消关机
    REQUIRE(dlg.Open(media_cloud_exit_shutdown, titles));
    ::SendMessageW(dlg.m_hWnd, WM_CLOSE, 0, 0);
    CHECK(dlg.IsDecided());
    CHECK(dlg.Result() == media_cloud_exit_cancel);
    dlg.Close();

    //退出场合：没有勾选项，第二个按钮为放弃
    REQUIRE(dlg.Open(media_cloud_exit_app, titles));
    CHECK(CexdButton(dlg, CCloudExitDialog::kIdAbandonCheck) == NULL);
    CexdClick(dlg.m_hWnd, CCloudExitDialog::kIdButtonSecond);
    CHECK(dlg.Result() == media_cloud_exit_abandon);
    dlg.Close();
}

TEST_CASE("CEXD6 exit, logout and system shutdown go through the cloud record dialog", "[cloudexit]")
{
    const std::string root = CexdClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string session = CexdReadFile(root + "\\Source\\UI\\BalloonMain\\MainPanelSessionController.cpp");
    REQUIRE_FALSE(session.empty());
    const std::string doExit = CexdFunctionBody(session, "void CSessionController::DoExit(", "void CSessionController::");
    REQUIRE_FALSE(doExit.empty());
    CHECK(doExit.find("ConfirmCloudRecordExit(flamingomedia::media_cloud_exit_app)") != std::string::npos);
    CHECK(doExit.find("media_cloud_exit_cancel") != std::string::npos);
    CHECK(session.find("ConfirmCloudRecordExit(flamingomedia::media_cloud_exit_logout)") != std::string::npos);

    const std::string panel = CexdReadFile(root + "\\Source\\UI\\BalloonMain\\BalloonMainPanel.cpp");
    REQUIRE_FALSE(panel.empty());
    const std::string query =
        CexdFunctionBody(panel, "LRESULT CBalloonMainPanel::OnQueryEndSession(", "LRESULT CBalloonMainPanel::");
    REQUIRE_FALSE(query.empty());
    CHECK(query.find("ENDSESSION_CRITICAL") != std::string::npos);
    CHECK(query.find("m_endSessionApproved") != std::string::npos);
    CHECK(query.find("ShutdownBlockReasonCreate(") != std::string::npos);
    CHECK(query.find("PostMessage(kMsgAskCloudOnEndSession") != std::string::npos);
    CHECK(query.find("return FALSE;") != std::string::npos);
    const std::string ask =
        CexdFunctionBody(panel, "LRESULT CBalloonMainPanel::OnAskCloudOnEndSession(", "LRESULT CBalloonMainPanel::");
    REQUIRE_FALSE(ask.empty());
    CHECK(ask.find("ShutdownBlockReasonDestroy(") != std::string::npos);
    //先记下已确认，再重新发起关机、重启或注销
    const size_t approved = ask.find("m_endSessionApproved = true;");
    const size_t power = ask.find("RunPowerAction(flamingolock::kPowerShutdown)");
    REQUIRE(approved != std::string::npos);
    REQUIRE(power != std::string::npos);
    CHECK(approved < power);
    CHECK(ask.find("RunPowerAction(flamingolock::kPowerRestart)") != std::string::npos);
    CHECK(ask.find("EWX_LOGOFF") != std::string::npos);
}

TEST_CASE("CEXD7 every button is wide enough for its text", "[cloudexit]")
{
    const int kinds[] = { media_cloud_exit_app, media_cloud_exit_logout, media_cloud_exit_logoff,
                          media_cloud_exit_shutdown };
    const UINT ids[] = { CCloudExitDialog::kIdButtonFirst, CCloudExitDialog::kIdButtonSecond,
                         CCloudExitDialog::kIdButtonThird };
    //按钮文字用的默认字体：按本机最高的显示器 DPI 取字高，与对话框排版时一致
    const int fontPx = i18n::DefaultFontPixelHeightForDpi(i18n::MaxMonitorDpi());
    std::vector<std::wstring> titles(1, L"张勇的屏幕共享");
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k)
    {
        CCloudExitDialog dlg;
        REQUIRE(dlg.Open(kinds[k], titles));
        RECT client = { 0, 0, 0, 0 };
        ::GetClientRect(dlg.m_hWnd, &client);
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        {
            balloonwjui::DuiButton* btn = CexdButton(dlg, ids[i]);
            REQUIRE(btn != NULL);
            const std::wstring text(btn->GetText().GetString());
            const RECT rc = btn->GetRect();
            const int need = i18n::MeasureUiTextWidth(text.c_str(), fontPx, false) + i18n::kDefaultButtonTextPadPx;
            INFO("kind " << kinds[k] << ", button " << i << ", width " << (rc.right - rc.left) << ", need " << need);
            CHECK(rc.right - rc.left >= need);
            CHECK(rc.left >= client.left);
            CHECK(rc.right <= client.right);
        }
        dlg.Close();
    }
}
