/**
 *  聊天消息「添加到备忘」的单测（docs/备忘与提醒方案.md §8.5），MemoPrefillTests.cpp
 *
 *  [memoprefill]
 *    MP1 标题：第一个非空行、50 字边界、代理对按 1 个计、空文字时为「聊天消息」、不含换行
 *    MP2 备注：三种来源行的格式；未超长原样保留；超长时正文截短、以省略号结尾、总长恰好 200、来源行完整
 *    MP3 纯文字：内联表情、@、Markdown（模块不可用时为源文本）、代码片段
 *    MP4 菜单：单聊 / 群聊各项的出现条件、id 唯一
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonChatWnd/ChatMemoPrefill.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../Common/Util/EncodeUtil.h"

using namespace balloonchatwnd;

namespace {

//一条别人发来的文字消息
ChatMessage MakeText(const wchar_t* text)
{
    ChatMessage m;
    m.kind = kMsgText;
    m.fromMe = false;
    m.msgid = 2001;
    m.text = text;
    m.senderName = L"张三";
    return m;
}

//UTF-8 转宽字符，便于按字数断言
std::wstring W(const std::string& utf8)
{
    return EncodeUtil::Utf8ToUnicode(utf8);
}

//重复 n 次同一个字
std::wstring Repeat(wchar_t ch, int n)
{
    return std::wstring((size_t)n, ch);
}

} // namespace

TEST_CASE("MP1 memo title is the first non-empty line within 50 chars", "[memoprefill]")
{
    MemoPrefillSource src;
    src.m_senderName = L"张三";

    MemoPrefill p = BuildMemoPrefill(MakeText(L"\n   \n  周五前交周报  \n第二行"), src);
    CHECK(W(p.m_title) == L"周五前交周报");

    //恰好 50 字不截断
    const std::wstring fifty = Repeat(L'字', 50);
    p = BuildMemoPrefill(MakeText(fifty.c_str()), src);
    CHECK(W(p.m_title) == fifty);

    //51 字截成 49 字加省略号
    const std::wstring fiftyOne = Repeat(L'字', 51);
    p = BuildMemoPrefill(MakeText(fiftyOne.c_str()), src);
    CHECK(W(p.m_title) == Repeat(L'字', 49) + L"\x2026");
    CHECK(CountMemoChars(W(p.m_title)) == 50);

    //代理对算 1 个字：50 个表情字符（100 个 UTF-16）不截断
    std::wstring emojis;
    for (int i = 0; i < 50; ++i)
    {
        emojis += L"\xD83D\xDE00";
    }
    p = BuildMemoPrefill(MakeText(emojis.c_str()), src);
    CHECK(W(p.m_title) == emojis);

    //没有文字
    p = BuildMemoPrefill(MakeText(L"   \n "), src);
    CHECK(W(p.m_title) == L"聊天消息");

    //标题不含换行
    p = BuildMemoPrefill(MakeText(L"一\r\n二"), src);
    CHECK(W(p.m_title).find(L'\n') == std::wstring::npos);
    CHECK(W(p.m_title).find(L'\r') == std::wstring::npos);
}

TEST_CASE("MP2 memo note starts with the source line and stays within 200 chars", "[memoprefill]")
{
    MemoPrefillSource single;
    single.m_senderName = L"张三";
    MemoPrefill p = BuildMemoPrefill(MakeText(L"明天开会"), single);
    CHECK(W(p.m_note) == L"来自 张三\n明天开会");

    MemoPrefillSource group;
    group.m_groupName = L"项目群";
    group.m_senderName = L"张三";
    p = BuildMemoPrefill(MakeText(L"明天开会"), group);
    CHECK(W(p.m_note) == L"来自 项目群 / 张三\n明天开会");

    MemoPrefillSource me;
    me.m_fromMe = true;
    me.m_senderName = L"不该出现";
    p = BuildMemoPrefill(MakeText(L"明天开会"), me);
    CHECK(W(p.m_note) == L"来自 我\n明天开会");

    //带时间：来源行里有「月」「日」与冒号
    MemoPrefillSource timed;
    timed.m_senderName = L"张三";
    timed.m_timestampMs = 1791230000000LL;
    p = BuildMemoPrefill(MakeText(L"明天开会"), timed);
    const std::wstring note = W(p.m_note);
    const std::wstring firstLine = note.substr(0, note.find(L'\n'));
    CHECK(firstLine.find(L"来自 张三，") == 0);
    CHECK(firstLine.find(L'月') != std::wstring::npos);
    CHECK(firstLine.find(L':') != std::wstring::npos);

    //超长：正文截短、以省略号结尾、总长恰好 200、来源行完整
    const std::wstring longBody = Repeat(L'长', 300);
    p = BuildMemoPrefill(MakeText(longBody.c_str()), single);
    const std::wstring longNote = W(p.m_note);
    CHECK(CountMemoChars(longNote) == kMemoPrefillNoteMaxChars);
    CHECK(longNote.find(L"来自 张三\n") == 0);
    CHECK(longNote[longNote.size() - 1] == L'\x2026');

    //正好放得下时不截断
    const std::wstring header = L"来自 张三\n";
    const std::wstring exact = Repeat(L'满', kMemoPrefillNoteMaxChars - (int)header.size());
    p = BuildMemoPrefill(MakeText(exact.c_str()), single);
    CHECK(W(p.m_note) == header + exact);
}

TEST_CASE("MP3 memo plain text handles faces, mentions, markdown and code", "[memoprefill]")
{
    //内联表情写成 [表情]，@ 保留显示名
    ChatMessage face = MakeText(L"@李四 你好/f[\"5\"]");
    const std::wstring plain = MessagePlainTextForMemo(face);
    CHECK(plain.find(L"@李四 你好") == 0);
    CHECK(plain.find(L"/f[") == std::wstring::npos);

    //代码片段取原文
    ChatMessage code = MakeText(L"int a = 1;\nint b = 2;");
    code.isCode = true;
    CHECK(MessagePlainTextForMemo(code) == L"int a = 1;\nint b = 2;");

    //Markdown：单测里没有加载 Markdown 模块，取到的是源文本
    ChatMessage md = MakeText(L"# 标题");
    md.isMarkdown = true;
    CHECK_FALSE(MessagePlainTextForMemo(md).empty());

    //CR LF 统一成 LF
    CHECK(MessagePlainTextForMemo(MakeText(L"一\r\n二")) == L"一\n二");

    //能否添加
    CHECK(CanAddMessageToMemo(MakeText(L"x")));
    ChatMessage failed = MakeText(L"x");
    failed.sendFailed = true;
    CHECK_FALSE(CanAddMessageToMemo(failed));
    ChatMessage emoji = MakeText(L"/f[\"5\"]");
    emoji.kind = kMsgEmoji;
    CHECK_FALSE(CanAddMessageToMemo(emoji));
    ChatMessage image = MakeText(L"");
    image.kind = kMsgImage;
    CHECK_FALSE(CanAddMessageToMemo(image));
}

namespace {

//菜单里有没有某一项
bool HasMenuItem(const std::vector<ChatMessageMenuEntry>& model, ChatMessageMenuId id)
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

//按消息类型与「添加到备忘」的模式建一份菜单（自己发出、已发出、可转发的普通行）
std::vector<ChatMessageMenuEntry> MemoMenu(int kind, int memoMode, bool sendFailed)
{
    return BuildChatMessageMenuModel(kind, 0, false, false, true, false, true, sendFailed, false, false, memoMode);
}

} // namespace

TEST_CASE("MP4 add-to-memo menu items appear by chat type and permission", "[memoprefill]")
{
    //菜单模型里文字消息的 kind 与 ChatMsgKind 一致
    CHECK(kChatMenuKindText == (int)kMsgText);

    //单聊：一项「添加到备忘」
    std::vector<ChatMessageMenuEntry> m = MemoMenu(kMsgText, kChatMemoMenuSingle, false);
    CHECK(HasMenuItem(m, kChatMsgMenuAddMemo));
    CHECK_FALSE(HasMenuItem(m, kChatMsgMenuAddGroupMemo));

    //群聊普通成员：只有「添加到我的备忘」
    m = MemoMenu(kMsgText, kChatMemoMenuGroupMine, false);
    CHECK(HasMenuItem(m, kChatMsgMenuAddMemo));
    CHECK_FALSE(HasMenuItem(m, kChatMsgMenuAddGroupMemo));

    //群主、管理员：两项
    m = MemoMenu(kMsgText, kChatMemoMenuGroupBoth, false);
    CHECK(HasMenuItem(m, kChatMsgMenuAddMemo));
    CHECK(HasMenuItem(m, kChatMsgMenuAddGroupMemo));

    //不是聊天窗（模式为不出现）、大表情、图片、文件、发送失败的行：都没有
    CHECK_FALSE(HasMenuItem(MemoMenu(kMsgText, kChatMemoMenuNone, false), kChatMsgMenuAddMemo));
    CHECK_FALSE(HasMenuItem(MemoMenu(kMsgEmoji, kChatMemoMenuGroupBoth, false), kChatMsgMenuAddMemo));
    CHECK_FALSE(HasMenuItem(MemoMenu(kMsgImage, kChatMemoMenuGroupBoth, false), kChatMsgMenuAddMemo));
    CHECK_FALSE(HasMenuItem(MemoMenu(kMsgFile, kChatMemoMenuGroupBoth, false), kChatMsgMenuAddMemo));
    CHECK_FALSE(HasMenuItem(MemoMenu(kMsgText, kChatMemoMenuGroupBoth, true), kChatMsgMenuAddMemo));

    //所有菜单项 id 仍互不相同
    m = MemoMenu(kMsgText, kChatMemoMenuGroupBoth, false);
    for (size_t i = 0; i < m.size(); ++i)
    {
        for (size_t j = i + 1; j < m.size(); ++j)
        {
            if (!m[i].isSeparator && !m[j].isSeparator)
            {
                CHECK(m[i].id != m[j].id);
            }
        }
    }
}
