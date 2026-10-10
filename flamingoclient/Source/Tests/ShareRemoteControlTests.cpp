/**
 *  屏幕共享中控制键鼠的单元测试（Suite SSKM，标签 [sharectl]），ShareRemoteControlTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareRemoteControl 与 ShareProtocolClient 的控制键鼠部分（docs/屏幕共享与录制方案.md
 *  第 2 节第 24～26 条、第 8 节第 18 期 18b）：
 *    - SSKM1 键鼠输入控制帧：组帧后载荷为 [4][目标座位][输入命令]，经重组器还原后解析出相同的输入；
 *            类型非法、座位越界、命令不完整、控制类型不是输入时不认；
 *    - SSKM2 坐标换算：观看窗里画面以外的点不发送，画面四角对应 0 与最大值；归一化坐标换回显示器（含负坐标的副屏）
 *            与窗口矩形，越界值夹到边缘；换成远程桌面命令后坐标为屏幕绝对坐标；
 *    - SSKM3 系统级按键过滤：Win 键、Alt+Tab、Alt+Esc、Ctrl+Esc、Ctrl+Shift+Esc、Ctrl+Alt+Del 拦下（抬起也拦下），
 *            Ctrl+C、Alt+F4 等普通组合键放行；记下仍按着的键，Reset 后清空；
 *    - SSKM4 注入判定：发送者不是控制者丢弃；只共享窗口时点在窗口外、窗口不在前台时的键盘输入丢弃；系统级按键丢弃；
 *    - SSKM5 本机动静：注入的输入一律不算，本机按键与鼠标按钮立即算，本机鼠标移动累计超过阈值才算；
 *    - SSKM6 信令：1735 的五种动作、1736 的解析、1725 成员的 controlledby 与事件 11 / 12；
 *    - SSKM7 观看端换算窗口消息：画面外按下不发、已按下的按钮松开照发，双击当按下，鼠标移动限频，字符不发；
 *    - SSKM8 观看者名单：发起人每一行都能授予或收回控制，正在控制的那一行标出来、状态文字为「正在控制」。
 *  纯逻辑，不注入、不装钩子。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[sharectl]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareRemoteControl.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/RemoteDesktop/RdInputDef.h"
#include "../../FlamingoMedia/RemoteDesktop/RdInputInjector.h"
#include "../net/Protocol.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace screenshare;

namespace {

//测试用的座位号：控制者 3、画面主人 0
const int kSskmController = 3;
const int kSskmOwner = 0;

//把一个包体经重组器还原成一帧；还原不出时让用例失败
ShareMediaFrame sskmReassemble(const std::string& body)
{
    ShareFrameReassembler r;
    ShareMediaFrame frame;
    REQUIRE(r.push(body.data(), body.size(), frame));
    return frame;
}

//造一个矩形
RECT sskmRect(int l, int t, int r, int b)
{
    RECT rc = { l, t, r, b };
    return rc;
}

//把 JSON 文本解析成 Json::Value
bool sskmParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

} // namespace

TEST_CASE("SSKM1 键鼠输入控制帧：组帧、经重组器还原后解析出相同的输入，格式不对时不认", "[sharectl]")
{
    const ShareInputEvent click = ShareInputEvent::Mouse(WM_LBUTTONDOWN, 1200, 65535, 0);
    CHECK(click.m_type == remotedesktop::rd_input_mouse);
    const std::string body = ShareRemoteInput::BuildInputBody(kSskmController, kSskmOwner, click);
    REQUIRE(body.size() > kShareFrameHeaderSize + 2);
    //载荷前两个字节：控制类型为输入、目标座位（服务端据此只转发给画面主人）
    CHECK((unsigned char)body[0] == screenshare_track_control);
    CHECK((unsigned char)body[kShareFrameHeaderSize] == screenshare_control_input);
    CHECK((unsigned char)body[kShareFrameHeaderSize + 1] == kSskmOwner);

    int target = -1;
    ShareInputEvent got;
    REQUIRE(ShareRemoteInput::ParseInputFrame(sskmReassemble(body), target, got));
    CHECK(target == kSskmOwner);
    CHECK(got.m_type == remotedesktop::rd_input_mouse);
    CHECK(got.m_message == (uint32_t)WM_LBUTTONDOWN);
    CHECK(got.m_x == 1200);
    CHECK(got.m_y == 65535);

    //滚轮带负增量；键盘输入带虚拟键码
    const ShareInputEvent wheel = ShareInputEvent::Mouse(WM_MOUSEWHEEL, 10, 20, -WHEEL_DELTA);
    REQUIRE(ShareRemoteInput::ParseInputFrame(
        sskmReassemble(ShareRemoteInput::BuildInputBody(kSskmController, 7, wheel)), target, got));
    CHECK(target == 7);
    CHECK(got.m_wheel == -WHEEL_DELTA);
    const ShareInputEvent key = ShareInputEvent::Key(WM_KEYDOWN, 'A');
    CHECK(key.m_type == remotedesktop::rd_input_key);
    REQUIRE(ShareRemoteInput::ParseInputFrame(
        sskmReassemble(ShareRemoteInput::BuildInputBody(kSskmController, kSskmOwner, key)), target, got));
    CHECK(got.m_type == remotedesktop::rd_input_key);
    CHECK(got.m_message == (uint32_t)WM_KEYDOWN);
    CHECK(got.m_vkey == (uint32_t)'A');

    //类型非法、目标座位越界：不组帧
    ShareInputEvent bad;
    CHECK(ShareRemoteInput::BuildInputBody(kSskmController, kSskmOwner, bad).empty());
    CHECK(ShareRemoteInput::BuildInputBody(kSskmController, screenshare_seat_server, click).empty());
    CHECK(ShareRemoteInput::BuildInputBody(kSskmController, -1, click).empty());

    //命令不完整：截掉最后一个字节
    std::string truncated = body.substr(0, body.size() - 1);
    CHECK_FALSE(ShareRemoteInput::ParseInputFrame(sskmReassemble(truncated), target, got));
    //控制类型不是输入（共享内容状态帧）
    CHECK_FALSE(ShareRemoteInput::ParseInputFrame(
        sskmReassemble(ShareFraming::buildSourceStateBody(kSskmOwner, 1)), target, got));
}

TEST_CASE("SSKM2 坐标换算：画面以外的点不发送，归一化坐标换回显示器或窗口矩形", "[sharectl]")
{
    //观看窗里画面占 (100,50)-(900,500)，左右留黑边
    const RECT video = sskmRect(100, 50, 900, 500);
    int nx = -1;
    int ny = -1;
    POINT tl = { 100, 50 };
    REQUIRE(ShareRemoteInput::ViewToNormalized(video, tl, nx, ny));
    CHECK(nx == 0);
    CHECK(ny == 0);
    POINT mid = { 500, 275 };
    REQUIRE(ShareRemoteInput::ViewToNormalized(video, mid, nx, ny));
    CHECK(nx == kAnnotCoordMax / 2);
    CHECK(ny == kAnnotCoordMax / 2);
    POINT br = { 899, 499 };
    REQUIRE(ShareRemoteInput::ViewToNormalized(video, br, nx, ny));
    CHECK(nx > kAnnotCoordMax - 100);
    CHECK(ny > kAnnotCoordMax - 200);
    //黑边上、右下边缘上、画面矩形为空：不发送
    POINT black = { 50, 275 };
    CHECK_FALSE(ShareRemoteInput::ViewToNormalized(video, black, nx, ny));
    POINT edge = { 900, 275 };
    CHECK_FALSE(ShareRemoteInput::ViewToNormalized(video, edge, nx, ny));
    CHECK_FALSE(ShareRemoteInput::ViewToNormalized(sskmRect(10, 10, 10, 10), tl, nx, ny));

    //主屏 1920×1080：0 与最大值对应左上角与右下角像素
    const RECT primary = sskmRect(0, 0, 1920, 1080);
    POINT p = ShareRemoteInput::NormalizedToSource(primary, 0, 0);
    CHECK(p.x == 0);
    CHECK(p.y == 0);
    p = ShareRemoteInput::NormalizedToSource(primary, kAnnotCoordMax, kAnnotCoordMax);
    CHECK(p.x == 1919);
    CHECK(p.y == 1079);
    p = ShareRemoteInput::NormalizedToSource(primary, kAnnotCoordMax / 2, kAnnotCoordMax / 2);
    CHECK(p.x == 960);
    CHECK(p.y == 540);
    //主屏左边的副屏（负坐标）
    const RECT left = sskmRect(-1280, -200, 0, 824);
    p = ShareRemoteInput::NormalizedToSource(left, 0, kAnnotCoordMax);
    CHECK(p.x == -1280);
    CHECK(p.y == 823);
    //只共享一个窗口：换到窗口矩形里；越界值夹到边缘
    const RECT window = sskmRect(300, 200, 1100, 800);
    p = ShareRemoteInput::NormalizedToSource(window, kAnnotCoordMax / 2, 0);
    CHECK(p.x == 700);
    CHECK(p.y == 200);
    p = ShareRemoteInput::NormalizedToSource(window, -50, 999999);
    CHECK(p.x == 300);
    CHECK(p.y == 799);

    //换成远程桌面命令：鼠标坐标为屏幕绝对坐标，RdInputInjector 能解析
    POINT screen = { -640, 300 };
    const std::string cmd = ShareRemoteInput::ToRdCommand(ShareInputEvent::Mouse(WM_RBUTTONUP, 1, 2, 0), screen);
    remotedesktop::RdInputCommand rd;
    REQUIRE(remotedesktop::RdInputInjector::parse((const BYTE*)cmd.data(), cmd.size(), rd));
    CHECK(rd.type == remotedesktop::rd_input_mouse);
    CHECK(rd.message == (uint32_t)WM_RBUTTONUP);
    CHECK(rd.x == -640);
    CHECK(rd.y == 300);
    const std::string keyCmd = ShareRemoteInput::ToRdCommand(ShareInputEvent::Key(WM_KEYUP, VK_RETURN), screen);
    REQUIRE(remotedesktop::RdInputInjector::parse((const BYTE*)keyCmd.data(), keyCmd.size(), rd));
    CHECK(rd.type == remotedesktop::rd_input_key);
    CHECK(rd.vkey == (uint32_t)VK_RETURN);
    CHECK(ShareRemoteInput::ToRdCommand(ShareInputEvent(), screen).empty());
}

TEST_CASE("SSKM3 系统级按键过滤：Win 键与系统组合键拦下，普通组合键放行，记下仍按着的键", "[sharectl]")
{
    ShareInputKeyFilter f;
    //普通按键
    CHECK(f.Allow(WM_KEYDOWN, 'A'));
    CHECK(f.Allow(WM_KEYUP, 'A'));
    //Ctrl+C
    CHECK(f.Allow(WM_KEYDOWN, VK_CONTROL));
    CHECK(f.Allow(WM_KEYDOWN, 'C'));
    CHECK(f.Allow(WM_KEYUP, 'C'));
    CHECK(f.Allow(WM_KEYUP, VK_CONTROL));
    //Alt+F4 是普通组合键（关掉前台窗口），放行
    CHECK(f.Allow(WM_SYSKEYDOWN, VK_MENU));
    CHECK(f.Allow(WM_SYSKEYDOWN, VK_F4));
    CHECK(f.Allow(WM_SYSKEYUP, VK_F4));
    //Alt+Tab、Alt+Esc：拦下，抬起也拦下；Alt 本身照常
    CHECK_FALSE(f.Allow(WM_SYSKEYDOWN, VK_TAB));
    CHECK_FALSE(f.Allow(WM_SYSKEYUP, VK_TAB));
    CHECK_FALSE(f.Allow(WM_SYSKEYDOWN, VK_ESCAPE));
    CHECK_FALSE(f.Allow(WM_SYSKEYUP, VK_ESCAPE));
    CHECK(f.Allow(WM_KEYUP, VK_MENU));
    //Alt 抬起后 Tab 是普通按键
    CHECK(f.Allow(WM_KEYDOWN, VK_TAB));
    CHECK(f.Allow(WM_KEYUP, VK_TAB));
    //Win 键：左右都拦下
    CHECK_FALSE(f.Allow(WM_KEYDOWN, VK_LWIN));
    CHECK_FALSE(f.Allow(WM_KEYUP, VK_LWIN));
    CHECK_FALSE(f.Allow(WM_KEYDOWN, VK_RWIN));
    //Ctrl+Esc（开始菜单）、Ctrl+Shift+Esc（任务管理器）
    CHECK(f.Allow(WM_KEYDOWN, VK_CONTROL));
    CHECK_FALSE(f.Allow(WM_KEYDOWN, VK_ESCAPE));
    CHECK_FALSE(f.Allow(WM_KEYUP, VK_ESCAPE));
    CHECK(f.Allow(WM_KEYDOWN, VK_SHIFT));
    CHECK_FALSE(f.Allow(WM_KEYDOWN, VK_ESCAPE));
    CHECK_FALSE(f.Allow(WM_KEYUP, VK_ESCAPE));
    //Ctrl+Alt+Del
    CHECK(f.Allow(WM_KEYDOWN, VK_MENU));
    CHECK_FALSE(f.Allow(WM_KEYDOWN, VK_DELETE));
    CHECK_FALSE(f.Allow(WM_KEYUP, VK_DELETE));
    //不是键盘消息：拦下
    CHECK_FALSE(f.Allow(WM_CHAR, 'x'));

    //仍按着的键：Ctrl、Shift、Alt（按按下先后），被拦下的键不算
    std::vector<uint32_t> held = f.HeldKeys();
    REQUIRE(held.size() == 3);
    CHECK(held[0] == (uint32_t)VK_CONTROL);
    CHECK(held[1] == (uint32_t)VK_SHIFT);
    CHECK(held[2] == (uint32_t)VK_MENU);
    //只抬起没按下的键（控制开始前就按着）：放行，不影响记录
    CHECK(f.Allow(WM_KEYUP, 'Z'));
    CHECK(f.HeldKeys().size() == 3);
    f.Reset();
    CHECK(f.HeldKeys().empty());
    //Reset 之后修饰键状态也清空：Esc 是普通按键
    CHECK(f.Allow(WM_KEYDOWN, VK_ESCAPE));
}

TEST_CASE("SSKM4 注入判定：只注入当前控制者的输入，只共享窗口时限制在窗口内与前台", "[sharectl]")
{
    ShareInputKeyFilter f;
    const ShareInputEvent move = ShareInputEvent::Mouse(WM_MOUSEMOVE, 100, 100, 0);
    const ShareInputEvent key = ShareInputEvent::Key(WM_KEYDOWN, 'A');
    const ShareInputEvent win = ShareInputEvent::Key(WM_KEYDOWN, VK_LWIN);

    //共享整块屏幕
    CHECK(DecideShareInput(move, kSskmController, kSskmController, false, false, false, f) == share_input_inject);
    CHECK(DecideShareInput(key, kSskmController, kSskmController, false, false, false, f) == share_input_inject);
    CHECK(DecideShareInput(win, kSskmController, kSskmController, false, false, false, f) == share_input_system_key);
    //发送者不是控制者、没人在控制
    CHECK(DecideShareInput(move, 5, kSskmController, false, true, true, f) == share_input_not_controller);
    CHECK(DecideShareInput(key, kSskmController, -1, false, true, true, f) == share_input_not_controller);

    //只共享一个窗口：鼠标点在窗口外丢弃；键盘只在窗口在前台时注入
    CHECK(DecideShareInput(move, kSskmController, kSskmController, true, true, false, f) == share_input_inject);
    CHECK(DecideShareInput(move, kSskmController, kSskmController, true, false, true, f) ==
          share_input_outside_window);
    CHECK(DecideShareInput(key, kSskmController, kSskmController, true, true, false, f) ==
          share_input_not_foreground);
    CHECK(DecideShareInput(key, kSskmController, kSskmController, true, false, true, f) == share_input_inject);

    //被丢弃的键盘输入不改过滤器状态：不在前台时按下的 Ctrl 不算按着
    ShareInputKeyFilter g;
    const ShareInputEvent ctrl = ShareInputEvent::Key(WM_KEYDOWN, VK_CONTROL);
    CHECK(DecideShareInput(ctrl, kSskmController, kSskmController, true, true, false, g) ==
          share_input_not_foreground);
    CHECK(g.HeldKeys().empty());
}

TEST_CASE("SSKM5 本机动静：注入的输入不算，本机按键与按钮立即算，鼠标移动累计超过阈值才算", "[sharectl]")
{
    ShareLocalActivityDetector d;
    POINT p = { 500, 500 };
    //注入的输入一律不算，哪怕移动很远
    CHECK_FALSE(d.OnMouseMove(p, true));
    POINT distant = { 1500, 900 };
    CHECK_FALSE(d.OnMouseMove(distant, true));
    CHECK_FALSE(d.OnMouseButton(true));
    CHECK_FALSE(d.OnKey(true));

    //本机鼠标轻微移动（累计不超过阈值）不算
    const int step = ShareLocalActivityDetector::kShareLocalMoveThresholdPx / 4;
    POINT q = { 500, 500 };
    CHECK_FALSE(d.OnMouseMove(q, false));
    q.x += step;
    CHECK_FALSE(d.OnMouseMove(q, false));
    q.y += step;
    CHECK_FALSE(d.OnMouseMove(q, false));
    //继续移动，累计超过阈值：算，此后一直为 true
    q.x += ShareLocalActivityDetector::kShareLocalMoveThresholdPx;
    CHECK(d.OnMouseMove(q, false));
    CHECK(d.OnMouseMove(q, true));

    //Reset 之后：本机鼠标按钮、按键立即算
    d.Reset();
    CHECK_FALSE(d.OnMouseMove(q, false));
    CHECK(d.OnMouseButton(false));
    d.Reset();
    CHECK(d.OnKey(false));

    //注入时把光标挪到的位置：系统可能把这次挪动报成不带注入标志的移动，落在这个位置上的移动不算本机操作，
    //此后从这个位置起算位移
    d.Reset();
    POINT start = { 100, 100 };
    CHECK_FALSE(d.OnMouseMove(start, false));
    POINT injectedAt = { 900, 700 };
    d.NoteInjectedPosition(injectedAt);
    CHECK_FALSE(d.OnMouseMove(injectedAt, false));
    POINT nudge = { 905, 700 };
    CHECK_FALSE(d.OnMouseMove(nudge, false));
    //离开那个位置之后照常累计
    POINT moved = { 905 + ShareLocalActivityDetector::kShareLocalMoveThresholdPx, 700 };
    CHECK(d.OnMouseMove(moved, false));
}

TEST_CASE("SSKM7 观看端换算窗口消息：画面外按下不发、松开照发，双击当按下，鼠标移动限频，字符不发", "[sharectl]")
{
    ShareViewerInputTranslator t;
    const RECT video = sskmRect(0, 0, 1000, 500);
    ShareInputEvent ev;
    POINT in = { 500, 250 };
    POINT out = { 1200, 600 };

    //画面外按下：不发；没发过按下的松开：不发
    CHECK_FALSE(t.Translate(WM_LBUTTONDOWN, MK_LBUTTON, out, video, 0, ev));
    CHECK_FALSE(t.Translate(WM_LBUTTONUP, 0, in, video, 1, ev));
    //画面内按下：发，坐标归一化
    REQUIRE(t.Translate(WM_LBUTTONDOWN, MK_LBUTTON, in, video, 2, ev));
    CHECK(ev.m_type == remotedesktop::rd_input_mouse);
    CHECK(ev.m_message == (uint32_t)WM_LBUTTONDOWN);
    CHECK(ev.m_x == kAnnotCoordMax / 2);
    CHECK(ev.m_y == kAnnotCoordMax / 2);
    REQUIRE(t.PendingButtonUps().size() == 1);
    CHECK(t.PendingButtonUps()[0] == (uint32_t)WM_LBUTTONUP);
    //拖到画面外松开：照发，坐标夹到右下边缘
    REQUIRE(t.Translate(WM_LBUTTONUP, 0, out, video, 3, ev));
    CHECK(ev.m_message == (uint32_t)WM_LBUTTONUP);
    CHECK(ev.m_x > kAnnotCoordMax - 100);
    CHECK(ev.m_y > kAnnotCoordMax - 200);
    CHECK(t.PendingButtonUps().empty());

    //双击当作按下
    REQUIRE(t.Translate(WM_RBUTTONDBLCLK, MK_RBUTTON, in, video, 4, ev));
    CHECK(ev.m_message == (uint32_t)WM_RBUTTONDOWN);
    REQUIRE(t.Translate(WM_RBUTTONUP, 0, in, video, 5, ev));
    CHECK(ev.m_message == (uint32_t)WM_RBUTTONUP);

    //鼠标移动：第一次发；不足间隔不发；位置不变不发；过了间隔且位置变了才发；画面外不发
    POINT a = { 100, 100 };
    POINT b = { 110, 100 };
    POINT c = { 120, 100 };
    REQUIRE(t.Translate(WM_MOUSEMOVE, 0, a, video, 100, ev));
    CHECK(ev.m_message == (uint32_t)WM_MOUSEMOVE);
    CHECK_FALSE(t.Translate(WM_MOUSEMOVE, 0, b, video, 100 + ShareViewerInputTranslator::kShareMoveIntervalMs - 1, ev));
    CHECK_FALSE(t.Translate(WM_MOUSEMOVE, 0, a, video, 200, ev));
    REQUIRE(t.Translate(WM_MOUSEMOVE, 0, c, video, 200, ev));
    CHECK_FALSE(t.Translate(WM_MOUSEMOVE, 0, out, video, 300, ev));

    //滚轮：带增量，不限频
    REQUIRE(t.Translate(WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), in, video, 201, ev));
    CHECK(ev.m_message == (uint32_t)WM_MOUSEWHEEL);
    CHECK(ev.m_wheel == -WHEEL_DELTA);
    //画面外的滚轮不发
    CHECK_FALSE(t.Translate(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), out, video, 202, ev));

    //键盘：按下与抬起原样发（含系统键），字符不发
    REQUIRE(t.Translate(WM_SYSKEYDOWN, VK_MENU, in, video, 300, ev));
    CHECK(ev.m_type == remotedesktop::rd_input_key);
    CHECK(ev.m_message == (uint32_t)WM_SYSKEYDOWN);
    CHECK(ev.m_vkey == (uint32_t)VK_MENU);
    REQUIRE(t.Translate(WM_KEYUP, 'Q', out, video, 301, ev));
    CHECK(ev.m_vkey == (uint32_t)'Q');
    CHECK_FALSE(t.Translate(WM_CHAR, 'q', in, video, 302, ev));
    CHECK_FALSE(t.Translate(WM_PAINT, 0, in, video, 303, ev));

    //中键按下未松开：记在待补发的松开里；Reset 清空
    REQUIRE(t.Translate(WM_MBUTTONDOWN, MK_MBUTTON, in, video, 400, ev));
    REQUIRE(t.PendingButtonUps().size() == 1);
    CHECK(t.PendingButtonUps()[0] == (uint32_t)WM_MBUTTONUP);
    t.Reset();
    CHECK(t.PendingButtonUps().empty());

    //控制中要拦下的消息
    CHECK(ShareViewerInputTranslator::IsControlMessage(WM_SYSKEYUP));
    CHECK(ShareViewerInputTranslator::IsControlMessage(WM_SYSCHAR));
    CHECK(ShareViewerInputTranslator::IsControlMessage(WM_CHAR));
    CHECK(ShareViewerInputTranslator::IsControlMessage(WM_MOUSEWHEEL));
    CHECK(ShareViewerInputTranslator::IsControlMessage(WM_LBUTTONDBLCLK));
    CHECK_FALSE(ShareViewerInputTranslator::IsControlMessage(WM_PAINT));
    CHECK_FALSE(ShareViewerInputTranslator::IsControlMessage(WM_TIMER));
}

TEST_CASE("SSKM8 观看者名单：发起人每一行都能授予或收回控制，正在控制的那一行标出来；观看者看到的名单没有这一项", "[sharectl]")
{
    ShareStateNotify s;
    s.m_hostId = 5127;
    ShareMember host;
    host.m_userId = 5127;
    host.m_seat = 0;
    host.m_controlledBy = 5310;
    ShareMember a;
    a.m_userId = 5308;
    a.m_seat = 1;
    ShareMember b;
    b.m_userId = 5310;
    b.m_seat = 2;
    s.m_members.push_back(host);
    s.m_members.push_back(a);
    s.m_members.push_back(b);
    const std::map<int64_t, std::wstring> names;
    const std::set<int> speaking;

    std::vector<ShareViewerRow> rows;
    BuildShareViewerRows(s, names, speaking, true, rows);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_userId == 5308);
    CHECK(rows[0].m_canControl);
    CHECK_FALSE(rows[0].m_controlling);
    CHECK(rows[1].m_userId == 5310);
    CHECK(rows[1].m_canControl);
    CHECK(rows[1].m_controlling);

    //观看者看到的名单：不能授予，但仍标出谁在控制（名单里不显示按钮，只供状态文字使用）
    BuildShareViewerRows(s, names, speaking, false, rows);
    REQUIRE(rows.size() == 2);
    CHECK_FALSE(rows[0].m_canControl);
    CHECK_FALSE(rows[1].m_canControl);
    CHECK(rows[1].m_controlling);
    //正在控制的人状态文字为「正在控制」
    CHECK(ShareViewerStatusText(rows[1]) == L"正在控制");

    //没人控制：都不标
    s.m_members[0].m_controlledBy = 0;
    BuildShareViewerRows(s, names, speaking, true, rows);
    CHECK_FALSE(rows[0].m_controlling);
    CHECK_FALSE(rows[1].m_controlling);
}

TEST_CASE("SSKM6 控制键鼠的信令：1735 的五种动作、1736 的解析、1725 成员的 controlledby", "[sharectl]")
{
    Json::Value root;
    //授予带 target；申请不带
    REQUIRE(sskmParseJson(ShareProtocolClient::buildControl("ss-1", share_control_grant, 5127, 5308), root));
    CHECK(root["roomid"].asString() == "ss-1");
    CHECK(root["action"].asInt() == 3);
    CHECK(root["owner"].asString() == "5127");
    CHECK(root["target"].asString() == "5308");
    REQUIRE(sskmParseJson(ShareProtocolClient::buildControl("ss-1", share_control_request, 5127, 0), root));
    CHECK(root["action"].asInt() == 1);
    CHECK_FALSE(root.isMember("target"));

    //1736
    ShareControlNotify n;
    REQUIRE(ShareProtocolClient::parseControlNotify(
        u8"{\"roomid\":\"ss-1\",\"action\":1,\"owner\":\"5127\",\"actor\":\"5308\",\"actorname\":\"李四\"}", n));
    CHECK(n.m_roomId == "ss-1");
    CHECK(n.m_action == share_control_request);
    CHECK(n.m_owner == 5127);
    CHECK(n.m_actor == 5308);
    CHECK(n.m_actorName == u8"李四");
    CHECK_FALSE(ShareProtocolClient::parseControlNotify("{\"roomid\":\"ss-1\",\"action\":9,\"owner\":\"1\"}", n));
    CHECK_FALSE(ShareProtocolClient::parseControlNotify("not json", n));

    //1725：成员的 controlledby，没带为 0；事件 11 / 12
    ShareStateNotify s;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":11,\"actor\":\"5127\",\"host\":\"5127\",\"hostonline\":1,\"annotateallowed\":1,"
        "\"recordallowed\":1,\"target\":\"5308\",\"members\":[{\"uid\":\"5127\",\"seat\":0,\"mic\":0,\"camera\":0,"
        "\"sysaudio\":0,\"recording\":0,\"controlledby\":\"5308\"},{\"uid\":\"5308\",\"seat\":1,\"mic\":0,"
        "\"camera\":0,\"sysaudio\":0,\"recording\":0}]}",
        s));
    CHECK(s.m_event == share_event_control_granted);
    REQUIRE(s.m_members.size() == 2);
    CHECK(s.m_members[0].m_controlledBy == 5308);
    CHECK(s.m_members[1].m_controlledBy == 0);
    CHECK(share_event_control_revoked == 12);
    CHECK(share_control_revoke == 5);
}
