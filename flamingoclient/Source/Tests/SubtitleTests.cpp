/**
 *  SubtitleTests.cpp
 *
 *  聊天字幕模式（docs/聊天字幕模式方案.md）的单元测试，标签 [subtitle]：
 *    SUBP  —— 偏好设置（Session/SubtitlePrefs）：位置文本的编码 / 解码、外观的整理、按会话存取、
 *             经 CUserConfig 写进配置文件再读回；
 *    SUBM  —— 内容模型（SubtitleModel）：来一条换一条、本人消息与系统提示不上字幕、按消息 id 去重、
 *             撤回替换、各类消息的占位文字；
 *    SUBF  —— 折行与截断（SubtitleTextFit）：逐字折行、英文单词不拆、超出行数时末尾加省略号、
 *             说话人前缀整体放第一行、名字过长时截短、不拆开代理对；
 *    SUBS  —— 交互状态（SubtitleState）：解锁悬停时底色淡入淡出、锁定时穿透、小锁按钮的出现延时；
 *    SUBL  —— 几何（SubtitleLayout）：尺寸随字号与 DPI 缩放、工具条居中、命中测试；
 *    SUBPL —— 放置：默认在主显示器底部居中、避开其它字幕、保存的位置在拔掉的显示器上时回到默认位置；
 *    SUBR  —— 绘制（SubtitleRenderer）：透明区域的透明度为 0、像素是预乘的、解锁时整窗可命中、颜色；
 *    SUBC  —— 会话控制器（SubtitleSession，用假视图，不建窗口）：进入、退出、消息转交、位置与外观的保存、
 *             多条字幕之间的避让与外观同步；
 *    SUBW  —— 真正的字幕窗口（隐藏用例 [.][subtitle-wnd]，会在桌面右下角显示约一秒的字幕，须单独运行）；
 *    SUBRD —— 已读判定（balloonmain::ShouldTreatAsReadOnArrival）：聊天窗处于字幕模式时，不论可见、
 *             最小化、在不在前台，新消息都算已经看到（用户确认「字幕上显示过就算已读」）；
 *    SUBSNAP、SUBSNAP2 —— 效果图（隐藏用例 [.][subtitle-snap]），把几种状态与群聊字幕画成 PNG 写到 snapshots 目录；
 *    SUBM12、SUBF12、SUBR9 —— 群聊字幕行首的来源标注「【群名】」：内容模型、排版、浅蓝色绘制；
 *    SUBN  —— 单聊字幕的说话人短名（balloonmain::ResolveContactSpeakerName）：有备注只用备注；
 *    SUBFG —— ⚙ 菜单关闭后是否把前台交还给弹菜单前的窗口（ShouldRestoreForegroundAfterMenu）；
 *    SUBB  —— 主面板列表行上的字幕徽章（balloonmain::MainPanelTreeView::SetItemSubtitle）；
 *    SUBG  —— 字幕集合（CSubtitleRegistry，主面板持有）：按会话开关、转交消息与撤回、通知主面板、全部关闭；
 *    SUBH3 —— 头部「字幕模式」按钮在字幕开着时高亮：只把图标画成蓝色、不画底色（ChatIconButton::SetActive）；
 *    SUBH4 —— 头部「字幕模式」按钮的悬停提示随状态切换：「字幕已开启」/「字幕已关闭」（ChatIconButton::SetToggleTips）；
 *    SUBH5 —— 图标按钮在鼠标移入、移出时都请求重画，点过「字幕模式」按钮后把鼠标移开，浅灰悬停底能及时消失；
 *    SUBMN —— 主面板最近会话、联系人、群组三个右键菜单里的「开启字幕模式 / 关闭字幕模式」；
 *    SUBGC1 —— 主面板为开着字幕的群记下的群名片（CGroupCardCache）：群名片优先，没有时退回联系人短名。
 *
 *  用 Catch2 v3（amalgamated）作框架。除两个隐藏用例与 SUBH5 外都是纯内存断言，不建窗口；SUBH5 在屏幕外
 *  建一个可见的宿主窗口（不可见窗口不维护待重画区域），不会在桌面上出现。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../Session/SubtitlePrefs.h"
#include "../Session/UserConfig.h"
#include "../UI/BalloonSubtitle/SubtitleLayout.h"
#include "../UI/BalloonSubtitle/SubtitleModel.h"
#include "../UI/BalloonSubtitle/SubtitleRenderer.h"
#include "../UI/BalloonSubtitle/SubtitleSession.h"
#include "../UI/BalloonSubtitle/SubtitleState.h"
#include "../UI/BalloonSubtitle/SubtitleTextFit.h"
#include "../UI/BalloonSubtitle/SubtitleView.h"
#include "../UI/BalloonSubtitle/SubtitleWindow.h"
#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"   // FormatVoiceDuration：语音占位文字里的时长写法
#include "../UI/BalloonMain/ConversationListOps.h"  // ShouldTreatAsReadOnArrival：字幕模式算正在看
#include "../UI/BalloonMain/ContactRemarkModel.h"   // SUBN：单聊字幕的说话人短名
#include "../UI/BalloonChatWnd/ChatIconButton.h"     // SUBH3：头部「字幕模式」按钮的高亮
#include "../UI/BalloonChatWnd/ChatIcons.h"          // kChatIconSubtitle
#include "../UI/BalloonMain/MainPanelConversationMenu.h"   // SUBMN1：最近会话右键菜单
#include "../UI/BalloonMain/MainPanelContactMenu.h"        // SUBMN2：联系人右键菜单
#include "../UI/BalloonMain/MainPanelGroupMenu.h"          // SUBMN3：群组右键菜单
#include "../UI/BalloonChatWnd/ChatWindowLayout.h"        // SUBH1：单聊窗头部的「字幕模式」按钮
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"   // SUBH2：群聊窗头部的「字幕模式」按钮
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"  // SUBH4：查按钮登记的悬停提示
#include "../../../third-party/balloonui/balloonui/DuiHost.h"                      // SUBH5：真实宿主窗口里的悬停与重画
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"

using namespace subtitle;

namespace {

//会话类型：单聊与群聊，取值与协议 conversation_type 一致
const int kSingle = 1;
const int kGroup = 2;
//测试里用到的会话 id 与用户 id
const int64_t kPeerId = 10023;
const int64_t kGroupId = 268435458;
const int64_t kSelfId = 10001;

//假测量：宽字符（CJK、全角标点、代理对的高位）20 像素，其它字符 10 像素，代理对的低位 0 像素
const int kWideW = 20;
const int kNarrowW = 10;
//「宽字符」的起点：CJK 部首补充区起，全角标点（如「：」U+FF1A）也在其后
const wchar_t kWideStart = 0x2E80;
//UTF-16 代理对的范围
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast = 0xDBFF;
const wchar_t kLowSurrogateFirst = 0xDC00;
const wchar_t kLowSurrogateLast = 0xDFFF;
//一个 emoji（U+1F600）的代理对
const wchar_t kEmojiHigh = 0xD83D;
const wchar_t kEmojiLow = 0xDE00;
//「一」字（U+4E00），造一串各不相同的汉字时以它为起点
const wchar_t kCjkFirst = 0x4E00;

int FakeMeasure(void* ctx, const wchar_t* text, int len)
{
    (void)ctx;
    int w = 0;
    for (int i = 0; i < len; ++i)
    {
        const wchar_t c = text[i];
        if (c >= kHighSurrogateFirst && c <= kHighSurrogateLast)
            w += kWideW;
        else if (c >= kLowSurrogateFirst && c <= kLowSurrogateLast)
            w += 0;
        else if (c >= kWideStart)
            w += kWideW;
        else
            w += kNarrowW;
    }
    return w;
}

int FakeWidth(const std::wstring& s)
{
    return FakeMeasure(NULL, s.c_str(), (int)s.size());
}

//n 个各不相同的汉字
std::wstring CjkRun(int n)
{
    std::wstring s;
    for (int i = 0; i < n; ++i)
        s += (wchar_t)(kCjkFirst + i);
    return s;
}

//n 个 emoji（每个是一个代理对）
std::wstring EmojiRun(int n)
{
    std::wstring s;
    for (int i = 0; i < n; ++i)
    {
        s += kEmojiHigh;
        s += kEmojiLow;
    }
    return s;
}

//文字里有没有被拆开的代理对（高位后面不是低位，或低位前面不是高位）
bool HasBrokenSurrogate(const std::wstring& s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        const wchar_t c = s[i];
        if (c >= kHighSurrogateFirst && c <= kHighSurrogateLast)
        {
            if (i + 1 >= s.size() || s[i + 1] < kLowSurrogateFirst || s[i + 1] > kLowSurrogateLast)
                return true;
            ++i;
        }
        else if (c >= kLowSurrogateFirst && c <= kLowSurrogateLast)
        {
            return true;
        }
    }
    return false;
}

const std::wstring kEllipsis(1, kEllipsisChar);

//说话人模板（与产品代码里的原文相同）
const wchar_t* const kSpeakerPattern = L"{0}说：";

//造一条推送
net::MessageNotify MakeNotify(long long msgid, int64_t from, int msgtype, const char* textUtf8)
{
    net::MessageNotify n;
    n.m_msgid = msgid;
    n.m_ctype = kSingle;
    n.m_cid = from;
    n.m_from = from;
    n.m_msgtype = msgtype;
    if (textUtf8 != NULL)
        n.m_text = textUtf8;
    return n;
}

//造一条交给字幕的消息
SubtitleIncoming MakeIncoming(long long msgid, const wchar_t* speaker, const wchar_t* content)
{
    SubtitleIncoming in;
    in.m_msgid = msgid;
    in.m_speaker = speaker;
    in.m_content = content;
    return in;
}

bool SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

RECT MakeRect(int l, int t, int r, int b)
{
    RECT rc = { l, t, r, b };
    return rc;
}

POINT Center(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

} // namespace

// =====================================================================
// SUBP —— 偏好设置
// =====================================================================

TEST_CASE("SUBP1 位置文本编码后能原样解码，负坐标也可以", "[subtitle]")
{
    SubtitlePlacement pl;
    pl.m_left = -1500;
    pl.m_top = 860;
    pl.m_width = 960;
    pl.m_locked = true;
    const CString text = EncodePlacement(pl);
    CHECK(text == CString(_T("-1500,860,960,1")));

    SubtitlePlacement back;
    REQUIRE(DecodePlacement(text, back));
    CHECK(back.m_left == -1500);
    CHECK(back.m_top == 860);
    CHECK(back.m_width == 960);
    CHECK(back.m_locked);

    pl.m_locked = false;
    CHECK(EncodePlacement(pl) == CString(_T("-1500,860,960,0")));
}

TEST_CASE("SUBP2 格式不对的位置文本解码失败且不改动出参", "[subtitle]")
{
    SubtitlePlacement out;
    out.m_left = 7;
    const LPCTSTR bad[] = { _T(""), _T("1,2,3"), _T("1,2,3,4,5"), _T("a,b,c,d"), _T("1,2,0,1"),
                            _T("1,2,-5,0"), _T("1,,3,1"), _T("1,2,3,x") };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        INFO("text=" << CW2A(bad[i]));
        CHECK_FALSE(DecodePlacement(bad[i], out));
        CHECK(out.m_left == 7);
    }
    CHECK_FALSE(DecodePlacement(NULL, out));
}

TEST_CASE("SUBP3 会话键名为「会话类型_会话ID」", "[subtitle]")
{
    CHECK(MakeConversationKey(kSingle, kPeerId) == CString(_T("1_10023")));
    CHECK(MakeConversationKey(kGroup, kGroupId) == CString(_T("2_268435458")));
}

TEST_CASE("SUBP4 外观整理到合法范围", "[subtitle]")
{
    SubtitleAppearance ap;
    CHECK(ap.m_fontSize == kDefaultFontSize);
    CHECK(ap.m_maxLines == kDefaultMaxLines);
    CHECK_FALSE(ap.m_hideFromCapture);

    ap.m_fontSize = kMaxFontSize + 100;
    ap.m_maxLines = kMaxMaxLines + 5;
    SubtitleAppearance n = NormalizeAppearance(ap);
    CHECK(n.m_fontSize == kMaxFontSize);
    CHECK(n.m_maxLines == kMaxMaxLines);

    ap.m_fontSize = 0;
    ap.m_maxLines = 0;
    n = NormalizeAppearance(ap);
    CHECK(n.m_fontSize == kMinFontSize);
    CHECK(n.m_maxLines == kMinMaxLines);

    SubtitlePrefs prefs;
    ap.m_fontSize = kFontSizeLarge;
    ap.m_maxLines = kMaxMaxLines;
    ap.m_hideFromCapture = true;
    prefs.SetAppearance(ap);
    CHECK(prefs.GetAppearance().m_fontSize == kFontSizeLarge);
    CHECK(prefs.GetAppearance().m_maxLines == kMaxMaxLines);
    CHECK(prefs.GetAppearance().m_hideFromCapture);
}

TEST_CASE("SUBP5 按会话存取位置，导出有序、导入跳过坏行", "[subtitle]")
{
    SubtitlePrefs prefs;
    SubtitlePlacement pl;
    CHECK_FALSE(prefs.GetPlacement(kSingle, kPeerId, pl));

    SubtitlePlacement a;
    a.m_left = 10;
    a.m_top = 20;
    a.m_width = 700;
    a.m_locked = true;
    prefs.SetPlacement(kGroup, kGroupId, a);
    SubtitlePlacement b;
    b.m_left = 30;
    b.m_top = 40;
    b.m_width = 800;
    prefs.SetPlacement(kSingle, kPeerId, b);
    //宽度不为正数的位置不保存
    SubtitlePlacement zero;
    prefs.SetPlacement(kSingle, kSelfId, zero);
    CHECK_FALSE(prefs.GetPlacement(kSingle, kSelfId, pl));

    REQUIRE(prefs.GetPlacement(kGroup, kGroupId, pl));
    CHECK(pl.m_width == 700);
    CHECK(pl.m_locked);

    std::vector<std::pair<CString, CString> > entries;
    prefs.ExportPlacements(entries);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].first == CString(_T("1_10023")));
    CHECK(entries[0].second == CString(_T("30,40,800,0")));
    CHECK(entries[1].first == CString(_T("2_268435458")));

    std::vector<std::pair<CString, CString> > in;
    in.push_back(std::make_pair(CString(_T("1_5")), CString(_T("1,2,300,1"))));
    in.push_back(std::make_pair(CString(_T("bad")), CString(_T("1,2,300,1"))));
    in.push_back(std::make_pair(CString(_T("1_6")), CString(_T("oops"))));
    in.push_back(std::make_pair(CString(_T("2_7")), CString(_T("5,6,400,0"))));
    prefs.ImportPlacements(in);
    CHECK_FALSE(prefs.GetPlacement(kSingle, kPeerId, pl));   //整体替换
    REQUIRE(prefs.GetPlacement(kSingle, 5, pl));
    CHECK(pl.m_locked);
    REQUIRE(prefs.GetPlacement(kGroup, 7, pl));
    CHECK(pl.m_width == 400);
    CHECK_FALSE(prefs.GetPlacement(kSingle, 6, pl));
}

TEST_CASE("SUBP6 外观与各会话位置经账号配置写盘后能读回", "[subtitle]")
{
    TCHAR tempDir[MAX_PATH] = { 0 };
    ::GetTempPath(MAX_PATH, tempDir);
    CString path;
    path.Format(_T("%sflamingo_subp6_%lu.ini"), tempDir, (unsigned long)::GetCurrentProcessId());
    ::DeleteFile(path);

    {
        CUserConfig cfg;
        SubtitleAppearance ap;
        ap.m_fontSize = kFontSizeHuge;
        ap.m_maxLines = kMinMaxLines;
        ap.m_hideFromCapture = true;
        cfg.Subtitles().SetAppearance(ap);
        SubtitlePlacement pl;
        pl.m_left = -800;
        pl.m_top = 600;
        pl.m_width = 900;
        pl.m_locked = true;
        cfg.Subtitles().SetPlacement(kGroup, kGroupId, pl);
        REQUIRE(cfg.SaveConfig(path));
    }
    {
        CUserConfig cfg;
        REQUIRE(cfg.LoadConfig(path));
        CHECK(cfg.Subtitles().GetAppearance().m_fontSize == kFontSizeHuge);
        CHECK(cfg.Subtitles().GetAppearance().m_maxLines == kMinMaxLines);
        CHECK(cfg.Subtitles().GetAppearance().m_hideFromCapture);
        SubtitlePlacement pl;
        REQUIRE(cfg.Subtitles().GetPlacement(kGroup, kGroupId, pl));
        CHECK(pl.m_left == -800);
        CHECK(pl.m_top == 600);
        CHECK(pl.m_width == 900);
        CHECK(pl.m_locked);
    }
    {
        //从没写过字幕设置的配置：外观为默认值、没有任何位置
        ::DeleteFile(path);
        CUserConfig cfg;
        cfg.LoadConfig(path);
        CHECK(cfg.Subtitles().GetAppearance().m_fontSize == kDefaultFontSize);
        CHECK(cfg.Subtitles().GetAppearance().m_maxLines == kDefaultMaxLines);
        CHECK_FALSE(cfg.Subtitles().GetAppearance().m_hideFromCapture);
        SubtitlePlacement pl;
        CHECK_FALSE(cfg.Subtitles().GetPlacement(kGroup, kGroupId, pl));
    }
    ::DeleteFile(path);
}

// =====================================================================
// SUBM —— 内容模型
// =====================================================================

TEST_CASE("SUBM1 一开始与 Reset 之后显示等待提示", "[subtitle]")
{
    CSubtitleModel model;
    CHECK(model.Current().m_kind == kLineWaiting);
    CHECK(model.Current().m_content == std::wstring(WaitingHintText()));
    CHECK(model.Current().m_msgid == 0);
    CHECK(std::wstring(WaitingHintText()) == L"等待新消息…");

    REQUIRE(model.OnIncoming(MakeIncoming(1, L"张三", L"你好")));
    model.Reset();
    CHECK(model.Current().m_kind == kLineWaiting);
    CHECK(model.Current().m_speaker.empty());
}

TEST_CASE("SUBM2 来一条显示一条，再来一条就替换", "[subtitle]")
{
    CSubtitleModel model;
    CHECK(model.OnIncoming(MakeIncoming(11, L"张三", L"第一条")));
    CHECK(model.Current().m_kind == kLineMessage);
    CHECK(model.Current().m_speaker == L"张三");
    CHECK(model.Current().m_content == L"第一条");
    CHECK(model.Current().m_msgid == 11);

    CHECK(model.OnIncoming(MakeIncoming(12, L"李四", L"第二条")));
    CHECK(model.Current().m_speaker == L"李四");
    CHECK(model.Current().m_content == L"第二条");
    CHECK(model.Current().m_msgid == 12);
}

TEST_CASE("SUBM3 本人消息与系统提示不上字幕", "[subtitle]")
{
    CSubtitleModel model;
    REQUIRE(model.OnIncoming(MakeIncoming(21, L"张三", L"对方的")));

    SubtitleIncoming mine = MakeIncoming(22, L"我", L"我自己的");
    mine.m_fromSelf = true;
    CHECK_FALSE(model.OnIncoming(mine));
    SubtitleIncoming sys = MakeIncoming(23, L"", L"李四加入了群聊");
    sys.m_isSystem = true;
    CHECK_FALSE(model.OnIncoming(sys));

    CHECK(model.Current().m_msgid == 21);
    CHECK(model.Current().m_content == L"对方的");
}

TEST_CASE("SUBM4 同一条消息重复推送不会把字幕换回旧消息", "[subtitle]")
{
    CSubtitleModel model;
    REQUIRE(model.OnIncoming(MakeIncoming(31, L"张三", L"旧")));
    REQUIRE(model.OnIncoming(MakeIncoming(32, L"张三", L"新")));
    CHECK_FALSE(model.OnIncoming(MakeIncoming(31, L"张三", L"旧")));
    CHECK_FALSE(model.OnIncoming(MakeIncoming(32, L"张三", L"新")));
    CHECK(model.Current().m_content == L"新");
}

TEST_CASE("SUBM5 消息 id 未知的消息不参与去重", "[subtitle]")
{
    CSubtitleModel model;
    CHECK(model.OnIncoming(MakeIncoming(0, L"张三", L"甲")));
    CHECK(model.OnIncoming(MakeIncoming(0, L"张三", L"乙")));
    CHECK(model.Current().m_content == L"乙");
}

TEST_CASE("SUBM6 撤回正在显示的那条才换成撤回提示", "[subtitle]")
{
    CSubtitleModel model;
    REQUIRE(model.OnIncoming(MakeIncoming(41, L"张三", L"甲")));
    REQUIRE(model.OnIncoming(MakeIncoming(42, L"张三", L"乙")));
    CHECK_FALSE(model.OnRecalled(41, L"张三撤回了一条消息"));
    CHECK(model.Current().m_content == L"乙");

    CHECK(model.OnRecalled(42, L"张三撤回了一条消息"));
    CHECK(model.Current().m_kind == kLineNotice);
    CHECK(model.Current().m_speaker.empty());
    CHECK(model.Current().m_content == L"张三撤回了一条消息");
    //已经是撤回提示了，再报一次不算变化；等待提示状态下的撤回也不算
    CHECK_FALSE(model.OnRecalled(42, L"张三撤回了一条消息"));
    model.Reset();
    CHECK_FALSE(model.OnRecalled(0, L"x"));
    CHECK(model.Current().m_kind == kLineWaiting);
}

TEST_CASE("SUBM7 重新进入字幕模式后去重记录清空", "[subtitle]")
{
    CSubtitleModel model;
    REQUIRE(model.OnIncoming(MakeIncoming(51, L"张三", L"甲")));
    model.Reset();
    CHECK(model.OnIncoming(MakeIncoming(51, L"张三", L"甲")));
}

TEST_CASE("SUBM8 换行与连续空白合并成一个空格", "[subtitle]")
{
    CHECK(CollapseWhitespace(L"  第一行\r\n第二行\t\t第三行  ") == L"第一行 第二行 第三行");
    CHECK(CollapseWhitespace(L"a \n\n  b") == L"a b");
    CHECK(CollapseWhitespace(L"") == L"");
    CHECK(CollapseWhitespace(L" \r\n\t ") == L"");
}

TEST_CASE("SUBM9 各类消息的字幕内容", "[subtitle]")
{
    CHECK(BuildSubtitleContent(MakeNotify(1, kPeerId, message_type_text, u8"你好")) == L"你好");
    CHECK(BuildSubtitleContent(MakeNotify(2, kPeerId, message_type_text, u8"看/f[\"5\"]这个")) == L"看[表情]这个");
    CHECK(BuildSubtitleContent(MakeNotify(3, kPeerId, message_type_text, u8"第一行\n第二行")) == L"第一行 第二行");
    CHECK(BuildSubtitleContent(MakeNotify(4, kPeerId, message_type_image, NULL)) == L"[图片]");
    CHECK(BuildSubtitleContent(MakeNotify(5, kPeerId, message_type_emoji, NULL)) == L"[表情]");
    CHECK(BuildSubtitleContent(MakeNotify(6, kPeerId, message_type_shake, NULL)) == L"[抖一抖]");
    CHECK(BuildSubtitleContent(MakeNotify(7, kPeerId, message_type_merged, NULL)) == L"[聊天记录]");

    const int kVoiceSeconds = 8;
    net::MessageNotify voice = MakeNotify(8, kPeerId, message_type_voice, NULL);
    voice.m_voiceDuration = kVoiceSeconds;
    CHECK(BuildSubtitleContent(voice) == std::wstring(L"[语音] ") + balloonchatwnd::FormatVoiceDuration(kVoiceSeconds));
    //时长未知时只写类型
    voice.m_voiceDuration = 0;
    CHECK(BuildSubtitleContent(voice) == L"[语音]");

    net::MessageNotify file = MakeNotify(9, kPeerId, message_type_file, NULL);
    file.m_fileName = u8"报价单.xlsx";
    CHECK(BuildSubtitleContent(file) == L"[文件] 报价单.xlsx");
    file.m_fileName.clear();
    CHECK(BuildSubtitleContent(file) == L"[文件]");
}

TEST_CASE("SUBM10 过长的内容截掉并以省略号结尾", "[subtitle]")
{
    const size_t kLongChars = kMaxContentChars * 3;
    std::string text(kLongChars, 'a');
    const std::wstring content = BuildSubtitleContent(MakeNotify(1, kPeerId, message_type_text, text.c_str()));
    CHECK(content.size() == kMaxContentChars);
    CHECK(content[content.size() - 1] == kEllipsisChar);
}

TEST_CASE("SUBM11 推送换算成交给字幕的消息", "[subtitle]")
{
    SubtitleIncoming in = MakeSubtitleIncoming(MakeNotify(61, kPeerId, message_type_text, u8"嗨"), kSelfId, L"张三");
    CHECK(in.m_msgid == 61);
    CHECK_FALSE(in.m_fromSelf);
    CHECK_FALSE(in.m_isSystem);
    CHECK(in.m_speaker == L"张三");
    CHECK(in.m_content == L"嗨");

    in = MakeSubtitleIncoming(MakeNotify(62, kSelfId, message_type_text, u8"我发的"), kSelfId, L"我");
    CHECK(in.m_fromSelf);
    in = MakeSubtitleIncoming(MakeNotify(63, kPeerId, message_type_system, u8"李四加入了群聊"), kSelfId, L"");
    CHECK(in.m_isSystem);
    //本人 id 未知时不判定本人消息
    in = MakeSubtitleIncoming(MakeNotify(64, kSelfId, message_type_text, u8"x"), 0, L"我");
    CHECK_FALSE(in.m_fromSelf);
}

// =====================================================================
// SUBF —— 折行与截断（假测量：汉字 20 像素、其它 10 像素）
// =====================================================================

namespace {

//检查每一行都不比 maxWidth 宽
void CheckLineWidths(const FitResult& r, int maxWidth)
{
    for (size_t i = 0; i < r.m_lines.size(); ++i)
    {
        INFO("line " << i);
        CHECK(FakeWidth(r.m_lines[i].m_text) <= maxWidth);
    }
}

//「张三说：」：四个宽字符，80 像素
const std::wstring kPrefix = L"张三说：";
const int kPrefixLen = 4;

} // namespace

TEST_CASE("SUBF1 一行放得下时只有一行", "[subtitle]")
{
    const int kWidth = 400;
    FitResult r = FitSubtitleText(kPrefix, L"你好", kWidth, kDefaultMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 1);
    CHECK(r.m_lines[0].m_text == L"张三说：你好");
    CHECK(r.m_lines[0].m_speakerLen == kPrefixLen);
    CHECK_FALSE(r.m_truncated);
}

TEST_CASE("SUBF2 汉字逐字折行，前缀只在第一行", "[subtitle]")
{
    const int kWidth = 300;
    const int kChars = 30;
    const std::wstring content = CjkRun(kChars);
    FitResult r = FitSubtitleText(kPrefix, content, kWidth, kMaxMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 3);
    CHECK_FALSE(r.m_truncated);
    //第一行：前缀 80 + 11 个字 220 = 300；第二行 15 个字；第三行剩下 4 个
    CHECK(r.m_lines[0].m_text == kPrefix + content.substr(0, 11));
    CHECK(r.m_lines[1].m_text == content.substr(11, 15));
    CHECK(r.m_lines[2].m_text == content.substr(26));
    CHECK(r.m_lines[0].m_speakerLen == kPrefixLen);
    CHECK(r.m_lines[1].m_speakerLen == 0);
    CheckLineWidths(r, kWidth);
}

TEST_CASE("SUBF3 英文单词不拆开，行首行尾不留空格", "[subtitle]")
{
    const int kWidth = 100;
    FitResult r = FitSubtitleText(L"", L"hello world foo", kWidth, kMaxMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 2);
    CHECK(r.m_lines[0].m_text == L"hello");
    CHECK(r.m_lines[1].m_text == L"world foo");
    CHECK_FALSE(r.m_truncated);

    const int kWider = 120;
    r = FitSubtitleText(L"", L"hello world foo", kWider, kMaxMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 2);
    CHECK(r.m_lines[0].m_text == L"hello world");
    CHECK(r.m_lines[1].m_text == L"foo");
}

TEST_CASE("SUBF4 正好占满行数时不加省略号", "[subtitle]")
{
    const int kWidth = 300;
    const int kChars = 41;   //11 + 15 + 15
    FitResult r = FitSubtitleText(kPrefix, CjkRun(kChars), kWidth, kMaxMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 3);
    CHECK_FALSE(r.m_truncated);
    CHECK(r.m_lines[2].m_text.size() == 15);
    CHECK(r.m_lines[2].m_text.find(kEllipsisChar) == std::wstring::npos);
}

TEST_CASE("SUBF5 超出行数时最后一行截断并加省略号", "[subtitle]")
{
    const int kWidth = 300;
    const int kChars = 50;
    const std::wstring content = CjkRun(kChars);
    FitResult r = FitSubtitleText(kPrefix, content, kWidth, kMaxMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 3);
    CHECK(r.m_truncated);
    //第三行：14 个字 280 + 省略号 10 = 290；15 个字加省略号就超过 300 了
    CHECK(r.m_lines[2].m_text == content.substr(26, 14) + kEllipsis);
    CheckLineWidths(r, kWidth);
}

TEST_CASE("SUBF6 比一整行还长的单词按字符拆", "[subtitle]")
{
    const int kWidth = 100;
    FitResult r = FitSubtitleText(L"", L"abcdefghijklmnopqrstuvwxyz", kWidth, kMaxMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 3);
    CHECK(r.m_lines[0].m_text == L"abcdefghij");
    CHECK(r.m_lines[1].m_text == L"klmnopqrst");
    CHECK(r.m_lines[2].m_text == L"uvwxyz");
    CHECK_FALSE(r.m_truncated);
}

TEST_CASE("SUBF7 内容为空时只有前缀，两者都空时是一个空行", "[subtitle]")
{
    const int kWidth = 300;
    FitResult r = FitSubtitleText(kPrefix, L"", kWidth, kDefaultMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 1);
    CHECK(r.m_lines[0].m_text == kPrefix);
    CHECK(r.m_lines[0].m_speakerLen == kPrefixLen);

    r = FitSubtitleText(L"", L"", kWidth, kDefaultMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 1);
    CHECK(r.m_lines[0].m_text.empty());
    CHECK(r.m_lines[0].m_speakerLen == 0);
    CHECK_FALSE(r.m_truncated);
}

TEST_CASE("SUBF8 不拆开代理对", "[subtitle]")
{
    const int kWidth = 100;   //一行 5 个 emoji
    const int kEmojis = 20;
    FitResult r = FitSubtitleText(L"", EmojiRun(kEmojis), kWidth, kDefaultMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 2);
    CHECK(r.m_truncated);
    CHECK(r.m_lines[0].m_text == EmojiRun(5));
    //第二行：4 个 emoji 80 + 省略号 10
    CHECK(r.m_lines[1].m_text == EmojiRun(4) + kEllipsis);
    for (size_t i = 0; i < r.m_lines.size(); ++i)
        CHECK_FALSE(HasBrokenSurrogate(r.m_lines[i].m_text));
    CheckLineWidths(r, kWidth);
}

TEST_CASE("SUBF9 名字过长时截短名字", "[subtitle]")
{
    const int kMax = 150;
    CHECK(FitSpeakerPrefix(kSpeakerPattern, L"张三", kMax, &FakeMeasure, NULL) == L"张三说：");
    //十个字的名字 200 + 「说：」40 超过 150：留 5 个字 100 + 省略号 10 + 40 = 150
    const std::wstring longName = CjkRun(10);
    CHECK(FitSpeakerPrefix(kSpeakerPattern, longName, kMax, &FakeMeasure, NULL) ==
          longName.substr(0, 5) + kEllipsis + L"说：");
    //连一个字都放不下时只留省略号
    const int kTiny = 30;
    CHECK(FitSpeakerPrefix(kSpeakerPattern, longName, kTiny, &FakeMeasure, NULL) == kEllipsis + L"说：");
    //名字里的代理对不拆开
    const std::wstring emojiName = EmojiRun(10);
    CHECK_FALSE(HasBrokenSurrogate(FitSpeakerPrefix(kSpeakerPattern, emojiName, kMax, &FakeMeasure, NULL)));
    //模板为 NULL 时只有名字
    CHECK(FitSpeakerPrefix(NULL, L"张三", kMax, &FakeMeasure, NULL) == L"张三");
}

TEST_CASE("SUBF10 只允许一行时前缀完整保留、内容截断", "[subtitle]")
{
    const int kWidth = 200;
    const int kChars = 20;
    const std::wstring content = CjkRun(kChars);
    FitResult r = FitSubtitleText(kPrefix, content, kWidth, kMinMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 1);
    CHECK(r.m_truncated);
    //前缀 80 + 5 个字 100 + 省略号 10 = 190
    CHECK(r.m_lines[0].m_text == kPrefix + content.substr(0, 5) + kEllipsis);
    CHECK(r.m_lines[0].m_speakerLen == kPrefixLen);
}

TEST_CASE("SUBF11 折到下一行的空格被去掉，最多行数非法时按 1 行", "[subtitle]")
{
    const int kWidth = 100;
    FitResult r = FitSubtitleText(L"", L"aaaaaaaaaa bbbb", kWidth, kMaxMaxLines, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 2);
    CHECK(r.m_lines[0].m_text == L"aaaaaaaaaa");
    CHECK(r.m_lines[1].m_text == L"bbbb");

    r = FitSubtitleText(L"", L"aaaaaaaaaa bbbb", kWidth, 0, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 1);
    CHECK(r.m_truncated);
}

// =====================================================================
// SUBS —— 交互状态
// =====================================================================

namespace {

//起始时刻（毫秒），取非 0 值，避免与「还没有发生过」的 0 混淆
const uint64_t kT0 = 100000;

} // namespace

TEST_CASE("SUBS1 一开始解锁、底色透明、不穿透", "[subtitle]")
{
    CSubtitleState st;
    CHECK_FALSE(st.IsLocked());
    SubtitleVisual v = st.Tick(kT0);
    CHECK(v.m_chromeAlpha == 0);
    CHECK(v.m_lockButtonAlpha == 0);
    CHECK_FALSE(v.m_clickThrough);
    CHECK_FALSE(v.m_buttonsActive);
    CHECK_FALSE(v.m_animating);
}

TEST_CASE("SUBS2 解锁时悬停淡入，完全显示后按钮才可点", "[subtitle]")
{
    CSubtitleState st;
    st.Tick(kT0);
    st.OnCursor(kPartBody, kT0);
    SubtitleVisual v = st.Tick(kT0 + kChromeFadeInMs / 2);
    CHECK(v.m_chromeAlpha > 0);
    CHECK(v.m_chromeAlpha < kAlphaOpaque);
    CHECK_FALSE(v.m_buttonsActive);
    CHECK(v.m_animating);
    v = st.Tick(kT0 + kChromeFadeInMs + 1);
    CHECK(v.m_chromeAlpha == kAlphaOpaque);
    CHECK(v.m_buttonsActive);
    CHECK_FALSE(v.m_animating);
    CHECK_FALSE(v.m_clickThrough);
}

TEST_CASE("SUBS3 离开后先等一会儿再淡出", "[subtitle]")
{
    CSubtitleState st;
    st.Tick(kT0);
    st.OnCursor(kPartBody, kT0);
    st.Tick(kT0 + kChromeFadeInMs + 1);
    const uint64_t leave = kT0 + 1000;
    st.OnCursor(kPartNone, leave);
    SubtitleVisual v = st.Tick(leave + kChromeHideDelayMs / 2);
    CHECK(v.m_chromeAlpha == kAlphaOpaque);
    CHECK(v.m_animating);   //还在等待计时
    v = st.Tick(leave + kChromeHideDelayMs + kChromeFadeOutMs / 2);
    CHECK(v.m_chromeAlpha > 0);
    CHECK(v.m_chromeAlpha < kAlphaOpaque);
    v = st.Tick(leave + kChromeHideDelayMs + kChromeFadeOutMs + kChromeFadeOutMs);
    CHECK(v.m_chromeAlpha == 0);
    CHECK_FALSE(v.m_animating);
}

TEST_CASE("SUBS4 拖动与菜单弹出期间保持显示", "[subtitle]")
{
    CSubtitleState st;
    st.Tick(kT0);
    st.OnCursor(kPartBody, kT0);
    st.Tick(kT0 + kChromeFadeInMs + 1);
    st.SetDragging(true, kT0 + 500);
    st.OnCursor(kPartNone, kT0 + 500);   //拖动太快，光标一度跑到窗口外
    SubtitleVisual v = st.Tick(kT0 + 5000);
    CHECK(v.m_chromeAlpha == kAlphaOpaque);
    st.SetDragging(false, kT0 + 5000);
    st.SetMenuOpen(true, kT0 + 5000);
    v = st.Tick(kT0 + 10000);
    CHECK(v.m_chromeAlpha == kAlphaOpaque);
    st.SetMenuOpen(false, kT0 + 10000);
    v = st.Tick(kT0 + 10000 + kChromeHideDelayMs + kChromeFadeOutMs + kChromeFadeOutMs);
    CHECK(v.m_chromeAlpha == 0);
}

TEST_CASE("SUBS5 锁定后立刻没有底色并且穿透", "[subtitle]")
{
    CSubtitleState st;
    st.Tick(kT0);
    st.OnCursor(kPartBody, kT0);
    st.Tick(kT0 + kChromeFadeInMs + 1);
    st.SetLocked(true, kT0 + 300);
    CHECK(st.IsLocked());
    SubtitleVisual v = st.Tick(kT0 + 300);
    CHECK(v.m_chromeAlpha == 0);
    CHECK(v.m_clickThrough);
    CHECK_FALSE(v.m_buttonsActive);
}

TEST_CASE("SUBS6 锁定时光标停留够久才浮出小锁按钮", "[subtitle]")
{
    CSubtitleState st;
    st.SetLocked(true, kT0);
    st.Tick(kT0);
    st.OnCursor(kPartBody, kT0);
    SubtitleVisual v = st.Tick(kT0 + kLockButtonHoverDelayMs / 2);
    CHECK(v.m_lockButtonAlpha == 0);
    CHECK(v.m_animating);   //出现延时在计时中
    v = st.Tick(kT0 + kLockButtonHoverDelayMs + kChromeFadeInMs + 1);
    CHECK(v.m_lockButtonAlpha == kAlphaOpaque);
    CHECK(v.m_chromeAlpha == 0);
    CHECK(v.m_clickThrough);   //光标在文字处，不在小锁按钮上
}

TEST_CASE("SUBS7 光标在浮出的小锁按钮上时不穿透", "[subtitle]")
{
    CSubtitleState st;
    st.SetLocked(true, kT0);
    st.Tick(kT0);
    //小锁按钮还没浮出时，光标正好在它的位置上也照样穿透
    st.OnCursor(kPartLock, kT0);
    SubtitleVisual v = st.Tick(kT0 + 10);
    CHECK(v.m_clickThrough);
    v = st.Tick(kT0 + kLockButtonHoverDelayMs + kChromeFadeInMs + 1);
    REQUIRE(v.m_lockButtonAlpha == kAlphaOpaque);
    CHECK_FALSE(v.m_clickThrough);
    st.OnCursor(kPartBody, kT0 + 2000);
    v = st.Tick(kT0 + 2000);
    CHECK(v.m_clickThrough);
}

TEST_CASE("SUBS8 锁定时光标离开后小锁按钮稍等再消失", "[subtitle]")
{
    CSubtitleState st;
    st.SetLocked(true, kT0);
    st.Tick(kT0);
    st.OnCursor(kPartBody, kT0);
    st.Tick(kT0 + kLockButtonHoverDelayMs + kChromeFadeInMs + 1);
    const uint64_t leave = kT0 + 2000;
    st.OnCursor(kPartNone, leave);
    SubtitleVisual v = st.Tick(leave + kChromeHideDelayMs / 2);
    CHECK(v.m_lockButtonAlpha == kAlphaOpaque);
    v = st.Tick(leave + kChromeHideDelayMs + kChromeFadeOutMs + kChromeFadeOutMs);
    CHECK(v.m_lockButtonAlpha == 0);
    CHECK(v.m_clickThrough);
    CHECK_FALSE(v.m_animating);
    //再回来时重新计算出现延时
    st.OnCursor(kPartBody, leave + 5000);
    v = st.Tick(leave + 5000 + kLockButtonHoverDelayMs / 2);
    CHECK(v.m_lockButtonAlpha == 0);
}

TEST_CASE("SUBS9 光标在字幕上时解锁，底色随即淡入", "[subtitle]")
{
    CSubtitleState st;
    st.SetLocked(true, kT0);
    st.Tick(kT0);
    st.OnCursor(kPartLock, kT0);
    st.Tick(kT0 + kLockButtonHoverDelayMs + kChromeFadeInMs + 1);
    st.SetLocked(false, kT0 + 2000);
    SubtitleVisual v = st.Tick(kT0 + 2000);
    CHECK_FALSE(v.m_clickThrough);
    CHECK(v.m_lockButtonAlpha == 0);
    v = st.Tick(kT0 + 2000 + kChromeFadeInMs + 1);
    CHECK(v.m_chromeAlpha == kAlphaOpaque);
    CHECK(st.CursorPart() == kPartLock);
}

TEST_CASE("SUBS10 时刻倒退时不出错", "[subtitle]")
{
    CSubtitleState st;
    st.Tick(kT0);
    st.OnCursor(kPartBody, kT0);
    st.Tick(kT0 + kChromeFadeInMs / 2);
    SubtitleVisual v = st.Tick(kT0);
    CHECK(v.m_chromeAlpha >= 0);
    CHECK(v.m_chromeAlpha <= kAlphaOpaque);
}

// =====================================================================
// SUBL —— 几何
// =====================================================================

namespace {

const int kDpi96 = 96;
const int kDpi144 = 144;
//测试用的窗口宽度与会话名宽度
const int kTestWidth = 800;
const int kTestTitleWidth = 100;
//百分比的分母
const int kPercent = 100;

} // namespace

TEST_CASE("SUBL1 尺寸按字号与 DPI 缩放", "[subtitle]")
{
    SubtitleMetrics m = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    CHECK(m.m_dpi == kDpi96);
    CHECK(m.m_fontPx == kDefaultFontSize);
    CHECK(m.m_lineHeight == ::MulDiv(m.m_fontPx, kLineHeightPercent, kPercent));
    CHECK(m.m_toolbarHeight == kToolbarHeightDip);
    CHECK(m.m_buttonSize == kButtonSizeDip);
    CHECK(m.m_minWidth == kMinWidthDip);

    SubtitleMetrics m2 = ComputeSubtitleMetrics(kDefaultFontSize, kDpi144);
    CHECK(m2.m_fontPx == ::MulDiv(kDefaultFontSize, kDpi144, kDpi96));
    CHECK(m2.m_toolbarHeight == ::MulDiv(kToolbarHeightDip, kDpi144, kDpi96));
    CHECK(m2.m_lineHeight == ::MulDiv(m2.m_fontPx, kLineHeightPercent, kPercent));

    //DPI 不大于 0 时按 96 处理
    SubtitleMetrics m3 = ComputeSubtitleMetrics(kDefaultFontSize, 0);
    CHECK(m3.m_fontPx == kDefaultFontSize);
}

TEST_CASE("SUBL2 窗口高度只取决于字号与行数", "[subtitle]")
{
    SubtitleMetrics m = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    const int h2 = SubtitleWindowHeight(m, 2);
    CHECK(h2 == m.m_toolbarHeight + m.m_textTopGap + 2 * m.m_lineHeight + m.m_paddingBottom);
    CHECK(SubtitleWindowHeight(m, 3) - h2 == m.m_lineHeight);
    CHECK(SubtitleWindowHeight(m, 0) == SubtitleWindowHeight(m, kMinMaxLines));
    CHECK(SubtitleWindowHeight(m, kMaxMaxLines + 5) == SubtitleWindowHeight(m, kMaxMaxLines));

    const RECT text = SubtitleTextRect(m, kTestWidth, 2);
    CHECK(text.left == m.m_paddingX);
    CHECK(text.right == kTestWidth - m.m_paddingX);
    CHECK(text.top == m.m_toolbarHeight + m.m_textTopGap);
    CHECK(text.bottom - text.top == 2 * m.m_lineHeight);
}

TEST_CASE("SUBL3 工具条整组水平居中，按钮依次为设置、锁定、回到聊天窗、退出", "[subtitle]")
{
    SubtitleMetrics m = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    SubtitleToolbar tb = ComputeSubtitleToolbar(m, kTestWidth, kTestTitleWidth);
    const RECT* buttons[kToolbarButtonCount] = { &tb.m_settings, &tb.m_lock, &tb.m_back, &tb.m_close };
    for (int i = 0; i < kToolbarButtonCount; ++i)
    {
        INFO("button " << i);
        CHECK(buttons[i]->right - buttons[i]->left == m.m_buttonSize);
        CHECK(buttons[i]->bottom - buttons[i]->top == m.m_buttonSize);
        CHECK(buttons[i]->top == (m.m_toolbarHeight - m.m_buttonSize) / 2);
        if (i > 0)
            CHECK(buttons[i]->left == buttons[i - 1]->right + m.m_buttonGap);
    }
    CHECK(tb.m_title.right - tb.m_title.left == kTestTitleWidth);
    CHECK(tb.m_title.right + m.m_titleGap == tb.m_settings.left);
    const int leftMargin = tb.m_title.left;
    const int rightMargin = kTestWidth - tb.m_close.right;
    CHECK(abs(leftMargin - rightMargin) <= 1);

    //会话名太宽时按窗口宽度的上限摆放
    tb = ComputeSubtitleToolbar(m, kTestWidth, kTestWidth * 2);
    CHECK(tb.m_title.right - tb.m_title.left == kTestWidth * kTitleMaxWidthPercent / kPercent);
}

TEST_CASE("SUBL4 没有会话名时四个按钮居中", "[subtitle]")
{
    SubtitleMetrics m = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    SubtitleToolbar tb = ComputeSubtitleToolbar(m, kTestWidth, 0);
    CHECK(tb.m_title.right - tb.m_title.left == 0);
    const int leftMargin = tb.m_settings.left;
    const int rightMargin = kTestWidth - tb.m_close.right;
    CHECK(abs(leftMargin - rightMargin) <= 1);
}

TEST_CASE("SUBL5 解锁时的命中测试", "[subtitle]")
{
    SubtitleMetrics m = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    const int h = SubtitleWindowHeight(m, kDefaultMaxLines);
    SubtitleToolbar tb = ComputeSubtitleToolbar(m, kTestWidth, kTestTitleWidth);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_settings), false, true, false) == kPartSettings);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_lock), false, true, false) == kPartLock);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_back), false, true, false) == kPartBack);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_close), false, true, false) == kPartClose);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_title), false, true, false) == kPartBody);
    POINT left = { 2, h / 2 };
    POINT right = { kTestWidth - 2, h / 2 };
    POINT mid = { kTestWidth / 2, h - m.m_paddingBottom - 2 };
    POINT outsideL = { -1, h / 2 };
    POINT outsideR = { kTestWidth, h / 2 };
    POINT outsideB = { kTestWidth / 2, h };
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, left, false, true, false) == kPartLeftEdge);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, right, false, true, false) == kPartRightEdge);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, mid, false, true, false) == kPartBody);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, outsideL, false, true, false) == kPartNone);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, outsideR, false, true, false) == kPartNone);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, outsideB, false, true, false) == kPartNone);
    //按钮还看不见时，按钮所在处算作文字处（可以拖动）
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_close), false, false, false) == kPartBody);
}

TEST_CASE("SUBL6 锁定时只有浮出的小锁按钮可以命中", "[subtitle]")
{
    SubtitleMetrics m = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    const int h = SubtitleWindowHeight(m, kDefaultMaxLines);
    SubtitleToolbar tb = ComputeSubtitleToolbar(m, kTestWidth, kTestTitleWidth);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_lock), true, false, true) == kPartLock);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_lock), true, false, false) == kPartBody);
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, Center(tb.m_close), true, true, true) == kPartBody);
    POINT left = { 2, h / 2 };
    CHECK(HitTestSubtitle(m, tb, kTestWidth, h, left, true, false, true) == kPartBody);
}

TEST_CASE("SUBL7 默认宽度取工作区一半并限制在上下限之内", "[subtitle]")
{
    SubtitleMetrics m = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    const int kNormalW = 1920;
    const int kWorkH = 1040;
    CHECK(DefaultSubtitleWidth(MakeRect(0, 0, kNormalW, kWorkH), m) == kNormalW * kDefaultWidthPercent / kPercent);
    const int kHugeW = 4000;
    CHECK(DefaultSubtitleWidth(MakeRect(0, 0, kHugeW, kWorkH), m) == kDefaultMaxWidthDip);
    const int kNarrowWork = 500;
    CHECK(DefaultSubtitleWidth(MakeRect(0, 0, kNarrowWork, kWorkH), m) == m.m_minWidth);
    const int kTinyWork = 300;   //比最小宽度还窄的工作区：以工作区为准
    CHECK(DefaultSubtitleWidth(MakeRect(0, 0, kTinyWork, kWorkH), m) == kTinyWork);
}

// =====================================================================
// SUBPL —— 放置
// =====================================================================

namespace {

const int kWorkW = 1920;
const int kWorkH = 1040;
const int kWinH = 120;
const int kDefW = 960;
const int kMinW = 320;
const int kGap = 8;

SubtitlePlaceInput MakePlaceInput()
{
    SubtitlePlaceInput in;
    in.m_height = kWinH;
    in.m_defaultWidth = kDefW;
    in.m_minWidth = kMinW;
    in.m_stackGap = kGap;
    in.m_primaryWork = MakeRect(0, 0, kWorkW, kWorkH);
    in.m_workAreas.push_back(in.m_primaryWork);
    return in;
}

//默认位置：主显示器工作区底部居中、离底边留出工作区高度的 kDefaultBottomMarginPercent
RECT DefaultRect()
{
    const int left = (kWorkW - kDefW) / 2;
    const int bottom = kWorkH - kWorkH * kDefaultBottomMarginPercent / kPercent;
    return MakeRect(left, bottom - kWinH, left + kDefW, bottom);
}

} // namespace

TEST_CASE("SUBPL1 没保存过位置时放在主显示器底部居中", "[subtitle]")
{
    SubtitlePlaceInput in = MakePlaceInput();
    CHECK(SameRect(ResolveSubtitleRect(in), DefaultRect()));
}

TEST_CASE("SUBPL2 与其它字幕重叠时逐个往上挪", "[subtitle]")
{
    SubtitlePlaceInput in = MakePlaceInput();
    const RECT def = DefaultRect();
    in.m_occupied.push_back(def);
    RECT rc = ResolveSubtitleRect(in);
    CHECK(rc.bottom == def.top - kGap);
    CHECK(rc.bottom - rc.top == kWinH);
    CHECK(rc.left == def.left);

    in.m_occupied.push_back(rc);
    RECT rc2 = ResolveSubtitleRect(in);
    CHECK(rc2.bottom == rc.top - kGap);

    //不重叠的字幕不影响
    SubtitlePlaceInput in2 = MakePlaceInput();
    in2.m_occupied.push_back(MakeRect(0, 0, 200, 100));
    CHECK(SameRect(ResolveSubtitleRect(in2), DefaultRect()));
}

TEST_CASE("SUBPL3 保存的位置可见时原样使用", "[subtitle]")
{
    SubtitlePlaceInput in = MakePlaceInput();
    in.m_hasSaved = true;
    in.m_saved.m_left = 100;
    in.m_saved.m_top = 200;
    in.m_saved.m_width = 700;
    //保存过的位置不再避让其它字幕：那是用户自己放的
    in.m_occupied.push_back(MakeRect(100, 200, 800, 320));
    CHECK(SameRect(ResolveSubtitleRect(in), MakeRect(100, 200, 800, 200 + kWinH)));
}

TEST_CASE("SUBPL4 保存的位置有一部分在屏幕外时平移进来", "[subtitle]")
{
    SubtitlePlaceInput in = MakePlaceInput();
    in.m_hasSaved = true;
    in.m_saved.m_left = 1500;
    in.m_saved.m_top = 1000;
    in.m_saved.m_width = 700;
    RECT rc = ResolveSubtitleRect(in);
    CHECK(rc.right == kWorkW);
    CHECK(rc.left == kWorkW - 700);
    CHECK(rc.bottom == kWorkH);
    CHECK(rc.top == kWorkH - kWinH);
}

TEST_CASE("SUBPL5 保存的位置在已拔掉的显示器上时回到默认位置", "[subtitle]")
{
    SubtitlePlaceInput in = MakePlaceInput();
    in.m_hasSaved = true;
    in.m_saved.m_left = -2500;
    in.m_saved.m_top = 300;
    in.m_saved.m_width = 700;
    CHECK(SameRect(ResolveSubtitleRect(in), DefaultRect()));
}

TEST_CASE("SUBPL6 保存的宽度超出范围时限制到工作区宽度与最小宽度之间", "[subtitle]")
{
    SubtitlePlaceInput in = MakePlaceInput();
    in.m_hasSaved = true;
    in.m_saved.m_left = 100;
    in.m_saved.m_top = 200;
    in.m_saved.m_width = 3000;
    RECT rc = ResolveSubtitleRect(in);
    CHECK(rc.left == 0);
    CHECK(rc.right == kWorkW);

    in.m_saved.m_width = 100;
    rc = ResolveSubtitleRect(in);
    CHECK(rc.right - rc.left == kMinW);
}

TEST_CASE("SUBPL7 限制到与窗口重叠最多的工作区里", "[subtitle]")
{
    std::vector<RECT> works;
    works.push_back(MakeRect(0, 0, kWorkW, kWorkH));
    works.push_back(MakeRect(-kWorkW, 0, 0, kWorkH));
    //左边 500 像素在副屏、右边 100 像素在主屏：落到副屏里
    RECT rc = ClampSubtitleRect(MakeRect(-500, 100, 100, 220), works, works[0]);
    CHECK(SameRect(rc, MakeRect(-600, 100, 0, 220)));
    //都不重叠时用 fallback
    rc = ClampSubtitleRect(MakeRect(5000, 5000, 5600, 5120), works, works[0]);
    CHECK(rc.right == kWorkW);
    CHECK(rc.bottom == kWorkH);
}

// =====================================================================
// SUBR —— 绘制
// =====================================================================

namespace {

//GDI+ 的启动与关闭（用例里建一个实例即可）
class GdiplusScope
{
public:
    GdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }
    ~GdiplusScope()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
    bool m_ok;           //是否启动成功
};

//绘制测试用的窗口宽度与会话名
const int kRenderWidth = 640;
const wchar_t* const kRenderTitle = L"产品讨论组";
//颜色判定的阈值：「亮」通道不低于 kBright，「暗」通道不高于 kDarkChannel
const int kBright = 200;
const int kDarkChannel = 120;
const int kWhiteMin = 235;
//底色区域应有的最低不透明度
const int kChromeMinAlpha = 100;

//取一个像素：b g r a
struct Px
{
    int b;
    int g;
    int r;
    int a;
};

Px PixelAt(const std::vector<uint8_t>& bits, int width, int x, int y)
{
    const size_t i = ((size_t)y * width + x) * kSubtitleBytesPerPixel;
    Px p = { bits[i], bits[i + 1], bits[i + 2], bits[i + 3] };
    return p;
}

//按给定状态准备一份绘制输入，内容按真实测量折行
SubtitleRenderParams MakeRenderParams(int lineKind, const std::wstring& speaker, const std::wstring& content,
                                      bool locked, int chromeAlpha, int lockAlpha)
{
    SubtitleRenderParams p;
    p.m_metrics = ComputeSubtitleMetrics(kDefaultFontSize, kDpi96);
    p.m_maxLines = kDefaultMaxLines;
    p.m_width = kRenderWidth;
    p.m_height = SubtitleWindowHeight(p.m_metrics, p.m_maxLines);
    CSubtitleTextMeasurer titleMeasurer(p.m_metrics.m_titleFontPx, false);
    p.m_title = kRenderTitle;
    const int titleW = titleMeasurer.Measure(p.m_title.c_str(), (int)p.m_title.size());
    p.m_toolbar = ComputeSubtitleToolbar(p.m_metrics, p.m_width, titleW);
    CSubtitleTextMeasurer measurer(p.m_metrics.m_fontPx, true);
    const RECT text = SubtitleTextRect(p.m_metrics, p.m_width, p.m_maxLines);
    const int textW = text.right - text.left;
    std::wstring prefix;
    if (!speaker.empty())
        prefix = FitSpeakerPrefix(kSpeakerPattern, speaker, textW * kSpeakerMaxWidthPercent / kPercent,
                                  &CSubtitleTextMeasurer::MeasureThunk, &measurer);
    p.m_text = FitSubtitleText(prefix, content, textW, p.m_maxLines, &CSubtitleTextMeasurer::MeasureThunk, &measurer);
    p.m_lineKind = lineKind;
    p.m_locked = locked;
    p.m_chromeAlpha = chromeAlpha;
    p.m_lockButtonAlpha = lockAlpha;
    p.m_hitTestable = !locked;
    return p;
}

} // namespace

TEST_CASE("SUBR1 锁定且没有底色时，文字以外的像素完全透明", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams p = MakeRenderParams(kLineWaiting, L"", WaitingHintText(), true, 0, 0);
    std::vector<uint8_t> bits;
    REQUIRE(RenderSubtitle(p, bits));
    REQUIRE(bits.size() == (size_t)p.m_width * p.m_height * kSubtitleBytesPerPixel);
    CHECK(PixelAt(bits, p.m_width, 0, 0).a == 0);
    CHECK(PixelAt(bits, p.m_width, p.m_width - 1, p.m_height - 1).a == 0);
    //工具条整行都透明
    int toolbarOpaque = 0;
    for (int y = 0; y < p.m_metrics.m_toolbarHeight; ++y)
        for (int x = 0; x < p.m_width; ++x)
            if (PixelAt(bits, p.m_width, x, y).a != 0)
                ++toolbarOpaque;
    CHECK(toolbarOpaque == 0);
    //文字确实画出来了
    int drawn = 0;
    for (size_t i = 3; i < bits.size(); i += kSubtitleBytesPerPixel)
        if (bits[i] != 0)
            ++drawn;
    CHECK(drawn > 0);
}

TEST_CASE("SUBR2 所有像素都是预乘透明度的", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams p = MakeRenderParams(kLineMessage, L"张三", L"今天下午三点开会，记得带上周报", false,
                                              kAlphaOpaque, 0);
    p.m_hoverPart = kPartLock;
    std::vector<uint8_t> bits;
    REQUIRE(RenderSubtitle(p, bits));
    int bad = 0;
    for (size_t i = 0; i < bits.size(); i += kSubtitleBytesPerPixel)
    {
        const int a = bits[i + 3];
        if (bits[i] > a || bits[i + 1] > a || bits[i + 2] > a)
            ++bad;
    }
    CHECK(bad == 0);
}

TEST_CASE("SUBR3 解锁时整窗都铺了一层可命中的底", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams p = MakeRenderParams(kLineWaiting, L"", WaitingHintText(), false, 0, 0);
    std::vector<uint8_t> bits;
    REQUIRE(RenderSubtitle(p, bits));
    int zero = 0;
    for (size_t i = 3; i < bits.size(); i += kSubtitleBytesPerPixel)
        if (bits[i] == 0)
            ++zero;
    CHECK(zero == 0);
    //这层底肉眼看不见：角上的像素透明度只有 1/255
    CHECK(PixelAt(bits, p.m_width, 0, 0).a <= 1);
}

TEST_CASE("SUBR4 悬停时画出半透明底色与白色图标", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams p = MakeRenderParams(kLineMessage, L"张三", L"好的", false, kAlphaOpaque, 0);
    std::vector<uint8_t> bits;
    REQUIRE(RenderSubtitle(p, bits));
    //离圆角足够远、又不在文字上的一点：底色
    const Px bg = PixelAt(bits, p.m_width, p.m_metrics.m_paddingX, p.m_height - p.m_metrics.m_paddingBottom / 2);
    CHECK(bg.a >= kChromeMinAlpha);
    CHECK(bg.r <= kDarkChannel);
    //退出按钮里有白色像素（图标）
    int white = 0;
    for (int y = p.m_toolbar.m_close.top; y < p.m_toolbar.m_close.bottom; ++y)
        for (int x = p.m_toolbar.m_close.left; x < p.m_toolbar.m_close.right; ++x)
        {
            const Px px = PixelAt(bits, p.m_width, x, y);
            if (px.r >= kBright && px.g >= kBright && px.b >= kBright)
                ++white;
        }
    CHECK(white > 0);
}

TEST_CASE("SUBR5 说话人名字金黄、内容白色", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams p = MakeRenderParams(kLineMessage, L"张三", L"晚上一起吃饭", true, 0, 0);
    std::vector<uint8_t> bits;
    REQUIRE(RenderSubtitle(p, bits));
    int gold = 0;
    int white = 0;
    for (int y = 0; y < p.m_height; ++y)
        for (int x = 0; x < p.m_width; ++x)
        {
            const Px px = PixelAt(bits, p.m_width, x, y);
            if (px.a != kAlphaOpaque)
                continue;
            if (px.r >= kBright && px.g >= kBright - 50 && px.b <= kDarkChannel)
                ++gold;
            if (px.r >= kWhiteMin && px.g >= kWhiteMin && px.b >= kWhiteMin)
                ++white;
        }
    CHECK(gold > 0);
    CHECK(white > 0);
}

TEST_CASE("SUBR6 锁定时浮出的小锁按钮只画在它自己的位置上", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams p = MakeRenderParams(kLineWaiting, L"", WaitingHintText(), true, 0, kAlphaOpaque);
    std::vector<uint8_t> bits;
    REQUIRE(RenderSubtitle(p, bits));
    int inLock = 0;
    int elsewhere = 0;
    for (int y = 0; y < p.m_metrics.m_toolbarHeight; ++y)
        for (int x = 0; x < p.m_width; ++x)
        {
            POINT pt = { x, y };
            const bool inside = ::PtInRect(&p.m_toolbar.m_lock, pt) != FALSE;
            if (PixelAt(bits, p.m_width, x, y).a != 0)
            {
                if (inside)
                    ++inLock;
                else
                    ++elsewhere;
            }
        }
    CHECK(inLock > 0);
    CHECK(elsewhere == 0);
}

TEST_CASE("SUBR7 测量：空串为 0，长的比短的宽，转接函数结果一致", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    CSubtitleTextMeasurer m(kDefaultFontSize, true);
    CHECK(m.Measure(L"", 0) == 0);
    CHECK(m.Measure(NULL, 3) == 0);
    const std::wstring s1 = L"你好";
    const std::wstring s2 = L"你好世界";
    const int w1 = m.Measure(s1.c_str(), (int)s1.size());
    const int w2 = m.Measure(s2.c_str(), (int)s2.size());
    CHECK(w1 > 0);
    CHECK(w2 > w1);
    CHECK(CSubtitleTextMeasurer::MeasureThunk(&m, s2.c_str(), (int)s2.size()) == w2);
    //行尾空格计入宽度
    const std::wstring s3 = L"你好 ";
    CHECK(m.Measure(s3.c_str(), (int)s3.size()) > w1);
}

TEST_CASE("SUBR8 尺寸无效时绘制失败", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams p;
    std::vector<uint8_t> bits;
    CHECK_FALSE(RenderSubtitle(p, bits));
}

// =====================================================================
// SUBC —— 会话控制器（假视图）
// =====================================================================

namespace {

//假视图：只记录调用，不建窗口
class FakeView : public ISubtitleView
{
public:
    static int s_alive;      //此刻还活着的假视图个数
    static RECT s_nextRect;  //下一个 Create 的假视图报告的矩形

    FakeView()
        : m_createCalls(0)
        , m_destroyCalls(0)
        , m_contentCalls(0)
        , m_appearanceCalls(0)
        , m_locked(false)
        , m_hidden(false)
        , m_hiddenCalls(0)
    {
        ::SetRectEmpty(&m_rect);
        ++s_alive;
    }
    virtual ~FakeView()
    {
        --s_alive;
    }
    virtual bool Create(const SubtitleViewInit& init) override
    {
        ++m_createCalls;
        m_init = init;
        m_line = init.m_line;
        m_title = init.m_title;
        m_appearance = init.m_appearance;
        m_locked = init.m_hasSaved ? init.m_saved.m_locked : false;
        m_rect = s_nextRect;
        return true;
    }
    virtual void Destroy() override
    {
        ++m_destroyCalls;
    }
    virtual void SetContent(const SubtitleLine& line) override
    {
        ++m_contentCalls;
        m_line = line;
    }
    virtual void SetTitle(const std::wstring& title) override
    {
        m_title = title;
    }
    virtual void SetAppearance(const SubtitleAppearance& ap) override
    {
        ++m_appearanceCalls;
        m_appearance = ap;
    }
    virtual RECT GetRect() const override
    {
        return m_rect;
    }
    virtual bool IsLocked() const override
    {
        return m_locked;
    }
    virtual void SetHidden(bool hidden) override
    {
        ++m_hiddenCalls;
        m_hidden = hidden;
    }

    int                m_createCalls;      //Create 被调用的次数
    int                m_destroyCalls;     //Destroy 被调用的次数
    int                m_contentCalls;     //SetContent 被调用的次数
    int                m_appearanceCalls;  //SetAppearance 被调用的次数
    SubtitleViewInit   m_init;             //Create 收到的输入
    SubtitleLine       m_line;             //最近一次的内容
    std::wstring       m_title;            //最近一次的会话名
    SubtitleAppearance m_appearance;       //最近一次的外观
    RECT               m_rect;             //报告的矩形
    bool               m_locked;           //报告的锁定状态
    bool               m_hidden;           //最近一次 SetHidden 的取值
    int                m_hiddenCalls;      //SetHidden 被调用的次数
};

int FakeView::s_alive = 0;
RECT FakeView::s_nextRect = { 0, 0, 0, 0 };

//假视图工厂：记下建出的视图（会话负责 delete）
struct FakeFactory
{
    std::vector<FakeView*> m_created;
};

ISubtitleView* CreateFakeView(void* ctx)
{
    FakeFactory* f = static_cast<FakeFactory*>(ctx);
    FakeView* v = new FakeView();
    f->m_created.push_back(v);
    return v;
}

//记录聊天窗收到的事件
struct HostRecorder
{
    std::vector<int> m_events;
};

void RecordHostEvent(void* ctx, int event)
{
    static_cast<HostRecorder*>(ctx)->m_events.push_back(event);
}

void CountSave(void* ctx)
{
    ++*static_cast<int*>(ctx);
}

//一组用于会话测试的依赖
struct SessionFixture
{
    SubtitlePrefs m_prefs;
    FakeFactory   m_factory;
    HostRecorder  m_host;
    int           m_saves;

    SessionFixture()
        : m_saves(0)
    {
        FakeView::s_nextRect = MakeRect(480, 816, 1440, 936);
    }

    void InitSession(CSubtitleSession& s, int ctype, int64_t cid)
    {
        s.Init(ctype, cid, &m_prefs, &CountSave, &m_saves, &RecordHostEvent, &m_host);
        s.SetViewFactory(&CreateFakeView, &m_factory);
    }
};

} // namespace

TEST_CASE("SUBC1 进入字幕模式建出视图，显示等待提示与会话名", "[subtitle]")
{
    SessionFixture fx;
    const int aliveBefore = FakeView::s_alive;
    const int activeBefore = CSubtitleSession::ActiveCount();
    {
        CSubtitleSession s;
        fx.InitSession(s, kSingle, kPeerId);
        CHECK_FALSE(s.IsActive());
        REQUIRE(s.Enter(L"张三"));
        CHECK(s.IsActive());
        CHECK(CSubtitleSession::ActiveCount() == activeBefore + 1);
        REQUIRE(fx.m_factory.m_created.size() == 1);
        FakeView* v = fx.m_factory.m_created[0];
        CHECK(s.View() == v);
        CHECK(v->m_createCalls == 1);
        CHECK(v->m_init.m_host == &s);
        CHECK(v->m_title == L"张三");
        CHECK(v->m_line.m_kind == kLineWaiting);
        CHECK_FALSE(v->m_init.m_hasSaved);
        CHECK(v->m_appearance.m_fontSize == fx.m_prefs.GetAppearance().m_fontSize);

        //已在字幕模式时再进入不会再建一个
        CHECK(s.Enter(L"张三"));
        CHECK(fx.m_factory.m_created.size() == 1);
    }
    //会话析构时视图被销毁
    CHECK(FakeView::s_alive == aliveBefore);
    CHECK(CSubtitleSession::ActiveCount() == activeBefore);
    CHECK(fx.m_host.m_events.empty());
}

TEST_CASE("SUBC2 保存过的位置与锁定状态交给视图", "[subtitle]")
{
    SessionFixture fx;
    SubtitlePlacement pl;
    pl.m_left = 50;
    pl.m_top = 60;
    pl.m_width = 700;
    pl.m_locked = true;
    fx.m_prefs.SetPlacement(kGroup, kGroupId, pl);
    CSubtitleSession s;
    fx.InitSession(s, kGroup, kGroupId);
    REQUIRE(s.Enter(L"产品讨论组"));
    FakeView* v = fx.m_factory.m_created[0];
    CHECK(v->m_init.m_hasSaved);
    CHECK(v->m_init.m_saved.m_left == 50);
    CHECK(v->m_init.m_saved.m_width == 700);
    CHECK(v->m_init.m_saved.m_locked);
}

TEST_CASE("SUBC3 消息只在字幕模式下转交，本人消息不重画", "[subtitle]")
{
    SessionFixture fx;
    CSubtitleSession s;
    fx.InitSession(s, kSingle, kPeerId);
    //不在字幕模式时忽略
    s.OnIncoming(MakeIncoming(1, L"张三", L"之前的"));
    REQUIRE(s.Enter(L"张三"));
    FakeView* v = fx.m_factory.m_created[0];
    CHECK(v->m_line.m_kind == kLineWaiting);

    s.OnIncoming(MakeIncoming(2, L"张三", L"你好"));
    CHECK(v->m_contentCalls == 1);
    CHECK(v->m_line.m_kind == kLineMessage);
    CHECK(v->m_line.m_content == L"你好");

    SubtitleIncoming mine = MakeIncoming(3, L"我", L"我的");
    mine.m_fromSelf = true;
    s.OnIncoming(mine);
    CHECK(v->m_contentCalls == 1);

    s.OnRecalled(2, L"张三撤回了一条消息");
    CHECK(v->m_contentCalls == 2);
    CHECK(v->m_line.m_kind == kLineNotice);
}

TEST_CASE("SUBC4 程序退出字幕模式时销毁视图、不通知聊天窗", "[subtitle]")
{
    SessionFixture fx;
    const int aliveBefore = FakeView::s_alive;
    CSubtitleSession s;
    fx.InitSession(s, kSingle, kPeerId);
    REQUIRE(s.Enter(L"张三"));
    s.Exit();
    CHECK_FALSE(s.IsActive());
    CHECK(s.View() == NULL);
    CHECK(FakeView::s_alive == aliveBefore);
    CHECK(fx.m_host.m_events.empty());
    //再次进入时内容回到等待提示
    s.OnIncoming(MakeIncoming(9, L"张三", L"x"));
    REQUIRE(s.Enter(L"张三"));
    CHECK(s.Model().Current().m_kind == kLineWaiting);
}

TEST_CASE("SUBC5 用户点退出按钮：退出并通知聊天窗", "[subtitle]")
{
    SessionFixture fx;
    CSubtitleSession s;
    fx.InitSession(s, kSingle, kPeerId);
    REQUIRE(s.Enter(L"张三"));
    s.OnSubtitleClose();
    CHECK_FALSE(s.IsActive());
    REQUIRE(fx.m_host.m_events.size() == 1);
    CHECK(fx.m_host.m_events[0] == kSubtitleEventExited);
}

TEST_CASE("SUBC6 用户点回到聊天窗：通知聊天窗、字幕保留", "[subtitle]")
{
    SessionFixture fx;
    CSubtitleSession s;
    fx.InitSession(s, kSingle, kPeerId);
    REQUIRE(s.Enter(L"张三"));
    s.OnSubtitleBack();
    CHECK(s.IsActive());
    REQUIRE(fx.m_host.m_events.size() == 1);
    CHECK(fx.m_host.m_events[0] == kSubtitleEventBack);
}

TEST_CASE("SUBC7 拖动、调宽、锁定后立刻保存这个会话的位置", "[subtitle]")
{
    SessionFixture fx;
    CSubtitleSession s;
    fx.InitSession(s, kGroup, kGroupId);
    REQUIRE(s.Enter(L"产品讨论组"));
    FakeView* v = fx.m_factory.m_created[0];

    const RECT moved = MakeRect(-900, 300, -100, 420);
    v->m_rect = moved;
    s.OnSubtitleMoved(moved);
    CHECK(fx.m_saves == 1);
    SubtitlePlacement pl;
    REQUIRE(fx.m_prefs.GetPlacement(kGroup, kGroupId, pl));
    CHECK(pl.m_left == -900);
    CHECK(pl.m_top == 300);
    CHECK(pl.m_width == 800);
    CHECK_FALSE(pl.m_locked);

    v->m_locked = true;
    s.OnSubtitleLockChanged(true);
    CHECK(fx.m_saves == 2);
    REQUIRE(fx.m_prefs.GetPlacement(kGroup, kGroupId, pl));
    CHECK(pl.m_locked);
    CHECK(pl.m_left == -900);
}

TEST_CASE("SUBC8 改外观：保存并让所有字幕一起换新外观", "[subtitle]")
{
    SessionFixture fx;
    CSubtitleSession a;
    CSubtitleSession b;
    CSubtitleSession idle;
    fx.InitSession(a, kSingle, kPeerId);
    fx.InitSession(b, kGroup, kGroupId);
    fx.InitSession(idle, kSingle, kSelfId);
    REQUIRE(a.Enter(L"张三"));
    FakeView::s_nextRect = MakeRect(480, 688, 1440, 808);
    REQUIRE(b.Enter(L"产品讨论组"));
    FakeView* va = fx.m_factory.m_created[0];
    FakeView* vb = fx.m_factory.m_created[1];

    SubtitleAppearance ap;
    ap.m_fontSize = kFontSizeLarge;
    ap.m_maxLines = kMaxMaxLines;
    ap.m_hideFromCapture = true;
    a.OnSubtitleAppearanceChanged(ap);
    CHECK(fx.m_prefs.GetAppearance().m_fontSize == kFontSizeLarge);
    CHECK(fx.m_prefs.GetAppearance().m_hideFromCapture);
    CHECK(fx.m_saves >= 1);
    CHECK(va->m_appearanceCalls == 1);
    CHECK(vb->m_appearanceCalls == 1);
    CHECK(vb->m_appearance.m_maxLines == kMaxMaxLines);
    //两条字幕的新位置都保存了
    SubtitlePlacement pl;
    CHECK(fx.m_prefs.GetPlacement(kSingle, kPeerId, pl));
    CHECK(fx.m_prefs.GetPlacement(kGroup, kGroupId, pl));
    //之后进入的字幕直接用新外观
    REQUIRE(idle.Enter(L"李四"));
    CHECK(fx.m_factory.m_created[2]->m_appearance.m_fontSize == kFontSizeLarge);
}

TEST_CASE("SUBC9 进入时把其它字幕所在的位置交给视图避开", "[subtitle]")
{
    SessionFixture fx;
    CSubtitleSession a;
    CSubtitleSession b;
    fx.InitSession(a, kSingle, kPeerId);
    fx.InitSession(b, kGroup, kGroupId);
    const RECT ra = MakeRect(480, 816, 1440, 936);
    FakeView::s_nextRect = ra;
    REQUIRE(a.Enter(L"张三"));
    REQUIRE(b.Enter(L"产品讨论组"));
    FakeView* vb = fx.m_factory.m_created[1];
    REQUIRE(vb->m_init.m_occupied.size() == 1);
    CHECK(SameRect(vb->m_init.m_occupied[0], ra));
}

TEST_CASE("SUBC10 切换与改会话名", "[subtitle]")
{
    SessionFixture fx;
    CSubtitleSession s;
    fx.InitSession(s, kGroup, kGroupId);
    CHECK(s.Toggle(L"旧群名"));
    CHECK(s.IsActive());
    s.SetTitle(L"新群名");
    CHECK(fx.m_factory.m_created[0]->m_title == L"新群名");
    CHECK_FALSE(s.Toggle(L"新群名"));
    CHECK_FALSE(s.IsActive());
    //不在字幕模式时改名只记下，下次进入时使用
    s.SetTitle(L"再改的群名");
    REQUIRE(s.Enter(L"再改的群名"));
    CHECK(fx.m_factory.m_created[1]->m_title == L"再改的群名");
}

TEST_CASE("SUBC11 没有偏好对象时也能进入与退出", "[subtitle]")
{
    FakeFactory factory;
    CSubtitleSession s;
    s.Init(kSingle, kPeerId, NULL, NULL, NULL, NULL, NULL);
    s.SetViewFactory(&CreateFakeView, &factory);
    REQUIRE(s.Enter(L"张三"));
    s.OnSubtitleMoved(MakeRect(0, 0, 500, 100));
    s.OnSubtitleLockChanged(true);
    s.OnSubtitleBack();
    s.OnSubtitleClose();
    CHECK_FALSE(s.IsActive());
}

// =====================================================================
// SUBRD —— 已读判定
// =====================================================================

TEST_CASE("SUBRD1 字幕模式下窗口不在前台、被遮住或最小化都算已读", "[subtitle][convunread]")
{
    //窗口开着但不在前台
    CHECK(balloonmain::ShouldTreatAsReadOnArrival(true, true, true, false, false, true));
    //最小化
    CHECK(balloonmain::ShouldTreatAsReadOnArrival(true, true, true, true, false, true));
    //不可见
    CHECK(balloonmain::ShouldTreatAsReadOnArrival(true, true, false, false, false, true));
}

TEST_CASE("SUBRD2 字幕模式下聊天窗已经关掉（对象不存在或句柄已失效）时同样算已读", "[subtitle][convunread]")
{
    //2026-10-05 起字幕由主面板持有、不随聊天窗关闭；用户确认只剩字幕开着时收到的消息也算已读
    CHECK(balloonmain::ShouldTreatAsReadOnArrival(false, false, false, false, false, true));
    CHECK(balloonmain::ShouldTreatAsReadOnArrival(true, false, true, false, true, true));
    //不在字幕模式时窗口不存在仍不算已读
    CHECK_FALSE(balloonmain::ShouldTreatAsReadOnArrival(false, false, false, false, false, false));
}

TEST_CASE("SUBRD3 不在字幕模式时与原有判据相同", "[subtitle][convunread]")
{
    const int kCombinationCount = 32;   //五个布尔量共 2^5 种组合
    for (int mask = 0; mask < kCombinationCount; ++mask)
    {
        const bool windowExists = (mask & 1) != 0;
        const bool isWindow = (mask & 2) != 0;
        const bool isVisible = (mask & 4) != 0;
        const bool isIconic = (mask & 8) != 0;
        const bool isForeground = (mask & 16) != 0;
        CHECK(balloonmain::ShouldTreatAsReadOnArrival(windowExists, isWindow, isVisible, isIconic, isForeground,
                                                      false) ==
              balloonmain::ShouldTreatAsReadOnArrival(windowExists, isWindow, isVisible, isIconic, isForeground));
        //字幕模式只会把「不算」变成「算」，不会反过来
        if (balloonmain::ShouldTreatAsReadOnArrival(windowExists, isWindow, isVisible, isIconic, isForeground))
            CHECK(balloonmain::ShouldTreatAsReadOnArrival(windowExists, isWindow, isVisible, isIconic, isForeground,
                                                          true));
    }
}

// =====================================================================
// SUBH —— 聊天窗头部的「字幕模式」按钮
// =====================================================================

namespace {

/**
 *  检查「字幕模式」按钮是头部那一行里的按钮、紧挨在「消息记录」左边、与它同宽。
 *    root：      聊天窗客户区根控件。
 *    subtitleId：「字幕模式」按钮的控件编号。
 *    historyId： 「消息记录」按钮的控件编号。
 */
