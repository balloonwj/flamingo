/**
 *  通话窗图标按钮的窗口级用例（标签 [.][calldlg]，默认不运行），CallDlgIconBarTests.cpp
 *
 *  这些用例真的创建并显示 CBalloonCallDlg（OpenConnected / OpenCalling / OpenIncoming 都会显示窗口、抢前台），
 *  放进默认运行会在跑全量测试时闪出窗口、抢走焦点，故标为隐藏，改动通话窗后手工运行：
 *      FlamingoUnit.exe "[calldlg]"
 *  覆盖：
 *    - CDB1 语音接通态：麦克风、音量、切到视频、共享屏幕、挂断五个圆形按钮，从左到右、尺寸正确、挂断贴右边距；
 *           点麦克风经控制器关麦、按钮外观变化，再点恢复；音量滑块调到 0 时音量按钮出现斜杠；点音量按钮弹出浮层；
 *    - CDB2 视频接通态：多出摄像头按钮；窗口缩到最小宽度 776 时按钮之间、按钮与左侧的小画面、信号图标和时长都不重叠；
 *           共享状态变化时共享按钮的外观随之变化；「共享电脑声音」按钮在共享屏幕与挂断之间，开关时外观随之变化
 *           （2026-10-06，docs/音视频通话方案.md 第 10 节的 CSA9；最小宽度由 716 改为 776）；
 *    - CDB3 呼叫中与来电两种形态的按钮；
 *    - CDB4 网络信号图标（2026-10-06，网络状况标识第 3 期）：紧挨小画面、20×16、不接收焦点；快照为默认值时悬停详情写
 *           「正在检测」；对方报告「较差」时图标换位图、悬停详情为五行、浮出降级提示；恢复后图标与详情随之更新；
 *    - CDB5 视频态操作条浮在画面上（2026-10-06，界面美化方案第 3.4 节）：大画面铺满、操作条贴底、小画面浮在左下角；
 *           鼠标静止后淡出，完全隐藏时不参与命中而小画面仍可点；鼠标一动淡入；离开窗口后淡出；大画面右上角有通话时长
 *           胶囊。「停在操作条上、音量浮层开着时保持显示」不在这里测：模拟的鼠标移动会让 balloonui 调 TrackMouseEvent，
 *           真实鼠标不在窗口上时系统随即补发 WM_MOUSELEAVE；音量浮层在测试进程不在前台时会自动关闭。保持的时序由
 *           [callbar] 的 BAH4 覆盖。用例里要真实等待静止时长，约 8 秒；运行期间不要在窗口上移动鼠标。
 *  每条用例都把窗口截图存到 %TEMP%\calldlg_*.png，供人工核对观感。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <string.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Call/BalloonCallDlg.h"
#include "../../FlamingoMedia/Call/CallButtonArt.h"
#include "../../FlamingoMedia/Call/CallingLayout.h"   //通话窗的尺寸常量
#include "../../FlamingoMedia/Call/CallSignalArt.h"   //CDB4：信号图标控件与设计尺寸
#include "../../FlamingoMedia/Call/CallOverlayBar.h"  //CDB5：浮在画面上的操作条
#include "../../FlamingoMedia/Call/CallBarAutoHide.h" //CDB5：淡入淡出的时长
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   //CDB4：读信号图标的悬停详情
#include <algorithm>
#include "../Common/Log/IULog.h"                       //CIULog：CDB1 把通话窗的日志写到临时目录
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace {

//通话窗里各按钮的 ctrlId（与 BalloonCallDlg.cpp 的取值一致）
const UINT kCdbLeft = 10;
const UINT kCdbRight = 11;
const UINT kCdbPip = 13;
const UINT kCdbSwitch = 14;
const UINT kCdbCamera = 15;
const UINT kCdbShare = 16;
const UINT kCdbMic = 17;
const UINT kCdbVolume = 18;
const UINT kCdbVolumeSlider = 19;
const UINT kCdbSignal = 20;     //网络信号图标（2026-10-06）
const UINT kCdbNetToast = 21;   //网络变差的提示（2026-10-06）
const UINT kCdbOverlayBar = 22; //视频态浮在画面上的操作条（2026-10-06）
const UINT kCdbSysAudio = 23;   //共享电脑声音（2026-10-06）
const UINT kCdbBig = 12;        //大画面
//CDB5：小画面的尺寸与它离客户区下边缘的距离（在操作条高度内上下居中：(120 - 90) / 2）
const int kCdbPipW = 160;
const int kCdbPipH = 90;
const int kCdbPipBottomGap = (call::kCallBottomBarH - kCdbPipH) / 2;
//CDB5：等操作条淡出的时长（毫秒）：静止时长 + 淡出时长，再留 500 毫秒余量（计时器到点有延迟）
const DWORD kCdbIdleWaitMs = static_cast<DWORD>(call::kCallBarIdleMs + call::kCallBarFadeOutMs + 500);
//CDB5：等操作条淡入的时长（毫秒）：淡入时长再留 300 毫秒余量
const DWORD kCdbFadeWaitMs = static_cast<DWORD>(call::kCallBarFadeInMs + 300);
//通话时长计时器的 id（与 BalloonCallDlg.cpp 的 kCallDurationTimerId 一致）；CDB4 直接发这条 WM_TIMER 驱动每秒刷新
const UINT_PTR kCdbDurationTimerId = 1;
//CDB4 快照里的往返时延（毫秒）与对方收我的丢包率（千分比，即 12.5%）
const uint32_t kCdbRttMs = 34;
const uint16_t kCdbPeerLossPermille = 125;
//CDB4 悬停详情五行之间的换行符个数
const long kCdbDetailLineBreaks = 4;
//对方的 userid 与名字
const int64_t kCdbPeer = 5302;
//窗口显示后泵消息的时长（毫秒），让布局与绘制完成
const DWORD kCdbSettleMs = 300;
//单次泵消息之间的让出时长（毫秒）
const DWORD kCdbPumpSliceMs = 10;
//视频态的最小客户区宽（与 BalloonCallDlg.cpp 的 kVideoMinClientW 一致；2026-10-06 加信号图标后由 700 改为 716，
//同日加「共享电脑声音」按钮后改为 776）
const int kCdbMinWidth = 776;
//视频态底部栏左侧被小画面（含左右边距）占去的宽度：24 + 160 + 24，信号图标从这里开始
const int kCdbPipRightUsed = 208;
//视频态底部栏左侧被小画面、信号图标（20 + 间距 8）与通话时长（52）占去的宽度
const int kCdbVideoLeftUsed = 288;
//把鼠标「移到」画面区左上角的坐标（客户区像素）：按钮都在底部栏，这里不会悬停在任何按钮上
const int kCdbAwayX = 30;
const int kCdbAwayY = 80;

//假的通话控制器：记下通话窗交来的操作
class FakeCallActions : public call::ICallDialogActions
{
public:
    FakeCallActions()
        : m_micCalls(0)
        , m_lastMic(true)
        , m_micOpenOk(true)
        , m_volume(100)
        , m_hangups(0)
        , m_snapshotCalls(0)
        , m_sysAudioCalls(0)
        , m_lastSysAudio(false)
        , m_sysAudioResult(0)
    {
    }

    virtual void CancelCall(const std::string& /*sessionId*/) override
    {
    }

    virtual void RespondCall(const std::string& /*sessionId*/, bool /*accept*/) override
    {
    }

    virtual void HangupCall(const std::string& /*sessionId*/) override
    {
        ++m_hangups;
    }

    virtual void SwitchCallMedia(const std::string& /*sessionId*/, int /*media*/) override
    {
    }

    virtual void ToggleScreenShare() override
    {
    }

    virtual int SetSystemAudio(bool on) override
    {
        ++m_sysAudioCalls;
        m_lastSysAudio = on;
        return on ? m_sysAudioResult : 0;
    }

    virtual bool SetLocalCamera(bool on) override
    {
        return on;
    }

    virtual bool SetLocalMic(bool on) override
    {
        ++m_micCalls;
        m_lastMic = on;
        return on && m_micOpenOk;
    }

    virtual void SetPlaybackVolume(int percent) override
    {
        m_volume = percent;
    }

    virtual int GetPlaybackVolume() const override
    {
        return m_volume;
    }

    virtual call::CallNetSnapshot GetNetSnapshot() const override
    {
        ++m_snapshotCalls;
        return m_snapshot;
    }

    call::CallNetSnapshot m_snapshot;   //GetNetSnapshot 返回的快照，用例按需填写
    mutable int m_snapshotCalls;        //GetNetSnapshot 被调用的次数
    int  m_micCalls;    //SetLocalMic 被调用的次数
    bool m_lastMic;     //最近一次 SetLocalMic 的参数
    bool m_micOpenOk;   //打开麦克风是否成功（模拟设备不可用时置 false）
    int  m_volume;      //当前播放音量（%）
    int  m_hangups;     //HangupCall 被调用的次数
    int  m_sysAudioCalls;    //SetSystemAudio 被调用的次数（2026-10-06）
    bool m_lastSysAudio;     //最近一次 SetSystemAudio 的参数
    int  m_sysAudioResult;   //打开时 SetSystemAudio 的返回值（取 CallSystemAudioResult，默认 0 即成功）
};

