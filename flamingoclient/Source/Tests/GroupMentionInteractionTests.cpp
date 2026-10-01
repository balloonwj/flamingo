/**
 *  GroupMentionInteractionTests.cpp
 *
 *  含 @ 的群聊消息在气泡里的两项交互 —— 「能选中文字」与「@ 片段右键菜单」的测试。
 *
 *  背景：含 @ 的消息原本单独走 GDI+ 排版，与手写排版引擎产出的字符级选区模型不同源，
 *  于是这类消息一律禁止选中文字；@ 片段的矩形也是画完就扔，没法做命中测试。现已把
 *  含 @ 的收敛到手写排版引擎，一份字符级模型同时供「选中文字」与「@ 片段命中」使用。
 *
 *  覆盖：
 *    F 组 —— 发送正文下标 ↔ 扁平文本下标的换算（表情 / 转义斜杠在场时的关键一步）
 *    S 组 —— 选中文字（含 @ 的气泡可选、与不含 @ 的排版一致、取文本正确）
 *    R 组 —— 收敛后的渲染回归（@ 底色块画得出来、不含 @ 的不会凭空出现）
 *    M 组 —— @ 片段命中测试与右键菜单模型
 *    D 组 —— 群成员侧栏的成员行双击：投递与右键菜单「与 TA 聊天」相同的动作
 *
 *  用 Catch2 v3（amalgamated）。除 D 组外均为纯内存 + 离屏位图，不创建窗口、不要 HWND；
 *  D 组要一个真实窗口接收成员行的投递，故建一个不显示的 DuiHost。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // D 组：成员行投递的目标窗口
#include "../App/CustomMsgDef.h"                                 // D 组：WM_CHAT_MENTION_ACTION
#include "../UI/BalloonChatWnd/GroupMemberItem.h"                // D 组：被测的成员行
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatTextSelection.h"
#include "../UI/BalloonChatWnd/GroupMentionModel.h"
#include "../UI/BalloonChatWnd/GroupMentionMenu.h"
#include "../Session/GroupRoleRules.h"
#include "../UI/BalloonChat/BalloonChatMsgFormat.h"
#include "../UI/BalloonMain/StarredModel.h"   // 特别关注项文案，M24 用它核对两处措辞一致

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区尺寸（逻辑像素）。宽度足够让下面的短文本排成一行。
const int kListWidth  = 400;
const int kListHeight = 600;

// 本人与他人 uid。
const long long kMyUid    = 10001;
const long long kOtherUid = 10002;

// M 组菜单用例里的角色简写，取值见 grouprole::NetGroupRole（网络侧：0成员 1管理员 2群主）。
const int kMember  = grouprole::kNetRoleMember;
const int kAdmin   = grouprole::kNetRoleAdmin;
const int kOwner   = grouprole::kNetRoleOwner;
const int kUnknown = -1;   // 角色未知 / 不在本群（单聊窗、尚未拉到群资料）

// @ 别人时片段的底色（与 ChatBubble.cpp 的 kAtOtherBg 同值）。渲染用例据它确认
// 底色块确实画出来了。
const COLORREF kAtOtherBg = RGB(0xE4, 0xEE, 0xFB);

RECT MakeRect(int w)
{
    RECT r = { 0, 0, w, kListHeight };
    return r;
}

// 造一条自己发的、开头 @ 了一个人的文字消息。
//   text： 气泡文本。
//   atLen：开头 @ 片段的长度（UTF-16 字符数，含 '@' 本身）。
//   uid：  被 @ 的 userid。
//   isMe： 被 @ 的是不是本人（决定配色，也决定右键菜单形态）。
ChatMessage MakeMentionMessage(const CString& text, int atLen, long long uid, bool isMe)
{
    ChatMessage m = MakeTextMessage(true, text);
    RenderSpan span;
    span.m_uid  = uid;
    span.m_off  = 0;
    span.m_len  = atLen;
    span.m_isMe = isMe;
    m.ats.push_back(span);
    return m;
}

ChatBubble* BubbleAt(ChatMessageList& list, size_t idx)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(idx < kids.size());
    ChatBubble* b = dynamic_cast<ChatBubble*>(kids[idx].get());
    REQUIRE(b != NULL);
    return b;
}

// 造一个单行、等宽字符的排版模型，供命中测试做纯逻辑断言 —— 不依赖真实字体度量，
// 换机器换字体都不会让用例飘。
//   charCount：字符数。
//   charWidth：每个字符宽（像素）。
//   lineHeight：行高（像素）。
SelLayout MakeUniformLayout(int charCount, int charWidth, int lineHeight)
{
    SelLayout layout;
    layout.m_totalChars = charCount;

    SelLine line;
    line.m_top    = 0;
    line.m_bottom = lineHeight;

    SelRun run;
    run.m_charBegin = 0;
    run.m_charCount = charCount;
    run.m_left      = 0;
    run.m_right     = charCount * charWidth;
    // 每个字符的右边界；BuildSelectionRects 靠它把区间切到字符级。
    for (int k = 0; k < charCount; ++k)
    {
        run.m_charRights.push_back((k + 1) * charWidth);
    }
    line.m_runs.push_back(run);

    layout.m_lines.push_back(line);
    return layout;
}

// 把列表画到白底离屏位图上，数出指定颜色的像素个数（确认某种配色确实画出来了）。
// @return 像素个数；画布建不出来返回 -1。
int PaintAndCountColor(ChatMessageList& list, int w, int h, COLORREF want)
{
    HDC screenDC = ::GetDC(NULL);
    if (screenDC == NULL)
    {
        return -1;
    }
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return -1;
    }

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return -1;
    }
    ::SelectObject(memDC, bmp);

    RECT all = { 0, 0, w, h };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);

    list.OnPaint(memDC, all);

    // DIB 像素按 BGRA 存放，比对时只看 RGB 三通道。
    const unsigned int target = ((unsigned int)GetRValue(want) << 16)
                              | ((unsigned int)GetGValue(want) << 8)
                              |  (unsigned int)GetBValue(want);
    const unsigned int* px = (const unsigned int*)bits;
    int count = 0;
    for (int i = 0; i < w * h; ++i)
    {
        if ((px[i] & 0x00FFFFFFu) == target)
        {
            ++count;
        }
    }

    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return count;
}

} // anonymous namespace

// =====================================================================
// F 组：发送正文下标 → 扁平文本下标的换算
//
//   正文里 /f["5"] 占 7 个字符、扁平文本里只占 1 个；`//` 占 2 个、扁平里占 1 个。
//   @ 片段的偏移记的是正文口径，而气泡的字符级排版模型是扁平口径 —— 不做这一步
//   换算，「@ 与表情混排」时高亮区间会整体错位。
// =====================================================================

TEST_CASE("F1 纯文本时两套下标一一对应", "[mention][flatmap]")
{
    const std::vector<int> map = balloonchat::BuildFlatIndexMap(_T("abc"));

    REQUIRE(map.size() == 4);   // 长度恒为 正文长度 + 1
    CHECK(map[0] == 0);
    CHECK(map[1] == 1);
    CHECK(map[2] == 2);
    CHECK(map[3] == 3);         // 末元素 = 扁平文本总长
}

TEST_CASE("F2 表情标记整体折叠成一个扁平字符", "[mention][flatmap]")
{
    // 正文 a/f["5"]b：'a' + 7 字符的表情标记 + 'b'，共 9 个字符；
    // 扁平文本是 'a' + 占位符 + 'b'，共 3 个字符。
    const CString body = _T("a/f[\"5\"]b");
    REQUIRE(body.GetLength() == 9);

    const std::vector<int> map = balloonchat::BuildFlatIndexMap(body);
    REQUIRE(map.size() == 10);

    CHECK(map[0] == 0);   // 'a'
    CHECK(map[1] == 1);   // 表情标记起点 → 占位符
    CHECK(map[7] == 1);   // 标记内部任意位置都落在同一个占位符上，不会越过它
    CHECK(map[8] == 2);   // 'b'
    CHECK(map[9] == 3);   // 末元素 = 扁平总长
}

TEST_CASE("F3 普通双斜杠各占一位，转义双斜杠才折叠成一个", "[mention][flatmap]")
{
    //后面不跟表情标记：不是转义，两个斜杠在扁平文本里各占一位（网址就靠这一条）
    {
        const CString body = _T("a//b");
        const std::vector<int> map = balloonchat::BuildFlatIndexMap(body);

        REQUIRE(map.size() == 5);
        CHECK(map[0] == 0);   // 'a'
        CHECK(map[1] == 1);   // 第一个斜杠
        CHECK(map[2] == 2);   // 第二个斜杠，各占各的
        CHECK(map[3] == 3);   // 'b'
        CHECK(map[4] == 4);
    }

    //后面紧跟表情标记：这才是转义，两个原文字符折叠成一个扁平位置
    {
        const CString body = _T("a//f[\"3\"]");
        const std::vector<int> map = balloonchat::BuildFlatIndexMap(body);

        REQUIRE(map.size() == (std::size_t)body.GetLength() + 1);
        CHECK(map[0] == 0);   // 'a'
        CHECK(map[1] == 1);   // 两个斜杠的第一个（转义位）
        CHECK(map[2] == 1);   // 第二个，与上一个同位
        CHECK(map[3] == 2);   // 'f'，转义之后按普通文字逐字排下去
    }
}

TEST_CASE("F4 换算表与 ParseSendBody 的扁平长度一致", "[mention][flatmap]")
{
    // 两者必须同源 —— 换算表的末元素就是扁平文本长度，对不上说明两处对转义的判断
    // 出现了分歧，@ 高亮必然错位。
    const CString body = _T("hi/f[\"3\"]there//x");

    const std::vector<int> map = balloonchat::BuildFlatIndexMap(body);
    const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(body);

    int flatLen = 0;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        flatLen += segs[i].m_bFace ? 1 : segs[i].m_strText.GetLength();
    }
    CHECK(map[(size_t)body.GetLength()] == flatLen);
}

// =====================================================================
// S 组：含 @ 的气泡可以选中文字
// =====================================================================

TEST_CASE("S1 含 @ 的文字气泡允许选中", "[mention][select]")
{
    ChatMessageList list;
    list.AppendMessage(MakeMentionMessage(_T("@张三 你好"), 3, kOtherUid, false));
    list.ForceLayout(MakeRect(kListWidth));

    // 「是否可选」本身是气泡的内部判断（IsTextSelectable 为 private），这里用它的
    // 公开表现来断言：可选的气泡全选之后才会真的产生选区。收敛前含 @ 的一律不可选，
    // SelectAllText 直接早返回、选区为空；收敛后应当与普通文字气泡一样能选中。
    ChatBubble* b = BubbleAt(list, 0);
    b->SelectAllText();
    CHECK(b->HasTextSelection());
}

TEST_CASE("S2 含 @ 与不含 @ 的同一段文本行高完全一致", "[mention][select]")
{
    // @ 只改配色、不改排版 —— 两者走同一次折行，行高必须一模一样。
    ChatMessageList withAt;
    withAt.AppendMessage(MakeMentionMessage(_T("@张三 你好"), 3, kOtherUid, false));
    withAt.ForceLayout(MakeRect(kListWidth));

    ChatMessageList plain;
    plain.AppendMessage(MakeTextMessage(true, _T("@张三 你好")));
    plain.ForceLayout(MakeRect(kListWidth));

    CHECK(BubbleAt(withAt, 0)->GetRowHeight() == BubbleAt(plain, 0)->GetRowHeight());
}

TEST_CASE("S3 含 @ 的气泡全选后取回整条文本", "[mention][select]")
{
    const CString text = _T("@张三 你好");

    ChatMessageList list;
    list.AppendMessage(MakeMentionMessage(text, 3, kOtherUid, false));
    list.ForceLayout(MakeRect(kListWidth));

    ChatBubble* b = BubbleAt(list, 0);
    b->SelectAllText();

    REQUIRE(b->HasTextSelection());
    // @ 的名字本身也在选区里 —— 它就是正文的一部分，不该被排除在外。
    CHECK(b->GetSelectedText() == text);
}

TEST_CASE("S4 清除选区后不再有选中内容", "[mention][select]")
{
    ChatMessageList list;
    list.AppendMessage(MakeMentionMessage(_T("@张三 你好"), 3, kOtherUid, false));
    list.ForceLayout(MakeRect(kListWidth));

    ChatBubble* b = BubbleAt(list, 0);
    b->SelectAllText();
    REQUIRE(b->HasTextSelection());

    b->ClearTextSelection();
    CHECK_FALSE(b->HasTextSelection());
}

// =====================================================================
// R 组：收敛后的渲染回归
// =====================================================================

TEST_CASE("R1 @ 片段的底色块确实画了出来", "[mention][paint]")
{
    // 收敛后底色块改由排版模型出矩形。若下标换算或矩形取错，这个颜色一个像素都
    // 不会出现 —— 这条用例就是守住这一点。
    ChatMessageList list;
    list.AppendMessage(MakeMentionMessage(_T("@张三 你好"), 3, kOtherUid, false));
    list.ForceLayout(MakeRect(kListWidth));

    const int painted = PaintAndCountColor(list, kListWidth, kListHeight, kAtOtherBg);
    REQUIRE(painted >= 0);
    INFO("@ 底色块像素数 = " << painted);
    CHECK(painted > 0);
}

TEST_CASE("R2 不含 @ 的气泡不应出现 @ 底色块", "[mention][paint]")
{
    // 对照组：同样的文本但没有 ats，不该凭空冒出高亮底色。
    ChatMessageList list;
    list.AppendMessage(MakeTextMessage(true, _T("@张三 你好")));
    list.ForceLayout(MakeRect(kListWidth));

    const int painted = PaintAndCountColor(list, kListWidth, kListHeight, kAtOtherBg);
    REQUIRE(painted >= 0);
    CHECK(painted == 0);
}

// =====================================================================
// M 组：@ 片段命中测试与右键菜单模型
// =====================================================================

TEST_CASE("M1 点在片段矩形内命中、点在矩形外不命中", "[mention][menu]")
{
    // 10 个字符、每个 10px 宽、行高 20px：前 3 个字符（x 落在 0~30）是 @ 片段。
    const SelLayout layout = MakeUniformLayout(10, 10, 20);

    std::vector<std::pair<int, int> > ranges;
    ranges.push_back(std::make_pair(0, 3));

    CHECK(HitTestCharRanges(layout, ranges, 10, 10) == 0);    // 片段之内
    CHECK(HitTestCharRanges(layout, ranges, 80, 10) == -1);   // 片段之后的正文上
    CHECK(HitTestCharRanges(layout, ranges, 10, 50) == -1);   // 这一行之外
}

TEST_CASE("M2 点在气泡空白处不该误命中", "[mention][menu]")
{
    // 这是 HitTestCharRanges 与 CharIndexFromPoint 的关键差别：后者为拖选服务，
    // 点在行尾之外也会归到最近的字符；命中测试则必须算"没点中"，否则用户在气泡
    // 空白处右键也会弹出 @ 的菜单。
    const SelLayout layout = MakeUniformLayout(10, 10, 20);

    std::vector<std::pair<int, int> > ranges;
    ranges.push_back(std::make_pair(0, 3));

    CHECK(HitTestCharRanges(layout, ranges, 500, 10) == -1);   // 远在行尾之外
    CHECK(HitTestCharRanges(layout, ranges, -20, 10) == -1);   // 行首之左
}

TEST_CASE("M3 空模型与空区间都不命中", "[mention][menu]")
{
    SelLayout empty;
    std::vector<std::pair<int, int> > ranges;
    ranges.push_back(std::make_pair(0, 3));
    CHECK(HitTestCharRanges(empty, ranges, 0, 0) == -1);

    const SelLayout layout = MakeUniformLayout(5, 10, 20);
    std::vector<std::pair<int, int> > degenerate;
    degenerate.push_back(std::make_pair(2, 2));   // 空区间
    CHECK(HitTestCharRanges(layout, degenerate, 20, 10) == -1);
}

TEST_CASE("M4 @ 别人时菜单五项且顺序固定", "[mention][menu]")
{
    //本人是普通成员：末尾的管理项不出现，菜单形态与加管理项之前完全一致
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kMember, kMember);

    REQUIRE(items.size() == 5);
    CHECK(items[0].id == kMentionMenuChat);
    CHECK(items[1].id == kMentionMenuProfile);
    CHECK(items[2].id == kMentionMenuMentionMore);
    CHECK(items[3].id == kMentionMenuCopyName);
    CHECK(items[4].id == kMentionMenuStarred);   //特别关注排在日常动作这一组的末尾
    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK(items[i].enabled);
    }
}

TEST_CASE("M5 @ 自己时只留查看资料", "[mention][menu]")
{
    //即便本人是群主，对自己右键也只有「查看资料」——群主不能改自己的角色
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kMyUid, kMyUid, true, kOwner, kOwner);

    REQUIRE(items.size() == 1);
    CHECK(items[0].id == kMentionMenuProfile);
    CHECK(items[0].enabled);
}

TEST_CASE("M6 成员已退群时各项照列但再 @ 一次置灰", "[mention][menu]")
{
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, false, kMember, kMember);

    REQUIRE(items.size() == 5);
    CHECK(items[0].enabled);         // 与 TA 聊天
    CHECK(items[1].enabled);         // 查看资料
    CHECK(items[2].id == kMentionMenuMentionMore);
    CHECK_FALSE(items[2].enabled);   // 人不在群里，再 @ 也送不到
    CHECK(items[3].enabled);         // 复制 TA 的昵称
    CHECK(items[4].id == kMentionMenuStarred);
    CHECK(items[4].enabled);         // 关注的是这位同事本人，跟他还在不在这个群无关
}

TEST_CASE("M7 @所有人 不弹菜单", "[mention][menu]")
{
    CHECK_FALSE(ShouldShowMentionMenu(kMentionUidAll));
    CHECK(BuildMentionMenuModel(kMentionUidAll, kMyUid, true, kOwner, kMember).empty());

    // 具体的人则该弹。
    CHECK(ShouldShowMentionMenu(kOtherUid));
}

TEST_CASE("M8 未登录时不把任何人误判成自己", "[mention][menu]")
{
    // selfUid 为 0 表示取不到本人 id，无从判定，按"不是自己"处理给全四项 ——
    // 宁可多给一项，也不要在正常场景下莫名其妙少掉三项。
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, 0, true, kMember, kMember);
    CHECK(items.size() == 5);
}

// =====================================================================
// M9-M16 —— 菜单末尾「设为 / 撤销管理员」的门控
// =====================================================================
//
// 规则与群管理窗口成员卡菜单共用 grouprole::CanSetAdmin，也与服务端
// CheckSetMemberRole 对齐：只有群主能设 / 撤，且不能对自己、不能对另一个群主。

// 在菜单模型里找某个 id 的项下标；没有返回 -1。
static int FindEntryIndex(const std::vector<MentionMenuEntry>& items, MentionMenuId id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && items[i].id == id)
        {
            return (int)i;
        }
    }
    return -1;
}

static bool HasEntry(const std::vector<MentionMenuEntry>& items, MentionMenuId id)
{
    return FindEntryIndex(items, id) >= 0;
}

TEST_CASE("M9 群主看普通成员：有设为管理员、无撤销管理员", "[mention][menu][admin]")
{
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kMember);

    CHECK(HasEntry(items, kMentionMenuSetAdmin));
    CHECK_FALSE(HasEntry(items, kMentionMenuCancelAdmin));

    //管理项排在日常动作那一组（五项）之后，中间隔一条分隔线。群主对普通成员既能
    //设管理员、又能移出，故管理项有两条：设为管理员 + 移出本群（移出排最末）。
    REQUIRE(items.size() == 8);
    CHECK(items[5].isSeparator);
    CHECK(items[6].id == kMentionMenuSetAdmin);
    CHECK(items[6].enabled);
    CHECK(items[7].id == kMentionMenuKick);
    CHECK(items[7].enabled);
}

TEST_CASE("M10 群主看管理员：有撤销管理员、无设为管理员", "[mention][menu][admin]")
{
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kAdmin);

    CHECK(HasEntry(items, kMentionMenuCancelAdmin));
    CHECK_FALSE(HasEntry(items, kMentionMenuSetAdmin));
}

TEST_CASE("M11 群主看另一个群主：两项都没有", "[mention][menu][admin]")
{
    //一群只有一个群主，此输入主要来自列表未刷新等异常情形；不该发出这个请求
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kOwner);

    CHECK_FALSE(HasEntry(items, kMentionMenuSetAdmin));
    CHECK_FALSE(HasEntry(items, kMentionMenuCancelAdmin));
    CHECK(items.size() == 5);
}

TEST_CASE("M12 管理员看任何角色：两项都没有", "[mention][menu][admin]")
{
    const int targets[] = { kMember, kAdmin, kOwner };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        INFO("目标角色 = " << targets[i]);
        std::vector<MentionMenuEntry> items =
            BuildMentionMenuModel(kOtherUid, kMyUid, true, kAdmin, targets[i]);
        CHECK_FALSE(HasEntry(items, kMentionMenuSetAdmin));
        CHECK_FALSE(HasEntry(items, kMentionMenuCancelAdmin));
    }
}

TEST_CASE("M13 普通成员看任何角色：两项都没有", "[mention][menu][admin]")
{
    const int targets[] = { kMember, kAdmin, kOwner };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        INFO("目标角色 = " << targets[i]);
        std::vector<MentionMenuEntry> items =
            BuildMentionMenuModel(kOtherUid, kMyUid, true, kMember, targets[i]);
        CHECK_FALSE(HasEntry(items, kMentionMenuSetAdmin));
        CHECK_FALSE(HasEntry(items, kMentionMenuCancelAdmin));
    }
}

TEST_CASE("M14 角色未知时两项都不出现", "[mention][menu][admin]")
{
    //单聊窗没装角色查询钩子、群资料还没拉到时都会走到这里。宁可不给，
    //也不要给一个点下去必定被服务端拒绝的菜单项。
    std::vector<MentionMenuEntry> a =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kUnknown, kMember);
    CHECK_FALSE(HasEntry(a, kMentionMenuSetAdmin));
    CHECK_FALSE(HasEntry(a, kMentionMenuCancelAdmin));

    //本人是群主但目标角色未知（他已不在群里）：同样不给
    std::vector<MentionMenuEntry> b =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kUnknown);
    CHECK_FALSE(HasEntry(b, kMentionMenuSetAdmin));
    CHECK_FALSE(HasEntry(b, kMentionMenuCancelAdmin));
}

TEST_CASE("M15 加了管理项后原有各项行为不变", "[mention][menu][admin]")
{
    //群主看普通成员：前四项的 id、顺序、可用性都与没有管理项时一致
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kMember);

    REQUIRE(items.size() >= 4);
    CHECK(items[0].id == kMentionMenuChat);
    CHECK(items[1].id == kMentionMenuProfile);
    CHECK(items[2].id == kMentionMenuMentionMore);
    CHECK(items[3].id == kMentionMenuCopyName);

    //「已退群时 @TA 置灰」这条既有规则在管理项出现时依然成立
    std::vector<MentionMenuEntry> left =
        BuildMentionMenuModel(kOtherUid, kMyUid, false, kOwner, kMember);
    const int idx = FindEntryIndex(left, kMentionMenuMentionMore);
    REQUIRE(idx >= 0);
    CHECK_FALSE(left[(size_t)idx].enabled);
}

TEST_CASE("M16 没有管理项时不留多余的分隔线", "[mention][menu][admin]")
{
    //普通成员的菜单应当干干净净五项，末尾不能挂一条孤零零的分隔线
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kMember, kMember);

    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK_FALSE(items[i].isSeparator);
    }

    //对自己右键时同样只有一项、没有分隔线
    std::vector<MentionMenuEntry> self =
        BuildMentionMenuModel(kMyUid, kMyUid, true, kOwner, kOwner);
    REQUIRE(self.size() == 1);
    CHECK_FALSE(self[0].isSeparator);
}

// =====================================================================
// M17-M23 —— 菜单末尾「移出本群」的门控
// =====================================================================
//
// 与设 / 撤管理员的门控<u>不同</u>：移出管理员也能做（只是够不到其他管理员和
// 群主），设 / 撤管理员则仅限群主。两项各问各的判定，故要分别断言。

TEST_CASE("M17 群主可移出管理员与普通成员", "[mention][menu][kick]")
{
    std::vector<MentionMenuEntry> onMember =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kMember);
    CHECK(HasEntry(onMember, kMentionMenuKick));

    std::vector<MentionMenuEntry> onAdmin =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kAdmin);
    CHECK(HasEntry(onAdmin, kMentionMenuKick));
}

TEST_CASE("M18 谁都不能移出群主", "[mention][menu][kick]")
{
    const int myRoles[] = { kOwner, kAdmin, kMember };
    for (size_t i = 0; i < sizeof(myRoles) / sizeof(myRoles[0]); ++i)
    {
        INFO("我的角色 = " << myRoles[i]);
        std::vector<MentionMenuEntry> items =
            BuildMentionMenuModel(kOtherUid, kMyUid, true, myRoles[i], kOwner);
        CHECK_FALSE(HasEntry(items, kMentionMenuKick));
    }
}

TEST_CASE("M19 管理员只能移出普通成员", "[mention][menu][kick]")
{
    //管理员看普通成员：能移出，但<u>不能</u>设管理员 —— 两项门控不同，一并断言
    std::vector<MentionMenuEntry> onMember =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kAdmin, kMember);
    CHECK(HasEntry(onMember, kMentionMenuKick));
    CHECK_FALSE(HasEntry(onMember, kMentionMenuSetAdmin));
    CHECK_FALSE(HasEntry(onMember, kMentionMenuCancelAdmin));

    //管理员看另一个管理员：移不动
    std::vector<MentionMenuEntry> onAdmin =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kAdmin, kAdmin);
    CHECK_FALSE(HasEntry(onAdmin, kMentionMenuKick));
}

TEST_CASE("M20 普通成员看不到移出本群", "[mention][menu][kick]")
{
    const int targets[] = { kMember, kAdmin, kOwner };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        INFO("目标角色 = " << targets[i]);
        std::vector<MentionMenuEntry> items =
            BuildMentionMenuModel(kOtherUid, kMyUid, true, kMember, targets[i]);
        CHECK_FALSE(HasEntry(items, kMentionMenuKick));
    }
}

TEST_CASE("M21 右键自己时不含移出本群", "[mention][menu][kick]")
{
    //即便本人是群主，对自己右键也只有「查看资料」——退群走的是另一条路
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kMyUid, kMyUid, true, kOwner, kOwner);
    REQUIRE(items.size() == 1);
    CHECK(items[0].id == kMentionMenuProfile);
    CHECK_FALSE(HasEntry(items, kMentionMenuKick));
}

TEST_CASE("M22 角色未知时不给移出本群", "[mention][menu][kick]")
{
    //单聊窗没装角色查询钩子、群资料还没拉到时都会走到这里
    std::vector<MentionMenuEntry> a =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kUnknown, kMember);
    CHECK_FALSE(HasEntry(a, kMentionMenuKick));

    //目标已不在群里：给了也点不动
    std::vector<MentionMenuEntry> b =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kUnknown);
    CHECK_FALSE(HasEntry(b, kMentionMenuKick));
}

TEST_CASE("M23 移出本群排在最末，且分隔线只有一条", "[mention][menu][kick]")
{
    //群主看普通成员：日常五项 + 分隔线 + 设为管理员 + 移出本群，共 8 行
    std::vector<MentionMenuEntry> byOwner =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kOwner, kMember);
    REQUIRE(byOwner.size() == 8);
    CHECK(byOwner[5].isSeparator);
    CHECK(byOwner[6].id == kMentionMenuSetAdmin);
    CHECK(byOwner[7].id == kMentionMenuKick);   //不可撤销的操作放在最远处

    //管理员看普通成员：没有角色项，只有移出本群 —— 分隔线仍只有一条，
    //且不能因为少了角色项就在末尾留一条孤零零的分隔线
    std::vector<MentionMenuEntry> byAdmin =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kAdmin, kMember);
    REQUIRE(byAdmin.size() == 7);
    CHECK(byAdmin[5].isSeparator);
    CHECK(byAdmin[6].id == kMentionMenuKick);
    int sepCount = 0;
    for (size_t i = 0; i < byAdmin.size(); ++i)
    {
        if (byAdmin[i].isSeparator)
        {
            ++sepCount;
        }
    }
    CHECK(sepCount == 1);
}

// =====================================================================
// M24-M26 —— 菜单里的「特别关注 / 取消特别关注」
// =====================================================================
//
// 这一项与主面板的联系人 / 会话右键菜单是同一个功能，文案由
// balloonmain::StarredModel::GetMenuLabel 统一给出。这里只验证 @片段菜单
// （群成员列表与气泡发送者头像共用同一份模型）把状态正确地传了下去。

TEST_CASE("M24 特别关注项的文案随当前状态取反", "[mention][menu][starred]")
{
    //未关注：显示"特别关注"，点下去是去关注
    std::vector<MentionMenuEntry> notStarred =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kMember, kMember, false);
    const int idxNot = FindEntryIndex(notStarred, kMentionMenuStarred);
    REQUIRE(idxNot >= 0);
    CHECK(std::wstring(notStarred[(size_t)idxNot].label) == L"特别关注");

    //已关注：显示"取消特别关注"
    std::vector<MentionMenuEntry> starred =
        BuildMentionMenuModel(kOtherUid, kMyUid, true, kMember, kMember, true);
    const int idxYes = FindEntryIndex(starred, kMentionMenuStarred);
    REQUIRE(idxYes >= 0);
    CHECK(std::wstring(starred[(size_t)idxYes].label) == L"取消特别关注");

    //文案必须与主面板两处菜单逐字一致 —— 同一个动作不该有两种叫法
    CHECK(std::wstring(balloonmain::StarredModel::GetMenuLabel(false)) == L"特别关注");
    CHECK(std::wstring(balloonmain::StarredModel::GetMenuLabel(true)) == L"取消特别关注");
}

TEST_CASE("M25 右键自己时没有特别关注项", "[mention][menu][starred]")
{
    //自己的上下线自己清楚，关注自己没有意义；@ 到自己时菜单只留「查看资料」
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kMyUid, kMyUid, true, kOwner, kOwner, true);
    REQUIRE(items.size() == 1);
    CHECK(items[0].id == kMentionMenuProfile);
    CHECK_FALSE(HasEntry(items, kMentionMenuStarred));
}

TEST_CASE("M26 成员已退群仍可特别关注", "[mention][menu][starred]")
{
    //关注的是这位同事本人（企业通讯录里的人），跟他还在不在这个群没有关系。
    //与「@TA」不同 —— 那一项退群后确实送不到，故置灰。
    std::vector<MentionMenuEntry> items =
        BuildMentionMenuModel(kOtherUid, kMyUid, false, kMember, kMember, false);
    const int idx = FindEntryIndex(items, kMentionMenuStarred);
    REQUIRE(idx >= 0);
    CHECK(items[(size_t)idx].enabled);
}

// =====================================================================
// D 组：群成员侧栏的成员行双击 —— 打开与该成员的单聊窗
//
//   成员行被双击时，向宿主群聊窗投递一条与右键菜单「与 TA 聊天」完全相同的
//   WM_CHAT_MENTION_ACTION，后续由群聊窗转请主面板打开或激活单聊窗（那一段是
//   右键菜单一直在用的既有路径，不在本组范围）。本组建一个不显示的 DuiHost 作为
//   宿主，直接调用成员行的 OnLButtonDblClk，再从宿主窗口的消息队列里取出消息
//   核对负载。系统层面的双击识别（DuiHost 的双击合成）属 balloonui 的职责，群聊窗
//   已经开启，也不在本组范围。
// =====================================================================

namespace {

// 宿主窗口尺寸（像素）。窗口不显示，尺寸只需非零。
const int kDblClkHostWidth  = 320;
const int kDblClkHostHeight = 240;

// 双击落点（宿主客户区坐标）。成员行不关心落点，任取一点即可。
const POINT kDblClkPt = { 10, 10 };

// 被双击成员的显示名与头像字。
const wchar_t* const kDblClkOtherName  = L"李四";
const wchar_t* const kDblClkAvatarText = L"李";

// 头像底色。本组不看绘制，任取一色即可。
const COLORREF kDblClkAvatarColor = RGB(0x4A, 0x90, 0xE2);

/**
 *  不显示的宿主窗口，作用域结束时销毁。
 *  成员行投递的目标是 GetHost()->m_hWnd，故必须是真实窗口。须比挂在它上面的成员行
 *  活得久：DuiControl 析构时会回头清理宿主里指向自己的指针。
 */