void CheckSubtitleButtonBeforeHistory(balloonwjui::DuiControl* root, UINT subtitleId, UINT historyId)
{
    balloonwjui::DuiControl* subtitleBtn = root->FindCtrlById(subtitleId);
    balloonwjui::DuiControl* history = root->FindCtrlById(historyId);
    REQUIRE(dynamic_cast<balloonwjui::DuiButton*>(subtitleBtn) != NULL);
    REQUIRE(history != NULL);
    REQUIRE(subtitleBtn->GetParent() == history->GetParent());
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = history->GetParent()->Children();
    size_t subtitleAt = kids.size();
    size_t historyAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == subtitleBtn)
            subtitleAt = i;
        if (kids[i].get() == history)
            historyAt = i;
    }
    CHECK(historyAt == subtitleAt + 1);
    balloonwjui::DuiLayout* row = dynamic_cast<balloonwjui::DuiLayout*>(history->GetParent());
    REQUIRE(row != NULL);
    CHECK(row->GetHint(subtitleBtn).fixedMain == row->GetHint(history).fixedMain);
}

} // namespace

TEST_CASE("SUBH1 单聊窗头部有「字幕模式」按钮，紧挨在「消息记录」左边", "[subtitle]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root.get() != NULL);
    CheckSubtitleButtonBeforeHistory(root.get(), balloonchatwnd::kIdHdrSubtitle, balloonchatwnd::kIdHdrHistory);
    CHECK(balloonchatwnd::kIdHdrSubtitle == 441);
}

