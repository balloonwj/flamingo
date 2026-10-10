/**
 *  多人共享（第 22 期）已知限制的修正（标签 [sharehosttransfer]），ShareHostTransferTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.3 节（2026-10-07 用户决定四项都修）。本文件覆盖第 2～4 项：
 *    - HTR1 「主持人 ▾」菜单的各项：修改标题与描述、全员静音、分隔线、允许他人录制、允许他人标注（后两项按房间权限打勾）、
 *           分隔线、允许截图（2026-10-08，按本机开关打勾）；
 *    - HTR2 本人是不是「经移交成为主持人、仍在观看窗里」的主持人：只在观看中、快照是这一场、主持人是本人、本人没在推流时成立；
 *    - HTR3 收到 1726 怎么处理：发起一侧优先；正在观看的房间又收到 1726（新主持人掉线后重新登录）时按重连处理；
 *    - HTR4 源文件守卫（控制器）：观看端套用 1725 后刷新主持人入口、强制模式不约束主持人；名单与全员静音按当前房间、
 *           对经移交成为主持人的人开放；onRelayInfo 按 HTR3 的决定重连观看窗；
 *    - HTR5 源文件守卫（观看窗）：全屏工具条上「主持人 ▾」的判断排在「共享说明」之前（没有共享说明时两者下标相同）；
 *    - HTR6 标注输入面：主持人在「其他人的屏幕」窗口里标注时，由那个窗口的画面接住输入、工具条锚在那个窗口上；
 *           白板优先；不是主持人时不起作用；
 *    - HTR7 「其他人的屏幕」窗口：顶部两个按钮把命令投给控制器，文字随标注与控制状态变化；标注或控制中关窗时先请控制器退出；
 *    - HTR8 控制中转发键鼠（ShareControlForwarder，观看窗与「其他人的屏幕」窗口共用）：键盘照常转发并记下按着的键，
 *           结束时补发抬起；画面以外、又没有按着按钮的鼠标消息不拦；
 *    - HTR9 源文件守卫（控制器）：主持人也走「控制别人」一侧的逻辑，输入经推流管线发出，申请对象取「其他人的屏幕」窗口
 *           当前那一块。
 *    - HTR10 源文件守卫（控制器）：收到「换了主持人」（1725 事件 14）后，观看中的成员重新查询这个会话的共享状态，
 *            让聊天窗卡片与横幅上的发起人换成新主持人（1725 不带「是否需要密码」，不能直接上报）；
 *    - HTR11 源文件守卫（控制器与控制条）：主持人开始推流、显示控制条之后按企业开关设置「云端录制可用」，
 *            只改是否可用、不动录制状态，主持人独自一人时「更多」菜单里也有「开始云端录制」。
 *    - HTR12 观看窗在画面清单从有到无时给出提示：画面控件记下「有过画面、现在一块都没有」，从来没有设过清单时不算；
 *            源文件守卫：观看窗换清单后刷新提示，「目前没有其他人在共享屏幕」排在暂停、最小化、窗口关闭这几种提示之后；
 *    - HTR13 观看者名单「允许控制」能不能用：原主持人推流中、经移交成为主持人且经共享者管线推流中可用，
 *            原主持人停掉了自己的画面、经移交的主持人没在推流、不是主持人时不可用；源文件守卫：名单与点击处理都按它判断。
 *  第 3 项的服务端部分（新主持人掉线后房间保留、重新登录时按原座位签令牌）见 flamingoserver/tests/test_screenshare_multi.cpp
 *  的 SSMS9。源文件守卫在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"
#include "../../FlamingoMedia/Share/ShareAnnotModePolicy.h"
#include "../../FlamingoMedia/Share/ShareScreensWindow.h"
#include "../../FlamingoMedia/Share/ShareViewerWindow.h"
#include "../../FlamingoMedia/Share/ShareMessages.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//读文件时每次读的字节数、本文件在 flamingoclient 下的层数
const size_t kHtrReadChunk = 4096;
const int kHtrDepth = 3;
//测试用的房间号与用户
const char* const kHtrRoom = "room-htr";
const char* const kHtrOtherRoom = "room-other";
const int64_t kHtrSelf = 5001;
const int64_t kHtrOther = 5002;
//测试用的按键（字母 A 的虚拟键码）
const UINT kHtrKeyA = 'A';

std::string HtrReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kHtrReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string HtrClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kHtrDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//取一个函数的函数体：从签名开始，到下一个以 nextPrefix 开头的行为止
std::string HtrFunctionBody(const std::string& source, const std::string& signature, const std::string& nextPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + nextPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

HWND HtrNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//把通知窗口里已有的消息全部取走
void HtrDrain(HWND notify)
{
    MSG msg;
    while (::PeekMessageW(&msg, notify, 0, 0, PM_REMOVE))
    {
    }
}

//取走通知窗口里的 kMsgShareScreensCommand，按顺序返回各条的 wParam
std::vector<WPARAM> HtrTakeCommands(HWND notify)
{
    std::vector<WPARAM> out;
    MSG msg;
    while (::PeekMessageW(&msg, notify, kMsgShareScreensCommand, kMsgShareScreensCommand, PM_REMOVE))
        out.push_back(msg.wParam);
    return out;
}

void HtrClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

std::wstring HtrButtonText(CShareScreensWindow& wnd, UINT id)
{
    balloonwjui::DuiControl* content = wnd.GetClientContent();
    balloonwjui::DuiButton* btn =
        content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
    return btn != NULL ? std::wstring(btn->GetText().GetString()) : std::wstring();
}

//没有画面的画面来源
class HtrEmptyFeed : public IShareScreenFeed
{
public:
    virtual bool GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                uint64_t& serial) const override
    {
        (void)seat;
        (void)bgra;
        (void)width;
        (void)height;
        (void)serial;
        return false;
    }
    virtual std::vector<int> ScreenSeats() const override
    {
        return std::vector<int>();
    }
    virtual int ScreenSourceState(int seat) const override
    {
        (void)seat;
        return 0;
    }
};

//记下收到的控制输入
class HtrRecordingInput : public IShareControlInput
{
public:
    virtual void OnControlInput(const ShareInputEvent& ev) override
    {
        m_events.push_back(ev);
    }

    std::vector<ShareInputEvent> m_events;   //按收到的顺序
};

} // namespace

TEST_CASE("HTR1 host menu items", "[sharehosttransfer]")
{
    std::vector<ShareHostMenuItem> items;
    BuildShareHostMenu(true, false, false, items);
    REQUIRE(items.size() == 5);
    CHECK(items[0].m_id == share_host_menu_edit_info);
    CHECK(items[1].m_id == share_host_menu_mute_all);
    CHECK(items[2].m_id == share_host_menu_record_perm);
    CHECK(items[3].m_id == share_host_menu_annot_perm);
    CHECK(items[4].m_id == share_host_menu_allow_capture);
    for (size_t i = 0; i < items.size(); ++i)
        CHECK_FALSE(items[i].m_text.empty());
    //前两项是动作，后两项是勾选项，中间画一条分隔线
    CHECK_FALSE(items[0].m_checkable);
    CHECK_FALSE(items[1].m_checkable);
    CHECK(items[2].m_checkable);
    CHECK(items[3].m_checkable);
    CHECK(items[2].m_separatorBefore);
    CHECK_FALSE(items[3].m_separatorBefore);
    //勾选状态跟着房间权限
    CHECK(items[2].m_checked);
    CHECK_FALSE(items[3].m_checked);
    //「允许截图」（2026-10-08）：勾选项，前面画分隔线，勾选状态跟着本机开关
    CHECK(items[4].m_checkable);
    CHECK(items[4].m_separatorBefore);
    CHECK_FALSE(items[4].m_checked);
    BuildShareHostMenu(false, true, true, items);
    REQUIRE(items.size() == 5);
    CHECK_FALSE(items[2].m_checked);
    CHECK(items[3].m_checked);
    CHECK(items[4].m_checked);
}

TEST_CASE("HTR2 transferred host detection", "[sharehosttransfer]")
{
    CHECK(IsTransferredShareHost(false, true, kHtrRoom, kHtrRoom, kHtrSelf, kHtrSelf));
    //自己在推流（建房间的主持人有控制条）
    CHECK_FALSE(IsTransferredShareHost(true, true, kHtrRoom, kHtrRoom, kHtrSelf, kHtrSelf));
    //不在观看
    CHECK_FALSE(IsTransferredShareHost(false, false, kHtrRoom, kHtrRoom, kHtrSelf, kHtrSelf));
    //快照不是这一场
    CHECK_FALSE(IsTransferredShareHost(false, true, kHtrRoom, kHtrOtherRoom, kHtrSelf, kHtrSelf));
    CHECK_FALSE(IsTransferredShareHost(false, true, std::string(), std::string(), kHtrSelf, kHtrSelf));
    //主持人是别人
    CHECK_FALSE(IsTransferredShareHost(false, true, kHtrRoom, kHtrRoom, kHtrOther, kHtrSelf));
    //还不知道本人是谁
    CHECK_FALSE(IsTransferredShareHost(false, true, kHtrRoom, kHtrRoom, 0, 0));
}

TEST_CASE("HTR3 relay info decisions", "[sharehosttransfer]")
{
    const std::string none;
    //发起人：等 1726 时开始推流；推流在跑时不用管；推流停了（宽限期内重新登录）时重新开始
    CHECK(DecideShareRelayInfoAction(kHtrRoom, true, kHtrRoom, true, false, false, false, none) ==
          share_relay_info_start_streaming);
    CHECK(DecideShareRelayInfoAction(kHtrRoom, true, kHtrRoom, false, true, false, false, none) ==
          share_relay_info_host_running);
    CHECK(DecideShareRelayInfoAction(kHtrRoom, true, kHtrRoom, false, false, false, false, none) ==
          share_relay_info_start_streaming);
    //观看者：加入中打开观看窗；观看中又收到同一房间的 1726（新主持人掉线后重新登录）时重连
    CHECK(DecideShareRelayInfoAction(kHtrRoom, false, none, false, false, true, false, kHtrRoom) ==
          share_relay_info_open_viewer);
    CHECK(DecideShareRelayInfoAction(kHtrRoom, false, none, false, false, false, true, kHtrRoom) ==
          share_relay_info_reconnect_viewer);
    //不认识的房间、空房间号、什么都没在做：忽略
    CHECK(DecideShareRelayInfoAction(kHtrOtherRoom, false, none, false, false, false, true, kHtrRoom) ==
          share_relay_info_ignore);
    CHECK(DecideShareRelayInfoAction(std::string(), false, none, false, false, false, true, none) ==
          share_relay_info_ignore);
    CHECK(DecideShareRelayInfoAction(kHtrRoom, false, none, false, false, false, false, kHtrRoom) ==
          share_relay_info_ignore);
    //发起一侧与观看一侧的房间号相同时以发起一侧为准
    CHECK(DecideShareRelayInfoAction(kHtrRoom, true, kHtrRoom, false, true, false, true, kHtrRoom) ==
          share_relay_info_host_running);
}

TEST_CASE("HTR4 controller opens host tools to the transferred host", "[sharehosttransfer]")
{
    const std::string root = HtrClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(src.empty());
    //观看端套用 1725：强制模式不约束主持人，套用成员之后刷新主持人入口
    const std::string state = HtrFunctionBody(src, "void CShareController::onStateNotify(", "void CShareController::");
    REQUIRE_FALSE(state.empty());
    CHECK(state.find("m_viewer.SetForced(n.m_forced && !isRoomHost());") != std::string::npos);
    CHECK(state.find("refreshHostTools();") != std::string::npos);
    //名单命令、全员静音、修改标题与描述：经移交成为主持人的人同样可以，房间号取当前这一场
    const std::string list =
        HtrFunctionBody(src, "LRESULT CShareController::OnViewerListCommandMsg(", "LRESULT CShareController::");
    REQUIRE_FALSE(list.empty());
    CHECK(list.find("if (!isRoomHost())") != std::string::npos);
    CHECK(list.find("m_hostRoomId") == std::string::npos);
    const std::string mute = HtrFunctionBody(src, "void CShareController::muteAllViewers(", "void CShareController::");
    REQUIRE_FALSE(mute.empty());
    CHECK(mute.find("if (!isRoomHost())") != std::string::npos);
    CHECK(mute.find("m_hostRoomId") == std::string::npos);
    const std::string info =
        HtrFunctionBody(src, "LRESULT CShareController::OnInfoEditResultMsg(", "void CShareController::");
    REQUIRE_FALSE(info.empty());
    CHECK(info.find("m_hostRoomId") == std::string::npos);
    //1726：按 DecideShareRelayInfoAction 的决定，观看中重连观看窗
    const std::string relay = HtrFunctionBody(src, "void CShareController::onRelayInfo(", "void CShareController::");
    REQUIRE_FALSE(relay.empty());
    CHECK(relay.find("DecideShareRelayInfoAction(") != std::string::npos);
    const size_t reconnect = relay.find("share_relay_info_reconnect_viewer");
    REQUIRE(reconnect != std::string::npos);
    CHECK(relay.find("m_viewer.Reconnect(", reconnect) != std::string::npos);
}

TEST_CASE("HTR5 viewer toolbar checks the host button before the info button", "[sharehosttransfer]")
{
    const std::string root = HtrClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareViewerWindow.cpp");
    REQUIRE_FALSE(src.empty());
    const std::string body = HtrFunctionBody(src, "LRESULT CShareViewerWindow::OnToolbarCmd(", "LRESULT CShareViewerWindow::");
    REQUIRE_FALSE(body.empty());
    const size_t host = body.find("kCmdExtra + hostExtraIndex()");
    const size_t info = body.find("kCmdExtra + kInfoExtraIndex");
    REQUIRE(host != std::string::npos);
    REQUIRE(info != std::string::npos);
    CHECK(host < info);
    //「共享说明」只在有描述时认
    CHECK(body.find("!m_shareDesc.empty() && wParam == CRdViewerToolbar::kCmdExtra + kInfoExtraIndex") != std::string::npos);
}

TEST_CASE("HTR6 annotation surfaces for the screens window", "[sharehosttransfer]")
{
    //主持人在「其他人的屏幕」窗口里标注：那个窗口的画面接住输入，共享内容上的输入层不打开
    ShareAnnotSurfacePlan plan = DecideAnnotSurfaces(true, true, false, false, false, false, true);
    CHECK(plan.m_screensCanvasInput);
    CHECK_FALSE(plan.m_hostOverlayInput);
    CHECK(plan.m_anchor == annot_anchor_screens);
    //「鼠标」状态：工具条仍锚在那个窗口上，但画面不拦截输入
    plan = DecideAnnotSurfaces(true, true, false, false, false, true, true);
    CHECK_FALSE(plan.m_screensCanvasInput);
    CHECK(plan.m_anchor == annot_anchor_screens);
    //白板优先
    plan = DecideAnnotSurfaces(true, true, false, false, true, false, true);
    CHECK(plan.m_boardInput);
    CHECK_FALSE(plan.m_screensCanvasInput);
    CHECK(plan.m_anchor == annot_anchor_board);
    //不是主持人：不起作用
    plan = DecideAnnotSurfaces(true, false, true, false, false, false, true);
    CHECK_FALSE(plan.m_screensCanvasInput);
    CHECK(plan.m_anchor == annot_anchor_viewer);
    //不在标注模式：全部关闭
    plan = DecideAnnotSurfaces(false, true, false, false, false, false, true);
    CHECK_FALSE(plan.m_screensCanvasInput);
    CHECK(plan.m_anchor == annot_anchor_none);
    //不传最后一个参数时与原来相同
    plan = DecideAnnotSurfaces(true, true, false, false, false, false);
    CHECK(plan.m_hostOverlayInput);
    CHECK(plan.m_anchor == annot_anchor_host_source);
}

TEST_CASE("HTR7 screens window buttons and states", "[sharehosttransfer]")
{
    const HWND notify = HtrNotifyWindow();
    REQUIRE(notify != NULL);
    HtrEmptyFeed feed;
    CShareScreensWindow wnd;
    std::vector<int> seats;
    seats.push_back(3);
    std::vector<std::wstring> names;
    names.push_back(L"张三");   //i18n-ignore: 测试数据
    wnd.SetScreens(seats, names);
    wnd.Open(NULL, &feed, notify);
    REQUIRE(wnd.IsWindow());
    CHECK(wnd.ActiveScreenSeat() == 3);
    CHECK_FALSE(wnd.ActiveIsBoard());
    const std::wstring annotIdle = HtrButtonText(wnd, CShareScreensWindow::kIdAnnotate);
    const std::wstring controlIdle = HtrButtonText(wnd, CShareScreensWindow::kIdControl);
    CHECK_FALSE(annotIdle.empty());
    CHECK_FALSE(controlIdle.empty());
    //两个按钮把命令投给控制器
    HtrDrain(notify);
    HtrClick(wnd.m_hWnd, CShareScreensWindow::kIdAnnotate);
    HtrClick(wnd.m_hWnd, CShareScreensWindow::kIdControl);
    std::vector<WPARAM> cmds = HtrTakeCommands(notify);
    REQUIRE(cmds.size() == 2);
    CHECK(cmds[0] == static_cast<WPARAM>(CShareScreensWindow::kCmdAnnotate));
    CHECK(cmds[1] == static_cast<WPARAM>(CShareScreensWindow::kCmdControl));
    //状态由控制器告知：按钮文字随之变化
    wnd.SetAnnotating(true, NULL);
    CHECK(HtrButtonText(wnd, CShareScreensWindow::kIdAnnotate) != annotIdle);
    wnd.SetControlState(share_viewer_control_requested);
    const std::wstring requested = HtrButtonText(wnd, CShareScreensWindow::kIdControl);
    CHECK(requested != controlIdle);
    wnd.SetControlState(share_viewer_control_none);
    CHECK(HtrButtonText(wnd, CShareScreensWindow::kIdControl) == controlIdle);
    //标注中、申请中关窗：先请控制器退出，再隐藏窗口
    wnd.SetControlState(share_viewer_control_requested);
    HtrDrain(notify);
    ::SendMessageW(wnd.m_hWnd, WM_CLOSE, 0, 0);
    CHECK_FALSE(::IsWindowVisible(wnd.m_hWnd));
    cmds = HtrTakeCommands(notify);
    REQUIRE(cmds.size() == 2);
    CHECK(cmds[0] == static_cast<WPARAM>(CShareScreensWindow::kCmdControl));
    CHECK(cmds[1] == static_cast<WPARAM>(CShareScreensWindow::kCmdAnnotate));
    wnd.Close();
    CHECK_FALSE(wnd.IsWindow());
    ::DestroyWindow(notify);
}

TEST_CASE("HTR8 control forwarder", "[sharehosttransfer]")
{
    const HWND notify = HtrNotifyWindow();
    REQUIRE(notify != NULL);
    HtrRecordingInput input;
    ShareControlForwarder forwarder;
    RECT video = { 0, 0, 100, 100 };
    //键盘：转发按下并拦下
    CHECK(forwarder.Handle(notify, WM_KEYDOWN, kHtrKeyA, 0, video, &input));
    REQUIRE(input.m_events.size() == 1);
    CHECK(input.m_events[0].m_message == WM_KEYDOWN);
    CHECK(input.m_events[0].m_vkey == kHtrKeyA);
    //画面以外、又没有按着按钮的鼠标移动：不拦
    CHECK_FALSE(forwarder.Handle(notify, WM_MOUSEMOVE, 0, MAKELPARAM(500, 500), video, &input));
    //结束控制：补发还按着的键的抬起
    forwarder.Release(&input);
    REQUIRE(input.m_events.size() == 2);
    CHECK(input.m_events[1].m_message == WM_KEYUP);
    CHECK(input.m_events[1].m_vkey == kHtrKeyA);
    //已经补发过：再结束一次不重复补发
    forwarder.Release(&input);
    CHECK(input.m_events.size() == 2);
    //没有接收方时不处理
    CHECK_FALSE(forwarder.Handle(notify, WM_KEYDOWN, kHtrKeyA, 0, video, NULL));
    ::DestroyWindow(notify);
}

TEST_CASE("HTR9 host controls other sharers from the screens window", "[sharehosttransfer]")
{
    const std::string root = HtrClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareControllerControl.cpp");
    REQUIRE_FALSE(src.empty());
    //控制关系：主持人不再提前返回
    const std::string apply =
        HtrFunctionBody(src, "void CShareController::applyControlState(", "void CShareController::");
    REQUIRE_FALSE(apply.empty());
    CHECK(apply.find("if (asHost)") == std::string::npos);
    CHECK(apply.find("setControlScreen(controlSeat);") != std::string::npos);
    //输入：主持人经推流管线发出
    const std::string input = HtrFunctionBody(src, "void CShareController::OnControlInput(", "void CShareController::");
    REQUIRE_FALSE(input.empty());
    CHECK(input.find("&m_streamer") != std::string::npos);
    //申请对象：主持人取「其他人的屏幕」窗口当前那一块
    const std::string toggle =
        HtrFunctionBody(src, "void CShareController::toggleViewerControl(", "void CShareController::");
    REQUIRE_FALSE(toggle.empty());
    CHECK(toggle.find("m_screensWindow.ActiveScreenSeat()") != std::string::npos);
    CHECK(toggle.find("m_screensWindow.ActiveIsBoard()") != std::string::npos);
}

TEST_CASE("HTR10 members re-query the conversation after the host changes", "[sharehosttransfer]")
{
    const std::string root = HtrClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareControllerMulti.cpp");
    REQUIRE_FALSE(src.empty());
    const std::string apply =
        HtrFunctionBody(src, "void CShareController::applyMultiState(", "void CShareController::");
    REQUIRE_FALSE(apply.empty());
    //换了主持人的分支里：观看中时按观看的会话重新查询
    const size_t changed = apply.find("n.m_event == share_event_host_changed");
    REQUIRE(changed != std::string::npos);
    const size_t query = apply.find("QueryShare(m_viewerCtype, m_viewerCid);", changed);
    REQUIRE(query != std::string::npos);
    CHECK(apply.find("m_viewerPhase != viewer_idle", changed) < query);
}

TEST_CASE("HTR11 the host's control bar knows cloud recording is available before anyone joins", "[sharehosttransfer]")
{
    const std::string root = HtrClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(src.empty());
    const std::string start =
        HtrFunctionBody(src, "void CShareController::startStreaming(", "void CShareController::");
    REQUIRE_FALSE(start.empty());
    const size_t show = start.find("m_bar.Show(");
    REQUIRE(show != std::string::npos);
    //控制条显示之后按企业开关设置，且用只改「是否可用」的那一个接口
    const size_t avail = start.find("m_bar.SetCloudRecordAvailable(m_host != NULL && m_host->IsCloudRecordEnabled());");
    REQUIRE(avail != std::string::npos);
    CHECK(avail > show);
    CHECK(start.find("m_bar.SetCloudRecordState(") == std::string::npos);
    //控制条：SetCloudRecordAvailable 不改录制状态
    const std::string bar = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareControlBar.cpp");
    REQUIRE_FALSE(bar.empty());
    const std::string body =
        HtrFunctionBody(bar, "void CShareControlBar::SetCloudRecordAvailable(", "void CShareControlBar::");
    REQUIRE_FALSE(body.empty());
    CHECK(body.find("m_cloudRecordAvailable = available;") != std::string::npos);
    CHECK(body.find("m_cloudRecording") == std::string::npos);
}

TEST_CASE("HTR12 the viewer explains when nobody else is sharing any more", "[sharehosttransfer]")
{
    //画面座位与画面尺寸
    const int seatA = 3;
    const int seatB = 4;
    const int frameW = 64;
    const int frameH = 36;
    CShareVideoCanvas canvas;
    //从来没有设过清单（老的单画面路径）：不算
    CHECK_FALSE(canvas.ScreensGone());
    std::vector<int> seats(1, seatA);
    std::vector<std::wstring> names(1, L"A");
    canvas.SetScreens(seats, names);
    std::vector<uint8_t> frame(static_cast<size_t>(frameW) * frameH * 4, 0x80);
    REQUIRE(canvas.SwapScreenFrame(seatA, frame, frameW, frameH));
    CHECK_FALSE(canvas.ScreensGone());
    //清单变空：有过画面、现在一块都没有
    canvas.SetScreens(std::vector<int>(), std::vector<std::wstring>());
    CHECK(canvas.ScreensGone());
    //又有人共享：不再算
    seats[0] = seatB;
    canvas.SetScreens(seats, names);
    CHECK_FALSE(canvas.ScreensGone());
    //源文件守卫：观看窗换清单后刷新提示；新提示排在暂停、最小化、窗口关闭之后，「正在加载画面」之前
    const std::string root = HtrClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareViewerWindow.cpp");
    REQUIRE_FALSE(src.empty());
    const std::string set =
        HtrFunctionBody(src, "void CShareViewerWindow::SetScreens(", "int CShareViewerWindow::");
    REQUIRE_FALSE(set.empty());
    CHECK(set.find("refreshStatus();") != std::string::npos);
    const std::string status = HtrFunctionBody(src, "void CShareViewerWindow::refreshStatus(", "void CShareViewerWindow::");
    REQUIRE_FALSE(status.empty());
    const size_t closed = status.find("screenshare_source_closed");
    const size_t gone = status.find("m_canvas->ScreensGone()");
    const size_t loading = status.find("!m_canvas->HasFrame()");
    REQUIRE(closed != std::string::npos);
    REQUIRE(gone != std::string::npos);
    REQUIRE(loading != std::string::npos);
    CHECK(closed < gone);
    CHECK(gone < loading);
}

TEST_CASE("HTR13 who can hand their screen to a viewer from the viewer list", "[sharehosttransfer]")
{
    //参数：主持人、原主持人推流中、原主持人停掉了自己的画面、经共享者管线推流中
    CHECK(ShareViewerListCanGrantControl(true, true, false, false));
    CHECK(ShareViewerListCanGrantControl(true, false, false, true));
    CHECK_FALSE(ShareViewerListCanGrantControl(true, true, true, false));
    CHECK_FALSE(ShareViewerListCanGrantControl(true, false, false, false));
    CHECK_FALSE(ShareViewerListCanGrantControl(false, false, false, true));
    CHECK_FALSE(ShareViewerListCanGrantControl(false, false, false, false));
    //源文件守卫：名单的「允许控制」与点击后的处理都按它判断
    const std::string root = HtrClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = HtrReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(src.empty());
    const std::string list = HtrFunctionBody(src, "void CShareController::refreshViewerList(", "void CShareController::");
    REQUIRE_FALSE(list.empty());
    CHECK(list.find("ShareViewerListCanGrantControl(") != std::string::npos);
    const size_t cmd = src.find("case CShareViewerListPanel::kCmdControl:");
    REQUIRE(cmd != std::string::npos);
    const size_t next = src.find("case CShareViewerListPanel::kCmdRemove:", cmd);
    REQUIRE(next != std::string::npos);
    const std::string click = src.substr(cmd, next - cmd);
    CHECK(click.find("ShareViewerListCanGrantControl(") != std::string::npos);
    CHECK(click.find("m_hostPhase != host_streaming") == std::string::npos);
}
