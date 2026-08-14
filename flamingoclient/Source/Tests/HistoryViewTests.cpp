/**
 *  历史消息记录窗口——记录到行视图转换纯逻辑单元测试（Suite V2V），HistoryViewTests.cpp
 *
 *  覆盖：PreviewFromContent 各类型预览（文本 / 图片 / 文件含/不含文件名 / 语音 / 未知 /
 *  非法JSON）、FullTextFromContent 的未净化正文口径、ParseContentBrief 取出的行内预览
 *  与下载所需信息、TimeRangeFromComboIndex 各档映射。纯逻辑、不触 UI。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../BalloonHistory/HistoryView.h"
#include "../net/Protocol.h"   // message_type_*
#include <string>
#include <vector>

using namespace balloonhistory;

TEST_CASE("V2V1 PreviewFromContent 各类型预览正确")
{
    //文本：取正文
    CHECK(PreviewFromContent(::message_type_text, "{\"text\":\"今天的周报我整理后发群里\"}")
          == "今天的周报我整理后发群里");
    //文本缺字段 / 非法 JSON：空串
    CHECK(PreviewFromContent(::message_type_text, "{}") == "");
    CHECK(PreviewFromContent(::message_type_text, "not a json") == "");

    //图片 / 截图：占位
    CHECK(PreviewFromContent(::message_type_image, "{\"image\":{\"fileid\":\"x\"}}") == "[图片]");
    CHECK(PreviewFromContent(::message_type_screenshot, "{}") == "[图片]");

    //文件：带文件名；缺文件名退化为 "[文件]"
    CHECK(PreviewFromContent(::message_type_file, "{\"file\":{\"name\":\"设计规范-v2.docx\"}}")
          == "[文件] 设计规范-v2.docx");
    CHECK(PreviewFromContent(::message_type_file, "{\"file\":{}}") == "[文件]");

    //语音
    CHECK(PreviewFromContent(::message_type_voice, "{}") == "[语音]");

    //表情 / 窗口抖动：与会话列表 digestForMsgType 口径对齐，给出有意义占位，
    //不再落到通用“[消息]”（此前抖动消息缺分支被显示成“[消息]”）。
    CHECK(PreviewFromContent(::message_type_emoji, "{}") == "[表情]");
    CHECK(PreviewFromContent(::message_type_shake, "{}") == "[抖一抖]");

    //尚未单独渲染的类型（如名片）：通用占位兜底
    CHECK(PreviewFromContent(::message_type_card, "{}") == "[消息]");
    CHECK(PreviewFromContent(::message_type_system, "{}") == "[消息]");
}

TEST_CASE("V2V2 TimeRangeFromComboIndex 各档映射正确")
{
    CHECK(TimeRangeFromComboIndex(0) == kHistoryTimeAll);
    CHECK(TimeRangeFromComboIndex(1) == kHistoryTime7Days);
    CHECK(TimeRangeFromComboIndex(2) == kHistoryTime30Days);
    CHECK(TimeRangeFromComboIndex(3) == kHistoryTime3Months);
    CHECK(TimeRangeFromComboIndex(4) == kHistoryTime6Months);
    CHECK(TimeRangeFromComboIndex(5) == kHistoryTime1Year);
    CHECK(TimeRangeFromComboIndex(6) == kHistoryTimeCustom);

    //越界一律退回“全部时间”。-1 是真会出现的：下拉一项都没选中时 GetCurSel 就返回它。
    CHECK(TimeRangeFromComboIndex(-1) == kHistoryTimeAll);
    CHECK(TimeRangeFromComboIndex(7) == kHistoryTimeAll);
    CHECK(TimeRangeFromComboIndex(99) == kHistoryTimeAll);
}

// ---------------------------------------------------------------------------
// V2Q-PAGER / V2Q-EMPTY  省略式分页器模型与空状态文案。
//                        方案见 docs/消息记录界面优化方案.md §3.2 / §3.3。
// ---------------------------------------------------------------------------

//取分页器模型里各格的页号，省略号记为 -1，便于整体比对。
static std::vector<int> PagerPages(const std::vector<HistoryPagerCell>& cells)
{
    std::vector<int> pages;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        pages.push_back(cells[i].m_isEllipsis ? -1 : cells[i].m_page);
    }
    return pages;
}

//取被标为"当前页"的那一格的页号；没有则返回 0。
static int PagerCurrent(const std::vector<HistoryPagerCell>& cells)
{
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (!cells[i].m_isEllipsis && cells[i].m_isCurrent)
        {
            return cells[i].m_page;
        }
    }
    return 0;
}

TEST_CASE("V2Q-PAGER1 页数不多时全部平铺，不出现省略号")
{
    const std::vector<HistoryPagerCell> cells = BuildPagerModel(2, 5);
    CHECK(PagerPages(cells) == std::vector<int>({ 1, 2, 3, 4, 5 }));
    CHECK(PagerCurrent(cells) == 2);

    //恰好等于上限时仍然平铺
    const std::vector<HistoryPagerCell> full = BuildPagerModel(4, kHistoryPagerMaxCells);
    CHECK((int)full.size() == kHistoryPagerMaxCells);
    for (size_t i = 0; i < full.size(); ++i)
    {
        CHECK_FALSE(full[i].m_isEllipsis);
    }
}

TEST_CASE("V2Q-PAGER2 当前页在中间时产出「1 … 前 当前 后 … 末」")
{
    const std::vector<HistoryPagerCell> cells = BuildPagerModel(8, 250);
    CHECK(PagerPages(cells) == std::vector<int>({ 1, -1, 7, 8, 9, -1, 250 }));
    CHECK(PagerCurrent(cells) == 8);
}

TEST_CASE("V2Q-PAGER3 贴近首页 / 末页时那一侧不出现省略号")
{
    //贴近首页：窗口起点已经是第 2 页，左边接上即可，不能写成"1 … 2"——
    //那等于把一个页号藏进省略号里
    const std::vector<HistoryPagerCell> head = BuildPagerModel(2, 250);
    CHECK(PagerPages(head) == std::vector<int>({ 1, 2, 3, -1, 250 }));

    const std::vector<HistoryPagerCell> first = BuildPagerModel(1, 250);
    CHECK(PagerPages(first) == std::vector<int>({ 1, 2, -1, 250 }));

    //贴近末页，右侧同理
    const std::vector<HistoryPagerCell> tail = BuildPagerModel(249, 250);
    CHECK(PagerPages(tail) == std::vector<int>({ 1, -1, 248, 249, 250 }));

    const std::vector<HistoryPagerCell> last = BuildPagerModel(250, 250);
    CHECK(PagerPages(last) == std::vector<int>({ 1, -1, 249, 250 }));
}

TEST_CASE("V2Q-PAGER4 只有一页时只出第 1 页，越界页号被夹回")
{
    const std::vector<HistoryPagerCell> one = BuildPagerModel(1, 1);
    CHECK(PagerPages(one) == std::vector<int>({ 1 }));
    CHECK(PagerCurrent(one) == 1);

    //页号越界 / 页数非法都要能兜住，不能产出空模型或负页号
    CHECK(PagerPages(BuildPagerModel(99, 1)) == std::vector<int>({ 1 }));
    CHECK(PagerPages(BuildPagerModel(0, 1)) == std::vector<int>({ 1 }));
    CHECK(PagerPages(BuildPagerModel(1, 0)) == std::vector<int>({ 1 }));
    CHECK(PagerPages(BuildPagerModel(-5, -5)) == std::vector<int>({ 1 }));
}

TEST_CASE("V2Q-PAGER5 格子数恒不超过上限，且当前页恒被标出")
{
    //省略式分页器存在的全部意义就是这个数有上限 —— 原先按总页数平铺，
    //5000 条消息的会话会在底部排出 600 多个按钮，整行直接失控
    const int counts[] = { 1, 2, 7, 8, 9, 50, 250, 625, 10000 };
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
    {
        const int total = counts[i];
        const int probes[] = { 1, 2, total / 2, total - 1, total };
        for (size_t j = 0; j < sizeof(probes) / sizeof(probes[0]); ++j)
        {
            //探针里会出现越界页号（total 很小时 total/2 为 0、total-1 为 0，
            //或反过来探针比 total 还大）。模型对越界一律夹回 [1, total]（见 PAGER4），
            //故这里断言"当前页被标出"时也要按夹取后的值比，否则测的是用例自己写错的期望。
            int cur = probes[j];
            if (cur < 1)
            {
                cur = 1;
            }
            if (cur > total)
            {
                cur = total;
            }
            const std::vector<HistoryPagerCell> cells = BuildPagerModel(cur, total);
            CHECK((int)cells.size() <= kHistoryPagerMaxCells);
            CHECK_FALSE(cells.empty());
            CHECK(PagerCurrent(cells) == cur);
            //首末页恒在
            CHECK(cells.front().m_page == 1);
            CHECK(cells.back().m_page == total);
            //省略号不会挨着出现，也不会出现在两端
            CHECK_FALSE(cells.front().m_isEllipsis);
            CHECK_FALSE(cells.back().m_isEllipsis);
            for (size_t k = 1; k < cells.size(); ++k)
            {
                CHECK_FALSE((cells[k].m_isEllipsis && cells[k - 1].m_isEllipsis));
            }
        }
    }
}

TEST_CASE("V2Q-BRIEF1 从 content 取图片 md5 与文件名 / 大小")
{
    const HistoryContentBrief img = ParseContentBrief(
        ::message_type_image, "{\"image\":{\"fileid\":\"f1\",\"md5\":\"abc123\",\"w\":100,\"h\":80,\"size\":\"2048\"}}");
    CHECK(img.m_imageMd5 == "abc123");
    CHECK(img.m_fileName.empty());

    //截图与普通图片同一路子
    const HistoryContentBrief shot = ParseContentBrief(
        ::message_type_screenshot, "{\"image\":{\"md5\":\"shot99\"}}");
    CHECK(shot.m_imageMd5 == "shot99");

    //size 落库时是字符串（64 位整数一律以字符串编码），也要认整数写法
    const HistoryContentBrief f = ParseContentBrief(
        ::message_type_file, "{\"file\":{\"name\":\"季度报告.xlsx\",\"size\":\"1887436\",\"md5\":\"m\"}}");
    CHECK(f.m_fileName == "季度报告.xlsx");
    CHECK(f.m_fileSize == 1887436);

    const HistoryContentBrief f2 = ParseContentBrief(
        ::message_type_file, "{\"file\":{\"name\":\"a.zip\",\"size\":2048}}");
    CHECK(f2.m_fileSize == 2048);
}

TEST_CASE("V2Q-BRIEF2 JSON 非法 / 缺字段 / 类型不符时各项留空")
{
    CHECK(ParseContentBrief(::message_type_image, "").m_imageMd5.empty());
    CHECK(ParseContentBrief(::message_type_image, "{oops").m_imageMd5.empty());
    CHECK(ParseContentBrief(::message_type_image, "{}").m_imageMd5.empty());
    CHECK(ParseContentBrief(::message_type_image, "{\"image\":{}}").m_imageMd5.empty());

    const HistoryContentBrief f = ParseContentBrief(::message_type_file, "{\"file\":{}}");
    CHECK(f.m_fileName.empty());
    CHECK(f.m_fileSize == 0);

    //文本消息不该拿到图片 / 文件信息
    const HistoryContentBrief t = ParseContentBrief(::message_type_text, "{\"text\":\"hi\"}");
    CHECK(t.m_imageMd5.empty());
    CHECK(t.m_fileName.empty());
}

TEST_CASE("V2Q-BRIEF3 扩展名取小写，无扩展名与隐藏文件名返回空")
{
    CHECK(FileExtensionOf("季度报告.xlsx") == "xlsx");
    CHECK(FileExtensionOf("REPORT.PDF") == "pdf");
    CHECK(FileExtensionOf("a.b.tar.gz") == "gz");
    CHECK(FileExtensionOf("noext").empty());
    //点在开头的是没有主干的隐藏文件名，不算扩展名
    CHECK(FileExtensionOf(".gitignore").empty());
    //点在末尾，后面没东西
    CHECK(FileExtensionOf("trailing.").empty());
    CHECK(FileExtensionOf("").empty());
}

TEST_CASE("V2Q-BRIEF4 文件大小分档格式化")
{
    //不足 1 KB 直接按字节写，省得出现 "0.0 KB" 这种没信息量的值
    CHECK(FormatFileSize(512) == "512 B");
    CHECK(FormatFileSize(1024) == "1.0 KB");
    CHECK(FormatFileSize(1887436) == "1.8 MB");
    CHECK(FormatFileSize(1024LL * 1024 * 1024 * 3) == "3.00 GB");
    //非正数没有意义，返回空串让调用方省掉那一段文字
    CHECK(FormatFileSize(0).empty());
    CHECK(FormatFileSize(-1).empty());
}

TEST_CASE("V2Q-HL1 单次与多次命中的区间正确")
{
    const std::vector<HighlightRange> one = FindHighlightRanges(L"看下这个季度报告", L"季度");
    REQUIRE(one.size() == 1);
    CHECK(one[0].m_begin == 4);
    CHECK(one[0].m_len == 2);

    const std::vector<HighlightRange> many = FindHighlightRanges(L"abc abc abc", L"abc");
    REQUIRE(many.size() == 3);
    CHECK(many[0].m_begin == 0);
    CHECK(many[1].m_begin == 4);
    CHECK(many[2].m_begin == 8);

    //命中不重叠：从上一处末尾继续找
    const std::vector<HighlightRange> overlap = FindHighlightRanges(L"aaa", L"aa");
    REQUIRE(overlap.size() == 1);
    CHECK(overlap[0].m_begin == 0);
}

TEST_CASE("V2Q-HL2 大小写不敏感")
{
    const std::vector<HighlightRange> r = FindHighlightRanges(L"See HTTP://x.com", L"http");
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_begin == 4);

    const std::vector<HighlightRange> r2 = FindHighlightRanges(L"report.PDF", L"pdf");
    REQUIRE(r2.size() == 1);
    CHECK(r2[0].m_begin == 7);
}

TEST_CASE("V2Q-HL3 中文按字符口径给区间，不劈开汉字")
{
    //这一条是整个高亮功能的关键：区间若按 UTF-8 字节算，"季度"在 "本季度报告" 里
    //的起点会是 3（字节），画出来的底色横跨半个汉字。按字符算应当是 1。
    const std::wstring text = L"本季度报告";
    const std::vector<HighlightRange> r = FindHighlightRanges(text, L"季度");
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_begin == 1);
    CHECK(r[0].m_len == 2);
    //用返回的区间去截原串，截出来的就该是关键字本身
    CHECK(text.substr((size_t)r[0].m_begin, (size_t)r[0].m_len) == L"季度");

    //中英混排
    const std::wstring mixed = L"发你 report 看下报告";
    const std::vector<HighlightRange> r2 = FindHighlightRanges(mixed, L"报告");
    REQUIRE(r2.size() == 1);
    CHECK(mixed.substr((size_t)r2[0].m_begin, (size_t)r2[0].m_len) == L"报告");
}

TEST_CASE("V2Q-HL4 空关键字 / 无命中 / 关键字比正文长都返回空集")
{
    CHECK(FindHighlightRanges(L"季度报告", L"").empty());
    CHECK(FindHighlightRanges(L"", L"季度").empty());
    CHECK(FindHighlightRanges(L"", L"").empty());
    CHECK(FindHighlightRanges(L"季度报告", L"预算").empty());
    CHECK(FindHighlightRanges(L"短", L"很长很长的关键字").empty());
}

TEST_CASE("V2Q-DATE1 今天 / 昨天 / 更早三档")
{
    CHECK(FormatDateGroupLabel(20260804, 20260804) == "今天");
    CHECK(FormatDateGroupLabel(20260803, 20260804) == "昨天");
    CHECK(FormatDateGroupLabel(20260802, 20260804) == "2026-08-02");
    //未来的日期（时钟不准 / 消息时间异常）不该被说成今天或昨天
    CHECK(FormatDateGroupLabel(20260805, 20260804) == "2026-08-05");
}

TEST_CASE("V2Q-DATE2 跨月跨年的「昨天」也要认得出来")
{
    //直接比 yyyymmdd 的大小是算不出"差一天"的：月初与上月末尾数值上差了几十
    CHECK(FormatDateGroupLabel(20260731, 20260801) == "昨天");
    CHECK(FormatDateGroupLabel(20251231, 20260101) == "昨天");
    //闰年 2 月 29 日
    CHECK(FormatDateGroupLabel(20240229, 20240301) == "昨天");
    CHECK(FormatDateGroupLabel(20240228, 20240229) == "昨天");
    //差两天就不是昨天了
    CHECK(FormatDateGroupLabel(20260730, 20260801) == "2026-07-30");
}

TEST_CASE("V2Q-DATE3 非法输入不崩")
{
    CHECK(FormatDateGroupLabel(0, 20260804).empty());
    CHECK(FormatDateGroupLabel(-1, 20260804).empty());
    //today 非法时退化成写出日期，不误报今天 / 昨天
    CHECK(FormatDateGroupLabel(20260804, 0) == "2026-08-04");
}

TEST_CASE("V2Q-COMPACT1 与上一行同发送者且同一天才折叠，首行恒不折叠")
{
    //三条都是同一人、同一天：第 1 条亮头像，后两条折叠
    const std::vector<int64_t> senders = { 100, 100, 100 };
    const std::vector<int64_t> days    = { 20260804, 20260804, 20260804 };
    const std::vector<bool> flags = ComputeCompactFlags(senders, days);
    REQUIRE(flags.size() == 3);
    CHECK_FALSE(flags[0]);
    CHECK(flags[1]);
    CHECK(flags[2]);
}

TEST_CASE("V2Q-COMPACT2 换人就重新亮头像")
{
    const std::vector<int64_t> senders = { 100, 200, 200, 100 };
    const std::vector<int64_t> days    = { 20260804, 20260804, 20260804, 20260804 };
    const std::vector<bool> flags = ComputeCompactFlags(senders, days);
    REQUIRE(flags.size() == 4);
    CHECK_FALSE(flags[0]);
    CHECK_FALSE(flags[1]);   //换成 200
    CHECK(flags[2]);         //还是 200
    CHECK_FALSE(flags[3]);   //又换回 100
}

TEST_CASE("V2Q-COMPACT3 同一人跨天不折叠")
{
    //只看发送者的话，跨天的两条会连成一片、看不出中间隔了一夜；日后加上日期分组头
    //时，组头下面还会冒出一条没有归属的孤儿行
    const std::vector<int64_t> senders = { 100, 100, 100 };
    const std::vector<int64_t> days    = { 20260803, 20260804, 20260804 };
    const std::vector<bool> flags = ComputeCompactFlags(senders, days);
    REQUIRE(flags.size() == 3);
    CHECK_FALSE(flags[0]);
    CHECK_FALSE(flags[1]);   //换天了
    CHECK(flags[2]);
}

TEST_CASE("V2Q-COMPACT4 空输入与长度不齐都不崩，且宁可不折叠")
{
    CHECK(ComputeCompactFlags({}, {}).empty());

    //只有一行：恒不折叠
    const std::vector<bool> one = ComputeCompactFlags({ 100 }, { 20260804 });
    REQUIRE(one.size() == 1);
    CHECK_FALSE(one[0]);

    //两个数组长度对不上：只在共同前缀里判，多出来的行一律不折叠 ——
    //宁可多画几个头像，也不能越界读到垃圾值把不相干的两行连成一片
    const std::vector<int64_t> senders = { 100, 100, 100, 100 };
    const std::vector<int64_t> days    = { 20260804, 20260804 };
    const std::vector<bool> flags = ComputeCompactFlags(senders, days);
    REQUIRE(flags.size() == senders.size());
    CHECK_FALSE(flags[0]);
    CHECK(flags[1]);
    CHECK_FALSE(flags[2]);
    CHECK_FALSE(flags[3]);
}

TEST_CASE("V2Q-EMPTY1 有关键字时文案里含该关键字")
{
    HistoryFilter f;
    f.m_keyword = "季度报告";
    const HistoryEmptyText t = BuildEmptyStateText(f);
    CHECK(t.m_title.find("季度报告") != std::string::npos);
    CHECK_FALSE(t.m_subtitle.empty());

    //关键字优先级最高：同时选了类型也仍然说"没搜到这个词"
    f.m_type = kHistoryTypeImage;
    const HistoryEmptyText t2 = BuildEmptyStateText(f);
    CHECK(t2.m_title.find("季度报告") != std::string::npos);
}

TEST_CASE("V2Q-EMPTY2 Pin 分类、类型胶囊、无筛选三种情形文案各不相同")
{
    HistoryFilter pin;
    pin.m_type = kHistoryTypePinned;
    const HistoryEmptyText tPin = BuildEmptyStateText(pin);
    //Pin 空的时候顺带告诉用户怎么钉 —— 这类"还没用过的功能"最需要一句引导
    CHECK_FALSE(tPin.m_subtitle.empty());
    CHECK(tPin.m_subtitle.find("右键") != std::string::npos);

    HistoryFilter image;
    image.m_type = kHistoryTypeImage;
    const HistoryEmptyText tImage = BuildEmptyStateText(image);
    CHECK(tImage.m_title.find("图片") != std::string::npos);

    HistoryFilter none;
    const HistoryEmptyText tNone = BuildEmptyStateText(none);
    CHECK_FALSE(tNone.m_title.empty());

    //三者互不相同
    CHECK(tPin.m_title != tImage.m_title);
    CHECK(tImage.m_title != tNone.m_title);
    CHECK(tPin.m_title != tNone.m_title);
}

TEST_CASE("V2Q-EMPTY3 选了时间范围时文案要点明是范围内没有")
{
    //选了"近7天 + 图片"却搜空时，不能说成"这个会话还没有图片消息"——更早的图片
    //可能有一堆，那句话是误导
    HistoryFilter ranged;
    ranged.m_type  = kHistoryTypeImage;
    ranged.m_range = kHistoryTime7Days;
    const HistoryEmptyText t = BuildEmptyStateText(ranged);
    CHECK(t.m_title.find("时间范围") != std::string::npos);
    CHECK(t.m_title.find("图片") != std::string::npos);

    HistoryFilter unranged;
    unranged.m_type = kHistoryTypeImage;
    CHECK(BuildEmptyStateText(unranged).m_title.find("时间范围") == std::string::npos);

    //只选时间、不选类型
    HistoryFilter onlyTime;
    onlyTime.m_range = kHistoryTime30Days;
    CHECK(BuildEmptyStateText(onlyTime).m_title.find("时间范围") != std::string::npos);
}

// ---------------------------------------------------------------------------
// V2Q-FULLTEXT  未净化正文（表情画成图的数据源）。
//               方案见 docs/消息记录内容交互方案.md §5 F2、§8 测试清单第 6 条。
// ---------------------------------------------------------------------------

TEST_CASE("V2Q-FULLTEXT1 文本消息原样返回含表情标记的正文，与预览口径形成对照")
{
    //这一条正是新函数存在的理由：预览把 /f["5"] 净化成字面量“[表情]”（会话列表只有
    //一行、画不了图），而消息记录窗要把它画成表情图，就必须拿到原始标记 —— 净化过
    //的正文里 faceId 已经丢了，再也还原不回来
    const std::string content = "{\"text\":\"改了发你看下/f[\\\"5\\\"]好\"}";
    CHECK(FullTextFromContent(::message_type_text, content) == "改了发你看下/f[\"5\"]好");
    CHECK(PreviewFromContent(::message_type_text, content) == "改了发你看下[表情]好");
    //两者必须不同 —— 一旦相同，就说明净化被误加到了新函数上
    CHECK(FullTextFromContent(::message_type_text, content)
          != PreviewFromContent(::message_type_text, content));

    //多个表情连排，一个都不能丢
    const std::string faces = "{\"text\":\"/f[\\\"0\\\"]/f[\\\"142\\\"]\"}";
    CHECK(FullTextFromContent(::message_type_text, faces) == "/f[\"0\"]/f[\"142\"]");
    CHECK(PreviewFromContent(::message_type_text, faces) == "[表情][表情]");

    //转义斜杠同理：预览会把 // 还原成 /，而原文这里要原样留着 —— 上层 ParseSendBody
    //还要自己解一次转义，提前解完它就少认一个字符
    const std::string slash = "{\"text\":\"a//b\"}";
    CHECK(FullTextFromContent(::message_type_text, slash) == "a//b");
    CHECK(PreviewFromContent(::message_type_text, slash) == "a/b");

    //不含表情的纯文字：两者恰好相同，这不算矛盾（净化对它无事可做）
    const std::string plain = "{\"text\":\"今天的周报我整理后发群里\"}";
    CHECK(FullTextFromContent(::message_type_text, plain) == "今天的周报我整理后发群里");
    CHECK(FullTextFromContent(::message_type_text, plain)
          == PreviewFromContent(::message_type_text, plain));
}

TEST_CASE("V2Q-FULLTEXT2 非文本类型与 PreviewFromContent 逐一相等")
{
    //非文本类型一律转调 PreviewFromContent。这条用例正是防着日后有人图省事把那段
    //switch 复制一份 —— 两处口径会慢慢漂移，届时同一条消息在两个界面上写着不同的字
    const int types[] = {
        ::message_type_image,
        ::message_type_screenshot,
        ::message_type_file,
        ::message_type_voice,
        ::message_type_emoji,
        ::message_type_shake,
        ::message_type_card,
        ::message_type_system
    };
    //几种有代表性的 content：带结构化子对象的、空对象、非法 JSON、空串
    const char* contents[] = {
        "{\"image\":{\"fileid\":\"f1\",\"md5\":\"abc123\"}}",
        "{\"file\":{\"fileid\":\"f-2\",\"name\":\"季度报告.xlsx\",\"md5\":\"m-2\"}}",
        "{}",
        "not a json",
        ""
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
    {
        for (size_t j = 0; j < sizeof(contents) / sizeof(contents[0]); ++j)
        {
            INFO("msgtype=" << types[i] << " content=" << contents[j]);
            CHECK(FullTextFromContent(types[i], contents[j])
                  == PreviewFromContent(types[i], contents[j]));
        }
    }

    //再抽查几个具体值，免得两边同时错成一样、上面那圈比对还比不出来
    CHECK(FullTextFromContent(::message_type_image, "{}") == "[图片]");
    CHECK(FullTextFromContent(::message_type_screenshot, "{}") == "[图片]");
    CHECK(FullTextFromContent(::message_type_file, "{\"file\":{\"name\":\"a.zip\"}}")
          == "[文件] a.zip");
    CHECK(FullTextFromContent(::message_type_file, "{\"file\":{}}") == "[文件]");
    CHECK(FullTextFromContent(::message_type_voice, "{}") == "[语音]");
    CHECK(FullTextFromContent(::message_type_emoji, "{}") == "[表情]");
    CHECK(FullTextFromContent(::message_type_shake, "{}") == "[抖一抖]");
    CHECK(FullTextFromContent(::message_type_card, "{}") == "[消息]");
    CHECK(FullTextFromContent(::message_type_system, "{}") == "[消息]");
}

TEST_CASE("V2Q-FULLTEXT3 空串 / 非法 JSON / 缺 text 字段一律退化为空串")
{
    //退化行为要与 PreviewFromContent 一模一样：这几种 content 在库里真会出现
    //（早期版本写进去的记录、以及本期还没落结构化内容的类型）
    CHECK(FullTextFromContent(::message_type_text, "") == "");
    CHECK(FullTextFromContent(::message_type_text, "not a json") == "");
    CHECK(FullTextFromContent(::message_type_text, "{oops") == "");
    CHECK(FullTextFromContent(::message_type_text, "{}") == "");
    //顶层不是对象（ParseObject 只认对象）
    CHECK(FullTextFromContent(::message_type_text, "[1,2,3]") == "");
    //text 字段类型不对：不能当成字符串硬取，更不能抛异常
    CHECK(FullTextFromContent(::message_type_text, "{\"text\":123}") == "");
    CHECK(FullTextFromContent(::message_type_text, "{\"text\":null}") == "");
    //正文本身就是空串
    CHECK(FullTextFromContent(::message_type_text, "{\"text\":\"\"}") == "");

    //逐个与预览口径对齐
    const char* degraded[] = { "", "not a json", "{oops", "{}", "[1,2,3]", "{\"text\":123}" };
    for (size_t i = 0; i < sizeof(degraded) / sizeof(degraded[0]); ++i)
    {
        INFO("content=" << degraded[i]);
        CHECK(FullTextFromContent(::message_type_text, degraded[i])
              == PreviewFromContent(::message_type_text, degraded[i]));
    }
}

// ---------------------------------------------------------------------------
// V2Q-BRIEF5..7  文件行下载所需的 fileid / md5。
//                方案见 docs/消息记录内容交互方案.md §5 F4、§8 测试清单第 7 条。
// ---------------------------------------------------------------------------

TEST_CASE("V2Q-BRIEF5 从 content 取文件 fileid 与 md5")
{
    //键名以归一化 content JSON 的产出方 BuildContentJson（V2MsgQuery.cpp）为准：
    //file 子对象下依次是 fileid / name / size / md5
    const HistoryContentBrief f = ParseContentBrief(
        ::message_type_file,
        "{\"file\":{\"fileid\":\"20260805-7f3a\",\"name\":\"季度报告.xlsx\","
        "\"size\":\"1887436\",\"md5\":\"9f86d081884c7d659a2feaa0c55ad015\"}}");
    CHECK(f.m_fileId == "20260805-7f3a");
    CHECK(f.m_fileMd5 == "9f86d081884c7d659a2feaa0c55ad015");
    //取新字段的同时旧字段照旧
    CHECK(f.m_fileName == "季度报告.xlsx");
    CHECK(f.m_fileSize == 1887436);

    //fileid 一律按字符串原样取，不作数值解析 —— 服务端下发什么就原样传回去
    const HistoryContentBrief numeric = ParseContentBrief(
        ::message_type_file, "{\"file\":{\"fileid\":\"00123\",\"md5\":\"m\"}}");
    CHECK(numeric.m_fileId == "00123");
    CHECK(numeric.m_fileMd5 == "m");
}

TEST_CASE("V2Q-BRIEF6 fileid / md5 缺字段、类型不对、JSON 非法、消息类型不符时留空")
{
    //只有文件名，没有下载所需的两项：留空即可，调用方据"空串"判定这一行下载不了
    const HistoryContentBrief onlyName = ParseContentBrief(
        ::message_type_file, "{\"file\":{\"name\":\"a.zip\"}}");
    CHECK(onlyName.m_fileName == "a.zip");
    CHECK(onlyName.m_fileId.empty());
    CHECK(onlyName.m_fileMd5.empty());

    //file 子对象是空的
    const HistoryContentBrief empty = ParseContentBrief(::message_type_file, "{\"file\":{}}");
    CHECK(empty.m_fileId.empty());
    CHECK(empty.m_fileMd5.empty());

    //字段类型不对：不能硬取，更不能抛异常
    const HistoryContentBrief badType = ParseContentBrief(
        ::message_type_file, "{\"file\":{\"fileid\":123,\"md5\":true}}");
    CHECK(badType.m_fileId.empty());
    CHECK(badType.m_fileMd5.empty());

    //JSON 非法 / 空串 / 顶层非对象
    CHECK(ParseContentBrief(::message_type_file, "").m_fileId.empty());
    CHECK(ParseContentBrief(::message_type_file, "{oops").m_fileId.empty());
    CHECK(ParseContentBrief(::message_type_file, "{oops").m_fileMd5.empty());
    CHECK(ParseContentBrief(::message_type_file, "[1,2,3]").m_fileMd5.empty());
    //根本没有 file 子对象
    CHECK(ParseContentBrief(::message_type_file, "{}").m_fileId.empty());
    CHECK(ParseContentBrief(::message_type_file, "{}").m_fileMd5.empty());

    //消息类型不是文件时一律留空。图片那一路尤其要看住：image 子对象下的 md5 键名与
    //file 下的恰好同名，取串了会把图片的 MD5 当成文件 MD5 拿去发下载请求
    const HistoryContentBrief img = ParseContentBrief(
        ::message_type_image, "{\"image\":{\"fileid\":\"i-1\",\"md5\":\"abc123\"}}");
    CHECK(img.m_imageMd5 == "abc123");
    CHECK(img.m_fileId.empty());
    CHECK(img.m_fileMd5.empty());

    //哪怕文本消息的 content 里混进了一个 file 子对象，也不该按文件解析
    const HistoryContentBrief t = ParseContentBrief(
        ::message_type_text, "{\"text\":\"hi\",\"file\":{\"fileid\":\"f\",\"md5\":\"m\"}}");
    CHECK(t.m_fileId.empty());
    CHECK(t.m_fileMd5.empty());
}

TEST_CASE("V2Q-BRIEF7 既有的文件名 / 大小 / 图片 md5 行为不变（回归哨兵）")
{
    //加字段最容易顺手碰坏隔壁，这条把改动前就有的行为原样再钉一遍
    const HistoryContentBrief f = ParseContentBrief(
        ::message_type_file, "{\"file\":{\"name\":\"季度报告.xlsx\",\"size\":\"1887436\",\"md5\":\"m\"}}");
    CHECK(f.m_fileName == "季度报告.xlsx");
    CHECK(f.m_fileSize == 1887436);
    //size 落库时是字符串（64 位整数一律以字符串编码避免丢精度），整数写法也要认
    const HistoryContentBrief f2 = ParseContentBrief(
        ::message_type_file, "{\"file\":{\"name\":\"a.zip\",\"size\":2048}}");
    CHECK(f2.m_fileSize == 2048);
    //文件消息不该拿到图片 MD5
    CHECK(f.m_imageMd5.empty());

    //图片 / 截图同一路子，且不该拿到文件名与大小
    const HistoryContentBrief img = ParseContentBrief(
        ::message_type_image, "{\"image\":{\"fileid\":\"f1\",\"md5\":\"abc123\",\"w\":100,\"h\":80,\"size\":\"2048\"}}");
    CHECK(img.m_imageMd5 == "abc123");
    CHECK(img.m_fileName.empty());
    CHECK(img.m_fileSize == 0);
    CHECK(ParseContentBrief(::message_type_screenshot, "{\"image\":{\"md5\":\"shot99\"}}").m_imageMd5
          == "shot99");

    //缺字段与非法 JSON 时各项留空
    const HistoryContentBrief none = ParseContentBrief(::message_type_file, "{\"file\":{}}");
    CHECK(none.m_fileName.empty());
    CHECK(none.m_fileSize == 0);
    CHECK(ParseContentBrief(::message_type_image, "{oops").m_imageMd5.empty());
}

// ===================================================================
// 文件夹传输记录条（::message_type_folder）在消息记录窗里的展示
// ===================================================================

namespace {

//按落库时的 BuildContentJson 口径拼一份文件夹记录的归一化 content。
static std::string MakeFolderContentJson(const std::string& sessionId,
                                         const std::string& folderName,
                                         long long files, long long bytes, int result)
{
    std::string json = "{\"folder\":{";
    json += "\"sessionid\":\"" + sessionId + "\",";
    json += "\"foldername\":\"" + folderName + "\",";
    json += "\"totaldirs\":1,";
    json += "\"totalfiles\":" + std::to_string(files) + ",";
    json += "\"totalsize\":" + std::to_string(bytes) + ",";
    json += "\"result\":" + std::to_string(result) + ",";
    json += "\"errcode\":0";
    json += "}}";
    return json;
}

} // namespace

TEST_CASE("V2V-FOLDER1 PreviewFromContent 对文件夹记录带出文件夹名")
{
    CHECK(PreviewFromContent(::message_type_folder,
                             MakeFolderContentJson("s1", u8"项目资料", 128, 1024,
                                                   ::folder_state_completed))
          == u8"[文件夹] 项目资料");

    //缺文件夹名 / 缺 folder 子对象 / 非法 JSON：一律退化成不带名字的占位
    CHECK(PreviewFromContent(::message_type_folder,
                             MakeFolderContentJson("s1", "", 0, 0, ::folder_state_completed))
          == u8"[文件夹]");
    CHECK(PreviewFromContent(::message_type_folder, "{}") == u8"[文件夹]");
    CHECK(PreviewFromContent(::message_type_folder, "not a json") == u8"[文件夹]");
}

TEST_CASE("V2V-FOLDER2 FullTextFromContent 对文件夹记录与预览逐字相同")
{
    //非文本类型一律转调 PreviewFromContent，两处口径不会漂移
    const std::string content =
        MakeFolderContentJson("s1", u8"项目资料", 128, 1024, ::folder_state_completed);
    CHECK(FullTextFromContent(::message_type_folder, content)
          == PreviewFromContent(::message_type_folder, content));
}

TEST_CASE("V2V-FOLDER3 ParseContentBrief 取出会话 id / 名字 / 规模 / 结局")
{
    const HistoryContentBrief brief =
        ParseContentBrief(::message_type_folder,
                          MakeFolderContentJson("sess-8811", u8"项目资料", 128,
                                                2576980378LL, ::folder_state_completed));
    CHECK(brief.m_folderSessionId == "sess-8811");
    CHECK(brief.m_folderName == u8"项目资料");
    CHECK(brief.m_folderTotalFiles == 128);
    CHECK(brief.m_folderTotalSize == 2576980378LL);
    CHECK(brief.m_folderResult == ::folder_state_completed);

    //文件夹记录不该带出图片 / 文件那几项 —— 带出来会让菜单给出下载、另存为这类
    //点下去必然无效的项
    CHECK(brief.m_imageMd5.empty());
    CHECK(brief.m_fileName.empty());
    CHECK(brief.m_fileId.empty());
}

TEST_CASE("V2V-FOLDER4 ParseContentBrief 对缺字段与非法 JSON 安全降级")
{
    const HistoryContentBrief none = ParseContentBrief(::message_type_folder, "not a json");
    CHECK(none.m_folderSessionId.empty());
    CHECK(none.m_folderResult == 0);

    const HistoryContentBrief empty = ParseContentBrief(::message_type_folder, "{\"folder\":{}}");
    CHECK(empty.m_folderSessionId.empty());
    CHECK(empty.m_folderName.empty());
    CHECK(empty.m_folderTotalFiles == 0);
    CHECK(empty.m_folderResult == 0);
}

TEST_CASE("V2V-FOLDER5 FolderResultText 六种终态各有文案、非终态返回空串")
{
    CHECK(FolderResultText(::folder_state_rejected) == u8"已拒绝");
    CHECK(FolderResultText(::folder_state_canceled) == u8"已撤回");
    CHECK(FolderResultText(::folder_state_peer_offline) == u8"对方不在线");
    CHECK(FolderResultText(::folder_state_timeout) == u8"邀请已超时");
    CHECK(FolderResultText(::folder_state_failed) == u8"传输失败");
    CHECK(FolderResultText(::folder_state_completed) == u8"已完成");

    //过程状态与越界值：不显示结局，好过显示一个编不出名字的状态
    CHECK(FolderResultText(0).empty());
    CHECK(FolderResultText(::folder_state_ringing).empty());
    CHECK(FolderResultText(::folder_state_transferring).empty());
    CHECK(FolderResultText(::folder_state_completed + 1).empty());
}

TEST_CASE("V2V-FOLDER6 BuildFolderRowText 各段齐全时按 · 顺排")
{
    HistoryContentBrief brief;
    brief.m_folderName       = u8"项目资料";
    brief.m_folderTotalFiles = 128;
    brief.m_folderTotalSize  = 2576980378LL;
    brief.m_folderResult     = ::folder_state_completed;

    //大小文字复用 FormatFileSize，故这里按它的输出拼期望值，避免把格式化口径抄成两份
    const std::string expected =
        u8"[文件夹] 项目资料 · 128 个文件 · " + FormatFileSize(2576980378LL) + u8" · 已完成";
    CHECK(BuildFolderRowText(brief) == expected);
}

TEST_CASE("V2V-FOLDER7 BuildFolderRowText 缺哪一段就少哪一段、不留悬空分隔点")
{
    //空目录（0 个文件、0 字节）且结局是被拒绝：只剩类型前缀 + 名字 + 结局
    {
        HistoryContentBrief brief;
        brief.m_folderName   = u8"空目录";
        brief.m_folderResult = ::folder_state_rejected;
        CHECK(BuildFolderRowText(brief) == u8"[文件夹] 空目录 · 已拒绝");
    }
    //连文件夹名都没有、结局也不可用：只剩类型前缀，且不以分隔点开头或结尾
    {
        HistoryContentBrief brief;
        const std::string line = BuildFolderRowText(brief);
        CHECK(line == u8"[文件夹]");
        CHECK(line.find(u8" · ") == std::string::npos);
    }
    //有名字有大小、结局不可用：末尾不留分隔点
    {
        HistoryContentBrief brief;
        brief.m_folderName      = u8"资料";
        brief.m_folderTotalSize = 1024;
        const std::string line = BuildFolderRowText(brief);
        CHECK(line == u8"[文件夹] 资料 · " + FormatFileSize(1024));
    }
}
