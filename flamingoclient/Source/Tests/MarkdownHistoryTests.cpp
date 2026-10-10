/**
 *  消息记录窗、聊天记录管理器与聊天记录查看窗里 Markdown 消息显示为卡片的单元测试（docs/Markdown消息方案.md 第 12 节）：
 *
 *    - MK1 识别本地库 content 列：文字消息加 format markdown 为 Markdown；代码片段、没有 format、非文字消息、坏 JSON
 *          都不是；BuildContentJson 写出的 Markdown 消息能认出
 *    - MK2 聊天记录查看窗：合并转发里的 Markdown 子消息生成卡片行（标题、发送者、时间、消息标识），其它行不受影响；
 *          DLL 不可用时按普通文字显示
 *    - MK3 消息记录窗的列表：卡片内容由正文生成（DLL 不可用时不生成，这一行按普通文字显示）；Markdown 行里建出卡片，
 *          卡片落在行内、宽度与聊天窗卡片相同、行高按卡片算；没有标志的行没有卡片
 *    - MK4 卡片控件 ChatMarkdownCardControl：尺寸与聊天窗卡片相同（宽度不超过允许的宽度，高度随摘要变化）；单击向
 *          宿主窗口投递打开预览的请求，内容与卡片一致；点在卡片外不投递；双击不投递
 *    - MK5 一行摘要 DigestFromContent：Markdown 消息为「[Markdown] 标题」，DLL 不可用时为正文；其余消息与
 *          PreviewFromContent 相同；导出用的 PreviewFromContent 仍是源文本
 *
 *  需要一个真实窗口接收投递，故建一个不显示的 DuiHost。框架 Catch2 v3，标签 [mdhistory]。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // 接收投递的宿主窗口
#include "../UI/BalloonChatWnd/ChatMarkdown.h"                  // MarkdownSourceFromBody
#include "../UI/BalloonChatWnd/ChatMarkdownCard.h"
#include "../UI/BalloonChatWnd/ChatMarkdownCardControl.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"               // WM_CHAT_OPEN_MARKDOWN_PREVIEW / MarkdownPreviewOpenRequest
#include "../UI/BalloonHistory/HistoryView.h"                   // MK5：DigestFromContent / PreviewFromContent
#include "../UI/BalloonHistory/HistoryWindowLayout.h"           // MK3：BuildListContent / kIdHistoryMarkdownCardBase
#include "../UI/BalloonHistory/MergedRecordRows.h"              // MK2：BuildMergedRecordRows
#include "../UI/BalloonHistory/V2MsgQuery.h"                    // MK1：BuildContentJson
#include "../net/MessageProtocolClient.h"                       // MK1：net::MessageNotify / text_format_markdown
#include "../net/Protocol.h"                                    // ::message_type_*
#include "../Feature/Markdown/MarkdownDocument.h"               // SetMarkdownApiOverrideForTest
#include "../../FlamingoMarkdown/MdApiTable.h"                  // 编进测试程序的函数表，代替 DLL

using namespace balloonchatwnd;
using namespace balloonwjui;

namespace {

//宿主窗口与卡片的位置、允许的最大宽度（像素）
const int kHostWidth = 800;
const int kHostHeight = 600;
const int kCardLeft = 10;
const int kCardTop = 20;
const int kWideMax = 640;
const int kNarrowMax = 200;

//不显示的宿主窗口，析构时自动销毁
class ScopedHiddenHost
{
public:
    //创建宿主窗口（WS_POPUP、不带 WS_VISIBLE，跑测试时屏幕上不会闪出窗口）
    ScopedHiddenHost()
    {
        RECT rc = { 0, 0, kHostWidth, kHostHeight };
        m_host.Create(NULL, rc, _T("MarkdownHistoryTests"), WS_POPUP);
    }

    //销毁宿主窗口
    ~ScopedHiddenHost()
    {
        if (m_host.IsWindow())
            m_host.DestroyWindow();
    }

    //返回宿主对象（由本类持有，调用方借用）
    DuiHost& Host()
    {
        return m_host;
    }

private:
    DuiHost m_host;   //宿主对象，生命周期与本类相同
};

//取走投给 hWnd 的全部打开预览请求（取走后由本函数接管并返回，调用方用完释放）
std::vector<std::unique_ptr<MarkdownPreviewOpenRequest> > TakePreviewRequests(HWND hWnd)
{
    std::vector<std::unique_ptr<MarkdownPreviewOpenRequest> > out;
    MSG msg;
    while (::PeekMessageW(&msg, hWnd, WM_CHAT_OPEN_MARKDOWN_PREVIEW, WM_CHAT_OPEN_MARKDOWN_PREVIEW, PM_REMOVE))
        out.push_back(std::unique_ptr<MarkdownPreviewOpenRequest>((MarkdownPreviewOpenRequest*)msg.lParam));
    return out;
}

//屏幕 DC 下摘要折行后的高度（与 MeasureCard 的测量方式相同）
int ScreenSummaryHeight(const std::wstring& summary, int width)
{
    HDC hdc = ::GetDC(NULL);
    const int h = MeasureMarkdownCardSummary(hdc, summary, width);
    ::ReleaseDC(NULL, hdc);
    return h;
}

//MK3：测试列表的宽度与排版高度（像素）
const int kListWidth = 640;
const int kListHeight = 4000;

//造一条 Markdown 行（消息记录窗的行数据）
balloonhistory::HistoryRowView MakeMarkdownRow(const MarkdownCardInfo& info, long long msgid)
{
    balloonhistory::HistoryRowView row;
    row.m_senderName = _T("张三");
    row.m_timeText = _T("10:00");
    row.m_fullText = CString(info.m_sourceUtf8.c_str());
    row.m_previewText = row.m_fullText;
    row.m_msgid = msgid;
    row.m_isMarkdown = true;
    row.m_markdownCard = info;
    return row;
}

//作用域内用编进测试程序的函数表（或 NULL，模拟 DLL 不可用）代替 DLL，离开时恢复
class ApiOverride
{
public:
    explicit ApiOverride(bool available)
    {
        markdown::SetMarkdownApiOverrideForTest(true, available ? flamingomd::GetMdApiTable() : NULL);
    }

    ~ApiOverride()
    {
        markdown::SetMarkdownApiOverrideForTest(false, NULL);
    }
};

} // namespace

TEST_CASE("MK1 识别本地库 content：文字消息加 format markdown 为 Markdown；代码片段、没有 format、非文字消息、"
          "坏 JSON 都不是", "[mdhistory]")
{
    CHECK(IsMarkdownContentJson(::message_type_text, "{\"text\":\"# t\",\"format\":\"markdown\"}"));
    CHECK_FALSE(IsMarkdownContentJson(::message_type_text, "{\"text\":\"x\",\"format\":\"code\",\"lang\":\"cpp\"}"));
    CHECK_FALSE(IsMarkdownContentJson(::message_type_text, "{\"text\":\"x\"}"));
    CHECK_FALSE(IsMarkdownContentJson(::message_type_text, "{\"text\":\"x\",\"format\":1}"));
    CHECK_FALSE(IsMarkdownContentJson(::message_type_image, "{\"text\":\"x\",\"format\":\"markdown\"}"));
    CHECK_FALSE(IsMarkdownContentJson(::message_type_text, "not json"));
    CHECK_FALSE(IsMarkdownContentJson(::message_type_text, "[1,2]"));
    CHECK_FALSE(IsMarkdownContentJson(::message_type_text, ""));

    //与写入一致：本地库写 content 列的 BuildContentJson 写出的 Markdown 消息能认出，普通文字不会
    net::MessageNotify n;
    n.m_msgtype = ::message_type_text;
    n.m_text = "# t";
    n.m_textFormat = net::text_format_markdown;
    CHECK(IsMarkdownContentJson(::message_type_text, balloonhistory::BuildContentJson(n)));
    n.m_textFormat = net::text_format_plain;
    CHECK_FALSE(IsMarkdownContentJson(::message_type_text, balloonhistory::BuildContentJson(n)));
}

namespace {

//MK2：一份合并转发的数据，第一条是 Markdown 消息，第二条是普通文字（与服务端组装的写法相同：content 原样带上 format）
const char* const kMergedWithMarkdown =
    "{\"title\":\"周报汇总\",\"messages\":["
    "{\"msgid\":\"31\",\"from\":\"100001\",\"msgtype\":1,\"timestamp\":1700000000000,"
    "\"content\":{\"text\":\"# 本周进展\\n\\n完成了 **卡片**。\",\"format\":\"markdown\"},"
    "\"fromname\":\"张三\"},"
    "{\"msgid\":\"32\",\"from\":\"100002\",\"msgtype\":1,\"timestamp\":1700000060000,"
    "\"content\":{\"text\":\"收到\"},\"fromname\":\"李四\"}"
    "]}";

} // namespace

TEST_CASE("MK2 聊天记录查看窗：合并转发里的 Markdown 子消息生成卡片行（标题、发送者、时间、消息标识），其它行不受影响；"
          "DLL 不可用时按普通文字显示", "[mdhistory]")
{
    net::MergedRecord record;
    REQUIRE(net::MessageProtocolClient::parseMergedContent(kMergedWithMarkdown, record));
    REQUIRE(record.m_messages.size() == 2);
    //解析时子消息的 format 保留下来
    CHECK(record.m_messages[0].m_textFormat == net::text_format_markdown);
    {
        ApiOverride api(true);
        std::vector<balloonhistory::HistoryRowView> rows;
        balloonhistory::BuildMergedRecordRows(record, std::map<int64_t, CString>(), 0, rows);
        REQUIRE(rows.size() == 2);
        CHECK(rows[0].m_isMarkdown);
        CHECK(rows[0].m_markdownCard.m_title == L"本周进展");
        CHECK(rows[0].m_markdownCard.m_sender == L"张三");
        CHECK_FALSE(rows[0].m_markdownCard.m_fromMe);
        CHECK(rows[0].m_markdownCard.m_timestampMs == 1700000000000LL);
        CHECK(rows[0].m_markdownCard.m_key == "31");
        CHECK(rows[0].m_markdownCard.m_sourceUtf8.find("**") != std::string::npos);
        CHECK_FALSE(rows[1].m_isMarkdown);
        CHECK_FALSE(rows[1].m_isCode);
    }
    {
        //DLL 不可用：不画卡片，正文仍是源文本
        ApiOverride api(false);
        std::vector<balloonhistory::HistoryRowView> rows;
        balloonhistory::BuildMergedRecordRows(record, std::map<int64_t, CString>(), 0, rows);
        REQUIRE(rows.size() == 2);
        CHECK_FALSE(rows[0].m_isMarkdown);
        CHECK(std::wstring((LPCWSTR)rows[0].m_fullText).find(L"# 本周进展") != std::wstring::npos);
    }
}

TEST_CASE("MK3 消息记录窗的列表：Markdown 行里建出卡片，卡片在行内、宽度与聊天窗卡片相同、行高按卡片算；"
          "DLL 不可用时不生成卡片内容", "[mdhistory]")
{
    using namespace balloonhistory;
    const CString body = _T("# 周报\n\n本周完成了 **卡片**，下周继续。");
    MarkdownCardInfo info;
    {
        //卡片内容由正文生成：标题、摘要、源文本与打开预览用的字段
        ApiOverride api(true);
        REQUIRE(MakeMarkdownCardInfo(body, L"张三", false, 1700000000000LL, 77, info));
        CHECK(info.m_title == L"周报");
        CHECK(info.m_summary.find(L"本周完成了") != std::wstring::npos);
        CHECK(info.m_sourceUtf8 == MarkdownSourceFromBody(body));
        CHECK(info.m_sender == L"张三");
        CHECK_FALSE(info.m_fromMe);
        CHECK(info.m_timestampMs == 1700000000000LL);
        CHECK(info.m_key == "77");
        //没有 msgid 时消息标识留空
        MarkdownCardInfo noKey;
        REQUIRE(MakeMarkdownCardInfo(body, L"", true, 0, 0, noKey));
        CHECK(noKey.m_key.empty());
        CHECK(noKey.m_fromMe);
    }
    {
        //DLL 不可用：不生成卡片内容，这一行按普通文字显示
        ApiOverride api(false);
        MarkdownCardInfo none;
        CHECK_FALSE(MakeMarkdownCardInfo(body, L"张三", false, 0, 1, none));
        CHECK(none.m_title.empty());
        CHECK(none.m_sourceUtf8.empty());
    }

    //列表：两条 Markdown 行（一条有摘要、一条没有）与一条普通行
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeMarkdownRow(info, 9201));
    MarkdownCardInfo noSummary = info;
    noSummary.m_summary.clear();
    rows.push_back(MakeMarkdownRow(noSummary, 9202));
    HistoryRowView plain;
    plain.m_senderName = _T("李四");
    plain.m_timeText = _T("10:01");
    plain.m_fullText = _T("普通文字");
    plain.m_previewText = plain.m_fullText;
    plain.m_msgid = 9203;
    rows.push_back(plain);

    std::unique_ptr<DuiControl> content = BuildListContent(rows, _T(""), _T(""), kListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kListWidth, kListHeight });

    for (int i = 0; i < 2; ++i)
    {
        INFO("row " << i);
        ChatMarkdownCardControl* card =
            dynamic_cast<ChatMarkdownCardControl*>(content->FindCtrlById((UINT)(kIdHistoryMarkdownCardBase + i)));
        REQUIRE(card != NULL);
        DuiControl* row = content->FindCtrlById((UINT)(kIdHistoryRowBase + i));
        REQUIRE(row != NULL);
        const RECT rcCard = card->GetRect();
        const RECT rcRow = row->GetRect();
        //宽度与聊天窗卡片相同，高度为按摘要量出的卡片高度
        CHECK(rcCard.right - rcCard.left == kMarkdownCardWidth);
        CHECK(rcCard.bottom - rcCard.top
              == ChatMarkdownCardControl::MeasureCard(rows[i].m_markdownCard.m_summary, kListWidth).cy);
        //行高按卡片高度算出：卡片完整落在行内
        CHECK(rcCard.top >= rcRow.top);
        CHECK(rcCard.bottom <= rcRow.bottom);
        CHECK(rcCard.right <= rcRow.right);
        //卡片带着打开预览用的内容
        CHECK(card->GetCard().m_key == rows[i].m_markdownCard.m_key);
        CHECK(card->GetCard().m_title == rows[i].m_markdownCard.m_title);
    }
    //没有摘要的卡片更矮
    const RECT rc0 = content->FindCtrlById((UINT)kIdHistoryMarkdownCardBase)->GetRect();
    const RECT rc1 = content->FindCtrlById((UINT)(kIdHistoryMarkdownCardBase + 1))->GetRect();
    CHECK(rc1.bottom - rc1.top < rc0.bottom - rc0.top);
    //普通行没有卡片
    CHECK(content->FindCtrlById((UINT)(kIdHistoryMarkdownCardBase + 2)) == NULL);
}

TEST_CASE("MK4 卡片控件：尺寸与聊天窗卡片相同；单击投递打开预览的请求，内容与卡片一致；点在卡片外与双击不投递",
          "[mdhistory]")
{
    //尺寸：宽度为卡片宽度，放不下时收窄；高度随摘要变化，没有摘要时只有标题与底部一行
    const std::wstring summary = L"第一段摘要文字，用来撑出两三行的高度。第一段摘要文字，用来撑出两三行的高度。";
    const SIZE wide = ChatMarkdownCardControl::MeasureCard(summary, kWideMax);
    CHECK(wide.cx == kMarkdownCardWidth);
    CHECK(wide.cy == MarkdownCardHeight(ScreenSummaryHeight(summary, kMarkdownCardWidth)));
    CHECK(ChatMarkdownCardControl::MeasureCard(summary, kNarrowMax).cx == kNarrowMax);
    CHECK(ChatMarkdownCardControl::MeasureCard(std::wstring(), kWideMax).cy == MarkdownCardHeight(0));
    CHECK(wide.cy > MarkdownCardHeight(0));

    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMarkdownCardControl card;
    MarkdownCardInfo info;
    info.m_title = L"发布说明";
    info.m_summary = summary;
    info.m_sourceUtf8 = "# release\n\nbody";
    info.m_sender = L"张三";
    info.m_fromMe = false;
    info.m_timestampMs = 1700000000123LL;
    info.m_key = "4242";
    card.SetCard(info);
    card.AttachToHost(&host.Host());
    const RECT rc = { kCardLeft, kCardTop, kCardLeft + wide.cx, kCardTop + wide.cy };
    card.ForceLayout(rc);

    //单击卡片：投递一条请求，内容与卡片一致
    const POINT inside = { kCardLeft + wide.cx / 2, kCardTop + wide.cy / 2 };
    CHECK(card.OnLButtonDown(inside, MK_LBUTTON));
    std::vector<std::unique_ptr<MarkdownPreviewOpenRequest> > reqs = TakePreviewRequests(host.Host().m_hWnd);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0]->m_sourceUtf8 == info.m_sourceUtf8);
    CHECK(reqs[0]->m_title == info.m_title);
    CHECK(reqs[0]->m_sender == info.m_sender);
    CHECK_FALSE(reqs[0]->m_fromMe);
    CHECK(reqs[0]->m_timestampMs == info.m_timestampMs);
    CHECK(reqs[0]->m_key == info.m_key);

    //点在卡片外：不处理、不投递
    const POINT outside = { kCardLeft - 1, kCardTop - 1 };
    CHECK_FALSE(card.OnLButtonDown(outside, MK_LBUTTON));
    CHECK(TakePreviewRequests(host.Host().m_hWnd).empty());

    //双击：控件处理后忽略（返回 true，不再传给所在的行），不投递
    CHECK(card.OnLButtonDblClk(inside, MK_LBUTTON));
    CHECK(TakePreviewRequests(host.Host().m_hWnd).empty());

    //光标在卡片上显示手形
    CHECK(card.OnSetCursor(inside));
    CHECK_FALSE(card.OnSetCursor(outside));
}

TEST_CASE("MK5 一行摘要：Markdown 消息为「[Markdown] 标题」，DLL 不可用时为正文；其余消息与 PreviewFromContent 相同；"
          "PreviewFromContent 仍是源文本", "[mdhistory]")
{
    using namespace balloonhistory;
    const std::string md = "{\"text\":\"# 周报\\n\\n正文\",\"format\":\"markdown\"}";
    const std::string plain = "{\"text\":\"普通文字\"}";
    const std::string image = "{\"name\":\"a.png\"}";
    {
        ApiOverride api(true);
        const std::string digest = DigestFromContent(::message_type_text, md);
        CHECK(digest.compare(0, strlen("[Markdown]"), "[Markdown]") == 0);
        CHECK(digest.find("周报") != std::string::npos);
        CHECK(digest.find("#") == std::string::npos);
        //导出与搜索用的 PreviewFromContent 保留源文本
        CHECK(PreviewFromContent(::message_type_text, md).find("# 周报") != std::string::npos);
        //其余消息与 PreviewFromContent 完全相同
        CHECK(DigestFromContent(::message_type_text, plain) == PreviewFromContent(::message_type_text, plain));
        CHECK(DigestFromContent(::message_type_image, image) == PreviewFromContent(::message_type_image, image));
    }
    {
        //DLL 不可用：不加「[Markdown]」，显示正文
        ApiOverride api(false);
        const std::string digest = DigestFromContent(::message_type_text, md);
        CHECK(digest.find("[Markdown]") == std::string::npos);
        CHECK(digest.find("周报") != std::string::npos);
    }
}