class ScopedHiddenHost
{
public:
    // 创建宿主窗口（WS_POPUP、不带 WS_VISIBLE，跑测试时屏幕上不会闪出窗口）。
    ScopedHiddenHost()
    {
        RECT rc = { 0, 0, kDblClkHostWidth, kDblClkHostHeight };
        m_host.Create(NULL, rc, _T("GroupMemberDblClkTests"), WS_POPUP);
    }

    // 销毁宿主窗口。窗口已被用例提前销毁时跳过。
    ~ScopedHiddenHost()
    {
        if (m_host.IsWindow())
        {
            m_host.DestroyWindow();
        }
    }

    // 返回宿主对象（由本类持有，调用方借用）。
    DuiHost& Host()
    {
        return m_host;
    }

private:
    DuiHost m_host;   // 宿主对象，生命周期与本类相同
};

/**
 *  造一行成员行。
 *    host：   要挂上去的宿主（借用）；传 NULL 表示不挂宿主。
 *    uid：    成员 userid；0 表示未绑定（mock / 占位行）。
 *    isSelf： 是否为本人那一行。
 *    netRole：该成员在本群的角色，取值见 grouprole::NetGroupRole。
 *  @return 新建的成员行，所有权归调用方。
 */
std::unique_ptr<GroupMemberItem> MakeMemberRow(DuiHost* host, long long uid,
                                               bool isSelf, int netRole)
{
    std::unique_ptr<GroupMemberItem> row(new GroupMemberItem());
    row->SetUserId(uid);
    row->SetIsSelf(isSelf);
    row->SetMember(CString(kDblClkOtherName), CString(kDblClkAvatarText),
                   kDblClkAvatarColor, netRole);
    row->SetMyNetRole(kMember);
    if (host != NULL)
    {
        row->AttachToHost(host);
    }
    return row;
}

