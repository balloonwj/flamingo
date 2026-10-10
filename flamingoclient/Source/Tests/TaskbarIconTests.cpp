/**
 *  会出现在任务栏的窗口有没有给系统提供图标（标签 [taskicon]），TaskbarIconTests.cpp
 *
 *  背景：2026-10-07 统查标题栏程序图标时发现，系统设置、关于、修改密码、消息记录等十几个窗口在标题栏左侧画了程序图标
 *  （DuiFrameWindow::SetIcon 设的是标题栏位图），但没有发 WM_SETICON，也没有调 ProgramWindowIcons::ApplyTaskIcons。
 *  用户选定「先核对，确有问题就全部补上」。
 *    - TBI1 核对：按这些窗口的做法建一个没有所有者的 DuiFrameWindow、只设标题栏位图，程序提供图标的几个途径
 *           （WM_GETICON 的 ICON_BIG / ICON_SMALL、窗口类的 GCLP_HICON / GCLP_HICONSM）全为空，即窗口没有给系统提供
 *           任何图标；ICON_SMALL2 按文档是「程序没有提供时由系统生成的图标」，与系统默认的程序图标 IDI_APPLICATION 比较，
 *           作为系统替它显示什么的依据（比较结果写进用例输出）；调 ApplyTaskIcons 之后大小图标都取得到（对照）。
 *    - TBI2 源文件守卫：核对过的 15 个窗口（都没有所有者、会出现在任务栏）头文件里有 m_taskIcons，建窗后调用了
 *           m_taskIcons.ApplyTaskIcons(m_hWnd)（这些窗口依赖账号、网络或主面板，不在单元测试工程里，只能读源文件检查）。
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。建的窗口不显示。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>

#include "../UI/BalloonMain/TitleBarIcon.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

namespace {

//测试窗口的整窗尺寸（像素）
const int kTbiWidth = 320;
const int kTbiHeight = 200;

//按「关于」「修改密码」等窗口的做法建窗：没有所有者、WS_OVERLAPPEDWINDOW，只在标题栏放程序图标位图
class TbiFrame : public balloonwjui::DuiFrameWindow
{
public:
    BEGIN_MSG_MAP(TbiFrame)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()
};

//程序给窗口提供的图标：WM_GETICON 的大、小两种与窗口类的两种，全为空时返回 true
bool TbiNoProvidedIcon(HWND hwnd)
{
    const LRESULT bigIcon = ::SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    const LRESULT smallIcon = ::SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    const ULONG_PTR classBig = ::GetClassLongPtr(hwnd, GCLP_HICON);
    const ULONG_PTR classSmall = ::GetClassLongPtr(hwnd, GCLP_HICONSM);
    INFO("WM_GETICON big " << bigIcon << ", small " << smallIcon << "; class icon " << classBig << ", class small icon "
         << classSmall);
    return bigIcon == 0 && smallIcon == 0 && classBig == 0 && classSmall == 0;
}

//读文件时每次读的字节数
const size_t kTbiReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）：去掉这么多级就是 flamingoclient
const int kTbiDepth = 3;

//核对过、补了任务栏图标的窗口（相对 Source/UI 的路径，不含扩展名；fopen 接受正斜杠）
const char* const kTbiWindows[] = {
    "BalloonUpdate/BalloonUpdateWindow",
    "BalloonSettings/BalloonSystemSettingDlg",
    "BalloonOcr/OcrResultWindow",
    "BalloonNotifyMode/BalloonNotifyManageWindow",
    "BalloonMsgBox/BalloonMsgBoxWindow",
    "BalloonMemo/BalloonMemoWindow",
    "BalloonHistory/MergedRecordWindow",
    "BalloonHistory/BalloonHistoryWindow",
    "BalloonGroupManage/BalloonGroupManageWindow",
    "BalloonGroupManage/BalloonAnnouncePopupDlg",
    "BalloonMain/BalloonChangePwdDlg",
    "BalloonMain/BalloonAboutDlg",
    "BalloonMain/BalloonModifyAvatarDlg",
    "BalloonAnnounce/BalloonAnnounceBoxWindow",
    "BalloonMain/CreateGroupDlg",
};

//读一个源文件的全部内容；读不到时返回空串
std::string TbiReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kTbiReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//Source/UI 目录（带结尾斜杠）：本测试文件所在目录往上两级是 flamingoclient
std::string TbiUiRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kTbiDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self + "/Source/UI/";
}

} // namespace

TEST_CASE("TBI1 只设标题栏位图的窗口没有给系统提供任何图标，补上 ApplyTaskIcons 后才有", "[taskicon]")
{
    balloonmain::ProgramWindowIcons titleIcons;
    TbiFrame frame;
    REQUIRE(frame.Create(NULL, CWindow::rcDefault, _T("tbi"), WS_OVERLAPPEDWINDOW, 0) != NULL);
    frame.ResizeClient(kTbiWidth, kTbiHeight);
    frame.SetIcon(titleIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    //只设标题栏位图：程序没有提供任何图标
    CHECK(TbiNoProvidedIcon(frame.m_hWnd));
    //系统替它生成的小图标（据推测任务栏、Alt+Tab 显示的就是它，有待人工核对）：与系统默认的程序图标比较，结果写进输出
    const HICON systemSmall = reinterpret_cast<HICON>(::SendMessage(frame.m_hWnd, WM_GETICON, ICON_SMALL2, 0));
    const HICON defaultApp = ::LoadIcon(NULL, IDI_APPLICATION);
    WARN("ICON_SMALL2 " << systemSmall << ", IDI_APPLICATION " << defaultApp << ", same handle "
         << ((systemSmall == defaultApp) ? 1 : 0));

    //对照：补上任务栏图标之后，大小图标都取得到
    balloonmain::ProgramWindowIcons taskIcons;
    taskIcons.ApplyTaskIcons(frame.m_hWnd);
    CHECK(::SendMessage(frame.m_hWnd, WM_GETICON, ICON_BIG, 0) != 0);
    CHECK(::SendMessage(frame.m_hWnd, WM_GETICON, ICON_SMALL, 0) != 0);
    frame.DestroyWindow();
}

TEST_CASE("TBI2 会出现在任务栏的 15 个窗口建窗后都设置了任务栏图标", "[taskicon]")
{
    const std::string root = TbiUiRoot();
    REQUIRE(root.size() > 1);
    for (size_t i = 0; i < sizeof(kTbiWindows) / sizeof(kTbiWindows[0]); ++i)
    {
        INFO("window " << kTbiWindows[i]);
        const std::string cpp = TbiReadFile(root + kTbiWindows[i] + ".cpp");
        const std::string h = TbiReadFile(root + kTbiWindows[i] + ".h");
        REQUIRE_FALSE(cpp.empty());
        REQUIRE_FALSE(h.empty());
        CHECK(h.find("balloonmain::ProgramWindowIcons m_taskIcons;") != std::string::npos);
        CHECK(cpp.find("m_taskIcons.ApplyTaskIcons(m_hWnd);") != std::string::npos);
    }
}