TEST_CASE("SUBH2 群聊窗头部有「字幕模式」按钮，紧挨在「消息记录」左边", "[subtitle]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = balloonchatwnd::BuildGroupChatWindowBody(false);
    REQUIRE(root.get() != NULL);
    CheckSubtitleButtonBeforeHistory(root.get(), balloonchatwnd::kGIdHdrSubtitle, balloonchatwnd::kGIdHdrHistory);
    CHECK(balloonchatwnd::kGIdHdrSubtitle == 536);
}

// =====================================================================
// SUBW —— 真正的字幕窗口（隐藏用例）
// =====================================================================

namespace {

//空的用户操作接收方
class NullHost : public ISubtitleViewHost
{
public:
    virtual void OnSubtitleBack() override
    {
    }
    virtual void OnSubtitleClose() override
    {
    }
    virtual void OnSubtitleLockChanged(bool locked) override
    {
        (void)locked;
    }
    virtual void OnSubtitleMoved(const RECT& rc) override
    {
        (void)rc;
    }
    virtual void OnSubtitleAppearanceChanged(const SubtitleAppearance& ap) override
    {
        (void)ap;
    }
};

//等窗口消息与 DWM 合成的轮数与每轮毫秒数
const int kPumpRounds = 6;
const DWORD kPumpMs = 50;

void PumpMessages()
{
    for (int i = 0; i < kPumpRounds; ++i)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(kPumpMs);
    }
}