/**
 *  取走消息队列里全部 WM_CHAT_MENTION_ACTION。
 *    hWnd：只取投给该窗口的；传 NULL 取本线程所有窗口的。
 *  @return 各条消息的负载。所有权转给调用方，用完须交给 DeleteMentionActions 释放
 *          （与宿主群聊窗处理完负责 delete 的约定一致）。
 */
std::vector<MentionActionData*> TakeMentionActions(HWND hWnd)
{
    std::vector<MentionActionData*> out;
    MSG msg;
    while (::PeekMessageW(&msg, hWnd, WM_CHAT_MENTION_ACTION, WM_CHAT_MENTION_ACTION, PM_REMOVE))
    {
        out.push_back((MentionActionData*)msg.lParam);
    }
    return out;
}

// 释放 TakeMentionActions 取出的全部负载。
void DeleteMentionActions(std::vector<MentionActionData*>& actions)
{
    for (size_t i = 0; i < actions.size(); ++i)
    {
        delete actions[i];
    }
    actions.clear();
}

} // anonymous namespace

TEST_CASE("D1 双击普通成员行投递「与 TA 聊天」，群主与管理员行同样有效", "[groupmember][dblclick]")
{
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());

    const int roles[] = { kMember, kAdmin, kOwner };
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
    {
        INFO("netRole=" << roles[i]);
        std::unique_ptr<GroupMemberItem> row =
            MakeMemberRow(&host.Host(), kOtherUid, false, roles[i]);

        CHECK(row->OnLButtonDblClk(kDblClkPt, 0));

        //与右键菜单选「与 TA 聊天」投的是同一条消息、同样的负载
        std::vector<MentionActionData*> actions = TakeMentionActions(host.Host().m_hWnd);
        REQUIRE(actions.size() == 1);
        CHECK(actions[0]->id == kMentionMenuChat);
        CHECK(actions[0]->uid == kOtherUid);
        CHECK(actions[0]->displayName == std::wstring(kDblClkOtherName));
        DeleteMentionActions(actions);
    }
}