//泵一会儿消息
void CdbPump(DWORD ms)
{
    const ULONGLONG until = ::GetTickCount64() + ms;
    MSG msg;
    while (::GetTickCount64() < until)
    {
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        ::Sleep(kCdbPumpSliceMs);
    }
}

//向通话窗发一条控件通知，与 DuiHost::SendNotify 发出的相同
void CdbNotify(HWND hwnd, UINT code, UINT ctrlId, LPARAM extra)
{
    balloonwjui::DuiNotify n;
    memset(&n, 0, sizeof(n));
    n.code = code;
    n.ctrlId = ctrlId;
    n.pCtrl = NULL;
    n.extra = extra;
    ::SendMessage(hwnd, WM_DUI_NOTIFY, static_cast<WPARAM>(ctrlId), reinterpret_cast<LPARAM>(&n));
}

//向窗口发一条鼠标移到画面区左上角的消息：屏幕上真实鼠标恰好停在某个按钮上时，按钮处于悬停态，
//GetBgBitmapForCurrentState 取到的是悬停位图，比较前先排除这一干扰
void CdbUnhover(HWND hwnd)
{
    ::SendMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(kCdbAwayX, kCdbAwayY));
}

//按 ctrlId 取通话窗客户区里的按钮
balloonwjui::DuiButton* CdbButton(CBalloonCallDlg& dlg, UINT id)
{
    balloonwjui::DuiControl* root = dlg.GetClientContent();
    if (root == NULL)
        return NULL;
    return static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(id));
}

