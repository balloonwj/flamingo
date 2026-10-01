/**
 *  ChatForwardSelectionTests.cpp
 *
 *  聊天列表多选转发的测试（标签 [forward]），见 docs/聊天消息转发方案.md §7.2。
 *
 *    FWD-C7  ChatMessageList::GetSelectedForwardMsgids：按 msgid 升序去重；单聊里收到的
 *            文件行取 fileMsgid；发送中 / 发送失败的行、文件夹行不收并计入跳过条数；
 *            只读，不改选中态、不退出多选；未处于多选模式时返回空。
 *    FWD-C8  多选操作条四等分为「取消 / 逐条转发 / 合并转发 / 删除」：命中判定与按钮矩形
 *            一致；点转发按钮时，有可转发的行才响应并退出多选，否则不响应。
 *    FWD-C9  合并转发的标题（balloonmain::BuildMergedForwardTitle）：单聊、群聊两种格式，
 *            名字缺失时的写法，超长截断且不留下半个代理对。
 *    FWD-C13 群聊回显去重（ChatMessageList::ContainsMessage）：按 clientmsgid 或 msgid
 *            （含文件行的 fileMsgid）判断列表里是否已有同一条。
 *    FWD-C14 合并卡片（ChatMergedCard）：预览最多 4 行、条数、缺名字与非文字子消息的写法、
 *            嵌套记录显示「[聊天记录]」；content 不可用时退化为只有缺省标题的卡片。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存：不创建窗口、不要 HWND。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatMergedCard.h"
#include "../UI/BalloonMain/ForwardRequest.h"
#include "../net/Protocol.h"   // conversation_single / conversation_group

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区宽度 / 高度（逻辑像素），足够放下本组用例里的全部消息行。
const int kListWidth  = 400;
const int kListHeight = 800;

// 消息时间轴的起点（Unix 毫秒）与间隔。一分钟远小于插时间分隔行的阈值（5 分钟），
// 本组用例的消息之间都不插分隔行，只有列表开头的那一行。
const long long kT0       = 1700000000000LL;
const long long kMinuteMs = 60LL * 1000;

// 造一条对方发来的文字消息。msgid：服务器 msgid，0 表示发送中 / 发送失败。
ChatMessage MakeText(long long msgid, long long timestampMs)
{
    ChatMessage m = MakeTextMessage(false, _T("转发测试"));
    m.msgid = msgid;
    m.timestampMs = timestampMs;
    return m;
}

// 按测试宽度排版。
void LayoutList(ChatMessageList& list)
{
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
}

// 取第 index 个子控件对应的气泡；不是气泡时测试失败。
ChatBubble* BubbleAt(ChatMessageList& list, size_t index)
{
    REQUIRE(index < list.Children().size());
    ChatBubble* b = dynamic_cast<ChatBubble*>(list.Children()[index].get());
    REQUIRE(b != NULL);
    return b;
}

} // anonymous namespace

TEST_CASE("FWD-C7 GetSelectedForwardMsgids 按 msgid 升序、文件行取 fileMsgid、跳过不能转发的行", "[forward]")
{
    // 单聊里收到的文件：服务器 msgid 放在 fileMsgid 里，msgid 为 0
    ChatMessage file = MakeIncomingFileMessage(false, _T("排期表.xlsx"), 1024, 3, "77", "md5", 6);
    file.timestampMs = kT0 + 3 * kMinuteMs;
    REQUIRE(file.msgid == 0);
    REQUIRE(file.fileMsgid == 3);

    // 文件夹行：即便带着 msgid 也不能转发
    ChatMessage folder = MakeText(9, kT0 + 5 * kMinuteMs);
    folder.kind = kMsgFolder;

    // [分隔, 7, 0(发送失败), 文件3, 5, 文件夹9]：显示顺序与 msgid 顺序不一致
    ChatMessageList list;
    list.AppendMessage(MakeText(7, kT0 + kMinuteMs));
    list.AppendMessage(MakeText(0, kT0 + 2 * kMinuteMs));
    list.AppendMessage(file);
    list.AppendMessage(MakeText(5, kT0 + 4 * kMinuteMs));
    list.AppendMessage(folder);
    LayoutList(list);
    REQUIRE(list.Children().size() == 6);

    // 未处于多选模式：返回空，跳过条数为 0
    size_t skipped = 99;
    CHECK(list.GetSelectedForwardMsgids(&skipped).empty());
    CHECK(skipped == 0);

    // 选中全部五条消息行
    list.EnterMultiSelectWith(BubbleAt(list, 1));
    for (size_t i = 2; i <= 5; ++i)
    {
        BubbleAt(list, i)->SetSelected(true);
    }
    REQUIRE(list.IsMultiSelectMode());

    const std::vector<long long> ids = list.GetSelectedForwardMsgids(&skipped);
    std::vector<long long> expect;
    expect.push_back(3);
    expect.push_back(5);
    expect.push_back(7);
    CHECK(ids == expect);
    // 发送失败的那行与文件夹行
    CHECK(skipped == 2);

    // 只读：不退出多选、不改选中态
    CHECK(list.IsMultiSelectMode());
    for (size_t i = 1; i <= 5; ++i)
    {
        CHECK(BubbleAt(list, i)->IsSelected());
    }

    // 跳过条数的出参可以为 NULL
    CHECK(list.GetSelectedForwardMsgids(NULL) == expect);

    // 只选中不能转发的行：结果为空
    for (size_t i = 1; i <= 5; ++i)
    {
        BubbleAt(list, i)->SetSelected(false);
    }
    BubbleAt(list, 2)->SetSelected(true);
    CHECK(list.GetSelectedForwardMsgids(&skipped).empty());
    CHECK(skipped == 1);
}

TEST_CASE("FWD-C8a 多选操作条按四等分命中四个按钮，条外不命中", "[forward]")
{
    const RECT bar = { 100, 500, 500, 548 };   // 宽 400，每段 100
    const int midY = 524;

    const POINT cancel  = { 150, midY };
    const POINT each    = { 250, midY };
    const POINT merged  = { 350, midY };
    const POINT del     = { 450, midY };
    CHECK(MultiSelectBarHitTest(bar, cancel) == kBarButtonCancel);
    CHECK(MultiSelectBarHitTest(bar, each)   == kBarButtonForwardEach);
    CHECK(MultiSelectBarHitTest(bar, merged) == kBarButtonForwardMerged);
    CHECK(MultiSelectBarHitTest(bar, del)    == kBarButtonDelete);

    // 分界点属于右侧那一段；最右一列像素仍属「删除」
    const POINT edge1 = { 200, midY };
    const POINT edge3 = { 400, midY };
    const POINT last  = { 499, midY };
    CHECK(MultiSelectBarHitTest(bar, edge1) == kBarButtonForwardEach);
    CHECK(MultiSelectBarHitTest(bar, edge3) == kBarButtonDelete);
    CHECK(MultiSelectBarHitTest(bar, last)  == kBarButtonDelete);

    // 条外：上方、右边界之外
    const POINT above = { 250, 499 };
    const POINT right = { 500, midY };
    CHECK(MultiSelectBarHitTest(bar, above) == kBarButtonNone);
    CHECK(MultiSelectBarHitTest(bar, right) == kBarButtonNone);

    // 按钮矩形与命中判定一致：每个按钮矩形的中心点命中它自己
    const MultiSelectBarButton buttons[] = {
        kBarButtonCancel, kBarButtonForwardEach, kBarButtonForwardMerged, kBarButtonDelete
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        const RECT r = MultiSelectBarButtonRect(bar, buttons[i]);
        const POINT center = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
        CHECK(MultiSelectBarHitTest(bar, center) == buttons[i]);
    }
}

TEST_CASE("FWD-C8b 点转发按钮：有可转发的行时退出多选，没有时不响应", "[forward]")
{
    const int listHeight = 300;
    ChatMessageList list;
    list.AppendMessage(MakeText(1, kT0 + kMinuteMs));
    list.AppendMessage(MakeText(0, kT0 + 2 * kMinuteMs));
    RECT rc = { 0, 0, kListWidth, listHeight };
    list.ForceLayout(rc);

    // 操作条占据列表底部；每段宽 kListWidth / 4
    const int barMidY = listHeight - 10;
    const POINT each   = { kListWidth * 3 / 8, barMidY };
    const POINT merged = { kListWidth * 5 / 8, barMidY };

    // 只选中发送失败的那行：两个转发按钮都不响应，仍在多选模式
    list.EnterMultiSelectWith(BubbleAt(list, 2));
    REQUIRE(list.IsMultiSelectMode());
    CHECK(list.OnLButtonDown(each, MK_LBUTTON));
    CHECK(list.IsMultiSelectMode());
    CHECK(list.OnLButtonDown(merged, MK_LBUTTON));
    CHECK(list.IsMultiSelectMode());

    // 再选中能转发的那行：点「合并转发」后退出多选（列表不在窗口里，投给宿主这一步跳过）
    BubbleAt(list, 1)->SetSelected(true);
    CHECK(list.OnLButtonDown(merged, MK_LBUTTON));
    CHECK_FALSE(list.IsMultiSelectMode());

    // 同样适用于「逐条转发」
    list.EnterMultiSelectWith(BubbleAt(list, 1));
    CHECK(list.OnLButtonDown(each, MK_LBUTTON));
    CHECK_FALSE(list.IsMultiSelectMode());
}

TEST_CASE("FWD-C13 群聊回显去重：按 clientmsgid 或 msgid 判断列表里是否已有同一条", "[forward]")
{
    ChatMessageList list;

    // 本端刚发出、回执还没回填的乐观气泡：只有 clientmsgid
    ChatMessage pending = MakeTextMessage(true, _T("刚发出的"));
    pending.clientMsgId = "c-1";
    pending.timestampMs = kT0 + kMinuteMs;
    list.AppendMessage(pending);

    // 回执已回填的气泡：有 msgid
    ChatMessage acked = MakeText(55, kT0 + 2 * kMinuteMs);
    acked.fromMe = true;
    acked.clientMsgId = "c-2";
    list.AppendMessage(acked);

    // 单聊里收到的文件：msgid 放在 fileMsgid
    ChatMessage file = MakeIncomingFileMessage(false, _T("a.txt"), 10, 66, "f", "m", 6);
    file.timestampMs = kT0 + 3 * kMinuteMs;
    list.AppendMessage(file);
    LayoutList(list);

    // 普通发送的回显：乐观气泡已在，按 clientmsgid 命中（回执先到或后到都一样）
    CHECK(list.ContainsMessage(101, "c-1"));
    // 回执已回填：按 msgid 命中
    CHECK(list.ContainsMessage(55, "另一个"));
    // 文件行按 fileMsgid 命中
    CHECK(list.ContainsMessage(66, ""));

    // 转发到本群的回显：列表里没有，应当显示
    CHECK_FALSE(list.ContainsMessage(77, "fwd-1-0"));
    // msgid 为 0、clientmsgid 为空：不命中任何行（不会把时间分隔行当成同一条）
    CHECK_FALSE(list.ContainsMessage(0, ""));

    // 显示之后再收到同一条（重复推送）：命中
    list.AppendMessage(MakeText(77, kT0 + 4 * kMinuteMs));
    CHECK(list.ContainsMessage(77, "fwd-1-0"));
}

// 一条合并记录的 content：6 条子消息，含无 fromname 的、图片、内联表情、嵌套的合并记录
static const char* const kCardContent =
    "{\"title\":\"项目周会的聊天记录\",\"messages\":["
    "{\"msgid\":\"1\",\"from\":\"100001\",\"msgtype\":1,\"content\":{\"text\":\"第一条\"},\"fromname\":\"张三\"},"
    "{\"msgid\":\"2\",\"from\":\"100004\",\"msgtype\":1,\"content\":{\"text\":\"没有名字\"}},"
    "{\"msgid\":\"3\",\"from\":\"100002\",\"msgtype\":2,\"content\":{\"fileid\":\"9\"},\"fromname\":\"李四\"},"
    "{\"msgid\":\"4\",\"from\":\"100001\",\"msgtype\":15,\"content\":{\"title\":\"内层\",\"messages\":[]},\"fromname\":\"张三\"},"
    "{\"msgid\":\"5\",\"from\":\"100002\",\"msgtype\":1,\"content\":{\"text\":\"第五条\"},\"fromname\":\"李四\"},"
    "{\"msgid\":\"6\",\"from\":\"100002\",\"msgtype\":1,\"content\":{\"text\":\"第六条\"},\"fromname\":\"李四\"}]}";

TEST_CASE("FWD-C14 合并卡片的预览：标题、最多 4 行、条数与各类子消息的摘要", "[forward]")
{
    MergedCardPreview preview;
    REQUIRE(BuildMergedCardPreview(kCardContent, preview));
    CHECK(preview.m_title == _T("项目周会的聊天记录"));
    CHECK(preview.m_count == 6);
    REQUIRE(preview.m_lines.size() == kMergedCardPreviewLines);
    CHECK(preview.m_lines[0] == _T("张三：第一条"));
    // 没有 fromname：退回「用户<id>」
    CHECK(preview.m_lines[1] == _T("用户100004：没有名字"));
    // 非文字消息取类型占位
    CHECK(preview.m_lines[2] == _T("李四：[图片]"));
    // 嵌套的合并记录显示为「[聊天记录]」
    CHECK(preview.m_lines[3] == _T("张三：[聊天记录]"));

    // 套用到一条气泡上：变成合并卡片，原 content 留着供打开查看窗
    ChatMessage msg = MakeTextMessage(false, _T("占位"));
    ApplyMergedCard(msg, kCardContent);
    CHECK(msg.kind == kMsgMerged);
    CHECK(msg.mergedTitle == _T("项目周会的聊天记录"));
    CHECK(msg.mergedCount == 6);
    CHECK(msg.mergedLines.size() == kMergedCardPreviewLines);
    CHECK(msg.mergedContent == kCardContent);
    // 引用回复、复制等走 text 的地方拿到的是类型占位
    CHECK(msg.text == _T("[聊天记录]"));

    // content 不可用：仍是一张合并卡片，标题取缺省值、没有预览行
    ChatMessage broken = MakeTextMessage(false, _T("占位"));
    ApplyMergedCard(broken, "not json");
    CHECK(broken.kind == kMsgMerged);
    CHECK(broken.mergedTitle == _T("聊天记录"));
    CHECK(broken.mergedCount == 0);
    CHECK(broken.mergedLines.empty());
    MergedCardPreview none;
    CHECK_FALSE(BuildMergedCardPreview("not json", none));
}

TEST_CASE("FWD-C9 合并转发的标题：单聊、群聊两种格式与超长截断", "[forward]")
{
    using balloonmain::BuildMergedForwardTitle;
    using balloonmain::kForwardTitleMaxChars;

    // 单聊：双方名字都在
    CHECK(BuildMergedForwardTitle(conversation_single, L"李四", L"张三") == L"李四和张三的聊天记录");
    // 单聊：只有一方有名字
    CHECK(BuildMergedForwardTitle(conversation_single, L"", L"张三") == L"张三的聊天记录");
    CHECK(BuildMergedForwardTitle(conversation_single, L"李四", L"") == L"李四的聊天记录");
    // 单聊：都没有名字
    CHECK(BuildMergedForwardTitle(conversation_single, L"", L"") == L"聊天记录");

    // 群聊：取群名，本人名字不参与
    CHECK(BuildMergedForwardTitle(conversation_group, L"李四", L"项目组") == L"项目组的聊天记录");
    CHECK(BuildMergedForwardTitle(conversation_group, L"李四", L"") == L"群聊的聊天记录");

    // 超长：截到上限，是完整标题的前缀
    const std::wstring longName(kForwardTitleMaxChars, L'群');
    const std::wstring cut = BuildMergedForwardTitle(conversation_group, L"", longName);
    CHECK(cut.size() == kForwardTitleMaxChars);
    CHECK(cut == longName);

    // 截断点落在代理对中间：连同高位一起舍去，不留下半个字符
    std::wstring emojiName(kForwardTitleMaxChars - 1, L'a');
    emojiName += L"\xD83D\xDE00";   // 一个补充平面字符（笑脸），占两个 UTF-16 单元
    const std::wstring emojiCut = BuildMergedForwardTitle(conversation_group, L"", emojiName);
    CHECK(emojiCut == std::wstring(kForwardTitleMaxChars - 1, L'a'));
}
