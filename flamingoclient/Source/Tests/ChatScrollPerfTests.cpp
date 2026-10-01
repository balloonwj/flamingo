/**
 *  ChatScrollPerfTests.cpp
 *
 *  聊天窗消息列表的性能基准（标签 [.chatperf]，隐藏标签，全量运行时不执行）。
 *
 *  用途：量出「打开聊天窗时灌入首屏历史」与「滚轮滚动一格」这两条路径上各环节的耗时，
 *  供优化前后对比。只输出数据、不设耗时阈值断言 —— 耗时随机器与负载波动，阈值写死了
 *  会在别的机器上误报。
 *
 *  运行：FlamingoUnit.exe "[.chatperf]"
 *
 *  量的是什么：
 *    · 灌入：按首屏历史的做法逐条 AppendMessage，统计总耗时；
 *    · 首帧：灌完后第一次绘制（各行的渲染缓存都还是空的）与第二次绘制；
 *    · 滚轮：从底部起逐格向上滚，每格的平滑滚动按 16 毫秒一帧逐帧推进，每帧分别统计排版与
 *            绘制（ChatMessageList::OnPaint）的耗时，给出平均值与最大值；再逐格滚回
 *            底部统计一遍（此时渲染缓存已经建好）。
 *
 *  消息组成模拟真实会话：长短文字混排、带内联表情的文字、图片、文件，双方交替发言，
 *  时间间隔足以插入部分时间分隔行。
 *
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../Common/Util/PerfStopWatch.h"
#include "../net/MessageProtocolClient.h"   // CPF2：构造并解析一页同步应答
#include "../UI/BalloonChatWnd/ChatInlineText.h"   // CPF4：单独量纯折行排版
#include "../UI/BalloonChat/BalloonChatMsgFormat.h"  // CPF4：ParseSendBody
#include "../UI/BalloonChatWnd/ChatTextExtentCache.h"  // CPF5：开关文字测量缓存
#include "../UI/BalloonChatWnd/ChatLinkDetect.h"       // CPF6：单独量超链接识别
#include "../UI/BalloonChatWnd/ChatTextWarmup.h"       // CPF7：取预热字符表
#include "ChatLinkSampleText.h"                     // CPF11：每行带一个链接的长消息

using namespace balloonchatwnd;

namespace {

// 消息区尺寸（像素），取单聊窗默认大小下消息区的实际尺寸量级。
const int kViewW = 620;
const int kViewH = 420;

// 模拟一次滚动操作连续滚的格数。
const int kWheelNotches = 30;

// 一格滚轮的 delta（Windows 约定值）。
const short kOneNotch = WHEEL_DELTA;

// 相邻两条消息的时间间隔（毫秒）。取 3 分钟：时间分隔的阈值是 5 分钟，
// 这样一部分消息之间会插入时间分隔行，一部分不会，与真实会话相近。
const long long kMsgIntervalMs = 3LL * 60 * 1000;

// 第一条消息的时间（Unix 毫秒，2026-09-30 前后的任意值即可）。
const long long kBaseTimestampMs = 1790000000000LL;

// 图片消息的显示尺寸（像素）。
const int kImageW = 200;
const int kImageH = 150;

// 文件消息的大小（字节）。
const long long kFileBytes = 1536LL * 1024;

// 在作用域内启动 GDI+，离开时关闭。气泡的形状都用 GDI+ 绘制。
class ScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    ScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~ScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 一块离屏 32 位位图画布，析构时释放。
class ScopedCanvas
{
public:
    // 建一块 w×h 的画布；失败时 Dc() 返回 NULL。
    ScopedCanvas(int w, int h)
        : m_dc(NULL)
        , m_bmp(NULL)
        , m_old(NULL)
    {
        HDC screen = ::GetDC(NULL);
        m_dc = ::CreateCompatibleDC(screen);
        m_bmp = ::CreateCompatibleBitmap(screen, w, h);
        ::ReleaseDC(NULL, screen);
        if (m_dc != NULL && m_bmp != NULL)
        {
            m_old = ::SelectObject(m_dc, m_bmp);
        }
    }

    // 换回原位图并释放资源。
    ~ScopedCanvas()
    {
        if (m_dc != NULL && m_old != NULL)
        {
            ::SelectObject(m_dc, m_old);
        }
        if (m_bmp != NULL)
        {
            ::DeleteObject(m_bmp);
        }
        if (m_dc != NULL)
        {
            ::DeleteDC(m_dc);
        }
    }

    // 画布的设备上下文（由本类持有）。
    HDC Dc() const
    {
        return m_dc;
    }

private:
    HDC     m_dc;    // 内存 DC
    HBITMAP m_bmp;   // 画布位图
    HGDIOBJ m_old;   // m_dc 原先选入的位图，释放前换回
};

// 一组耗时样本的统计。
struct Stat
{
    double m_sum;     // 总和（毫秒）
    double m_max;     // 最大值（毫秒）
    int    m_count;   // 样本数

    Stat()
        : m_sum(0.0)
        , m_max(0.0)
        , m_count(0)
    {
    }

    // 记一个样本。
    void Add(double ms)
    {
        m_sum += ms;
        m_max = (ms > m_max) ? ms : m_max;
        ++m_count;
    }

    // 平均值（毫秒）；没有样本时为 0。
    double Avg() const
    {
        return (m_count > 0) ? (m_sum / m_count) : 0.0;
    }
};

// 造第 i 条模拟消息。按 i 取模轮换消息类型，使长短文字、表情、图片、文件按固定
// 比例混排，每次运行生成的内容完全相同，前后两次测量才可比。
ChatMessage MakeSampleMessage(int i)
{
    const bool fromMe = (i % 3 == 0);
    ChatMessage m;
    switch (i % 10)
    {
    //长文字：会折成好几行，是排版最重的一类
    case 0:
        m = MakeTextMessage(fromMe,
            _T("这一版的改动比较多，主要集中在消息列表和输入区两块。消息列表那边把滚动时的重排")
            _T("做了收敛，输入区换成了无窗口的富文本控件，粘贴图片和表情都走同一套逻辑。另外")
            _T("会话列表的未读数改成由服务端推送驱动，不再每次切换都去拉一遍，大家回归的时候")
            _T("重点看一下这几个地方，有问题直接在群里说。"));
        break;
    //图片：占位图（下载中）
    case 3:
        m = MakeDownloadingImageMessage(fromMe, kImageW, kImageH, 900000 + i);
        break;
    //文件：已下载完成的文件卡片
    case 6:
        m = MakeIncomingFileMessage(fromMe, _T("季度报告.xlsx"), kFileBytes,
                                    900000 + i, "fileid-perf", "md5-perf", 0);
        break;
    //短文字
    case 1:
    case 4:
    case 7:
        m = MakeTextMessage(fromMe, _T("好的，收到"));
        break;
    //带内联表情的中等长度文字
    case 8:
        m = MakeTextMessage(fromMe, _T("哈哈这个太好笑了/f[\"5\"]/f[\"12\"]，明天再聊"));
        break;
    //中等长度文字
    default:
        m = MakeTextMessage(fromMe, _T("我这边的接口已经联调通了，下午可以提测，你那边准备好了吗？"));
        break;
    }
    m.msgid             = 900000 + i;
    m.timestampMs       = kBaseTimestampMs + kMsgIntervalMs * i;
    m.senderAvatarText  = fromMe ? _T("我") : _T("苏");
    m.senderAvatarColor = fromMe ? RGB(0xFF, 0x7A, 0x45) : RGB(0x3B, 0x82, 0xF6);
    return m;
}

// 滚 kWheelNotches 格，每格按 16 毫秒一帧把平滑滚动的动画逐帧推进完，
// 每一帧分别统计排版（AdvanceSmoothScroll 内的重排）与绘制的耗时。
//   list / dc / view：被测列表、画布与视口矩形。
//   wheelPt / delta：  滚轮落点与每格的 delta。
//   layout / paint：   [出参] 每帧排版、绘制耗时的统计。
void RunWheelFrames(ChatMessageList& list, HDC dc, const RECT& view, POINT wheelPt,
                    short delta, Stat& layout, Stat& paint)
{
    const double kFrameMs = 16.0;
    PerfStopWatch sw;
    for (int n = 0; n < kWheelNotches; ++n)
    {
        list.OnMouseWheel(wheelPt, delta, 0);
        double t = PerfNowMs();
        while (list.IsSmoothScrolling())
        {
            t += kFrameMs;
            sw.Restart();
            list.AdvanceSmoothScroll(t);
            layout.Add(sw.ElapsedMs());
            sw.Restart();
            list.OnPaint(dc, view);
            paint.Add(sw.ElapsedMs());
        }
    }
}

// 对一个装好 count 条消息的列表做一整套测量，并打印结果。
void RunScenario(int count)
{
    ScopedCanvas canvas(kViewW, kViewH);
    REQUIRE(canvas.Dc() != NULL);
    const RECT view = { 0, 0, kViewW, kViewH };
    const POINT wheelPt = { kViewW / 2, kViewH / 2 };

    ChatMessageList list;
    list.ForceLayout(view);

    //灌入：与首屏历史的做法一致，逐条追加
    ChatBubble::ResetHeavyMeasureCount();
    PerfStopWatch sw;
    for (int i = 0; i < count; ++i)
    {
        list.AppendMessage(MakeSampleMessage(i));
    }
    const double fillMs = sw.ElapsedMs();
    const unsigned long heavyMeasures = ChatBubble::HeavyMeasureCount();

    //首帧与第二帧
    sw.Restart();
    list.OnPaint(canvas.Dc(), view);
    const double firstPaintMs = sw.ElapsedMs();
    sw.Restart();
    list.OnPaint(canvas.Dc(), view);
    const double secondPaintMs = sw.ElapsedMs();

    //逐格向上滚：途经的行多数是第一次绘制。滚轮只改平滑滚动的目标（2026-09-30 起），
    //这里按 16 毫秒一帧把每一格的动画逐帧推进完，统计每一帧的排版与绘制。
    Stat upLayout;
    Stat upPaint;
    RunWheelFrames(list, canvas.Dc(), view, wheelPt, kOneNotch, upLayout, upPaint);

    //逐格滚回底部：途经的行都已画过一次
    Stat downLayout;
    Stat downPaint;
    RunWheelFrames(list, canvas.Dc(), view, wheelPt, (short)-kOneNotch, downLayout, downPaint);

    printf("\n[chatperf] messages=%d rows=%u content=%dpx view=%dx%d\n",
           count, (unsigned)list.Children().size(), list.ContentHeight(), kViewW, kViewH);
    printf("[chatperf]   fill (AppendMessage x %d): %8.2f ms total, %.3f ms/msg, heavy measures=%lu\n",
           count, fillMs, fillMs / count, heavyMeasures);
    printf("[chatperf]   first paint: %8.2f ms, second paint: %8.2f ms\n",
           firstPaintMs, secondPaintMs);
    printf("[chatperf]   wheel up   x %d (%d frames): layout avg %6.3f / max %6.3f ms, paint avg %6.3f / max %6.3f ms\n",
           kWheelNotches, upLayout.m_count, upLayout.Avg(), upLayout.m_max, upPaint.Avg(), upPaint.m_max);
    printf("[chatperf]   wheel down x %d (%d frames): layout avg %6.3f / max %6.3f ms, paint avg %6.3f / max %6.3f ms\n",
           kWheelNotches, downLayout.m_count, downLayout.Avg(), downLayout.m_max, downPaint.Avg(), downPaint.m_max);
    fflush(stdout);

    //滚回底部后列表应当仍然完整（防止测量代码本身出错而不自知）
    CHECK(list.ContentHeight() > kViewH);
}

} // anonymous namespace

TEST_CASE("CPF1 消息列表灌入与滚轮滚动的耗时基准", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());

    //首屏历史一页是 100 条；300 条对应用户往上翻过两页之后的列表规模
    RunScenario(100);
    RunScenario(300);
}

TEST_CASE("CPF2 解析一页 50 条消息的同步应答的耗时", "[.chatperf]")
{
    //用客户端自己的协议构造函数拼消息体，再补上服务端应答才有的 msgid / from / timestamp，
    //与 GroupMentionTests.cpp 构造历史应答的做法相同。文字按 MakeSampleMessage 的比例混排。
    const int kPageSize = 50;
    const int kRepeat   = 20;
    std::string resp = "{\"code\":0,\"msg\":\"ok\",\"ctype\":1,\"cid\":\"5308\",\"hasmore\":true,"
                       "\"peerreadmsgid\":\"328\",\"messages\":[";
    for (int i = 0; i < kPageSize; ++i)
    {
        const ChatMessage sample = MakeSampleMessage(i);
        const std::string text = (sample.kind == kMsgText)
            ? std::string(CW2A((LPCWSTR)sample.text, CP_UTF8)) : std::string("[image]");
        net::TextFont font;
        font.m_valid = true;
        font.m_size  = 14;
        std::string msg = net::MessageProtocolClient::buildSendTextRequest(5308, "CLIENT-PERF", text, font);
        char head[128];
        sprintf_s(head, "\"msgid\":\"%d\",\"from\":\"%d\",\"timestamp\":\"%lld\",",
                  900000 + i, (i % 3 == 0) ? 5127 : 5308, 1790000000000LL + i * 60000LL);
        msg.insert(1, head);
        if (i > 0)
        {
            resp += ",";
        }
        resp += msg;
    }
    resp += "]}";

    PerfStopWatch sw;
    size_t parsed = 0;
    for (int r = 0; r < kRepeat; ++r)
    {
        net::SyncMessagesResult out;
        REQUIRE(net::MessageProtocolClient::parseSyncMessagesResponse(resp, out));
        parsed = out.m_messages.size();
    }
    const double perParseMs = sw.ElapsedMs() / kRepeat;
    CHECK(parsed == (size_t)kPageSize);
    printf("\n[chatperf] parse sync response: %u messages, %u bytes, %.2f ms per parse\n",
           (unsigned)parsed, (unsigned)resp.size(), perParseMs);
    fflush(stdout);
}

TEST_CASE("CPF3 渲染 50 条消息时建气泡、折行测量、追加进列表各花多久", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    const int kPageSize = 50;
    std::vector<ChatMessage> msgs;
    for (int i = 0; i < kPageSize; ++i)
    {
        msgs.push_back(MakeSampleMessage(i));
    }

    //一、只建气泡、设消息，不测量、不进列表
    PerfStopWatch sw;
    std::vector<std::unique_ptr<ChatBubble> > bubbles;
    for (int i = 0; i < kPageSize; ++i)
    {
        std::unique_ptr<ChatBubble> b(new ChatBubble());
        b->SetMessage(msgs[i]);
        bubbles.push_back(std::move(b));
    }
    const double createMs = sw.ElapsedMs();

    //二、对刚建好的气泡逐个做一次折行测量（首次测量，最重的那一步）
    sw.Restart();
    for (int i = 0; i < kPageSize; ++i)
    {
        bubbles[i]->Measure(kViewW);
    }
    const double measureMs = sw.ElapsedMs();

    //三、按首屏历史的做法逐条追加进列表（含建气泡、测量、每次追加后的整表重排）
    ChatMessageList list;
    const RECT view = { 0, 0, kViewW, kViewH };
    list.ForceLayout(view);
    sw.Restart();
    for (int i = 0; i < kPageSize; ++i)
    {
        list.AppendMessage(msgs[i]);
    }
    const double appendMs = sw.ElapsedMs();

    printf("\n[chatperf] render %d messages: create %.2f ms, first measure %.2f ms, "
           "append all (create + measure + relayout) %.2f ms => relayout and rest %.2f ms\n",
           kPageSize, createMs, measureMs, appendMs, appendMs - createMs - measureMs);
    fflush(stdout);
    CHECK(list.ContentHeight() > 0);
}

namespace {

//CPF4 各项微基准的重复次数
const int kCpf4Repeat = 2000;
//CPF4 纯折行排版的可用宽度（像素），与单聊窗默认大小下文字气泡的最大正文宽度同一量级
const int kCpf4WrapWidth = 400;
//CPF4 使用的字号（像素），即气泡正文的默认字号
const int kCpf4FontPx = 14;

//与 ChatBubble.cpp 的 DrawGdiText / MeasureTextWidth / Measure 建字体的写法完全相同
HFONT CreateBubbleFont(int pixelHeight)
{
    return ::CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

} // anonymous namespace

TEST_CASE("CPF4 首次测量的构成：建字体、取屏幕 DC、纯折行排版各花多久", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    const int kPageSize = 50;

    HDC hdc = ::GetDC(NULL);
    REQUIRE(hdc != NULL);

    //一、建字体 + 选入 DC + 选出 + 删除，一次的平均耗时
    PerfStopWatch sw;
    for (int i = 0; i < kCpf4Repeat; ++i)
    {
        HFONT hFont = CreateBubbleFont(kCpf4FontPx);
        HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
        ::SelectObject(hdc, hOld);
        ::DeleteObject(hFont);
    }
    const double fontUs = sw.ElapsedMs() * 1000.0 / kCpf4Repeat;

    //二、字体已建好时只做选入 + 选出，一次的平均耗时（字体缓存命中后剩下的开销）
    HFONT hShared = CreateBubbleFont(kCpf4FontPx);
    sw.Restart();
    for (int i = 0; i < kCpf4Repeat; ++i)
    {
        HFONT hOld = (HFONT)::SelectObject(hdc, hShared);
        ::SelectObject(hdc, hOld);
    }
    const double selectUs = sw.ElapsedMs() * 1000.0 / kCpf4Repeat;

    //三、GetDC(NULL) + ReleaseDC，一次的平均耗时（Measure 每次都取一次屏幕 DC）
    sw.Restart();
    for (int i = 0; i < kCpf4Repeat; ++i)
    {
        HDC h = ::GetDC(NULL);
        ::ReleaseDC(NULL, h);
    }
    const double getDcUs = sw.ElapsedMs() * 1000.0 / kCpf4Repeat;

    //四、纯折行排版：字体只建一次，逐条 ParseSendBody + LayoutInlineContent（只算文字消息）
    std::vector<ChatMessage> msgs;
    for (int i = 0; i < kPageSize; ++i)
        msgs.push_back(MakeSampleMessage(i));
    int textCount = 0;
    HFONT hOld = (HFONT)::SelectObject(hdc, hShared);
    sw.Restart();
    for (int i = 0; i < kPageSize; ++i)
    {
        if (msgs[i].kind != kMsgText)
            continue;
        ++textCount;
        std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(msgs[i].text);
        std::vector<InlinePlaced> placed;
        LayoutInlineContent(hdc, segs, kCpf4WrapWidth, kCpf4FontPx, &placed);
    }
    const double layoutMs = sw.ElapsedMs();
    ::SelectObject(hdc, hOld);
    ::DeleteObject(hShared);
    ::ReleaseDC(NULL, hdc);

    //五、完整的首次测量（ChatBubble::Measure，全部 50 条，含图片 / 文件等非文字气泡）
    std::vector<std::unique_ptr<ChatBubble> > bubbles;
    for (int i = 0; i < kPageSize; ++i)
    {
        std::unique_ptr<ChatBubble> b(new ChatBubble());
        b->SetMessage(msgs[i]);
        bubbles.push_back(std::move(b));
    }
    sw.Restart();
    for (int i = 0; i < kPageSize; ++i)
        bubbles[i]->Measure(kViewW);
    const double measureMs = sw.ElapsedMs();

    printf("\n[chatperf] CPF4 font create+select+delete %.2f us, select only %.2f us, "
           "GetDC+ReleaseDC %.2f us\n", fontUs, selectUs, getDcUs);
    printf("[chatperf] CPF4 pure wrap layout of %d text messages %.2f ms; full first Measure of %d bubbles %.2f ms\n",
           textCount, layoutMs, kPageSize, measureMs);
    fflush(stdout);
    CHECK(textCount > 0);
}

namespace {

//CPF5 对 50 条消息排版的重复轮数，取平均
const int kCpf5Rounds = 5;

//对 msgs 里的文字消息逐条做一次纯折行排版，返回总耗时（毫秒）。bPlaced 为 false 时只测尺寸、
//不产出放置项，用来区分"测量"与"产出放置项（含逐项复制文字）"两部分开销
double TimePureLayout(HDC hdc, const std::vector<ChatMessage>& msgs, bool bPlaced)
{
    PerfStopWatch sw;
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        if (msgs[i].kind != kMsgText)
            continue;
        std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(msgs[i].text);
        std::vector<InlinePlaced> placed;
        LayoutInlineContent(hdc, segs, kCpf4WrapWidth, kCpf4FontPx, bPlaced ? &placed : NULL);
    }
    return sw.ElapsedMs();
}

//对 msgs 逐条新建气泡并做首次测量，返回测量部分的总耗时（毫秒）
double TimeFirstMeasure(const std::vector<ChatMessage>& msgs)
{
    std::vector<std::unique_ptr<ChatBubble> > bubbles;
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        std::unique_ptr<ChatBubble> b(new ChatBubble());
        b->SetMessage(msgs[i]);
        bubbles.push_back(std::move(b));
    }
    PerfStopWatch sw;
    for (size_t i = 0; i < bubbles.size(); ++i)
        bubbles[i]->Measure(kViewW);
    return sw.ElapsedMs();
}

} // anonymous namespace

TEST_CASE("CPF5 文字测量缓存关闭 / 为空 / 已命中三种情况下，排版与首次测量各花多久", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    const int kPageSize = 50;
    std::vector<ChatMessage> msgs;
    for (int i = 0; i < kPageSize; ++i)
        msgs.push_back(MakeSampleMessage(i));

    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();
    HDC hdc = ::GetDC(NULL);
    HFONT hFont = CreateBubbleFont(kCpf4FontPx);
    HFONT hOld = (HFONT)::SelectObject(hdc, hFont);

    double offPlaced = 0.0;
    double offSizeOnly = 0.0;
    double coldPlaced = 0.0;
    double hotPlaced = 0.0;
    double hotSizeOnly = 0.0;
    double measureOff = 0.0;
    double measureHot = 0.0;
    for (int r = 0; r < kCpf5Rounds; ++r)
    {
        cache.SetEnabled(false);
        offPlaced += TimePureLayout(hdc, msgs, true);
        offSizeOnly += TimePureLayout(hdc, msgs, false);
        measureOff += TimeFirstMeasure(msgs);

        cache.SetEnabled(true);
        cache.Clear();
        coldPlaced += TimePureLayout(hdc, msgs, true);
        hotPlaced += TimePureLayout(hdc, msgs, true);
        hotSizeOnly += TimePureLayout(hdc, msgs, false);
        measureHot += TimeFirstMeasure(msgs);
    }
    cache.SetEnabled(true);
    cache.Clear();

    ::SelectObject(hdc, hOld);
    ::DeleteObject(hFont);
    ::ReleaseDC(NULL, hdc);

    printf("\n[chatperf] CPF5 pure layout (avg of %d rounds): cache off %.2f ms (size only %.2f), "
           "cold %.2f ms, hot %.2f ms (size only %.2f)\n",
           kCpf5Rounds, offPlaced / kCpf5Rounds, offSizeOnly / kCpf5Rounds, coldPlaced / kCpf5Rounds,
           hotPlaced / kCpf5Rounds, hotSizeOnly / kCpf5Rounds);
    printf("[chatperf] CPF5 first Measure of %d bubbles: cache off %.2f ms, cache hot %.2f ms\n",
           kPageSize, measureOff / kCpf5Rounds, measureHot / kCpf5Rounds);
    fflush(stdout);
    CHECK(msgs.size() == (size_t)kPageSize);
}

TEST_CASE("CPF6 文字气泡首次测量的各个子步骤各花多久（文字测量缓存已命中）", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    const int kPageSize = 50;
    std::vector<ChatMessage> msgs;
    for (int i = 0; i < kPageSize; ++i)
        msgs.push_back(MakeSampleMessage(i));

    HDC hdc = ::GetDC(NULL);
    HFONT hFont = CreateBubbleFont(kCpf4FontPx);
    HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
    //先完整排一遍，让文字测量缓存命中，下面量的就是缓存之外的开销
    TimePureLayout(hdc, msgs, true);

    double parseMs = 0.0;
    double layoutMs = 0.0;
    double flatMs = 0.0;
    double selMs = 0.0;
    double linkMs = 0.0;
    for (int r = 0; r < kCpf5Rounds; ++r)
    {
        for (size_t i = 0; i < msgs.size(); ++i)
        {
            if (msgs[i].kind != kMsgText)
                continue;
            PerfStopWatch sw;
            std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(msgs[i].text);
            parseMs += sw.ElapsedMs();

            sw.Restart();
            std::vector<InlinePlaced> placed;
            LayoutInlineContent(hdc, segs, kCpf4WrapWidth, kCpf4FontPx, &placed);
            layoutMs += sw.ElapsedMs();

            sw.Restart();
            const std::wstring flat = BuildFlatText(segs);
            flatMs += sw.ElapsedMs();

            sw.Restart();
            SelLayout sel;
            BuildSelLayoutFromPlaced(hdc, placed, (int)flat.size(), sel);
            selMs += sw.ElapsedMs();

            sw.Restart();
            const std::vector<LinkSpan> links = DetectLinks(flat);
            linkMs += sw.ElapsedMs();
        }
    }
    ::SelectObject(hdc, hOld);
    ::DeleteObject(hFont);
    ::ReleaseDC(NULL, hdc);

    const double measureMs = TimeFirstMeasure(msgs);
    printf("\n[chatperf] CPF6 per page of %d messages (avg of %d rounds, extent cache hot): "
           "parse %.2f ms, layout %.2f ms, flat text %.2f ms, sel layout %.2f ms, links %.2f ms; "
           "full first Measure %.2f ms\n",
           kPageSize, kCpf5Rounds, parseMs / kCpf5Rounds, layoutMs / kCpf5Rounds, flatMs / kCpf5Rounds,
           selMs / kCpf5Rounds, linkMs / kCpf5Rounds, measureMs);
    fflush(stdout);
    CHECK(msgs.size() == (size_t)kPageSize);
}

TEST_CASE("CPF7 单条长消息的首次测量耗时随字数如何增长（缓存为空 / 已命中）", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    const std::wstring chars = BuildWarmupCharacters();
    const int kLengths[] = { 25, 50, 100, 150, 300 };
    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();

    for (size_t li = 0; li < sizeof(kLengths) / sizeof(kLengths[0]); ++li)
    {
        const int len = kLengths[li];
        const CString text(chars.substr(0, (size_t)len).c_str());
        cache.Clear();

        PerfStopWatch sw;
        ChatBubble cold;
        cold.SetMessage(MakeTextMessage(false, text));
        const double setMs = sw.ElapsedMs();
        sw.Restart();
        cold.Measure(kViewW);
        const double coldMs = sw.ElapsedMs();

        ChatBubble hot;
        hot.SetMessage(MakeTextMessage(false, text));
        sw.Restart();
        hot.Measure(kViewW);
        const double hotMs = sw.ElapsedMs();

        HDC hdc = ::GetDC(NULL);
        HFONT hFont = CreateBubbleFont(kCpf4FontPx);
        HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
        std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(text);
        sw.Restart();
        std::vector<InlinePlaced> placed;
        LayoutInlineContent(hdc, segs, kCpf4WrapWidth, kCpf4FontPx, &placed);
        const double layoutMs = sw.ElapsedMs();
        sw.Restart();
        SelLayout sel;
        BuildSelLayoutFromPlaced(hdc, placed, (int)BuildFlatText(segs).size(), sel);
        const double selMs = sw.ElapsedMs();
        sw.Restart();
        DetectLinks(BuildFlatText(segs));
        const double linkMs = sw.ElapsedMs();
        ::SelectObject(hdc, hOld);
        ::DeleteObject(hFont);
        ::ReleaseDC(NULL, hdc);

        printf("[chatperf] CPF7 %3d chars: SetMessage %.3f ms, Measure cold %.3f ms, hot %.3f ms | "
               "hot layout %.3f ms, sel %.3f ms, links %.3f ms\n",
               len, setMs, coldMs, hotMs, layoutMs, selMs, linkMs);
    }
    cache.Clear();
    fflush(stdout);
    CHECK(chars.size() > 0);
}

TEST_CASE("CPF8 预热时每一批的测量耗时", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatTextWarmup& warmup = ChatTextWarmup::Inst();
    warmup.Reset();
    ChatTextExtentCache::Inst().Clear();

    std::vector<double> sliceMs;
    bool bMore = true;
    while (bMore)
    {
        PerfStopWatch sw;
        bMore = warmup.RunSlice(ChatTextWarmup::kSliceChars);
        sliceMs.push_back(sw.ElapsedMs());
    }
    printf("\n[chatperf] CPF8 %u slices:", (unsigned)sliceMs.size());
    double total = 0.0;
    for (size_t i = 0; i < sliceMs.size(); ++i)
    {
        printf(" %.1f", sliceMs[i]);
        total += sliceMs[i];
    }
    printf("\n[chatperf] CPF8 total %.1f ms\n", total);
    fflush(stdout);
    warmup.Reset();
    ChatTextExtentCache::Inst().Clear();
    CHECK(!sliceMs.empty());
}

namespace {

//CPF9 重复型语料：测试服务端里灌的消息内容（tmp/chatperf/seed_perf_data.py），长、中、短混排
const wchar_t* const kSeedTexts[] = {
    L"这一版的改动比较多，主要集中在消息列表和输入区两块。消息列表那边把滚动时的重排做了收敛，"
    L"输入区换成了无窗口的富文本控件，粘贴图片和表情都走同一套逻辑。另外会话列表的未读数改成由"
    L"服务端推送驱动，不再每次切换都去拉一遍，大家回归的时候重点看一下这几个地方，有问题直接在群里说。",
    L"好的", L"收到", L"嗯嗯", L"没问题", L"稍等我看一下", L"OK", L"明白了", L"马上",
    L"我这边的接口已经联调通了，下午可以提测，你那边准备好了吗？",
    L"刚才那个问题复现了，是登录之后立刻切换会话才会出现，我把日志发你。",
    L"周五之前能把设计稿定下来吗？开发这边等着排期。",
    L"测试环境的数据库我重置了一下，账号还是原来那几个。",
    L"明天上午十点开个短会，把这周的进度对一下。",
};
//CPF9 多样型语料：每条消息的字数，以及在常用字表里取字的步长（与表长互质，50 条消息几乎不重复用字）
const int kVariedMsgChars = 30;
const int kVariedStride = 97;

//CPF9：造一页 50 条消息。bVaried 为 true 时用多样型语料，否则用重复型语料
std::vector<ChatMessage> MakeCpf9Page(bool bVaried)
{
    const int kPageSize = 50;
    std::vector<ChatMessage> msgs;
    const std::wstring table = BuildWarmupCharacters();
    size_t pos = 0;
    for (int i = 0; i < kPageSize; ++i)
    {
        CString text;
        if (bVaried)
        {
            std::wstring w;
            for (int k = 0; k < kVariedMsgChars; ++k)
            {
                pos = (pos + kVariedStride) % table.size();
                w += table[pos];
            }
            text = w.c_str();
        }
        else
        {
            text = kSeedTexts[(size_t)i % (sizeof(kSeedTexts) / sizeof(kSeedTexts[0]))];
        }
        msgs.push_back(MakeTextMessage(i % 3 == 0, text));
    }
    return msgs;
}

//CPF9：在当前进程里第一次新建并测量这一页的 50 个气泡，返回耗时（毫秒）；bWarmup 为 true 时先预热
double RunCpf9(bool bVaried, bool bWarmup)
{
    const std::vector<ChatMessage> msgs = MakeCpf9Page(bVaried);
    if (bWarmup)
    {
        ChatTextWarmup::Inst().Reset();
        while (ChatTextWarmup::Inst().RunSlice(ChatTextWarmup::kSliceChars))
        {
        }
    }
    PerfStopWatch sw;
    std::vector<std::unique_ptr<ChatBubble> > bubbles;
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        std::unique_ptr<ChatBubble> b(new ChatBubble());
        b->SetMessage(msgs[i]);
        b->Measure(kViewW);
        bubbles.push_back(std::move(b));
    }
    return sw.ElapsedMs();
}

} // anonymous namespace

//以下四条各自须在全新进程里单独运行（每次只跑一条），量的才是"进程启动后第一次打开聊天窗"
TEST_CASE("CPF9A 重复型语料，不预热：全新进程里第一页 50 个气泡的新建与测量", "[.chatperf]")
{
    ScopedGdiplus gdip;
    printf("[chatperf] CPF9 seed corpus, no warmup: %.2f ms\n", RunCpf9(false, false));
}

TEST_CASE("CPF9B 重复型语料，先预热：全新进程里第一页 50 个气泡的新建与测量", "[.chatperf]")
{
    ScopedGdiplus gdip;
    printf("[chatperf] CPF9 seed corpus, warmed up: %.2f ms\n", RunCpf9(false, true));
}

TEST_CASE("CPF9C 多样型语料，不预热：全新进程里第一页 50 个气泡的新建与测量", "[.chatperf]")
{
    ScopedGdiplus gdip;
    printf("[chatperf] CPF9 varied corpus, no warmup: %.2f ms\n", RunCpf9(true, false));
}

TEST_CASE("CPF9D 多样型语料，先预热：全新进程里第一页 50 个气泡的新建与测量", "[.chatperf]")
{
    ScopedGdiplus gdip;
    printf("[chatperf] CPF9 varied corpus, warmed up: %.2f ms\n", RunCpf9(true, true));
}

// =====================================================================
// CPF11：一条几十行、每行带一个链接的长文字消息（2026-10-01 用户反馈发出后界面卡顿约 9 秒）
// =====================================================================

namespace {

//CPF11 依次量的行数，最后一档与用户实际发送的那段文字行数相同
const int kCpf11LineCounts[] = { 5, 10, 20, 40, 68 };

} // anonymous namespace

TEST_CASE("CPF11 几十行带链接的长消息：追加、排版、首次绘制与各子步骤的耗时随行数如何增长", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();

    for (size_t li = 0; li < sizeof(kCpf11LineCounts) / sizeof(kCpf11LineCounts[0]); ++li)
    {
        const int lines = kCpf11LineCounts[li];
        const CString text = chattest::BuildLinkSampleText(lines);
        cache.Clear();

        //与发送路径相同：先追加进列表（乐观回显），再排版、绘制可见区域
        ScopedCanvas canvas(kViewW, kViewH);
        REQUIRE(canvas.Dc() != NULL);
        const RECT view = { 0, 0, kViewW, kViewH };
        ChatMessageList list;
        list.ForceLayout(view);

        PerfStopWatch sw;
        list.AppendMessage(MakeTextMessage(true, text));
        const double appendMs = sw.ElapsedMs();
        sw.Restart();
        list.ForceLayout(view);
        const double layoutMs = sw.ElapsedMs();
        sw.Restart();
        list.OnPaint(canvas.Dc(), view);
        const double firstPaintMs = sw.ElapsedMs();
        sw.Restart();
        list.OnPaint(canvas.Dc(), view);
        const double secondPaintMs = sw.ElapsedMs();

        //子步骤单独量（文字测量缓存此时已由上面的排版填过）
        HDC hdc = ::GetDC(NULL);
        HFONT hFont = CreateBubbleFont(kCpf4FontPx);
        HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
        sw.Restart();
        std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(text);
        const double parseMs = sw.ElapsedMs();
        sw.Restart();
        std::vector<InlinePlaced> placed;
        LayoutInlineContent(hdc, segs, kCpf4WrapWidth, kCpf4FontPx, &placed);
        const double inlineMs = sw.ElapsedMs();
        sw.Restart();
        const std::wstring flat = BuildFlatText(segs);
        const double flatMs = sw.ElapsedMs();
        sw.Restart();
        SelLayout sel;
        BuildSelLayoutFromPlaced(hdc, placed, (int)flat.size(), sel);
        const double selMs = sw.ElapsedMs();
        sw.Restart();
        const std::vector<LinkSpan> links = DetectLinks(flat);
        const double linkMs = sw.ElapsedMs();
        ::SelectObject(hdc, hOld);
        ::DeleteObject(hFont);
        ::ReleaseDC(NULL, hdc);

        //气泡画超链接的方式：所有链接一次交给 DrawInlineContentRestyledRanges，整条只排版一次、每个矩形
        //只重画落在里面的项（2026-10-01 之前是逐个链接、逐个矩形整条重画）。分别量完整画一遍、以及把全部
        //链接重画一遍的耗时，并统计链接一共占几个矩形
        size_t linkRects = 0;
        std::vector<InlineCharRange> linkRanges;
        for (size_t k = 0; k < links.size(); ++k)
        {
            linkRects += BuildSelectionRects(sel, links[k].m_off, links[k].m_off + links[k].m_len).size();
            linkRanges.push_back(InlineCharRange(links[k].m_off, links[k].m_off + links[k].m_len));
        }
        const RECT txRc = { 0, 0, kCpf4WrapWidth, kViewH };
        InlineTextStyle st;
        st.sizePx = kCpf4FontPx;
        sw.Restart();
        DrawInlineContent(canvas.Dc(), segs, txRc, st, kCpf4FontPx);
        const double fullDrawMs = sw.ElapsedMs();
        sw.Restart();
        DrawInlineContentRestyledRanges(canvas.Dc(), segs, sel, linkRanges, txRc, st, kCpf4FontPx);
        const double restyleMs = sw.ElapsedMs();
        printf("[chatperf] CPF11 %2d lines: link rects %u, one full DrawInlineContent %.2f ms, "
               "restyle all links %.2f ms\n", lines, (unsigned)linkRects, fullDrawMs, restyleMs);

        //对照：同一段文字把 "https://" 改成 "https//"，不再被识别为链接，其余完全相同
        CString noLinkText = text;
        noLinkText.Replace(L"https://", L"https//");
        ChatMessageList noLinkList;
        noLinkList.ForceLayout(view);
        noLinkList.AppendMessage(MakeTextMessage(true, noLinkText));
        noLinkList.ForceLayout(view);
        sw.Restart();
        noLinkList.OnPaint(canvas.Dc(), view);
        const double noLinkPaintMs = sw.ElapsedMs();
        printf("[chatperf] CPF11 %2d lines: first paint without links %.2f ms\n", lines, noLinkPaintMs);

        printf("[chatperf] CPF11 %2d lines (%4d chars, %3u links, %4u placed): append %8.2f ms, layout %8.2f ms, "
               "first paint %8.2f ms, second paint %8.2f ms | parse %.2f, inline layout %.2f, flat %.2f, "
               "sel layout %.2f, links %.2f ms\n",
               lines, text.GetLength(), (unsigned)links.size(), (unsigned)placed.size(), appendMs, layoutMs,
               firstPaintMs, secondPaintMs, parseMs, inlineMs, flatMs, selMs, linkMs);
        fflush(stdout);
    }
    cache.Clear();
    CHECK(true);
}

// =====================================================================
// CPF12：一条两三万字的长文字消息，选区变化后重画要多久（2026-10-01 用户反馈拖选不流畅）
// =====================================================================

namespace {

//CPF12 依次量的消息字数（约数），最后一档与用户反馈的那条调用栈消息同一量级
const int kCpf12Lengths[] = { 3000, 9000, 27000 };

//调用栈里的一行，取自用户那条消息的格式（不含链接），反复拼接到目标字数
const wchar_t* const kCpf12StackLine =
    L"     Flamingo.exe!std::string::append(const unsigned int _Count=1164724235, const char _Ch=0) "
    L"Line 1558    C++    Non-user code. Symbols loaded.\r\n";

//拼一段约 chars 字的调用栈文字
CString BuildCpf12Text(int chars)
{
    std::wstring text;
    while ((int)text.size() < chars)
        text += kCpf12StackLine;
    return CString(text.c_str());
}

} // anonymous namespace

TEST_CASE("CPF12 长文字消息选区变化后重画的耗时随字数如何增长", "[.chatperf]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    for (size_t li = 0; li < sizeof(kCpf12Lengths) / sizeof(kCpf12Lengths[0]); ++li)
    {
        const CString text = BuildCpf12Text(kCpf12Lengths[li]);
        ScopedCanvas canvas(kViewW, kViewH);
        REQUIRE(canvas.Dc() != NULL);
        const RECT view = { 0, 0, kViewW, kViewH };
        ChatMessageList list;
        list.ForceLayout(view);
        list.AppendMessage(MakeTextMessage(false, text));
        list.ForceLayout(view);

        PerfStopWatch sw;
        list.OnPaint(canvas.Dc(), view);
        const double firstMs = sw.ElapsedMs();

        //最后一行是本条消息的气泡（前面可能有时间分隔行）
        ChatBubble* bubble = dynamic_cast<ChatBubble*>(list.Children().back().get());
        REQUIRE(bubble != NULL);

        //选区变化：全选一次、取消一次，各自紧跟一次绘制（与拖选时每步的流程相同）
        bubble->SelectAllText();
        sw.Restart();
        list.OnPaint(canvas.Dc(), view);
        const double selectMs = sw.ElapsedMs();
        bubble->ClearTextSelection();
        sw.Restart();
        list.OnPaint(canvas.Dc(), view);
        const double clearMs = sw.ElapsedMs();
        //没有任何变化时的绘制，作对照
        sw.Restart();
        list.OnPaint(canvas.Dc(), view);
        const double idleMs = sw.ElapsedMs();

        //拖选一步：在可视区里按下，再往下拖两行左右（拖选时每次移动鼠标就是这种小变化）
        const int kPressY = 100;   //按下处距可视区上沿（像素）
        const int kDragY = 140;    //拖到处距可视区上沿（像素）
        const int kDx = 120;       //气泡内的横向落点（像素）
        const RECT rcBubble = bubble->GetRect();
        POINT press = { rcBubble.left + kDx, kPressY };
        POINT drag = { rcBubble.left + kDx, kDragY };
        bubble->OnLButtonDown(press, MK_LBUTTON);
        list.OnPaint(canvas.Dc(), view);
        bubble->OnMouseMove(drag, MK_LBUTTON);
        sw.Restart();
        list.OnPaint(canvas.Dc(), view);
        const double dragMs = sw.ElapsedMs();
        bubble->OnLButtonUp(drag, 0);

        printf("[chatperf] CPF12 %5d chars (row %d px): first paint %8.2f ms, after select-all %8.2f ms, "
               "after clear %8.2f ms, unchanged %6.2f ms, drag one step %6.2f ms\n",
               text.GetLength(), bubble->GetRowHeight(), firstMs, selectMs, clearMs, idleMs, dragMs);
        fflush(stdout);
    }
    CHECK(true);
}