//取按钮当前用的背景位图（先排除鼠标悬停的干扰）；按钮不存在时返回 NULL
HBITMAP CdbBitmap(CBalloonCallDlg& dlg, UINT id)
{
    CdbUnhover(dlg.m_hWnd);
    balloonwjui::DuiButton* btn = CdbButton(dlg, id);
    return (btn != NULL) ? btn->GetBgBitmapForCurrentState() : NULL;
}

//通话窗客户区的宽
int CdbClientWidth(CBalloonCallDlg& dlg)
{
    RECT rc;
    ::GetClientRect(dlg.m_hWnd, &rc);
    return rc.right - rc.left;
}

//本线程里属于 owner、正在显示的浮层窗口（音量浮层）
struct CdbPopupSearch
{
    HWND m_owner;   //所有者
    HWND m_found;   //找到的浮层；没有时为 NULL
};

BOOL CALLBACK CdbFindPopup(HWND hwnd, LPARAM lParam)
{
    CdbPopupSearch* s = reinterpret_cast<CdbPopupSearch*>(lParam);
    //按「属于通话窗、可见、带 WS_EX_TOOLWINDOW」认出浮层，不按类名找：浮层的窗口类在 balloonui 修正 DuiPopupHost 的窗口类注册（2026-10-05）之前
    //是基类的 __DuiHost__、之后是 __DuiPopupHost__，按所有者与样式判断与 balloonui 的版本无关
    const LONG exStyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
    if (::GetWindow(hwnd, GW_OWNER) == s->m_owner && ::IsWindowVisible(hwnd) && (exStyle & WS_EX_TOOLWINDOW) != 0)
    {
        s->m_found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND CdbFindVisiblePopup(HWND owner)
{
    CdbPopupSearch s;
    s.m_owner = owner;
    s.m_found = NULL;
    ::EnumThreadWindows(::GetCurrentThreadId(), &CdbFindPopup, reinterpret_cast<LPARAM>(&s));
    return s.m_found;
}

bool CdbPopupShowing(HWND owner)
{
    return CdbFindVisiblePopup(owner) != NULL;
}

//在 GDI+ 已注册的编码器里找 PNG 的那个
bool CdbPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//PrintWindow 的「按合成后的完整内容绘制」标志（Windows 8.1 起；老 SDK 头文件里没有这个宏）
const UINT kCdbPrintFullContent = 0x00000002;

//把窗口自身的内容（PrintWindow，与 DPI 缩放和遮挡无关）存成 %TEMP%\calldlg_<name>.png
void CdbShot(HWND hwnd, const wchar_t* name)
{
    CdbPump(kCdbSettleMs);
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    ::PrintWindow(hwnd, mem, kCdbPrintFullContent);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    {
        Gdiplus::Bitmap image(bmp, NULL);
        wchar_t dir[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, dir);
        const std::wstring path = std::wstring(dir) + L"calldlg_" + name + L".png";
        CLSID png;
        if (CdbPngEncoder(png))
            image.Save(path.c_str(), &png, NULL);
    }
    ::DeleteObject(bmp);
}

//进程内 GDI+ 的启停（截图存 PNG 用）
class CdbGdiplus
{
public:
    CdbGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }

    ~CdbGdiplus()
    {
        if (m_token != 0)
            Gdiplus::GdiplusShutdown(m_token);
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
};

} // namespace

TEST_CASE("CDB1 语音接通态：五个圆形按钮的顺序与尺寸；麦克风开关、音量滑块与音量浮层", "[.][calldlg]")
{
    CdbGdiplus gdiplus;
    //通话窗的日志写到 %TEMP%\calldlg_cdb1.*.log，失败时据此排查
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    CIULog::Init(true, false, (std::wstring(tempDir) + L"calldlg_cdb1").c_str());
    FakeCallActions actions;
    CBalloonCallDlg dlg;
    dlg.SetActions(&actions);
    dlg.SetVideoCall(false);
    dlg.OpenCalling(NULL, CString(L"韩晓燕"), kCdbPeer);
    dlg.SetSessionId("cdb-session");
    dlg.OpenConnected(NULL);
    REQUIRE(dlg.IsWindow());
    CdbPump(kCdbSettleMs);

    //从左到右：麦克风、音量、切到视频、共享屏幕、共享电脑声音、挂断；语音态没有摄像头与小画面
    const UINT order[] = { kCdbMic, kCdbVolume, kCdbSwitch, kCdbShare, kCdbSysAudio, kCdbRight };
    int lastRight = 0;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
    {
        INFO("ctrlId=" << order[i]);
        balloonwjui::DuiButton* btn = CdbButton(dlg, order[i]);
        REQUIRE(btn != NULL);
        CHECK(btn->GetBgBitmapForCurrentState() != NULL);
        CHECK(btn->GetText().IsEmpty());
        const RECT r = btn->GetRect();
        const int expect = (order[i] == kCdbRight) ? call::kCallRoundBtnLargePx : call::kCallRoundBtnPx;
        CHECK(r.right - r.left == expect);
        CHECK(r.bottom - r.top == expect);
        CHECK(r.left >= lastRight);
        lastRight = r.right;
    }
    CHECK(CdbButton(dlg, kCdbCamera) == NULL);
    CHECK(CdbButton(dlg, kCdbPip) == NULL);
    CHECK(CdbButton(dlg, kCdbRight)->GetRect().right == CdbClientWidth(dlg) - call::kCallSideMargin);
    CdbShot(dlg.m_hWnd, L"voice");

    //麦克风：点一下关麦，按钮换成「已关闭」的外观；再点恢复
    HBITMAP micOn = CdbBitmap(dlg, kCdbMic);
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_CLICK, kCdbMic, 0);
    CHECK(actions.m_micCalls == 1);
    CHECK_FALSE(actions.m_lastMic);
    CHECK(CdbBitmap(dlg, kCdbMic) != micOn);
    CdbShot(dlg.m_hWnd, L"voice_micoff");
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_CLICK, kCdbMic, 0);
    CHECK(actions.m_micCalls == 2);
    CHECK(actions.m_lastMic);
    CHECK(CdbBitmap(dlg, kCdbMic) == micOn);

    //打开麦克风失败（设备不可用）：按钮保持「已关闭」
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_CLICK, kCdbMic, 0);
    actions.m_micOpenOk = false;
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_CLICK, kCdbMic, 0);
    CHECK(actions.m_lastMic);
    CHECK(CdbBitmap(dlg, kCdbMic) != micOn);
    actions.m_micOpenOk = true;
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_CLICK, kCdbMic, 0);
    CHECK(CdbBitmap(dlg, kCdbMic) == micOn);

    //真实的鼠标点击（经 DuiHost 的按下 / 抬起处理）：照样切换麦克风，但焦点不落在按钮上，
    //所以不会在圆形按钮外画方形虚线焦点框（2026-10-05 真机核对时发现）。之后用点击通知恢复麦克风：
    //再发一次按下会被通话窗的双击合成当成双击（SendMessage 的消息时间不随等待更新），不是这里要测的
    {
        balloonwjui::DuiButton* mic = CdbButton(dlg, kCdbMic);
        REQUIRE(mic != NULL);
        const RECT r = mic->GetRect();
        const LPARAM center = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        const int callsBefore = actions.m_micCalls;
        ::SendMessage(dlg.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, center);
        ::SendMessage(dlg.m_hWnd, WM_LBUTTONUP, 0, center);
        CHECK(actions.m_micCalls == callsBefore + 1);
        CHECK_FALSE(actions.m_lastMic);
        CHECK(dlg.GetDuiFocus() != static_cast<balloonwjui::DuiControl*>(mic));
        CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_CLICK, kCdbMic, 0);
        CHECK(actions.m_micCalls == callsBefore + 2);
        CHECK(CdbBitmap(dlg, kCdbMic) == micOn);
    }

    //音量滑块：调到 0 时音量按钮出现斜杠，调回来斜杠消失
    HBITMAP volNormal = CdbBitmap(dlg, kCdbVolume);
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_VALUECHANGED, kCdbVolumeSlider, 0);
    CHECK(actions.m_volume == 0);
    CHECK(CdbBitmap(dlg, kCdbVolume) != volNormal);
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_VALUECHANGED, kCdbVolumeSlider, 120);
    CHECK(actions.m_volume == 120);
    CHECK(CdbBitmap(dlg, kCdbVolume) == volNormal);

    //点音量按钮：弹出浮层。点击后立刻就能找到它；泵消息之后它是否还在，取决于本进程能否拿到前台
    //（测试进程不在前台时浮层得不到激活，会按「失去激活」自动关闭；真实使用时用户的点击让通话窗处于前台）
    CHECK_FALSE(CdbPopupShowing(dlg.m_hWnd));
    CdbNotify(dlg.m_hWnd, balloonwjui::DUIN_CLICK, kCdbVolume, 0);
    const HWND popup = CdbFindVisiblePopup(dlg.m_hWnd);
    CHECK(popup != NULL);
    if (popup != NULL)
    {
        //浮层在音量按钮上方
        RECT popupRc;
        ::GetWindowRect(popup, &popupRc);
        RECT btnRc = CdbButton(dlg, kCdbVolume)->GetRect();
        ::ClientToScreen(dlg.m_hWnd, reinterpret_cast<POINT*>(&btnRc.left));
        CHECK(popupRc.bottom <= btnRc.top);
        CdbShot(popup, L"volume_popup");
    }
    CdbPump(kCdbSettleMs);
    INFO("popup still showing after pumping: " << (CdbPopupShowing(dlg.m_hWnd) ? 1 : 0)
         << ", foreground is dialog: " << ((::GetForegroundWindow() == dlg.m_hWnd) ? 1 : 0));
    CdbShot(dlg.m_hWnd, L"voice_volume");

    //通话窗关闭后浮层随之消失（关窗后 m_hWnd 被清空，先记下句柄）
    const HWND dlgWnd = dlg.m_hWnd;
    dlg.CloseDlg();
    CdbPump(kCdbSettleMs);
    CHECK_FALSE(CdbPopupShowing(dlgWnd));
    CIULog::Uninit();
}

