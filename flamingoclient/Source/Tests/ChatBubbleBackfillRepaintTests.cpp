/**
 *  ChatBubbleBackfillRepaintTests.cpp
 *
 *  「群聊 @ 某人带正文，发出去后气泡是空白的，滚一下才显示」这个缺陷的复现与回归测试。
 *
 *  缺陷链路（本文件用可执行断言逐环坐实）：
 *    1) 发送后服务器回执到达，CBalloonGroupChatWindow::OnSendChatAckBackfill 调
 *       ChatMessageList::BackfillSentMessage 回填 msgid / 时间戳。
 *    2) BackfillSentMessage 用 ChatBubble::SetMessage 写回整条消息，而 SetMessage 是
 *       「内容整体换了」的语义 —— 它无条件把折行测量缓存置为未测量（m_measuredWidth = -1），
 *       且回填路径<u>不</u>重排，于是没有任何人再去 Measure 它。
 *    3) 气泡绘制时发现自己「未测量」，退化到单行回退尺寸：高度取 kTextBubbleH。文字矩形
 *       高 = kTextBubbleH - kTextPadV*2 = 18px，比一行文字还矮。
 *    4) 不含 @ 的气泡走手写折行排版，不按矩形高度裁剪，照样画得出来，所以看不出问题；
 *       含 @ 的气泡走 GDI+ DrawString，而 GDI+ 对<u>放不下的整行直接不画</u>——
 *       一行都放不下时就是一个字都没有，即用户看到的空白气泡。
 *    5) 滚动会触发 ChatMessageList::Layout 重新 Measure，尺寸恢复，文字随之出现。
 *
 *  用例分两类：
 *    · B1 / B2 / B3 —— 针对缺陷本身的回归断言。修复前 B1 / B2 为红。
 *    · B4          —— 机制实证：GDI+ 的排版矩形只要放不下一整行就一个像素都不画，
 *                     解释「为什么是全空白而不是被截掉半行」，也解释为什么只有含 @ 的
 *                     气泡出问题。此用例与产品代码改动无关，修复前后都应为绿。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存 + 离屏位图：不创建窗口、不要 HWND。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/GroupMentionModel.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区宽度 / 高度（逻辑像素）。宽度决定折行结果，取一个能让下面的长
// 文本真正折成多行的值；高度对本文件的断言无影响。
const int kListWidth  = 400;
const int kListHeight = 600;

// 本文件统一用的 clientMsgId（回执按它找回本行）。
const char* const kClientMsgId = "cmid-mention-1";

// 回执带回来的服务器 msgid 与时间戳（毫秒）。取值本身无特殊含义，只要非零、
// 能与回填前的 0 区分开即可。
const long long kAckMsgid       = 987654321LL;
const long long kAckTimestampMs = 1700000000000LL;

// 造一个宽 w、高 kListHeight 的矩形（左上角在原点）。
RECT MakeRect(int w)
{
    RECT r = { 0, 0, w, kListHeight };
    return r;
}

// 造一条「自己发出的、开头 @ 了一个人、后面跟正文」的文字消息。
//   text：  气泡文本（含开头的 "@名字 "）。
//   atLen： 开头 @ 片段的长度（UTF-16 字符数，含 '@' 本身）。
// 片段的 uid / isMe 取值对本文件的断言无影响，给一组固定值即可。
ChatMessage MakeMentionMessage(const CString& text, int atLen)
{
    ChatMessage m = MakeTextMessage(true, text);
    m.clientMsgId = kClientMsgId;

    RenderSpan span;
    span.m_uid  = 10001;
    span.m_off  = 0;
    span.m_len  = atLen;
    span.m_isMe = false;
    m.ats.push_back(span);
    return m;
}

// 本文件各用例统一用的长文本：开头 "@张三 " 六个字符是 @ 片段，后面跟足够长的
// 正文，保证在 kListWidth 下必然折成多行 —— 折行后的行高与「未测量回退高度」拉开
// 明显差距，塌回与否一眼可辨。
CString MentionText()
{
    return _T("@张三 这条消息要足够长，长到在测试宽度下必须折行显示，")
           _T("这样一来它折行后的行高就会明显高于未测量时的单行回退高度。");
}

// 取列表第 idx 个子控件并转成 ChatBubble*（断言非空）。
ChatBubble* BubbleAt(ChatMessageList& list, size_t idx)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(idx < kids.size());
    ChatBubble* b = dynamic_cast<ChatBubble*>(kids[idx].get());
    REQUIRE(b != NULL);
    return b;
}

// 探测「一条从未被 Measure 过的文字气泡」的行高，也就是绘制时的单行回退高度。
// 用探测而不是把 kTextBubbleH 的数值抄进测试：产品端调整该常量时本测试不会失效。
int UnmeasuredTextRowHeight()
{
    ChatBubble b;
    b.SetMessage(MakeTextMessage(true, _T("短")));
    return b.GetRowHeight();
}

// 建一张 w×h 的 32 位离屏位图并铺成白底，供 B4 数「画上去的像素」。
//   memDC / bitmap / bits：[出参] 调用方负责用 DeleteDC / DeleteObject 释放。
// @return 建成功返回 true。
bool CreateWhiteCanvas(int w, int h, HDC& memDC, HBITMAP& bitmap, void*& bits)
{
    memDC  = NULL;
    bitmap = NULL;
    bits   = NULL;

    HDC screenDC = ::GetDC(NULL);
    if (screenDC == NULL)
    {
        return false;
    }
    memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return false;
    }

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下，扫描行顺序与直觉一致
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    bitmap = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bitmap == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        memDC = NULL;
        return false;
    }
    ::SelectObject(memDC, bitmap);

    // 铺白底。后面凡是不等于白的像素，就是被 DrawString 画上去的。
    RECT all = { 0, 0, w, h };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);
    return true;
}

// 把整个消息列表画到一张白底离屏位图上，数出其中「暗像素」的个数。
// 气泡底色（浅橙 / 浅灰）、行底色（暖白）、头像底色（橙）亮度都不低，只有正文
// 文字够暗，所以暗像素数量约等于「画出来的字有多少」。
//   list：已经 ForceLayout 过的消息列表。
//   w / h：画布尺寸，取列表矩形大小。
// @return 暗像素个数；画布或 DC 建不出来时返回 -1。
int PaintListAndCountDarkPixels(ChatMessageList& list, int w, int h)
{
    HDC     memDC  = NULL;
    HBITMAP bitmap = NULL;
    void*   bits   = NULL;
    if (!CreateWhiteCanvas(w, h, memDC, bitmap, bits))
    {
        return -1;
    }

    RECT all = { 0, 0, w, h };
    list.OnPaint(memDC, all);

    // 亮度阈值：低于它才算「文字像素」。取 110 —— 正文深灰（0x33 一档）远低于它，
    // 而气泡底、头像底、发送者名浅灰（0x8A）都高于它。
    const int kDarkLuma = 110;

    const unsigned int* px = (const unsigned int*)bits;
    int count = 0;
    for (int i = 0; i < w * h; ++i)
    {
        const int b = (int)( px[i]        & 0xFF);
        const int g = (int)((px[i] >>  8) & 0xFF);
        const int r = (int)((px[i] >> 16) & 0xFF);
        // 整数近似的 Rec.601 亮度，避免浮点。
        const int luma = (r * 299 + g * 587 + b * 114) / 1000;
        if (luma < kDarkLuma)
        {
            ++count;
        }
    }

    ::DeleteObject(bitmap);
    ::DeleteDC(memDC);
    return count;
}

// 数离屏位图里有多少个非白像素。
int CountNonWhitePixels(const void* bits, int w, int h)
{
    const unsigned int* px = (const unsigned int*)bits;
    int count = 0;
    for (int i = 0; i < w * h; ++i)
    {
        // 32 位 DIB 的高字节是未用的 alpha，比对时只看 RGB 三个通道。
        if ((px[i] & 0x00FFFFFFu) != 0x00FFFFFFu)
        {
            ++count;
        }
    }
    return count;
}

} // anonymous namespace

// =====================================================================
// B1 发送回执回填后，气泡必须仍然保持已测量的行高
//     改前：BackfillSentMessage → SetMessage → m_measuredWidth = -1，
//           而回填路径不重排，行高塌回单行回退高度 → 本用例红。
//     改后：回填只写 msgid / 时间戳，折行结果原样保留 → 绿。
// =====================================================================

TEST_CASE("B1 发送回执回填后气泡保持已测量的行高", "[mention][backfill]")
{
    ChatMessageList list;
    list.AppendMessage(MakeMentionMessage(MentionText(), 4));
    list.ForceLayout(MakeRect(kListWidth));

    const int measuredH = BubbleAt(list, 0)->GetRowHeight();

    // 前置条件：这条文本在测试宽度下确实折了行，行高明显高于单行回退高度。
    // 否则下面的断言即便通过也说明不了问题。
    REQUIRE(measuredH > UnmeasuredTextRowHeight());

    REQUIRE(list.BackfillSentMessage(kClientMsgId, kAckMsgid, kAckTimestampMs));

    // 回执只带回 msgid 与时间戳，既不改文本也不改字体，行高不该有任何变化。
    CHECK(BubbleAt(list, 0)->GetRowHeight() == measuredH);
}

// =====================================================================
// B2 发送回执回填不应让折行测量缓存失效
//     与 B1 是同一个缺陷的另一面：B1 看结果（行高塌了），B2 看原因（缓存被丢了）。
//     改前：回填后同宽重排会重新折行测量一次 → 本用例红（实际为 1）。
//     改后：缓存仍然命中 → 0。
// =====================================================================

TEST_CASE("B2 发送回执回填不使折行测量缓存失效", "[mention][backfill]")
{
    ChatMessageList list;
    list.AppendMessage(MakeMentionMessage(MentionText(), 4));
    list.ForceLayout(MakeRect(kListWidth));   // 首次测量，进缓存

    ChatBubble::ResetHeavyMeasureCount();
    REQUIRE(list.BackfillSentMessage(kClientMsgId, kAckMsgid, kAckTimestampMs));
    list.ForceLayout(MakeRect(kListWidth));   // 同宽重排

    CHECK(ChatBubble::HeavyMeasureCount() == 0ul);
}

// =====================================================================
// B3 回填本身仍须生效 —— 防止「为了不丢测量缓存」把回填一起改没了
// =====================================================================

TEST_CASE("B3 发送回执回填仍写入 msgid 与时间戳", "[mention][backfill]")
{
    ChatMessageList list;
    list.AppendMessage(MakeMentionMessage(MentionText(), 4));
    list.ForceLayout(MakeRect(kListWidth));

    // 回填前：本端刚发出，这两个字段都还是 0。
    REQUIRE(BubbleAt(list, 0)->GetMessage().msgid == 0);
    REQUIRE(BubbleAt(list, 0)->GetMessage().timestampMs == 0);

    REQUIRE(list.BackfillSentMessage(kClientMsgId, kAckMsgid, kAckTimestampMs));

    CHECK(BubbleAt(list, 0)->GetMessage().msgid == kAckMsgid);
    CHECK(BubbleAt(list, 0)->GetMessage().timestampMs == kAckTimestampMs);

    // 文本与 @ 片段必须原样保留 —— 回填不是重写内容。
    CHECK(BubbleAt(list, 0)->GetMessage().text == MentionText());
    CHECK(BubbleAt(list, 0)->GetMessage().ats.size() == 1u);
}

// =====================================================================
// B5 刚追加进列表的含 @ 气泡，第一次绘制就必须把文字画出来
//     这是对用户所见现象（发出去是空白，滚一下才显示）最直接的端到端断言：
//     追加之后不做任何额外重排，立刻绘制，气泡里必须有文字像素。
//     基线取一条文本为空的同型消息 —— 它画出来的暗像素只有头像 / 回执那些固定装饰，
//     用它扣掉装饰，剩下的才是「正文有没有画出来」。
// =====================================================================

TEST_CASE("B5 刚追加的含 @ 气泡第一次绘制就画得出文字", "[mention][paint]")
{
    // 基线：同样是自己发的文字气泡，但正文为空。
    ChatMessageList baseline;
    baseline.ForceLayout(MakeRect(kListWidth));
    baseline.AppendMessage(MakeTextMessage(true, _T("")));
    const int decorPixels = PaintListAndCountDarkPixels(baseline, kListWidth, kListHeight);
    REQUIRE(decorPixels >= 0);

    // 含 @ 的短消息（单行），与用户截图里那条同型。
    ChatMessageList list;
    list.ForceLayout(MakeRect(kListWidth));
    list.AppendMessage(MakeMentionMessage(_T("@张三 你好"), 3));
    const int mentionPixels = PaintListAndCountDarkPixels(list, kListWidth, kListHeight);
    REQUIRE(mentionPixels >= 0);

    INFO("装饰基线暗像素 = " << decorPixels << "，含 @ 气泡暗像素 = " << mentionPixels);
    CHECK(mentionPixels > decorPixels);
}

// =====================================================================
// B6 对照组：同样内容但不含 @ 的气泡第一次绘制也画得出文字
//     用来分辨「是含 @ 这条路专有的问题」还是「测量 / 绘制管线整体没通」。
// =====================================================================

TEST_CASE("B6 刚追加的纯文字气泡第一次绘制就画得出文字", "[mention][paint]")
{
    ChatMessageList baseline;
    baseline.ForceLayout(MakeRect(kListWidth));
    baseline.AppendMessage(MakeTextMessage(true, _T("")));
    const int decorPixels = PaintListAndCountDarkPixels(baseline, kListWidth, kListHeight);
    REQUIRE(decorPixels >= 0);

    ChatMessageList list;
    list.ForceLayout(MakeRect(kListWidth));
    list.AppendMessage(MakeTextMessage(true, _T("@张三 你好")));   // 同样文本，但不带 ats
    const int plainPixels = PaintListAndCountDarkPixels(list, kListWidth, kListHeight);
    REQUIRE(plainPixels >= 0);

    INFO("装饰基线暗像素 = " << decorPixels << "，纯文字气泡暗像素 = " << plainPixels);
    CHECK(plainPixels > decorPixels);
}

// =====================================================================
// B7 气泡被打回「未测量」而无人重排时，含 @ 的气泡也必须照样画得出文字
//
//     加固前这里是缺陷的复现点，也就是用户截图里的那一幕：SetMessage 是「整条内容
//     换了」的语义，它会丢弃折行测量结果；调用方若不跟一次重排，气泡就带着「未测量」
//     状态去绘制，退化到单行回退高度，文字矩形只剩 18px —— 含 @ 的走 GDI+，整行放
//     不下就一个字都不画（机制见 B4），实测文字像素从 209 直接掉到 0；纯文字走手写
//     排版、不按矩形高度裁剪，220 → 220 毫发无损。这条差异正是「为什么只有 @ 消息
//     空白」的答案。
//
//     加固做法：DrawMentionText 给 GDI+ 的排版矩形改用足够大的高度上界
//     （kMentionLayoutMaxHeight），高度不再参与截断，最坏只是文字画出气泡边界，
//     而不是静默地什么都不画。本用例守的就是这条：无论气泡是否已测量，含 @ 的文字
//     都得画得出来。
// =====================================================================

TEST_CASE("B7 未测量状态下含 @ 气泡仍画得出文字", "[mention][paint]")
{
    // ---- 含 @ 的气泡 ----
    ChatMessageList mentionList;
    mentionList.ForceLayout(MakeRect(kListWidth));
    mentionList.AppendMessage(MakeMentionMessage(_T("@张三 你好"), 3));

    const int mentionBefore =
        PaintListAndCountDarkPixels(mentionList, kListWidth, kListHeight);
    REQUIRE(mentionBefore > 0);   // 正常追加后画得出

    // 把整条消息原样写回去（旧的发送回执回填就是这么干的），不跟重排。
    {
        ChatBubble* b = BubbleAt(mentionList, 0);
        ChatMessage m = b->GetMessage();
        m.msgid = kAckMsgid;
        b->SetMessage(m);
    }
    const int mentionAfter =
        PaintListAndCountDarkPixels(mentionList, kListWidth, kListHeight);

    // ---- 同样文本但不含 @ 的对照气泡 ----
    ChatMessageList plainList;
    plainList.ForceLayout(MakeRect(kListWidth));
    plainList.AppendMessage(MakeTextMessage(true, _T("@张三 你好")));

    const int plainBefore =
        PaintListAndCountDarkPixels(plainList, kListWidth, kListHeight);
    REQUIRE(plainBefore > 0);

    {
        ChatBubble* b = BubbleAt(plainList, 0);
        ChatMessage m = b->GetMessage();
        m.msgid = kAckMsgid;
        b->SetMessage(m);
    }
    const int plainAfter =
        PaintListAndCountDarkPixels(plainList, kListWidth, kListHeight);

    INFO("含 @：写回前 " << mentionBefore << " → 写回后 " << mentionAfter
         << "；纯文字：写回前 " << plainBefore << " → 写回后 " << plainAfter);

    // 加固后：含 @ 的气泡即便处在「未测量」状态也照样把文字画出来（加固前这里是 0）。
    CHECK(mentionAfter > 0);
    // 纯文字这一路本来就不受影响，一个像素不少 —— 顺带确认加固没有波及它。
    CHECK(plainAfter == plainBefore);
}

// =====================================================================
// B4 机制实证：GDI+ 的排版矩形只要放不下一整行，整行一个像素都不画
//     这解释了「为什么行高塌回去之后是<u>全空白</u>而不是被截掉半行」，也解释了
//     为什么同样塌回去的纯文字气泡看不出问题（它走手写排版，不按矩形高度裁剪）。
//     本用例只验证 GDI+ 自身的行为，与产品代码改动无关，修复前后都应为绿。
// =====================================================================

TEST_CASE("B4 GDI+ 排版矩形放不下整行时一个像素都不画", "[mention][gdiplus]")
{
    // 与 ChatBubble 含 @ 气泡的绘制口径保持一致：微软雅黑、UnitPixel + em 尺寸、
    // GenericTypographic 且允许折行。
    const int   kFontPx    = 14;              // ChatBubble 的 kBubbleTextPx
    const int   kCanvasW   = 200;
    const int   kCanvasH   = 60;
    const WCHAR kSample[]  = L"测试文字";

    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startupInput;
    REQUIRE(Gdiplus::GdiplusStartup(&token, &startupInput, NULL) == Gdiplus::Ok);

    HDC     memDC  = NULL;
    HBITMAP bitmap = NULL;
    void*   bits   = NULL;
    REQUIRE(CreateWhiteCanvas(kCanvasW, kCanvasH, memDC, bitmap, bits));

    Gdiplus::FontFamily family(L"Microsoft YaHei");
    REQUIRE(family.IsAvailable());

    Gdiplus::Font font(&family, (Gdiplus::REAL)kFontPx,
                       Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    REQUIRE(font.GetLastStatus() == Gdiplus::Ok);

    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    fmt.SetFormatFlags(fmt.GetFormatFlags() & ~Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetTrimming(Gdiplus::StringTrimmingNone);
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

    Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));

    // 先量出这一行占多高。
    int lineH = 0;
    {
        Gdiplus::Graphics g(memDC);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        const Gdiplus::RectF loose(0.0f, 0.0f, (Gdiplus::REAL)kCanvasW, 10000.0f);
        Gdiplus::RectF       bound;
        REQUIRE(g.MeasureString(kSample, -1, &font, loose, &fmt, &bound) == Gdiplus::Ok);
        lineH = (int)(bound.Height + 0.9999f);
    }
    REQUIRE(lineH > 0);

    // 关键前置条件：气泡未测量时的文字矩形高度是 18px
    // （kTextBubbleH 36 - kTextPadV 9 × 2），本行文字比它高，放不下。
    const int kFallbackTextRectH = 18;
    REQUIRE(lineH > kFallbackTextRectH);

    // 一、按未测量回退的 18px 高度排版 —— 放不下一整行，GDI+ 一个像素都不画。
    {
        Gdiplus::Graphics g(memDC);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        const Gdiplus::RectF tooShort(0.0f, 0.0f,
                                      (Gdiplus::REAL)kCanvasW,
                                      (Gdiplus::REAL)kFallbackTextRectH);
        g.DrawString(kSample, -1, &font, tooShort, &fmt, &black);
    }
    CHECK(CountNonWhitePixels(bits, kCanvasW, kCanvasH) == 0);

    // 二、对照组：按实际行高排版，同样的文字就画出来了 —— 差别只在这几个像素的高度。
    {
        Gdiplus::Graphics g(memDC);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        const Gdiplus::RectF justFits(0.0f, 0.0f,
                                      (Gdiplus::REAL)kCanvasW,
                                      (Gdiplus::REAL)lineH);
        g.DrawString(kSample, -1, &font, justFits, &fmt, &black);
    }
    CHECK(CountNonWhitePixels(bits, kCanvasW, kCanvasH) > 0);

    ::DeleteObject(bitmap);
    ::DeleteDC(memDC);

    // 刻意<u>不</u>调 GdiplusShutdown。产品代码里 ChatBubble 的 EnsureGdiplusForBubble
    // 用一个静态 token 只初始化一次、此后一律早返回，它并不知道别人把 GDI+ 关了；
    // 本用例若在这里关掉，后面跑到的气泡绘制用例就会拿着"以为还活着"的 GDI+ 去画，
    // 直接访问违例（用例执行顺序是随机的，表现为偶发崩溃，极难定位）。
    // 测试进程整体退出时由系统回收即可，与真实客户端的做法一致 —— 那边也从不关。
    (void)token;
}
