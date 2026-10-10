/**
 *  FlamingoMedia 窗口标题栏程序图标的守卫用例（标签 [mediaicon]），MediaWindowIconTests.cpp
 *  背景：2026-10-07 用户确认按开发约定文档「UI 约定」第 10 条给 FlamingoMedia 的窗口在标题栏左侧加程序图标：远程桌面观看窗、
 *  屏幕共享观看窗、白板、其他人的屏幕、修改标题与描述、主持人退出选择框、确认框、共享说明、观看者名单；摄像头小窗不加。
 *  这些窗口依赖网络与推流管线，不在单元测试工程里，本用例在运行时读取它们的源文件检查：
 *    - MICN1 每个窗口建窗后调用 SetIcon(m_programIcons.TitleBitmap(...))，底色为不透明标题栏的颜色，头文件里有 m_programIcons；
 *    - MICN2 会出现在任务栏的四个窗口（没有所有者的重叠窗口）另外调用 ApplyTaskIcons；
 *    - MICN3 摄像头小窗没有加；
 *    - MICN4 文字输入框（加入密码、标注的添加文字 / 修改文字 / 修改名字等共用 CShareTextInputDialog）各种用途都在标题栏显示
 *      程序图标，任务栏图标只在进任务栏的用途（加入密码）设置（2026-10-07 补，原先只有加入密码时才有标题栏图标）。
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>

namespace {

//读文件时每次读的字节数
const size_t kMicnReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）：去掉这么多级就是 flamingoclient
const int kMicnDepth = 3;

std::string MicnReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kMicnReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string MicnMediaRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kMicnDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self + "\\FlamingoMedia\\";
}

//一个要加图标的窗口：源文件（不含扩展名）与是否会出现在任务栏
struct MicnWindow
{
    const char* m_file;
    bool        m_taskbar;
};

const MicnWindow kMicnWindows[] = {
    { "RemoteDesktop\\RdViewerWindow", true },
    { "Share\\ShareViewerWindow", true },
    { "Share\\ShareWhiteboardWindow", true },
    { "Share\\ShareScreensWindow", true },
    { "Share\\ShareInfoEditDialog", false },
    { "Share\\ShareSharerBar", false },
    { "Share\\ShareConfirmDialog", false },
    { "Share\\ShareInfoPopup", false },
    { "Share\\ShareViewerListPanel", false },
};

} // namespace

TEST_CASE("MICN1 media windows set the program icon on the title bar", "[mediaicon]")
{
    const std::string root = MicnMediaRoot();
    REQUIRE_FALSE(root.empty());
    for (size_t i = 0; i < sizeof(kMicnWindows) / sizeof(kMicnWindows[0]); ++i)
    {
        INFO("window " << kMicnWindows[i].m_file);
        const std::string cpp = MicnReadFile(root + kMicnWindows[i].m_file + ".cpp");
        const std::string h = MicnReadFile(root + kMicnWindows[i].m_file + ".h");
        REQUIRE_FALSE(cpp.empty());
        REQUIRE_FALSE(h.empty());
        CHECK(cpp.find("SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, "
                       "balloonmain::kOpaqueTitleBarColor))") != std::string::npos);
        CHECK(h.find("balloonmain::ProgramWindowIcons m_programIcons;") != std::string::npos);
    }
}

TEST_CASE("MICN2 taskbar windows also set the taskbar icons", "[mediaicon]")
{
    const std::string root = MicnMediaRoot();
    for (size_t i = 0; i < sizeof(kMicnWindows) / sizeof(kMicnWindows[0]); ++i)
    {
        INFO("window " << kMicnWindows[i].m_file);
        const std::string cpp = MicnReadFile(root + kMicnWindows[i].m_file + ".cpp");
        CHECK((cpp.find("m_programIcons.ApplyTaskIcons(m_hWnd)") != std::string::npos) == kMicnWindows[i].m_taskbar);
    }
}

TEST_CASE("MICN3 the camera window has no program icon", "[mediaicon]")
{
    const std::string cpp = MicnReadFile(MicnMediaRoot() + "Share\\ShareCameraWindow.cpp");
    REQUIRE_FALSE(cpp.empty());
    CHECK(cpp.find("m_programIcons") == std::string::npos);
}

TEST_CASE("MICN4 the text input dialog shows the program icon for every use", "[mediaicon]")
{
    const std::string cpp = MicnReadFile(MicnMediaRoot() + "Share\\ShareTextInputDialog.cpp");
    REQUIRE_FALSE(cpp.empty());
    //建窗时不论用途都调用，参数只决定是否另设任务栏图标
    CHECK(cpp.find("applyProgramIcons(spec.m_inTaskbar);") != std::string::npos);
    CHECK(cpp.find("if (spec.m_inTaskbar)\n        applyProgramIcons") == std::string::npos);
    //标题栏图标在判断是否设任务栏图标之前设置
    const size_t setIcon = cpp.find("SetIcon(m_titleIcon);");
    const size_t taskCheck = cpp.find("if (!taskIcons)");
    REQUIRE(setIcon != std::string::npos);
    REQUIRE(taskCheck != std::string::npos);
    CHECK(setIcon < taskCheck);
}