TEST_CASE("CDB2 视频接通态：多出摄像头；最小宽度下按钮不与小画面、时长重叠；共享状态改变共享按钮", "[.][calldlg]")
{
    CdbGdiplus gdiplus;
    FakeCallActions actions;
    CBalloonCallDlg dlg;
    dlg.SetActions(&actions);
    dlg.SetVideoCall(true);
    dlg.OpenCalling(NULL, CString(L"韩晓燕"), kCdbPeer);
    dlg.SetSessionId("cdb-session");
    dlg.OpenConnected(NULL);
    REQUIRE(dlg.IsWindow());
    CdbPump(kCdbSettleMs);
    CdbShot(dlg.m_hWnd, L"video");

    //缩到最小宽度
    dlg.ResizeClient(kCdbMinWidth, call::kCallCanvasH + call::kCallBottomBarH + call::kCallDlgTitleBarH);
    CdbPump(kCdbSettleMs);
    CHECK(CdbClientWidth(dlg) == kCdbMinWidth);
    const UINT order[] = { kCdbMic, kCdbVolume, kCdbCamera, kCdbSwitch, kCdbShare, kCdbSysAudio, kCdbRight };
    int lastRight = kCdbVideoLeftUsed;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
    {
        INFO("ctrlId=" << order[i]);
        balloonwjui::DuiButton* btn = CdbButton(dlg, order[i]);
        REQUIRE(btn != NULL);
        const RECT r = btn->GetRect();
        CHECK(r.left >= lastRight);
        //没有被压扁
        const int expect = (order[i] == kCdbRight) ? call::kCallRoundBtnLargePx : call::kCallRoundBtnPx;
        CHECK(r.right - r.left == expect);
        lastRight = r.right;
    }
    CHECK(lastRight <= kCdbMinWidth - call::kCallSideMargin);
    CdbShot(dlg.m_hWnd, L"video_minwidth");

    //共享屏幕：开始共享后外观改变，结束后恢复
    HBITMAP shareIdle = CdbBitmap(dlg, kCdbShare);
    dlg.SetScreenSharing(true);
    CHECK(CdbBitmap(dlg, kCdbShare) != shareIdle);
    dlg.SetScreenSharing(false);
    CHECK(CdbBitmap(dlg, kCdbShare) == shareIdle);

    //共享电脑声音：打开后外观改变，关闭后恢复（2026-10-06）
    HBITMAP sysIdle = CdbBitmap(dlg, kCdbSysAudio);
    REQUIRE(sysIdle != NULL);
    dlg.SetSystemAudioOn(true);
    CHECK(CdbBitmap(dlg, kCdbSysAudio) != sysIdle);
    CdbShot(dlg.m_hWnd, L"video_sysaudio_on");
    dlg.SetSystemAudioOn(false);
    CHECK(CdbBitmap(dlg, kCdbSysAudio) == sysIdle);

    dlg.CloseDlg();
    CdbPump(kCdbSettleMs);
}