typedef BOOL (WINAPI* GetWindowDisplayAffinityFn)(HWND hwnd, DWORD* affinity);

} // namespace

TEST_CASE("SUBW1 字幕窗口：逐像素透明显示、锁定时穿透、可排除在采集之外", "[.][subtitle-wnd]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    NullHost host;
    RECT work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SubtitleViewInit init;
    init.m_host = &host;
    init.m_title = L"SUBW1";
    init.m_hasSaved = true;
    init.m_saved.m_width = 520;
    init.m_saved.m_left = work.right - 560;
    init.m_saved.m_top = work.bottom - 200;
    init.m_line.m_kind = kLineMessage;
    init.m_line.m_speaker = L"张三";
    init.m_line.m_content = L"这是一条测试字幕";

    CSubtitleWindow wnd;
    REQUIRE(wnd.Create(init));
    PumpMessages();
    HWND hwnd = wnd.GetHwnd();
    REQUIRE(hwnd != NULL);
    CHECK(::IsWindowVisible(hwnd) != FALSE);
    const LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    CHECK((ex & WS_EX_TOPMOST) != 0);
    CHECK((ex & WS_EX_TOOLWINDOW) != 0);
    CHECK((ex & WS_EX_NOACTIVATE) != 0);
    CHECK(wnd.IsCompositionMode());
    const RECT rc = wnd.GetRect();
    CHECK(rc.right - rc.left == 520);
    CHECK(rc.left >= work.left);
    CHECK(rc.bottom <= work.bottom);
    CHECK((ex & WS_EX_TRANSPARENT) == 0);

    wnd.SetLockedForTest(true);
    PumpMessages();
    CHECK((::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0);
    CHECK(wnd.IsLocked());
    wnd.SetLockedForTest(false);
    PumpMessages();
    CHECK((::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) == 0);

    GetWindowDisplayAffinityFn getAffinity = reinterpret_cast<GetWindowDisplayAffinityFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetWindowDisplayAffinity"));
    REQUIRE(getAffinity != NULL);
    DWORD affinity = 1;
    CHECK(getAffinity(hwnd, &affinity) != FALSE);
    CHECK(affinity == 0);   //默认不排除
    SubtitleAppearance ap;
    ap.m_hideFromCapture = true;
    wnd.SetAppearance(ap);
    CHECK(getAffinity(hwnd, &affinity) != FALSE);
    CHECK(affinity == kDisplayAffinityExclude);
    ap.m_hideFromCapture = false;
    ap.m_maxLines = kMaxMaxLines;
    wnd.SetAppearance(ap);
    CHECK(getAffinity(hwnd, &affinity) != FALSE);
    CHECK(affinity == 0);
    //行数变多后底边不动、向上长高
    const RECT taller = wnd.GetRect();
    CHECK(taller.bottom == rc.bottom);
    CHECK(taller.top < rc.top);

    wnd.Destroy();
    CHECK(::IsWindow(hwnd) == FALSE);
    CHECK(wnd.GetHwnd() == NULL);
}

// =====================================================================
// SUBSNAP —— 效果图（隐藏用例）
// =====================================================================

namespace {

//PNG 编码器
bool GetPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0)
        return false;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok)
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

