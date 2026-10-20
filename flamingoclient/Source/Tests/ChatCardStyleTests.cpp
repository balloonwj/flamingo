/**
 *  聊天卡片美化的用例（标签 [cardstyle]），ChatCardStyleTests.cpp
 *
 *  背景见 docs/聊天卡片美化方案.md：各类卡片统一为白底卡片，标题前画类型色图标块，状态用彩色标签，按钮分三级，
 *  底部一行左边写类型、右边放操作。本文件按方案第 8 节逐条覆盖：
 *    CS1 各卡片的类型色、底部类型标签与图标；已结束的屏幕共享、群通话取灰色；
 *    CS2 文件类型色：八组扩展名各取对应颜色，大小写不敏感，空串与未知扩展名取「其他」；
 *    CS5 底部一行的排版：按钮从右往左排、间距 14、垂直居中，类型标签放不下时截短而按钮完整，置灰按钮不命中；
 *        标题即类型名且没有按钮时不显示这一行；
 *    CS6 公共绘制：离屏画出底板、图标块、状态标签与三级按钮，逐点取样颜色；
 *    CS3 / CS4（共享与通话）屏幕共享、一对一通话、群通话、群公告、群备忘、云端录像、文件共享各状态的标签与按钮，
 *        卡片一律经各自的 ApplyXxx 构造（content 照 ChatCardShotTests.cpp 的样例手写）；
 *    CS7 / CS8（共享与通话）把卡片排进挂在隐藏宿主窗口上的消息列表、画进内存位图，按边框色找出卡片、按按钮颜色找出按钮：
 *        卡片宽 300、本人与对方的卡片内部都是白色、按钮完整落在卡片内；点按钮中心投递的请求与改动前相同，
 *        置灰的按钮点了没有反应、不显示手型，多选模式下不显示手型。
 *    CS3 / CS4 / CS7 / CS8（邀请）入群邀请与密聊邀请：标签、按钮、尺寸与命中；入群邀请只有点按钮才加入，点卡片别处不触发。
 *    CS3 / CS4 / CS7 / CS8（接龙与合并转发）：接龙的状态标签、人数、截止时间与「参与接龙」「修改我的」；合并转发没有标签与按钮，
 *        点整张卡片打开聊天记录；接龙点按钮与点卡片别处投递不同的动作。
 *    CS3 / CS4 / CS7 / CS8 / CS9 / CS10（文件）：各文件态的状态标签与按钮（按钮从右往左按重要程度排），白底、宽 300，
 *        点按钮投递的动作与原来相同，多选模式下不显示手型；上传、下载的进度条为品牌橙；记录窗的文件卡片控件与聊天窗一致。
 *    CS3 / CS4 / CS7 / CS8 / CS9（文件夹）：各传输状态的标签、信息行与按钮（保留期已过的「重试」不再出现），白底、宽 300，
 *        点「接收」投递接收动作，多选模式下不显示手型；传输中的进度条为品牌橙。
 *    CS4 / CS7 / CS8 / CS10 / CS11（Markdown、代码片段、语音）：Markdown 卡片宽 300、白底、没有按钮、点卡片打开预览；
 *        代码片段卡片白底且外面不再套一层气泡；记录窗的 Markdown、代码卡片控件与聊天窗一致；语音未听红点为 #E5484D。
 *
 *  颜色取值抄自方案第 3 节；图标块底色是类型色按比例混入白色后的不透明色，推算见 ChatCardModel.cpp。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <ctime>
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // CS7 / CS8：消息列表挂载的宿主窗口
#include "../UI/BalloonChatWnd/ChatAnnouncementCard.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatCallCard.h"
#include "../UI/BalloonChatWnd/ChatCardModel.h"
#include "../UI/BalloonChatWnd/ChatCloudRecordCard.h"
#include "../App/CustomMsgDef.h"                    // WM_CHAT_FILE_ACTION：文件卡片按钮投给宿主的消息
#include "../UI/BalloonChatWnd/ChatFileAction.h"
#include "../UI/BalloonChatWnd/ChatFileCard.h"
#include "../UI/BalloonChatWnd/ChatFileCardControl.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../UI/BalloonChatWnd/ChatFolderAction.h"
#include "../UI/BalloonChatWnd/ChatFolderStateModel.h"
#include "../UI/BalloonChatWnd/ChatFileShareCard.h"
#include "../UI/BalloonChatWnd/ChatGroupCallCard.h"
#include "../UI/BalloonChatWnd/ChatGroupInviteCard.h"
#include "../UI/BalloonChatWnd/ChatCodeCardControl.h"
#include "../UI/BalloonChatWnd/ChatMarkdownCard.h"
#include "../UI/BalloonChatWnd/ChatMarkdownCardControl.h"
#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../Feature/Markdown/MarkdownDocument.h"     // SetMarkdownApiOverrideForTest：接上测试程序里的 Markdown 函数表
#include "../../FlamingoMarkdown/MdApiTable.h"         // flamingomd::GetMdApiTable
#include "../UI/BalloonChatWnd/ChatMemoCard.h"
#include "../UI/BalloonChatWnd/ChatMergedCard.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatRelayCard.h"
#include "../UI/BalloonChatWnd/ChatScreenShareCard.h"
#include "../UI/BalloonChatWnd/ChatSecretMode.h"
#include "../UI/BalloonChatWnd/ChatCardPaint.h"
#include "../UI/BalloonChatWnd/ChatCardStyle.h"
#include "../UI/BalloonChatWnd/ChatPaintUtil.h"
#include "../UI/BalloonChatWnd/ChatFileVisual.h"
#include "../UI/BalloonChatWnd/ChatIcons.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../Feature/GroupCall/GroupCallStatusCache.h"
#include "../Feature/Memo/MemoClock.h"
#include "../Feature/ScreenShare/ScreenShareRoomState.h"
#include "../net/Protocol.h"

using namespace balloonwjui;
using namespace balloonchatwnd;
using namespace balloonchatwnd::cardpaint;

namespace {

// 屏幕共享、群通话房间状态里「进行中」「已结束」的取值（ScreenShareRoomState.h、GroupCallStatusCache.h，两者取值相同）
const int kCsRoomActive = 1;
const int kCsRoomEnded = 2;

// 群通话、一对一通话的媒体类型：语音、视频
const int kCsMediaVoice = 1;
const int kCsMediaVideo = 2;

// 文件共享的共享类型：磁盘、文件夹、文件（share_kind）
const int kCsShareDisk = 1;
const int kCsShareFolder = 2;
const int kCsShareFile = 3;

// 已结束时图标块的灰色底与灰色图标、文件卡片底部标签图标的灰色（方案 3.1 节）
const COLORREF kCsGrayTile = RGB(0xF1, 0xEF, 0xEC);
const COLORREF kCsGrayGlyph = RGB(0x9A, 0x9A, 0x9F);

// CS6 离屏画布的尺寸与底色（消息列表的暖白底，卡片投影要与它区分）
const int kCsCanvasW = 400;
const int kCsCanvasH = 320;
const COLORREF kCsCanvasBg = RGB(0xFA, 0xF9, 0xF6);
// 32 位 DIB 的位深
const WORD kCsBitsPerPixel = 32;
// 抗锯齿描边取样时允许的每个分量的误差
const int kCsColorTolerance = 2;
// 投影比底色暗的上限：投影只有约 7% 的不透明度，每个分量最多暗这么多
const int kCsShadowMaxDarken = 30;
// 次要按钮文字最暗像素的亮度上限与下限（0~255）：正常为 #5E5E62（亮度约 94），悬停为 #1F2329（约 33），置灰为 #C2BFBA（约 191）
const int kCsSecondaryNormalMaxLum = 125;
const int kCsSecondaryHoverMaxLum = 75;
const int kCsSecondaryDisabledMinLum = 165;

// 造一条指定种类的消息
ChatMessage CsMsg(int kind)
{
    ChatMessage m;
    m.kind = kind;
    return m;
}

// 造一张 kMsgScreenShare 一族的卡片：屏幕共享本身没有标志，其余几种各置一个标志
ChatMessage CsShareFamily(bool call, bool groupCall, bool announcement, bool memo, bool cloud)
{
    ChatMessage m = CsMsg(kMsgScreenShare);
    m.screenShareIsCallCard = call;
    m.screenShareIsGroupCall = groupCall;
    m.screenShareIsAnnouncement = announcement;
    m.screenShareIsMemo = memo;
    m.screenShareIsCloudRecord = cloud;
    return m;
}

// 断言一张有图标块的卡片取到的类型、颜色、图标与标签
void CheckTile(const ChatMessage& m, ChatCardType type, COLORREF tile, COLORREF glyph, int icon, const wchar_t* label)
{
    const ChatCardTypeStyle s = ChatCardTypeStyleOf(m);
    CHECK(s.m_type == type);
    CHECK(s.m_hasTile);
    CHECK_FALSE(s.m_ended);
    CHECK(s.m_tileFill == tile);
    CHECK(s.m_glyphColor == glyph);
    CHECK(s.m_labelGlyphColor == glyph);
    CHECK(s.m_glyph == icon);
    CHECK(ChatCardTypeLabel(m) == CString(label));
}

// 断言一张卡片按「已结束」画成灰色图标块
void CheckEnded(const ChatMessage& m)
{
    const ChatCardTypeStyle s = ChatCardTypeStyleOf(m);
    CHECK(s.m_hasTile);
    CHECK(s.m_ended);
    CHECK(s.m_tileFill == kCsGrayTile);
    CHECK(s.m_glyphColor == kCsGrayGlyph);
    CHECK(s.m_labelGlyphColor == kCsGrayGlyph);
}

} // namespace

TEST_CASE("CS1 各卡片的类型色、底部类型标签与图标", "[cardstyle]")
{
    //文件：标题前画文档图标、没有图标块；底部标签图标为灰色
    {
        const ChatMessage m = CsMsg(kMsgFile);
        const ChatCardTypeStyle s = ChatCardTypeStyleOf(m);
        CHECK(s.m_type == kCardTypeFile);
        CHECK_FALSE(s.m_hasTile);
        CHECK(s.m_labelGlyphColor == kCsGrayGlyph);
        CHECK(s.m_glyph == kChatIconFile);
        CHECK(ChatCardTypeLabel(m) == CString(L"文件"));
    }
    CheckTile(CsMsg(kMsgFolder), kCardTypeFolder, RGB(0xFC, 0xF2, 0xE3), RGB(0xD9, 0x8E, 0x1F), kChatIconFolder, L"文件夹");
    CheckTile(CsMsg(kMsgMerged), kCardTypeMerged, RGB(0xEC, 0xEE, 0xF1), RGB(0x5E, 0x6B, 0x80), kChatIconChatRecord,
              L"聊天记录");
    CheckTile(CsMsg(kMsgRelay), kCardTypeRelay, RGB(0xFF, 0xEE, 0xE7), RGB(0xF2, 0x6A, 0x33), kChatIconRelay, L"接龙");

    //文件共享：图标按共享类型取，颜色相同
    {
        ChatMessage m = CsMsg(kMsgFileShare);
        m.shareKind = kCsShareFolder;
        CheckTile(m, kCardTypeFileShare, RGB(0xE2, 0xF4, 0xF6), RGB(0x0E, 0x97, 0xA8), kChatIconShare, L"文件共享");
        m.shareKind = kCsShareDisk;
        CHECK(ChatCardTypeStyleOf(m).m_glyph == kChatIconDisk);
        m.shareKind = kCsShareFile;
        CHECK(ChatCardTypeStyleOf(m).m_glyph == kChatIconFile);
    }

    //屏幕共享：进行中为蓝色，已结束为灰色
    {
        ChatMessage m = CsShareFamily(false, false, false, false, false);
        m.screenShareState = kCsRoomActive;
        CheckTile(m, kCardTypeScreenShare, RGB(0xEA, 0xEE, 0xFE), RGB(0x4C, 0x6E, 0xF5), kChatIconScreenShare, L"屏幕共享");
        m.screenShareState = kCsRoomEnded;
        CheckEnded(m);
    }

    //一对一通话：按媒体类型取图标与标签；通话结束后仍为绿色（没有可加入的状态，不算「已结束」）
    {
        ChatMessage m = CsShareFamily(true, false, false, false, false);
        m.groupCallMedia = kCsMediaVoice;
        CheckTile(m, kCardTypeCall, RGB(0xE4, 0xF3, 0xEB), RGB(0x1F, 0x9D, 0x5A), kChatIconPhoneLine, L"语音通话");
        m.groupCallMedia = kCsMediaVideo;
        CheckTile(m, kCardTypeCall, RGB(0xE4, 0xF3, 0xEB), RGB(0x1F, 0x9D, 0x5A), kChatIconVideo, L"视频通话");
    }

    //群通话：进行中为绿色；状态缓存判定结束、或卡片已被服务端改写为结束（没有按钮）时为灰色
    {
        ChatMessage m = CsShareFamily(false, true, false, false, false);
        m.groupCallMedia = kCsMediaVideo;
        m.screenShareHasButton = true;
        m.screenShareState = kCsRoomActive;
        CheckTile(m, kCardTypeGroupCall, RGB(0xE4, 0xF3, 0xEB), RGB(0x1F, 0x9D, 0x5A), kChatIconVideo, L"群通话");
        m.groupCallMedia = kCsMediaVoice;
        CHECK(ChatCardTypeStyleOf(m).m_glyph == kChatIconPhoneLine);
        m.screenShareState = kCsRoomEnded;
        CheckEnded(m);
        m.screenShareState = kCsRoomActive;
        m.screenShareHasButton = false;
        CheckEnded(m);
    }

    CheckTile(CsShareFamily(false, false, true, false, false), kCardTypeAnnouncement, RGB(0xFC, 0xEB, 0xEB),
              RGB(0xE5, 0x48, 0x4D), kChatIconAnnounce, L"群公告");
    CheckTile(CsShareFamily(false, false, false, true, false), kCardTypeMemo, RGB(0xFC, 0xF2, 0xE3), RGB(0xD9, 0x8E, 0x1F),
              kChatIconMemo, L"群备忘");

    //云端录像：只有声音的录音画话筒
    {
        ChatMessage m = CsShareFamily(false, false, false, false, true);
        CheckTile(m, kCardTypeCloudRecord, RGB(0xF1, 0xEB, 0xFE), RGB(0x8B, 0x5C, 0xF6), kChatIconCloudRecord, L"云端录像");
        m.cloudRecordAudioOnly = true;
        CHECK(ChatCardTypeStyleOf(m).m_glyph == kChatIconMic);
    }

    CheckTile(CsMsg(kMsgGroupInvite), kCardTypeGroupInvite, RGB(0xE6, 0xF0, 0xFD), RGB(0x2F, 0x80, 0xED),
              kChatIconGroupInvite, L"群聊邀请");
    CheckTile(CsMsg(kMsgSecretInvite), kCardTypeSecretInvite, RGB(0xEA, 0xEB, 0xEC), RGB(0x3C, 0x46, 0x54), kChatIconLock,
              L"密聊");

    //Markdown 与代码片段都是 kMsgText，按消息标志区分
    {
        ChatMessage md = CsMsg(kMsgText);
        md.isMarkdown = true;
        CheckTile(md, kCardTypeMarkdown, RGB(0xEA, 0xEB, 0xEC), RGB(0x3C, 0x46, 0x54), kChatIconMarkdown, L"Markdown 文档");

        ChatMessage code = CsMsg(kMsgText);
        code.isCode = true;
        const ChatCardTypeStyle s = ChatCardTypeStyleOf(code);
        CHECK(s.m_type == kCardTypeCode);
        CHECK_FALSE(s.m_hasTile);
        CHECK(s.m_glyph == kChatIconCode);
        CHECK(ChatCardTypeLabel(code) == CString(L"代码片段"));
    }

    //不是卡片的消息
    const int plainKinds[] = { kMsgTime, kMsgText, kMsgEmoji, kMsgImage, kMsgVoice };
    for (size_t i = 0; i < sizeof(plainKinds) / sizeof(plainKinds[0]); ++i)
    {
        const ChatMessage m = CsMsg(plainKinds[i]);
        CHECK(ChatCardTypeOf(m) == kCardTypeNone);
        CHECK_FALSE(ChatCardTypeStyleOf(m).m_hasTile);
        CHECK(ChatCardTypeLabel(m).IsEmpty());
    }
}

TEST_CASE("CS2 文件类型色按扩展名分八组，大小写不敏感", "[cardstyle]")
{
    struct Case
    {
        const wchar_t* m_ext;   // 扩展名
        COLORREF       m_color; // 期望的文档图标底色
    };
    const COLORREF pdf = RGB(0xE5, 0x48, 0x4D);
    const COLORREF word = RGB(0x2F, 0x6F, 0xED);
    const COLORREF sheet = RGB(0x1F, 0x9D, 0x5A);
    const COLORREF slide = RGB(0xF0, 0x66, 0x2B);
    const COLORREF design = RGB(0xA2, 0x59, 0xFF);
    const COLORREF image = RGB(0x0E, 0xA5, 0xB7);
    const COLORREF archive = RGB(0x8C, 0x6D, 0x4F);
    const COLORREF other = RGB(0x8A, 0x94, 0xA6);
    const Case cases[] = {
        { L"pdf", pdf }, { L"PDF", pdf },
        { L"doc", word }, { L"docx", word }, { L"DOCX", word },
        { L"xls", sheet }, { L"xlsx", sheet }, { L"csv", sheet },
        { L"ppt", slide }, { L"pptx", slide }, { L"PPTX", slide },
        { L"fig", design }, { L"sketch", design }, { L"psd", design }, { L"ai", design }, { L"xd", design },
        { L"png", image }, { L"jpg", image }, { L"jpeg", image }, { L"gif", image }, { L"bmp", image }, { L"webp", image },
        { L"svg", image },
        { L"zip", archive }, { L"rar", archive }, { L"7z", archive }, { L"tar", archive }, { L"gz", archive },
        { L"", other }, { L"txt", other }, { L"exe", other },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        INFO("ext=" << CW2A(cases[i].m_ext));
        CHECK(FileTypeColor(CString(cases[i].m_ext)) == cases[i].m_color);
    }
}

namespace {

// 在作用域内启动 GDI+，须在画布之前声明
class ScopedCsGdiplus
{
public:
    ScopedCsGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~ScopedCsGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    // GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌
    bool      m_ok;      // 是否启动成功
};

// 一张 32 位离屏画布：建好后铺满底色，析构时释放
class CsCanvas
{
public:
    CsCanvas()
        : m_dc(NULL), m_bmp(NULL), m_old(NULL), m_bits(NULL)
    {
        HDC screen = ::GetDC(NULL);
        m_dc = ::CreateCompatibleDC(screen);
        ::ReleaseDC(NULL, screen);
        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kCsCanvasW;
        bi.bmiHeader.biHeight = -kCsCanvasH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = kCsBitsPerPixel;
        bi.bmiHeader.biCompression = BI_RGB;
        m_bmp = ::CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &m_bits, NULL, 0);
        m_old = ::SelectObject(m_dc, m_bmp);
        RECT all = { 0, 0, kCsCanvasW, kCsCanvasH };
        HBRUSH bg = ::CreateSolidBrush(kCsCanvasBg);
        ::FillRect(m_dc, &all, bg);
        ::DeleteObject(bg);
    }

    ~CsCanvas()
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

    // 取一个像素的颜色
    COLORREF At(int x, int y) const
    {
        ::GdiFlush();
        const unsigned int px = ((const unsigned int*)m_bits)[y * kCsCanvasW + x];
        return RGB((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF);
    }

    // 矩形内最暗像素的亮度（0~255，按 (R*299 + G*587 + B*114) / 1000 计）
    int DarkestLum(const RECT& rc) const
    {
        int darkest = 255;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                const COLORREF c = At(x, y);
                const int lum = (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000;
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

// 两个颜色每个分量都在误差以内
bool CsNear(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= kCsColorTolerance
        && abs((int)GetGValue(a) - (int)GetGValue(b)) <= kCsColorTolerance
        && abs((int)GetBValue(a) - (int)GetBValue(b)) <= kCsColorTolerance;
}

// 造一个矩形
RECT CsRect(int left, int top, int right, int bottom)
{
    RECT rc = { left, top, right, bottom };
    return rc;
}

// 两个矩形相等
bool CsSameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

} // namespace

TEST_CASE("CS5 底部一行：按钮从右往左排，标签放不下时截短", "[cardstyle]")
{
    const RECT footer = CsRect(0, 100, cardstyle::kCardWidth, 100 + cardstyle::kFooterH);
    //按钮垂直居中：上下各留 (40 - 28) / 2 = 6
    const int btnTop = 106;
    const int btnBottom = 134;

    //一个次要（宽 40）一个主要（宽 70）：最右边距右边 14，间距 14
    std::vector<int> widths;
    widths.push_back(40);
    widths.push_back(70);
    CardFooterLayout lay = LayoutCardFooter(footer, 60, widths);
    REQUIRE(lay.m_buttonRects.size() == 2);
    CHECK(CsSameRect(lay.m_buttonRects[1], CsRect(216, btnTop, 286, btnBottom)));
    CHECK(CsSameRect(lay.m_buttonRects[0], CsRect(162, btnTop, 202, btnBottom)));
    CHECK(CsSameRect(lay.m_labelRect, CsRect(14, 100, 74, 140)));
    CHECK_FALSE(lay.m_labelTruncated);

    //标签太长：截到最左边按钮左侧 10 像素处，按钮不动
    lay = LayoutCardFooter(footer, 300, widths);
    CHECK(CsSameRect(lay.m_labelRect, CsRect(14, 100, 152, 140)));
    CHECK(lay.m_labelTruncated);
    CHECK(CsSameRect(lay.m_buttonRects[0], CsRect(162, btnTop, 202, btnBottom)));

    //没有按钮：标签最多排到右边 14 处
    lay = LayoutCardFooter(footer, 60, std::vector<int>());
    CHECK(lay.m_buttonRects.empty());
    CHECK(CsSameRect(lay.m_labelRect, CsRect(14, 100, 74, 140)));
    lay = LayoutCardFooter(footer, 400, std::vector<int>());
    CHECK(CsSameRect(lay.m_labelRect, CsRect(14, 100, 286, 140)));
    CHECK(lay.m_labelTruncated);

    //按钮占满整行：标签一点都放不下，按钮仍然完整
    widths[0] = 200;
    lay = LayoutCardFooter(footer, 60, widths);
    CHECK(::IsRectEmpty(&lay.m_labelRect));
    CHECK(lay.m_labelTruncated);
    CHECK(CsSameRect(lay.m_buttonRects[0], CsRect(2, btnTop, 202, btnBottom)));

    //命中：可用按钮按矩形命中，置灰的与间隙都不命中
    widths[0] = 40;
    lay = LayoutCardFooter(footer, 60, widths);
    std::vector<ChatCardButton> buttons;
    buttons.push_back(ChatCardButton(CString(L"拒绝"), kCardButtonSecondary, true, 1));
    buttons.push_back(ChatCardButton(CString(L"接收"), kCardButtonPrimary, true, 2));
    POINT inRight = { 250, 120 };
    POINT inLeft = { 180, 120 };
    POINT inGap = { 209, 120 };
    CHECK(CardFooterButtonAt(lay, buttons, inRight) == 1);
    CHECK(CardFooterButtonAt(lay, buttons, inLeft) == 0);
    CHECK(CardFooterButtonAt(lay, buttons, inGap) == -1);
    buttons[0].m_enabled = false;
    CHECK(CardFooterButtonAt(lay, buttons, inLeft) == -1);

    //标题即类型名且没有按钮时不显示这一行
    CHECK_FALSE(ChatCardFooterVisible(true, 0));
    CHECK(ChatCardFooterVisible(true, 1));
    CHECK(ChatCardFooterVisible(false, 0));
}

TEST_CASE("CS6 公共绘制：底板、图标块、状态标签与三级按钮的颜色", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());

    //底板：内部白色，左边框为暖灰，底边下方一行有淡投影
    {
        CsCanvas canvas;
        REQUIRE(canvas.Ok());
        Gdiplus::Graphics g(canvas.Dc());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        PaintCardFrame(g, CsRect(20, 20, 320, 140));
        g.Flush(Gdiplus::FlushIntentionSync);
        CHECK(canvas.At(170, 80) == cardstyle::kCardFill);
        CHECK(CsNear(canvas.At(20, 80), cardstyle::kCardBorder));
        const COLORREF shadow = canvas.At(170, 140);
        CHECK(GetRValue(shadow) < GetRValue(kCsCanvasBg));
        CHECK(GetRValue(shadow) > GetRValue(kCsCanvasBg) - kCsShadowMaxDarken);
        CHECK(canvas.At(170, 145) == kCsCanvasBg);
    }

    //图标块：屏幕共享进行中为浅蓝底，已结束为灰底
    {
        CsCanvas canvas;
        REQUIRE(canvas.Ok());
        Gdiplus::Graphics g(canvas.Dc());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        ChatMessage m = CsShareFamily(false, false, false, false, false);
        m.screenShareState = kCsRoomActive;
        PaintCardTile(g, 40, 40, ChatCardTypeStyleOf(m));
        m.screenShareState = kCsRoomEnded;
        PaintCardTile(g, 120, 40, ChatCardTypeStyleOf(m));
        g.Flush(Gdiplus::FlushIntentionSync);
        CHECK(canvas.At(43, 58) == RGB(0xEA, 0xEE, 0xFE));
        CHECK(canvas.At(123, 58) == kCsGrayTile);
        //图标块之外仍是底色
        CHECK(canvas.At(38, 58) == kCsCanvasBg);
    }

    //状态标签：浅绿底，圆点为绿色；宽度为左右内边距、圆点、间距与文字之和
    {
        CsCanvas canvas;
        REQUIRE(canvas.Ok());
        Gdiplus::Graphics g(canvas.Dc());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const CString chip(L"正在共享");
        const int right = PaintCardChip(canvas.Dc(), g, 20, 260, 280, chip, kCardChipLive);
        g.Flush(Gdiplus::FlushIntentionSync);
        const int textW = chatpaint::MeasureTextWidth(canvas.Dc(), chip, cardstyle::kChipPx);
        CHECK(MeasureCardChipWidth(canvas.Dc(), chip) == cardstyle::kChipPadH * 2 + cardstyle::kChipDotSize
                                                          + cardstyle::kChipDotGap + textW);
        CHECK(right == 20 + MeasureCardChipWidth(canvas.Dc(), chip));
        CHECK(canvas.At(23, 270) == cardstyle::kChipLiveFill);
        CHECK(canvas.At(31, 270) == cardstyle::kChipLiveText);
        CHECK(CardChipColorsOf(kCardChipBad).m_fill == cardstyle::kChipBadFill);
        CHECK(CardChipColorsOf(kCardChipNeutral).m_text == cardstyle::kChipNeutralText);
    }

    //三级按钮的正常、悬停、置灰
    {
        CsCanvas canvas;
        REQUIRE(canvas.Ok());
        Gdiplus::Graphics g(canvas.Dc());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const int states[] = { kCardButtonNormal, kCardButtonHover, kCardButtonDisabled };
        for (int s = 0; s < 3; ++s)
        {
            const int top = 20 + s * 40;
            PaintCardButton(canvas.Dc(), g, CsRect(20, top, 100, top + cardstyle::kButtonH), CString(L"加入"),
                            kCardButtonPrimary, states[s]);
            PaintCardButton(canvas.Dc(), g, CsRect(120, top, 200, top + cardstyle::kButtonH), CString(L"查看"),
                            kCardButtonView, states[s]);
            PaintCardButton(canvas.Dc(), g, CsRect(220, top, 300, top + cardstyle::kButtonH), CString(L"忽略"),
                            kCardButtonSecondary, states[s]);
        }
        g.Flush(Gdiplus::FlushIntentionSync);
        const int midY = cardstyle::kButtonH / 2;
        //主要按钮的底色
        CHECK(canvas.At(24, 20 + midY) == cardstyle::kPrimaryFill);
        CHECK(canvas.At(24, 60 + midY) == cardstyle::kPrimaryHoverFill);
        CHECK(canvas.At(24, 100 + midY) == cardstyle::kPrimaryDisabledFill);
        //查看按钮的底色与上边框
        CHECK(canvas.At(124, 20 + midY) == cardstyle::kViewFill);
        CHECK(canvas.At(124, 60 + midY) == cardstyle::kViewHoverFill);
        CHECK(CsNear(canvas.At(160, 20), cardstyle::kViewBorder));
        CHECK(CsNear(canvas.At(160, 100), cardstyle::kViewDisabledBorder));
        //次要按钮只有文字：按最暗像素的亮度判断颜色深浅
        CHECK(canvas.DarkestLum(CsRect(220, 20, 300, 48)) < kCsSecondaryNormalMaxLum);
        CHECK(canvas.DarkestLum(CsRect(220, 60, 300, 88)) < kCsSecondaryHoverMaxLum);
        CHECK(canvas.DarkestLum(CsRect(220, 100, 300, 128)) > kCsSecondaryDisabledMinLum);
        //次要按钮没有底色，文字以外仍是画布底色
        CHECK(canvas.At(222, 20 + midY) == kCsCanvasBg);
        //颜色表本身
        CHECK(CardButtonColorsOf(kCardButtonPrimary, kCardButtonNormal).m_bold);
        CHECK_FALSE(CardButtonColorsOf(kCardButtonSecondary, kCardButtonNormal).m_hasFill);
        CHECK(CardButtonColorsOf(kCardButtonView, kCardButtonDisabled).m_text == cardstyle::kViewDisabledText);
    }
}

namespace {

// ---- CS3 / CS4（共享与通话）的样例 content，照 ChatCardShotTests.cpp ----

const char* const kCsShareContent =
    "{\"roomid\":\"ss-20261014-01\",\"host\":\"100003\",\"hostname\":\"李晓雯\",\"title\":\"登录页改版效果演示\"}";
const char* const kCsCallConnected =
    "{\"kind\":\"call\",\"sessionid\":\"call-01\",\"media\":1,\"caller\":\"100002\",\"callee\":\"100001\","
    "\"callername\":\"张三\",\"result\":7,\"duration\":332}";
const char* const kCsCallRejected =
    "{\"kind\":\"call\",\"sessionid\":\"call-02\",\"media\":2,\"caller\":\"100001\",\"callee\":\"100002\","
    "\"callername\":\"林知夏\",\"result\":2}";
const char* const kCsCallCanceled =
    "{\"kind\":\"call\",\"sessionid\":\"call-03\",\"media\":1,\"caller\":\"100001\",\"callee\":\"100002\","
    "\"callername\":\"林知夏\",\"result\":3}";
const char* const kCsCallRinging =
    "{\"kind\":\"call\",\"sessionid\":\"call-04\",\"media\":1,\"caller\":\"100001\",\"callee\":\"100002\","
    "\"callername\":\"林知夏\"}";
const char* const kCsGroupCallLive =
    "{\"kind\":\"groupcall\",\"roomid\":\"gc-20261014-02\",\"media\":2,\"initiator\":\"100004\",\"initiatorname\":\"王建国\"}";
const char* const kCsGroupCallDone =
    "{\"kind\":\"groupcall\",\"roomid\":\"gc-20261013-05\",\"media\":1,\"initiator\":\"100005\",\"initiatorname\":\"赵敏\","
    "\"duration\":1520}";
const char* const kCsAnnouncement =
    "{\"announcementid\":\"501\",\"groupid\":\"3001\",\"title\":\"关于国庆假期值班安排的通知\",\"body\":\"请登记值班日期\","
    "\"severity\":1,\"publisherid\":\"100004\",\"publisher\":\"王建国\",\"publishtime\":\"1759190400000\"}";
const char* const kCsAnnouncementNormal =
    "{\"announcementid\":\"502\",\"groupid\":\"3001\",\"title\":\"周会改到周四\",\"body\":\"地点不变\","
    "\"severity\":0,\"publisherid\":\"100004\",\"publisher\":\"王建国\",\"publishtime\":\"1759190400000\"}";
const char* const kCsAnnouncementUrgent =
    "{\"announcementid\":\"503\",\"groupid\":\"3001\",\"title\":\"服务器维护\",\"body\":\"今晚 22 点停机\","
    "\"severity\":2,\"publisherid\":\"100004\",\"publisher\":\"王建国\",\"publishtime\":\"1759190400000\"}";
const char* const kCsAnnouncementDeleted =
    "{\"announcementid\":\"504\",\"groupid\":\"3001\",\"title\":\"已撤下的通知\",\"deleted\":true,"
    "\"publisherid\":\"100004\",\"publisher\":\"王建国\",\"publishtime\":\"1759190400000\"}";
const char* const kCsMemo =
    "{\"memoid\":\"701\",\"groupid\":\"3001\",\"change\":1,\"title\":\"第四季度规划评审会\",\"note\":\"准备汇报材料\","
    "\"start\":\"2026-10-20 14:30\",\"advance\":15,\"repeat\":0,\"weekdays\":0,\"count\":1,\"targetall\":true,"
    "\"targetcount\":0,\"editorid\":\"100003\",\"editor\":\"李晓雯\"}";
const char* const kCsCloudRecord =
    "{\"recordid\":\"801\",\"owner\":\"100002\",\"title\":\"产品评审会录像\",\"durationms\":2723000,"
    "\"bytes\":356515840,\"audioonly\":0}";
const char* const kCsShareFolderContent = "{\"shareid\":\"901\",\"ownerid\":\"100003\",\"kind\":2,\"name\":\"设计稿归档\"}";
const char* const kCsShareFileContent = "{\"shareid\":\"902\",\"ownerid\":\"100003\",\"kind\":3,\"name\":\"plan.docx\"}";
const char* const kCsBadContent = "{\"kind\":\"nothing\"}";

// 群通话里已经在通话的人数
const int kCsJoined = 4;
// 一对一通话正在进行时的媒体类型（ApplyCallCard 的 inCallMedia；0 表示本人不在这通电话里）
const int kCsInCallVoice = 1;

// 对方发来的一张卡片消息
ChatMessage CsPeerMsg()
{
    ChatMessage m;
    m.fromMe = false;
    return m;
}

// 本人发出的一张卡片消息
ChatMessage CsMineMsg()
{
    ChatMessage m;
    m.fromMe = true;
    return m;
}

// 断言状态标签与信息行
void CheckChip(const ChatMessage& m, int tone, const wchar_t* chip, const wchar_t* meta)
{
    CHECK(m.cardChipTone == tone);
    CHECK(m.cardChipText == CString(chip));
    CHECK(m.cardMetaText == CString(meta));
}

// 断言只有一个按钮
void CheckOneButton(const ChatMessage& m, const wchar_t* text, int level, bool enabled)
{
    const std::vector<ChatCardButton> b = ChatCardButtonsOf(m);
    REQUIRE(b.size() == 1);
    CHECK(b[0].m_text == CString(text));
    CHECK(b[0].m_level == level);
    CHECK(b[0].m_enabled == enabled);
}

// 文件共享的实时状态
ShareCardLiveState CsLive(bool moduleReady, int state, int reason)
{
    ShareCardLiveState live;
    live.m_moduleReady = moduleReady;
    live.m_hasState = (state != 0);
    live.m_state = state;
    live.m_reason = reason;
    return live;
}

} // namespace

TEST_CASE("CS3 状态标签：共享与通话", "[cardstyle]")
{
    //屏幕共享：标签就是原来的状态文字，进行中为绿，其余为灰
    {
        ChatMessage m = CsPeerMsg();
        ApplyScreenShareCard(m, kCsShareContent, CString(L"李晓雯"), kScreenShareRoomActive);
        CheckChip(m, kCardChipLive, L"正在共享", L"");
        ApplyScreenShareState(m, kScreenShareRoomEnded);
        CheckChip(m, kCardChipEnded, L"共享已结束", L"");
        ApplyScreenShareState(m, kScreenShareRoomUnknown);
        CheckChip(m, kCardChipEnded, L"正在获取状态", L"");
    }

    //一对一通话
    {
        ChatMessage m = CsPeerMsg();
        ApplyCallCard(m, kCsCallConnected, CString(L"张三"), 0);
        CheckChip(m, kCardChipEnded, L"已结束", L"通话时长 05:32");
        //原来的状态文字不变
        CHECK(m.screenShareStatus == CString(L"通话时长 05:32"));

        ChatMessage mine = CsMineMsg();
        ApplyCallCard(mine, kCsCallRejected, CString(L"张三"), 0);
        CheckChip(mine, kCardChipBad, L"对方已拒绝", L"");
        ChatMessage peer = CsPeerMsg();
        ApplyCallCard(peer, kCsCallRejected, CString(L"林知夏"), 0);
        CheckChip(peer, kCardChipBad, L"已拒绝", L"");

        ChatMessage canceled = CsMineMsg();
        ApplyCallCard(canceled, kCsCallCanceled, CString(L"张三"), 0);
        CheckChip(canceled, kCardChipEnded, L"已取消", L"");

        ChatMessage ringing = CsMineMsg();
        ApplyCallCard(ringing, kCsCallRinging, CString(L"张三"), 0);
        CheckChip(ringing, kCardChipPending, L"正在呼叫", L"");
        ChatMessage invited = CsPeerMsg();
        ApplyCallCard(invited, kCsCallRinging, CString(L"林知夏"), 0);
        CheckChip(invited, kCardChipPending, L"邀请你通话", L"");
        ApplyCallCard(invited, kCsCallRinging, CString(L"林知夏"), kCsInCallVoice);
        CheckChip(invited, kCardChipLive, L"通话中", L"");

        ChatMessage bad = CsPeerMsg();
        ApplyCallCard(bad, kCsBadContent, CString(L"张三"), 0);
        CheckChip(bad, kCardChipEnded, L"通话已结束", L"");
    }

    //群通话：进行中写人数，结束后写时长
    {
        ChatMessage m = CsPeerMsg();
        ApplyGroupCallCard(m, kCsGroupCallLive, CString(L"王建国"), balloongroupcall::kGroupCallRoomActive, kCsJoined);
        CheckChip(m, kCardChipLive, L"正在通话", L"4 人");
        ApplyGroupCallState(m, balloongroupcall::kGroupCallRoomActive, 0);
        CheckChip(m, kCardChipLive, L"正在通话", L"");
        ApplyGroupCallState(m, balloongroupcall::kGroupCallRoomEnded, 0);
        CheckChip(m, kCardChipEnded, L"通话已结束", L"");
        ApplyGroupCallState(m, balloongroupcall::kGroupCallRoomUnknown, 0);
        CheckChip(m, kCardChipEnded, L"正在获取状态", L"");

        ChatMessage done = CsPeerMsg();
        ApplyGroupCallCard(done, kCsGroupCallDone, CString(L"赵敏"), balloongroupcall::kGroupCallRoomEnded, 0);
        CheckChip(done, kCardChipEnded, L"通话已结束", L"时长 25:20");
    }

    //群公告：标签写优先级，重要、紧急为红；删除后、内容不可用时为灰
    {
        ChatMessage m = CsPeerMsg();
        ApplyAnnouncementCard(m, kCsAnnouncement, CString(L"王建国"));
        CheckChip(m, kCardChipBad, L"重要", L"");
        ApplyAnnouncementCard(m, kCsAnnouncementUrgent, CString(L"王建国"));
        CheckChip(m, kCardChipBad, L"紧急", L"");
        ApplyAnnouncementCard(m, kCsAnnouncementNormal, CString(L"王建国"));
        CheckChip(m, kCardChipEnded, L"通知", L"");
        ApplyAnnouncementCard(m, kCsAnnouncementDeleted, CString(L"王建国"));
        CheckChip(m, kCardChipEnded, L"该公告已被删除", L"");
        ApplyAnnouncementCard(m, kCsBadContent, CString(L"王建国"));
        CheckChip(m, kCardChipEnded, L"公告内容不可用", L"");
    }

    //群备忘：提醒对象为蓝；不可用时为灰，恢复后变回蓝
    {
        ChatMessage m = CsPeerMsg();
        ApplyMemoCard(m, kCsMemo, CString(L"李晓雯"), memo::NowLocalMinute());
        CheckChip(m, kCardChipNeutral, L"提醒全体成员", L"");
        REQUIRE(SetMemoCardAvailable(m, false));
        CheckChip(m, kCardChipEnded, L"备忘已删除，或你已不在提醒对象中", L"");
        REQUIRE(SetMemoCardAvailable(m, true));
        CheckChip(m, kCardChipNeutral, L"提醒全体成员", L"");
        ApplyMemoCard(m, kCsBadContent, CString(L"李晓雯"), memo::NowLocalMinute());
        CheckChip(m, kCardChipEnded, L"备忘内容不可用", L"");
    }

    //云端录像：没有标签，信息行写时长与大小
    {
        ChatMessage m = CsPeerMsg();
        ApplyCloudRecordCard(m, kCsCloudRecord, CString(L"张三"));
        CheckChip(m, kCardChipNone, L"", L"时长 45:23 · 340.0 MB");
        ApplyCloudRecordCard(m, kCsBadContent, CString(L"张三"));
        CheckChip(m, kCardChipEnded, L"录像内容不可用", L"");
    }

    //文件共享：标签就是原来的状态文字；可用为绿，暂不可用为橙，其余为灰
    {
        ChatMessage m = CsPeerMsg();
        ApplyFileShareCard(m, kCsShareFolderContent, CString(L"李晓雯"), CsLive(true, share_state_available, 0));
        CheckChip(m, kCardChipLive, L"可用", L"");
        ApplyFileShareState(m, true, share_state_unavailable, share_unavailable_owner_offline);
        CHECK(m.cardChipTone == kCardChipPending);
        CHECK(m.cardChipText == m.shareStatus);
        ApplyFileShareState(m, true, share_state_invalid, share_invalid_owner_canceled);
        CHECK(m.cardChipTone == kCardChipEnded);
        CHECK(m.cardChipText == m.shareStatus);
        ApplyFileShareState(m, false, share_state_available, 0);
        CheckChip(m, kCardChipEnded, L"当前客户端未启用共享功能", L"");
        ApplyFileShareState(m, true, 0, 0);
        CheckChip(m, kCardChipEnded, L"正在获取状态", L"");
    }
}

TEST_CASE("CS4 按钮：共享与通话", "[cardstyle]")
{
    //屏幕共享：别人发起、未结束时有「加入」（主要）；结束后去掉；本人发起的没有
    {
        ChatMessage m = CsPeerMsg();
        ApplyScreenShareCard(m, kCsShareContent, CString(L"李晓雯"), kScreenShareRoomActive);
        CheckOneButton(m, L"加入", kCardButtonPrimary, true);
        ApplyScreenShareState(m, kScreenShareRoomUnknown);
        CheckOneButton(m, L"加入", kCardButtonPrimary, true);
        ApplyScreenShareState(m, kScreenShareRoomEnded);
        CHECK(ChatCardButtonsOf(m).empty());
        ChatMessage mine = CsMineMsg();
        ApplyScreenShareCard(mine, kCsShareContent, CString(L"林知夏"), kScreenShareRoomActive);
        CHECK(ChatCardButtonsOf(mine).empty());
    }

    //一对一通话：没有按钮
    {
        ChatMessage m = CsPeerMsg();
        ApplyCallCard(m, kCsCallRinging, CString(L"林知夏"), 0);
        CHECK(ChatCardButtonsOf(m).empty());
    }

    //群通话：进行中、状态未知时有「加入」；状态缓存判定结束、卡片已被改写为结束时都去掉
    {
        ChatMessage m = CsPeerMsg();
        ApplyGroupCallCard(m, kCsGroupCallLive, CString(L"王建国"), balloongroupcall::kGroupCallRoomActive, kCsJoined);
        CheckOneButton(m, L"加入", kCardButtonPrimary, true);
        ApplyGroupCallState(m, balloongroupcall::kGroupCallRoomUnknown, 0);
        CheckOneButton(m, L"加入", kCardButtonPrimary, true);
        ApplyGroupCallState(m, balloongroupcall::kGroupCallRoomEnded, 0);
        CHECK(ChatCardButtonsOf(m).empty());
        ChatMessage done = CsPeerMsg();
        ApplyGroupCallCard(done, kCsGroupCallDone, CString(L"赵敏"), balloongroupcall::kGroupCallRoomEnded, 0);
        CHECK(ChatCardButtonsOf(done).empty());
    }

    //群公告：「查看」（查看级）；删除后、内容不可用时去掉
    {
        ChatMessage m = CsPeerMsg();
        ApplyAnnouncementCard(m, kCsAnnouncement, CString(L"王建国"));
        CheckOneButton(m, L"查看", kCardButtonView, true);
        ApplyAnnouncementCard(m, kCsAnnouncementDeleted, CString(L"王建国"));
        CHECK(ChatCardButtonsOf(m).empty());
        ApplyAnnouncementCard(m, kCsBadContent, CString(L"王建国"));
        CHECK(ChatCardButtonsOf(m).empty());
    }

    //群备忘：「查看」；不可用时置灰（分不清删除与被移出提醒对象，按可能恢复处理）；内容不可用时去掉
    {
        ChatMessage m = CsPeerMsg();
        ApplyMemoCard(m, kCsMemo, CString(L"李晓雯"), memo::NowLocalMinute());
        CheckOneButton(m, L"查看", kCardButtonView, true);
        REQUIRE(SetMemoCardAvailable(m, false));
        CheckOneButton(m, L"查看", kCardButtonView, false);
        ApplyMemoCard(m, kCsBadContent, CString(L"李晓雯"), memo::NowLocalMinute());
        CHECK(ChatCardButtonsOf(m).empty());
    }

    //云端录像：「播放」（查看级）；内容不可用时去掉
    {
        ChatMessage m = CsPeerMsg();
        ApplyCloudRecordCard(m, kCsCloudRecord, CString(L"张三"));
        CheckOneButton(m, L"播放", kCardButtonView, true);
        ApplyCloudRecordCard(m, kCsBadContent, CString(L"张三"));
        CHECK(ChatCardButtonsOf(m).empty());
    }

    //文件共享：按共享类型与是否本人写按钮文字；已失效、内容不可用时去掉；模块没加载、功能被关闭时置灰
    {
        ChatMessage m = CsPeerMsg();
        ApplyFileShareCard(m, kCsShareFolderContent, CString(L"李晓雯"), CsLive(true, share_state_available, 0));
        CheckOneButton(m, L"打开", kCardButtonView, true);
        ApplyFileShareState(m, false, share_state_available, 0);
        CheckOneButton(m, L"打开", kCardButtonView, false);
        ApplyFileShareState(m, true, share_state_unavailable, share_unavailable_feature_disabled);
        CheckOneButton(m, L"打开", kCardButtonView, false);
        ApplyFileShareState(m, true, share_state_unavailable, share_unavailable_owner_offline);
        CheckOneButton(m, L"打开", kCardButtonView, true);
        ApplyFileShareState(m, true, share_state_invalid, share_invalid_owner_canceled);
        CHECK(ChatCardButtonsOf(m).empty());

        ChatMessage file = CsPeerMsg();
        ApplyFileShareCard(file, kCsShareFileContent, CString(L"李晓雯"), CsLive(true, share_state_available, 0));
        CheckOneButton(file, L"下载", kCardButtonView, true);
        ChatMessage own = CsMineMsg();
        ApplyFileShareCard(own, kCsShareFolderContent, CString(L"林知夏"), CsLive(true, share_state_available, 0));
        CheckOneButton(own, L"在本机打开", kCardButtonView, true);
        ChatMessage bad = CsPeerMsg();
        ApplyFileShareCard(bad, kCsBadContent, CString(L"李晓雯"), CsLive(true, share_state_available, 0));
        CHECK(ChatCardButtonsOf(bad).empty());
    }

    //标题即类型名：一对一通话、群通话、没有标题的卡片
    {
        ChatMessage call = CsPeerMsg();
        ApplyCallCard(call, kCsCallConnected, CString(L"张三"), 0);
        CHECK(ChatCardTitleIsTypeName(call));
        ChatMessage group = CsPeerMsg();
        ApplyGroupCallCard(group, kCsGroupCallDone, CString(L"赵敏"), balloongroupcall::kGroupCallRoomEnded, 0);
        CHECK(ChatCardTitleIsTypeName(group));
        ChatMessage titled = CsPeerMsg();
        ApplyScreenShareCard(titled, kCsShareContent, CString(L"李晓雯"), kScreenShareRoomActive);
        CHECK_FALSE(ChatCardTitleIsTypeName(titled));
        ChatMessage untitled = CsPeerMsg();
        ApplyScreenShareCard(untitled, "{\"roomid\":\"ss-1\",\"host\":\"100003\"}", CString(L"李晓雯"), kScreenShareRoomActive);
        CHECK(ChatCardTitleIsTypeName(untitled));
        ChatMessage notice = CsPeerMsg();
        ApplyAnnouncementCard(notice, kCsAnnouncement, CString(L"王建国"));
        CHECK_FALSE(ChatCardTitleIsTypeName(notice));
    }
}

namespace {

// ---- CS7 / CS8：把一张卡片排进消息列表、画出来再找卡片与按钮 ----

const int kCsListW = 760;          // 消息列表宽（像素），常见聊天窗宽度
const int kCsListH = 600;          // 消息列表高（像素），放得下一张最高的卡片
const int kCsInset = 3;            // 取卡片内部颜色时离左边框的距离（落在内边距里，不会碰到文字与图标）

// 隐藏的宿主窗口，析构时销毁
class CsHost
{
public:
    CsHost()
    {
        RECT rc = { 0, 0, kCsListW, kCsListH };
        m_host.Create(NULL, rc, _T("ChatCardStyleTests"), WS_POPUP);
    }

    ~CsHost()
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

// 消息列表的离屏画布：与列表同尺寸
class CsListCanvas
{
public:
    CsListCanvas()
        : m_dc(NULL), m_bmp(NULL), m_old(NULL), m_bits(NULL)
    {
        HDC screen = ::GetDC(NULL);
        m_dc = ::CreateCompatibleDC(screen);
        ::ReleaseDC(NULL, screen);
        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kCsListW;
        bi.bmiHeader.biHeight = -kCsListH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = kCsBitsPerPixel;
        bi.bmiHeader.biCompression = BI_RGB;
        m_bmp = ::CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &m_bits, NULL, 0);
        m_old = ::SelectObject(m_dc, m_bmp);
    }

    ~CsListCanvas()
    {
        ::SelectObject(m_dc, m_old);
        ::DeleteObject(m_bmp);
        ::DeleteDC(m_dc);
    }

    // 把列表画上来
    void Paint(ChatMessageList& list)
    {
        RECT all = { 0, 0, kCsListW, kCsListH };
        list.OnPaint(m_dc, all);
        ::GdiFlush();
    }

    // 取一个像素的颜色
    COLORREF At(int x, int y) const
    {
        const unsigned int px = ((const unsigned int*)m_bits)[y * kCsListW + x];
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
                    box.left = x;
                    box.top = y;
                    box.right = x + 1;
                    box.bottom = y + 1;
                    found = true;
                }
                else
                {
                    box.left = (x < box.left) ? x : box.left;
                    box.top = (y < box.top) ? y : box.top;
                    box.right = (x + 1 > box.right) ? x + 1 : box.right;
                    box.bottom = (y + 1 > box.bottom) ? y + 1 : box.bottom;
                }
            }
        }
        return box;
    }

private:
    HDC     m_dc;     // 内存设备上下文
    HBITMAP m_bmp;    // DIB 位图
    HGDIOBJ m_old;    // 选入位图前的原位图
    void*   m_bits;   // 像素数据（BGRA，自上而下）
};

// 一张排好版、画好的卡片：列表、气泡、卡片与按钮的矩形
struct CsCard
{
    RECT m_card;     // 卡片的外接矩形（按边框色找出）
    RECT m_button;   // 唯一那个按钮的外接矩形（按主要按钮底色或查看按钮边框色找出）；没有按钮时为空
};

// 把一条消息排进列表、挂上宿主、画一遍，找出卡片与按钮
CsCard CsLayCard(DuiHost& host, ChatMessageList& list, const ChatMessage& msg)
{
    list.AppendMessage(msg);
    list.AttachToHost(&host);
    RECT rc = { 0, 0, kCsListW, kCsListH };
    list.ForceLayout(rc);
    CsListCanvas canvas;
    canvas.Paint(list);
    CsCard out;
    const RECT all = { 0, 0, kCsListW, kCsListH };
    out.m_card = canvas.BoundsOf(cardstyle::kCardBorder, all);
    out.m_button = canvas.BoundsOf(cardstyle::kPrimaryFill, out.m_card);
    if (::IsRectEmpty(&out.m_button))
        out.m_button = canvas.BoundsOf(cardstyle::kViewBorder, out.m_card);
    return out;
}

// 卡片内部（离左边框 kCsInset、垂直居中处）的颜色
COLORREF CsCardInside(DuiHost& host, ChatMessageList& list, const CsCard& card)
{
    (void)host;
    CsListCanvas canvas;
    canvas.Paint(list);
    return canvas.At(card.m_card.left + kCsInset, (card.m_card.top + card.m_card.bottom) / 2);
}

// 列表里唯一那条消息的气泡
ChatBubble* CsOnlyBubble(ChatMessageList& list)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().kind != kMsgTime)
            return b;
    }
    FAIL("no card bubble in list");
    return NULL;
}

// 矩形中心
POINT CsCenter(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

// 在宿主窗口的消息队列里取一条指定的消息并释放载荷；取到返回 true
template <typename Payload>
bool CsTakePosted(HWND hwnd, UINT msgId)
{
    MSG m;
    bool got = false;
    while (::PeekMessageW(&m, hwnd, msgId, msgId, PM_REMOVE))
    {
        delete reinterpret_cast<Payload*>(m.lParam);
        got = true;
    }
    return got;
}

// 断言一张卡片宽 300、内部白色、按钮（如有）完整落在卡片内
void CsCheckCardBox(DuiHost& host, ChatMessageList& list, const CsCard& card, bool hasButton)
{
    CHECK(card.m_card.right - card.m_card.left == cardstyle::kCardWidth);
    CHECK(CsCardInside(host, list, card) == cardstyle::kCardFill);
    CHECK((::IsRectEmpty(&card.m_button) != FALSE) == !hasButton);
    if (hasButton)
    {
        CHECK(card.m_button.left > card.m_card.left);
        CHECK(card.m_button.right < card.m_card.right);
        CHECK(card.m_button.top > card.m_card.top);
        CHECK(card.m_button.bottom < card.m_card.bottom);
    }
}

} // namespace

TEST_CASE("CS7 卡片尺寸与底色：共享与通话", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());

    //屏幕共享：对方发起有「加入」；本人发起的没有按钮，同样是白底、宽 300
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyScreenShareCard(m, kCsShareContent, CString(L"李晓雯"), kScreenShareRoomActive);
        const CsCard card = CsLayCard(host.Host(), list, m);
        CsCheckCardBox(host.Host(), list, card, true);
    }
    {
        ChatMessageList list;
        ChatMessage m = CsMineMsg();
        ApplyScreenShareCard(m, kCsShareContent, CString(L"林知夏"), kScreenShareRoomActive);
        const CsCard card = CsLayCard(host.Host(), list, m);
        CsCheckCardBox(host.Host(), list, card, false);
    }
    //一对一通话、群公告、群备忘、云端录像（本人发出）、文件共享
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyCallCard(m, kCsCallConnected, CString(L"张三"), 0);
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), false);
    }
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyAnnouncementCard(m, kCsAnnouncement, CString(L"王建国"));
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), true);
    }
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyMemoCard(m, kCsMemo, CString(L"李晓雯"), memo::NowLocalMinute());
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), true);
    }
    {
        ChatMessageList list;
        ChatMessage m = CsMineMsg();
        ApplyCloudRecordCard(m, kCsCloudRecord, CString(L"林知夏"));
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), true);
    }
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyFileShareCard(m, kCsShareFolderContent, CString(L"李晓雯"), CsLive(true, share_state_available, 0));
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), true);
    }
}

TEST_CASE("CS8 按钮命中：共享与通话", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    const HWND hwnd = host.Host().m_hWnd;

    //屏幕共享：点「加入」请宿主加入共享
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyScreenShareCard(m, kCsShareContent, CString(L"李晓雯"), kScreenShareRoomActive);
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        const POINT pt = CsCenter(card.m_button);
        CHECK(b->OnSetCursor(pt));
        b->OnLButtonDown(pt, MK_LBUTTON);
        b->OnLButtonUp(pt, 0);
        CHECK(CsTakePosted<ScreenShareJoinRequest>(hwnd, WM_CHAT_JOIN_SCREENSHARE));

        //多选模式下不显示手型
        list.SetMultiSelectMode(true);
        CsListCanvas canvas;
        canvas.Paint(list);
        CHECK_FALSE(b->OnSetCursor(CsCenter(canvas.BoundsOf(cardstyle::kPrimaryFill, card.m_card))));
    }

    //群公告：点「查看」请宿主打开公告
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyAnnouncementCard(m, kCsAnnouncement, CString(L"王建国"));
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        b->OnLButtonDown(CsCenter(card.m_button), MK_LBUTTON);
        b->OnLButtonUp(CsCenter(card.m_button), 0);
        CHECK(CsTakePosted<InfoCardOpenRequest>(hwnd, WM_CHAT_OPEN_ANNOUNCEMENT_CARD));
    }

    //群备忘：可用时点「查看」打开备忘；不可用时按钮置灰，点了没有反应、不显示手型
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyMemoCard(m, kCsMemo, CString(L"李晓雯"), memo::NowLocalMinute());
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        b->OnLButtonDown(CsCenter(card.m_button), MK_LBUTTON);
        b->OnLButtonUp(CsCenter(card.m_button), 0);
        CHECK(CsTakePosted<InfoCardOpenRequest>(hwnd, WM_CHAT_OPEN_MEMO_CARD));

        ChatMessageList grayList;
        ChatMessage gray = m;
        REQUIRE(SetMemoCardAvailable(gray, false));
        CsLayCard(host.Host(), grayList, gray);
        ChatBubble* gb = CsOnlyBubble(grayList);
        const POINT pt = CsCenter(card.m_button);
        CHECK_FALSE(gb->OnSetCursor(pt));
        gb->OnLButtonDown(pt, MK_LBUTTON);
        gb->OnLButtonUp(pt, 0);
        CHECK_FALSE(CsTakePosted<InfoCardOpenRequest>(hwnd, WM_CHAT_OPEN_MEMO_CARD));
    }

    //云端录像：点「播放」请宿主播放
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyCloudRecordCard(m, kCsCloudRecord, CString(L"张三"));
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        b->OnLButtonDown(CsCenter(card.m_button), MK_LBUTTON);
        b->OnLButtonUp(CsCenter(card.m_button), 0);
        CHECK(CsTakePosted<CloudRecordOpenRequest>(hwnd, WM_CHAT_OPEN_CLOUD_RECORD_CARD));
    }

    //文件共享：点「打开」请宿主打开共享
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyFileShareCard(m, kCsShareFolderContent, CString(L"李晓雯"), CsLive(true, share_state_available, 0));
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        b->OnLButtonDown(CsCenter(card.m_button), MK_LBUTTON);
        b->OnLButtonUp(CsCenter(card.m_button), 0);
        CHECK(CsTakePosted<long long>(hwnd, WM_CHAT_OPEN_SHARE));
    }
}

namespace {

// ---- 邀请：样例 content 与参数，照 ChatCardShotTests.cpp ----

const char* const kCsGroupInvite =
    "{\"inviteid\":\"1001\",\"groupid\":\"3002\",\"groupname\":\"产品研发部周会群\",\"inviter\":\"张三\","
    "\"invitee\":\"林知夏\",\"state\":0,\"membercount\":32}";
const uint64_t kCsSecretSession = 9001;        // 密聊会话号（任意非零值）
const uint32_t kCsSecretRemain = 58;           // 密聊邀请剩余秒数
const uint32_t kCsSecretRemainLater = 45;      // 倒计时走了一会儿之后的剩余秒数
const int kCsSecretProtectOk = 1;              // 对方能防截图（ApplySecretInviteCard 的 peerProtect，大于 0 即可）

// 断言按钮列表的文字、级别与动作（按显示顺序从左到右）
void CheckButtons(const ChatMessage& m, size_t count, const wchar_t* const* texts, const int* levels, const int* actions)
{
    const std::vector<ChatCardButton> b = ChatCardButtonsOf(m);
    REQUIRE(b.size() == count);
    for (size_t i = 0; i < count; ++i)
    {
        CHECK(b[i].m_text == CString(texts[i]));
        CHECK(b[i].m_level == levels[i]);
        CHECK(b[i].m_action == actions[i]);
        CHECK(b[i].m_enabled);
    }
}

} // namespace

TEST_CASE("CS3 状态标签：邀请", "[cardstyle]")
{
    //入群邀请：待加入、等待审批为橙，已加入为绿，未通过审批、过期、失效为灰
    {
        ChatMessage invitee = CsPeerMsg();
        ApplyGroupInviteCard(invitee, kCsGroupInvite);
        CheckChip(invitee, kCardChipPending, L"邀请 7 天内有效", L"");
        ChatMessage inviter = CsMineMsg();
        ApplyGroupInviteCard(inviter, kCsGroupInvite);
        CheckChip(inviter, kCardChipPending, L"等待对方确认", L"");

        ApplyGroupInviteState(invitee, group_invite_approving);
        CHECK(invitee.cardChipTone == kCardChipPending);
        ApplyGroupInviteState(invitee, group_invite_joined);
        CheckChip(invitee, kCardChipLive, L"已加入", L"");
        ApplyGroupInviteState(inviter, group_invite_joined);
        CheckChip(inviter, kCardChipLive, L"对方已加入", L"");
        const int endStates[] = { group_invite_rejected, group_invite_expired, group_invite_invalid };
        for (size_t i = 0; i < 3; ++i)
        {
            ApplyGroupInviteState(invitee, endStates[i]);
            CHECK(invitee.cardChipTone == kCardChipEnded);
            CHECK(invitee.cardChipText == invitee.inviteStatus);
        }
        //内容不可用：没有状态文字，也不画标签
        ChatMessage bad = CsPeerMsg();
        ApplyGroupInviteCard(bad, kCsBadContent);
        CheckChip(bad, kCardChipNone, L"", L"");
    }

    //密聊邀请：剩余秒数为橙，倒计时刷新时跟着变
    {
        ChatMessage invitee;
        ApplySecretInviteCard(invitee, false, CString(L"张三"), kCsSecretSession, kCsSecretRemain, kCsSecretProtectOk);
        CheckChip(invitee, kCardChipPending, L"剩余 58 秒", L"");
        UpdateSecretInviteRemain(invitee, kCsSecretRemainLater);
        CheckChip(invitee, kCardChipPending, L"剩余 45 秒", L"");
    }
}

TEST_CASE("CS4 按钮：邀请", "[cardstyle]")
{
    //入群邀请：被邀请人待加入时「加入群聊」（主要）；点过之后写「正在加入…」并置灰；其余状态与邀请人一侧没有按钮
    {
        ChatMessage invitee = CsPeerMsg();
        ApplyGroupInviteCard(invitee, kCsGroupInvite);
        CheckOneButton(invitee, L"加入群聊", kCardButtonPrimary, true);
        SetGroupInviteJoining(invitee, true);
        CheckOneButton(invitee, L"正在加入…", kCardButtonPrimary, false);
        ApplyGroupInviteState(invitee, group_invite_joined);
        CHECK(ChatCardButtonsOf(invitee).empty());
        ChatMessage inviter = CsMineMsg();
        ApplyGroupInviteCard(inviter, kCsGroupInvite);
        CHECK(ChatCardButtonsOf(inviter).empty());
        CHECK_FALSE(ChatCardTitleIsTypeName(invitee));
    }

    //密聊邀请：被邀请方左「忽略」（次要）右「同意」（主要），动作取值即原来的按钮编号（0 同意、1 忽略）；发起方只有「取消」
    {
        ChatMessage invitee;
        ApplySecretInviteCard(invitee, false, CString(L"张三"), kCsSecretSession, kCsSecretRemain, kCsSecretProtectOk);
        const wchar_t* const texts[] = { L"忽略", L"同意" };
        const int levels[] = { kCardButtonSecondary, kCardButtonPrimary };
        const int actions[] = { 1, 0 };
        CheckButtons(invitee, 2, texts, levels, actions);
        CHECK(SecretInviteButtonAction(invitee.secretInviteButtons, actions[0]) == kSecretInviteIgnore);
        CHECK(SecretInviteButtonAction(invitee.secretInviteButtons, actions[1]) == kSecretInviteAccept);

        ChatMessage inviter;
        ApplySecretInviteCard(inviter, true, CString(L"张三"), kCsSecretSession, kCsSecretRemain, kCsSecretProtectOk);
        const wchar_t* const cancelText[] = { L"取消" };
        const int cancelLevel[] = { kCardButtonSecondary };
        const int cancelAction[] = { 0 };
        CheckButtons(inviter, 1, cancelText, cancelLevel, cancelAction);
        CHECK(SecretInviteButtonAction(inviter.secretInviteButtons, 0) == kSecretInviteCancel);
    }
}

TEST_CASE("CS7 卡片尺寸与底色：邀请", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyGroupInviteCard(m, kCsGroupInvite);
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), true);
    }
    {
        ChatMessageList list;
        ChatMessage m;
        ApplySecretInviteCard(m, false, CString(L"张三"), kCsSecretSession, kCsSecretRemain, kCsSecretProtectOk);
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), true);
    }
    //发起方的卡片在右侧，同样是白底（只有一个次要按钮，按颜色找不到胶囊，不查按钮）
    {
        ChatMessageList list;
        ChatMessage m;
        ApplySecretInviteCard(m, true, CString(L"张三"), kCsSecretSession, kCsSecretRemain, kCsSecretProtectOk);
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), false);
    }
}

TEST_CASE("CS8 按钮命中：邀请", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    const HWND hwnd = host.Host().m_hWnd;

    //入群邀请：点「加入群聊」才请宿主加入；点卡片上别处没有反应；点过之后按钮置灰，点了没有反应、不显示手型
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        ApplyGroupInviteCard(m, kCsGroupInvite);
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        const POINT body = { card.m_card.left + kCsInset * 4, card.m_card.top + kCsInset * 4 };
        CHECK_FALSE(b->OnSetCursor(body));
        b->OnLButtonDown(body, MK_LBUTTON);
        b->OnLButtonUp(body, 0);
        CHECK_FALSE(CsTakePosted<GroupInviteJoinRequest>(hwnd, WM_CHAT_JOIN_GROUP_INVITE));
        const POINT pt = CsCenter(card.m_button);
        CHECK(b->OnSetCursor(pt));
        b->OnLButtonDown(pt, MK_LBUTTON);
        b->OnLButtonUp(pt, 0);
        CHECK(CsTakePosted<GroupInviteJoinRequest>(hwnd, WM_CHAT_JOIN_GROUP_INVITE));

        ChatMessageList joiningList;
        ChatMessage joining = m;
        SetGroupInviteJoining(joining, true);
        CsLayCard(host.Host(), joiningList, joining);
        ChatBubble* jb = CsOnlyBubble(joiningList);
        CHECK_FALSE(jb->OnSetCursor(pt));
        jb->OnLButtonDown(pt, MK_LBUTTON);
        jb->OnLButtonUp(pt, 0);
        CHECK_FALSE(CsTakePosted<GroupInviteJoinRequest>(hwnd, WM_CHAT_JOIN_GROUP_INVITE));
    }

    //密聊邀请：右边「同意」、左边「忽略」各自投递对应的动作
    {
        ChatMessageList list;
        ChatMessage m;
        ApplySecretInviteCard(m, false, CString(L"张三"), kCsSecretSession, kCsSecretRemain, kCsSecretProtectOk);
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        const POINT accept = CsCenter(card.m_button);
        b->OnLButtonDown(accept, MK_LBUTTON);
        b->OnLButtonUp(accept, 0);
        MSG posted;
        REQUIRE(::PeekMessageW(&posted, hwnd, WM_CHAT_SECRET_INVITE_ACTION, WM_CHAT_SECRET_INVITE_ACTION, PM_REMOVE));
        CHECK(posted.wParam == (WPARAM)kSecretInviteAccept);
        delete reinterpret_cast<unsigned long long*>(posted.lParam);

        //「忽略」紧挨在「同意」左边，相隔一个按钮间距
        const POINT ignore = { card.m_button.left - cardstyle::kButtonGap - kCsInset, accept.y };
        CHECK(b->OnSetCursor(ignore));
        b->OnLButtonDown(ignore, MK_LBUTTON);
        b->OnLButtonUp(ignore, 0);
        REQUIRE(::PeekMessageW(&posted, hwnd, WM_CHAT_SECRET_INVITE_ACTION, WM_CHAT_SECRET_INVITE_ACTION, PM_REMOVE));
        CHECK(posted.wParam == (WPARAM)kSecretInviteIgnore);
        delete reinterpret_cast<unsigned long long*>(posted.lParam);
    }

    //发起方：最右边的「取消」
    {
        ChatMessageList list;
        ChatMessage m;
        ApplySecretInviteCard(m, true, CString(L"张三"), kCsSecretSession, kCsSecretRemain, kCsSecretProtectOk);
        const CsCard card = CsLayCard(host.Host(), list, m);
        ChatBubble* b = CsOnlyBubble(list);
        const POINT cancel = { card.m_card.right - cardstyle::kCardPadH - kCsInset,
                               card.m_card.bottom - cardstyle::kFooterH / 2 };
        CHECK(b->OnSetCursor(cancel));
        b->OnLButtonDown(cancel, MK_LBUTTON);
        b->OnLButtonUp(cancel, 0);
        MSG posted;
        REQUIRE(::PeekMessageW(&posted, hwnd, WM_CHAT_SECRET_INVITE_ACTION, WM_CHAT_SECRET_INVITE_ACTION, PM_REMOVE));
        CHECK(posted.wParam == (WPARAM)kSecretInviteCancel);
        delete reinterpret_cast<unsigned long long*>(posted.lParam);
    }
}

namespace {

// ---- 接龙与合并转发：样例 content 与参数，照 ChatCardShotTests.cpp ----

const int64_t kCsSelfUid = 100001;               // 当前用户 userid
const int64_t kCsOtherUidBase = 200000;          // 其他参与者 userid 的起点（依次加 1）
const long long kCsCardMsgid = 990500;           // 卡片的服务器 msgid（接龙卡片点击时要求大于 0）
const long long kCsMsPerSecond = 1000;           // 秒与毫秒的换算
const long long kCsHourMs = 3600LL * 1000LL;     // 一小时的毫秒数
const long long kCsRelayLeadMs = 52 * kCsHourMs; // 截止时间比现在晚 52 小时：接龙仍在进行中
const int kCsRelayEntries = 3;                   // 样例接龙的条数
const int kCsRelayFull = 200;                    // 接龙名额上限（net::kRelayMaxParticipants）

// 接龙卡片的名字解析：当前用户写「林知夏」，其余写「成员」
class CsRelayNames : public IRelayNameResolver
{
public:
    // 取 userid 对应的显示名
    virtual CString ResolveName(int64_t userid) const
    {
        return (userid == kCsSelfUid) ? CString(L"林知夏") : CString(L"成员");
    }
};

// 现在（Unix 毫秒）
long long CsNowMs()
{
    return static_cast<long long>(::time(NULL)) * kCsMsPerSecond;
}

// 拼一张接龙卡片的 content。
//   state：      卡片状态（relay_state）
//   deadline：   截止时间（Unix 毫秒），0 表示不限
//   items：      名单条数
//   includeSelf：名单第一条是不是当前用户
std::string CsRelayContent(int state, long long deadline, int items, bool includeSelf)
{
    std::string s = "{\"title\":\"周五团建报名\",\"note\":\"地点：西湖边的农家乐，费用 AA。\",\"deadline\":";
    s += std::to_string(deadline);
    s += ",\"items\":[";
    for (int i = 0; i < items; ++i)
    {
        if (i > 0)
            s += ",";
        const long long uid = (includeSelf && i == 0) ? kCsSelfUid : (kCsOtherUidBase + i);
        s += "{\"userid\":\"" + std::to_string(uid) + "\",\"text\":\"本人+1\"}";
    }
    s += "],\"state\":" + std::to_string(state) + ",\"closedby\":\"0\",\"closetime\":0}";
    return s;
}

// 对方发来的一张接龙卡片
ChatMessage CsRelay(const std::string& content)
{
    ChatMessage m = CsPeerMsg();
    m.msgid = kCsCardMsgid;
    CsRelayNames names;
    ApplyRelayCard(m, content, kCsSelfUid, false, CsNowMs(), names);
    return m;
}

const char* const kCsMergedContent =
    "{\"title\":\"产品研发部的聊天记录\",\"messages\":["
    "{\"msgid\":\"1\",\"from\":\"100002\",\"msgtype\":1,\"content\":{\"text\":\"周五下午的版本评审改到 3 点\"},\"fromname\":\"张三\"},"
    "{\"msgid\":\"2\",\"from\":\"100003\",\"msgtype\":1,\"content\":{\"text\":\"收到\"},\"fromname\":\"李晓雯\"}]}";

} // namespace

TEST_CASE("CS3 状态标签：接龙与合并转发", "[cardstyle]")
{
    const long long now = CsNowMs();
    //进行中：绿色「进行中」，信息行写人数，副标题写截止时间
    {
        const ChatMessage m = CsRelay(CsRelayContent(relay_state_open, now + kCsRelayLeadMs, kCsRelayEntries, false));
        CheckChip(m, kCardChipLive, L"进行中", L"共 3 人参与");
        CHECK(m.relayDeadlineText.Find(L"截止 ") == 0);
        //原来的状态行不变
        CHECK(m.relayStatus.Find(L"共 3 人参与") == 0);
    }
    //不限截止时间：没有副标题
    {
        const ChatMessage m = CsRelay(CsRelayContent(relay_state_open, 0, kCsRelayEntries, false));
        CHECK(m.relayDeadlineText.IsEmpty());
        CHECK(m.cardChipTone == kCardChipLive);
    }
    //已结束、已截止、名额已满且本人未参与：灰色
    {
        const ChatMessage closed = CsRelay(CsRelayContent(relay_state_closed, now + kCsRelayLeadMs, kCsRelayEntries, false));
        CheckChip(closed, kCardChipEnded, L"已结束", L"共 3 人参与");
        const ChatMessage expired = CsRelay(CsRelayContent(relay_state_open, now - kCsHourMs, kCsRelayEntries, false));
        CheckChip(expired, kCardChipEnded, L"已截止", L"共 3 人参与");
        const ChatMessage full = CsRelay(CsRelayContent(relay_state_open, now + kCsRelayLeadMs, kCsRelayFull, false));
        CheckChip(full, kCardChipEnded, L"已满", L"共 200 人参与");
        //本人已参与时满员也照常是进行中
        const ChatMessage fullJoined = CsRelay(CsRelayContent(relay_state_open, now + kCsRelayLeadMs, kCsRelayFull, true));
        CHECK(fullJoined.cardChipTone == kCardChipLive);
    }
    //合并转发：没有状态标签
    {
        ChatMessage m = CsPeerMsg();
        ApplyMergedCard(m, kCsMergedContent);
        CheckChip(m, kCardChipNone, L"", L"");
    }
}

TEST_CASE("CS4 按钮：接龙与合并转发", "[cardstyle]")
{
    const long long now = CsNowMs();
    //接龙：没参与时「参与接龙」（主要）；参与过时「修改我的」（次要）；已结束、已截止、名额已满时没有按钮
    CheckOneButton(CsRelay(CsRelayContent(relay_state_open, now + kCsRelayLeadMs, kCsRelayEntries, false)), L"参与接龙",
                   kCardButtonPrimary, true);
    CheckOneButton(CsRelay(CsRelayContent(relay_state_open, now + kCsRelayLeadMs, kCsRelayEntries, true)), L"修改我的",
                   kCardButtonSecondary, true);
    CHECK(ChatCardButtonsOf(CsRelay(CsRelayContent(relay_state_closed, now + kCsRelayLeadMs, kCsRelayEntries, false))).empty());
    CHECK(ChatCardButtonsOf(CsRelay(CsRelayContent(relay_state_open, now - kCsHourMs, kCsRelayEntries, false))).empty());
    CHECK(ChatCardButtonsOf(CsRelay(CsRelayContent(relay_state_open, now + kCsRelayLeadMs, kCsRelayFull, false))).empty());
    //合并转发：没有按钮，点整张卡片打开
    ChatMessage merged = CsPeerMsg();
    ApplyMergedCard(merged, kCsMergedContent);
    CHECK(ChatCardButtonsOf(merged).empty());
    CHECK_FALSE(ChatCardTitleIsTypeName(merged));
}

TEST_CASE("CS7 卡片尺寸与底色：接龙与合并转发", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    {
        ChatMessageList list;
        const ChatMessage m = CsRelay(CsRelayContent(relay_state_open, CsNowMs() + kCsRelayLeadMs, kCsRelayEntries, false));
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), true);
    }
    {
        ChatMessageList list;
        ChatMessage m = CsMineMsg();
        m.msgid = kCsCardMsgid;
        ApplyMergedCard(m, kCsMergedContent);
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, m), false);
    }
}

TEST_CASE("CS8 按钮命中：接龙与合并转发", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    const HWND hwnd = host.Host().m_hWnd;

    //接龙：点「参与接龙」投递「参与」，点卡片别处投递「打开详情」
    {
        ChatMessageList list;
        const ChatMessage m = CsRelay(CsRelayContent(relay_state_open, CsNowMs() + kCsRelayLeadMs, kCsRelayEntries, false));
        const CsCard card = CsLayCard(host.Host(), list, m);
        REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
        ChatBubble* b = CsOnlyBubble(list);
        const POINT btn = CsCenter(card.m_button);
        b->OnLButtonDown(btn, MK_LBUTTON);
        b->OnLButtonUp(btn, 0);
        MSG posted;
        REQUIRE(::PeekMessageW(&posted, hwnd, WM_CHAT_RELAY_ACTION, WM_CHAT_RELAY_ACTION, PM_REMOVE));
        CHECK(posted.wParam == (WPARAM)kChatRelayActionJoin);
        delete reinterpret_cast<long long*>(posted.lParam);

        const POINT body = { card.m_card.left + kCsInset * 4, card.m_card.top + kCsInset * 4 };
        b->OnLButtonDown(body, MK_LBUTTON);
        b->OnLButtonUp(body, 0);
        REQUIRE(::PeekMessageW(&posted, hwnd, WM_CHAT_RELAY_ACTION, WM_CHAT_RELAY_ACTION, PM_REMOVE));
        CHECK(posted.wParam == (WPARAM)kChatRelayActionOpen);
        delete reinterpret_cast<long long*>(posted.lParam);
    }

    //合并转发：点卡片打开聊天记录查看窗
    {
        ChatMessageList list;
        ChatMessage m = CsPeerMsg();
        m.msgid = kCsCardMsgid;
        ApplyMergedCard(m, kCsMergedContent);
        const CsCard card = CsLayCard(host.Host(), list, m);
        ChatBubble* b = CsOnlyBubble(list);
        const POINT body = CsCenter(card.m_card);
        CHECK(b->OnSetCursor(body));
        b->OnLButtonDown(body, MK_LBUTTON);
        b->OnLButtonUp(body, 0);
        CHECK(CsTakePosted<std::string>(hwnd, WM_CHAT_OPEN_MERGED_RECORD));
    }
}

namespace {

// ---- 文件：样例参数 ----

const long long kCsFileBytes = 4404019;          // 样例文件的字节数（约 4.2 MB）
const long long kCsFileMsgid = 990600;           // 收到的文件的服务器 msgid
const int kCsFilePct = 46;                       // 传输中的进度（百分比）
const int kCsLinkLocal = 1;                      // 在线直传的链路：局域网直连

// 收到的文件（单聊，有「接收 / 拒绝」流程）
ChatMessage CsIncomingFile(int state)
{
    ChatMessage m = MakeIncomingFileMessage(false, CString(L"2026 年第四季度规划.pptx"), kCsFileBytes, kCsFileMsgid, "1",
                                            "md5", state);
    m.fileProgress = kCsFilePct;
    return m;
}

// 本人发出的文件（带客户端消息 ID 与本地源文件，失败后可以重新发送）
ChatMessage CsOutgoingFile(int state)
{
    ChatMessage m = MakeFileMessage(true, CString(L"产品需求文档 v3.2.docx"), kCsFileBytes, CString(L"C:\\docs\\v3.2.docx"),
                                    "cmid-cs-file", state);
    m.fileProgress = kCsFilePct;
    return m;
}

// 断言文件卡片的状态标签
void CheckFileStatus(const ChatMessage& m, const wchar_t* text, int tone)
{
    CString t;
    int k = -1;
    FileCardStatus(m, t, k);
    CHECK(t == CString(text));
    CHECK(k == tone);
}

// 断言文件卡片的按钮（按显示顺序从左到右）
void CheckFileButtons(const ChatMessage& m, size_t count, const int* actions, const int* levels)
{
    const std::vector<ChatCardButton> b = ChatCardButtonsOf(m);
    REQUIRE(b.size() == count);
    for (size_t i = 0; i < count; ++i)
    {
        INFO("button " << i);
        CHECK(b[i].m_action == actions[i]);
        CHECK(b[i].m_level == levels[i]);
        CHECK(b[i].m_text == FileCardLinkLabel(m, actions[i]));
    }
}

} // namespace

TEST_CASE("CS3 状态标签：文件", "[cardstyle]")
{
    //收到的文件
    CheckFileStatus(CsIncomingFile(kFilePendingAccept), L"等待接收", kCardChipPending);
    CheckFileStatus(CsIncomingFile(kFileDownloading), L"下载中", kCardChipPending);
    CheckFileStatus(CsIncomingFile(kFileNormal), L"已完成", kCardChipLive);
    CheckFileStatus(CsIncomingFile(kFileFailed), L"传输失败", kCardChipBad);
    CheckFileStatus(CsIncomingFile(kFileCancelled), L"已取消", kCardChipEnded);
    CheckFileStatus(CsIncomingFile(kFileRejected), L"已拒绝接收", kCardChipEnded);
    CheckFileStatus(CsIncomingFile(kFileDeleted), L"文件已删除，无法下载", kCardChipEnded);
    //群聊与聊天记录里的文件没有接收流程，还没下载时写「未下载」
    {
        ChatMessage group = CsIncomingFile(kFilePendingAccept);
        group.fileDirectDownload = true;
        CheckFileStatus(group, L"未下载", kCardChipEnded);
    }
    //本人发出的文件：传送中、传送完成；对方有过动作后改说对方那边的进展
    CheckFileStatus(CsOutgoingFile(kFileUploading), L"传送中", kCardChipPending);
    CheckFileStatus(CsOutgoingFile(kFileNormal), L"传送完成", kCardChipLive);
    {
        ChatMessage sent = CsOutgoingFile(kFileNormal);
        sent.filePeerState = kFilePeerReceiving;
        CheckFileStatus(sent, L"对方正在接收", kCardChipPending);
        sent.filePeerState = kFilePeerReceived;
        CheckFileStatus(sent, L"对方已接收", kCardChipLive);
        sent.filePeerState = kFilePeerRejected;
        CheckFileStatus(sent, L"对方已拒绝接收", kCardChipEnded);
    }
    //在线直传：等待对方接收为橙，接收中不带百分比
    {
        ChatMessage waiting = CsOutgoingFile(kFileDirectWaiting);
        waiting.fileDirect = true;
        CheckFileStatus(waiting, L"等待对方接收", kCardChipPending);
        ChatMessage receiving = CsIncomingFile(kFileDownloading);
        receiving.fileDirect = true;
        CheckFileStatus(receiving, L"接收中", kCardChipPending);
    }
}

TEST_CASE("CS4 按钮：文件", "[cardstyle]")
{
    //单聊里对方发来、待接收：左「拒绝」（次要）右「接收」（主要）
    {
        const int actions[] = { kChatFileReject, kChatFileAccept };
        const int levels[] = { kCardButtonSecondary, kCardButtonPrimary };
        CheckFileButtons(CsIncomingFile(kFilePendingAccept), 2, actions, levels);
    }
    //群聊里的文件只有「下载」（主要）
    {
        ChatMessage group = CsIncomingFile(kFilePendingAccept);
        group.fileDirectDownload = true;
        const int actions[] = { kChatFileAccept };
        const int levels[] = { kCardButtonPrimary };
        CheckFileButtons(group, 1, actions, levels);
    }
    //传输中只有「取消」（次要）
    {
        const int actions[] = { kChatFileCancel };
        const int levels[] = { kCardButtonSecondary };
        CheckFileButtons(CsOutgoingFile(kFileUploading), 1, actions, levels);
        CheckFileButtons(CsIncomingFile(kFileDownloading), 1, actions, levels);
    }
    //完成：「另存为」「打开所在文件夹」（次要）在左，「打开文件」（查看）在最右
    {
        const int actions[] = { kChatFileSaveAs, kChatFileOpenFolder, kChatFileOpen };
        const int levels[] = { kCardButtonSecondary, kCardButtonSecondary, kCardButtonView };
        CheckFileButtons(CsIncomingFile(kFileNormal), 3, actions, levels);
    }
    //失败：重新发送 / 重新下载（查看）
    {
        const int actions[] = { kChatFileRetry };
        const int levels[] = { kCardButtonView };
        CheckFileButtons(CsOutgoingFile(kFileFailed), 1, actions, levels);
        CHECK(ChatCardButtonsOf(CsOutgoingFile(kFileFailed))[0].m_text == CString(L"\x21BB 重新发送"));
    }
    //在线直传等待对方接收：左「取消」（次要）右「发送离线文件」（主要）
    {
        ChatMessage waiting = CsOutgoingFile(kFileDirectWaiting);
        waiting.fileDirect = true;
        const int actions[] = { kChatFileCancel, kChatFileSendOffline };
        const int levels[] = { kCardButtonSecondary, kCardButtonPrimary };
        CheckFileButtons(waiting, 2, actions, levels);
    }
    //已删除：没有按钮
    CHECK(ChatCardButtonsOf(CsIncomingFile(kFileDeleted)).empty());
}

TEST_CASE("CS7 卡片尺寸与底色：文件", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    //对方发来、待接收（有主要按钮「接收」）
    {
        ChatMessageList list;
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, CsIncomingFile(kFilePendingAccept)), true);
    }
    //本人发出、已完成：同样是白底（最右是查看按钮「打开文件」）
    {
        ChatMessageList list;
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, CsOutgoingFile(kFileNormal)), true);
    }
    //已删除：没有按钮，仍是白底
    {
        ChatMessageList list;
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, CsIncomingFile(kFileDeleted)), false);
    }
}

TEST_CASE("CS8 按钮命中：文件", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    const HWND hwnd = host.Host().m_hWnd;

    ChatMessageList list;
    const CsCard card = CsLayCard(host.Host(), list, CsIncomingFile(kFilePendingAccept));
    REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
    ChatBubble* b = CsOnlyBubble(list);
    const POINT accept = CsCenter(card.m_button);
    CHECK(b->OnSetCursor(accept));
    b->OnLButtonDown(accept, MK_LBUTTON);
    b->OnLButtonUp(accept, 0);
    MSG posted;
    REQUIRE(::PeekMessageW(&posted, hwnd, WM_CHAT_FILE_ACTION, WM_CHAT_FILE_ACTION, PM_REMOVE));
    ChatFileActionData* data = reinterpret_cast<ChatFileActionData*>(posted.lParam);
    CHECK(data->action == kChatFileAccept);
    delete data;

    //多选模式下不显示手型，也不投递
    list.SetMultiSelectMode(true);
    CsListCanvas canvas;
    canvas.Paint(list);
    //多选模式下内容整体右移，按钮跟着移动：从卡片原来的左边到列表右边重新找一次「接收」（头像在卡片左侧，不在这个范围里）
    const POINT shifted = CsCenter(canvas.BoundsOf(cardstyle::kPrimaryFill, CsRect(card.m_card.left, card.m_card.top, kCsListW, card.m_card.bottom)));
    CHECK_FALSE(b->OnSetCursor(shifted));
}

TEST_CASE("CS9 进度条：文件上传、下载都是品牌橙", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    const ChatMessage cases[] = { CsOutgoingFile(kFileUploading), CsIncomingFile(kFileDownloading) };
    for (size_t i = 0; i < 2; ++i)
    {
        INFO("case " << i);
        ChatMessageList list;
        const CsCard card = CsLayCard(host.Host(), list, cases[i]);
        //这两种状态只有次要按钮「取消」，卡片里品牌橙的像素就是进度条已完成的那一段
        CsListCanvas canvas;
        canvas.Paint(list);
        const RECT bar = canvas.BoundsOf(cardstyle::kProgressFill, card.m_card);
        REQUIRE_FALSE(::IsRectEmpty(&bar));
        CHECK(bar.bottom - bar.top <= cardstyle::kProgressH);
        CHECK(bar.right - bar.left > 0);
    }
}

TEST_CASE("CS10 记录窗的文件卡片控件与聊天窗一致", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatFileCardControl control;
    const ChatMessage m = CsIncomingFile(kFilePendingAccept);
    control.SetCardMessage(m);
    const RECT rc = CsRect(20, 20, 20 + kFileCardWidth, 20 + FileCardHeight(m));
    control.ForceLayout(rc);
    CsCanvas canvas;
    REQUIRE(canvas.Ok());
    control.OnPaint(canvas.Dc(), rc);
    //左边框为暖灰、内部为白色、主要按钮「接收」在卡片里
    CHECK(CsNear(canvas.At(rc.left, (rc.top + rc.bottom) / 2), cardstyle::kCardBorder));
    CHECK(canvas.At(rc.left + kCsInset, (rc.top + rc.bottom) / 2) == cardstyle::kCardFill);
    CHECK(kFileCardWidth == cardstyle::kCardWidth);
}

namespace {

// ---- 文件夹：样例参数，照 ChatCardShotTests.cpp ----

const int kCsFolderFiles = 128;                       // 文件数
const int kCsFolderDirs = 12;                         // 子目录数
const long long kCsFolderBytes = 2576980378LL;        // 总字节数（约 2.4 GB）
const int kCsFolderPct = 62;                          // 传输中的进度（百分比）
const int kCsFolderCurIndex = 79;                     // 正在传第几个文件
const int kCsFolderErr = error_code_folder_peer_offline;   // 样例失败原因

// 一条文件夹传输气泡
ChatMessage CsFolder(bool fromMe, int state)
{
    ChatMessage m;
    m.kind = kMsgFolder;
    m.fromMe = fromMe;
    m.folderName = L"设计稿归档 2026Q4";
    m.folderFileCount = kCsFolderFiles;
    m.folderDirCount = kCsFolderDirs;
    m.folderSizeBytes = kCsFolderBytes;
    m.folderSize = L"2.4 GB";
    m.folderState = state;
    m.folderProgress = kCsFolderPct;
    m.folderTotalBytes = kCsFolderBytes;
    m.folderSentBytes = kCsFolderBytes * kCsFolderPct / 100;
    m.folderCurFile = L"首页-高保真.fig";
    m.folderCurIndex = kCsFolderCurIndex;
    m.folderSessionId = "fs-cs-1";
    return m;
}

// 断言文件夹卡片的标签与信息行
void CheckFolderStatus(const ChatMessage& m, const wchar_t* chip, int tone, const wchar_t* meta)
{
    CString c;
    CString t;
    int k = -1;
    FolderCardStatus(m, c, k, t);
    CHECK(c == CString(chip));
    CHECK(k == tone);
    CHECK(t == CString(meta));
}

// 断言文件夹卡片的按钮动作与级别（按显示顺序从左到右）
void CheckFolderButtons(const ChatMessage& m, size_t count, const int* actions, const int* levels)
{
    const std::vector<ChatCardButton> b = ChatCardButtonsOf(m);
    REQUIRE(b.size() == count);
    for (size_t i = 0; i < count; ++i)
    {
        INFO("button " << i);
        CHECK(b[i].m_action == actions[i]);
        CHECK(b[i].m_level == levels[i]);
        CHECK(b[i].m_enabled);
    }
}

} // namespace

TEST_CASE("CS3 状态标签：文件夹", "[cardstyle]")
{
    //待接收：接收端标签写「待接收」，信息行是原来那句说明；发送端标签就是「等待对方接收…」
    CheckFolderStatus(CsFolder(false, kFolderInviting), L"待接收", kCardChipPending, L"对方想给你发送一个文件夹");
    CheckFolderStatus(CsFolder(true, kFolderInviting), L"等待对方接收…", kCardChipPending, L"");
    CheckFolderStatus(CsFolder(false, kFolderPreparing), L"正在创建目录…", kCardChipPending, L"");
    //传输中：不画标签，进度写在进度条下面
    CheckFolderStatus(CsFolder(false, kFolderTransferring), L"", kCardChipNone, L"");
    CheckFolderStatus(CsFolder(false, kFolderCompleted), L"全部接收完成", kCardChipLive, L"");
    CheckFolderStatus(CsFolder(true, kFolderCompleted), L"全部发送完成", kCardChipLive, L"");
    CheckFolderStatus(CsFolder(false, kFolderRejected), L"已拒绝接收", kCardChipEnded, L"");
    CheckFolderStatus(CsFolder(true, kFolderRejected), L"对方已拒绝接收", kCardChipEnded, L"");
    CheckFolderStatus(CsFolder(true, kFolderTimeout), L"邀请已超时", kCardChipEnded, L"");
    CheckFolderStatus(CsFolder(true, kFolderUserCanceled), L"已取消", kCardChipEnded, L"");
    //失败：红色标签；宿主带回了错误码时信息行写原因
    CheckFolderStatus(CsFolder(true, kFolderFailed), L"传输失败", kCardChipBad, L"");
    {
        ChatMessage failed = CsFolder(true, kFolderFailed);
        failed.folderErrCode = kCsFolderErr;
        CheckFolderStatus(failed, L"传输失败", kCardChipBad, FolderErrorText(kCsFolderErr));
    }
    CheckFolderStatus(CsFolder(true, kFolderExpired), L"传输失败，已超过可续传的时限", kCardChipBad, L"");
}

TEST_CASE("CS4 按钮：文件夹", "[cardstyle]")
{
    //接收端待接收：「拒绝」「另存为」（次要）在左，「接收」（主要）在最右
    {
        const int actions[] = { kChatFolderReject, kChatFolderSaveAs, kChatFolderAccept };
        const int levels[] = { kCardButtonSecondary, kCardButtonSecondary, kCardButtonPrimary };
        CheckFolderButtons(CsFolder(false, kFolderInviting), 3, actions, levels);
    }
    //发送端待接收、传输中：只有「取消」
    {
        const int actions[] = { kChatFolderCancel };
        const int levels[] = { kCardButtonSecondary };
        CheckFolderButtons(CsFolder(true, kFolderInviting), 1, actions, levels);
        CheckFolderButtons(CsFolder(false, kFolderTransferring), 1, actions, levels);
    }
    //失败且仍在保留期内：「重试」（查看）；保留期已过：不再出现（永久不可用，方案 5.1）
    {
        ChatMessage within = CsFolder(true, kFolderFailed);
        within.folderWithinRetry = true;
        const int actions[] = { kChatFolderRetry };
        const int levels[] = { kCardButtonView };
        CheckFolderButtons(within, 1, actions, levels);
        ChatMessage expired = CsFolder(true, kFolderFailed);
        expired.folderWithinRetry = false;
        CHECK(ChatCardButtonsOf(expired).empty());
    }
    //已过保留期：「重新发送」（查看）
    {
        const int actions[] = { kChatFolderResend };
        const int levels[] = { kCardButtonView };
        CheckFolderButtons(CsFolder(true, kFolderExpired), 1, actions, levels);
    }
    //完成：「打开文件夹」（查看）；已拒绝：没有按钮
    {
        const int actions[] = { kChatFolderOpenFolder };
        const int levels[] = { kCardButtonView };
        CheckFolderButtons(CsFolder(false, kFolderCompleted), 1, actions, levels);
        CHECK(ChatCardButtonsOf(CsFolder(false, kFolderRejected)).empty());
    }
}

TEST_CASE("CS7 卡片尺寸与底色：文件夹", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    {
        ChatMessageList list;
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, CsFolder(false, kFolderInviting)), true);
    }
    //本人发起、已完成：在右侧，白底，最右是查看按钮「打开文件夹」
    {
        ChatMessageList list;
        CsCheckCardBox(host.Host(), list, CsLayCard(host.Host(), list, CsFolder(true, kFolderCompleted)), true);
    }
}

TEST_CASE("CS8 按钮命中：文件夹", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    const HWND hwnd = host.Host().m_hWnd;

    ChatMessageList list;
    const CsCard card = CsLayCard(host.Host(), list, CsFolder(false, kFolderInviting));
    REQUIRE_FALSE(::IsRectEmpty(&card.m_button));
    ChatBubble* b = CsOnlyBubble(list);
    const POINT accept = CsCenter(card.m_button);
    CHECK(b->OnSetCursor(accept));
    b->OnLButtonDown(accept, MK_LBUTTON);
    b->OnLButtonUp(accept, 0);
    MSG posted;
    REQUIRE(::PeekMessageW(&posted, hwnd, WM_CHAT_FOLDER_ACTION, WM_CHAT_FOLDER_ACTION, PM_REMOVE));
    ChatFolderActionData* data = reinterpret_cast<ChatFolderActionData*>(posted.lParam);
    CHECK(data->action == kChatFolderAccept);
    delete data;

    //多选模式下不显示手型（2026-10-14 之前多选时文件、文件夹的按钮仍会显示手型）
    list.SetMultiSelectMode(true);
    CsListCanvas canvas;
    canvas.Paint(list);
    const POINT shifted = CsCenter(canvas.BoundsOf(cardstyle::kPrimaryFill,
                                                   CsRect(card.m_card.left, card.m_card.top, kCsListW, card.m_card.bottom)));
    CHECK_FALSE(b->OnSetCursor(shifted));
}

TEST_CASE("CS9 进度条：文件夹传输中是品牌橙", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    const CsCard card = CsLayCard(host.Host(), list, CsFolder(true, kFolderTransferring));
    //传输中只有次要按钮「取消」，卡片里品牌橙的像素就是进度条已完成的那一段
    CsListCanvas canvas;
    canvas.Paint(list);
    const RECT bar = canvas.BoundsOf(cardstyle::kProgressFill, card.m_card);
    REQUIRE_FALSE(::IsRectEmpty(&bar));
    CHECK(bar.bottom - bar.top <= cardstyle::kProgressH);
    CHECK(card.m_card.right - card.m_card.left == cardstyle::kCardWidth);
}

namespace {

// ---- Markdown、代码片段、语音 ----

// Markdown 样例正文：第一个标题块作卡片标题，其余作摘要
const wchar_t* const kCsMarkdownText = L"# 第四季度迭代计划\n\n本季度重点完成聊天卡片美化与列表滚动性能优化，下面是各模块的负责人与时间点。";
// 代码片段样例
const wchar_t* const kCsCodeText = L"int CountUnread(const std::vector<int>& list)\n{\n    int total = 0;\n    return total;\n}";
const long long kCsMdMsgid = 990700;     // Markdown 消息的服务器 msgid
const int kCsCodeMaxW = 440;             // 记录窗代码卡片控件的最大宽度（像素）
const int kCsVoiceSeconds = 12;          // 语音时长（秒）
const long long kCsVoiceBytes = 19200;   // 语音文件字节数

// 在作用域内接上编进测试程序的 Markdown 函数表（等同于 FlamingoMarkdown.dll 已加载），析构时恢复；须比含 Markdown 消息的列表活得久
class CsMarkdownOn
{
public:
    CsMarkdownOn()
    {
        markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    }

    ~CsMarkdownOn()
    {
        markdown::SetMarkdownApiOverrideForTest(false, NULL);
    }
};

// 对方发来的一条 Markdown 消息
ChatMessage CsMarkdown()
{
    ChatMessage m = MakeTextMessage(false, CString(kCsMarkdownText));
    m.isMarkdown = true;
    m.msgid = kCsMdMsgid;
    return m;
}

} // namespace

TEST_CASE("CS7 / CS8 Markdown 卡片：宽 300、白底、没有按钮，点卡片打开预览", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsMarkdownOn mdOn;
    CsHost host;
    REQUIRE(host.Host().IsWindow());

    const ChatMessage m = CsMarkdown();
    CHECK(ChatCardButtonsOf(m).empty());
    ChatMessageList list;
    const CsCard card = CsLayCard(host.Host(), list, m);
    CsCheckCardBox(host.Host(), list, card, false);

    ChatBubble* b = CsOnlyBubble(list);
    const POINT body = CsCenter(card.m_card);
    CHECK(b->OnSetCursor(body));
    b->OnLButtonDown(body, MK_LBUTTON);
    b->OnLButtonUp(body, 0);
    CHECK(CsTakePosted<MarkdownPreviewOpenRequest>(host.Host().m_hWnd, WM_CHAT_OPEN_MARKDOWN_PREVIEW));
}

TEST_CASE("CS7 代码片段卡片：白底，外面不再套一层气泡", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());

    ChatMessage m = MakeTextMessage(false, CString(kCsCodeText));
    m.isCode = true;
    ChatMessageList list;
    const CsCard card = CsLayCard(host.Host(), list, m);
    REQUIRE_FALSE(::IsRectEmpty(&card.m_card));
    CsListCanvas canvas;
    canvas.Paint(list);
    const int midY = (card.m_card.top + card.m_card.bottom) / 2;
    //卡片代码区（离左边框 kCsInset）为白色
    CHECK(canvas.At(card.m_card.left + kCsInset, card.m_card.bottom - kCsInset * 2) == cardstyle::kCardFill);
    //卡片左边框外侧紧挨着的就是消息列表的底色（与这一行最右边的颜色相同），没有一圈气泡底色
    CHECK(canvas.At(card.m_card.left - 2, midY) == canvas.At(kCsListW - kCsInset, midY));
}

TEST_CASE("CS10 记录窗的 Markdown、代码卡片控件与聊天窗一致", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());

    //Markdown 卡片控件：宽为卡片宽度，左边框暖灰、内部白色
    {
        MarkdownCardInfo info;
        info.m_title = L"第四季度迭代计划";
        info.m_summary = L"本季度重点完成聊天卡片美化与列表滚动性能优化。";
        const SIZE size = ChatMarkdownCardControl::MeasureCard(info.m_summary, cardstyle::kCardWidth * 2);
        CHECK(size.cx == cardstyle::kCardWidth);
        ChatMarkdownCardControl control;
        control.SetCard(info);
        const RECT rc = CsRect(20, 20, 20 + size.cx, 20 + size.cy);
        control.ForceLayout(rc);
        CsCanvas canvas;
        REQUIRE(canvas.Ok());
        control.OnPaint(canvas.Dc(), rc);
        CHECK(CsNear(canvas.At(rc.left, (rc.top + rc.bottom) / 2), cardstyle::kCardBorder));
        CHECK(canvas.At(rc.left + kCsInset, (rc.top + rc.bottom) / 2) == cardstyle::kCardFill);
    }

    //代码卡片控件：左边框暖灰（取竖直中点：离上下边太近会落在半径 10 的圆角弧上，抗锯齿后颜色有偏差）
    {
        const SIZE size = ChatCodeCardControl::MeasureCard(kCsCodeText, 0, kCsCodeMaxW);
        ChatCodeCardControl control;
        control.SetCode(kCsCodeText, 0, std::string("990800"));
        const RECT rc = CsRect(20, 20, 20 + size.cx, 20 + size.cy);
        control.ForceLayout(rc);
        CsCanvas canvas;
        REQUIRE(canvas.Ok());
        control.OnPaint(canvas.Dc(), rc);
        CHECK(CsNear(canvas.At(rc.left, (rc.top + rc.bottom) / 2), cardstyle::kCardBorder));
    }
}

TEST_CASE("CS11 语音未听红点为 #E5484D", "[cardstyle]")
{
    ScopedCsGdiplus gdip;
    REQUIRE(gdip.Ok());
    CsHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessage m = CsPeerMsg();
    m.msgid = kCsMdMsgid;
    ApplyVoiceMessage(m, "cs-voice-fileid", "cs-voice-md5", kCsVoiceSeconds, kCsVoiceBytes, std::string(), false);
    ChatMessageList list;
    list.AppendMessage(m);
    list.AttachToHost(&host.Host());
    RECT rc = { 0, 0, kCsListW, kCsListH };
    list.ForceLayout(rc);
    CsListCanvas canvas;
    canvas.Paint(list);
    const RECT dot = canvas.BoundsOf(cardstyle::kUnreadDot, CsRect(0, 0, kCsListW, kCsListH));
    CHECK_FALSE(::IsRectEmpty(&dot));
}