TEST_CASE("CDB3 呼叫中只有红色的取消；来电弹窗是红色的拒绝与绿色的接听", "[.][calldlg]")
{
    CdbGdiplus gdiplus;
    FakeCallActions actions;
    CBalloonCallDlg dlg;
    dlg.SetActions(&actions);
    dlg.SetVideoCall(false);
    dlg.OpenCalling(NULL, CString(L"韩晓燕"), kCdbPeer);
    REQUIRE(dlg.IsWindow());
    CdbPump(kCdbSettleMs);
    balloonwjui::DuiButton* cancel = CdbButton(dlg, kCdbRight);
    REQUIRE(cancel != NULL);
    CHECK(cancel->GetBgBitmapForCurrentState() != NULL);
    CHECK(CdbButton(dlg, kCdbMic) == NULL);
    CdbShot(dlg.m_hWnd, L"calling");
    dlg.CloseDlg();
    CdbPump(kCdbSettleMs);

    CBalloonCallDlg incoming;
    incoming.SetActions(&actions);
    incoming.SetVideoCall(true);
    incoming.OpenIncoming(NULL, CString(L"韩晓燕"), "cdb-incoming", kCdbPeer, -1);
    REQUIRE(incoming.IsWindow());
    CdbPump(kCdbSettleMs);
    REQUIRE(CdbButton(incoming, kCdbLeft) != NULL);
    REQUIRE(CdbButton(incoming, kCdbRight) != NULL);
    CHECK(CdbButton(incoming, kCdbLeft)->GetBgBitmapForCurrentState() != NULL);
    CHECK(CdbButton(incoming, kCdbRight)->GetBgBitmapForCurrentState() != NULL);
    CdbShot(incoming.m_hWnd, L"incoming");
    incoming.CloseDlg();
    CdbPump(kCdbSettleMs);
}