//效果图的背景：自上而下由天蓝渐变到暗橙，模拟截图 t1 / t2 那样有明有暗的画面
const Gdiplus::Color kSnapTop(255, 70, 130, 170);
const Gdiplus::Color kSnapBottom(255, 120, 80, 40);
//效果图四周留的背景边距
const int kSnapMargin = 24;

/**
 *  把字幕合成到渐变背景上存成 PNG。
 *    p：   绘制输入。
 *    path：输出路径。
 */
bool SaveSnapshot(const SubtitleRenderParams& p, const std::wstring& path)
{
    std::vector<uint8_t> bits;
    if (!RenderSubtitle(p, bits))
        return false;
    const int w = p.m_width + kSnapMargin * 2;
    const int h = p.m_height + kSnapMargin * 2;
    Gdiplus::Bitmap canvas(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&canvas);
        Gdiplus::LinearGradientBrush bg(Gdiplus::Point(0, 0), Gdiplus::Point(0, h), kSnapTop, kSnapBottom);
        g.FillRectangle(&bg, 0, 0, w, h);
        Gdiplus::Bitmap layer(p.m_width, p.m_height, p.m_width * kSubtitleBytesPerPixel, PixelFormat32bppPARGB,
                              &bits[0]);
        g.DrawImage(&layer, kSnapMargin, kSnapMargin, p.m_width, p.m_height);
    }
    CLSID png;
    if (!GetPngEncoder(png))
        return false;
    return canvas.Save(path.c_str(), &png, NULL) == Gdiplus::Ok;
}

} // namespace

