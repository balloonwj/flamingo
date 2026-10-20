/**
 *  聊天气泡美化的用例（标签 [bubblestyle]），ChatBubbleStyleTests.cpp
 *
 *  背景见 docs/聊天气泡美化方案.md：同一人连续的消息合成一组、靠头像的一角收小、对方气泡加深、行距收紧与避头尾、
 *  提示行画成胶囊、图片与大表情不套气泡、引用块弱化、多选整行着色、@ 高亮与发送失败原因改色。本文件按方案第 4 节逐条覆盖：
 *    BS1 合组判定（纯函数 ChatBubblesGroupable）；
 *    BS2 列表里的合组：头像只画一次、同组两条气泡间距 4、遇到别人的消息或时间分隔行断开、删掉中间那条后重新合组；
 *    BS3 群聊里同一人连发两条，名字只画一次；
 *    BS4 每组第一条气泡靠头像的一角收小，组内后续气泡四角相同，本人与对方左右对调；
 *    BS5 对方气泡与聊天记录管理器的语音条底色为 #F0EEEA；
 *    BS6 带选项排版时逐行计算行高（纯文字行 22、含表情的行 28），不带选项时仍是每行 28；单行文字气泡高 38；
 *    BS7 / BS8 折行时句号、逗号不放在行首，左括号、左引号不留在行尾；
 *    BS9 时间分隔、系统提示、撤回提示画成胶囊，「编辑」为品牌橙，超长提示不超出行左右各 48 像素；
 *    BS10 图片与大表情不套气泡，图片圆角；
 *    BS11 引用块的竖线为 2 像素灰色、人名不再是品牌橙；
 *    BS12 多选模式下被勾选的行整行铺浅橙底；
 *    BS13 「@别人」的底色块为 #E6EEFC，发送失败的原因为红字；
 *    BS14 刚发出、还没有回执的消息不合组，回执带来服务端时间后与前一条合组（2026-10-15 实机检查发现回执到达后
 *         列表不重排，头像要等下一条消息到达才消失）。
 *
 *  消息列表挂在一个不显示的 DuiHost 上排版、画进内存位图（照 ChatCardStyleTests.cpp 的做法），按 ChatBubble::BubbleRectInHost
 *  找气泡、逐点取样颜色。时间分隔行的「现在」经 ChatMessageList::SetNowProviderForTest 固定。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // 消息列表挂载的宿主窗口
#include "../Common/I18n/I18n.h"                                 // i18n::UiFontName：测量用的界面字体
#include "../UI/BalloonChat/BalloonChatMsgFormat.h"              // balloonchat::ParseSendBody
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatBubbleGroup.h"
#include "../UI/BalloonChatWnd/ChatBubbleStyle.h"
#include "../UI/BalloonChatWnd/ChatInlineText.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"                // ApplyVoiceMessage
#include "../UI/BalloonChatWnd/ChatVoiceStrip.h"
#include "../UI/BalloonChatWnd/GroupMentionModel.h"              // RenderSpan：@ 片段
#include "../UI/BalloonChatWnd/RecallEdit.h"                     // RecallEditNowMs：撤回后「编辑」的有效期按真实时间算

using namespace balloonwjui;
using namespace balloonchatwnd;
namespace bs = balloonchatwnd::bubblestyle;

namespace {

// ---- 画布与列表 ----
const int kBsListW = 760;                        // 消息列表宽（像素），常见聊天窗宽度
const int kBsListH = 600;                        // 消息列表高（像素）
const WORD kBsBitsPerPixel = 32;                 // 32 位 DIB 的位深
const COLORREF kBsListBg = RGB(0xFA, 0xF9, 0xF6);   // 消息列表底色（ChatMessageList 铺的暖白底）
const int kBsColorTolerance = 2;                 // 取样颜色时允许的每个分量的误差

// ---- 时间 ----
const long long kBsBaseMs = 1791950400000LL;     // 各条消息时间的起点（Unix 毫秒），其余消息从它往后推
const long long kBsSecondMs = 1000LL;            // 1 秒（毫秒）
const long long kBsMinuteMs = 60LL * 1000LL;     // 1 分钟（毫秒）
const long long kBsHourMs = 60LL * kBsMinuteMs;  // 1 小时（毫秒）
const long long kBsLongGapMinutes = 10;          // 「隔了很久」的分钟数：超过时间分隔线的 5 分钟，中间会插一行时间

// ---- 发送者 ----
const int64_t kBsUidLi = 1003;                   // 群成员李晓雯的 userid
const int64_t kBsUidWang = 1004;                 // 群成员王建国的 userid
const COLORREF kBsAvatarColor = RGB(0x12, 0x9E, 0x5B);   // 对方字形头像的底色：列表里别处不会出现这个颜色，用来判断画没画头像

// ---- 气泡几何（与 ChatBubble.cpp 的同名常量一致） ----
const int kBsRowVMargin = 7;                     // 不在组内时行的上下外边距（kRowVMargin）
const int kBsSenderNameH = 17;                   // 群聊发送者名一行的高（kSenderNameHeight）
const int kBsTextPadH = 12;                      // 文字气泡左右内边距（kTextPadH）
const int kBsGroupedGap = 4;                     // 同组两条气泡之间的间距（两侧各 kGroupedHalfGap）
const int kBsCornerProbe = 2;                    // 取气泡角上颜色时离气泡上边、左右边的距离（像素）

// ---- 排版 ----
const int kBsFontPx = 14;                        // 正文字号（像素）
const int kBsExpectedLineH = 22;                 // 14 像素正文没有表情的行的行高（方案第 1 节第 4 条）
const int kBsFacePx = 28;                        // 内联表情格边长（ChatFaceTag.h 的 kFaceDisplayPx）
const int kBsWideW = 400;                        // 不会折行的排版宽度（像素）
const int kBsTipFontPx = 11;                     // 提示行文字的字号（ChatBubble.cpp 的 kTimeTextPx）
const int kBsPillProbe = 3;                      // 取胶囊内外颜色时离胶囊左边的距离（像素）
const int kBsLongTipChars = 100;                 // 超长系统提示的字数：远超一行能放下的宽度

// ---- 图片与大表情 ----
const int kBsImageW = 240;                       // 图片的显示宽（像素）
const int kBsImageH = 160;                       // 图片的显示高（像素）
const COLORREF kBsImagePlaceholder = RGB(0xEC, 0xEA, 0xE6);   // 图片还没解码时的占位底色（ChatBubble.cpp）
const int kBsOutsideImage = 3;                   // 取图片外侧颜色时离图片左边的距离（像素）
const int kBsEmojiProbe = 3;                     // 取大表情区域角上颜色时离边的距离（像素）

// ---- 引用块 ----
const int kBsQuoteProbeY = 10;                   // 取竖线颜色时离引用块上边的距离（像素）
const int kBsQuoteNameH = 17;                    // 引用块人名一行的高（ChatBubble.cpp 的 kQuoteLineH）
const int kBsQuoteTextLeft = 10;                 // 引用块文字离竖线左边的距离（竖线宽 + 间距）
const int kBsDarkLumMax = 160;                   // 人名应当画出的深色像素的亮度上限（0~255）

// ---- 多选 ----
const int kBsRowRightProbe = 20;                 // 取行底色时离行右边的距离（像素），落在对方气泡右侧的空白里

// ---- @ 高亮 ----
const int kBsAtLen = 4;                          // 「@王建国」在正文里占的字符数

// ---- 发送失败 ----
const int kBsReasonBandTop = 2;                  // 原因行离气泡下边的距离（ChatBubble.cpp 的 kReceiptTopGap）
const int kBsReasonBandH = 18;                   // 原因行的高（kFailReasonHeight）
const int kBsReasonBandLeft = 300;               // 在气泡左侧多大范围内找原因文字（像素）：原因可能比气泡宽

// ---- 回执回填 ----
const char* const kBsEchoId1 = "BS14-ECHO-1";    // 第一条回显的客户端消息 ID
const char* const kBsEchoId2 = "BS14-ECHO-2";    // 第二条回显的客户端消息 ID
const int kBsFillerCount = 20;                   // 前面垫的对方消息条数：让内容超出可视区，列表需要滚动
const long long kBsFillerFirstMsgid = 100;       // 垫的对方消息从这个 msgid 起编号，与用例里的其他 msgid 不重
const long long kBsEchoAck1Seconds = 30;         // 第一条回执的服务端时间比前一条晚多少秒
const long long kBsEchoAck2Seconds = 40;         // 第二条回执的服务端时间比前一条晚多少秒

// 两个颜色每个分量都在误差以内
bool BsNear(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= kBsColorTolerance
        && abs((int)GetGValue(a) - (int)GetGValue(b)) <= kBsColorTolerance
        && abs((int)GetBValue(a) - (int)GetBValue(b)) <= kBsColorTolerance;
}

// 颜色的亮度（0~255，按 (R*299 + G*587 + B*114) / 1000 计）
int BsLum(COLORREF c)
{
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000;
}

// 是否像品牌橙（#F26A33 / #FF7A45 一类，含 ClearType 抗锯齿后的过渡色）：红高、蓝低、红比蓝高出很多
bool BsOrangeish(COLORREF c)
{
    const int kMinRed = 200;        // 红分量下限
    const int kMaxGreen = 170;      // 绿分量上限
    const int kRedOverBlue = 110;   // 红比蓝至少高出多少
    return GetRValue(c) >= kMinRed && GetGValue(c) <= kMaxGreen
        && (int)GetRValue(c) - (int)GetBValue(c) >= kRedOverBlue;
}

// 是否像红字（#D93C3C 一类）：红高、绿蓝都低
bool BsReddish(COLORREF c)
{
    const int kMinRed = 170;        // 红分量下限
    const int kMaxGreenBlue = 120;  // 绿、蓝分量上限
    return GetRValue(c) >= kMinRed && GetGValue(c) <= kMaxGreenBlue && GetBValue(c) <= kMaxGreenBlue;
}

// 在作用域内初始化 GDI+（表情、圆角与图片都用 GDI+ 画）
class ScopedBsGdiplus
{
public:
    ScopedBsGdiplus()
        : m_token(0), m_ok(false)
    {
        Gdiplus::GdiplusStartupInput in;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &in, NULL) == Gdiplus::Ok);
    }

    ~ScopedBsGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    // 是否初始化成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌
    bool      m_ok;      // 是否初始化成功
};

// 时间分隔行用的「现在」：起点之后一小时
long long BsFakeNow()
{
    return kBsBaseMs + kBsHourMs;
}

// 在作用域内把消息列表的「现在」固定为 BsFakeNow，析构时恢复
class ScopedBsClock
{
public:
    ScopedBsClock()
    {
        ChatMessageList::SetNowProviderForTest(&BsFakeNow);
    }

    ~ScopedBsClock()
    {
        ChatMessageList::SetNowProviderForTest(NULL);
    }
};

// 隐藏的宿主窗口，析构时销毁
class BsHost
{
public:
    BsHost()
    {
        RECT rc = { 0, 0, kBsListW, kBsListH };
        m_host.Create(NULL, rc, _T("ChatBubbleStyleTests"), WS_POPUP);
    }

    ~BsHost()
    {
        if (m_host.IsWindow())
            m_host.DestroyWindow();
    }

    // 宿主对象（本类持有，调用方借用）
    DuiHost& Host()
    {
        return m_host;
    }

private:
    DuiHost m_host;   // 宿主对象
};

// 与消息列表同尺寸的离屏画布
class BsCanvas
{
public:
    BsCanvas()
        : m_dc(NULL), m_bmp(NULL), m_old(NULL), m_bits(NULL)
    {
        HDC screen = ::GetDC(NULL);
        m_dc = ::CreateCompatibleDC(screen);
        ::ReleaseDC(NULL, screen);
        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kBsListW;
        bi.bmiHeader.biHeight = -kBsListH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = kBsBitsPerPixel;
        bi.bmiHeader.biCompression = BI_RGB;
        m_bmp = ::CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &m_bits, NULL, 0);
        m_old = ::SelectObject(m_dc, m_bmp);
        RECT all = { 0, 0, kBsListW, kBsListH };
        HBRUSH bg = ::CreateSolidBrush(kBsListBg);
        ::FillRect(m_dc, &all, bg);
        ::DeleteObject(bg);
    }

    ~BsCanvas()
    {
        ::SelectObject(m_dc, m_old);
        ::DeleteObject(m_bmp);
        ::DeleteDC(m_dc);
    }

    // 画布是否建成
    bool Ok() const
    {
        return m_dc != NULL && m_bmp != NULL && m_bits != NULL;
    }

    // 画布的设备上下文（由本类持有）
    HDC Dc() const
    {
        return m_dc;
    }

    // 把列表画上来
    void Paint(ChatMessageList& list)
    {
        RECT all = { 0, 0, kBsListW, kBsListH };
        list.OnPaint(m_dc, all);
        ::GdiFlush();
    }

    // 取一个像素的颜色；越界时返回列表底色
    COLORREF At(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= kBsListW || y >= kBsListH)
            return kBsListBg;
        ::GdiFlush();
        const unsigned int px = ((const unsigned int*)m_bits)[y * kBsListW + x];
        return RGB((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF);
    }

    // 在 area 里找某个颜色的像素的外接矩形；找不到时返回空矩形
    RECT BoundsOf(COLORREF color, const RECT& area) const
    {
        RECT box = { 0, 0, 0, 0 };
        bool found = false;
        for (int y = area.top; y < area.bottom; ++y)
        {
            for (int x = area.left; x < area.right; ++x)
            {
                if (At(x, y) != color)
                    continue;
                if (!found)
                {
                    ::SetRect(&box, x, y, x + 1, y + 1);
                    found = true;
                    continue;
                }
                box.left = (x < box.left) ? x : box.left;
                box.top = (y < box.top) ? y : box.top;
                box.right = (x + 1 > box.right) ? x + 1 : box.right;
                box.bottom = (y + 1 > box.bottom) ? y + 1 : box.bottom;
            }
        }
        return box;
    }

    // area 里满足 pred 的像素个数
    int Count(bool (*pred)(COLORREF), const RECT& area) const
    {
        int n = 0;
        for (int y = area.top; y < area.bottom; ++y)
        {
            for (int x = area.left; x < area.right; ++x)
            {
                if (pred(At(x, y)))
                    ++n;
            }
        }
        return n;
    }

    // area 里最暗像素的亮度
    int DarkestLum(const RECT& area) const
    {
        int darkest = 255;
        for (int y = area.top; y < area.bottom; ++y)
        {
            for (int x = area.left; x < area.right; ++x)
            {
                const int lum = BsLum(At(x, y));
                if (lum < darkest)
                    darkest = lum;
            }
        }
        return darkest;
    }

private:
    HDC     m_dc;     // 内存设备上下文
    HBITMAP m_bmp;    // DIB 位图
    HGDIOBJ m_old;    // 选入位图前的原位图
    void*   m_bits;   // 像素数据（BGRA，自上而下）
};

// 选好界面字体的内存设备上下文，供直接调用排版函数与量字宽
class BsFontDc
{
public:
    // px：字号（像素）
    explicit BsFontDc(int px)
        : m_dc(NULL), m_font(NULL), m_old(NULL)
    {
        m_dc = ::CreateCompatibleDC(NULL);
        m_font = ::CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
        if (m_dc != NULL && m_font != NULL)
            m_old = ::SelectObject(m_dc, m_font);
    }

    ~BsFontDc()
    {
        if (m_old != NULL)
            ::SelectObject(m_dc, m_old);
        if (m_font != NULL)
            ::DeleteObject(m_font);
        if (m_dc != NULL)
            ::DeleteDC(m_dc);
    }

    // 是否建成
    bool Ok() const
    {
        return m_dc != NULL && m_font != NULL;
    }

    // 设备上下文（由本类持有）
    HDC Get() const
    {
        return m_dc;
    }

    // 逐字量出的宽度之和（与排版时中文逐字测量的口径相同）
    int CharsWidth(const wchar_t* s) const
    {
        int w = 0;
        for (const wchar_t* p = s; *p != 0; ++p)
        {
            SIZE sz = { 0, 0 };
            ::GetTextExtentPoint32W(m_dc, p, 1, &sz);
            w += sz.cx;
        }
        return w;
    }

    // 整段文字的宽度
    int TextWidth(const CString& s) const
    {
        SIZE sz = { 0, 0 };
        ::GetTextExtentPoint32W(m_dc, (LPCWSTR)s, s.GetLength(), &sz);
        return sz.cx;
    }

private:
    HDC     m_dc;     // 内存设备上下文
    HFONT   m_font;   // 选入的字体
    HGDIOBJ m_old;    // 选入字体前的原字体
};

// 造一条文字消息
ChatMessage BsText(bool fromMe, long long msgid, long long tsMs, const wchar_t* text)
{
    ChatMessage m = MakeTextMessage(fromMe, text);
    m.msgid = msgid;
    m.timestampMs = tsMs;
    return m;
}

// 单聊里对方发来的文字消息：字形头像用 kBsAvatarColor，便于判断画没画头像
ChatMessage BsPeerText(long long msgid, long long tsMs, const wchar_t* text)
{
    ChatMessage m = BsText(false, msgid, tsMs, text);
    m.senderAvatarText = L"韩";
    m.senderAvatarColor = kBsAvatarColor;
    return m;
}

// 群聊里某位成员发来的文字消息
ChatMessage BsGroupText(long long msgid, long long tsMs, int64_t uid, const wchar_t* name, const wchar_t* text)
{
    ChatMessage m = BsText(false, msgid, tsMs, text);
    m.senderUserId = uid;
    m.senderName = name;
    m.senderAvatarText = CString(name).Left(1);
    m.senderAvatarColor = kBsAvatarColor;
    return m;
}

// 按 msgid 找一行气泡（提示行也算，撤回后的行就是提示行）；找不到返回 NULL
ChatBubble* BsRow(ChatMessageList& list, long long msgid)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().msgid == msgid)
            return b;
    }
    return NULL;
}

// 按客户端消息 ID 找本人刚发出的那一行（回执到达前 msgid 为 0，不能按 msgid 找）；找不到返回 NULL
ChatBubble* BsEchoRow(ChatMessageList& list, const char* clientMsgId)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().clientMsgId == clientMsgId)
            return b;
    }
    return NULL;
}

// 列表里第一行时间分隔行（自动插入的，msgid 为 0）；没有时返回 NULL
ChatBubble* BsFirstSeparator(ChatMessageList& list)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().kind == kMsgTime && b->GetMessage().msgid <= 0)
            return b;
    }
    return NULL;
}

// 把列表挂到宿主上、按列表尺寸排版
void BsAttach(BsHost& host, ChatMessageList& list)
{
    list.AttachToHost(&host.Host());
    RECT rc = { 0, 0, kBsListW, kBsListH };
    list.ForceLayout(rc);
}

// 气泡上边中点往下 kBsCornerProbe 处的颜色：上内边距里，就是气泡底色
COLORREF BsBubbleFill(const BsCanvas& canvas, const RECT& bubble)
{
    return canvas.At((bubble.left + bubble.right) / 2, bubble.top + kBsCornerProbe);
}

// 一行排版结果的首尾：按纵坐标把放置项分行，记下每行第一项与最后一项的文字
struct BsLineEdge
{
    CString m_first;   // 本行第一个放置项的文字（表情为空串）
    CString m_last;    // 本行最后一个放置项的文字
};

// 把放置项按行拆开，返回每行的首尾文字（按从上到下的顺序）
std::vector<BsLineEdge> BsLines(const std::vector<InlinePlaced>& placed)
{
    std::vector<BsLineEdge> lines;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        if (i == 0 || placed[i].rc.top != placed[i - 1].rc.top)
        {
            BsLineEdge e;
            e.m_first = placed[i].text;
            lines.push_back(e);
        }
        lines.back().m_last = placed[i].text;
    }
    return lines;
}

// 检查一行提示行画成了胶囊：文字左边胶囊以内是胶囊底色，胶囊以外是列表底色
void BsCheckPill(const BsCanvas& canvas, ChatBubble* row)
{
    REQUIRE(row != NULL);
    BsFontDc dc(kBsTipFontPx);
    REQUIRE(dc.Ok());
    const RECT rc = row->GetRect();
    const int textW = dc.TextWidth(row->GetMessage().text);
    const int pillLeft = (rc.left + rc.right) / 2 - textW / 2 - bs::kTipPillPadH;
    const int midY = (rc.top + rc.bottom) / 2;
    CAPTURE((LPCSTR)CW2A(row->GetMessage().text, CP_UTF8));
    CHECK(BsNear(canvas.At(pillLeft + kBsPillProbe, midY), bs::kTipPillFill));
    CHECK(BsNear(canvas.At(pillLeft - kBsPillProbe, midY), kBsListBg));
}

} // namespace

TEST_CASE("BS1 合组判定：同一人 3 分钟内合组，其余情况断开", "[bubblestyle]")
{
    //单聊里对方的两条：发送者 ID 与名字都为空，视为同一人
    const ChatMessage a = BsText(false, 1, kBsBaseMs, L"第一条");
    ChatMessage b = BsText(false, 2, kBsBaseMs + kBsMinuteMs, L"第二条");
    CHECK(ChatBubblesGroupable(a, b));

    //正好 3 分钟合组，多 1 毫秒不合组
    b.timestampMs = a.timestampMs + bs::kGroupMaxGapMs;
    CHECK(ChatBubblesGroupable(a, b));
    b.timestampMs = a.timestampMs + bs::kGroupMaxGapMs + 1;
    CHECK_FALSE(ChatBubblesGroupable(a, b));

    //时间倒序、缺时间戳不合组
    b.timestampMs = a.timestampMs - kBsSecondMs;
    CHECK_FALSE(ChatBubblesGroupable(a, b));
    b.timestampMs = 0;
    CHECK_FALSE(ChatBubblesGroupable(a, b));

    //本人与对方不合组，本人的两条合组
    const ChatMessage mine1 = BsText(true, 3, kBsBaseMs, L"我说一句");
    const ChatMessage mine2 = BsText(true, 4, kBsBaseMs + kBsSecondMs, L"再说一句");
    CHECK_FALSE(ChatBubblesGroupable(a, mine1));
    CHECK_FALSE(ChatBubblesGroupable(mine1, a));
    CHECK(ChatBubblesGroupable(mine1, mine2));

    //群里同一成员合组、两位不同成员不合组；userid 都为 0 时按名字比
    const ChatMessage g1 = BsGroupText(5, kBsBaseMs, kBsUidLi, L"李晓雯", L"一");
    const ChatMessage g2 = BsGroupText(6, kBsBaseMs + kBsSecondMs, kBsUidLi, L"李晓雯", L"二");
    const ChatMessage g3 = BsGroupText(7, kBsBaseMs + kBsSecondMs, kBsUidWang, L"王建国", L"三");
    CHECK(ChatBubblesGroupable(g1, g2));
    CHECK_FALSE(ChatBubblesGroupable(g2, g3));
    ChatMessage n1 = g1;
    ChatMessage n2 = g2;
    n1.senderUserId = 0;
    n2.senderUserId = 0;
    CHECK(ChatBubblesGroupable(n1, n2));
    n2.senderName = L"王建国";
    CHECK_FALSE(ChatBubblesGroupable(n1, n2));

    //提示行（时间分隔、系统提示、撤回提示）不参与合组
    ChatMessage tip = BsText(false, 0, kBsBaseMs + kBsSecondMs, L"「王建国」加入了群聊");
    tip.kind = kMsgTime;
    CHECK_FALSE(ChatBubblesGroupable(a, tip));
    CHECK_FALSE(ChatBubblesGroupable(tip, b));

    //卡片与文字之间照常合组
    ChatMessage card = BsText(false, 8, kBsBaseMs + kBsSecondMs, L"");
    card.kind = kMsgFile;
    CHECK(ChatBubblesGroupable(a, card));
}

TEST_CASE("BS2 列表里的合组：头像只画一次、间距收窄、断开与重新合组", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    list.AppendMessage(BsPeerText(1, kBsBaseMs, L"第一条"));
    list.AppendMessage(BsPeerText(2, kBsBaseMs + 30 * kBsSecondMs, L"第二条"));
    list.AppendMessage(BsPeerText(3, kBsBaseMs + 60 * kBsSecondMs, L"第三条"));
    list.AppendMessage(BsText(true, 4, kBsBaseMs + 90 * kBsSecondMs, L"我插一句"));
    list.AppendMessage(BsPeerText(5, kBsBaseMs + 120 * kBsSecondMs, L"第五条"));
    list.AppendMessage(BsPeerText(6, kBsBaseMs + kBsLongGapMinutes * kBsMinuteMs, L"隔了很久"));
    BsAttach(host, list);

    ChatBubble* b1 = BsRow(list, 1);
    ChatBubble* b2 = BsRow(list, 2);
    ChatBubble* b3 = BsRow(list, 3);
    ChatBubble* b4 = BsRow(list, 4);
    ChatBubble* b5 = BsRow(list, 5);
    ChatBubble* b6 = BsRow(list, 6);
    REQUIRE(b1 != NULL);
    REQUIRE(b2 != NULL);
    REQUIRE(b3 != NULL);
    REQUIRE(b4 != NULL);
    REQUIRE(b5 != NULL);
    REQUIRE(b6 != NULL);

    //前三条合成一组；本人那条打断；隔了 10 分钟的那条前面有时间分隔行，也打断
    CHECK_FALSE(b1->IsGroupedWithPrev());
    CHECK(b1->IsGroupedWithNext());
    CHECK(b2->IsGroupedWithPrev());
    CHECK(b2->IsGroupedWithNext());
    CHECK(b3->IsGroupedWithPrev());
    CHECK_FALSE(b3->IsGroupedWithNext());
    CHECK_FALSE(b4->IsGroupedWithPrev());
    CHECK_FALSE(b4->IsGroupedWithNext());
    CHECK_FALSE(b5->IsGroupedWithPrev());
    CHECK_FALSE(b5->IsGroupedWithNext());
    CHECK_FALSE(b6->IsGroupedWithPrev());

    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    //合组的消息不画头像：第一条那一行有头像底色，第二条那一行没有
    const RECT row1 = b1->GetRect();
    const RECT row2 = b2->GetRect();
    const RECT av1 = canvas.BoundsOf(kBsAvatarColor, row1);
    const RECT av2 = canvas.BoundsOf(kBsAvatarColor, row2);
    CHECK_FALSE(::IsRectEmpty(&av1));
    CHECK(::IsRectEmpty(&av2));

    //同组两条气泡之间的间距为 4
    const RECT r1 = b1->BubbleRectInHost();
    const RECT r2 = b2->BubbleRectInHost();
    REQUIRE_FALSE(::IsRectEmpty(&r1));
    REQUIRE_FALSE(::IsRectEmpty(&r2));
    CHECK(r2.top - r1.bottom == kBsGroupedGap);

    //删掉中间那条本人的消息后，第三条（60 秒）与第五条（120 秒）重新合组
    std::vector<long long> removed(1, 4);
    list.RemoveMessagesByMsgid(removed);
    RECT rc = { 0, 0, kBsListW, kBsListH };
    list.ForceLayout(rc);
    b3 = BsRow(list, 3);
    b5 = BsRow(list, 5);
    REQUIRE(b3 != NULL);
    REQUIRE(b5 != NULL);
    CHECK(b3->IsGroupedWithNext());
    CHECK(b5->IsGroupedWithPrev());
}

TEST_CASE("BS3 群聊里同一人连发两条，名字只画一次", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    list.AppendMessage(BsGroupText(11, kBsBaseMs, kBsUidLi, L"李晓雯", L"设计稿今晚更新"));
    list.AppendMessage(BsGroupText(12, kBsBaseMs + 20 * kBsSecondMs, kBsUidLi, L"李晓雯", L"放在共享盘里"));
    BsAttach(host, list);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    ChatBubble* b1 = BsRow(list, 11);
    ChatBubble* b2 = BsRow(list, 12);
    REQUIRE(b1 != NULL);
    REQUIRE(b2 != NULL);
    const RECT r1 = b1->BubbleRectInHost();
    const RECT r2 = b2->BubbleRectInHost();
    REQUIRE_FALSE(::IsRectEmpty(&r1));
    REQUIRE_FALSE(::IsRectEmpty(&r2));

    //第一条：行顶外边距 7 + 名字一行 17；第二条：只有同组那一侧的 2，没有名字
    CHECK(r1.top - b1->GetRect().top == kBsRowVMargin + kBsSenderNameH);
    CHECK(r2.top - b2->GetRect().top == bs::kGroupedHalfGap);
}

TEST_CASE("BS4 每组第一条气泡靠头像的一角收小", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    list.AppendMessage(BsPeerText(21, kBsBaseMs, L"对方第一条"));
    list.AppendMessage(BsPeerText(22, kBsBaseMs + 10 * kBsSecondMs, L"对方第二条"));
    list.AppendMessage(BsText(true, 23, kBsBaseMs + 20 * kBsSecondMs, L"本人第一条"));
    list.AppendMessage(BsText(true, 24, kBsBaseMs + 30 * kBsSecondMs, L"本人第二条"));
    BsAttach(host, list);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    ChatBubble* p1 = BsRow(list, 21);
    ChatBubble* p2 = BsRow(list, 22);
    ChatBubble* m1 = BsRow(list, 23);
    ChatBubble* m2 = BsRow(list, 24);
    REQUIRE(p1 != NULL);
    REQUIRE(p2 != NULL);
    REQUIRE(m1 != NULL);
    REQUIRE(m2 != NULL);

    //对方第一条：左上角圆角为 4，离角 2 像素处已是气泡色；右上角仍是 12，同一位置是底色
    {
        const RECT r = p1->BubbleRectInHost();
        REQUIRE_FALSE(::IsRectEmpty(&r));
        const COLORREF fill = BsBubbleFill(canvas, r);
        CHECK(BsNear(canvas.At(r.left + kBsCornerProbe, r.top + kBsCornerProbe), fill));
        CHECK_FALSE(BsNear(canvas.At(r.right - 1 - kBsCornerProbe, r.top + kBsCornerProbe), fill));
    }
    //对方第二条（组内）：四角都是 12
    {
        const RECT r = p2->BubbleRectInHost();
        REQUIRE_FALSE(::IsRectEmpty(&r));
        const COLORREF fill = BsBubbleFill(canvas, r);
        CHECK_FALSE(BsNear(canvas.At(r.left + kBsCornerProbe, r.top + kBsCornerProbe), fill));
    }
    //本人第一条：左右对调，右上角收小
    {
        const RECT r = m1->BubbleRectInHost();
        REQUIRE_FALSE(::IsRectEmpty(&r));
        const COLORREF fill = BsBubbleFill(canvas, r);
        CHECK(BsNear(canvas.At(r.right - 1 - kBsCornerProbe, r.top + kBsCornerProbe), fill));
        CHECK_FALSE(BsNear(canvas.At(r.left + kBsCornerProbe, r.top + kBsCornerProbe), fill));
    }
    //本人第二条（组内）：右上角也是 12
    {
        const RECT r = m2->BubbleRectInHost();
        REQUIRE_FALSE(::IsRectEmpty(&r));
        const COLORREF fill = BsBubbleFill(canvas, r);
        CHECK_FALSE(BsNear(canvas.At(r.right - 1 - kBsCornerProbe, r.top + kBsCornerProbe), fill));
    }
}

TEST_CASE("BS5 对方气泡与聊天记录管理器的语音条底色为 #F0EEEA", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    //聊天窗里对方的文字气泡
    {
        ChatMessageList list;
        list.AppendMessage(BsPeerText(31, kBsBaseMs, L"好的，辛苦了"));
        BsAttach(host, list);
        BsCanvas canvas;
        REQUIRE(canvas.Ok());
        canvas.Paint(list);
        ChatBubble* b = BsRow(list, 31);
        REQUIRE(b != NULL);
        const RECT r = b->BubbleRectInHost();
        REQUIRE_FALSE(::IsRectEmpty(&r));
        CHECK(BsNear(BsBubbleFill(canvas, r), bs::kPeerBubbleFill));
    }

    //聊天记录管理器里的语音条：与对方气泡同色
    {
        const int kVoiceSeconds = 8;            // 语音时长（秒）
        const long long kVoiceBytes = 12800;    // 语音文件字节数
        const int kStripLeft = 20;              // 语音条控件在画布上的左上角
        const int kStripTop = 20;
        const int kStripH = 38;                 // 语音条控件的高（像素）
        ChatMessage v;
        v.msgid = 32;
        ApplyVoiceMessage(v, "bs-voice-fileid", "bs-voice-md5", kVoiceSeconds, kVoiceBytes, std::string(), false);
        ChatVoiceStrip strip;
        strip.SetVoiceMessage(v, false);
        const RECT rc = { kStripLeft, kStripTop, kStripLeft + VoiceStripWidth(v), kStripTop + kStripH };
        strip.ForceLayout(rc);
        BsCanvas canvas;
        REQUIRE(canvas.Ok());
        strip.OnPaint(canvas.Dc(), rc);
        ::GdiFlush();
        const RECT bar = VoiceStripBarRect(rc, v);
        CHECK(BsNear(canvas.At((bar.left + bar.right) / 2, bar.top + kBsCornerProbe), bs::kPeerBubbleFill));
    }
}

TEST_CASE("BS6 行高：带选项时逐行计算，单行文字气泡高 38", "[bubblestyle]")
{
    BsFontDc dc(kBsFontPx);
    REQUIRE(dc.Ok());
    const int lineH = bs::TextLineHeight(kBsFontPx);
    CHECK(lineH == kBsExpectedLineH);

    InlineLayoutOptions opts;
    opts.m_textLineH = lineH;
    opts.m_kinsoku = true;

    //两行纯文字：每行 22
    {
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(L"第一行\n第二行");
        std::vector<InlinePlaced> placed;
        const SIZE sz = LayoutInlineContentEx(dc.Get(), segs, kBsWideW, kBsFacePx, opts, &placed);
        CHECK(sz.cy == lineH * 2);
        //不带选项时（消息记录窗等其余调用方）仍是每行 28
        std::vector<InlinePlaced> old;
        const SIZE oldSz = LayoutInlineContent(dc.Get(), segs, kBsWideW, kBsFacePx, &old);
        CHECK(oldSz.cy == kBsFacePx * 2);
    }
    //第一行带一个表情：这一行按表情格撑到 28，第二行仍是 22
    {
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(L"第一行/f[\"5\"]\n第二行");
        std::vector<InlinePlaced> placed;
        const SIZE sz = LayoutInlineContentEx(dc.Get(), segs, kBsWideW, kBsFacePx, opts, &placed);
        CHECK(sz.cy == kBsFacePx + lineH);
        REQUIRE_FALSE(placed.empty());
        CHECK(placed.back().rc.top == kBsFacePx);
        CHECK(placed.back().rc.bottom == kBsFacePx + lineH);
    }

    //聊天窗里的单行文字气泡：22 + 上下内边距 8
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    list.AppendMessage(BsPeerText(41, kBsBaseMs, L"收到"));
    BsAttach(host, list);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);
    ChatBubble* b = BsRow(list, 41);
    REQUIRE(b != NULL);
    const RECT r = b->BubbleRectInHost();
    CHECK(r.bottom - r.top == lineH + bs::kTextPadV * 2);
}

TEST_CASE("BS7 折行时句号、逗号不放在行首", "[bubblestyle]")
{
    BsFontDc dc(kBsFontPx);
    REQUIRE(dc.Ok());
    InlineLayoutOptions opts;
    opts.m_textLineH = bs::TextLineHeight(kBsFontPx);
    opts.m_kinsoku = true;
    //可用宽度正好放下五个字：按原规则，第六个位置上的标点会被挤到第二行行首
    const int maxW = dc.CharsWidth(L"一二三四五");

    const wchar_t* const kSamples[] = { L"一二三四五。六七", L"一二三四五，六七", L"一二三四五,六七" };
    for (size_t i = 0; i < sizeof(kSamples) / sizeof(kSamples[0]); ++i)
    {
        CAPTURE(i);
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(kSamples[i]);
        //原规则确实会把标点放到行首，用例构造有效
        std::vector<InlinePlaced> old;
        LayoutInlineContent(dc.Get(), segs, maxW, kBsFacePx, &old);
        const std::vector<BsLineEdge> oldLines = BsLines(old);
        REQUIRE(oldLines.size() >= 2);
        REQUIRE(oldLines[1].m_first != L"五");

        //避头：标点连同前一个字「五」一起换到第二行
        std::vector<InlinePlaced> placed;
        LayoutInlineContentEx(dc.Get(), segs, maxW, kBsFacePx, opts, &placed);
        const std::vector<BsLineEdge> lines = BsLines(placed);
        REQUIRE(lines.size() >= 2);
        CHECK(lines[0].m_last == L"四");
        CHECK(lines[1].m_first == L"五");
    }
}

TEST_CASE("BS8 折行时左括号、左引号不留在行尾", "[bubblestyle]")
{
    BsFontDc dc(kBsFontPx);
    REQUIRE(dc.Ok());
    InlineLayoutOptions opts;
    opts.m_textLineH = bs::TextLineHeight(kBsFontPx);
    opts.m_kinsoku = true;

    const wchar_t* const kSamples[] = { L"一二三四（五六）", L"一二三四“五六”" };
    const wchar_t* const kOpeners[] = { L"（", L"“" };
    for (size_t i = 0; i < sizeof(kSamples) / sizeof(kSamples[0]); ++i)
    {
        CAPTURE(i);
        //可用宽度正好放下「一二三四」加开括号：按原规则开括号留在第一行行尾
        CString head(L"一二三四");
        head += kOpeners[i];
        const int maxW = dc.CharsWidth(head);
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(kSamples[i]);
        std::vector<InlinePlaced> old;
        LayoutInlineContent(dc.Get(), segs, maxW, kBsFacePx, &old);
        const std::vector<BsLineEdge> oldLines = BsLines(old);
        REQUIRE(oldLines.size() >= 2);
        REQUIRE(oldLines[0].m_last == kOpeners[i]);

        //避尾：开括号换到第二行行首
        std::vector<InlinePlaced> placed;
        LayoutInlineContentEx(dc.Get(), segs, maxW, kBsFacePx, opts, &placed);
        const std::vector<BsLineEdge> lines = BsLines(placed);
        REQUIRE(lines.size() >= 2);
        CHECK(lines[0].m_last == L"四");
        CHECK(lines[1].m_first == kOpeners[i]);
    }
}

TEST_CASE("BS9 提示行画成胶囊，「编辑」为品牌橙，超长提示不超出行边", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    //第一条消息前自动插入的时间分隔行
    list.AppendMessage(BsText(true, 51, kBsBaseMs, L"这条马上撤回"));
    //系统提示
    ChatMessage tip = BsText(false, 52, kBsBaseMs + kBsSecondMs, L"「王建国」加入了群聊");
    tip.kind = kMsgTime;
    list.AppendMessage(tip);
    //超长的系统提示
    CString longText;
    for (int i = 0; i < kBsLongTipChars; ++i)
        longText += L"长";
    ChatMessage longTip = BsText(false, 53, kBsBaseMs + 2 * kBsSecondMs, (LPCWSTR)longText);
    longTip.kind = kMsgTime;
    list.AppendMessage(longTip);
    BsAttach(host, list);
    //本人撤回第一条，提示行带「编辑」
    REQUIRE(list.RecallMessageByMsgid(51, L"你撤回了一条消息", true, RecallEditNowMs(), NULL));
    RECT rc = { 0, 0, kBsListW, kBsListH };
    list.ForceLayout(rc);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    //时间分隔与系统提示：文字两侧是胶囊底色，胶囊以外是列表底色
    BsCheckPill(canvas, BsFirstSeparator(list));
    BsCheckPill(canvas, BsRow(list, 52));

    //撤回提示：胶囊里能找到品牌橙的「编辑」
    ChatBubble* recalled = BsRow(list, 51);
    REQUIRE(recalled != NULL);
    REQUIRE(recalled->GetMessage().kind == kMsgTime);
    REQUIRE(recalled->HasRecallEdit(RecallEditNowMs()));
    CHECK(canvas.Count(&BsOrangeish, recalled->GetRect()) > 0);

    //超长提示：行左右各 48 像素以内只有列表底色
    ChatBubble* longRow = BsRow(list, 53);
    REQUIRE(longRow != NULL);
    const RECT lr = longRow->GetRect();
    const RECT leftBand = { lr.left, lr.top, lr.left + bs::kTipPillSideMargin - 1, lr.bottom };
    const RECT rightBand = { lr.right - bs::kTipPillSideMargin + 1, lr.top, lr.right, lr.bottom };
    CHECK(canvas.DarkestLum(leftBand) == BsLum(kBsListBg));
    CHECK(canvas.DarkestLum(rightBand) == BsLum(kBsListBg));
}

TEST_CASE("BS10 图片与大表情不套气泡，图片圆角", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    ChatMessage img = BsPeerText(61, kBsBaseMs, L"");
    img.kind = kMsgImage;
    img.imageW = kBsImageW;
    img.imageH = kBsImageH;
    list.AppendMessage(img);
    ChatMessage emoji = BsPeerText(62, kBsBaseMs + kBsLongGapMinutes * kBsMinuteMs, L"好");
    emoji.kind = kMsgEmoji;
    list.AppendMessage(emoji);
    BsAttach(host, list);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    ChatBubble* bi = BsRow(list, 61);
    ChatBubble* be = BsRow(list, 62);
    REQUIRE(bi != NULL);
    REQUIRE(be != NULL);

    //图片占位块（还没解码时的浅灰底）的范围；左侧外面不再有一圈气泡色，左上角是圆角
    const RECT placeholder = canvas.BoundsOf(kBsImagePlaceholder, bi->GetRect());
    REQUIRE_FALSE(::IsRectEmpty(&placeholder));
    const int midY = (placeholder.top + placeholder.bottom) / 2;
    CHECK(BsNear(canvas.At(placeholder.left - kBsOutsideImage, midY), kBsListBg));
    CHECK(canvas.At(placeholder.left, placeholder.top) != kBsImagePlaceholder);
    //行高不再含 8 像素内边距：图片高 + 上下外边距
    CHECK(bi->GetRect().bottom - bi->GetRect().top == kBsImageH + kBsRowVMargin * 2);

    //大表情：所在区域的角上是列表底色，没有气泡
    const RECT er = be->BubbleRectInHost();
    REQUIRE_FALSE(::IsRectEmpty(&er));
    CHECK(BsNear(canvas.At(er.left + kBsEmojiProbe, er.top + kBsEmojiProbe), kBsListBg));
}

TEST_CASE("BS11 引用块：2 像素灰色竖线，人名不再是品牌橙", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    ChatMessage m = BsPeerText(71, kBsBaseMs, L"我今晚之前给反馈");
    m.hasQuote = true;
    m.quoteName = L"张三";
    m.quoteText = L"新版本的设计稿麻烦确认下";
    m.quoteMsgType = kMsgText;
    list.AppendMessage(m);
    BsAttach(host, list);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    ChatBubble* b = BsRow(list, 71);
    REQUIRE(b != NULL);
    const RECT r = b->BubbleRectInHost();
    REQUIRE_FALSE(::IsRectEmpty(&r));
    const int qLeft = r.left + kBsTextPadH;
    const int qTop = r.top + bs::kTextPadV;

    //竖线：左起第一列是 #CBC6BF，第三列已不是竖线色（宽 2）
    CHECK(BsNear(canvas.At(qLeft, qTop + kBsQuoteProbeY), bs::kQuoteBar));
    CHECK_FALSE(BsNear(canvas.At(qLeft + bs::kQuoteBarW, qTop + kBsQuoteProbeY), bs::kQuoteBar));

    //人名一行：画出了深色文字，没有品牌橙
    const RECT nameRc = { qLeft + kBsQuoteTextLeft, qTop, r.right - kBsTextPadH, qTop + kBsQuoteNameH };
    CHECK(canvas.Count(&BsOrangeish, nameRc) == 0);
    CHECK(canvas.DarkestLum(nameRc) < kBsDarkLumMax);
}

TEST_CASE("BS12 多选模式下被勾选的行整行铺浅橙底", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    list.AppendMessage(BsPeerText(81, kBsBaseMs, L"选中这一条"));
    list.AppendMessage(BsPeerText(82, kBsBaseMs + kBsLongGapMinutes * kBsMinuteMs, L"这一条不选"));
    BsAttach(host, list);
    list.SetMultiSelectMode(true);
    ChatBubble* sel = BsRow(list, 81);
    ChatBubble* other = BsRow(list, 82);
    REQUIRE(sel != NULL);
    REQUIRE(other != NULL);
    sel->SetSelected(true);
    RECT rc = { 0, 0, kBsListW, kBsListH };
    list.ForceLayout(rc);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    const RECT selRow = sel->GetRect();
    const RECT otherRow = other->GetRect();
    CHECK(BsNear(canvas.At(selRow.right - kBsRowRightProbe, (selRow.top + selRow.bottom) / 2), bs::kSelectedRowFill));
    CHECK(BsNear(canvas.At(otherRow.right - kBsRowRightProbe, (otherRow.top + otherRow.bottom) / 2), kBsListBg));
}

TEST_CASE("BS13 「@别人」的底色块为 #E6EEFC，发送失败的原因为红字", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    //群里李晓雯 @王建国
    ChatMessage at = BsGroupText(91, kBsBaseMs, kBsUidLi, L"李晓雯", L"@王建国 看一下这个");
    RenderSpan span;
    span.m_uid = kBsUidWang;
    span.m_off = 0;
    span.m_len = kBsAtLen;
    span.m_isMe = false;
    at.ats.push_back(span);
    list.AppendMessage(at);
    //本人发出、被服务端拒绝的消息（群聊不预留回执行，原因画在气泡下方另加的一行）
    ChatMessage failed = BsText(true, 0, kBsBaseMs + kBsLongGapMinutes * kBsMinuteMs, L"这句话发不出去");
    failed.sendFailed = true;
    failed.failReason = L"消息含有敏感词";
    list.AppendMessage(failed);
    BsAttach(host, list);
    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);

    ChatBubble* ba = BsRow(list, 91);
    REQUIRE(ba != NULL);
    const RECT ra = ba->BubbleRectInHost();
    REQUIRE_FALSE(::IsRectEmpty(&ra));
    const RECT atBlock = canvas.BoundsOf(bs::kAtOtherBg, ra);
    CHECK_FALSE(::IsRectEmpty(&atBlock));

    //发送失败的那一行：气泡下方的原因行里有红色文字
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    ChatBubble* bf = NULL;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().sendFailed)
            bf = b;
    }
    REQUIRE(bf != NULL);
    const RECT rf = bf->BubbleRectInHost();
    REQUIRE_FALSE(::IsRectEmpty(&rf));
    const RECT band = { rf.left - kBsReasonBandLeft, rf.bottom + kBsReasonBandTop, rf.right,
                        rf.bottom + kBsReasonBandTop + kBsReasonBandH };
    CHECK(canvas.Count(&BsReddish, band) > 0);
}

TEST_CASE("BS14 刚发出的消息收到回执后与前一条合组", "[bubblestyle]")
{
    ScopedBsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedBsClock clock;
    BsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessageList list;
    //先垫一批对方的消息，让内容超出可视区：回执引起的重排之后列表仍应停在底部
    for (int k = 0; k < kBsFillerCount; ++k)
        list.AppendMessage(BsPeerText(kBsFillerFirstMsgid + k, kBsBaseMs + k * kBsSecondMs, L"前面的消息"));
    //本人的前一条在「现在」之前 1 分钟；回显按「现在」判定时间分隔，两者之间不插时间行
    const long long prevMs = BsFakeNow() - kBsMinuteMs;
    list.AppendMessage(BsText(true, 1, prevMs, L"前一条"));
    BsAttach(host, list);

    //连发两条：回执到达之前，回显的 msgid 与时间都为 0，只带客户端消息 ID
    ChatMessage echo1 = BsText(true, 0, 0, L"刚发出的第一条");
    echo1.clientMsgId = kBsEchoId1;
    ChatMessage echo2 = BsText(true, 0, 0, L"刚发出的第二条");
    echo2.clientMsgId = kBsEchoId2;
    list.AppendMessage(echo1);
    list.AppendMessage(echo2);

    ChatBubble* prev = BsRow(list, 1);
    ChatBubble* row1 = BsEchoRow(list, kBsEchoId1);
    ChatBubble* row2 = BsEchoRow(list, kBsEchoId2);
    REQUIRE(prev != NULL);
    REQUIRE(row1 != NULL);
    REQUIRE(row2 != NULL);
    //时间未知的回显不合组
    CHECK_FALSE(row1->IsGroupedWithPrev());
    CHECK_FALSE(row2->IsGroupedWithPrev());

    //第一条的回执到达：与前一条合组；第二条还没有时间，仍不与它合组
    REQUIRE(list.BackfillSentMessage(kBsEchoId1, 2, prevMs + kBsEchoAck1Seconds * kBsSecondMs));
    CHECK(prev->IsGroupedWithNext());
    CHECK(row1->IsGroupedWithPrev());
    CHECK_FALSE(row1->IsGroupedWithNext());

    //第二条的回执到达：三条合成一组，第一、二条气泡的间距收窄为 4
    REQUIRE(list.BackfillSentMessage(kBsEchoId2, 3, prevMs + kBsEchoAck2Seconds * kBsSecondMs));
    CHECK(row1->IsGroupedWithNext());
    CHECK(row2->IsGroupedWithPrev());

    BsCanvas canvas;
    REQUIRE(canvas.Ok());
    canvas.Paint(list);
    const RECT r1 = row1->BubbleRectInHost();
    const RECT r2 = row2->BubbleRectInHost();
    REQUIRE_FALSE(::IsRectEmpty(&r1));
    REQUIRE_FALSE(::IsRectEmpty(&r2));
    CHECK(r2.top - r1.bottom == kBsGroupedGap);

    //行高变了以后列表仍停在底部
    int maxScroll = list.ContentHeight() - list.ViewHeight();
    if (maxScroll < 0)
        maxScroll = 0;
    CHECK(list.ContentHeight() > list.ViewHeight());
    CHECK(list.ScrollY() == maxScroll);
}