TEST_CASE("CDB4 网络信号图标：位置、档位变化、多行悬停详情与降级提示", "[.][calldlg]")
{
    CdbGdiplus gdiplus;
    FakeCallActions actions;
    CBalloonCallDlg dlg;
    dlg.SetActions(&actions);
    dlg.SetVideoCall(true);
    dlg.OpenCalling(NULL, CString(L"韩晓燕"), kCdbPeer);
    dlg.SetSessionId("cdb-session");
    dlg.OpenConnected(NULL);
    REQUIRE(dlg.IsWindow());
    dlg.ResizeClient(kCdbMinWidth, call::kCallCanvasH + call::kCallBottomBarH + call::kCallDlgTitleBarH);
    CdbPump(kCdbSettleMs);

    balloonwjui::DuiControl* root = dlg.GetClientContent();
    REQUIRE(root != NULL);
    call::CallSignalIcon* icon = dynamic_cast<call::CallSignalIcon*>(root->FindCtrlById(kCdbSignal));
    REQUIRE(icon != NULL);
    balloonwjui::DuiToast* toast = dynamic_cast<balloonwjui::DuiToast*>(root->FindCtrlById(kCdbNetToast));
    REQUIRE(toast != NULL);

    //位置：紧挨小画面右边，设计尺寸 20×16，不接收焦点
    const RECT r = icon->GetRect();
    CHECK(r.left == kCdbPipRightUsed);
    CHECK(r.right - r.left == call::kCallSignalIconW);
    CHECK(r.bottom - r.top == call::kCallSignalIconH);
    CHECK_FALSE(icon->IsTabStop());

    //刚接通：快照为默认值，显示「正在检测」
    ::SendMessage(dlg.m_hWnd, WM_TIMER, kCdbDurationTimerId, 0);
    HBITMAP unknownBmp = icon->SignalBitmap();
    CHECK(unknownBmp != NULL);
    CHECK(actions.m_snapshotCalls > 0);
    std::wstring tip = (LPCWSTR)balloonwjui::DuiToolTipMgr::Inst().GetText(icon);
    CHECK(tip.find(L"网络状况：正在检测") == 0);
    CHECK_FALSE(toast->IsActive());

    //对方收得差：图标变为「较差」，悬停详情五行，浮出「对方网络不佳」
    actions.m_snapshot.m_hasRtt = true;
    actions.m_snapshot.m_rttMs = kCdbRttMs;
    actions.m_snapshot.m_myGrade = call::call_grade_good;
    actions.m_snapshot.m_hasMine = true;
    actions.m_snapshot.m_peerReports = 1;
    actions.m_snapshot.m_peerReport.m_grade = call::call_grade_poor;
    actions.m_snapshot.m_peerReport.m_lossPermille = kCdbPeerLossPermille;
    ::SendMessage(dlg.m_hWnd, WM_TIMER, kCdbDurationTimerId, 0);
    HBITMAP poorBmp = icon->SignalBitmap();
    CHECK(poorBmp != NULL);
    CHECK(poorBmp != unknownBmp);
    tip = (LPCWSTR)balloonwjui::DuiToolTipMgr::Inst().GetText(icon);
    CHECK(tip.find(L"网络状况：较差\n") == 0);
    CHECK(tip.find(L"往返时延：34 ms") != std::wstring::npos);
    CHECK(tip.find(L"对方收我：丢包 12.5%") != std::wstring::npos);
    CHECK(std::count(tip.begin(), tip.end(), L'\n') == kCdbDetailLineBreaks);
    CHECK(toast->IsActive());
    CdbShot(dlg.m_hWnd, L"netsignal_poor");

    //恢复良好：图标变为「良好」
    actions.m_snapshot.m_peerReport.m_grade = call::call_grade_good;
    ::SendMessage(dlg.m_hWnd, WM_TIMER, kCdbDurationTimerId, 0);
    CHECK(icon->SignalBitmap() != poorBmp);
    tip = (LPCWSTR)balloonwjui::DuiToolTipMgr::Inst().GetText(icon);
    CHECK(tip.find(L"网络状况：良好") == 0);
    CdbShot(dlg.m_hWnd, L"netsignal_good");

    dlg.CloseDlg();
    CdbPump(kCdbSettleMs);
}

