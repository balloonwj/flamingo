/**
 *  单聊窗首屏历史渲染之前插入的居中提示（标签 [earlytip]），ChatEarlyTipTests.cpp
 *  背景：2026-10-07 用户反馈从主面板右键菜单发起通话、对方不在线时，提示插进新打开的聊天窗后随即被首屏历史清掉
 *  （说明见 UI/BalloonChatWnd/ChatEarlyTips.h）。
 *    - ETIP1 复现：列表里先插一条居中提示，再按首屏的做法清空并渲染历史，提示随之消失（改动前后都成立，说明问题出在这一步）；
 *    - ETIP2~ETIP4 记录类：首屏前记录、首屏后不记录、按插入顺序交回、重新请求最新一页后再记录、最多保留 kMaxTips 条；
 *    - ETIP5 按单聊窗的调用顺序走一遍：首屏前插入的提示在首屏渲染后仍在列表里、只有一条、位于末尾；
 *    - ETIP6 源文件守卫：单聊窗在插入提示、首屏渲染、重新请求最新一页三处调用了记录类
 *      （单聊窗依赖网络与主面板，不在单元测试工程里，只能读源文件检查）；首屏补回前经 DropTipsShownInHistory 去重；
 *    - ETIP7 DropTipsShownInHistory：首屏历史里已画出的提示，每条抵掉一条同文字的早到提示，其余保持顺序。
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatEarlyTips.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"

namespace {

//用例里的提示文字，与通话控制器发出的一致
const wchar_t kEtipOfflineTip[] = L"对方当前无法接听（不在线或正在通话中）。";
//首屏前插入的第二条提示
const wchar_t kEtipSecondTip[] = L"对方不在线";
//首屏渲染之后插入的提示
const wchar_t kEtipLateTip[] = L"通话已中断";
//模拟首屏历史的条数
const int kEtipHistoryCount = 2;
//ETIP4 记录的提示条数：比上限多 5 条
const size_t kEtipOverflow = 5;
//读文件时每次读的字节数
const size_t kEtipReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）：去掉这么多级就是 flamingoclient
const int kEtipDepth = 3;

//列表里文字为 text 的居中提示行有几条
size_t EtipCountTips(const balloonchatwnd::ChatMessageList& list, const CString& text)
{
    size_t count = 0;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const balloonchatwnd::ChatBubble* b = dynamic_cast<const balloonchatwnd::ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().kind == balloonchatwnd::kMsgTime && b->GetMessage().text == text)
            ++count;
    }
    return count;
}

//列表最后一行的文字；列表为空或最后一行不是气泡时返回空串
CString EtipLastText(const balloonchatwnd::ChatMessageList& list)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    if (kids.empty())
        return CString();
    const balloonchatwnd::ChatBubble* b = dynamic_cast<const balloonchatwnd::ChatBubble*>(kids.back().get());
    return b != NULL ? b->GetMessage().text : CString();
}

//按单聊窗 OnHistoryMessages 首屏分支的做法渲染一页历史：先清空，再逐条追加
void EtipRenderFirstPage(balloonchatwnd::ChatMessageList& list)
{
    list.ClearAllMessages();
    for (int i = 0; i < kEtipHistoryCount; ++i)
    {
        CString text;
        text.Format(L"history %d", i);
        list.AppendMessage(balloonchatwnd::MakeTextMessage(i % 2 == 0, text));
    }
}

//按单聊窗 AppendSystemTip 的做法插入一条提示并交给记录类
void EtipAppendTip(balloonchatwnd::ChatMessageList& list, balloonchatwnd::EarlySystemTipTracker& tracker,
                   const std::wstring& text)
{
    list.AppendMessage(balloonchatwnd::MakeSystemTipMessage(CString(text.c_str())));
    tracker.OnTipShown(text);
}

//读一个源文件的全部内容；读不到时返回空串
std::string EtipReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kEtipReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//本测试文件所在目录往上两级就是 flamingoclient（Source\Tests\本文件）
std::string EtipClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kEtipDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//取一个成员函数的函数体：从定义开始，到其后第一个位于行首的右花括号为止（本仓库的函数体都以行首的 } 结束）
std::string EtipFunctionBody(const std::string& source, const std::string& signature)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t end = source.find("\n}", begin);
    return source.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

} // namespace

TEST_CASE("ETIP1 复现：首屏渲染清空列表时，之前插入的居中提示随之消失", "[earlytip]")
{
    balloonchatwnd::ChatMessageList list;
    list.AppendMessage(balloonchatwnd::MakeSystemTipMessage(kEtipOfflineTip));
    REQUIRE(EtipCountTips(list, kEtipOfflineTip) == 1);

    EtipRenderFirstPage(list);
    CHECK(EtipCountTips(list, kEtipOfflineTip) == 0);
}

TEST_CASE("ETIP2 记录类：首屏前按顺序记录，首屏后不再记录", "[earlytip]")
{
    balloonchatwnd::EarlySystemTipTracker tracker;
    CHECK_FALSE(tracker.IsFirstPageRendered());

    tracker.OnTipShown(kEtipOfflineTip);
    tracker.OnTipShown(std::wstring());   //空串不记录
    tracker.OnTipShown(kEtipSecondTip);
    CHECK(tracker.PendingCount() == 2);

    const std::vector<std::wstring> tips = tracker.TakeTipsOnFirstPageRendered();
    REQUIRE(tips.size() == 2);
    CHECK(tips[0] == kEtipOfflineTip);
    CHECK(tips[1] == kEtipSecondTip);
    CHECK(tracker.IsFirstPageRendered());
    CHECK(tracker.PendingCount() == 0);

    //首屏之后插入的提示会正常留在列表里，不记录
    tracker.OnTipShown(kEtipLateTip);
    CHECK(tracker.PendingCount() == 0);
    CHECK(tracker.TakeTipsOnFirstPageRendered().empty());
}

TEST_CASE("ETIP3 记录类：重新请求最新一页后回到首屏未渲染", "[earlytip]")
{
    balloonchatwnd::EarlySystemTipTracker tracker;
    tracker.OnTipShown(kEtipOfflineTip);
    REQUIRE(tracker.TakeTipsOnFirstPageRendered().size() == 1);

    tracker.OnReloadRequested();
    CHECK_FALSE(tracker.IsFirstPageRendered());
    tracker.OnTipShown(kEtipSecondTip);

    //首屏到达之前又重新请求了一次：已记录的提示保留，等新的首屏一并补回
    tracker.OnReloadRequested();
    const std::vector<std::wstring> tips = tracker.TakeTipsOnFirstPageRendered();
    REQUIRE(tips.size() == 1);
    CHECK(tips[0] == kEtipSecondTip);
}

TEST_CASE("ETIP4 记录类：最多保留 kMaxTips 条，丢弃最早的", "[earlytip]")
{
    balloonchatwnd::EarlySystemTipTracker tracker;
    const size_t total = balloonchatwnd::EarlySystemTipTracker::kMaxTips + kEtipOverflow;
    for (size_t i = 0; i < total; ++i)
    {
        wchar_t text[32];
        swprintf_s(text, L"tip %u", (unsigned)i);
        tracker.OnTipShown(text);
    }
    CHECK(tracker.PendingCount() == balloonchatwnd::EarlySystemTipTracker::kMaxTips);

    const std::vector<std::wstring> tips = tracker.TakeTipsOnFirstPageRendered();
    REQUIRE(tips.size() == balloonchatwnd::EarlySystemTipTracker::kMaxTips);
    wchar_t first[32];
    swprintf_s(first, L"tip %u", (unsigned)kEtipOverflow);
    CHECK(tips.front() == first);
}

TEST_CASE("ETIP5 按单聊窗的调用顺序：首屏前插入的提示在首屏渲染后仍在列表末尾", "[earlytip]")
{
    balloonchatwnd::ChatMessageList list;
    balloonchatwnd::EarlySystemTipTracker tracker;

    //聊天窗刚打开、首屏还没到：插入提示
    EtipAppendTip(list, tracker, kEtipOfflineTip);
    CHECK(EtipCountTips(list, kEtipOfflineTip) == 1);

    //首屏到达：清空、渲染历史，再补回记录的提示（补回走插入提示的同一条路径，此时已不再记录）
    EtipRenderFirstPage(list);
    const std::vector<std::wstring> tips = tracker.TakeTipsOnFirstPageRendered();
    for (size_t i = 0; i < tips.size(); ++i)
        EtipAppendTip(list, tracker, tips[i]);

    CHECK(EtipCountTips(list, kEtipOfflineTip) == 1);
    CHECK(EtipLastText(list) == CString(kEtipOfflineTip));
    CHECK(tracker.PendingCount() == 0);

    //首屏之后插入的提示只出现一次
    EtipAppendTip(list, tracker, kEtipLateTip);
    CHECK(EtipCountTips(list, kEtipLateTip) == 1);
    CHECK(tracker.PendingCount() == 0);
}

TEST_CASE("ETIP6 单聊窗在三处调用了首屏前提示的记录类", "[earlytip]")
{
    const std::string root = EtipClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = EtipReadFile(root + "\\Source\\UI\\BalloonChatWnd\\BalloonChatWindow.cpp");
    REQUIRE_FALSE(source.empty());

    const std::string append = EtipFunctionBody(source, "void CBalloonChatWindow::AppendSystemTip(");
    const std::string history = EtipFunctionBody(source, "void CBalloonChatWindow::OnHistoryMessages(");
    const std::string reload = EtipFunctionBody(source, "void CBalloonChatWindow::ReloadLatestMessages(");
    REQUIRE_FALSE(append.empty());
    REQUIRE_FALSE(history.empty());
    REQUIRE_FALSE(reload.empty());

    CHECK(append.find("MakeSystemTipMessage(") != std::string::npos);
    CHECK(append.find("m_earlyTips.OnTipShown(") != std::string::npos);
    CHECK(history.find("m_earlyTips.TakeTipsOnFirstPageRendered()") != std::string::npos);
    CHECK(history.find("DropTipsShownInHistory(") != std::string::npos);
    CHECK(history.find("MakeShakeTipRow(") != std::string::npos);
    CHECK(reload.find("m_earlyTips.OnReloadRequested()") != std::string::npos);
}

//收到窗口抖动时新打开的单聊窗，即时插入的「对方向你发送了窗口抖动」被记为早到提示；首屏历史里同一条抖动
//也画成了同文字的提示行，补回时要去掉一条，否则同一句出现两次
TEST_CASE("ETIP7 首屏历史里已画出的提示，补回时每条抵掉一条同文字的早到提示", "[earlytip]")
{
    const std::wstring shake = L"对方向你发送了窗口抖动";
    std::vector<std::wstring> tips;
    tips.push_back(kEtipOfflineTip);
    tips.push_back(shake);
    tips.push_back(kEtipSecondTip);
    tips.push_back(shake);

    //首屏里有一条抖动提示：去掉第一条同文字的，其余保持顺序
    std::vector<std::wstring> shown(1, shake);
    const std::vector<std::wstring> rest = balloonchatwnd::DropTipsShownInHistory(tips, shown);
    REQUIRE(rest.size() == 3);
    CHECK(rest[0] == kEtipOfflineTip);
    CHECK(rest[1] == kEtipSecondTip);
    CHECK(rest[2] == shake);

    //首屏里有两条：两条都去掉
    shown.push_back(shake);
    CHECK(balloonchatwnd::DropTipsShownInHistory(tips, shown).size() == 2);

    //首屏里没有、或首屏里的提示不在记录里：原样返回
    CHECK(balloonchatwnd::DropTipsShownInHistory(tips, std::vector<std::wstring>()) == tips);
    CHECK(balloonchatwnd::DropTipsShownInHistory(tips, std::vector<std::wstring>(1, L"别的提示")) == tips);
}
