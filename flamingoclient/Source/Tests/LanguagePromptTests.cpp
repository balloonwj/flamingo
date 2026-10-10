/**
 *  系统设置「语言」下拉框选中后弹询问框的时机（标签 [langprompt]），LanguagePromptTests.cpp
 *
 *  背景：2026-10-06 用户在系统设置里切换语言，「是否立即重启？」选「否」后 Flamingo.exe 在 atlwin.h 第 3238 行断言
 *  （Expression: 0，snapshots/225221.png）。2026-10-07 的修改把询问框从下拉框的选中通知里移到投给自己的消息
 *  kMsgLanguageChosen 里，当时的原因分析是推测：选中通知来自下拉框浮层的消息处理过程，在通知里同步弹询问框，
 *  浮层会因失去焦点在询问框的模态循环里被销毁，浮层内的宿主对象在 m_hWnd 尚未清空时被释放，触发
 *  CWindowImplRoot 析构函数里的断言（「Object deleted before window was destroyed」）。
 *
 *  覆盖：
 *    LSP1 旧写法复现：在选中通知里同步弹询问框（选「否」），子进程触发 atlwin.h 的断言；
 *    LSP2 新写法：选中通知里只投递消息，消息处理时再弹询问框（选「否」），子进程不触发断言、正常结束；
 *    LSP3 源文件守卫：设置窗的「语言」分支只投递 kMsgLanguageChosen，询问框在 OnLanguageChosen 里弹出
 *         （设置窗依赖账号配置与主面板，不在单元测试工程里，只能读源文件检查）。
 *
 *  做法：LSP1、LSP2 各启动一个本程序的子进程运行隐藏用例 LSP-CHILD。子进程建一个带下拉框的宿主窗口，打开浮层，
 *  向浮层里的宿主窗口发真实的鼠标消息选中一项；询问框用真实的 MessageBox，由线程定时器找到它并点「否」。
 *  子进程装了 CRT 报告钩子：遇到断言时把报告内容写进结果文件并立即结束进程（退出码 kLspAssertExitCode），
 *  不弹断言框，也不会继续执行到释放后的内存。断言会让测试进程本身崩溃，所以不能在当前进程里跑。
 *  子进程会弹出真实的下拉浮层与消息框，建议在不可见桌面上运行整套用例。
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3；全局 _Module 由其他测试文件统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <crtdbg.h>
#include <stdio.h>
#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <memory>
#include <string>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

namespace {

//子进程运行的隐藏用例的标签
const wchar_t kLspChildTag[] = L"[langprompt-child]";
//向子进程传递写法（sync / post）的环境变量
const wchar_t kLspModeEnv[] = L"FLAMINGO_LSP_MODE";
//向子进程传递结果文件路径的环境变量
const wchar_t kLspResultEnv[] = L"FLAMINGO_LSP_RESULT";
//旧写法：在选中通知里同步弹询问框
const wchar_t kLspModeSync[] = L"sync";
//新写法：选中通知里只投递消息，消息处理时再弹询问框
const wchar_t kLspModePost[] = L"post";
//子进程遇到断言时的退出码（「LS」两个字母的 ASCII）
const DWORD kLspAssertExitCode = 0x4C53;
//等子进程结束的最长时间（毫秒）；超时则结束子进程并判失败
const DWORD kLspChildTimeoutMs = 30000;
//询问框出现后多久自动点「否」（毫秒）。要长于浮层失去焦点到投递并处理关闭消息所需的时间
const UINT kLspDismissDelayMs = 300;
//选中一项之后继续处理消息的时间（毫秒）：让投递的询问消息、浮层关闭消息都处理完
const DWORD kLspSettleMs = 1500;
//打开浮层后等它建好的时间（毫秒）
const DWORD kLspPopupWaitMs = 500;
//消息循环每轮空闲时的休眠（毫秒）
const DWORD kLspPumpSleepMs = 10;
//宿主窗口的位置与尺寸（像素）：放在主屏左上角附近，浮层在它下方弹出
const int kLspFrameX = 100;
const int kLspFrameY = 100;
const int kLspFrameW = 320;
const int kLspFrameH = 80;
//下拉框在宿主里的矩形（像素）
const int kLspComboX = 10;
const int kLspComboY = 10;
const int kLspComboW = 200;
const int kLspComboH = 28;
//下拉框的控件编号
const UINT kLspComboId = 7001;
//下拉框的项数；要选的是第 1 项（当前选中第 0 项），点在浮层的竖直中线上正好是它
const int kLspItemCount = 3;
//点击位置距浮层左边的水平距离（像素）
const int kLspClickX = 20;
//询问框的标题，定时器靠它找到询问框
const wchar_t kLspPromptCaption[] = L"LSP prompt";
//宿主投给自己的「弹询问框」消息（只在本测试的宿主窗口内使用）
const UINT kLspMsgPrompt = WM_APP + 0x700;
//下拉浮层的窗口类名（balloonui DuiComboBox.cpp 的 DECLARE_WND_CLASS_EX）
const wchar_t kLspPopupClass[] = L"__DuiComboBoxPopup__";
//读文件时每次读的字节数
const size_t kLspReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）：去掉这么多级就是 flamingoclient
const int kLspDepth = 3;

//子进程的结果文件路径（宽字符），钩子与询问框都往这里追加一行
std::wstring g_lspResultPath;

//在作用域内启动 GDI+（下拉框与浮层用 GDI+ 绘制；单元测试程序的入口不统一启动它）
class LspGdiplusScope
{
public:
    LspGdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~LspGdiplusScope()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，析构时交还
    bool m_ok;           // 是否启动成功
};

//往结果文件末尾追加一行（UTF-8）。钩子里也会调用，所以只用 Win32 文件接口，不分配 CRT 堆以外的资源
void LspAppendResult(const std::wstring& line)
{
    if (g_lspResultPath.empty())
        return;
    HANDLE h = ::CreateFileW(g_lspResultPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    const std::wstring text = line + L"\n";
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (bytes > 0)
    {
        std::string utf8((size_t)bytes, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], bytes, NULL, NULL);
        DWORD written = 0;
        ::WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    }
    ::CloseHandle(h);
}

//CRT 报告钩子（宽字符版）：断言时写下报告内容并立即结束进程；其余报告交给 CRT 默认处理
int __cdecl LspReportHookW(int reportType, wchar_t* message, int* returnValue)
{
    if (reportType != _CRT_ASSERT)
        return FALSE;
    LspAppendResult(std::wstring(L"assert: ") + (message != NULL ? message : L""));
    if (returnValue != NULL)
        *returnValue = 0;
    ::TerminateProcess(::GetCurrentProcess(), kLspAssertExitCode);
    return TRUE;
}

//CRT 报告钩子（窄字符版），做法同上
int __cdecl LspReportHookA(int reportType, char* message, int* returnValue)
{
    if (reportType != _CRT_ASSERT)
        return FALSE;
    std::wstring text;
    if (message != NULL)
    {
        const int n = ::MultiByteToWideChar(CP_ACP, 0, message, -1, NULL, 0);
        if (n > 1)
        {
            text.resize((size_t)n - 1);
            ::MultiByteToWideChar(CP_ACP, 0, message, -1, &text[0], n);
        }
    }
    LspAppendResult(L"assert: " + text);
    if (returnValue != NULL)
        *returnValue = 0;
    ::TerminateProcess(::GetCurrentProcess(), kLspAssertExitCode);
    return TRUE;
}

//线程定时器回调：找到询问框并点「否」
void CALLBACK LspDismissPrompt(HWND /*hwnd*/, UINT /*msg*/, UINT_PTR id, DWORD /*time*/)
{
    HWND box = ::FindWindowW(L"#32770", kLspPromptCaption);
    if (box == NULL)
        return;
    ::KillTimer(NULL, id);
    ::PostMessageW(box, WM_COMMAND, MAKEWPARAM(IDNO, BN_CLICKED), 0);
}

