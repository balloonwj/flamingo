/**
 *  SensitiveSendFailTests.cpp
 *
 *  发送被服务端拒绝之后的客户端表现（docs/敏感词方案.md 第 8.2～8.5 节，Suite SF，标签 [sendfail]）。
 *
 *    SF-C1  MarkSendFailed：只标记本列表里、还没回填服务器 msgid 的那一行；已回填的、不存在的、空 id 都返回 false。
 *    SF-C2  失败的文字行：没有已读回执；菜单只有复制 / 重新编辑 / 删除 / 多选，没有引用、转发、Pin、撤回。
 *    SF-C3  非文字的失败行：图片行置为上传失败、文件行置为失败，都没有「重新编辑」；对方的消息不能重新编辑。
 *    SF-C4  失败行的布局与绘制：群聊里比同内容的正常行多出原因行的高度，单聊里原因占回执行的位置、行高不变；
 *           失败行比正常行多画出一个红色感叹号。
 *    SF-C5  重新编辑放回输入框：输入框为空时内容即原文；不为空且光标在中间时插在光标处，原内容保留。
 *    SF-C6  重新编辑带表情与 @：表情还原为表情片段；@ 按气泡上的片段重新登记为待发候选，同一个人不重复登记。
 *    SF-C7  失败文案：SendFailureText 各错误码的短句（不带句号），FileSendFailureText 的文件名敏感词与通用说法。
 *    SF-C8  会话冒泡队列：成功时取出一次，失败时丢弃，不认识的 id 取不到；超过上限时丢弃最早的。
 *    SF-C9  其它操作被拒的提示（8.6）：资料、签名、建群、群昵称、群管理操作、文件夹、发起与参与接龙。
 *    SF-C11 群文件上传被拒的提示：文件名包含敏感词有专门说法（联调时发现群聊窗「发文件」走群文件上传）。
 *    SF-C10 群聊里标记失败后行高增加：原本停在最底部的列表仍停在最底部（联调时发现原因行被挡在视口下方），
 *           停在别处的不动。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存：不创建窗口。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/ChatReedit.h"
#include "../UI/BalloonChatWnd/ChatSendBumpQueue.h"
#include "../UI/BalloonChatWnd/ChatSendLimit.h"
#include "../UI/BalloonChatWnd/ChatInputRichEdit.h"
#include "../net/Protocol.h"
#include "../UI/BalloonMain/SensitiveWordTips.h"
#include "../UI/BalloonGroupManage/GroupManageMapping.h"
#include "../UI/BalloonChatWnd/ChatFolderStateModel.h"
#include "../UI/BalloonChatWnd/ChatRelayCard.h"
#include "../UI/BalloonCard/RelayComposeModel.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区宽度 / 高度（逻辑像素）
const int kListWidth  = 400;
const int kListHeight = 300;

// 服务端回填的 msgid，取值无特殊含义
const long long kServerMsgid = 88001;

// 失败原因（测试用）
const wchar_t* const kReason = L"消息包含敏感词，未能发出";

// 输入框尺寸（像素）
const int kInputW = 300;
const int kInputH = 60;

// 判定「红色像素」的阈值：失败标记用 #D9534F 画，抗锯齿会把边缘混浅，只统计足够纯的部分
const int kRedMinR = 180;
const int kRedMaxG = 120;
const int kRedMaxB = 120;

// 失败标记至少要贡献的红色像素数：标记是直径 16 的实心圆（约 200 像素），扣掉白色的「!」与抗锯齿边缘后仍远多于此
const int kMinMarkRedPixels = 100;

// 造一条自己发出、还没回填 msgid 的文字消息
ChatMessage MakeOwnPending(const CString& text, const std::string& clientMsgId)
{
    ChatMessage m = MakeTextMessage(true, text);
    m.clientMsgId = clientMsgId;
    return m;
}

// 取列表里第 idx 条真实消息的气泡（断言存在）。时间分隔行等 kMsgTime 行不计数：本人回显前面也会插时间行
// （docs/聊天消息时间显示方案.md 第 7 节，2026-10-14），按子控件下标取会取到时间行。
ChatBubble* BubbleAt(ChatMessageList& list, size_t idx)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    size_t seen = 0;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        REQUIRE(b != NULL);
        if (b->GetMessage().kind == kMsgTime)
            continue;
        if (seen == idx)
            return b;
        ++seen;
    }
    FAIL("no real message at index " << idx);
    return NULL;
}

// 造一个只含一条消息的列表并排版
void BuildList(ChatMessageList& list, const ChatMessage& msg, bool receiptEnabled)
{
    list.SetReadReceiptEnabled(receiptEnabled);
    list.AppendMessage(msg);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
}

// 把列表画到白底离屏位图上，数一数「红色」像素。气泡的底色、头像与失败标记都用 GDI+ 画，测试进程
// 不像客户端那样在启动时初始化 GDI+，这里自己初始化一次，否则 GDI+ 的填充一笔都画不出来。
int CountRedPixels(ChatMessageList& list)
{
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput startupInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, NULL) == Gdiplus::Ok);
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kListWidth;
    bi.bmiHeader.biHeight      = -kListHeight;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bitmap = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bitmap != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bitmap);
    RECT all = { 0, 0, kListWidth, kListHeight };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);
    list.OnPaint(memDC, all);
    ::GdiFlush();
    const unsigned int* px = (const unsigned int*)bits;
    int red = 0;
    for (int i = 0; i < kListWidth * kListHeight; ++i)
    {
        //32 位 DIB 按 BGRA 排列
        const int b = (int)(px[i] & 0xFF);
        const int g = (int)((px[i] >> 8) & 0xFF);
        const int r = (int)((px[i] >> 16) & 0xFF);
        if (r >= kRedMinR && g <= kRedMaxG && b <= kRedMaxB)
        {
            ++red;
        }
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bitmap);
    ::DeleteDC(memDC);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return red;
}

// 菜单模型里有没有某一项
bool HasItem(const std::vector<ChatMessageMenuEntry>& model, ChatMessageMenuId id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator && model[i].id == id)
        {
            return true;
        }
    }
    return false;
}

// 造一个排好版的聊天输入框，内容为 text
std::unique_ptr<ChatInputRichEdit> MakeInput(const CString& text)
{
    std::unique_ptr<ChatInputRichEdit> input(new ChatInputRichEdit());
    input->SetMultiLine(true);
    input->SetWordWrap(true);
    input->SetShowBorder(false);
    RECT rc = { 0, 0, kInputW, kInputH };
    input->Layout(rc);
    input->SetText(text);
    return input;
}

} // namespace

TEST_CASE("SF-C1 MarkSendFailed 只标记还没回填 msgid 的那一行", "[sendfail]")
{
    ChatMessageList list;
    list.AppendMessage(MakeOwnPending(_T("第一条"), "c1"));
    list.AppendMessage(MakeOwnPending(_T("第二条"), "c2"));
    list.AppendMessage(MakeTextMessage(false, _T("对方的消息")));
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);

    //第二条已经回填了服务器 msgid：再来失败回执也不认
    REQUIRE(list.BackfillSentMessage("c2", kServerMsgid, 1));

    CHECK(list.MarkSendFailed("c1", kReason));
    CHECK(BubbleAt(list, 0)->IsSendFailed());
    CHECK(BubbleAt(list, 0)->GetMessage().failReason == CString(kReason));

    CHECK_FALSE(list.MarkSendFailed("c2", kReason));
    CHECK_FALSE(BubbleAt(list, 1)->IsSendFailed());
    CHECK_FALSE(list.MarkSendFailed("nope", kReason));
    CHECK_FALSE(list.MarkSendFailed(std::string(), kReason));
    CHECK_FALSE(BubbleAt(list, 2)->IsSendFailed());
}

TEST_CASE("SF-C2 失败的文字行：没有已读回执，菜单只有复制 / 重新编辑 / 删除 / 多选", "[sendfail]")
{
    ChatMessage m = MakeOwnPending(_T("含敏感词的话"), "c1");
    m.sendFailed = true;
    m.failReason = kReason;
    CHECK(DecideReadReceipt(m, true) == kReceiptNone);
    CHECK(CanReeditMessage(m));

    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgText, 0, false, false, false, false, false, true, CanReeditMessage(m));
    CHECK(HasItem(model, kChatMsgMenuCopy));
    CHECK(HasItem(model, kChatMsgMenuReedit));
    CHECK(HasItem(model, kChatMsgMenuDelete));
    CHECK(HasItem(model, kChatMsgMenuMultiSelect));
    CHECK_FALSE(HasItem(model, kChatMsgMenuReply));
    CHECK_FALSE(HasItem(model, kChatMsgMenuForward));
    CHECK_FALSE(HasItem(model, kChatMsgMenuPin));
    CHECK_FALSE(HasItem(model, kChatMsgMenuRecall));
    //「重新编辑」紧跟在「复制」之后
    REQUIRE(model.size() >= 2);
    CHECK(model[0].id == kChatMsgMenuCopy);
    CHECK(model[1].id == kChatMsgMenuReedit);

    //正常的行（没有失败）菜单不变：照旧有「引用回复」，没有「重新编辑」
    const std::vector<ChatMessageMenuEntry> normal =
        BuildChatMessageMenuModel(kMsgText, 0, false, false, true, false, true);
    CHECK(HasItem(normal, kChatMsgMenuReply));
    CHECK_FALSE(HasItem(normal, kChatMsgMenuReedit));
}

TEST_CASE("SF-C3 非文字的失败行没有「重新编辑」；图片、文件行同时置为失败态", "[sendfail]")
{
    ChatMessageList list;
    list.AppendMessage(MakeImageMessage(true, _T(""), 100, 80, "img1", kChatImageStateUploading));
    list.AppendMessage(MakeFileMessage(true, _T("合同.pdf"), 1024, _T(""), "file1", 1));
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);

    CHECK(list.MarkSendFailed("img1", L"你已被禁言，消息未能发出"));
    CHECK(list.MarkSendFailed("file1", L"文件名包含敏感词，未能发出"));
    const ChatMessage& img = BubbleAt(list, 0)->GetMessage();
    const ChatMessage& file = BubbleAt(list, 1)->GetMessage();
    CHECK(img.sendFailed);
    CHECK(img.imageState == kChatImageStateFailed);
    CHECK(file.sendFailed);
    CHECK(file.fileState == 2);
    CHECK_FALSE(CanReeditMessage(img));
    CHECK_FALSE(CanReeditMessage(file));

    //对方的消息、没有失败的自己的消息都不能重新编辑
    ChatMessage peer = MakeTextMessage(false, _T("对方"));
    peer.sendFailed = true;
    CHECK_FALSE(CanReeditMessage(peer));
    CHECK_FALSE(CanReeditMessage(MakeOwnPending(_T("正常"), "c9")));

    //大表情同样是文字消息，可以重新编辑
    ChatMessage emoji = MakeOwnPending(_T("/f[\"12\"]"), "c10");
    emoji.kind = kMsgEmoji;
    emoji.sendFailed = true;
    CHECK(CanReeditMessage(emoji));
}

TEST_CASE("SF-C4 失败行的布局与绘制", "[sendfail][layout]")
{
    //群聊（不预留回执行）：失败行比正常行多出原因行
    ChatMessageList groupNormal;
    BuildList(groupNormal, MakeOwnPending(_T("同样的内容"), "a"), false);
    ChatMessageList groupFailed;
    BuildList(groupFailed, MakeOwnPending(_T("同样的内容"), "b"), false);
    REQUIRE(groupFailed.MarkSendFailed("b", kReason));
    CHECK(BubbleAt(groupFailed, 0)->GetRowHeight() > BubbleAt(groupNormal, 0)->GetRowHeight());

    //单聊（预留回执行）：原因画在回执行的位置，行高不变
    ChatMessageList singleNormal;
    BuildList(singleNormal, MakeOwnPending(_T("同样的内容"), "c"), true);
    ChatMessageList singleFailed;
    BuildList(singleFailed, MakeOwnPending(_T("同样的内容"), "d"), true);
    REQUIRE(singleFailed.MarkSendFailed("d", kReason));
    CHECK(BubbleAt(singleFailed, 0)->GetRowHeight() == BubbleAt(singleNormal, 0)->GetRowHeight());

    //失败行画出红色感叹号：比同内容的正常行多出一个实心圆的红色像素（头像底色等原有的红色两边都有，相减抵消）
    CHECK(CountRedPixels(groupFailed) - CountRedPixels(groupNormal) >= kMinMarkRedPixels);

    //标记失败后列表已重排：内容高度随行高增加
    CHECK(groupFailed.ContentHeight() > groupNormal.ContentHeight());
}

TEST_CASE("SF-C5 重新编辑放回输入框：空输入框直接填入，非空时插在光标处", "[sendfail][reedit]")
{
    ChatMessage m = MakeOwnPending(_T("中间"), "c1");
    m.sendFailed = true;
    const ChatReeditData data = BuildReeditData(m);
    REQUIRE(data.m_segments.size() == 1);
    CHECK(data.m_segments[0].m_kind == kChatClipText);
    CHECK(data.m_segments[0].m_text == L"中间");

    std::unique_ptr<ChatInputRichEdit> empty = MakeInput(CString());
    REQUIRE(empty->Test_IsEngineReady());
    ApplyReeditToInput(data, empty.get(), NULL);
    CHECK(empty->GetText() == CString(_T("中间")));

    std::unique_ptr<ChatInputRichEdit> filled = MakeInput(_T("前缀后缀"));
    REQUIRE(filled->Test_IsEngineReady());
    //光标放在「前缀」之后
    filled->SetSel(2, 2);
    ApplyReeditToInput(data, filled.get(), NULL);
    CHECK(filled->GetText() == CString(_T("前缀中间后缀")));

    //不满足条件的消息：内容为空，输入框不变
    const ChatReeditData none = BuildReeditData(MakeTextMessage(false, _T("对方")));
    CHECK(none.m_segments.empty());
}

TEST_CASE("SF-C6 重新编辑带表情与 @", "[sendfail][reedit]")
{
    //正文：「@张三 你好/f["12"]@所有人 」，两个 @ 片段（UTF-16 偏移）
    const CString text = _T("@张三 你好/f[\"12\"]@所有人 ");
    ChatMessage m = MakeOwnPending(text, "c1");
    m.sendFailed = true;
    RenderSpan zhang;
    zhang.m_uid = 7;
    zhang.m_off = 0;
    zhang.m_len = 3;
    RenderSpan all;
    all.m_uid = 0;
    all.m_off = text.Find(_T("@所有人"));
    all.m_len = 4;
    m.ats.push_back(zhang);
    m.ats.push_back(all);

    const ChatReeditData data = BuildReeditData(m);
    bool hasFace = false;
    for (size_t i = 0; i < data.m_segments.size(); ++i)
    {
        if (data.m_segments[i].m_kind == kChatClipFace && data.m_segments[i].m_faceId == 12)
        {
            hasFace = true;
        }
    }
    CHECK(hasFace);
    REQUIRE(data.m_mentions.size() == 2);
    CHECK(data.m_mentions[0].m_uid == 7);
    CHECK(data.m_mentions[0].m_displayName == u8"张三");
    CHECK(data.m_mentions[1].m_uid == 0);
    CHECK(data.m_mentions[1].m_displayName == u8"所有人");

    //登记进宿主的待发候选：已有的同一人不重复登记
    std::vector<MentionCandidate> pending;
    MentionCandidate existing;
    existing.m_uid = 7;
    existing.m_displayName = u8"张三";
    pending.push_back(existing);
    std::unique_ptr<ChatInputRichEdit> input = MakeInput(CString());
    ApplyReeditToInput(data, input.get(), &pending);
    REQUIRE(pending.size() == 2);
    CHECK(pending[1].m_uid == 0);

    //越界与过短的片段跳过
    std::vector<RenderSpan> bad;
    RenderSpan out;
    out.m_uid = 9;
    out.m_off = 100;
    out.m_len = 3;
    bad.push_back(out);
    RenderSpan tooShort;
    tooShort.m_uid = 9;
    tooShort.m_off = 0;
    tooShort.m_len = 1;
    bad.push_back(tooShort);
    CHECK(MentionsFromRenderSpans(text, bad).empty());
}

TEST_CASE("SF-C7 发送失败与发文件失败的文案", "[sendfail][text]")
{
    CHECK(SendFailureText(error_code_sensitive_word) == L"消息包含敏感词，未能发出");
    CHECK(SendFailureText(error_code_member_muted) == L"你已被禁言，消息未能发出");
    CHECK(SendFailureText(error_code_at_all_denied) == L"本群仅群主和管理员可以 @所有人");
    CHECK(SendFailureText(error_code_msg_too_large) == L"消息内容过长，未能发出，请删减后分条发送");
    const int kOtherCode = 9999;
    CHECK(SendFailureText(kOtherCode) == L"消息发送失败（错误码 9999）");

    CHECK(FileSendFailureText(error_code_file_sensitive_word) == L"文件名包含敏感词，未能发出");
    CHECK(FileSendFailureText(error_code_sensitive_word) == L"文件名包含敏感词，未能发出");
    CHECK(FileSendFailureText(kOtherCode) == L"文件发送失败（错误码 9999）");
    CHECK(FileSendFailureText(0) == L"文件发送失败");
}

TEST_CASE("SF-C8 会话冒泡队列：成功取出一次，失败丢弃", "[sendfail][bump]")
{
    ChatSendBumpQueue q;
    ConvSendBumpData d;
    d.ctype = 2;
    d.cid = 33;
    d.msgType = 1;
    d.digest = "hello";
    q.Add("c1", d);
    q.Add("c2", d);
    q.Add(std::string(), d);
    CHECK(q.Size() == 2);

    ConvSendBumpData out;
    CHECK(q.TakeOnSuccess("c1", out));
    CHECK(out.cid == 33);
    CHECK(out.digest == "hello");
    CHECK_FALSE(q.TakeOnSuccess("c1", out));

    q.Drop("c2");
    CHECK_FALSE(q.TakeOnSuccess("c2", out));
    CHECK_FALSE(q.TakeOnSuccess("unknown", out));
    CHECK(q.Size() == 0);

    //超过上限：丢弃最早的
    for (size_t i = 0; i <= ChatSendBumpQueue::kMaxPending; ++i)
    {
        q.Add("id" + std::to_string(i), d);
    }
    CHECK(q.Size() == ChatSendBumpQueue::kMaxPending);
    CHECK_FALSE(q.TakeOnSuccess("id0", out));
    CHECK(q.TakeOnSuccess("id" + std::to_string(ChatSendBumpQueue::kMaxPending), out));
}

TEST_CASE("SF-C9 其它操作被拒的提示文案", "[sendfail][text]")
{
    const int kOtherCode = 1;
    const int kCreateFailCode = 300;
    const int kUpdateGroupInfoCmd = 1311;

    CHECK(balloonmain::ProfileSaveFailureText(error_code_profile_sensitive_word) == CString(_T("资料中包含敏感词，未能保存。")));
    CHECK(balloonmain::ProfileSaveFailureText(kOtherCode) == CString(_T("保存资料失败，请稍后重试。")));
    CHECK(balloonmain::SignatureSaveFailureText(error_code_profile_sensitive_word) == CString(_T("个性签名包含敏感词，未能保存。")));
    CHECK(balloonmain::SignatureSaveFailureText(kOtherCode) == CString(_T("修改个性签名失败，请稍后重试。")));
    CHECK(balloonmain::CreateGroupFailureText(error_code_group_sensitive_word) == CString(_T("群名称包含敏感词，请修改后重试。")));
    CHECK(balloonmain::CreateGroupFailureText(kCreateFailCode) == CString(_T("创建群聊失败（错误码 300）。")));
    CHECK(balloonmain::GroupCardSaveFailureText(error_code_group_sensitive_word, _T("x")) == CString(_T("群昵称包含敏感词，未能保存。")));
    CHECK(balloonmain::GroupCardSaveFailureText(kOtherCode, _T("x")) == CString(_T("修改群昵称失败：x")));
    CHECK(balloonmain::GroupCardSaveFailureText(kOtherCode, CString()) == CString(_T("修改群昵称失败，请稍后重试。")));

    CHECK(balloongroupmanage::DescribeGroupOpFailure(kUpdateGroupInfoCmd, error_code_group_sensitive_word, std::string())
          == CString(_T("内容包含敏感词，未能保存。")));
    CHECK(std::wstring(FolderErrorText(error_code_file_sensitive_word)) == L"文件夹名包含敏感词，未能发出");
    CHECK(ballooncard::RelaySendFailureText(error_code_sensitive_word) == CString(L"内容包含敏感词，未能发起。"));
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_sensitive_word) == CString(L"接龙内容包含敏感词，未能提交。"));
}

TEST_CASE("SF-C10 标记失败后原本停在最底部的列表仍停在最底部", "[sendfail][layout]")
{
    //造一个内容超出视口的群聊列表（不开回执），最后一条是自己刚发出的消息
    const int kOverflowRows = 20;
    ChatMessageList list;
    list.SetReadReceiptEnabled(false);
    for (int i = 0; i < kOverflowRows; ++i)
    {
        list.AppendMessage(MakeTextMessage(false, _T("占位的对方消息")));
    }
    list.AppendMessage(MakeOwnPending(_T("最后一条"), "last"));
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
    list.ScrollToBottom();
    REQUIRE(list.ContentHeight() > list.ViewHeight());
    REQUIRE(list.ScrollY() == list.ContentHeight() - list.ViewHeight());

    REQUIRE(list.MarkSendFailed("last", kReason));
    CHECK(list.ScrollY() == list.ContentHeight() - list.ViewHeight());

    //停在上方时标记失败：位置不动。AppendMessage 会自动贴底，先滚到第一条（带 msgid 的对方消息）
    const long long kFirstMsgid = 7001;
    ChatMessageList top;
    top.SetReadReceiptEnabled(false);
    ChatMessage firstPeer = MakeTextMessage(false, _T("第一条对方消息"));
    firstPeer.msgid = kFirstMsgid;
    top.AppendMessage(firstPeer);
    top.AppendMessage(MakeOwnPending(_T("第二条"), "second"));
    for (int i = 0; i < kOverflowRows; ++i)
    {
        top.AppendMessage(MakeTextMessage(false, _T("占位的对方消息")));
    }
    top.ForceLayout(rc);
    REQUIRE(top.ScrollToMsgid(kFirstMsgid));
    const int before = top.ScrollY();
    REQUIRE(before < top.ContentHeight() - top.ViewHeight());
    REQUIRE(top.MarkSendFailed("second", kReason));
    CHECK(top.ScrollY() == before);
}

TEST_CASE("SF-C11 群文件上传被拒的提示", "[sendfail][text]")
{
    const int kUploadFailCode = 600;
    CHECK(GroupFileUploadFailureText(error_code_file_sensitive_word) == L"文件名包含敏感词，未能上传。");
    CHECK(GroupFileUploadFailureText(kUploadFailCode) == L"文件上传失败，请稍后重试。");
    CHECK(GroupFileUploadFailureText(0) == L"文件上传失败，请稍后重试。");
}
