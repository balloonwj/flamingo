/**
 *  群问答客户端的单测（docs/群问答方案.md），GroupQaTests.cpp
 *
 *  [groupqa]
 *    QA-C1 协议：1336 应答（来源、跳过不合规条目、失败应答）、1337 / 1338 请求、1339 推送
 *    QA-C2 面板逻辑：筛选、网址提取、发到群里的文字、输入校验、谁能修改、空状态提示、卡片 id 换算
 *    QA-C3 「新建问答」对话框按设计尺寸排得下
 *    QA-C4 答案带长网址时卡片高度容得下全部文字
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../net/GroupQaProtocolClient.h"
#include "../UI/BalloonChatWnd/GroupQaModel.h"
#include "../UI/BalloonChatWnd/GroupQaEditLayout.h"
#include "../UI/BalloonChatWnd/GroupQaCardList.h"
#include "../UI/BalloonMemo/MemoCardList.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "LayoutFitCheck.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

using namespace balloonchatwnd;

namespace {

// 群主、管理员、普通成员（网络侧角色取值）
const int kNetOwner = 2;
const int kNetAdmin = 1;
const int kNetMember = 0;

net::GroupQaItem MakeItem(int64_t id, const char* q, const char* a, int source)
{
    net::GroupQaItem item;
    item.m_id = id;
    item.m_question = q;
    item.m_answer = a;
    item.m_source = source;
    return item;
}

// 把标签按 width × kTallPx 的矩形画到白底位图上，返回最下面一行有墨迹的行号（从 0 起）；什么都没画出时返回 -1。
// 会改动标签的矩形，调用方须在此之前取好需要的排版结果。
int LastInkRow(balloonwjui::DuiLabel* label, int width)
{
    const int kTallPx = 400;     // 位图高，足够画下卡片里任何一段答案
    const int kInkLevel = 160;   // 三个分量都低于该值的像素算作墨迹（答案是深灰字，抗锯齿边缘较浅）
    const RECT rc = { 0, 0, width, kTallPx };
    label->SetRect(rc);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -kTallPx;   // 负值表示自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dc = ::CreateCompatibleDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dc == NULL || bmp == NULL || bits == NULL)
    {
        return -1;
    }
    HGDIOBJ old = ::SelectObject(dc, bmp);
    ::FillRect(dc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
    label->OnPaint(dc, rc);
    ::GdiFlush();
    int last = -1;
    const DWORD* px = static_cast<const DWORD*>(bits);
    for (int y = 0; y < kTallPx; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const DWORD v = px[y * width + x];
            if (((v >> 16) & 0xFF) < kInkLevel && ((v >> 8) & 0xFF) < kInkLevel && (v & 0xFF) < kInkLevel)
            {
                last = y;
                break;
            }
        }
    }
    ::SelectObject(dc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
    return last;
}

} // namespace

TEST_CASE("QA-C1 协议：拉取、保存、删除与变更推送", "[groupqa]")
{
    net::GroupQaResult r;
    REQUIRE(net::GroupQaProtocolClient::parseGetResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"items\":["
        "{\"id\":\"7\",\"question\":\"几点下班\",\"answer\":\"18:00\",\"source\":\"group\",\"editorname\":\"张三\",\"updatetime\":1700000000000},"
        "{\"id\":\"9\",\"question\":\"WiFi\",\"answer\":\"见墙上\",\"source\":\"enterprise\"},"
        "{\"id\":\"0\",\"question\":\"坏条目\",\"answer\":\"x\"},"
        "{\"id\":\"10\",\"question\":3,\"answer\":\"x\"}]}", r));
    CHECK(r.m_groupid == 268435457LL);
    REQUIRE(r.m_items.size() == 2);
    CHECK(r.m_items[0].m_source == net::kGroupQaSourceGroup);
    CHECK(r.m_items[0].m_editorname == std::string("张三"));
    CHECK(r.m_items[0].m_updatetime == 1700000000000LL);
    CHECK(r.m_items[1].m_source == net::kGroupQaSourceEnterprise);

    net::GroupQaResult failed;
    REQUIRE(net::GroupQaProtocolClient::parseGetResponse("{\"code\":302,\"msg\":\"no\"}", failed));
    CHECK(failed.m_code == 302);
    CHECK_FALSE(net::GroupQaProtocolClient::parseGetResponse("{\"code\":0,\"msg\":\"ok\"}", failed));

    CHECK(net::GroupQaProtocolClient::buildGetRequest(268435457LL) == std::string("{\"groupid\":\"268435457\"}"));
    const std::string save = net::GroupQaProtocolClient::buildSaveRequest(268435457LL, 0, "q", "a");
    CHECK(save.find("\"id\":\"0\"") != std::string::npos);
    CHECK(save.find("\"question\":\"q\"") != std::string::npos);
    CHECK(net::GroupQaProtocolClient::buildDeleteRequest(268435457LL, 7)
          == std::string("{\"groupid\":\"268435457\",\"id\":\"7\"}"));

    int64_t gid = 0;
    REQUIRE(net::GroupQaProtocolClient::parseChangedNotify("{\"groupid\":\"268435457\"}", gid));
    CHECK(gid == 268435457LL);
    CHECK_FALSE(net::GroupQaProtocolClient::parseChangedNotify("{}", gid));
}

TEST_CASE("QA-C2 筛选、网址、发到群里、校验与权限", "[groupqa]")
{
    std::vector<net::GroupQaItem> items;
    items.push_back(MakeItem(1, "报销流程", "见 https://oa.example.com/claim。", net::kGroupQaSourceGroup));
    items.push_back(MakeItem(2, "WiFi 密码", "问前台", net::kGroupQaSourceEnterprise));
    items.push_back(MakeItem(3, "几点下班", "18:00，周五 17:30", net::kGroupQaSourceGroup));

    //关键字为空或只有空白：全部入选
    CHECK(FilterGroupQa(items, L"").size() == 3);
    CHECK(FilterGroupQa(items, L"  ").size() == 3);
    //问题或答案命中，英文不区分大小写
    std::vector<size_t> hit = FilterGroupQa(items, L"wifi");
    REQUIRE(hit.size() == 1);
    CHECK(hit[0] == 1);
    hit = FilterGroupQa(items, L"17:30");
    REQUIRE(hit.size() == 1);
    CHECK(hit[0] == 2);
    CHECK(FilterGroupQa(items, L"不存在").empty());

    //网址：末尾的句号、中文标点不算；重复的只留一个；只认 http / https；最多 maxCount 个
    std::vector<std::wstring> urls = ExtractQaUrls(L"见 https://oa.example.com/claim。也可以 HTTP://a.cn/x?y=1, 再看 https://oa.example.com/claim", 8);
    REQUIRE(urls.size() == 2);
    CHECK(urls[0] == L"https://oa.example.com/claim");
    CHECK(urls[1] == L"HTTP://a.cn/x?y=1");
    CHECK(ExtractQaUrls(L"file:///c:/x javascript:alert(1) https://", 8).empty());
    CHECK(ExtractQaUrls(L"https://a.cn https://b.cn https://c.cn", 2).size() == 2);

    //发到群里的文字
    const std::wstring text = BuildQaSendText(L" 几点下班 ", L"18:00");
    CHECK(text.find(L"几点下班") != std::wstring::npos);
    CHECK(text.find(L"18:00") != std::wstring::npos);
    CHECK(text.find(L"\n") != std::wstring::npos);

    //输入校验：首尾空白不算；按字符计数（代理对算一个）
    CHECK(CheckQaInput(L"问", L"答") == kQaInputOk);
    CHECK(CheckQaInput(L"  ", L"答") == kQaInputEmptyQuestion);
    CHECK(CheckQaInput(L"问", L"\r\n") == kQaInputEmptyAnswer);
    CHECK(CheckQaInput(std::wstring(kQaQuestionMaxChars + 1, L'问'), L"答") == kQaInputQuestionTooLong);
    CHECK(CheckQaInput(L"问", std::wstring(kQaAnswerMaxChars + 1, L'a')) == kQaInputAnswerTooLong);
    std::wstring emoji;
    for (size_t i = 0; i < kQaQuestionMaxChars; ++i)
    {
        emoji += L"\xD83D\xDE00";   //一个表情是一对代理，算一个字符
    }
    CHECK(CountQaChars(emoji) == kQaQuestionMaxChars);
    CHECK(CheckQaInput(emoji, L"答") == kQaInputOk);
    CHECK(QaInputCheckText(kQaInputOk).empty());
    CHECK_FALSE(QaInputCheckText(kQaInputEmptyQuestion).empty());

    //谁能修改：群主、管理员能改本群问答，企业问答谁都不能改
    CHECK(CanEditQaItem(kNetOwner, net::kGroupQaSourceGroup));
    CHECK(CanEditQaItem(kNetAdmin, net::kGroupQaSourceGroup));
    CHECK_FALSE(CanEditQaItem(kNetMember, net::kGroupQaSourceGroup));
    CHECK_FALSE(CanEditQaItem(kNetOwner, net::kGroupQaSourceEnterprise));
    CHECK(CanAddQa(kNetAdmin));
    CHECK_FALSE(CanAddQa(kNetMember));
    CHECK_FALSE(CanAddQa(-1));

    //空状态提示
    CHECK_FALSE(QaEmptyHintText(kQaPanelLoading, 0, 0, false).empty());
    CHECK_FALSE(QaEmptyHintText(kQaPanelFailed, 0, 0, false).empty());
    CHECK_FALSE(QaEmptyHintText(kQaPanelLoaded, 0, 0, false).empty());
    CHECK(QaEmptyHintText(kQaPanelLoaded, 3, 3, false).empty());
    CHECK_FALSE(QaEmptyHintText(kQaPanelLoaded, 3, 0, true).empty());
    CHECK(QaEmptyHintText(kQaPanelLoaded, 3, 0, true) != QaEmptyHintText(kQaPanelLoaded, 0, 0, false));

    //卡片 id 换算：与备忘卡片（20000～27999）不重叠
    CHECK(kQaCardIdBase >= 28000u);
    int index = 0;
    int part = 0;
    REQUIRE(QaCardFromCtrlId(QaCardCtrlId(5, kQaCardPartEdit), index, part));
    CHECK(index == 5);
    CHECK(part == kQaCardPartEdit);
    CHECK_FALSE(QaCardFromCtrlId(kQaCardIdBase - 1, index, part));
    CHECK_FALSE(QaCardFromCtrlId(kQaCardIdBase + (unsigned)kQaCardMaxCount * kQaCardIdStride, index, part));
    //网址链接的部分编号放得进一张卡片的 id 段
    CHECK((unsigned)(kQaCardPartLinkFirst + kQaMaxLinksPerCard) <= kQaCardIdStride);
}

TEST_CASE("QA-C3 「新建问答」对话框按设计尺寸排得下", "[groupqa][layoutfit]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildQaEditBody();
    REQUIRE(root != nullptr);
    const int contentH = kQaEditWindowHeight - kQaEditTitleBarH;
    const UINT ids[] = { kQaEditIdSave, kQaEditIdCancel, kQaEditIdError, kQaEditIdAnswer };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("control id = " << ids[i]);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), ids[i], kQaEditWindowWidth, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_bottom <= contentH);
        //答案输入框是弹性区，按设计尺寸排时仍要有足够高度（至少 5 行文字）
        if (ids[i] == kQaEditIdAnswer)
        {
            const int kMinAnswerH = 100;
            CHECK(r.m_height >= kMinAnswerH);
        }
        else
        {
            CHECK(r.m_height == r.m_roomyHeight);
        }
    }
}

//答案里有比卡片还宽的网址时，DuiLabel::MeasureHeight 曾按 DT_CALCRECT 加宽矩形量高，量出的行数比实际
//画出的少，最后一行「账号统一找周思源申请。」被截掉。这里按演示环境里的原文建卡片，答案标签分到的高度必须容得下
//它实际画出的全部墨迹。
TEST_CASE("QA-C4 答案带长网址时卡片高度容得下全部文字", "[groupqa]")
{
    //视口宽度：与群聊窗边栏宽度相近，卡片文字区比网址窄
    const RECT kScrollRc = { 0, 0, 240, 600 };
    const COLORREF kOuterBg = RGB(255, 255, 255);
    std::unique_ptr<balloonwjui::DuiScrollView> scroll = balloonmemo::CreateMemoCardScroll(1);
    scroll->SetRect(kScrollRc);

    QaCardText card;
    card.m_question = L"测试环境的地址是什么？";
    card.m_meta = L"罗俊辉 修改于 今天 21:45";
    card.m_answer = L"Web 端：https://test.mingyuan.example/portal\r\n"
                    L"接口：https://test.mingyuan.example/api\r\n"
                    L"账号统一找周思源申请。";
    card.m_links.push_back(L"https://test.mingyuan.example/portal");
    card.m_links.push_back(L"https://test.mingyuan.example/api");
    card.m_editable = true;
    std::vector<QaCardText> cards(1, card);
    REQUIRE(SetQaCards(scroll.get(), cards, kOuterBg) == 1);

    balloonwjui::DuiLabel* answer =
        dynamic_cast<balloonwjui::DuiLabel*>(scroll->FindCtrlById(QaCardCtrlId(0, kQaCardPartAnswer)));
    REQUIRE(answer != NULL);
    const RECT rc = answer->GetRect();
    const int allotted = rc.bottom - rc.top;
    const int lastInk = LastInkRow(answer, rc.right - rc.left);
    INFO("allotted = " << allotted << ", last ink row = " << lastInk);
    REQUIRE(lastInk >= 0);
    CHECK(allotted > lastInk);
}