TEST_CASE("SUBSNAP 输出字幕几种状态的效果图", "[.][subtitle-snap]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    wchar_t dir[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"FLAMINGO_SNAPSHOT_DIR", dir, MAX_PATH) == 0)
        wcscpy_s(dir, L"E:\\flamingo\\snapshots");
    const std::wstring base = std::wstring(dir) + L"\\subtitle_";

    const std::wstring longText = L"下午三点在三楼会议室评审新版本的设计稿，请大家提前把各自负责模块的问题清单整理好，"
                                  L"会上逐条过一遍，评审结束后统一更新到任务看板里，周五之前全部改完。";
    SubtitleRenderParams locked = MakeRenderParams(kLineMessage, L"张三", L"晚上一起吃饭吗？", true, 0, 0);
    CHECK(SaveSnapshot(locked, base + L"locked.png"));
    SubtitleRenderParams hover = MakeRenderParams(kLineMessage, L"李四", longText, false, kAlphaOpaque, 0);
    hover.m_hoverPart = kPartLock;
    CHECK(SaveSnapshot(hover, base + L"hover.png"));
    SubtitleRenderParams lockBtn = MakeRenderParams(kLineMessage, L"王五", L"[图片]", true, 0, kAlphaOpaque);
    CHECK(SaveSnapshot(lockBtn, base + L"locked_hover.png"));
    SubtitleRenderParams waiting = MakeRenderParams(kLineWaiting, L"", WaitingHintText(), false, 0, 0);
    CHECK(SaveSnapshot(waiting, base + L"waiting.png"));
}

//----------------------------------------------------------------------------------------------------------------------
// SUBN：单聊字幕的说话人短名（balloonmain::ResolveContactSpeakerName）。有备注只用备注，没有备注用真名
//----------------------------------------------------------------------------------------------------------------------

TEST_CASE("SUBN1 有备注时只用备注，并去掉首尾空白", "[subtitle]")
{
    CHECK(balloonmain::ResolveContactSpeakerName(L"小燕", L"韩晓燕", L"MY0308", 5308) == CString(L"小燕"));
    CHECK(balloonmain::ResolveContactSpeakerName(L"  小燕 \t", L"韩晓燕", L"MY0308", 5308) == CString(L"小燕"));
    //备注与真名相同时同样只出现一次
    CHECK(balloonmain::ResolveContactSpeakerName(L"韩晓燕", L"韩晓燕", L"MY0308", 5308) == CString(L"韩晓燕"));
}

TEST_CASE("SUBN2 备注为空或只有空白时用真名", "[subtitle]")
{
    CHECK(balloonmain::ResolveContactSpeakerName(L"", L"韩晓燕", L"MY0308", 5308) == CString(L"韩晓燕"));
    CHECK(balloonmain::ResolveContactSpeakerName(L"   ", L"韩晓燕", L"MY0308", 5308) == CString(L"韩晓燕"));
}

TEST_CASE("SUBN3 备注与昵称都为空时与真名规则一样退到账号或兜底名", "[subtitle]")
{
    CHECK(balloonmain::ResolveContactSpeakerName(L"", L" ", L"MY0308", 5308) == CString(L"MY0308"));
    CHECK(balloonmain::ResolveContactSpeakerName(L"", L"", L"", 5308) ==
          balloonmain::ResolveContactRealName(L"", L"", 5308));
}

//----------------------------------------------------------------------------------------------------------------------
// SUBFG：⚙ 菜单关闭后是否把前台交还给弹菜单前的窗口（subtitle::ShouldRestoreForegroundAfterMenu）
//----------------------------------------------------------------------------------------------------------------------

namespace {

//假窗口句柄：只比较取值，不会被当成真窗口使用
const HWND kFakeSubtitleWnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x1001));
const HWND kFakeUserAppWnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x2002));
const HWND kFakeOtherWnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x3003));

} // namespace

TEST_CASE("SUBFG1 菜单关闭后前台仍是字幕自己或暂时没有前台时交还给原来的窗口", "[subtitle]")
{
    CHECK(ShouldRestoreForegroundAfterMenu(kFakeUserAppWnd, true, kFakeSubtitleWnd, kFakeSubtitleWnd));
    CHECK(ShouldRestoreForegroundAfterMenu(kFakeUserAppWnd, true, NULL, kFakeSubtitleWnd));
}

TEST_CASE("SUBFG2 用户点了别的窗口来关闭菜单时不交还", "[subtitle]")
{
    CHECK_FALSE(ShouldRestoreForegroundAfterMenu(kFakeUserAppWnd, true, kFakeOtherWnd, kFakeSubtitleWnd));
    //点回原来那个程序关闭菜单：它已经在前台，同样不必再设一次
    CHECK_FALSE(ShouldRestoreForegroundAfterMenu(kFakeUserAppWnd, true, kFakeUserAppWnd, kFakeSubtitleWnd));
}

TEST_CASE("SUBFG3 原来的前台为空、已经关掉或就是字幕自己时不交还", "[subtitle]")
{
    CHECK_FALSE(ShouldRestoreForegroundAfterMenu(NULL, false, kFakeSubtitleWnd, kFakeSubtitleWnd));
    CHECK_FALSE(ShouldRestoreForegroundAfterMenu(kFakeUserAppWnd, false, kFakeSubtitleWnd, kFakeSubtitleWnd));
    CHECK_FALSE(ShouldRestoreForegroundAfterMenu(kFakeSubtitleWnd, true, kFakeSubtitleWnd, kFakeSubtitleWnd));
}

//----------------------------------------------------------------------------------------------------------------------
// SUBG：字幕集合（CSubtitleRegistry，主面板持有，用假视图）。字幕不随聊天窗关闭而关闭（用户 2026-10-05 确认）
//----------------------------------------------------------------------------------------------------------------------

namespace {

//集合通知主面板的一次事件
struct RegistryEventRecord
{
    int     m_event;   //SubtitleRegistryEvent
    int     m_ctype;   //会话类型
    int64_t m_cid;     //会话ID
    bool    m_active;  //事件后是否处于字幕模式
};

//记录集合发给主面板的事件
struct RegistryRecorder
{
    std::vector<RegistryEventRecord> m_events;
};

void RecordRegistryEvent(void* ctx, int event, int ctype, int64_t cid, bool active)
{
    RegistryEventRecord r;
    r.m_event = event;
    r.m_ctype = ctype;
    r.m_cid = cid;
    r.m_active = active;
    static_cast<RegistryRecorder*>(ctx)->m_events.push_back(r);
}

//一组用于集合测试的依赖
struct RegistryFixture
{
    SubtitlePrefs    m_prefs;
    FakeFactory      m_factory;
    RegistryRecorder m_rec;
    int              m_saves;

    RegistryFixture()
        : m_saves(0)
    {
        FakeView::s_nextRect = MakeRect(480, 816, 1440, 936);
    }

    void Init(CSubtitleRegistry& r)
    {
        r.Init(&m_prefs, &CountSave, &m_saves, &RecordRegistryEvent, &m_rec);
        r.SetViewFactory(&CreateFakeView, &m_factory);
    }
};

//检查一条记录下来的事件
bool IsEvent(const RegistryEventRecord& r, int event, int ctype, int64_t cid, bool active)
{
    return r.m_event == event && r.m_ctype == ctype && r.m_cid == cid && r.m_active == active;
}

} // namespace

TEST_CASE("SUBG1 按会话开关字幕：开了查得到、关了查不到，重复开启不多建窗口，不同会话各一条", "[subtitle]")
{
    RegistryFixture fx;
    const int aliveBefore = FakeView::s_alive;
    {
        CSubtitleRegistry r;
        fx.Init(r);
        CHECK_FALSE(r.IsActive(kSingle, kPeerId));
        CHECK(r.ActiveCount() == 0);

        REQUIRE(r.Enter(kSingle, kPeerId, L"张三"));
        CHECK(r.IsActive(kSingle, kPeerId));
        //会话类型不同就是另一个会话，哪怕 ID 数值相同
        CHECK_FALSE(r.IsActive(kGroup, kPeerId));
        //已经开着时再开不会多建一个
        CHECK(r.Enter(kSingle, kPeerId, L"张三"));
        CHECK(fx.m_factory.m_created.size() == 1);

        REQUIRE(r.Enter(kGroup, kGroupId, L"产品讨论组"));
        CHECK(fx.m_factory.m_created.size() == 2);
        CHECK(r.ActiveCount() == 2);
        std::vector<std::pair<int, int64_t> > active;
        r.ActiveConversations(active);
        REQUIRE(active.size() == 2);
        CHECK(std::find(active.begin(), active.end(), std::make_pair(kSingle, kPeerId)) != active.end());
        CHECK(std::find(active.begin(), active.end(), std::make_pair(kGroup, kGroupId)) != active.end());

        r.Exit(kSingle, kPeerId);
        CHECK_FALSE(r.IsActive(kSingle, kPeerId));
        CHECK(r.IsActive(kGroup, kGroupId));
        CHECK(r.ActiveCount() == 1);

        //关掉的会话可以再打开，建出新的视图
        REQUIRE(r.Enter(kSingle, kPeerId, L"张三"));
        CHECK(fx.m_factory.m_created.size() == 3);
    }
    //集合析构时全部视图被销毁
    CHECK(FakeView::s_alive == aliveBefore);
}

TEST_CASE("SUBG2 消息、撤回、改名只交给对应会话的字幕，没开字幕的会话忽略", "[subtitle]")
{
    RegistryFixture fx;
    CSubtitleRegistry r;
    fx.Init(r);
    //没开字幕的会话：不建视图、不出错
    r.OnIncoming(kSingle, kPeerId, MakeIncoming(1, L"张三", L"之前的"));
    r.OnRecalled(kSingle, kPeerId, 1, L"张三撤回了一条消息");
    r.SetTitle(kSingle, kPeerId, L"张三");
    CHECK(fx.m_factory.m_created.empty());

    REQUIRE(r.Enter(kSingle, kPeerId, L"张三"));
    REQUIRE(r.Enter(kGroup, kGroupId, L"产品讨论组"));
    FakeView* single = fx.m_factory.m_created[0];
    FakeView* group = fx.m_factory.m_created[1];

    r.OnIncoming(kGroup, kGroupId, MakeIncoming(2, L"李四", L"群里的消息"));
    CHECK(group->m_line.m_content == L"群里的消息");
    CHECK(single->m_line.m_kind == kLineWaiting);

    r.OnIncoming(kSingle, kPeerId, MakeIncoming(3, L"张三", L"单聊的消息"));
    CHECK(single->m_line.m_content == L"单聊的消息");
    CHECK(group->m_line.m_content == L"群里的消息");

    r.OnRecalled(kGroup, kGroupId, 2, L"李四撤回了一条消息");
    CHECK(group->m_line.m_kind == kLineNotice);
    CHECK(single->m_line.m_kind == kLineMessage);

    r.SetTitle(kGroup, kGroupId, L"新群名");
    CHECK(group->m_title == L"新群名");
    CHECK(single->m_title == L"张三");
}

TEST_CASE("SUBG3 用户在字幕上点退出：该会话关闭，主面板收到状态变化（已关闭）", "[subtitle]")
{
    RegistryFixture fx;
    CSubtitleRegistry r;
    fx.Init(r);
    REQUIRE(r.Enter(kGroup, kGroupId, L"产品讨论组"));
    fx.m_rec.m_events.clear();

    //字幕窗口把用户的点击交给它的宿主（会话对象）
    fx.m_factory.m_created[0]->m_init.m_host->OnSubtitleClose();
    CHECK_FALSE(r.IsActive(kGroup, kGroupId));
    REQUIRE(fx.m_rec.m_events.size() == 1);
    CHECK(IsEvent(fx.m_rec.m_events[0], kRegistryEventStateChanged, kGroup, kGroupId, false));
}

