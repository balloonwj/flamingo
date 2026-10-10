/**
 *  ChatWindowLayoutTests.cpp
 *
 *  P1a 测试：验证 ChatWindowLayout 的种子函数 BuildChatWindowBody() 产出
 *  的 balloonui 控件树符合"控件点名契约" —— 单聊窗所有需要被窗口代码
 *  引用的控件都能按既定数字 id 找到、类型正确。
 *
 *  覆盖用例（与需求确认一致）：
 *    W1-W7  单聊窗布局契约
 *    C1     CtrlId enum 自洽
 *    IS1-IS4 输入框的滚动条与消息区统一：内置滚动条不出现、细滑块几何、拖动、文字不被滑块盖住
 *    IS5    没有任何变化时重复绘制输入框，不再向引擎查询滚动状态（避免绘制死循环饿死计时器）
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 + 断言。全局 _Module 由 BalloonChatLayout
 *  Tests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatWindowLayout.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"   // 验证分割记忆单/群共用
#include "../UI/BalloonChatWnd/ChatBannerLabel.h"        // 顶部状态提示带（带底色的 DuiLabel 子类）
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../net/MessageProtocolClient.h"   // canRecallMessage（验证回填前后撤回条件的变化）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../UI/BalloonChatWnd/ChatIconButton.h"   // W21 / G6h：截图下拉按钮 ▾ 的控件类型
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // 读头部按钮的布局提示（W18 / G6c）
#include "../UI/BalloonChatWnd/ChatInputRichEdit.h"
#include "../UI/BalloonChatWnd/ChatScrollThumb.h"   // IS2：与消息区共用的细滚动条常量

#include <gdiplus.h>   // IS5：绘制带滑块的输入框
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"   // 消息区/输入区可拖分隔

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// W. 单聊窗布局契约
// =====================================================================

TEST_CASE("W1 BuildChatWindowBody 返回非空控件树", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("W2 单聊窗含消息区容器", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);
    CHECK(root->FindCtrlById(kIdMessageArea) != nullptr);
}

TEST_CASE("W3 单聊窗输入框已迁移为无窗口富文本控件", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    DuiControl* input = root->FindCtrlById(kIdInputEdit);
    REQUIRE(input != nullptr);
    // 输入框换过两次：先由纯文本输入框换成富文本，再由早先内嵌真 Win32 输入框
    // 子窗口的那个换成无窗口的 ChatInputRichEdit（回车发送 / @提醒等定制都在
    // 它里面）。
    CHECK(dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(input) != nullptr);
    // 确认确实换掉了——不再是纯文本的 DuiEdit（二者非继承关系，互斥）。
    CHECK(dynamic_cast<DuiEdit*>(input) == nullptr);
}

TEST_CASE("W14 单聊窗输入框配置：多行 / 自动换行 / 有占位符", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    balloonchatwnd::ChatInputRichEdit* input =
        dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(root->FindCtrlById(kIdInputEdit));
    REQUIRE(input != nullptr);
    CHECK(input->IsMultiLine());
    CHECK(input->IsWordWrap());
    CHECK_FALSE(input->GetPlaceholder().IsEmpty());
}

TEST_CASE("W15 单聊窗输入框文本 round-trip（缓存层）", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    balloonchatwnd::ChatInputRichEdit* input =
        dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(root->FindCtrlById(kIdInputEdit));
    REQUIRE(input != nullptr);

    // 测试环境不创建窗口，SetText / GetText 直接读写控件内部保存的文本，
    // 与旧控件等价，保证发送时读取文本的通道在迁移后不变。
    input->SetText(_T("你好"));
    CHECK(input->GetText() == CString(_T("你好")));
    input->SetText(_T(""));
    CHECK(input->GetText().IsEmpty());
}

TEST_CASE("W4 单聊窗含发送按钮", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    auto* send = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdSendButton));
    REQUIRE(send != nullptr);
    CHECK_FALSE(send->GetText().IsEmpty());
}

TEST_CASE("W5 单聊窗含头部四个图标按钮", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    const UINT hdrIds[] = { kIdHdrVoice, kIdHdrVideo, kIdHdrRemoteDesktop, kIdHdrMore };
    for (UINT id : hdrIds)
    {
        INFO("header button id = " << id);
        CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(id)) != nullptr);
    }
}

TEST_CASE("W5b 单聊窗头部不再有清空聊天记录按钮（2026-10-03 移到消息记录窗）", "[p1a][chatwnd][clearconv]")
{
    //416 是原先扫帚按钮的 id，枚举已删除，这里写成字面量
    const UINT kRetiredClearId = 416;
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);
    CHECK(root->FindCtrlById(kRetiredClearId) == nullptr);
    //「…」仍在头部，「消息记录」按钮也还在
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kIdHdrMore)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kIdHdrHistory)) != nullptr);
}

TEST_CASE("W6 单聊窗含输入工具栏六个图标按钮", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    const UINT tbIds[] =
    {
        kIdTbEmoji, kIdTbImage, kIdTbFile, kIdTbScreenshot, kIdTbVoice, kIdTbPlus
    };
    for (UINT id : tbIds)
    {
        INFO("toolbar button id = " << id);
        CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(id)) != nullptr);
    }
}

TEST_CASE("W7 单聊窗布局所有控件 id 互不重复", "[p1a][chatwnd]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateId(ids));
}

// =====================================================================
// C. CtrlId enum 自洽
// =====================================================================

TEST_CASE("C1 单聊窗 CtrlId 各枚举值互不重复", "[p1a][chatwnd][ids]")
{
    const std::vector<UINT> allIds =
    {
        kIdMessageArea, kIdInputEdit, kIdSendButton, kIdInputSplitter,
        kIdHdrVoice, kIdHdrVideo, kIdHdrRemoteDesktop, kIdHdrScreenShare, kIdHdrMore,
        kIdTbEmoji, kIdTbImage, kIdTbFile, kIdTbScreenshot, kIdTbVoice, kIdTbPlus,
        kIdTbFont, kIdTbShake, kIdTbFolder, kIdHdrShare, kIdTbRecord, kIdTbScreenshotMenu,
    };
    CHECK_FALSE(HasDuplicateId(allIds));
}

TEST_CASE("W18 单聊窗的「文件共享」按钮在头部、紧挨在「共享屏幕」右边，输入框工具栏里不再有", "[chatwnd][fileshare-entry]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* screenShare = root->FindCtrlById(kIdHdrScreenShare);
    DuiControl* share = root->FindCtrlById(kIdHdrShare);
    DuiControl* folder = root->FindCtrlById(kIdTbFolder);
    REQUIRE(dynamic_cast<DuiButton*>(share) != nullptr);
    REQUIRE(screenShare != nullptr);
    REQUIRE(folder != nullptr);
    //与「共享屏幕」同在头部那一行，不在「发送文件夹」所在的输入框工具栏里
    REQUIRE(share->GetParent() == screenShare->GetParent());
    CHECK(share->GetParent() != folder->GetParent());
    const auto& kids = share->GetParent()->Children();
    size_t screenAt = kids.size();
    size_t shareAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == screenShare)
            screenAt = i;
        if (kids[i].get() == share)
            shareAt = i;
    }
    CHECK(shareAt == screenAt + 1);
    //与头部其它按钮同宽
    balloonwjui::DuiLayout* row = dynamic_cast<balloonwjui::DuiLayout*>(share->GetParent());
    REQUIRE(row != nullptr);
    CHECK(row->GetHint(share).fixedMain == row->GetHint(screenShare).fixedMain);
    CHECK(kIdHdrShare == 431);
}

// =====================================================================
// S. 消息区/输入区可拖分隔条（拖工具栏上边缘改两者高度）+ 会话内记忆
// =====================================================================

TEST_CASE("W16 单聊窗消息区与输入区之间有可拖的横向分隔条", "[chatwnd][splitter]")
{
    auto root = BuildChatWindowBody(false);
    REQUIRE(root != nullptr);

    DuiSplitter* sp = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kIdInputSplitter));
    REQUIRE(sp != nullptr);
    // 横向分隔条：两 pane 上下叠（消息区在上、输入区块在下）。
    CHECK(sp->GetOrientation() == DuiSplitter::Horizontal);

    // pane0 内含消息区气泡列表，pane1 内含输入框 —— 拖分隔条即在二者间重分高度。
    DuiControl* pane0 = sp->GetPane(0);
    DuiControl* pane1 = sp->GetPane(1);
    REQUIRE(pane0 != nullptr);
    REQUIRE(pane1 != nullptr);
    CHECK(pane0->FindCtrlById(kIdMessageArea) != nullptr);
    CHECK(pane1->FindCtrlById(kIdInputEdit)   != nullptr);

    // 此处原有一条断言，检查消息区不是内嵌真实 Win32 子窗口的控件。balloonui
    // 已经删除了这套设施，库内全部控件都是纯 DUI 绘制，该检查失去了对象，故移除。

    // 布局到固定尺寸后：消息区在上、输入区在下，分隔线处可被命中（= 可拖）。
    sp->Layout(RECT{ 0, 0, 600, 500 });
    RECT rcMsg   = pane0->GetRect();
    RECT rcInput = pane1->GetRect();
    CHECK(rcMsg.bottom <= rcInput.top);
    RECT bar = sp->GetBarRect();
    POINT onBar = { 300, (bar.top + bar.bottom) / 2 };
    CHECK(sp->IsPointInBar(onBar));
}

TEST_CASE("W17 输入区块高度在会话内被记住，窗口缩放时保持固定", "[chatwnd][splitter]")
{
    // 访问器 round-trip：<=0 复位为默认，再记一个自定义高度 240。
    RememberChatInputHeightPx(0);
    CHECK(GetRememberedChatInputHeightPx() == kChatInputBlockDefaultPx);
    RememberChatInputHeightPx(240);
    CHECK(GetRememberedChatInputHeightPx() == 240);

    // 模拟聊天窗布局：在两个不同窗口高度下，按窗口 OnRelayout 的做法把输入区块
    // 固定在记住的 240 —— 两种高度下输入区块都应稳定为 240，窗口多出的高度全部
    // 由消息区吃掉（验证"输入区固定、消息区撑满"）。
    auto root = BuildChatWindowBody(false);
    REQUIRE(root != nullptr);
    DuiSplitter* sp = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kIdInputSplitter));
    REQUIRE(sp != nullptr);
    DuiControl* msg   = sp->GetPane(0);
    DuiControl* input = sp->GetPane(1);
    REQUIRE(msg   != nullptr);
    REQUIRE(input != nullptr);

    // —— 较矮窗口（高 500）——
    sp->Layout(RECT{ 0, 0, 600, 500 });
    {
        int track = (sp->GetRect().bottom - sp->GetRect().top) - sp->GetBarThickness();
        sp->SetSplitPx(track - GetRememberedChatInputHeightPx());
    }
    int inputShort = input->GetRect().bottom - input->GetRect().top;
    int msgShort   = msg->GetRect().bottom   - msg->GetRect().top;
    CHECK(inputShort == 240);

    // —— 较高窗口（高 700，+200）——
    sp->Layout(RECT{ 0, 0, 600, 700 });
    {
        int track = (sp->GetRect().bottom - sp->GetRect().top) - sp->GetBarThickness();
        sp->SetSplitPx(track - GetRememberedChatInputHeightPx());
    }
    int inputTall = input->GetRect().bottom - input->GetRect().top;
    int msgTall   = msg->GetRect().bottom   - msg->GetRect().top;
    CHECK(inputTall == 240);            // 输入区块仍固定为 240
    CHECK(msgTall - msgShort == 200);   // 多出的 200px 全部给了消息区

    // 群聊窗与单聊共用同一记忆 —— 新建群聊窗、布局后同样把输入区固定为 240。
    auto groot = BuildGroupChatWindowBody(false);
    REQUIRE(groot != nullptr);
    DuiSplitter* gsp = dynamic_cast<DuiSplitter*>(groot->FindCtrlById(kGIdInputSplitter));
    REQUIRE(gsp != nullptr);
    DuiControl* ginput = gsp->GetPane(1);
    REQUIRE(ginput != nullptr);
    gsp->Layout(RECT{ 0, 0, 400, 600 });
    {
        int track = (gsp->GetRect().bottom - gsp->GetRect().top) - gsp->GetBarThickness();
        gsp->SetSplitPx(track - GetRememberedChatInputHeightPx());
    }
    CHECK((ginput->GetRect().bottom - ginput->GetRect().top) == 240);

    // 复位，避免污染后续用例（W16 等依赖默认高度）。
    RememberChatInputHeightPx(0);
}

// =====================================================================
// W8-W13 气泡折行 / 右键菜单动作 / 多选模式契约
// =====================================================================

namespace {

// 一段足够长、必然超过气泡最大宽度的中文文字（用于折行测试）。
const TCHAR* const kLongText =
    _T("这是一段刻意写得很长的测试文字，用来验证当聊天文字超过气泡最大")
    _T("宽度时能够在气泡内部自动折行，并且气泡的高度会随折行后的行数一起")
    _T("增长，而不是把文字截断或者画到气泡外面去。");

// 新建一个承载指定文字的 ChatBubble（裸指针，调用方负责移交所有权）。
ChatBubble* NewTextBubble(bool fromMe, const TCHAR* text)
{
    ChatBubble* b = new ChatBubble();
    b->SetMessage(MakeTextMessage(fromMe, CString(text)));
    return b;
}

} // anonymous namespace

TEST_CASE("W8 长文字气泡：窄宽度折行更多、行高更大", "[chatwnd][wrap]")
{
    ChatBubble bubble;
    bubble.SetMessage(MakeTextMessage(false, CString(kLongText)));

    bubble.Measure(900);
    const int hWide = bubble.GetRowHeight();

    bubble.Measure(260);
    const int hNarrow = bubble.GetRowHeight();

    // 同一段长文字，气泡可用宽度越窄折行越多 → 行高越大。
    CHECK(hNarrow > hWide);
}

TEST_CASE("W9 短文字气泡：折行测量后仍接近单行高", "[chatwnd][wrap]")
{
    ChatBubble bubble;
    bubble.SetMessage(MakeTextMessage(false, CString(_T("好的"))));

    bubble.Measure(600);
    // 一两个字的短消息不应被撑高 —— 行高保持在单行量级。
    CHECK(bubble.GetRowHeight() < 90);
}

TEST_CASE("W10 ChatMessageList 多选模式可开关", "[chatwnd][multiselect]")
{
    ChatMessageList list;
    CHECK_FALSE(list.IsMultiSelectMode());

    list.SetMultiSelectMode(true);
    CHECK(list.IsMultiSelectMode());

    list.SetMultiSelectMode(false);
    CHECK_FALSE(list.IsMultiSelectMode());
}

TEST_CASE("W11 请求撤回不就地改界面，改动等服务端推送后才发生", "[chatwnd][recall]")
{
    // 撤回接入服务端之后语义变了：RequestRecallBubble 只把 msgid 投给宿主聊天窗去发
    // cmd_recall_message，界面一律等服务端推回 cmd_message_recalled_notify 时才改
    // （走 RecallMessageByMsgid）。这样本端与对端看到的结果必然一致，也不会出现
    // "本地已撤回、服务端其实拒绝了"的假象。
    ChatMessageList list;
    ChatBubble* bubble = NewTextBubble(true, _T("一条将被撤回的消息"));
    list.AddChild(std::unique_ptr<DuiControl>(bubble));

    list.RequestRecallBubble(bubble);
    // 本行仍是普通文本消息，没有被就地改成提示行。
    CHECK(bubble->GetMessage().kind != kMsgTime);
}

TEST_CASE("W11b 收到撤回推送后按 msgid 把该行变成提示行", "[chatwnd][recall]")
{
    ChatMessageList list;
    ChatBubble* bubble = NewTextBubble(true, _T("一条将被撤回的消息"));
    // 服务器 msgid 是推送与本地行对回的唯一依据。
    ChatMessage msg = bubble->GetMessage();
    msg.msgid = 12345;
    bubble->SetMessage(msg);
    list.AddChild(std::unique_ptr<DuiControl>(bubble));

    const CString kTip = _T("你撤回了一条消息");
    CHECK(list.RecallMessageByMsgid(12345, kTip));
    CHECK(bubble->GetMessage().kind == kMsgTime);
    CHECK(bubble->GetMessage().text == kTip);
}

TEST_CASE("W11d 回执按 clientMsgId 回填 msgid 与时间戳，回填后才可能撤回", "[chatwnd][recall]")
{
    // 这条锁定的是"撤回菜单为什么会出现"的前置条件：本端刚发出的气泡 msgid 与
    // timestampMs 都是 0，此时不可撤回；回执回填之后才满足 canRecallMessage。
    ChatMessageList list;
    ChatBubble* bubble = NewTextBubble(true, _T("刚发出的消息"));
    ChatMessage msg = bubble->GetMessage();
    msg.clientMsgId = "cmid-abc";
    bubble->SetMessage(msg);
    list.AddChild(std::unique_ptr<DuiControl>(bubble));

    // 回填前：两个字段都是 0，撤回条件不成立。
    CHECK(bubble->GetMessage().msgid == 0);
    CHECK(bubble->GetMessage().timestampMs == 0);
    CHECK_FALSE(net::MessageProtocolClient::canRecallMessage(
        bubble->GetMessage().msgid, bubble->GetMessage().timestampMs, 1700000000000LL));

    // 回填后：两个字段就位，且在时限内 → 可撤回。
    CHECK(list.BackfillSentMessage("cmid-abc", 8888, 1700000000000LL));
    CHECK(bubble->GetMessage().msgid == 8888);
    CHECK(bubble->GetMessage().timestampMs == 1700000000000LL);
    CHECK(net::MessageProtocolClient::canRecallMessage(
        bubble->GetMessage().msgid, bubble->GetMessage().timestampMs, 1700000000000LL));

    // 正文等其余字段不受回填影响。
    CHECK(bubble->GetMessage().text == CString(_T("刚发出的消息")));
}

TEST_CASE("W11e 回填对不上的 clientMsgId 不动任何行", "[chatwnd][recall]")
{
    // 回执是向所有聊天窗广播的，非本窗口那条必须安静忽略、不能误改别的行。
    ChatMessageList list;
    ChatBubble* bubble = NewTextBubble(true, _T("别人窗口的消息"));
    ChatMessage msg = bubble->GetMessage();
    msg.clientMsgId = "cmid-mine";
    bubble->SetMessage(msg);
    list.AddChild(std::unique_ptr<DuiControl>(bubble));

    CHECK_FALSE(list.BackfillSentMessage("cmid-other", 999, 1700000000000LL));
    CHECK(bubble->GetMessage().msgid == 0);

    // 空 clientMsgId（收到的消息 / 历史消息本就没有）一律忽略。
    CHECK_FALSE(list.BackfillSentMessage("", 999, 1700000000000LL));
    CHECK(bubble->GetMessage().msgid == 0);
}

TEST_CASE("W11c 撤回推送的 msgid 对不上时不动任何行", "[chatwnd][recall]")
{
    // 该条消息尚未加载进当前视图（或属于别的会话）时安静忽略，不能误撤别的行。
    ChatMessageList list;
    ChatBubble* bubble = NewTextBubble(true, _T("不该被撤回的消息"));
    ChatMessage msg = bubble->GetMessage();
    msg.msgid = 111;
    bubble->SetMessage(msg);
    list.AddChild(std::unique_ptr<DuiControl>(bubble));

    CHECK_FALSE(list.RecallMessageByMsgid(999, _T("你撤回了一条消息")));
    CHECK(bubble->GetMessage().kind != kMsgTime);

    // msgid 为 0 一律忽略（本端刚发出、服务器还没回执的行）。
    CHECK_FALSE(list.RecallMessageByMsgid(0, _T("你撤回了一条消息")));
}

TEST_CASE("W12 删除消息行把它从列表移除", "[chatwnd][delete]")
{
    ChatMessageList list;
    ChatBubble* b1 = NewTextBubble(false, _T("第一条"));
    ChatBubble* b2 = NewTextBubble(false, _T("第二条"));
    list.AddChild(std::unique_ptr<DuiControl>(b1));
    list.AddChild(std::unique_ptr<DuiControl>(b2));
    REQUIRE(list.Children().size() == 2u);

    list.RequestDeleteBubble(b1);   // 销毁 b1
    CHECK(list.Children().size() == 1u);
}

TEST_CASE("W13 进入多选只预选起点行、退出清空选中", "[chatwnd][multiselect]")
{
    ChatMessageList list;
    ChatBubble* b1 = NewTextBubble(false, _T("甲"));
    ChatBubble* b2 = NewTextBubble(false, _T("乙"));
    list.AddChild(std::unique_ptr<DuiControl>(b1));
    list.AddChild(std::unique_ptr<DuiControl>(b2));

    list.EnterMultiSelectWith(b2);
    CHECK(list.IsMultiSelectMode());
    CHECK_FALSE(b1->IsSelected());
    CHECK(b2->IsSelected());

    list.SetMultiSelectMode(false);
    CHECK_FALSE(b2->IsSelected());   // 退出多选清空所有选中态
}

// =====================================================================
// B1. 顶部离线提示带：在线（默认）时不得占据高度
//
// 症状：新开的聊天窗标题栏下面多出一条空白带。成因是提示条 label 在布局
// XML 里默认可见、文字为空、固定高 26px，而窗口刚建好时没有任何人把连接态
// 推给它，于是它一直以"空文字 + 26px"的形态占着位置。
// =====================================================================

TEST_CASE("B1 单聊窗顶部提示带默认不可见，头部紧贴窗口顶端", "[chatwnd][banner]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    // 提示条本身默认不可见 —— 有话要说（离线 / 正在连接）时才由
    // ApplyConnectionState 显示出来。
    DuiControl* banner = root->FindCtrlById(kIdOfflineBanner);
    REQUIRE(banner != nullptr);
    CHECK_FALSE(banner->IsVisible());

    // 布局之后头部信息块应当贴着客户区顶端，中间不留空白带。
    root->Layout(RECT{ 0, 0, 600, 700 });
    DuiControl* header = root->FindCtrlById(kIdHeaderInfo);
    REQUIRE(header != nullptr);
    RECT rcHeader = header->GetRect();
    CHECK(rcHeader.top == 0);
}

// =====================================================================
// B2. 顶部离线提示带：显示出来时必须重新布局才会真的占位
//
// DuiControl::SetVisible 只 Invalidate 重绘、<u>不</u>触发重新布局
// （见 DuiControl.cpp）。提示条初始是隐藏的（W14），首次布局时被跳过、
// 矩形为空；此后仅把它设为可见并不会让它拿到高度 —— 必须由调用方补一次
// 布局。本用例把这条契约钉死，防止有人以为 SetVisible(true) 就够了。
// =====================================================================

TEST_CASE("B2 提示带显示后须重新布局才占位，占位高度为 26px", "[chatwnd][banner]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    DuiControl* banner = root->FindCtrlById(kIdOfflineBanner);
    DuiControl* header = root->FindCtrlById(kIdHeaderInfo);
    REQUIRE(banner != nullptr);
    REQUIRE(header != nullptr);

    const RECT rcClient = { 0, 0, 600, 700 };
    root->Layout(rcClient);
    REQUIRE(header->GetRect().top == 0);   // 隐藏态：头部贴顶（B1 已验）

    // 只设可见、不重新布局 —— 提示条仍然没有高度，头部也不会让位。
    banner->SetVisible(true);
    RECT rcBannerNoLayout = banner->GetRect();
    CHECK((rcBannerNoLayout.bottom - rcBannerNoLayout.top) == 0);
    CHECK(header->GetRect().top == 0);

    // 补一次布局之后，提示条拿到 26px，头部整体下移同样的高度。
    root->ForceLayout(rcClient);
    RECT rcBanner = banner->GetRect();
    CHECK((rcBanner.bottom - rcBanner.top) == 26);
    CHECK(header->GetRect().top == 26);
}

// =====================================================================
// B3. 顶部提示带用的是 ChatBannerLabel（带底色），且不是链接模式
//
// 提示带原先是裸 DuiLabel + Link 模式，渲染成蓝色带下划线的超链接，既难看
// 又暗示"可以点"。改为纯告知之后：控件换成能铺底色的 ChatBannerLabel，模式
// 必须留在 ModeText，不得回退成 ModeLink。
// =====================================================================

TEST_CASE("B3 顶部提示带是 ChatBannerLabel 且为普通文字模式", "[chatwnd][banner]")
{
    auto rootSingle = BuildChatWindowBody();
    REQUIRE(rootSingle != nullptr);
    ChatBannerLabel* single = dynamic_cast<ChatBannerLabel*>(
        rootSingle->FindCtrlById(kIdOfflineBanner));
    REQUIRE(single != nullptr);
    CHECK(single->GetMode() == DuiLabel::ModeText);

    auto rootGroup = BuildGroupChatWindowBody(false);
    REQUIRE(rootGroup != nullptr);
    ChatBannerLabel* group = dynamic_cast<ChatBannerLabel*>(
        rootGroup->FindCtrlById(kGIdOfflineBanner));
    REQUIRE(group != nullptr);
    CHECK(group->GetMode() == DuiLabel::ModeText);
}

// =====================================================================
// B4 / B5. 顶部提示带的实际绘制像素
//
// B3 只验了控件类型与模式，看不出"画出来长什么样"。这里把 ChatBannerLabel
// 真的画进一张内存位图再逐像素断言 —— 离线提示带的观感（浅琥珀底 + 深琥珀字 +
// 底部一根分隔线）从此有了可回归的凭据，不必每次都登录进客户端肉眼核对。
// =====================================================================

namespace {

// 把一个控件按给定尺寸画进 32 位内存位图，返回像素数组（自上而下，每个元素
// 为 COLORREF）。画之前整张填白，便于分辨"控件没画到的地方"。
// 取不到 GDI 资源时返回空数组，调用方须判空。
std::vector<COLORREF> RenderControlToPixels(DuiControl& ctrl, int w, int h)
{
    std::vector<COLORREF> out;

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          // 负高 = top-down，行序与直觉一致
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void*   bits      = NULL;
    HDC     hdcScreen = ::GetDC(NULL);
    HDC     hdcMem    = ::CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp      = ::CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, hdcScreen);
    if (hdcMem == NULL || hBmp == NULL || bits == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        if (hdcMem != NULL)
        {
            ::DeleteDC(hdcMem);
        }
        return out;
    }

    HGDIOBJ hOld = ::SelectObject(hdcMem, hBmp);

    RECT rc = { 0, 0, w, h };
    HBRUSH hWhite = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(hdcMem, &rc, hWhite);
    ::DeleteObject(hWhite);

    ctrl.ForceLayout(rc);
    ctrl.OnPaint(hdcMem, rc);
    ::GdiFlush();

    const DWORD* src = (const DWORD*)bits;
    out.resize((size_t)w * (size_t)h);
    for (size_t i = 0; i < out.size(); ++i)
    {
        // DIB 32 位是 0x00RRGGBB（小端下字节序 B,G,R,X），COLORREF 是 0x00BBGGRR，
        // 故要把红蓝分量对调。
        DWORD v = src[i];
        BYTE  b = (BYTE)(v & 0xFF);
        BYTE  g = (BYTE)((v >> 8) & 0xFF);
        BYTE  r = (BYTE)((v >> 16) & 0xFF);
        out[i] = RGB(r, g, b);
    }

    ::SelectObject(hdcMem, hOld);
    ::DeleteObject(hBmp);
    ::DeleteDC(hdcMem);
    return out;
}

} // anonymous namespace

TEST_CASE("B4 提示带画出底色 + 下边框 + 文字", "[chatwnd][banner]")
{
    // 与 BalloonChatWindow.cpp 里离线态那三个常量取同一组值。
    const COLORREF kBg     = RGB(0xFF, 0xF4, 0xE5);
    const COLORREF kText   = RGB(0xB4, 0x53, 0x09);
    const COLORREF kBorder = RGB(0xF3, 0xE0, 0xC0);

    const int kW = 600;
    const int kH = 26;      // 与布局 XML 里提示带的 fixedHeight 一致

    ChatBannerLabel banner;
    banner.SetBannerColors(kBg, kBorder);
    banner.SetTextColor(kText);
    banner.SetText(_T("当前为离线状态，无法收发消息。离线期间的消息会在恢复在线后收到。"));

    std::vector<COLORREF> px = RenderControlToPixels(banner, kW, kH);
    REQUIRE(px.size() == (size_t)kW * (size_t)kH);

    // 左上角远离文字处：应当是底色，而不是初始的白 —— 证明底真的铺满了。
    CHECK(px[(size_t)1 * kW + 2] == kBg);

    // 最后一行是下边缘分隔线。
    CHECK(px[(size_t)(kH - 1) * kW + kW / 2] == kBorder);
    // 倒数第二行仍是底色 —— 分隔线只占 1px，没粗到吃掉文字空间。
    CHECK(px[(size_t)(kH - 2) * kW + 2] == kBg);

    // 垂直中线上应当找得到文字像素。文字是抗锯齿绘制的，边缘为过渡色，
    // 故只断言"存在与底色不同的像素"，不比对具体色值。
    bool foundGlyph = false;
    for (int x = 0; x < kW; ++x)
    {
        COLORREF c = px[(size_t)(kH / 2) * kW + x];
        if (c != kBg && c != RGB(255, 255, 255))
        {
            foundGlyph = true;
            break;
        }
    }
    CHECK(foundGlyph);
}

TEST_CASE("B5 提示带未设底色时不铺底，退化成纯文字标签", "[chatwnd][banner]")
{
    const int kW = 80;
    const int kH = 26;

    ChatBannerLabel banner;
    // 不调 SetBannerColors：底色与边框色都留在 CLR_INVALID。
    banner.SetTextColor(RGB(0x00, 0x00, 0x00));
    banner.SetText(_T("x"));

    std::vector<COLORREF> px = RenderControlToPixels(banner, kW, kH);
    REQUIRE(px.size() == (size_t)kW * (size_t)kH);

    // 角落与最后一行都仍是初始的白：既没铺底色，也没画下边框。
    CHECK(px[(size_t)1 * kW + 2] == RGB(255, 255, 255));
    CHECK(px[(size_t)(kH - 1) * kW + 2] == RGB(255, 255, 255));
}

// =====================================================================
// PB. 头部信息行下方的「已 Pin N 条」提示带
//
// 与顶部离线提示带（B 组）刻意分成两层：那条讲的是"连接态"、在最顶；这条讲
// 的是"本会话内容"、贴着消息区。两者同时出现时层次才清楚。
//
// 这组用例钉的是三件事：文案、初始隐藏且不占位、以及"点得动"（ModeLink 且
// 不自动打开浏览器 —— 点击行为归窗口自己处理，见各聊天窗的 OnDuiNotify）。
// =====================================================================

TEST_CASE("PB1 提示带文案：0 条时为空串，正数按条数拼", "[chatwnd][pinbanner]")
{
    // 0 条与负数都返回空串 —— 调用方据此把整条提示带收起来。
    CHECK(MakePinBannerText(0).empty());
    CHECK(MakePinBannerText(-3).empty());

    CHECK(MakePinBannerText(1) == std::wstring(L"已 Pin 1 条 ›"));
    CHECK(MakePinBannerText(7) == std::wstring(L"已 Pin 7 条 ›"));
    CHECK(MakePinBannerText(200) == std::wstring(L"已 Pin 200 条 ›"));
}

TEST_CASE("PB2 单聊窗 Pin 提示带默认隐藏且不占位", "[chatwnd][pinbanner]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    DuiControl* pinBanner = root->FindCtrlById(kIdPinBanner);
    REQUIRE(pinBanner != nullptr);
    CHECK_FALSE(pinBanner->IsVisible());

    // 一条都没钉的会话里，头部行下面直接就是消息区，中间不留空白带。
    const RECT rcClient = { 0, 0, 600, 700 };
    root->Layout(rcClient);
    RECT rcPin = pinBanner->GetRect();
    CHECK((rcPin.bottom - rcPin.top) == 0);
}

TEST_CASE("PB3 两个聊天窗的 Pin 提示带都是可点的 ChatBannerLabel", "[chatwnd][pinbanner]")
{
    auto rootSingle = BuildChatWindowBody();
    REQUIRE(rootSingle != nullptr);
    ChatBannerLabel* single = dynamic_cast<ChatBannerLabel*>(
        rootSingle->FindCtrlById(kIdPinBanner));
    REQUIRE(single != nullptr);
    // ModeText 是纯绘制控件，不发通知也不变光标，点了没反应 —— 这条必须是 ModeLink。
    CHECK(single->GetMode() == DuiLabel::ModeLink);
    // 不能自动导航：点它要打开的是消息记录窗，不是浏览器。
    CHECK_FALSE(single->GetAutoNavigate());

    auto rootGroup = BuildGroupChatWindowBody(false);
    REQUIRE(rootGroup != nullptr);
    ChatBannerLabel* group = dynamic_cast<ChatBannerLabel*>(
        rootGroup->FindCtrlById(kGIdPinBanner));
    REQUIRE(group != nullptr);
    CHECK(group->GetMode() == DuiLabel::ModeLink);
    CHECK_FALSE(group->GetAutoNavigate());
}

TEST_CASE("PB4 UpdatePinBanner 只在显隐真的变化时才报 true", "[chatwnd][pinbanner]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    ChatBannerLabel* pinBanner = dynamic_cast<ChatBannerLabel*>(
        root->FindCtrlById(kIdPinBanner));
    REQUIRE(pinBanner != nullptr);

    // 本来就隐藏，再刷一次 0 条 —— 没有变化，调用方不必补布局。
    CHECK_FALSE(UpdatePinBanner(root.get(), kIdPinBanner, 0));
    CHECK_FALSE(pinBanner->IsVisible());

    // 从 0 到 3：显出来，文案对上，且报告"变了"。
    CHECK(UpdatePinBanner(root.get(), kIdPinBanner, 3));
    CHECK(pinBanner->IsVisible());
    CHECK(pinBanner->GetText() == CString(_T("已 Pin 3 条 ›")));

    // 3 到 4：只换文字、显隐没变，不该报 true（补布局是有代价的整树重排）。
    CHECK_FALSE(UpdatePinBanner(root.get(), kIdPinBanner, 4));
    CHECK(pinBanner->IsVisible());
    CHECK(pinBanner->GetText() == CString(_T("已 Pin 4 条 ›")));

    // 取消到 0：收起来，并报告"变了"。
    CHECK(UpdatePinBanner(root.get(), kIdPinBanner, 0));
    CHECK_FALSE(pinBanner->IsVisible());

    // 根为空、id 找不到都安静返回 false，调用方不必先判空。
    CHECK_FALSE(UpdatePinBanner(NULL, kIdPinBanner, 5));
    CHECK_FALSE(UpdatePinBanner(root.get(), kIdInputEdit, 5));
}

TEST_CASE("PB5 Pin 提示带显示后重排占 28px，且夹在头部行与消息区之间", "[chatwnd][pinbanner]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    DuiControl* pinBanner = root->FindCtrlById(kIdPinBanner);
    DuiControl* header = root->FindCtrlById(kIdHeaderInfo);
    DuiControl* msgArea = root->FindCtrlById(kIdMessageArea);
    REQUIRE(pinBanner != nullptr);
    REQUIRE(header != nullptr);
    REQUIRE(msgArea != nullptr);

    const RECT rcClient = { 0, 0, 600, 700 };
    root->Layout(rcClient);

    // 与离线提示带同一条契约：SetVisible 只重绘不重排，须补一次布局才拿到高度。
    REQUIRE(UpdatePinBanner(root.get(), kIdPinBanner, 2));
    root->ForceLayout(rcClient);

    RECT rcPin = pinBanner->GetRect();
    CHECK((rcPin.bottom - rcPin.top) == 28);

    // 顺序契约：提示带在头部信息行<u>下方</u>、消息区<u>上方</u>。这正是本轮与
    // 离线提示带（贴窗口顶端）刻意分开的那一层，位置反了两条带子就糊在一起了。
    CHECK(rcPin.top >= header->GetRect().bottom);
    CHECK(rcPin.bottom <= msgArea->GetRect().top);
}

TEST_CASE("PB6 群聊窗 Pin 提示带同样夹在头部行与消息区之间", "[chatwnd][pinbanner]")
{
    auto root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    DuiControl* pinBanner = root->FindCtrlById(kGIdPinBanner);
    DuiControl* header = root->FindCtrlById(kGIdHeaderInfo);
    DuiControl* msgArea = root->FindCtrlById(kGIdMessageArea);
    REQUIRE(pinBanner != nullptr);
    REQUIRE(header != nullptr);
    REQUIRE(msgArea != nullptr);
    CHECK_FALSE(pinBanner->IsVisible());

    const RECT rcClient = { 0, 0, 900, 700 };
    root->Layout(rcClient);

    REQUIRE(UpdatePinBanner(root.get(), kGIdPinBanner, 5));
    root->ForceLayout(rcClient);

    RECT rcPin = pinBanner->GetRect();
    CHECK((rcPin.bottom - rcPin.top) == 28);
    CHECK(rcPin.top >= header->GetRect().bottom);
    CHECK(rcPin.bottom <= msgArea->GetRect().top);
}

TEST_CASE("PB7 ChatMessageList::CountPinned 数出带 Pin 标记的行数", "[chatwnd][pinbanner]")
{
    ChatMessageList list;
    CHECK(list.CountPinned() == 0);   // 空列表

    // 造三条消息，msgid 各不相同（CountPinned 数的是标记，与 msgid 无关，但
    // SetPinnedByMsgid 要靠 msgid 找行）。
    for (int i = 0; i < 3; ++i)
    {
        ChatMessage msg = MakeTextMessage(false, _T("正文"));
        msg.msgid = 5000 + i;
        list.AppendMessage(msg);
    }
    CHECK(list.CountPinned() == 0);

    REQUIRE(list.SetPinnedByMsgid(5000, true));
    CHECK(list.CountPinned() == 1);

    REQUIRE(list.SetPinnedByMsgid(5002, true));
    CHECK(list.CountPinned() == 2);

    // 取消一条，数目跟着回落。
    REQUIRE(list.SetPinnedByMsgid(5000, false));
    CHECK(list.CountPinned() == 1);
}

TEST_CASE("DZ1 拖放投放区（输入区块）的矩形", "[chatwnd][dropzone]")
{
    // 拖文件只在发送框接收，落点判定取的就是输入框父容器的矩形（见
    // CBalloonChatWindow::IsChatDropZone）。本用例把它的几何钉住：投放区必须
    // 与消息区不重叠、且覆盖客户区整宽 —— 高亮描边贴着客户区左右边缘这一点，
    // 正是"左边那条线看不见"那个 bug 的成因。
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);

    const int kW = 700;
    const int kH = 500;
    RECT rcAll = { 0, 0, kW, kH };
    root->ForceLayout(rcAll);

    DuiControl* input = root->FindCtrlById(kIdInputEdit);
    REQUIRE(input != nullptr);
    DuiControl* block = input->GetParent();
    REQUIRE(block != nullptr);

    const RECT rcBlock = block->GetRect();
    INFO("input block rect = (" << rcBlock.left << "," << rcBlock.top << ","
         << rcBlock.right << "," << rcBlock.bottom << ")  client w=" << kW << " h=" << kH);

    DuiControl* msgArea = root->FindCtrlById(kIdMessageArea);
    REQUIRE(msgArea != nullptr);
    const RECT rcMsg = msgArea->GetRect();

    // 投放区在消息区下方，两者不重叠。
    CHECK(rcBlock.top >= rcMsg.bottom);
    // 投放区非空。
    CHECK(rcBlock.right > rcBlock.left);
    CHECK(rcBlock.bottom > rcBlock.top);

    // 投放区左右两边正好压在客户区边线上 —— 拖入高亮之所以用<u>填充底色</u>而不是
    // 描边，原因就在这里：描边画在矩形边线上，贴边那一侧只剩半条、实测左边那条
    // 根本看不见。这条断言把"贴边"这个前提钉住，日后若布局改成留边距，看到它失败
    // 就知道可以回头考虑描边了。
    CHECK(rcBlock.left == 0);
    CHECK(rcBlock.right == kW);
}

// =====================================================================
// IS1-IS4：输入框的滚动条与消息区统一（2026-10-01）
// =====================================================================

namespace {

// 输入框尺寸（像素）：宽度取单聊窗默认大小下输入区的量级，高度只容得下三四行
const int kIsInputW = 300;
const int kIsInputH = 60;

// 内容放得下的短文字，与必然超出可视区的长文字（十余行）
const TCHAR* const kIsShortText = _T("一行短短的文字");
const int kIsLongLines = 16;

// 拖动滑块时指针往下移动的距离（像素），足以让内容滚动一段
const int kIsDragDistance = 30;

// 造一个排好版的聊天输入框，内容为 text。与真实输入框的配置一致：多行、自动换行、无边框。
std::unique_ptr<ChatInputRichEdit> MakeIsInput(const CString& text)
{
    std::unique_ptr<ChatInputRichEdit> input(new ChatInputRichEdit());
    input->SetMultiLine(true);
    input->SetWordWrap(true);
    input->SetShowBorder(false);
    RECT rc = { 0, 0, kIsInputW, kIsInputH };
    input->Layout(rc);
    input->SetText(text);
    return input;
}

// 十余行的长文字，每行一句、行间换行。
CString IsLongText()
{
    CString text;
    for (int i = 0; i < kIsLongLines; ++i)
    {
        if (i > 0)
        {
            text += _T("\r");
        }
        text.AppendFormat(_T("第 %d 行：这一行的内容足够把输入框撑出滚动条"), i + 1);
    }
    return text;
}

// 输入框当前的滚动位置（像素）。
int IsScrollPos(const ChatInputRichEdit& input)
{
    int contentH = 0;
    int pos = 0;
    int viewH = 0;
    input.GetVScrollMetrics(contentH, pos, viewH);
    return pos;
}

} // anonymous namespace

TEST_CASE("IS1 输入框内容超出可视区时，基类自带的滚动条不出现", "[chatwnd][inputscroll]")
{
    std::unique_ptr<ChatInputRichEdit> input = MakeIsInput(IsLongText());
    REQUIRE(input->Test_IsEngineReady());
    int contentH = 0;
    int pos = 0;
    int viewH = 0;
    REQUIRE(input->GetVScrollMetrics(contentH, pos, viewH));
    //前提：内容确实超出了可视区
    REQUIRE(contentH > viewH);
    CHECK_FALSE(input->Test_IsVScrollBarVisible());
}

TEST_CASE("IS2 输入框的细滑块与消息区同一套几何：宽 5、离右缘 2，内容放得下时没有滑块", "[chatwnd][inputscroll]")
{
    //内容放得下：没有滑块
    std::unique_ptr<ChatInputRichEdit> shortInput = MakeIsInput(kIsShortText);
    REQUIRE(shortInput->Test_IsEngineReady());
    const RECT noThumb = shortInput->ScrollThumbRect();
    CHECK(::IsRectEmpty(&noThumb));

    //内容超出：滑块贴着右缘，宽度、边距、最短长度都与消息区相同
    std::unique_ptr<ChatInputRichEdit> input = MakeIsInput(IsLongText());
    const RECT thumb = input->ScrollThumbRect();
    REQUIRE_FALSE(::IsRectEmpty(&thumb));
    CHECK(thumb.right - thumb.left == kChatScrollBarWidth);
    CHECK(thumb.right == kIsInputW - kChatScrollBarMargin);
    CHECK(thumb.bottom - thumb.top >= kChatScrollMinThumb);
    //滚动位置在顶部时，滑块从文字区上沿开始，且整个落在控件内
    const RECT text = input->Test_GetTextRect();
    CHECK(IsScrollPos(*input) == 0);
    CHECK(thumb.top == text.top);
    CHECK(thumb.bottom <= kIsInputH);
}

TEST_CASE("IS3 在滑块上按下拖动会滚动内容；在命中带以外按下仍是文字选择，不进入拖动", "[chatwnd][inputscroll]")
{
    std::unique_ptr<ChatInputRichEdit> input = MakeIsInput(IsLongText());
    const RECT thumb = input->ScrollThumbRect();
    REQUIRE_FALSE(::IsRectEmpty(&thumb));

    //在命中带以外（文字上）按下：不进入拖动；在可视区内移动不会让内容滚动
    const RECT text = input->Test_GetTextRect();
    POINT inText = { text.left + 10, text.top + 5 };
    input->OnLButtonDown(inText, MK_LBUTTON);
    CHECK_FALSE(input->IsDraggingScrollThumb());
    POINT inTextBelow = { text.left + 20, text.top + 15 };
    input->OnMouseMove(inTextBelow, MK_LBUTTON);
    input->OnLButtonUp(inTextBelow, 0);
    CHECK(IsScrollPos(*input) == 0);

    //抓住滑块往下拖：进入拖动，松开后内容已经往下滚了一段
    POINT onThumb = { (thumb.left + thumb.right) / 2, (thumb.top + thumb.bottom) / 2 };
    input->OnLButtonDown(onThumb, MK_LBUTTON);
    CHECK(input->IsDraggingScrollThumb());
    POINT dragTo = { onThumb.x, onThumb.y + kIsDragDistance };
    input->OnMouseMove(dragTo, MK_LBUTTON);
    input->OnLButtonUp(dragTo, 0);
    CHECK_FALSE(input->IsDraggingScrollThumb());
    CHECK(IsScrollPos(*input) > 0);
    //滑块跟着下移
    CHECK(input->ScrollThumbRect().top > thumb.top);
}

TEST_CASE("IS4 输入框文字区的右边缘不超过滑块的左边缘，文字不会被滑块盖住", "[chatwnd][inputscroll]")
{
    std::unique_ptr<ChatInputRichEdit> input = MakeIsInput(IsLongText());
    const RECT thumb = input->ScrollThumbRect();
    REQUIRE_FALSE(::IsRectEmpty(&thumb));
    CHECK(input->Test_GetTextRect().right <= thumb.left);
}

TEST_CASE("IS5 没有任何变化时重复绘制输入框，不再向引擎查询滚动状态", "[chatwnd][inputscroll]")
{
    //内容放得下时，查询滚动状态会让基类做一次内容测量，测量又让引擎请求重画。若每次绘制都查询，
    //就成了"绘制 → 测量 → 重画"的死循环：消息队列里永远有待处理的 WM_PAINT，计时器消息排不上，
    //聊天窗消息区的平滑滚动停摆，表现为滚轮失效（2026-10-01 实测）。
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);

    HDC screen = ::GetDC(NULL);
    HDC dc = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, kIsInputW, kIsInputH);
    ::ReleaseDC(NULL, screen);
    REQUIRE(dc != NULL);
    REQUIRE(bmp != NULL);
    HGDIOBJ oldBmp = ::SelectObject(dc, bmp);
    const RECT rc = { 0, 0, kIsInputW, kIsInputH };
    //重复绘制的次数，取值无特殊含义
    const int kRepaints = 5;

    const CString texts[] = { CString(kIsShortText), IsLongText() };
    for (size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); ++t)
    {
        INFO("text " << t << (t == 0 ? " (fits)" : " (overflows)"));
        std::unique_ptr<ChatInputRichEdit> input = MakeIsInput(texts[t]);
        input->OnPaint(dc, rc);
        const unsigned long first = input->PaintScrollQueryCount();
        CHECK(first >= 1);
        for (int i = 0; i < kRepaints; ++i)
        {
            input->OnPaint(dc, rc);
        }
        CHECK(input->PaintScrollQueryCount() == first);

        //滚动状态变化之后，下一次绘制重新查询一次，再往后又不再查询
        input->TxSiteScrollInfoChanged(true);
        input->OnPaint(dc, rc);
        CHECK(input->PaintScrollQueryCount() == first + 1);
        input->OnPaint(dc, rc);
        CHECK(input->PaintScrollQueryCount() == first + 1);
    }

    ::SelectObject(dc, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("W19 单聊窗头部的「共享屏幕」按钮紧挨在「远程桌面」右边", "[chatwnd][sscard]")
{
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* rd = root->FindCtrlById(kIdHdrRemoteDesktop);
    DuiControl* share = root->FindCtrlById(kIdHdrScreenShare);
    REQUIRE(dynamic_cast<DuiButton*>(share) != nullptr);
    REQUIRE(rd != nullptr);
    REQUIRE(share->GetParent() == rd->GetParent());
    const auto& kids = share->GetParent()->Children();
    size_t rdAt = kids.size();
    size_t shareAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == rd)
            rdAt = i;
        if (kids[i].get() == share)
            shareAt = i;
    }
    CHECK(shareAt == rdAt + 1);
    CHECK(kIdHdrScreenShare == 417);
}

TEST_CASE("W20 单聊窗工具栏的「屏幕录制」按钮紧挨在截图与其下拉按钮 ▾ 右边", "[chatwnd][record]")
{
    // 2026-10-05 截图右边加了 ▾（docs/截图快捷键与隐藏当前窗口方案.md），录屏从「截图右边第 1 个」变为第 2 个
    auto root = BuildChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* cut = root->FindCtrlById(kIdTbScreenshot);
    DuiControl* record = root->FindCtrlById(kIdTbRecord);
    REQUIRE(dynamic_cast<DuiButton*>(record) != nullptr);
    REQUIRE(cut != nullptr);
    REQUIRE(record->GetParent() == cut->GetParent());
    const auto& kids = record->GetParent()->Children();
    size_t cutAt = kids.size();
    size_t recordAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == cut)
            cutAt = i;
        if (kids[i].get() == record)
            recordAt = i;
    }
    CHECK(recordAt == cutAt + 2);
    CHECK(kIdTbRecord == 429);
}

TEST_CASE("W21 单聊窗工具栏的截图下拉按钮 ▾ 紧挨在「截图」右边、宽 14 像素", "[chatwnd][screenshot]")
{
    auto root = BuildChatWindowBody(false);
    REQUIRE(root != nullptr);
    RECT rc = { 0, 0, 800, 600 };
    root->SetRect(rc);
    DuiControl* cut = root->FindCtrlById(kIdTbScreenshot);
    DuiControl* caret = root->FindCtrlById(kIdTbScreenshotMenu);
    REQUIRE(cut != nullptr);
    REQUIRE(dynamic_cast<ChatIconButton*>(caret) != nullptr);
    REQUIRE(caret->GetParent() == cut->GetParent());
    const auto& kids = caret->GetParent()->Children();
    size_t cutAt = kids.size();
    size_t caretAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == cut)
            cutAt = i;
        if (kids[i].get() == caret)
            caretAt = i;
    }
    CHECK(caretAt == cutAt + 1);
    const int kCaretWidthPx = 14;   // 布局 XML 里 ▾ 的 fixedWidth
    CHECK(caret->GetRect().right - caret->GetRect().left == kCaretWidthPx);
    CHECK(kIdTbScreenshotMenu == 442);
}