//处理当前线程的消息 ms 毫秒
void LspPump(DWORD ms)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < ms)
    {
        MSG msg;
        bool any = false;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            any = true;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (!any)
            ::Sleep(kLspPumpSleepMs);
    }
}

/**
 *  子进程里的宿主窗口：内含一个子窗口 DuiHost，DuiHost 里只有一个「语言」下拉框。下拉框的通知经 DuiHost
 *  同步送到本窗口（WM_DUI_NOTIFY），本窗口按写法决定同步弹询问框还是先投递消息，与设置窗的两种写法对应。
 */
class LspFrame : public CWindowImpl<LspFrame, CWindow, CWinTraits<WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW> >
{
public:
    DECLARE_WND_CLASS(_T("__LspFrame__"))

    BEGIN_MSG_MAP(LspFrame)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kLspMsgPrompt, OnPromptMsg)
    END_MSG_MAP()

    //sync 为 true 时按旧写法在通知里同步弹询问框，否则按新写法先投递消息
    explicit LspFrame(bool sync)
        : m_sync(sync)
        , m_combo(NULL)
    {
    }

    //建窗并装入下拉框；成功时返回 true
    bool Build()
    {
        RECT rc = { kLspFrameX, kLspFrameY, kLspFrameX + kLspFrameW, kLspFrameY + kLspFrameH };
        if (Create(NULL, rc, _T("LanguagePromptTests")) == NULL)
            return false;
        RECT rcHost = { 0, 0, kLspFrameW, kLspFrameH };
        if (m_host.Create(m_hWnd, rcHost, _T(""), WS_CHILD | WS_VISIBLE) == NULL)
            return false;
        std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
        combo->SetCtrlId(kLspComboId);
        for (int i = 0; i < kLspItemCount; ++i)
        {
            wchar_t text[16];
            swprintf_s(text, L"lang %d", i);
            combo->AddString(text);
        }
        combo->SetCurSel(0, false);
        m_combo = combo.get();
        m_host.SetRoot(std::unique_ptr<balloonwjui::DuiControl>(combo.release()));
        RECT rcCombo = { kLspComboX, kLspComboY, kLspComboX + kLspComboW, kLspComboY + kLspComboH };
        m_combo->SetRect(rcCombo);
        return true;
    }

    //销毁窗口
    void Destroy()
    {
        if (m_host.IsWindow())
            m_host.DestroyWindow();
        if (IsWindow())
            DestroyWindow();
    }

    //打开下拉浮层
    void OpenPopup()
    {
        m_combo->OpenPopup();
    }