TEST_CASE("SUBG4 用户点回到聊天窗：主面板收到回到聊天窗，字幕保留", "[subtitle]")
{
    RegistryFixture fx;
    CSubtitleRegistry r;
    fx.Init(r);
    REQUIRE(r.Enter(kSingle, kPeerId, L"张三"));
    fx.m_rec.m_events.clear();

    fx.m_factory.m_created[0]->m_init.m_host->OnSubtitleBack();
    CHECK(r.IsActive(kSingle, kPeerId));
    REQUIRE(fx.m_rec.m_events.size() == 1);
    CHECK(IsEvent(fx.m_rec.m_events[0], kRegistryEventBack, kSingle, kPeerId, true));
}

TEST_CASE("SUBG5 头部按钮或右键菜单切换时，主面板收到状态变化（已开启 / 已关闭）", "[subtitle]")
{
    RegistryFixture fx;
    CSubtitleRegistry r;
    fx.Init(r);
    CHECK(r.Toggle(kSingle, kPeerId, L"张三"));
    CHECK_FALSE(r.Toggle(kSingle, kPeerId, L"张三"));
    REQUIRE(fx.m_rec.m_events.size() == 2);
    CHECK(IsEvent(fx.m_rec.m_events[0], kRegistryEventStateChanged, kSingle, kPeerId, true));
    CHECK(IsEvent(fx.m_rec.m_events[1], kRegistryEventStateChanged, kSingle, kPeerId, false));

    //状态没变的开启、关闭不发通知
    REQUIRE(r.Enter(kSingle, kPeerId, L"张三"));
    CHECK(r.Enter(kSingle, kPeerId, L"张三"));
    r.Exit(kGroup, kGroupId);
    CHECK(fx.m_rec.m_events.size() == 3);
}

TEST_CASE("SUBG6 注销或退出时全部关闭、不发通知，之后仍可再开", "[subtitle]")
{
    RegistryFixture fx;
    const int aliveBefore = FakeView::s_alive;
    CSubtitleRegistry r;
    fx.Init(r);
    REQUIRE(r.Enter(kSingle, kPeerId, L"张三"));
    REQUIRE(r.Enter(kGroup, kGroupId, L"产品讨论组"));
    fx.m_rec.m_events.clear();

    r.ExitAll();
    CHECK(r.ActiveCount() == 0);
    CHECK_FALSE(r.IsActive(kSingle, kPeerId));
    CHECK_FALSE(r.IsActive(kGroup, kGroupId));
    CHECK(FakeView::s_alive == aliveBefore);
    CHECK(fx.m_rec.m_events.empty());

    REQUIRE(r.Toggle(kGroup, kGroupId, L"产品讨论组"));
    CHECK(r.ActiveCount() == 1);
}

TEST_CASE("SUBG7 锁屏时全部字幕临时隐藏、期间新开的也隐藏，解锁后恢复；开关状态不变、不发通知", "[subtitle][lock]")
{
    RegistryFixture fx;
    CSubtitleRegistry r;
    fx.Init(r);
    REQUIRE(r.Enter(kSingle, kPeerId, L"张三"));
    REQUIRE(r.Enter(kGroup, kGroupId, L"产品讨论组"));
    fx.m_rec.m_events.clear();

    r.SetAllHidden(true);
    REQUIRE(fx.m_factory.m_created.size() == 2);
    CHECK(fx.m_factory.m_created[0]->m_hidden);
    CHECK(fx.m_factory.m_created[1]->m_hidden);
    CHECK(r.ActiveCount() == 2);
    CHECK(fx.m_rec.m_events.empty());

    //隐藏期间打开的字幕，窗口建好后同样隐藏
    const int64_t otherPeer = kPeerId + 1;
    REQUIRE(r.Enter(kSingle, otherPeer, L"王五"));
    REQUIRE(fx.m_factory.m_created.size() == 3);
    CHECK(fx.m_factory.m_created[2]->m_hidden);

    r.SetAllHidden(false);
    for (size_t i = 0; i < fx.m_factory.m_created.size(); ++i)
    {
        INFO("view " << i);
        CHECK_FALSE(fx.m_factory.m_created[i]->m_hidden);
    }
    CHECK(r.ActiveCount() == 3);
    //解锁后新开的字幕正常显示（不调用隐藏）
    r.Exit(kSingle, otherPeer);
    REQUIRE(r.Enter(kSingle, otherPeer, L"王五"));
    CHECK(fx.m_factory.m_created.back()->m_hiddenCalls == 0);
}

//----------------------------------------------------------------------------------------------------------------------
// 群聊字幕的来源标注「【群名】」（用户 2026-10-05 确认）：SUBM12 内容模型与会话、SUBF12 排版、SUBR9 绘制
//----------------------------------------------------------------------------------------------------------------------

namespace {

//来源标注模板，与 SubtitleWindow.cpp 的 kOriginPattern 相同
const wchar_t* const kOriginPattern = L"【{0}】";

//按群名标注、说话人与内容构造绘制参数（其余同 MakeRenderParams：锁定、无底色）
SubtitleRenderParams MakeOriginRenderParams(int lineKind, const std::wstring& groupName, const std::wstring& speaker,
                                            const std::wstring& content)
{
    SubtitleRenderParams p = MakeRenderParams(lineKind, speaker, content, true, 0, 0);
    CSubtitleTextMeasurer measurer(p.m_metrics.m_fontPx, true);
    const RECT text = SubtitleTextRect(p.m_metrics, p.m_width, p.m_maxLines);
    const int textW = text.right - text.left;
    const std::wstring origin = FitSpeakerPrefix(kOriginPattern, groupName, textW * kOriginMaxWidthPercent / kPercent,
                                                 &CSubtitleTextMeasurer::MeasureThunk, &measurer);
    std::wstring prefix;
    if (!speaker.empty())
        prefix = FitSpeakerPrefix(kSpeakerPattern, speaker, textW * kSpeakerMaxWidthPercent / kPercent,
                                  &CSubtitleTextMeasurer::MeasureThunk, &measurer);
    p.m_text = FitSubtitleText(origin, prefix, content, textW, p.m_maxLines, &CSubtitleTextMeasurer::MeasureThunk,
                               &measurer);
    return p;
}

//不透明且呈浅蓝色（来源标注的颜色）的像素个数；金黄、白、浅灰与黑色描边都不算
int CountOriginBlue(const std::vector<uint8_t>& bits, int w, int h)
{
    //浅蓝：蓝通道接近满值、红通道明显低于蓝通道，绿通道居中
    const int kBlueMin = 230;
    const int kRedMax = 170;
    const int kGreenMin = 150;
    const int kGreenMax = 225;
    int n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const Px px = PixelAt(bits, w, x, y);
            if (px.a != kAlphaOpaque)
                continue;
            if (px.b >= kBlueMin && px.r <= kRedMax && px.g >= kGreenMin && px.g <= kGreenMax)
                ++n;
        }
    return n;
}

} // namespace

TEST_CASE("SUBM12 群聊字幕的每种内容都带群名，单聊不带；群改名后跟着换", "[subtitle]")
{
    CSubtitleModel model;
    CHECK(model.Current().m_origin.empty());
    model.SetOrigin(L"产品讨论组");
    CHECK(model.Current().m_origin == L"产品讨论组");
    CHECK(model.Current().m_kind == kLineWaiting);
    REQUIRE(model.OnIncoming(MakeIncoming(1, L"李四", L"开会了")));
    CHECK(model.Current().m_origin == L"产品讨论组");
    REQUIRE(model.OnRecalled(1, L"李四撤回了一条消息"));
    CHECK(model.Current().m_origin == L"产品讨论组");
    //重新进入字幕模式时保留
    model.Reset();
    CHECK(model.Current().m_origin == L"产品讨论组");
    model.SetOrigin(L"");
    CHECK(model.Current().m_origin.empty());

    //会话：群聊进入时用群名作标注，改名后视图内容跟着换；单聊不带
    SessionFixture fx;
    CSubtitleSession group;
    fx.InitSession(group, kGroup, kGroupId);
    REQUIRE(group.Enter(L"产品讨论组"));
    FakeView* gv = fx.m_factory.m_created[0];
    CHECK(gv->m_line.m_origin == L"产品讨论组");
    group.SetTitle(L"新版本评审群");
    CHECK(gv->m_line.m_origin == L"新版本评审群");
    CHECK(gv->m_title == L"新版本评审群");

    CSubtitleSession single;
    fx.InitSession(single, kSingle, kPeerId);
    REQUIRE(single.Enter(L"张三"));
    CHECK(fx.m_factory.m_created[1]->m_line.m_origin.empty());
}

TEST_CASE("SUBF12 群名标注整体放在第一行行首、与说话人前缀分段，群名过长时截短到四分之一行宽", "[subtitle]")
{
    //一行 400 像素：宽字符 20 像素，正好 20 个汉字
    const int kWidth = 400;
    const int kOriginMax = kWidth * kOriginMaxWidthPercent / kPercent;
    //「【产品讨论组】」7 个宽字符 140 像素，超过四分之一行宽 100 像素：截短成「【产品…】」（90 像素）
    const std::wstring origin = FitSpeakerPrefix(kOriginPattern, L"产品讨论组", kOriginMax, &FakeMeasure, NULL);
    CHECK(origin == L"【产品" + kEllipsis + L"】");
    CHECK(FakeMeasure(NULL, origin.c_str(), (int)origin.size()) <= kOriginMax);
    //短群名原样放下
    CHECK(FitSpeakerPrefix(kOriginPattern, L"研发", kOriginMax, &FakeMeasure, NULL) == L"【研发】");

    const std::wstring prefix = L"张三说：";
    const std::wstring content = L"下午三点在三楼会议室评审新版本的设计稿请大家提前把问题清单整理好";
    const FitResult r = FitSubtitleText(origin, prefix, content, kWidth, 2, &FakeMeasure, NULL);
    REQUIRE(r.m_lines.size() == 2);
    CHECK(r.m_lines[0].m_text.compare(0, origin.size(), origin) == 0);
    CHECK(r.m_lines[0].m_text.compare(origin.size(), prefix.size(), prefix) == 0);
    CHECK(r.m_lines[0].m_originLen == (int)origin.size());
    CHECK(r.m_lines[0].m_speakerLen == (int)prefix.size());
    CHECK(r.m_lines[1].m_originLen == 0);
    CHECK(r.m_lines[1].m_speakerLen == 0);
    for (size_t i = 0; i < r.m_lines.size(); ++i)
        CHECK(FakeMeasure(NULL, r.m_lines[i].m_text.c_str(), (int)r.m_lines[i].m_text.size()) <= kWidth);
    CHECK(r.m_truncated);

    //没有说话人（等待提示、撤回提示）时只有来源标注
    const std::wstring shortOrigin = L"【研发】";
    const FitResult w = FitSubtitleText(shortOrigin, L"", L"等待新消息", kWidth, 2, &FakeMeasure, NULL);
    REQUIRE(w.m_lines.size() == 1);
    CHECK(w.m_lines[0].m_text == shortOrigin + L"等待新消息");
    CHECK(w.m_lines[0].m_originLen == (int)shortOrigin.size());
    CHECK(w.m_lines[0].m_speakerLen == 0);

    //没有来源标注（单聊）时与原来的排版完全相同
    const FitResult a = FitSubtitleText(L"", prefix, content, kWidth, 2, &FakeMeasure, NULL);
    const FitResult b = FitSubtitleText(prefix, content, kWidth, 2, &FakeMeasure, NULL);
    REQUIRE(a.m_lines.size() == b.m_lines.size());
    for (size_t i = 0; i < a.m_lines.size(); ++i)
    {
        CHECK(a.m_lines[i].m_text == b.m_lines[i].m_text);
        CHECK(a.m_lines[i].m_speakerLen == b.m_lines[i].m_speakerLen);
        CHECK(a.m_lines[i].m_originLen == 0);
    }
}

TEST_CASE("SUBR9 群聊字幕的群名标注画成浅蓝色，单聊没有浅蓝", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    SubtitleRenderParams group = MakeOriginRenderParams(kLineMessage, L"产品讨论组", L"李四", L"下午三点开会");
    std::vector<uint8_t> groupBits;
    REQUIRE(RenderSubtitle(group, groupBits));
    CHECK(CountOriginBlue(groupBits, group.m_width, group.m_height) > 0);

    //等待提示也带群名
    SubtitleRenderParams waiting = MakeOriginRenderParams(kLineWaiting, L"产品讨论组", L"", WaitingHintText());
    std::vector<uint8_t> waitingBits;
    REQUIRE(RenderSubtitle(waiting, waitingBits));
    CHECK(CountOriginBlue(waitingBits, waiting.m_width, waiting.m_height) > 0);

    SubtitleRenderParams single = MakeRenderParams(kLineMessage, L"李四", L"下午三点开会", true, 0, 0);
    std::vector<uint8_t> singleBits;
    REQUIRE(RenderSubtitle(single, singleBits));
    CHECK(CountOriginBlue(singleBits, single.m_width, single.m_height) == 0);
}

TEST_CASE("SUBSNAP2 输出群聊字幕（行首带群名）的效果图", "[.][subtitle-snap]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    wchar_t dir[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"FLAMINGO_SNAPSHOT_DIR", dir, MAX_PATH) == 0)
        wcscpy_s(dir, L"E:\\flamingo\\snapshots");
    const std::wstring base = std::wstring(dir) + L"\\subtitle_";
    SubtitleRenderParams group = MakeOriginRenderParams(kLineMessage, L"产品讨论组", L"晓燕-产品部",
                                                        L"下午三点开会，请大家提前把问题清单整理好，会上逐条过一遍。");
    CHECK(SaveSnapshot(group, base + L"group.png"));
}

//----------------------------------------------------------------------------------------------------------------------
// SUBH3：头部「字幕模式」按钮在字幕开着时高亮（balloonchatwnd::ChatIconButton::SetActive）。
//        用户 2026-10-05 改定：高亮只把图标画成蓝色，不画底色；鼠标悬停时与普通按钮一样显示浅灰底
// SUBH4：头部「字幕模式」按钮的悬停提示随状态切换（balloonchatwnd::ChatIconButton::SetToggleTips）
//----------------------------------------------------------------------------------------------------------------------

namespace {

//按钮在离屏位图上的边长（像素），与聊天窗头部按钮相当
const int kIconButtonSize = 36;
//高亮图标的蓝 #3B82F6、普通图标的灰 #6B6B6B、悬停底色 #F0EFEC，与 ChatIconButton.cpp 的常量同值
const COLORREF kActiveInkColor = RGB(0x3B, 0x82, 0xF6);
const COLORREF kNormalInkColor = RGB(0x6B, 0x6B, 0x6B);
const COLORREF kHoverBgColor = RGB(0xF0, 0xEF, 0xEC);
//颜色容差：抗锯齿边缘与底色混合，取图标颜色附近的一段
const int kInkTolerance = 40;
//检查「有没有底色」的两条取样带：图标（居中 18 像素，占第 9～26 行）上方与下方、底色圆角矩形
//（四周各缩 2 像素）之内的几行。不按「第一版浅蓝底 #E6EFFE」的颜色去数：那个颜色恰好接近蓝色图标
//以约 13% 透明度叠在白底上的颜色，图标的抗锯齿边缘会有个别像素正好是它（flamingo-a0 2026-10-05 实测
//高亮时数到 1 个），只能用「图标以外的区域是否纯白」来判断有没有底色
const int kBgBandTop = 3;
const int kBgBandBottom = 7;
const int kBgBand2Top = 29;
const int kBgBand2Bottom = 32;
const int kBgBandLeft = 6;
const int kBgBandRight = 30;
//白色像素值（32 位 DIB 的 0x00RRGGBB）
const unsigned int kWhitePixel = 0x00FFFFFF;

//像素 (r, g, b) 是否在 c 的容差范围内
bool NearColor(int r, int g, int b, COLORREF c, int tolerance)
{
    return abs(r - (int)GetRValue(c)) <= tolerance && abs(g - (int)GetGValue(c)) <= tolerance &&
           abs(b - (int)GetBValue(c)) <= tolerance;
}

//按钮画在白底上的像素统计
struct IconButtonPixels
{
    int m_blueInk;    //接近图标蓝 #3B82F6 的像素个数
    int m_hoverBg;    //恰为悬停底色 #F0EFEC 的像素个数
    int m_grayInk;    //接近普通图标灰 #6B6B6B 的像素个数
    int m_bandNonWhite;//两条取样带（图标上方与下方、底色范围之内）里不是白色的像素个数：没有底色时为 0
};

//把按钮画到白底的 32 位离屏位图上并统计颜色
IconButtonPixels PaintIconButton(balloonchatwnd::ChatIconButton& btn)
{
    IconButtonPixels px = { 0, 0, 0, 0 };
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kIconButtonSize;
    bi.bmiHeader.biHeight = -kIconButtonSize;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    ::ReleaseDC(NULL, screen);
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib == NULL || bits == NULL)
    {
        ::DeleteDC(mem);
        return px;
    }
    HGDIOBJ old = ::SelectObject(mem, dib);
    const RECT rc = { 0, 0, kIconButtonSize, kIconButtonSize };
    HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(mem, &rc, white);
    ::DeleteObject(white);
    btn.SetRect(rc);
    btn.OnPaint(mem, rc);
    const unsigned int* p = static_cast<const unsigned int*>(bits);
    //32 位 DIB 的像素按 0x00RRGGBB 排列
    const int kRedShift = 16;
    const int kGreenShift = 8;
    const unsigned int kChannelMask = 0xFF;
    for (int i = 0; i < kIconButtonSize * kIconButtonSize; ++i)
    {
        const int r = (int)((p[i] >> kRedShift) & kChannelMask);
        const int g = (int)((p[i] >> kGreenShift) & kChannelMask);
        const int b = (int)(p[i] & kChannelMask);
        if (NearColor(r, g, b, kActiveInkColor, kInkTolerance))
            ++px.m_blueInk;
        if (NearColor(r, g, b, kHoverBgColor, 0))
            ++px.m_hoverBg;
        if (NearColor(r, g, b, kNormalInkColor, kInkTolerance))
            ++px.m_grayInk;
        const int x = i % kIconButtonSize;
        const int y = i / kIconButtonSize;
        const bool inBand = (y >= kBgBandTop && y <= kBgBandBottom) || (y >= kBgBand2Top && y <= kBgBand2Bottom);
        if (inBand && x >= kBgBandLeft && x <= kBgBandRight && (p[i] & kWhitePixel) != kWhitePixel)
            ++px.m_bandNonWhite;
    }
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    return px;
}

} // namespace