TEST_CASE("D2 双击本人那一行什么都不做", "[groupmember][dblclick]")
{
    //主面板本来就拒绝和自己聊天，右键自己时菜单里也没有「与 TA 聊天」
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    std::unique_ptr<GroupMemberItem> row = MakeMemberRow(&host.Host(), kMyUid, true, kMember);

    row->OnLButtonDblClk(kDblClkPt, 0);

    std::vector<MentionActionData*> actions = TakeMentionActions(host.Host().m_hWnd);
    CHECK(actions.empty());
    DeleteMentionActions(actions);
}

TEST_CASE("D3 双击未绑定 userid 的行什么都不做", "[groupmember][dblclick]")
{
    //mock / 占位行没有可以聊天的对象
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    std::unique_ptr<GroupMemberItem> row = MakeMemberRow(&host.Host(), 0, false, kMember);

    row->OnLButtonDblClk(kDblClkPt, 0);

    std::vector<MentionActionData*> actions = TakeMentionActions(host.Host().m_hWnd);
    CHECK(actions.empty());
    DeleteMentionActions(actions);
}

TEST_CASE("D4 没有可用的宿主窗口时不投递、不崩溃", "[groupmember][dblclick]")
{
    SECTION("成员行没有挂到任何宿主上")
    {
        std::unique_ptr<GroupMemberItem> row = MakeMemberRow(NULL, kOtherUid, false, kMember);

        row->OnLButtonDblClk(kDblClkPt, 0);

        std::vector<MentionActionData*> actions = TakeMentionActions(NULL);
        CHECK(actions.empty());
        DeleteMentionActions(actions);
    }

    SECTION("宿主窗口已经销毁")
    {
        ScopedHiddenHost host;
        REQUIRE(host.Host().IsWindow());
        std::unique_ptr<GroupMemberItem> row =
            MakeMemberRow(&host.Host(), kOtherUid, false, kMember);
        host.Host().DestroyWindow();

        row->OnLButtonDblClk(kDblClkPt, 0);

        //窗口已不存在，只能按本线程的整个队列查
        std::vector<MentionActionData*> actions = TakeMentionActions(NULL);
        CHECK(actions.empty());
        DeleteMentionActions(actions);
    }
}