private:
    LRESULT OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
        const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
        if (n != NULL && n->code == (UINT)balloonwjui::DUIN_VALUECHANGED && n->ctrlId == kLspComboId && n->extra >= 0)
        {
            LspAppendResult(m_sync ? L"selected: prompt now" : L"selected: prompt posted");
            if (m_sync)
                Prompt();
            else
                PostMessage(kLspMsgPrompt);
        }
        return 0;
    }

    LRESULT OnPromptMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        Prompt();
        return 0;
    }

    //弹真实的询问框（与设置窗相同的 MB_YESNO），由线程定时器自动点「否」
    void Prompt()
    {
        ::SetTimer(NULL, 0, kLspDismissDelayMs, LspDismissPrompt);
        const int answer = ::MessageBoxW(m_hWnd, L"restart now?", kLspPromptCaption, MB_YESNO | MB_ICONQUESTION);
        wchar_t line[64];
        swprintf_s(line, L"prompt answered %d", answer);
        LspAppendResult(line);
    }

    bool m_sync;                          // true：旧写法（通知里同步弹框）；false：新写法（先投递消息）
    balloonwjui::DuiHost m_host;          // 承载下拉框的子窗口，随本对象销毁
    balloonwjui::DuiComboBox* m_combo;    // 下拉框，归 m_host 的控件树所有
};