TEST_CASE("SUBH3 字幕模式按钮高亮时只把图标画成蓝色、不画底色，取消后恢复灰色", "[subtitle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    balloonchatwnd::ChatIconButton btn;
    btn.SetIconKind(balloonchatwnd::kChatIconSubtitle);
    CHECK_FALSE(btn.IsActive());

    const IconButtonPixels normal = PaintIconButton(btn);
    INFO("普通：蓝 " << normal.m_blueInk << "，灰 " << normal.m_grayInk << "，取样带非白 " << normal.m_bandNonWhite);
    CHECK(normal.m_blueInk == 0);
    CHECK(normal.m_grayInk > 0);
    CHECK(normal.m_bandNonWhite == 0);

    //高亮：图标变蓝，图标以外仍是白底（不画浅蓝底，也不画任何底色）
    btn.SetActive(true);
    CHECK(btn.IsActive());
    const IconButtonPixels active = PaintIconButton(btn);
    INFO("高亮：蓝 " << active.m_blueInk << "，灰 " << active.m_grayInk << "，取样带非白 " << active.m_bandNonWhite);
    CHECK(active.m_blueInk > 0);
    CHECK(active.m_grayInk == 0);
    CHECK(active.m_bandNonWhite == 0);

    //高亮且鼠标悬停：与普通按钮一样画浅灰悬停底，图标保持蓝色
    btn.DebugSetHover(true);
    const IconButtonPixels activeHover = PaintIconButton(btn);
    INFO("高亮悬停：蓝 " << activeHover.m_blueInk << "，悬停底 " << activeHover.m_hoverBg << "，灰 "
         << activeHover.m_grayInk);
    CHECK(activeHover.m_blueInk > 0);
    CHECK(activeHover.m_hoverBg > 0);
    CHECK(activeHover.m_grayInk == 0);
    btn.DebugSetHover(false);

    btn.SetActive(false);
    const IconButtonPixels again = PaintIconButton(btn);
    CHECK(again.m_blueInk == 0);
    CHECK(again.m_grayInk > 0);
    CHECK(again.m_bandNonWhite == 0);
}

TEST_CASE("SUBH4 字幕模式按钮的悬停提示随状态切换", "[subtitle]")
{
    balloonwjui::DuiToolTipMgr& tips = balloonwjui::DuiToolTipMgr::Inst();

    //没有设置开关两态提示时，SetActive 不登记任何提示（保持按钮原有的通用行为）
    {
        balloonchatwnd::ChatIconButton plain;
        plain.SetActive(true);
        CHECK(tips.GetText(&plain).IsEmpty());
    }

    balloonchatwnd::ChatIconButton btn;
    btn.SetIconKind(balloonchatwnd::kChatIconSubtitle);
    //设置两态提示后立即按当前状态（关着）登记
    btn.SetToggleTips(L"字幕已开启", L"字幕已关闭");
    CHECK(tips.GetText(&btn) == CString(L"字幕已关闭"));

    btn.SetActive(true);
    CHECK(tips.GetText(&btn) == CString(L"字幕已开启"));

    //重复设置同一状态不改变提示
    btn.SetActive(true);
    CHECK(tips.GetText(&btn) == CString(L"字幕已开启"));

    btn.SetActive(false);
    CHECK(tips.GetText(&btn) == CString(L"字幕已关闭"));

    //按钮已高亮时再设置两态提示，登记的是「开启」那一条
    balloonchatwnd::ChatIconButton later;
    later.SetActive(true);
    later.SetToggleTips(L"字幕已开启", L"字幕已关闭");
    CHECK(tips.GetText(&later) == CString(L"字幕已开启"));
}

namespace {

//SUBH5 的宿主窗口：放到屏幕外，宽 kHoverHostW、高 kHoverHostH（像素）；左边是一个图标按钮，右边是普通控件
const int kHoverHostX = -32000;
const int kHoverHostY = -32000;
const int kHoverHostW = 200;
const int kHoverHostH = 60;

//宿主窗口当前的待重画区域是否与 rc 相交
bool HoverHostDirtyOver(HWND hwnd, const RECT& rc)
{
    RECT dirty;
    ::SetRectEmpty(&dirty);
    if (::GetUpdateRect(hwnd, &dirty, FALSE) == FALSE)
        return false;
    RECT inter;
    return ::IntersectRect(&inter, &dirty, &rc) != FALSE;
}

//把鼠标移动消息同步发给宿主窗口（客户区坐标）
void HoverHostMove(HWND hwnd, const POINT& pt)
{
    ::SendMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
}

} // namespace

TEST_CASE("SUBH5 图标按钮在鼠标移入移出时都请求重画，高亮后移开鼠标悬停底能及时擦掉", "[subtitle]")
{
    //用户 2026-10-05 反馈：点了「字幕模式」按钮、再点聊天窗别处，按钮上的浅灰悬停底一直不消失。
    //原因：balloonui 的悬停进入 / 离开只改标志不重画；点击时 SetActive 让按钮在悬停中重画了一次，
    //离开后没有人再让它重画。这里在真实宿主窗口里检查移入、移出后按钮区域都有待重画区域
    balloonwjui::DuiHost host;
    RECT wrc = { kHoverHostX, kHoverHostY, kHoverHostX + kHoverHostW, kHoverHostY + kHoverHostH };
    host.Create(NULL, wrc, _T("SubtitleHoverRepaintTests"), WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW);
    REQUIRE(host.IsWindow());

    balloonchatwnd::ChatIconButton* btn = NULL;
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetPadding(0);
        row->SetGap(0);
        std::unique_ptr<balloonchatwnd::ChatIconButton> b(new balloonchatwnd::ChatIconButton());
        b->SetIconKind(balloonchatwnd::kChatIconSubtitle);
        btn = b.get();
        row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(b.release()),
                      balloonwjui::DuiLayout::Hint().Fixed(kIconButtonSize));
        row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
        host.SetRoot(std::move(row));
    }
    ::UpdateWindow(host.m_hWnd);
    const RECT rcBtn = btn->GetRect();
    REQUIRE(rcBtn.right > rcBtn.left);
    const POINT onBtn = { (rcBtn.left + rcBtn.right) / 2, (rcBtn.top + rcBtn.bottom) / 2 };
    const POINT away = { kHoverHostW - kIconButtonSize, kHoverHostH / 2 };

    //移入：进入悬停，按钮区域要重画（画出浅灰悬停底）
    ::ValidateRect(host.m_hWnd, NULL);
    HoverHostMove(host.m_hWnd, onBtn);
    CHECK(btn->IsHover());
    CHECK(HoverHostDirtyOver(host.m_hWnd, rcBtn));

    //悬停中点亮（模拟点击后主面板回调 SetSubtitleHighlight），画一遍后清掉待重画区域
    btn->SetActive(true);
    ::UpdateWindow(host.m_hWnd);
    ::ValidateRect(host.m_hWnd, NULL);

    //移到别的控件上：离开悬停，按钮区域要重画（擦掉浅灰悬停底）
    HoverHostMove(host.m_hWnd, away);
    CHECK_FALSE(btn->IsHover());
    CHECK(HoverHostDirtyOver(host.m_hWnd, rcBtn));

    //再移回按钮、然后鼠标离开整个窗口（WM_MOUSELEAVE）：同样要重画
    HoverHostMove(host.m_hWnd, onBtn);
    ::UpdateWindow(host.m_hWnd);
    ::ValidateRect(host.m_hWnd, NULL);
    ::SendMessage(host.m_hWnd, WM_MOUSELEAVE, 0, 0);
    CHECK_FALSE(btn->IsHover());
    CHECK(HoverHostDirtyOver(host.m_hWnd, rcBtn));

    host.DestroyWindow();
}

//----------------------------------------------------------------------------------------------------------------------
// SUBMN：主面板三个页签右键菜单里的「开启字幕模式 / 关闭字幕模式」（用户 2026-10-05 确认三个页签都加）
//----------------------------------------------------------------------------------------------------------------------

namespace {

//菜单行模型里命令 id 为 id 的那一行的下标；没有时为 -1。三种菜单的行结构相同，用模板共用
template <typename Entry>
int IndexOfMenuId(const std::vector<Entry>& items, UINT id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].m_isSeparator && items[i].m_id == id)
            return (int)i;
    }
    return -1;
}

//第一条分隔条的下标；没有分隔条时为行数
template <typename Entry>
int FirstSeparatorIndex(const std::vector<Entry>& items)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_isSeparator)
            return (int)i;
    }
    return (int)items.size();
}

} // namespace

TEST_CASE("SUBMN1 最近会话右键菜单：第一组里有开启 / 关闭字幕模式，单聊群聊都有", "[subtitle]")
{
    balloonmain::ConversationMenuState st;
    CHECK(IndexOfMenuId(balloonmain::BuildConversationMenuModel(st), balloonmain::kConvMenuSubtitle) < 0);

    st.m_canSubtitle = true;
    for (int g = 0; g < 2; ++g)
    {
        st.m_isGroup = (g == 1);
        st.m_subtitleActive = false;
        std::vector<balloonmain::ConversationMenuEntry> off = balloonmain::BuildConversationMenuModel(st);
        const int iOff = IndexOfMenuId(off, balloonmain::kConvMenuSubtitle);
        REQUIRE(iOff >= 0);
        CHECK(iOff < FirstSeparatorIndex(off));
        CHECK(std::wstring(off[iOff].m_label) == L"开启字幕模式");
        CHECK(off[iOff].m_enabled);

        st.m_subtitleActive = true;
        std::vector<balloonmain::ConversationMenuEntry> on = balloonmain::BuildConversationMenuModel(st);
        const int iOn = IndexOfMenuId(on, balloonmain::kConvMenuSubtitle);
        REQUIRE(iOn >= 0);
        CHECK(std::wstring(on[iOn].m_label) == L"关闭字幕模式");
    }
    CHECK(balloonmain::kConvMenuSubtitle == 19u);
}

TEST_CASE("SUBMN2 联系人右键菜单：第一组里有开启 / 关闭字幕模式，右键自己时没有", "[subtitle]")
{
    balloonmain::ContactMenuState st;
    CHECK(IndexOfMenuId(balloonmain::BuildContactMenuModel(st), balloonmain::kContactMenuSubtitle) < 0);

    st.m_canSubtitle = true;
    std::vector<balloonmain::ContactMenuEntry> off = balloonmain::BuildContactMenuModel(st);
    const int iOff = IndexOfMenuId(off, balloonmain::kContactMenuSubtitle);
    REQUIRE(iOff >= 0);
    CHECK(iOff < FirstSeparatorIndex(off));
    CHECK(std::wstring(off[iOff].m_label) == L"开启字幕模式");

    st.m_subtitleActive = true;
    std::vector<balloonmain::ContactMenuEntry> on = balloonmain::BuildContactMenuModel(st);
    const int iOn = IndexOfMenuId(on, balloonmain::kContactMenuSubtitle);
    REQUIRE(iOn >= 0);
    CHECK(std::wstring(on[iOn].m_label) == L"关闭字幕模式");

    st.m_isSelf = true;
    CHECK(IndexOfMenuId(balloonmain::BuildContactMenuModel(st), balloonmain::kContactMenuSubtitle) < 0);
    CHECK(balloonmain::kContactMenuSubtitle == 19u);
}

TEST_CASE("SUBMN3 群组右键菜单：第一组里有开启 / 关闭字幕模式", "[subtitle]")
{
    balloonmain::GroupMenuState st;
    CHECK(IndexOfMenuId(balloonmain::BuildGroupMenuModel(st), balloonmain::kGroupMenuSubtitle) < 0);

    st.m_canSubtitle = true;
    std::vector<balloonmain::GroupMenuEntry> off = balloonmain::BuildGroupMenuModel(st);
    const int iOff = IndexOfMenuId(off, balloonmain::kGroupMenuSubtitle);
    REQUIRE(iOff >= 0);
    CHECK(iOff < FirstSeparatorIndex(off));
    CHECK(std::wstring(off[iOff].m_label) == L"开启字幕模式");

    st.m_subtitleActive = true;
    std::vector<balloonmain::GroupMenuEntry> on = balloonmain::BuildGroupMenuModel(st);
    const int iOn = IndexOfMenuId(on, balloonmain::kGroupMenuSubtitle);
    REQUIRE(iOn >= 0);
    CHECK(std::wstring(on[iOn].m_label) == L"关闭字幕模式");
    CHECK(balloonmain::kGroupMenuSubtitle == 15u);
}

//----------------------------------------------------------------------------------------------------------------------
// SUBGC1：主面板为开着字幕的群记下的群名片（CGroupCardCache）。群聊字幕说话人群名片优先，没有时用联系人短名
//----------------------------------------------------------------------------------------------------------------------

TEST_CASE("SUBGC1 群名片优先，没有群名片、群名片只有空白、不认识的成员或群都退回联系人短名", "[subtitle]")
{
    const int64_t kCardMember = 5308;     //设了群名片的成员
    const int64_t kNoCardMember = 5010;   //群名片为空的成员
    const int64_t kBlankMember = 5058;    //群名片只有空白的成员
    const int64_t kStranger = 9999;       //成员列表里没有的人（如刚入群、列表还没刷新）
    const int64_t kOtherGroup = 268435499;
    CGroupCardCache cache;
    CHECK_FALSE(cache.HasGroup(kGroupId));
    CHECK(cache.ResolveSpeaker(kGroupId, kCardMember, L"小燕") == L"小燕");

    std::map<int64_t, std::wstring> cards;
    cards[kCardMember] = L"晓燕-产品部";
    cards[kNoCardMember] = L"";
    cards[kBlankMember] = L"  ";
    cache.SetGroupCards(kGroupId, cards);
    CHECK(cache.HasGroup(kGroupId));
    CHECK(cache.ResolveSpeaker(kGroupId, kCardMember, L"小燕") == L"晓燕-产品部");
    CHECK(cache.ResolveSpeaker(kGroupId, kNoCardMember, L"韩雪飞") == L"韩雪飞");
    CHECK(cache.ResolveSpeaker(kGroupId, kBlankMember, L"韩娟丹") == L"韩娟丹");
    CHECK(cache.ResolveSpeaker(kGroupId, kStranger, L"新同事") == L"新同事");
    //别的群不受影响
    CHECK(cache.ResolveSpeaker(kOtherGroup, kCardMember, L"小燕") == L"小燕");

    //新的成员列表整个替换旧的：群名片被清掉的成员退回联系人短名
    std::map<int64_t, std::wstring> updated;
    updated[kNoCardMember] = L"雪飞";
    cache.SetGroupCards(kGroupId, updated);
    CHECK(cache.ResolveSpeaker(kGroupId, kCardMember, L"小燕") == L"小燕");
    CHECK(cache.ResolveSpeaker(kGroupId, kNoCardMember, L"韩雪飞") == L"雪飞");

    cache.ForgetGroup(kGroupId);
    CHECK_FALSE(cache.HasGroup(kGroupId));
    CHECK(cache.ResolveSpeaker(kGroupId, kNoCardMember, L"韩雪飞") == L"韩雪飞");

    cache.SetGroupCards(kOtherGroup, cards);
    cache.Clear();
    CHECK_FALSE(cache.HasGroup(kOtherGroup));
}