TEST_CASE("CDB5 视频态操作条浮在画面上：静止后淡出、鼠标一动淡入、停在操作条上保持显示", "[.][calldlg]")
{
    CdbGdiplus gdiplus;
    FakeCallActions actions;
    CBalloonCallDlg dlg;
    dlg.SetActions(&actions);
    dlg.SetVideoCall(true);
    dlg.OpenCalling(NULL, CString(L"韩晓燕"), kCdbPeer);
    dlg.SetSessionId("cdb-session");
    dlg.OpenConnected(NULL);
    REQUIRE(dlg.IsWindow());
    dlg.ResizeClient(call::kCallDlgWidth, call::kCallCanvasH + call::kCallBottomBarH + call::kCallDlgTitleBarH);
    CdbPump(kCdbSettleMs);

    balloonwjui::DuiControl* root = dlg.GetClientContent();
    REQUIRE(root != NULL);
    call::CallOverlayBar* bar = dynamic_cast<call::CallOverlayBar*>(root->FindCtrlById(kCdbOverlayBar));
    REQUIRE(bar != NULL);
    call::CallVideoCanvas* big = dynamic_cast<call::CallVideoCanvas*>(root->FindCtrlById(kCdbBig));
    REQUIRE(big != NULL);
    balloonwjui::DuiControl* pip = root->FindCtrlById(kCdbPip);
    REQUIRE(pip != NULL);
    balloonwjui::DuiButton* hangup = CdbButton(dlg, kCdbRight);
    REQUIRE(hangup != NULL);

    //大画面铺满客户区的内容部分（标题栏以下），操作条浮在它的底部，小画面浮在左下角
    const RECT content = root->GetRect();
    const RECT bigRect = big->GetRect();
    const RECT barRect = bar->GetRect();
    const RECT pipRect = pip->GetRect();
    CHECK(bigRect.bottom == content.bottom);
    CHECK(barRect.bottom == content.bottom);
    CHECK(barRect.bottom - barRect.top == call::kCallBottomBarH);
    CHECK(barRect.left == content.left);
    CHECK(barRect.right == content.right);
    CHECK(pipRect.left == content.left + call::kCallSideMargin);
    CHECK(pipRect.bottom == content.bottom - kCdbPipBottomGap);
    CHECK(pipRect.right - pipRect.left == kCdbPipW);
    CHECK(pipRect.bottom - pipRect.top == kCdbPipH);
    CHECK(bar->BarAlpha() == call::kCallBarAlphaOpaque);

    //右上角的时长胶囊
    ::SendMessage(dlg.m_hWnd, WM_TIMER, kCdbDurationTimerId, 0);
    CHECK_FALSE(big->CornerText().IsEmpty());

    //鼠标停在画面上（不在操作条上）后静止：淡出，完全隐藏后不再参与命中
    const LPARAM onCanvas = MAKELPARAM(kCdbAwayX, kCdbAwayY);
    ::SendMessage(dlg.m_hWnd, WM_MOUSEMOVE, 0, onCanvas);
    CdbPump(kCdbIdleWaitMs);
    CHECK(bar->BarAlpha() == 0);
    const RECT hangupRect = hangup->GetRect();
    POINT hangupCenter;
    hangupCenter.x = (hangupRect.left + hangupRect.right) / 2;
    hangupCenter.y = (hangupRect.top + hangupRect.bottom) / 2;
    CHECK(root->HitTest(hangupCenter) != static_cast<balloonwjui::DuiControl*>(hangup));
    CHECK(root->HitTest(hangupCenter) == static_cast<balloonwjui::DuiControl*>(big));
    //小画面不随操作条淡出
    POINT pipCenter;
    pipCenter.x = (pipRect.left + pipRect.right) / 2;
    pipCenter.y = (pipRect.top + pipRect.bottom) / 2;
    CHECK(root->HitTest(pipCenter) == pip);
    CdbShot(dlg.m_hWnd, L"overlay_hidden");

    //鼠标一动：淡入
    ::SendMessage(dlg.m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(kCdbAwayX + 1, kCdbAwayY));
    CdbPump(kCdbFadeWaitMs);
    CHECK(bar->BarAlpha() == call::kCallBarAlphaOpaque);
    CHECK(root->HitTest(hangupCenter) == static_cast<balloonwjui::DuiControl*>(hangup));
    CdbShot(dlg.m_hWnd, L"overlay_shown");

    //鼠标离开窗口：静止时长到点后淡出
    ::SendMessage(dlg.m_hWnd, WM_MOUSELEAVE, 0, 0);
    CdbPump(kCdbIdleWaitMs);
    CHECK(bar->BarAlpha() == 0);

    dlg.CloseDlg();
    CdbPump(kCdbSettleMs);
}