//读一个文件的全部内容；读不到时返回空串
std::string LspReadFile(const std::wstring& path)
{
    std::string data;
    FILE* f = NULL;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == NULL)
        return data;
    char buf[kLspReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//启动本程序的子进程运行 LSP-CHILD；返回子进程退出码，结果文件内容放进 result。启动失败或超时返回 MAXDWORD
DWORD LspRunChild(const wchar_t* mode, std::string& result)
{
    wchar_t exe[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    wchar_t resultPath[MAX_PATH] = { 0 };
    swprintf_s(resultPath, L"%slsp_%s_%u.txt", tempDir, mode, (unsigned)::GetCurrentProcessId());
    ::DeleteFileW(resultPath);

    ::SetEnvironmentVariableW(kLspModeEnv, mode);
    ::SetEnvironmentVariableW(kLspResultEnv, resultPath);
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" \"" + kLspChildTag + L"\"";
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    const BOOL started = ::CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    ::SetEnvironmentVariableW(kLspModeEnv, NULL);
    ::SetEnvironmentVariableW(kLspResultEnv, NULL);
    if (!started)
        return MAXDWORD;

    DWORD code = MAXDWORD;
    if (::WaitForSingleObject(pi.hProcess, kLspChildTimeoutMs) == WAIT_OBJECT_0)
        ::GetExitCodeProcess(pi.hProcess, &code);
    else
        ::TerminateProcess(pi.hProcess, MAXDWORD);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    result = LspReadFile(resultPath);
    ::DeleteFileW(resultPath);
    return code;
}

//本测试文件所在目录往上两级就是 flamingoclient（Source\Tests\本文件）
std::string LspClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kLspDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//读一个源文件的全部内容；读不到时返回空串
std::string LspReadSource(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kLspReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//取一个成员函数的函数体：从定义开始，到其后第一个位于行首的右花括号为止（本仓库的函数体都以行首的 } 结束）
std::string LspFunctionBody(const std::string& source, const std::string& signature)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t end = source.find("\n}", begin);
    return source.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

} // namespace

TEST_CASE("LSP-CHILD 子进程：选中下拉框一项后弹询问框并选「否」", "[.][langprompt-child]")
{
    wchar_t mode[16] = { 0 };
    wchar_t resultPath[MAX_PATH] = { 0 };
    REQUIRE(::GetEnvironmentVariableW(kLspModeEnv, mode, _countof(mode)) > 0);
    REQUIRE(::GetEnvironmentVariableW(kLspResultEnv, resultPath, MAX_PATH) > 0);
    g_lspResultPath = resultPath;
    _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, LspReportHookW);
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, LspReportHookA);

    const bool sync = (wcscmp(mode, kLspModeSync) == 0);
    LspGdiplusScope gdiplus;
    LspFrame frame(sync);
    REQUIRE(frame.Build());
    frame.OpenPopup();
    LspPump(kLspPopupWaitMs);

    HWND popup = ::FindWindowW(kLspPopupClass, NULL);
    REQUIRE(popup != NULL);
    HWND popupHost = ::GetWindow(popup, GW_CHILD);
    REQUIRE(popupHost != NULL);
    RECT rc;
    ::GetClientRect(popupHost, &rc);
    //三项里的第 1 项在浮层的竖直中线上
    const LPARAM pt = MAKELPARAM(kLspClickX, (rc.bottom - rc.top) / 2);
    ::SendMessageW(popupHost, WM_MOUSEMOVE, 0, pt);
    ::SendMessageW(popupHost, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    ::SendMessageW(popupHost, WM_LBUTTONUP, 0, pt);
    LspPump(kLspSettleMs);

    CHECK(::FindWindowW(kLspPopupClass, NULL) == NULL);
    LspAppendResult(L"done");
    frame.Destroy();
    _CrtSetReportHookW2(_CRT_RPTHOOK_REMOVE, LspReportHookW);
    _CrtSetReportHook2(_CRT_RPTHOOK_REMOVE, LspReportHookA);
}

TEST_CASE("LSP1 旧写法复现：在下拉框的选中通知里同步弹询问框，选「否」后触发 atlwin.h 的断言", "[langprompt]")
{
    std::string result;
    const DWORD code = LspRunChild(kLspModeSync, result);
    INFO("child exit code " << code << ", result:\n" << result);
    CHECK(result.find("selected: prompt now") != std::string::npos);
    CHECK(code == kLspAssertExitCode);
    CHECK(result.find("atlwin.h") != std::string::npos);
    CHECK(result.find("done") == std::string::npos);
}

TEST_CASE("LSP2 新写法：选中通知里只投递消息，之后再弹询问框，选「否」后不触发断言", "[langprompt]")
{
    std::string result;
    const DWORD code = LspRunChild(kLspModePost, result);
    INFO("child exit code " << code << ", result:\n" << result);
    CHECK(result.find("selected: prompt posted") != std::string::npos);
    CHECK(result.find("prompt answered 7") != std::string::npos);   //IDNO == 7
    CHECK(result.find("assert:") == std::string::npos);
    CHECK(result.find("done") != std::string::npos);
    CHECK(code == 0);
}

TEST_CASE("LSP3 设置窗的「语言」分支只投递消息，询问框在 OnLanguageChosen 里弹出", "[langprompt]")
{
    const std::string root = LspClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source =
        LspReadSource(root + "\\Source\\UI\\BalloonSettings\\BalloonSystemSettingDlg.cpp");
    REQUIRE_FALSE(source.empty());

    //「语言」分支：从 if (ctrlId == kIdLanguage) 到它的右花括号
    const size_t branch = source.find("if (ctrlId == kIdLanguage)");
    REQUIRE(branch != std::string::npos);
    const size_t branchEnd = source.find("\n    }", branch);
    REQUIRE(branchEnd != std::string::npos);
    const std::string languageBranch = source.substr(branch, branchEnd - branch);
    CHECK(languageBranch.find("::PostMessage(m_hWnd, kMsgLanguageChosen") != std::string::npos);
    CHECK(languageBranch.find("MessageBox") == std::string::npos);

    const std::string chosen = LspFunctionBody(source, "void CBalloonSystemSettingDlg::OnLanguageChosen(");
    REQUIRE_FALSE(chosen.empty());
    CHECK(chosen.find("MB_YESNO") != std::string::npos);

    const std::string header =
        LspReadSource(root + "\\Source\\UI\\BalloonSettings\\BalloonSystemSettingDlg.h");
    CHECK(header.find("MESSAGE_HANDLER(kMsgLanguageChosen, OnLanguageChosenMsg)") != std::string::npos);
}
