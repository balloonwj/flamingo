/**
 *  ChatBubble.cpp
 *
 *  单聊窗消息行的绘制实现。形状（头像 / 气泡 / 引用块 / 图片占位 / 文件
 *  类型块）用 GDI+ 抗锯齿；文字用 GDI DrawText（清晰）。文字气泡支持
 *  自动折行：超过气泡最大宽度的长文字在气泡内换行、气泡随之变高。
 *  本行还支持右键上下文菜单与多选模式（详见 ChatBubble.h）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>
#include <atlgdi.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ChatBubble.h"
#include "ChatImageCache.h"    // 图片后台解码 + 缩放缓存（大图首次显示不冻结 UI）
#include "../IULog.h"          // 引用缩略图的诊断日志
#include "ChatIcons.h"
#include "ChatFileVisual.h"   // FileTypeColor（与消息记录窗共用同一套配色）
#include "ChatMessageList.h"
#include "ChatFileAction.h"    // ChatFileActionKind / ChatFileActionData（文件气泡按钮动作负载）
#include "ChatFaceTag.h"       // kFaceDisplayPx（内联表情显示边长）
#include "ChatInlineText.h"    // 「文字 + 内联表情」的排版与绘制（与消息记录窗共用）
#include "ChatTextSelection.h" // 选中文字的纯逻辑（命中测试 / 选区矩形 / 取文本）
#include "GroupMentionMenu.h"  // @某人 片段的右键菜单模型与投递负载
#include "ChatMessageMenu.h"   // 气泡自身右键菜单的行模型与投递负载（含图片两项）
#include "../BalloonMain/PopupMenuPlacement.h"   // ClampPopupMenuToWorkArea（菜单不越屏）
#include "../CustomMsgDef.h"   // WM_VIEW_CHAT_IMAGE / WM_CHAT_FILE_ACTION / WM_CHAT_IMAGE_ACTION
#include "../BalloonChat/BalloonChatMsgFormat.h"  // ParseSendBody / ChatSegment（解析内联表情）
#include "../net/MessageProtocolClient.h"  // canRecallMessage（右键菜单里"撤回"项的时限判断）
#include <ctime>               // time —— 取当前时刻做撤回时限比较
#include "../../balloonui/balloonui/DuiHost.h"

#include <map>

namespace balloonchatwnd {

namespace {

// ---- 几何常量（逻辑像素）---------------------------------------------

const int kTimeRowHeight  = 38;   // 时间分隔行高
const int kAvatarSize     = 34;   // 头像边长
const int kEdgePad        = 16;   // 头像距窗口边
const int kAvatarGap      = 10;   // 头像与气泡列的间距
const int kRowVMargin     = 7;    // 行上下外边距
const int kTextBubbleH    = 36;   // 单行文字气泡高（折行测量失败时的回退）
const int kEmojiBubbleH   = 46;   // emoji 气泡高
const int kFileBubbleH    = 58;   // 文件气泡高
const int kImageBubblePad = 8;    // 图片气泡内边距（4 每侧）
// 引用块（气泡内部顶端那一块，标明本条回复的是谁的哪句话）。
// 早先它画在气泡外面、只有一行；现按设计稿挪进气泡内部并改成两行 —— 第一行人名、
// 第二行被引用内容，左侧一条竖线标明这是引用。
const int kQuoteLineH     = 17;   // 引用块内每行文字的行高
const int kQuoteBlockH    = kQuoteLineH * 2 + 4;   // 引用块总高（两行 + 行间距）
const int kQuoteGap       = 6;    // 引用块与其下方正文之间的间距
const int kQuoteBarW      = 3;    // 左侧竖线宽
const int kQuoteBarGap    = 7;    // 竖线与文字之间的间距
const int kQuoteThumbSize = kQuoteBlockH;   // 引用图片消息时缩略图的边长（与引用块等高）
const int kQuoteThumbGap  = 8;              // 缩略图与右侧文字的间距
const int kQuoteMinTextW  = 120;            // 引用块内文字至少留出的宽度（保证人名 / 摘要看得见）
const int kSenderNameHeight = 17; // 群聊气泡上方发送者名行高
const int kReceiptHeight  = 18;   // "已读"回执行高

// 文字气泡内边距 / 折行布局参数。
const int    kTextPadH = 12;            // 文字气泡水平内边距（每侧）
const int    kTextPadV = 9;             // 文字气泡垂直内边距（每侧）
const int    kMinTextWidth = 40;        // 折行测量时文字区最小宽度
const double kBubbleMaxWidthRatio = 0.62;  // 气泡最大宽 = 内容区宽 × 此比例

// 多选模式：行首勾选列。
const int kCheckColW = 30;        // 勾选列宽（内容整体右移这么多）
const int kCheckSize = 18;        // 勾选圈直径

const Gdiplus::REAL kBubbleRadius = 12.0f;   // 气泡圆角
const Gdiplus::REAL kAvatarRadius = 7.0f;    // 头像圆角

// ---- 配色 --------------------------------------------------------------

const Gdiplus::Color kBubbleOther(255, 0xF3, 0xF2, 0xEF);  // 对方气泡底（浅灰）
const Gdiplus::Color kBubbleSelf (255, 0xFF, 0xE6, 0xD8);  // 自己气泡底（浅橙）
const Gdiplus::Color kQuoteFill  (255, 0xEF, 0xEE, 0xEB);  // 引用块底
const Gdiplus::Color kAccent     (255, 0xFF, 0x7A, 0x45);  // 主题橙
const Gdiplus::Color kCheckRing  (255, 0xC2, 0xC0, 0xBB);  // 未选中勾选圈描边
const COLORREF kBubbleText  = RGB(0x2A, 0x2A, 0x2A);       // 气泡文字
const COLORREF kQuoteName   = RGB(0xFF, 0x7A, 0x45);       // 引用人名
const COLORREF kQuoteText   = RGB(0x8A, 0x8A, 0x8A);       // 引用内容
const COLORREF kTimeColor   = RGB(0xA8, 0xA6, 0xA2);       // 时间分隔文字
const COLORREF kReceiptText = RGB(0xA8, 0xA6, 0xA2);       // "已读"文字
const COLORREF kAvatarText  = RGB(255, 255, 255);          // 头像字母

// 气泡内文字 / emoji 字号。
const int kBubbleTextPx = 14;
const int kEmojiPx      = 30;

// ---- @某人 右键菜单的尺寸估值（逻辑像素）------------------------------
//
// Win32 的 HMENU 在真正弹出之前拿不到精确尺寸，而落点夹取又必须在弹出<u>之前</u>
// 算好，故这里按"每项一行"估一个够用的尺寸。估大不会误伤：夹取只在放不下时才
// 改落点，估大顶多让菜单在靠近屏幕边缘时更早翻向左 / 上，不会让正常位置跑偏。
const int kMentionMenuEstWidth      = 140;   // 菜单估计宽（最长项"复制 TA 的昵称"约 110px + 边距）
const int kMentionMenuEstItemHeight = 26;    // 每项估计高
const int kMessageMenuEstWidth      = 130;   // 气泡自身右键菜单的估计宽（最长项"图片另存为"约 100px + 边距）

// 注：气泡自身右键菜单的命令 id 与行模型已挪到 ChatMessageMenu.h
//（enum ChatMessageMenuId / BuildChatMessageMenuModel），那边是纯函数、有单测。

// ---- Pin 标记（被钉住的消息在气泡外侧上角画一枚小图钉）----
const int kPinIconSize = 14;   // 图钉边长（逻辑像素）
const int kPinIconGap  = 6;    // 图钉与气泡边之间的水平间距
// 暖琥珀色：比正文显眼，又不像红色那样读作"出错了"。
const Gdiplus::Color kPinIconColor(255, 0xE6, 0x8A, 0x2E);

// 注：文件类型色块配色已挪到 ChatFileVisual.h / .cpp —— 「消息记录」窗的列表行
// 也要画这个色块，同一个 .xlsx 在两处得是同一个绿，故抽出来共用。

// ---- 文件气泡（kMsgFile）几何与状态布局 --------------------------------

// 注：文件态取值（kFileNormal / kFileUploading / ... / kFileRejected）与「状态 → 可用
// 操作」的映射（FileStateHasProgress / FileStateButtonCount / FileButtonActions）已抽到
// ChatFileStateModel.h/.cpp，那是可单元测试的纯逻辑；本文件经 ChatFileAction.h 取用。

// 文件气泡几何常量（逻辑像素）。
const int kFileBubbleW      = 300;  // 文件气泡固定宽
const int kFilePadH         = 14;   // 气泡水平内边距（每侧）
const int kFileIconSize     = 40;   // 类型图标边长（圆角方块）
const int kFileIconTop      = 10;   // 图标距气泡顶
const int kFileNameTop      = 12;   // 文件名距气泡顶
const int kFileNameH        = 20;   // 文件名行高
const int kFileStatusTop    = 34;   // "大小 · 状态"行距气泡顶
const int kFileStatusH      = 18;   // 状态行高
const int kFileHeaderH      = 60;   // 头部区高（图标 + 名 + 状态）
const int kFileProgressH    = 5;    // 进度条厚
const int kFileBtnTopGap    = 10;   // 按钮行距上方
const int kFileBtnH         = 34;   // 按钮高
const int kFileBtnBottomPad = 12;   // 按钮行下边距
const int kFileBtnGap       = 10;   // 两按钮间距
const int kFileBadgeR       = 9;    // 图标角标半径

// 文件气泡总高（随状态变化：是否带进度条 / 按钮行）。fromMe 会影响待接收态的按钮数，
// 但只要还有按钮，行高就是同一份，故高度实际不随 fromMe 变——仍如实传入，免得日后
// 某个状态改成"某一方无按钮"时这里悄悄算错。
int FileBubbleHeight(int st, bool fromMe)
{
    int h = kFileHeaderH;
    if (FileStateHasProgress(st))
        h += kFileProgressH;
    if (FileStateButtonCount(st, fromMe) > 0)
        h += kFileBtnTopGap + kFileBtnH + kFileBtnBottomPad;
    return h;
}

// 向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 一条文本消息解析出来的绘制样式（字号 / 字形 / 颜色）。
//
// 它与消息记录窗要用的是同一种东西，故结构本体已随排版引擎一起搬到
// ChatInlineText.h（改名 InlineTextStyle）；这里留一个别名，使本文件里既有的
// 上百处 BubbleTextStyle 一个字都不用改。
typedef InlineTextStyle BubbleTextStyle;

// ---- 群聊 @提醒 的配色与几何（见 docs/群聊@提醒方案.md §5.2 / §5.3）----------
//
// 四格：两种气泡底（对方浅灰 / 自己浅橙）× 两种 @ 类型（@我 / @别人）。
// @我 走主题橙系，与品牌色一脉相承、最醒目；自己气泡本身就是浅橙底，故其上的
// 橙块要压深一档才拉得开层次。@别人 走中性蓝，两种气泡底共用一套 —— 蓝块落在
// 浅橙底上略跳，但正因为跳才能一眼与「@我」区分。
const COLORREF kAtMeBgOnOther    = RGB(0xFF, 0xDD, 0xCB);   // 对方气泡里 @我 的底色块
const COLORREF kAtMeFgOnOther    = RGB(0xD9, 0x53, 0x1E);   // 对方气泡里 @我 的文字
const COLORREF kAtMeBgOnSelf     = RGB(0xFF, 0xC9, 0xAE);   // 自己气泡里 @我 的底色块
const COLORREF kAtMeFgOnSelf     = RGB(0xB8, 0x43, 0x1A);   // 自己气泡里 @我 的文字
const COLORREF kAtOtherBg        = RGB(0xE4, 0xEE, 0xFB);   // @别人 的底色块（两种气泡共用）
const COLORREF kAtOtherFg        = RGB(0x2F, 0x6F, 0xD0);   // @别人 的文字（两种气泡共用）

const int kAtBlockRadius  = 3;   // 底色块圆角半径（像素）
const int kAtBlockPadX    = 2;   // 底色块相对文字左右各外扩（像素）
const int kAtBlockPadY    = 1;   // 底色块相对文字上下各外扩（像素）

// 取某个 @ 片段该用的底色与文字色。
//   fromMe：本条消息是不是自己发的（决定气泡底是浅橙还是浅灰）。
//   isMe：  这个片段 @ 的是不是本人。
//   bg / fg：[出参] 底色块颜色与文字颜色。
void ResolveMentionColors(bool fromMe, bool isMe, COLORREF& bg, COLORREF& fg)
{
    if (!isMe)
    {
        bg = kAtOtherBg;
        fg = kAtOtherFg;
        return;
    }
    if (fromMe)
    {
        bg = kAtMeBgOnSelf;
        fg = kAtMeFgOnSelf;
    }
    else
    {
        bg = kAtMeBgOnOther;
        fg = kAtMeFgOnOther;
    }
}

// 确保 GDI+ 在本模块可用。
//
// 本进程<u>没有</u>全局的 GdiplusStartup —— 各模块按仓库既有做法各自惰性初始化
// （见 ChatImageCache.cpp / AvatarImageUtil.cpp / BalloonAboutDlg.cpp）。本文件此前
// 也用 GDI+ 画气泡底与头像，之所以一直没出问题，是因为那些绘制发生在<u>绘制阶段</u>，
// 那时别的模块已经初始化过了。
//
// 而 @提醒 的排版是在 Measure()（<u>布局阶段</u>）就要用 GDI+ 的，比任何绘制都早。
// 此时 GDI+ 未初始化，Graphics / FontFamily 的状态会是 GdiplusNotInitialized(18)，
// 且 new Gdiplus::Font 会<u>返回空指针而不是抛异常</u>（GdiplusBase::operator new 内部
// 调 GdipAlloc，失败即返回 NULL，构造函数根本不执行）—— 表现为气泡尺寸为 0、
// 一个字都画不出来。故这里必须自己兜底初始化一次。
// 多次 GdiplusStartup 是安全的：各模块各持一个 token，进程级只需保证启动过。
void EnsureGdiplusForBubble()
{
    static ULONG_PTR s_token = 0;
    if (s_token != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&s_token, &input, NULL);
}

// 把各 @ 片段的圆角底色块画在文字之下。
//
// 矩形取自「排版模型 + 扁平口径的片段」，与正文用的是同一次排版，故片段跨行时
// 每行自然得到一块，不必自己按行切分。
//   hdc：     本行绘制目标。
//   layout：  字符级排版模型（Measure 产出）。
//   atsFlat： 扁平口径的 @ 片段。
//   fromMe：  本条是不是自己发的（决定底色深浅，与气泡底配合）。
//   origin：  文本区原点（绝对坐标，已按渲染缓存平移过）。
void DrawMentionBlocks(HDC hdc, const SelLayout& layout,
                       const std::vector<RenderSpan>& atsFlat,
                       bool fromMe, const POINT& origin)
{
    if (atsFlat.empty() || layout.m_lines.empty())
    {
        return;
    }
    // 本进程没有全局的 GdiplusStartup，各模块各自惰性初始化（原委见
    // EnsureGdiplusForBubble 的注释）。这里是本文件里唯一还用 GDI+ 画 @ 的地方，
    // 少了这一句，Graphics / GraphicsPath 会处在 GdiplusNotInitialized 状态，
    // FillPath 直接访问违例。
    EnsureGdiplusForBubble();

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    for (std::size_t i = 0; i < atsFlat.size(); ++i)
    {
        COLORREF bg = 0;
        COLORREF fg = 0;
        ResolveMentionColors(fromMe, atsFlat[i].m_isMe, bg, fg);
        Gdiplus::SolidBrush brush(
            Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));

        const std::vector<SelRect> rects = BuildSelectionRects(
            layout, atsFlat[i].m_off, atsFlat[i].m_off + atsFlat[i].m_len);
        for (std::size_t r = 0; r < rects.size(); ++r)
        {
            // 底色块比文字四周各外扩一点，视觉上像个标签而不是紧贴字形的色条。
            const Gdiplus::REAL x = (Gdiplus::REAL)(origin.x + rects[r].m_left - kAtBlockPadX);
            const Gdiplus::REAL y = (Gdiplus::REAL)(origin.y + rects[r].m_top  - kAtBlockPadY);
            const Gdiplus::REAL w = (Gdiplus::REAL)(rects[r].m_right - rects[r].m_left + kAtBlockPadX * 2);
            const Gdiplus::REAL h = (Gdiplus::REAL)(rects[r].m_bottom - rects[r].m_top + kAtBlockPadY * 2);

            Gdiplus::GraphicsPath path;
            AddRoundRect(path, x, y, w, h, (Gdiplus::REAL)kAtBlockRadius);
            g.FillPath(&brush, &path);
        }
    }
}

// 把各 @ 片段的文字用高亮色重画一遍。
//
// 做法是「按片段矩形设裁剪区、再整段重画」，而不是只画那几个字的子串 —— 后者遇到
// 片段跨行就得自己按行拆，还得复刻本引擎的折行规则；裁剪重画沿用同一次排版，落在
// 矩形里的字形恰好就是那几个字，跨行天然正确。
//
// 代价是每个片段矩形都要重跑一次 DrawInlineContent（内含一次折行排版）。一条消息里
// @ 的人通常个位数，且整行的绘制结果有渲染缓存兜着、不是每帧都跑，这个代价可以接受。
//   hdc：     本行绘制目标。
//   segs：    正文解析出的消息段（与 DrawInlineContent 用的同一份）。
//   layout：  字符级排版模型。
//   atsFlat： 扁平口径的 @ 片段。
//   fromMe：  本条是不是自己发的。
//   txRc：    文本区矩形（绝对坐标）。
//   st：      本条消息的文字样式；本函数只改颜色，其余照用。
//   faceSize：内联表情显示边长。
void DrawMentionHighlightText(HDC hdc,
                              const std::vector<balloonchat::ChatSegment>& segs,
                              const SelLayout& layout,
                              const std::vector<RenderSpan>& atsFlat,
                              bool fromMe,
                              const RECT& txRc,
                              const BubbleTextStyle& st,
                              int faceSize)
{
    if (atsFlat.empty() || layout.m_lines.empty())
    {
        return;
    }

    for (std::size_t i = 0; i < atsFlat.size(); ++i)
    {
        COLORREF bg = 0;
        COLORREF fg = 0;
        ResolveMentionColors(fromMe, atsFlat[i].m_isMe, bg, fg);

        BubbleTextStyle hi = st;
        hi.color = fg;

        const std::vector<SelRect> rects = BuildSelectionRects(
            layout, atsFlat[i].m_off, atsFlat[i].m_off + atsFlat[i].m_len);
        for (std::size_t r = 0; r < rects.size(); ++r)
        {
            // 保存原裁剪区。GetClipRgn 返回 1 表示原本有裁剪区、0 表示没有，
            // 恢复时据此决定是还原那个区域还是清成"不裁剪"。
            HRGN saved = ::CreateRectRgn(0, 0, 0, 0);
            const int hadClip = ::GetClipRgn(hdc, saved);

            ::IntersectClipRect(hdc,
                                txRc.left + rects[r].m_left,
                                txRc.top  + rects[r].m_top,
                                txRc.left + rects[r].m_right,
                                txRc.top  + rects[r].m_bottom);
            DrawInlineContent(hdc, segs, txRc, hi, faceSize);

            ::SelectClipRgn(hdc, (hadClip == 1) ? saved : NULL);
            ::DeleteObject(saved);
        }
    }
}

// 用 GDI 绘制文字。italic / underline / strike 默认关，供消息字体设置按需打开
// （字体族固定微软雅黑）。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags,
                 bool italic = false, bool underline = false, bool strike = false)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        italic ? TRUE : FALSE,
        underline ? TRUE : FALSE,
        strike ? TRUE : FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 从一条消息的字体设置解出绘制样式。font.m_valid 为 false 时回退到气泡默认
// （kBubbleTextPx、不加粗、默认色、无斜体/下划线/删除线）。net::TextFont 的颜色
// 是 0xRRGGBB，COLORREF 是 0x00BBGGRR，需把 R / B 调换。
BubbleTextStyle ResolveTextStyle(const ChatMessage& msg)
{
    BubbleTextStyle st;
    st.sizePx    = kBubbleTextPx;
    st.bold      = false;
    st.italic    = false;
    st.underline = false;
    st.strike    = false;
    st.color     = kBubbleText;
    if (msg.font.m_valid)
    {
        st.sizePx    = msg.font.m_size;
        st.bold      = msg.font.m_bold;
        st.italic    = msg.font.m_italic;
        st.underline = msg.font.m_underline;
        st.strike    = msg.font.m_strike;
        const unsigned int rgb = msg.font.m_colorRgb;
        st.color = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    return st;
}

// 测量一段文字的像素宽度（单行）。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// 把一段 Unicode 文字放进剪贴板（复制菜单用）。
//   owner：拥有剪贴板会话的窗口；text：要复制的文字（NULL 安全）。
void PutTextToClipboard(HWND owner, LPCWSTR text)
{
    if (text == NULL)
    {
        return;
    }
    if (!::OpenClipboard(owner))
    {
        return;
    }
    ::EmptyClipboard();
    const size_t chars = ::wcslen(text) + 1;
    const size_t bytes = chars * sizeof(WCHAR);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != NULL)
    {
        void* dst = ::GlobalLock(hMem);
        if (dst != NULL)
        {
            ::CopyMemory(dst, text, bytes);
            ::GlobalUnlock(hMem);
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else
        {
            ::GlobalFree(hMem);
        }
    }
    ::CloseClipboard();
}

// 把一条消息的可复制文本取出来（不同 kind 取不同字段）。
CString MessageCopyText(const ChatMessage& msg)
{
    switch (msg.kind)
    {
    case kMsgText:
    case kMsgEmoji:
        return msg.text;
    case kMsgFile:
        return msg.fileName;
    case kMsgImage:
        return msg.imageCaption.IsEmpty()
                   ? CString(_T("[图片]")) : msg.imageCaption;
    default:
        return msg.text;
    }
}

} // anonymous namespace

// 重量级折行测量的进程级计数器（性能探针，详见 ChatBubble.h 的声明）。
unsigned long ChatBubble::s_heavyMeasureCount = 0;

unsigned long ChatBubble::HeavyMeasureCount()
{
    return s_heavyMeasureCount;
}

void ChatBubble::ResetHeavyMeasureCount()
{
    s_heavyMeasureCount = 0;
}

ChatBubble::ChatBubble()
    : m_measuredWidth(-1)
    , m_measuredMultiSel(false)
    , m_textBubbleW(0)
    , m_textBubbleH(kTextBubbleH)
    , m_selected(false)
    , m_selAnchor(-1)
    , m_selCaret(-1)
    , m_selDragging(false)
    , m_avatarBmp(NULL)
    , m_avatarLoadTried(false)
    , m_fileBtnCount(0)
    , m_fileHoverIdx(-1)
    , m_renderDC(NULL)
    , m_renderBmp(NULL)
    , m_renderOldBmp(NULL)
    , m_renderW(0)
    , m_renderH(0)
    , m_renderMultiSel(false)
    , m_renderDirty(true)
    , m_quoteRectRel()
    , m_quoteThumbLogged(false)
{
    m_msg.kind = kMsgText;
    m_msg.fromMe = false;
    m_msg.imageW = m_msg.imageH = 0;
    m_msg.fileProgress = 0;
    m_msg.hasQuote = false;
    m_msg.read = false;
    ::SetRectEmpty(&m_imageRect);
    for (int i = 0; i < 3; ++i)
    {
        ::SetRectEmpty(&m_fileBtnRect[i]);
        m_fileBtnAction[i] = 0;
    }
}

ChatBubble::~ChatBubble()
{
    ReleaseAvatarBitmap();
    ReleaseRenderCache();
}

void ChatBubble::Invalidate()
{
    // 内容变 → 渲染缓存置脏，下次 OnPaint 重画缓存位图。再走基类原有重绘请求。
    m_renderDirty = true;
    DuiControl::Invalidate();
}

bool ChatBubble::IsRenderCacheable() const
{
    // 引用了一张图片的文字消息不能缓存：缩略图要等后台解码完成、之后重绘才画得出来，
    // 而缓存会把第一次画的那块占位一直搬下去 —— 解码完成后虽然重绘了，缓存却仍然有效
    // （内容没变、宽高没变），于是缩略图永远出不来，表现为引用块左侧一直是一块空白。
    // 图片气泡本身正是因为同样的原因才不缓存，这里与之保持一致。缩略图就绪之后其实
    // 可以恢复缓存，但带图片引用的消息本就少，不值得为此加一套状态机。
    if (m_msg.hasQuote && m_msg.quoteMsgType == kMsgImage)
    {
        return false;
    }

    // 文字 / emoji / 时间分隔行绘制时不记录任何宿主坐标系命中矩形，可安全缓存。
    // 图片 / 文件行绘制时要写 m_imageRect / m_fileBtnRect 供命中测试，不能缓存。
    return m_msg.kind == kMsgText
        || m_msg.kind == kMsgEmoji
        || m_msg.kind == kMsgTime;
}

void ChatBubble::EnsureRenderCache(HDC ref, int w, int h)
{
    if (m_renderDC != NULL && m_renderW == w && m_renderH == h)
    {
        return;   // 尺寸未变，复用现有缓存位图
    }
    ReleaseRenderCache();
    if (w <= 0 || h <= 0)
    {
        return;
    }
    m_renderDC = ::CreateCompatibleDC(ref);
    if (m_renderDC == NULL)
    {
        return;
    }
    m_renderBmp = ::CreateCompatibleBitmap(ref, w, h);
    if (m_renderBmp == NULL)
    {
        ::DeleteDC(m_renderDC);
        m_renderDC = NULL;
        return;
    }
    m_renderOldBmp = (HBITMAP)::SelectObject(m_renderDC, m_renderBmp);
    m_renderW = w;
    m_renderH = h;
}

void ChatBubble::ReleaseRenderCache()
{
    if (m_renderDC != NULL)
    {
        if (m_renderOldBmp != NULL)
        {
            ::SelectObject(m_renderDC, m_renderOldBmp);
            m_renderOldBmp = NULL;
        }
        ::DeleteDC(m_renderDC);
        m_renderDC = NULL;
    }
    if (m_renderBmp != NULL)
    {
        ::DeleteObject(m_renderBmp);
        m_renderBmp = NULL;
    }
    m_renderW = 0;
    m_renderH = 0;
}

void ChatBubble::ReleaseAvatarBitmap()
{
    if (m_avatarBmp != NULL)
    {
        delete m_avatarBmp;
        m_avatarBmp = NULL;
    }
}

// =======================================================================
// 气泡内文字选中
// =======================================================================

// 选区高亮底色。对方气泡（浅灰底）与自己气泡（浅橙底）各用一档，保证盖在底色上
// 仍能看清、且文字不被糊住。取值参照系统高亮色的浅色版。
static const COLORREF kSelHighlightOnOther = RGB(0xCC, 0xE0, 0xF8);   // 浅蓝
static const COLORREF kSelHighlightOnSelf  = RGB(0xFF, 0xD2, 0xB0);   // 浅橙（比气泡底深一档）

bool ChatBubble::IsTextSelectable() const
{
    // 所有文字气泡都支持选中 —— 含 @ 的已经和不含 @ 的走同一套手写折行排版，
    // 选区模型与画出来的文字同源，不会错位（早先含 @ 的走 GDI+，两套排版对不上，
    // 才不得不禁掉选中）。时间分隔 / 撤回提示 / 图片 / 文件行都没有可选文字。
    return m_msg.kind == kMsgText
        && !m_selLayout.m_lines.empty();
}

bool ChatBubble::HasTextSelection() const
{
    if (m_selAnchor < 0 || m_selCaret < 0)
    {
        return false;
    }
    return m_selAnchor != m_selCaret;
}

void ChatBubble::ClearTextSelection()
{
    if (m_selAnchor < 0 && m_selCaret < 0 && !m_selDragging)
    {
        return;   // 本就没有选区，避免无谓重绘
    }
    m_selAnchor   = -1;
    m_selCaret    = -1;
    m_selDragging = false;
    Invalidate();
}

void ChatBubble::SelectAllText()
{
    if (!IsTextSelectable())
    {
        return;
    }
    m_selAnchor = 0;
    m_selCaret  = (int)m_selFlatText.size();
    Invalidate();
}

CString ChatBubble::GetSelectedText() const
{
    if (!HasTextSelection())
    {
        return CString();
    }
    int s = 0;
    int e = 0;
    NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    // 表情替换成 [表情]，与会话列表预览同口径（BodyToDigest 也是这么做的）。
    const std::wstring picked = ExtractSelectedText(m_selFlatText, s, e, L"[表情]");
    return CString(picked.c_str());
}

POINT ChatBubble::TextOriginInHost(bool& okOut) const
{
    POINT origin;
    origin.x = 0;
    origin.y = 0;
    okOut = false;
    if (!IsTextSelectable() || m_measuredWidth < 0)
    {
        return origin;
    }

    // 与 RenderContent 里算 txRc 同口径：气泡左上角 + 内边距。气泡横向位置取决于
    // 是自己发的（靠右）还是对方发的（靠左），这里复用同一套几何。
    const int bubbleW = m_textBubbleW;
    int bubbleX = 0;
    if (m_msg.fromMe)
    {
        bubbleX = m_rcItem.right - kEdgePad - kAvatarSize - kAvatarGap - bubbleW;
    }
    else
    {
        bubbleX = m_rcItem.left + kEdgePad + kAvatarSize + kAvatarGap;
    }
    int bubbleY = m_rcItem.top + kRowVMargin;
    if (!m_msg.fromMe && !m_msg.senderName.IsEmpty())
    {
        bubbleY += kSenderNameHeight;   // 群聊：对方气泡上方有发送人名
    }
    origin.x = bubbleX + kTextPadH;
    origin.y = bubbleY + kTextPadV;
    if (m_msg.hasQuote)
    {
        // 引用块占据气泡内部顶端，正文从它下方开始。
        origin.y += kQuoteBlockH + kQuoteGap;
    }
    okOut = true;
    return origin;
}

int ChatBubble::CharIndexFromHostPoint(POINT pt) const
{
    bool ok = false;
    const POINT origin = TextOriginInHost(ok);
    if (!ok)
    {
        return 0;
    }
    return CharIndexFromPoint(m_selLayout, pt.x - origin.x, pt.y - origin.y);
}

void ChatBubble::DrawTextSelection(HDC hdc, const POINT& origin) const
{
    if (!HasTextSelection())
    {
        return;
    }
    int s = 0;
    int e = 0;
    NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    const std::vector<SelRect> rects = BuildSelectionRects(m_selLayout, s, e);
    if (rects.empty())
    {
        return;
    }

    const COLORREF fill = m_msg.fromMe ? kSelHighlightOnSelf : kSelHighlightOnOther;
    HBRUSH brush = ::CreateSolidBrush(fill);
    if (brush == NULL)
    {
        return;
    }
    for (size_t i = 0; i < rects.size(); ++i)
    {
        RECT rc;
        rc.left   = origin.x + rects[i].m_left;
        rc.top    = origin.y + rects[i].m_top;
        rc.right  = origin.x + rects[i].m_right;
        rc.bottom = origin.y + rects[i].m_bottom;
        ::FillRect(hdc, &rc, brush);
    }
    ::DeleteObject(brush);
}

void ChatBubble::SetMessage(const ChatMessage& msg)
{
    m_msg = msg;
    // 换了消息：旧选区与旧排版全部作废。
    m_selAnchor   = -1;
    m_selCaret    = -1;
    m_selDragging = false;
    m_selLayout   = SelLayout();
    m_selFlatText.clear();
    // 内容变了，折行测量缓存作废，须重新 Measure。
    m_measuredWidth = -1;
    // @ 片段的扁平口径副本跟着作废。非文字消息的 Measure 会提前返回、不重建它，
    // 若不在这里清掉，一条"文字消息改成图片消息"的气泡会留着上一条的 @ 片段，
    // 右键时按早已不存在的区间做命中。
    m_atsFlat.clear();
    ::SetRectEmpty(&m_quoteRectRel);
    // 换了消息（可能换了发送者头像）：丢弃旧头像真图缓存，下次绘制按新路径重新解码。
    ReleaseAvatarBitmap();
    m_avatarLoadTried = false;
    Invalidate();
}

void ChatBubble::BackfillServerIds(long long msgid, long long timestampMs)
{
    // 这几个字段只被撤回时限判断与按 msgid 的定位读取，不进折行、不进绘制，所以这里
    // 既不置 m_renderDirty、也不动 m_measuredWidth / m_selLayout —— 保住已经算好的
    // 折行结果与选区模型，本行才不会在回执到达后突然选不中文字。
    m_msg.msgid       = msgid;
    m_msg.timestampMs = timestampMs;

    // 文件气泡另有一份 fileMsgid（与图片的 msgid 是各自独立的字段），本端发出的那条
    // 全靠这里回填 —— 不填的话它恒为 0，之后"对方收取到什么程度"的推送就按 msgid
    // 找不到本行，状态行永远不会变。
    if (m_msg.kind == kMsgFile)
    {
        m_msg.fileMsgid = msgid;
    }
}

void ChatBubble::SetSenderAvatarImage(const CString& path)
{
    if (m_msg.senderAvatarImagePath == path)
    {
        return;   // 路径没变，无需重解码 / 重绘
    }
    m_msg.senderAvatarImagePath = path;
    // 换了头像图源：丢弃旧位图缓存，下次绘制按新路径重新解码（空路径=回退字形）。
    ReleaseAvatarBitmap();
    m_avatarLoadTried = false;
    Invalidate();
}

void ChatBubble::SetImageUploadProgress(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    m_msg.imageState    = 1;   // 上传中
    m_msg.imageProgress = percent;
    Invalidate();
}

void ChatBubble::SetImageUploadFailed()
{
    m_msg.imageState = 2;   // 失败
    Invalidate();
}

void ChatBubble::SetImageUploadDone()
{
    m_msg.imageState    = 0;   // 正常
    m_msg.imageProgress = 100;
    Invalidate();
}

void ChatBubble::SetImageDownloaded(const CString& localPath)
{
    m_msg.imagePath  = localPath;
    m_msg.imageState = 0;   // 正常（已就绪）
    // 换了图源（新本地路径）：下次绘制时 ChatImageCache 会按新路径解码 + 缩放并缓存。
    Invalidate();
}

void ChatBubble::SetImageDownloadFailed()
{
    m_msg.imageState = 2;   // 失败
    Invalidate();
}

// ---- 文件气泡态设置 ----
// 注：这些改 fileState 会改变气泡高度（FileBubbleHeight），故 ChatMessageList 在调用
// 后须 ForceLayout 重排；本处的 Invalidate 仅触发重绘，重排由列表负责。

void ChatBubble::SetFileUploadProgress(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    m_msg.fileState    = 1;   // 上传中
    m_msg.fileProgress = percent;
    Invalidate();
}

void ChatBubble::SetFileUploadDone()
{
    m_msg.fileState    = 0;   // 完成
    m_msg.fileProgress = 100;
    Invalidate();
}

void ChatBubble::SetFileUploadFailed()
{
    m_msg.fileState = 2;   // 失败（保留当前进度作红色停位）
    Invalidate();
}

void ChatBubble::SetFileDownloadProgress(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    m_msg.fileState    = 3;   // 下载中
    m_msg.fileProgress = percent;
    Invalidate();
}

void ChatBubble::SetFileDownloaded(const CString& localPath)
{
    m_msg.fileLocalPath = localPath;
    m_msg.fileState     = 0;   // 完成
    m_msg.fileProgress  = 100;
    Invalidate();
}

void ChatBubble::SetFileDownloadFailed()
{
    m_msg.fileState = 2;   // 失败
    Invalidate();
}

void ChatBubble::SetFileCancelled()
{
    m_msg.fileState = 4;   // 已取消（保留当前进度作灰色停位）
    Invalidate();
}

void ChatBubble::SetFileDeleted()
{
    m_msg.fileState = 5;   // 已删除 / 失效
    Invalidate();
}

bool ChatBubble::CanResendFile() const
{
    //重发需要两样东西：与"发送中"气泡对回的 clientMsgId，以及仍在原处的本地源文件。
    //只有本端刚发出的那笔同时具备；历史里自己发过的文件两样都没有，只能重新下载。
    return m_msg.fromMe
        && !m_msg.fileClientMsgId.empty()
        && !m_msg.fileLocalPath.IsEmpty();
}

void ChatBubble::SetFileRejected()
{
    m_msg.fileState = 7;   // 已拒绝接收（可再点「重新下载」反悔）
    Invalidate();
}

void ChatBubble::SetFileDownloadStarted()
{
    m_msg.fileState    = 3;   // 下载中
    m_msg.fileProgress = 0;   // 从头开始，清掉上一轮可能残留的进度
    Invalidate();
}

void ChatBubble::SetFilePeerState(int peerState)
{
    m_msg.filePeerState = peerState;
    Invalidate();
}

void ChatBubble::SetFileUploadStarted()
{
    m_msg.fileState    = 1;   // 上传中
    m_msg.fileProgress = 0;   // 重发从头开始，清掉上一轮停在半截的进度
    Invalidate();
}

bool ChatBubble::IsRealMessage() const
{
    // 时间分隔与"撤回了一条消息"提示行都用 kMsgTime 样式，均非真实消息。
    return m_msg.kind != kMsgTime;
}

bool ChatBubble::ParentInMultiSelect() const
{
    ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
    return (list != NULL) && list->IsMultiSelectMode();
}

void ChatBubble::SetSelected(bool selected)
{
    if (m_selected != selected)
    {
        m_selected = selected;
        Invalidate();
    }
}

void ChatBubble::SetPinned(bool pinned)
{
    if (m_msg.pinned != pinned)
    {
        m_msg.pinned = pinned;
        //Invalidate 是本类的同名版本，会顺带把渲染缓存置脏（见头文件说明），
        //故文字气泡下一帧会重画、图钉能真的出现或消失。
        Invalidate();
    }
}

void ChatBubble::RecallToTip(const CString& tipText)
{
    // 就地变成一条居中的系统提示行。
    m_msg.kind = kMsgTime;
    m_msg.text = tipText;
    m_msg.hasQuote = false;
    m_selected = false;
    m_measuredWidth = -1;
    // 撤回后本行变成系统提示，原消息的 @ 片段随之作废（理由同 SetMessage）。
    m_msg.ats.clear();
    m_atsFlat.clear();
    Invalidate();
}

// ====================================================================
// @ 片段的下标换算 —— 内联表情的排版与绘制已抽到 ChatInlineText.{h,cpp} 共用，
// 这里只剩气泡自己才有的这一件：把 @ 片段换算到排版模型认的扁平口径。
// ====================================================================

namespace {

// 把 @ 片段从「发送正文下标」换算到「扁平文本下标」。
//
// 两套下标只在正文里出现表情（/f["5"]）或转义斜杠（//）时才不同，但一旦不同，
// @ 的高亮区间就整体错位，所以必须显式换算，不能直接拿正文下标去查排版模型。
//   body：   发送正文（即 m_msg.text）。
//   ats：    正文口径的片段（来自协议换算，见 GroupMentionModel）。
//   flatLen：扁平文本长度，用于夹取，防越界。
// @return 扁平口径的片段；换算后落空的片段会被丢弃（不让坏数据毁掉整条渲染）。
std::vector<RenderSpan> ConvertAtsToFlat(const CString& body,
                                         const std::vector<RenderSpan>& ats,
                                         int flatLen)
{
    std::vector<RenderSpan> out;
    if (ats.empty() || flatLen <= 0)
    {
        return out;
    }
    const std::vector<int> map = balloonchat::BuildFlatIndexMap(body);
    const int rawLen = body.GetLength();
    out.reserve(ats.size());

    for (std::size_t i = 0; i < ats.size(); ++i)
    {
        const RenderSpan& src = ats[i];
        if (src.m_off < 0 || src.m_len <= 0 || src.m_off + src.m_len > rawLen)
        {
            continue;   // 越界片段丢弃 —— 上游已校验一遍，这里再挡一道
        }
        const int flatBegin = map[(std::size_t)src.m_off];
        const int flatEnd   = map[(std::size_t)(src.m_off + src.m_len)];
        if (flatEnd <= flatBegin || flatBegin < 0 || flatEnd > flatLen)
        {
            continue;   // 换算后为空区间（如整段都落在一个表情标记里）
        }

        RenderSpan dst = src;
        dst.m_off = flatBegin;
        dst.m_len = flatEnd - flatBegin;
        out.push_back(dst);
    }
    return out;
}


} // 匿名 namespace（@ 片段下标换算）

void ChatBubble::Measure(int availWidth)
{
    // 仅文字气泡需要折行测量；其余 kind 行高是定值，测量无副作用。仍记录
    // availWidth，便于 GetRowHeight 据 m_measuredWidth>=0 判定"已测量"。
    if (m_msg.kind != kMsgText)
    {
        m_measuredWidth = availWidth;
        return;
    }

    // 多选模式下内容区让出勾选列、气泡最大宽度随之收窄 —— 故多选状态与
    // availWidth 一起决定折行结果，二者共同构成折行缓存键。
    const bool multiSel = (ParentInMultiSelect() && IsRealMessage());

    // 折行缓存命中：宽度与多选状态都没变即直接复用上次结果（内容若变，
    // SetMessage / RecallToTip 已把 m_measuredWidth 置 -1，使下式不成立而
    // 强制重测）。命中时跳过昂贵的 GetDC + 建字体 + DrawText 折行测量 ——
    // 这是窗口缩放流畅的关键：一步缩放里的多遍同宽重排只在第一遍真测，
    // 纵向拖动（宽度不变）则完全不再重新折行。
    if (m_measuredWidth == availWidth && m_measuredMultiSel == multiSel)
    {
        return;
    }
    m_measuredWidth    = availWidth;
    m_measuredMultiSel = multiSel;
    // 真正重折行（宽度 / 多选态变了）→ 折行结果将变，渲染缓存置脏；垂直拖动走上面
    // 的早返回、不到这里，故不影响拖动性能。
    m_renderDirty = true;

    const int selW = multiSel ? kCheckColW : 0;
    int maxBubbleW = (int)((availWidth - selW) * kBubbleMaxWidthRatio);
    int maxTextW = maxBubbleW - kTextPadH * 2;
    if (maxTextW < kMinTextWidth)
    {
        maxTextW = kMinTextWidth;
    }

    m_textBubbleW = 0;
    m_textBubbleH = kTextBubbleH;

    // 走到这里即将执行一次真正的重量级折行测量 —— 计入性能探针。
    ++s_heavyMeasureCount;

    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return;
    }
    // 折行测量须按本条消息的实际字体（字号 / 粗体 / 斜体影响文字宽高），否则
    // 自定义字号的气泡会被错误地按默认 14px 测量而裁切或留白。
    const BubbleTextStyle st = ResolveTextStyle(m_msg);
    HFONT hFont = ::CreateFontW(
        -st.sizePx, 0, 0, 0,
        st.bold ? FW_SEMIBOLD : FW_NORMAL,
        st.italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (hFont != NULL)
    {
        HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);

        // 选区模型每次重测都要重建 —— 它记的是字符级坐标，宽度一变就全废。
        m_selLayout = SelLayout();
        m_selFlatText.clear();

        // 所有文字气泡 —— 纯文字、含内联表情、含 @提醒 —— 统一走手写折行排版。
        //
        // 含 @ 的曾经单独走 GDI+（当时只有它能按字符区间要到高亮矩形），代价是：
        //   · 选中文字用不了 —— GDI+ 的排版与本引擎产出的选区模型不同源，硬凑会错位，
        //     于是含 @ 的消息干脆禁止选中；
        //   · @ 与内联表情混排时，GDI+ 直接画正文原文，表情显示成 /f["5"] 这样的转义；
        //   · 两套排版并存，度量差异是长期的坑（气泡空白那次就是其一）。
        // 现在收敛成一条路：本引擎的 InlinePlaced 本来就带 charBegin / charCount 与矩形，
        // 字符级模型既够选中文字用，也够 @ 片段要矩形用，一份数据两用。
        //
        // 纯文字以前走 DrawText 一把梭，早先已改成与含表情的同一条路 —— 因为
        // "选中文字"要求把坐标映射到字符下标，而 DrawText 的折行是黑盒，做不到。
        std::vector<balloonchat::ChatSegment> segs =
            balloonchat::ParseSendBody(m_msg.text);
        std::vector<InlinePlaced> placed;
        SIZE sz = LayoutInlineContent(hdc, segs, maxTextW, kFaceDisplayPx, &placed);
        m_textBubbleW = sz.cx + kTextPadH * 2;
        m_textBubbleH = sz.cy + kTextPadV * 2;

        // 与排版同源地产出选区模型：同一次排版、同一套字体，选区矩形才不会与
        // 画出来的文字错位。
        m_selFlatText = BuildFlatText(segs);
        BuildSelLayoutFromPlaced(hdc, placed, (int)m_selFlatText.size(), m_selLayout);

        // 带引用的消息：引用块占据气泡内部顶端一块固定高度，正文从它下方开始，
        // 故气泡总高要把它算进去（早先引用画在气泡外，是加在行高上的）。
        if (m_msg.hasQuote)
        {
            m_textBubbleH += kQuoteBlockH + kQuoteGap;

            // 气泡宽度也要照顾引用块：正文很短时（比如只回一个字），按正文算出来的
            // 气泡窄得放不下引用块里的缩略图与两行文字，内容会溢出到气泡外面去。
            // 故给带引用的气泡定一个下限：竖线 + 间距 +（有图时的缩略图）+ 一段文字。
            int quoteMinW = kTextPadH * 2 + kQuoteBarW + kQuoteBarGap + kQuoteMinTextW;
            if (m_msg.quoteMsgType == kMsgImage)
            {
                quoteMinW += kQuoteThumbSize + kQuoteThumbGap;
            }
            if (quoteMinW > maxBubbleW)
            {
                quoteMinW = maxBubbleW;   // 再窄也不能超过气泡最大宽
            }
            if (m_textBubbleW < quoteMinW)
            {
                m_textBubbleW = quoteMinW;
            }
        }

        // @ 片段换算到扁平口径，与 m_selLayout 对齐，供画底色块与右键命中使用。
        m_atsFlat = ConvertAtsToFlat(m_msg.text, m_msg.ats, (int)m_selFlatText.size());
        ::SelectObject(hdc, hOldFont);
        ::DeleteObject(hFont);
    }
    ::ReleaseDC(NULL, hdc);

    // 夹紧：高不小于单行高、宽不超过气泡最大宽。
    if (m_textBubbleH < kTextBubbleH)
    {
        m_textBubbleH = kTextBubbleH;
    }
    if (m_textBubbleW > maxBubbleW)
    {
        m_textBubbleW = maxBubbleW;
    }
}

int ChatBubble::GetRowHeight() const
{
    if (m_msg.kind == kMsgTime)
    {
        return kTimeRowHeight;
    }

    int bubbleH = kTextBubbleH;
    switch (m_msg.kind)
    {
    case kMsgText:
        bubbleH = (m_measuredWidth >= 0) ? m_textBubbleH : kTextBubbleH;
        break;
    case kMsgEmoji:
        bubbleH = kEmojiBubbleH;
        break;
    case kMsgImage:
        bubbleH = m_msg.imageH + kImageBubblePad;
        break;
    case kMsgFile:
        // 文件气泡高随状态变化（有无进度条 / 按钮行）。
        bubbleH = FileBubbleHeight(m_msg.fileState, m_msg.fromMe);
        break;
    default:
        bubbleH = kTextBubbleH;
        break;
    }

    int stackH = bubbleH;
    // 引用块现在画在气泡内部，其高度已由 Measure 计入 m_textBubbleH，这里不能再加
    // 一次，否则每条带引用的消息都会多留出一块空白。
    if (!m_msg.fromMe && !m_msg.senderName.IsEmpty())
    {
        // 群聊：对方气泡上方显示发送者名，占一行。
        stackH += kSenderNameHeight;
    }
    if (m_msg.fromMe)
    {
        stackH += kReceiptHeight;
    }
    if (stackH < kAvatarSize)
    {
        stackH = kAvatarSize;
    }
    return stackH + kRowVMargin * 2;
}

bool ChatBubble::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    DuiControl* parent = GetParent();
    return (parent != NULL) ? parent->OnMouseWheel(pt, zDelta, mkFlags) : false;
}

bool ChatBubble::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // 点在引用块上：请宿主定位到被引用的原消息。放在最前 —— 否则这一下会被下面的
    // 拖拽选择吃掉，引用块就点不动了。多选模式下整行是勾选目标，不做定位。
    if (!ParentInMultiSelect() && m_msg.hasQuote && m_msg.quoteMsgid != 0
        && !::IsRectEmpty(&m_quoteRectRel))
    {
        RECT hit = m_quoteRectRel;
        ::OffsetRect(&hit, m_rcItem.left, m_rcItem.top);
        if (::PtInRect(&hit, pt))
        {
            balloonwjui::DuiHost* host = GetHost();
            HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
            if (hwnd != NULL)
            {
                long long* pMsgid = new long long(m_msg.quoteMsgid);
                if (!::PostMessage(hwnd, WM_CHAT_LOCATE_QUOTED, 0, (LPARAM)pMsgid))
                {
                    delete pMsgid;
                }
            }
            return true;
        }
    }

    // 文字气泡：非多选模式下，左键按下即开始拖拽选择文字。
    // 放在最前 —— 多选模式下整行是勾选目标，不做文字选择（下面那支会先返回）。
    if (!ParentInMultiSelect() && IsTextSelectable())
    {
        // 同一时刻只允许一个气泡有选区 —— 在别的气泡上开始新选择时，先清掉旧的，
        // 否则会出现两条消息同时高亮、而"复制"只取其中一条的割裂感。
        ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
        if (list != NULL)
        {
            list->ClearTextSelectionExcept(this);
        }

        const int idx = CharIndexFromHostPoint(pt);
        m_selAnchor   = idx;
        m_selCaret    = idx;
        m_selDragging = true;
        // 抓取鼠标：拖出气泡甚至拖出窗口都要继续收到移动事件，否则一出界选区就断了。
        Capture();
        Invalidate();
        return true;   // 消费掉，避免这次点击被别处再解释一遍
    }

    // 多选模式下、且本行是真实消息时，左键点击切换选中态（此模式下文件按钮不响应）。
    if (ParentInMultiSelect())
    {
        if (!IsRealMessage())
        {
            return false;
        }
        ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
        m_selected = !m_selected;
        Invalidate();
        if (list != NULL)
        {
            list->NotifySelectionChanged();
        }
        return true;
    }

    // 非多选：文件气泡底部操作链接命中 → 发动作给宿主聊天窗（打开 / 打开所在文件夹 /
    // 另存为 / 取消 / 重试）。
    if (m_msg.kind == kMsgFile)
    {
        int idx = FileLinkAtPoint(pt);
        if (idx >= 0)
        {
            HandleFileButton(m_fileBtnAction[idx]);
            return true;
        }
    }
    return false;
}

void ChatBubble::HandleFileButton(int action)
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (hwnd == NULL)
    {
        return;
    }

    // 组负载发给宿主聊天窗；宿主处理后 delete（异步 PostMessage，故堆分配传递）。
    ChatFileActionData* d = new ChatFileActionData();
    d->action      = action;
    d->fromMe      = m_msg.fromMe;
    d->clientMsgId = m_msg.fileClientMsgId;
    d->msgid       = m_msg.fileMsgid;
    d->localPath   = m_msg.fileLocalPath;
    d->fileName    = m_msg.fileName;
    d->fileId      = m_msg.fileFileId;
    d->md5         = m_msg.fileMd5;
    d->sizeBytes   = m_msg.fileSizeBytes;
    ::PostMessage(hwnd, WM_CHAT_FILE_ACTION, 0, (LPARAM)d);
}

int ChatBubble::FileLinkAtPoint(POINT pt) const
{
    if (m_msg.kind != kMsgFile)
    {
        return -1;
    }
    for (int bi = 0; bi < m_fileBtnCount; ++bi)
    {
        if (::PtInRect(&m_fileBtnRect[bi], pt))
        {
            return bi;
        }
    }
    return -1;
}

bool ChatBubble::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    // 正在拖拽选择文字：把当前点映射成字符下标作为选区另一端。
    if (m_selDragging)
    {
        const int idx = CharIndexFromHostPoint(pt);
        if (idx != m_selCaret)
        {
            m_selCaret = idx;
            Invalidate();
        }
        return true;
    }

    // 文件行：据落点更新底部操作链接的悬停高亮；悬停目标变化时才重绘（变深色 + 下划线）。
    if (m_msg.kind == kMsgFile)
    {
        const int idx = FileLinkAtPoint(pt);
        if (idx != m_fileHoverIdx)
        {
            m_fileHoverIdx = idx;
            Invalidate();
        }
    }
    return false;   // 不消费，鼠标移动交由框架继续分发
}

bool ChatBubble::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 结束拖拽选择：定下选区另一端并释放捕获。
    if (m_selDragging)
    {
        m_selCaret    = CharIndexFromHostPoint(pt);
        m_selDragging = false;
        ReleaseCapture();
        Invalidate();
        return true;
    }
    return false;
}

bool ChatBubble::OnMouseLeave()
{
    // 光标离开本行：清除文件链接悬停态并重绘。
    if (m_fileHoverIdx != -1)
    {
        m_fileHoverIdx = -1;
        Invalidate();
    }
    return false;
}

bool ChatBubble::OnSetCursor(POINT pt)
{
    // 光标落在文件操作链接上：显示手型光标（超链接感）。
    if (FileLinkAtPoint(pt) >= 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return false;
}

bool ChatBubble::OnLButtonDblClk(POINT pt, UINT /*mkFlags*/)
{
    // 文字气泡：双击选词。连点第三下（此时上一次双击已把整词选中）扩成全选整条 ——
    // DUI 层没有三击事件，靠"双击时发现已是整词选中"来近似识别第三下。
    if (!ParentInMultiSelect() && IsTextSelectable())
    {
        const int idx = CharIndexFromHostPoint(pt);
        int s = 0;
        int e = 0;
        WordRangeAt(m_selFlatText, idx, s, e);

        const int total = (int)m_selFlatText.size();
        int curS = 0;
        int curE = 0;
        NormalizeSelRange(m_selAnchor, m_selCaret, curS, curE);
        if (HasTextSelection() && curS == s && curE == e)
        {
            s = 0;         // 已经正好选中这个词 → 这是第三下，扩成整条
            e = total;
        }

        m_selAnchor   = s;
        m_selCaret    = e;
        m_selDragging = false;
        Invalidate();
        return true;
    }

    // 仅"已就绪的图片消息"（imageState==0，本地路径有效）响应双击放大。上传中 /
    // 下载中 / 失败态不开。
    if (m_msg.kind != kMsgImage || m_msg.imageState != 0)
    {
        return false;
    }
    if (m_msg.imagePath.IsEmpty())
    {
        return false;
    }
    // 双击点须落在图片矩形内（避开头像 / 空白）。
    if (!::PtInRect(&m_imageRect, pt))
    {
        return false;
    }

    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (hwnd == NULL)
    {
        return false;
    }

    // 同步发给宿主聊天窗：lParam 为图片本地路径。SendMessage 同步执行，期间
    // m_msg.imagePath 缓冲区有效；聊天窗在处理函数里据此打开看图窗。
    ::SendMessage(hwnd, WM_VIEW_CHAT_IMAGE, 0, (LPARAM)(LPCTSTR)m_msg.imagePath);
    return true;
}

bool ChatBubble::TryPopupMentionMenu(const POINT& pt, HWND hwnd)
{
    if (m_atsFlat.empty() || m_selLayout.m_lines.empty())
    {
        return false;   // 本条不含 @，或还没测量过
    }

    // 命中测试在"相对文本区原点"的坐标系里做，与排版模型同一套坐标。
    bool ok = false;
    const POINT origin = TextOriginInHost(ok);
    if (!ok)
    {
        return false;
    }

    std::vector<std::pair<int, int> > ranges;
    ranges.reserve(m_atsFlat.size());
    for (std::size_t i = 0; i < m_atsFlat.size(); ++i)
    {
        ranges.push_back(std::make_pair(m_atsFlat[i].m_off,
                                        m_atsFlat[i].m_off + m_atsFlat[i].m_len));
    }
    const int hit = HitTestCharRanges(m_selLayout, ranges, pt.x - origin.x, pt.y - origin.y);
    if (hit < 0)
    {
        return false;   // 点在正文或空白上，交回给气泡自己的菜单
    }

    const long long mentionUid = m_atsFlat[(std::size_t)hit].m_uid;
    if (!ShouldShowMentionMenu(mentionUid))
    {
        return false;   // @所有人：没有具体的人可操作，当普通文字处理
    }

    // 显示名 = 片段文字去掉开头的 '@'。取自扁平文本而不是原始正文 —— 片段偏移
    // 本来就是扁平口径（见 m_atsFlat 的说明）。
    std::wstring displayName;
    {
        const int begin = m_atsFlat[(std::size_t)hit].m_off;
        const int len   = m_atsFlat[(std::size_t)hit].m_len;
        if (begin >= 0 && len > 1 && (std::size_t)(begin + len) <= m_selFlatText.size())
        {
            displayName = m_selFlatText.substr((std::size_t)begin + 1, (std::size_t)len - 1);
        }
    }

    // 「@ 的是不是我自己」直接读片段上的 m_isMe —— 它在协议换算时就按本人 uid
    // 标好了（见 GroupMentionModel），气泡不必再去问谁是当前登录用户。
    const bool isSelf = m_atsFlat[(std::size_t)hit].m_isMe;
    ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
    const bool stillInGroup = (list != NULL) ? list->IsMemberInGroup(mentionUid) : true;

    // 本人与被 @ 者在本群的角色：菜单末尾的「设为 / 撤销管理员」据此决定要不要出现。
    // 两者都由消息列表代问宿主群聊窗 —— 气泡不认识"群成员表"这类业务对象。
    // 单聊窗没装查询钩子，两者均为负值，管理项自然不出现。
    const int myNetRole     = (list != NULL) ? list->MyNetRole() : -1;
    const int targetNetRole = (list != NULL) ? list->MemberNetRole(mentionUid) : -1;

    // selfUid 传法：BuildMentionMenuModel 只用它和 mentionUid 比对判本人，这里已经
    // 有现成的 isSelf，故把两者传成"相等 / 不等"即可，不必真去取本人 uid。
    const std::vector<MentionMenuEntry> model =
        BuildMentionMenuModel(mentionUid, isSelf ? mentionUid : 0, stillInGroup,
                              myNetRole, targetNetRole);
    if (model.empty())
    {
        return false;
    }

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return false;
    }
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].isSeparator)
        {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        const UINT flags = MF_STRING | (model[i].enabled ? MF_ENABLED : MF_GRAYED);
        ::AppendMenuW(menu, flags, (UINT_PTR)model[i].id, model[i].label);
    }

    // 落点必须夹进锚点所在显示器的工作区 —— 气泡靠近屏幕右 / 下边缘时，直接把光标
    // 坐标交给 TrackPopupMenu 会让菜单跑到桌面外看不见（开发约定文档的菜单不越屏规则）。
    // 菜单尺寸按行数估算：Win32 的 HMENU 在弹出前拿不到精确尺寸，估大一点即可 ——
    // 夹取只在"放不下"时才生效，估大只会让靠边时更早翻向左 / 上，不会误伤。
    POINT scr = pt;
    ::ClientToScreen(hwnd, &scr);
    SIZE menuSize;
    menuSize.cx = kMentionMenuEstWidth;
    menuSize.cy = (LONG)model.size() * kMentionMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(scr, menuSize);

    const int cmd = ::TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        at.x, at.y, 0, hwnd, NULL);
    ::DestroyMenu(menu);

    if (cmd == 0)
    {
        return true;   // 用户按 Esc / 点别处取消 —— 已经处理过了，不再弹气泡菜单
    }

    // 四个动作全要碰业务（打开单聊 / 资料窗、往输入框插字、写剪贴板），气泡不认识
    // 这些对象，打包投给宿主聊天窗去执行。投递失败时自行释放，避免泄漏。
    MentionActionData* pData = new MentionActionData();
    pData->id          = (MentionMenuId)cmd;
    pData->uid         = mentionUid;
    pData->displayName = displayName;
    if (!::PostMessage(hwnd, WM_CHAT_MENTION_ACTION, 0, (LPARAM)pData))
    {
        delete pData;
    }
    return true;
}

bool ChatBubble::OnRButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // 时间分隔 / 撤回提示行不弹菜单。
    if (!IsRealMessage())
    {
        return false;
    }
    ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
    if (list == NULL)
    {
        return false;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (hwnd == NULL)
    {
        return false;
    }

    // 先看这一下是不是点在某个 @某人 的片段上 —— 是的话弹「@ 的菜单」而不是气泡
    // 自己的菜单（复制 / 引用 / 撤回那套）。点空了照旧走下面的气泡菜单。
    if (TryPopupMentionMenu(pt, hwnd))
    {
        return true;
    }

    // 撤回只对自己发出、且仍在可撤回时限内的消息开放。时限判断与服务端同口径
    // （见 net::MessageProtocolClient::canRecallMessage）：超时的消息直接不显示该项，
    // 免得用户点了才被服务端以 error_code_recall_timeout 拒绝。翻上去的历史消息
    // 因此不再出现"撤回"。
    bool canRecall = false;
    if (m_msg.fromMe)
    {
        //本地当前时刻（秒 → 毫秒），与消息的服务器时间戳同口径比较。
        long long nowMs = (long long)::time(NULL) * 1000;
        canRecall = net::MessageProtocolClient::canRecallMessage(
            m_msg.msgid, m_msg.timestampMs, nowMs);
    }

    // 菜单该有哪几行交给纯模型算（图片行会多出「图片另存为」「再次发送」两项），
    // 这里只负责照着它建真菜单。
    // Pin 只要求服务器已给这条消息分配了 msgid —— 没有 msgid 时发过去必被判
    // message_not_exist，故整项不显示。取消 Pin 的权限不在这里预判：本端不一定知道
    // 一条历史消息是谁钉的，预判会出现"该显示的没显示"，交给服务端拒绝并提示。
    const bool canPin = (m_msg.msgid > 0);

    // 菜单该有哪几行交给纯模型算（图片行会多出「图片另存为」「再次发送」两项），
    // 这里只负责照着它建真菜单。
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(m_msg.kind, m_msg.imageState,
                                  !m_msg.imagePath.IsEmpty(), canRecall,
                                  canPin, m_msg.pinned);

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return false;
    }
    for (std::size_t mi = 0; mi < model.size(); ++mi)
    {
        if (model[mi].isSeparator)
        {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)model[mi].id, model[mi].label);
    }

    // pt 为 host 客户区坐标，TrackPopupMenu 要屏幕坐标。
    // 落点同样要夹进锚点所在显示器的工作区 —— 气泡靠近屏幕右 / 下边缘时（典型：
    // 消息区最后一行、窗口贴着任务栏），直接把光标坐标交给 TrackPopupMenu 会让菜单
    // 跑到桌面外看不见。这是开发约定文档里「弹出菜单必须整体落在工作区内」的要求，
    // 与上面 @ 菜单用的是同一个夹取函数。项数含分隔符，按整项高度估算偏大，只会让
    // 靠边时更早翻向左 / 上，不会误伤正常位置。
    POINT scr = pt;
    ::ClientToScreen(hwnd, &scr);
    SIZE msgMenuSize;
    msgMenuSize.cx = kMessageMenuEstWidth;
    msgMenuSize.cy = ::GetMenuItemCount(menu) * kMentionMenuEstItemHeight;
    const POINT msgMenuAt = balloonmain::ClampPopupMenuToWorkArea(scr, msgMenuSize);
    const int cmd = ::TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        msgMenuAt.x, msgMenuAt.y, 0, hwnd, NULL);
    ::DestroyMenu(menu);

    // 注意：kChatMsgMenuDelete 分支会经 RequestDeleteBubble 销毁 this，其后
    // 不得再访问任何成员 —— 各分支处理完立即 return。
    switch (cmd)
    {
    case kChatMsgMenuCopy:
        // 有选区就只复制选中的那一段，没有选区才复制整条 —— 不新增菜单项，
        // "复制"这一项按当前是否有选区自动切换含义，与浏览器 / 各家 IM 一致。
        if (HasTextSelection())
        {
            PutTextToClipboard(hwnd, (LPCWSTR)GetSelectedText());
        }
        else
        {
            PutTextToClipboard(hwnd, (LPCWSTR)MessageCopyText(m_msg));
        }
        return true;
    case kChatMsgMenuSaveImageAs:
        PostImageMenuAction(hwnd, kChatMsgMenuSaveImageAs);
        return true;
    case kChatMsgMenuResendImage:
        PostImageMenuAction(hwnd, kChatMsgMenuResendImage);
        return true;
    case kChatMsgMenuReply:
        list->RequestReplyBubble(this);
        return true;
    case kChatMsgMenuForward:
        list->RequestForwardBubble(this);
        return true;
    case kChatMsgMenuRecall:
        list->RequestRecallBubble(this);
        return true;
    case kChatMsgMenuPin:
        // 界面不就地改：把请求投给宿主窗发出去，等服务端推 cmd_message_pinned_notify
        // 回来再改标记。与撤回同口径，这样本端与对端看到的结果必然一致。
        list->RequestPinBubble(this, !m_msg.pinned);
        return true;
    case kChatMsgMenuMultiSelect:
        list->EnterMultiSelectWith(this);
        return true;
    case kChatMsgMenuDelete:
        list->RequestDeleteBubble(this);   // 此调用会销毁 this
        return true;
    default:
        return true;
    }
}

void ChatBubble::PostImageMenuAction(HWND hwnd, int menuId)
{
    // 另存为要弹模态文件对话框、再次发送要走宿主窗的发图链路，气泡两样都不该碰，
    // 打包投给宿主聊天窗执行。异步 PostMessage，故堆分配传递；投递失败自行释放。
    ChatImageActionData* d = new ChatImageActionData();
    d->id        = (ChatMessageMenuId)menuId;
    d->imagePath = (LPCWSTR)m_msg.imagePath;
    d->saveName  = MakeImageSaveAsDefaultName(d->imagePath, m_msg.msgid);
    if (!::PostMessage(hwnd, WM_CHAT_IMAGE_ACTION, 0, (LPARAM)d))
    {
        delete d;
    }
}

void ChatBubble::OnPaint(HDC hdc, const RECT& rcInter)
{
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 图片 / 文件行：逐帧实绘 —— 绘制时要更新 m_imageRect / m_fileBtnRect 命中矩形，
    // 不能缓存。
    if (!IsRenderCacheable())
    {
        RenderContent(hdc);
        return;
    }

    // 文字 / emoji / 时间分隔行：内容 / 行宽高 / 多选态都不变就复用缓存位图，省掉每帧
    // 用 GDI+ 重画整条气泡 —— 拖动分隔条 / 滚动时这是主要的每帧开销。
    const bool multi = ParentInMultiSelect();
    if (m_renderDirty || m_renderDC == NULL
        || m_renderW != w || m_renderH != h || m_renderMultiSel != multi)
    {
        EnsureRenderCache(hdc, w, h);
        if (m_renderDC != NULL)
        {
            // 缓存位图先铺消息行底色（与 ChatMessageList 行底一致），气泡抗锯齿边据此
            // 混色；再把本行按"矩形平移到原点"画进缓存（复用 RenderContent 的相对坐标）。
            HBRUSH bg = ::CreateSolidBrush(RGB(0xFA, 0xF9, 0xF6));
            RECT lr = { 0, 0, w, h };
            ::FillRect(m_renderDC, &lr, bg);
            ::DeleteObject(bg);

            const RECT saved = m_rcItem;
            ::OffsetRect(&m_rcItem, -saved.left, -saved.top);   // 行矩形临时移到 (0,0)
            RenderContent(m_renderDC);
            m_rcItem = saved;

            m_renderDirty    = false;
            m_renderMultiSel = multi;
        }
    }

    if (m_renderDC != NULL)
    {
        // 只把本行与脏区相交的那块从缓存位图搬回 hdc。
        RECT inter;
        if (::IntersectRect(&inter, &m_rcItem, &rcInter))
        {
            ::BitBlt(hdc, inter.left, inter.top,
                     inter.right - inter.left, inter.bottom - inter.top,
                     m_renderDC,
                     inter.left - m_rcItem.left, inter.top - m_rcItem.top,
                     SRCCOPY);
        }
    }
    else
    {
        RenderContent(hdc);   // 缓存创建失败的退化路径：直接实绘
    }
}

// 把本行按 m_rcItem 的相对坐标实际绘制到 hdc（原 OnPaint 主体，逻辑未变）。
void ChatBubble::RenderContent(HDC hdc)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // ---- 时间分隔 / 撤回提示 ----
    if (m_msg.kind == kMsgTime)
    {
        DrawGdiText(hdc, (LPCWSTR)m_msg.text, m_rcItem, 11, false, kTimeColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        return;
    }

    const bool isMe = m_msg.fromMe;
    // 群聊：对方气泡上方显示发送者名。
    const bool showSender = (!isMe && !m_msg.senderName.IsEmpty());

    // 多选模式：行首让出 kCheckColW 宽的勾选列，气泡内容整体右移。
    const bool multi = ParentInMultiSelect();
    const int  selW  = multi ? kCheckColW : 0;
    const int  cx    = x + selW;   // 内容区左边界
    const int  cw    = w - selW;   // 内容区宽

    // 头像底色 / 字：优先用消息自带的发送者头像（真实单聊每条都带本端/对端
    // 头像；群聊每条发送者不同）；无则回退到 mock 的 ChatMe / ChatPartner。
    CString  avatarText;
    COLORREF avatarColor = RGB(0xFF, 0x7A, 0x45);
    if (!m_msg.senderAvatarText.IsEmpty())
    {
        avatarText  = m_msg.senderAvatarText;
        avatarColor = m_msg.senderAvatarColor;
    }
    else if (isMe)
    {
        avatarText  = ChatMe().avatarText;
        avatarColor = ChatMe().avatarColor;
    }
    else
    {
        avatarText  = ChatPartner().avatarText;
        avatarColor = ChatPartner().avatarColor;
    }

    // ---- 头像位置 ----
    const int avX = isMe ? (cx + cw - kEdgePad - kAvatarSize) : (cx + kEdgePad);
    const int avY = y + kRowVMargin;

    // ---- 气泡尺寸 ----
    const int maxBubbleW = (int)(cw * kBubbleMaxWidthRatio);
    int bubbleW = 0;
    int bubbleH = kTextBubbleH;
    switch (m_msg.kind)
    {
    case kMsgText:
        if (m_measuredWidth >= 0)
        {
            bubbleW = m_textBubbleW;
            bubbleH = m_textBubbleH;
        }
        else
        {
            // 尚未折行测量时的单行回退：按本条消息字号估宽。
            bubbleW = MeasureTextWidth(hdc, (LPCWSTR)m_msg.text,
                                       ResolveTextStyle(m_msg).sizePx)
                      + kTextPadH * 2;
            bubbleH = kTextBubbleH;
        }
        if (bubbleW > maxBubbleW)
        {
            bubbleW = maxBubbleW;
        }
        break;
    case kMsgEmoji:
        bubbleW = 56;
        bubbleH = kEmojiBubbleH;
        break;
    case kMsgImage:
        bubbleW = m_msg.imageW + kImageBubblePad;
        bubbleH = m_msg.imageH + kImageBubblePad;
        break;
    case kMsgFile:
        bubbleW = kFileBubbleW;
        bubbleH = FileBubbleHeight(m_msg.fileState, m_msg.fromMe);
        break;
    default:
        break;
    }

    // 气泡列：自己靠右、对方靠左。
    const int colLeft  = avX + kAvatarSize + kAvatarGap;
    const int colRight = avX - kAvatarGap;
    const int bubbleX  = isMe ? (colRight - bubbleW) : colLeft;

    int contentTop = y + kRowVMargin;
    // 群聊：对方气泡上方留出发送者名一行（名字本身在文字层绘制）。
    if (showSender)
    {
        contentTop += kSenderNameHeight;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // ---- 头像 ----
    // 真图头像(facetype=2)命中本地缓存时画圆角真图，否则填底色 + 后面画字形。
    bool drewAvatarImage = false;
    {
        Gdiplus::GraphicsPath av;
        AddRoundRect(av, (Gdiplus::REAL)avX, (Gdiplus::REAL)avY,
                     (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                     kAvatarRadius);

        // 首次绘制按路径解码头像真图并缓存（失败也置位，避免每帧重复解码）。
        if (!m_avatarLoadTried && !m_msg.senderAvatarImagePath.IsEmpty())
        {
            m_avatarLoadTried = true;
            Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(
                (LPCWSTR)m_msg.senderAvatarImagePath, FALSE);
            if (bmp != NULL && bmp->GetLastStatus() == Gdiplus::Ok)
            {
                m_avatarBmp = bmp;
            }
            else
            {
                delete bmp;
            }
        }

        if (m_avatarBmp != NULL)
        {
            // 缓存命中：把真图按头像圆角矩形裁切后高质量缩放绘制。
            Gdiplus::Rect avRect(avX, avY, kAvatarSize, kAvatarSize);
            Gdiplus::InterpolationMode oldMode = g.GetInterpolationMode();
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetClip(&av);
            g.DrawImage(m_avatarBmp, avRect);
            g.ResetClip();
            g.SetInterpolationMode(oldMode);
            drewAvatarImage = true;
        }
        else
        {
            // 无真图 / 解码失败：填底色，字形稍后在文字层绘制。
            Gdiplus::SolidBrush avBrush(Gdiplus::Color(255,
                GetRValue(avatarColor), GetGValue(avatarColor),
                GetBValue(avatarColor)));
            g.FillPath(&avBrush, &av);
        }
    }

    // ---- 气泡底 ----
    const int bubbleY = contentTop;
    {
        Gdiplus::GraphicsPath bub;
        AddRoundRect(bub, (Gdiplus::REAL)bubbleX, (Gdiplus::REAL)bubbleY,
                     (Gdiplus::REAL)bubbleW, (Gdiplus::REAL)bubbleH, kBubbleRadius);
        // 文件"已删除 / 失效"态：整条气泡置灰（淡化），区别于正常发 / 收气泡。
        Gdiplus::Color bg = isMe ? kBubbleSelf : kBubbleOther;
        if (m_msg.kind == kMsgFile && m_msg.fileState == kFileDeleted)
        {
            bg = Gdiplus::Color(255, 0xED, 0xEA, 0xE8);
        }
        Gdiplus::SolidBrush bubBrush(bg);
        g.FillPath(&bubBrush, &bub);
    }

    // ---- 图片占位 / 文件类型块（GDI+）----
    if (m_msg.kind == kMsgImage)
    {
        const Gdiplus::Rect imgRect(bubbleX + 4, bubbleY + 4,
                                    m_msg.imageW, m_msg.imageH);

        // 记下图片在宿主坐标系里的矩形，供双击命中测试（双击图片打开看图窗）。
        m_imageRect.left   = bubbleX + 4;
        m_imageRect.top    = bubbleY + 4;
        m_imageRect.right  = bubbleX + 4 + m_msg.imageW;
        m_imageRect.bottom = bubbleY + 4 + m_msg.imageH;

        // 用 ChatImageCache 取"后台解码 + 缩放好的展示尺寸图"：命中即直接 1:1 画；未命中
        // 则发起一次后台请求并先画占位 —— 原图解码与首次缩放都在后台线程做，UI 线程零
        // 冻结（后台完成后会 PostMessage(WM_CHAT_IMAGE_READY) 让聊天窗重绘，届时命中缓存）。
        std::wstring imgPath((LPCWSTR)m_msg.imagePath);
        Gdiplus::Bitmap* ready = (m_msg.imageW > 0 && m_msg.imageH > 0)
            ? ChatImageCache::Inst().Get(imgPath, m_msg.imageW, m_msg.imageH)
            : NULL;
        if (ready != NULL)
        {
            g.SetClip(imgRect);
            g.DrawImage(ready, imgRect);
            g.ResetClip();
        }
        else
        {
            if (!m_msg.imagePath.IsEmpty())
            {
                ChatImageCache::Inst().Request(imgPath, m_msg.imageW, m_msg.imageH);
            }
            // 未就绪 / 无图：占位块 + 斜向纹理（后台缩放好后会重绘换成真图）。
            Gdiplus::SolidBrush imgBrush(Gdiplus::Color(255, 0xEC, 0xEA, 0xE6));
            g.FillRectangle(&imgBrush, imgRect);
            g.SetClip(imgRect);
            Gdiplus::Pen hatch(Gdiplus::Color(40, 0, 0, 0), 1.0f);
            for (int hx = bubbleX + 4 - m_msg.imageH; hx < bubbleX + 4 + m_msg.imageW;
                 hx += 14)
            {
                g.DrawLine(&hatch, (Gdiplus::REAL)hx, (Gdiplus::REAL)(bubbleY + 4),
                           (Gdiplus::REAL)(hx + m_msg.imageH),
                           (Gdiplus::REAL)(bubbleY + 4 + m_msg.imageH));
            }
            g.ResetClip();
        }

        // 上传中 / 下载中 / 失败：盖一层半透明遮罩（状态文字在后面的 GDI 文字层画）。
        if (m_msg.imageState == 1 || m_msg.imageState == 3)
        {
            //上传中 / 下载中：半透明黑遮罩，衬白色"上传中 NN%" / "下载中"
            Gdiplus::SolidBrush mask(Gdiplus::Color(110, 0, 0, 0));
            g.FillRectangle(&mask, imgRect);
        }
        else if (m_msg.imageState == 2)
        {
            //失败：半透明红遮罩，衬白色"发送失败"
            Gdiplus::SolidBrush mask(Gdiplus::Color(90, 0xC0, 0x30, 0x30));
            g.FillRectangle(&mask, imgRect);
        }
    }
    else if (m_msg.kind == kMsgFile)
    {
        const int  st      = m_msg.fileState;
        const bool deleted = (st == kFileDeleted);
        const int  iconX   = bubbleX + kFilePadH;
        const int  iconY   = bubbleY + kFileIconTop;

        // ---- 类型图标（圆角方块）：正常用类型色，已删除用灰 ----
        COLORREF ftc = deleted ? RGB(0xB8, 0xB5, 0xB1) : FileTypeColor(m_msg.fileType);
        {
            Gdiplus::GraphicsPath ftPath;
            AddRoundRect(ftPath, (Gdiplus::REAL)iconX, (Gdiplus::REAL)iconY,
                         (Gdiplus::REAL)kFileIconSize, (Gdiplus::REAL)kFileIconSize, 6.0f);
            Gdiplus::SolidBrush ftBrush(Gdiplus::Color(255,
                GetRValue(ftc), GetGValue(ftc), GetBValue(ftc)));
            g.FillPath(&ftBrush, &ftPath);
        }

        // 已删除：图标上画一个白色垃圾桶轮廓（盖 + 提手 + 桶身 + 竖纹）。
        if (deleted)
        {
            Gdiplus::Pen   trash(Gdiplus::Color(255, 255, 255, 255), 1.6f);
            Gdiplus::REAL  cx = (Gdiplus::REAL)(iconX + kFileIconSize / 2);
            Gdiplus::REAL  ty = (Gdiplus::REAL)(iconY + 14);   // 盖的 y
            g.DrawLine(&trash, cx - 9, ty, cx + 9, ty);                 // 盖
            g.DrawLine(&trash, cx - 3, ty - 3, cx + 3, ty - 3);         // 提手
            g.DrawLine(&trash, cx - 3, ty - 3, cx - 3, ty);
            g.DrawLine(&trash, cx + 3, ty - 3, cx + 3, ty);
            const Gdiplus::REAL bx = cx - 7, bw = 14, byTop = ty + 3, bh = 12;
            g.DrawLine(&trash, bx, byTop, bx + 1.5f, byTop + bh);       // 桶身左壁
            g.DrawLine(&trash, bx + bw, byTop, bx + bw - 1.5f, byTop + bh);  // 右壁
            g.DrawLine(&trash, bx + 1.5f, byTop + bh, bx + bw - 1.5f, byTop + bh);  // 底
            g.DrawLine(&trash, cx - 3, byTop + 2, cx - 3, byTop + bh - 2);   // 竖纹
            g.DrawLine(&trash, cx + 3, byTop + 2, cx + 3, byTop + bh - 2);
        }

        // ---- 状态角标（图标左下角小圆）：完成绿✓、失败红×、已取消灰停 ----
        if (st == kFileNormal || st == kFileFailed || st == kFileCancelled)
        {
            const Gdiplus::REAL bcx = (Gdiplus::REAL)(iconX + 3);
            const Gdiplus::REAL bcy = (Gdiplus::REAL)(iconY + kFileIconSize - 3);
            const Gdiplus::REAL r   = (Gdiplus::REAL)kFileBadgeR;
            COLORREF badgeCol = (st == kFileNormal) ? RGB(0x3F, 0xB9, 0x50)
                              : (st == kFileFailed) ? RGB(0xE5, 0x39, 0x35)
                              :                       RGB(0x9A, 0x97, 0x92);
            Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&white, bcx - r - 1.5f, bcy - r - 1.5f, (r + 1.5f) * 2, (r + 1.5f) * 2);
            Gdiplus::SolidBrush bb(Gdiplus::Color(255,
                GetRValue(badgeCol), GetGValue(badgeCol), GetBValue(badgeCol)));
            g.FillEllipse(&bb, bcx - r, bcy - r, r * 2, r * 2);
            Gdiplus::Pen mark(Gdiplus::Color(255, 255, 255, 255), 1.8f);
            if (st == kFileNormal)
            {
                g.DrawLine(&mark, bcx - 4, bcy, bcx - 1, bcy + 3);       // ✓
                g.DrawLine(&mark, bcx - 1, bcy + 3, bcx + 4, bcy - 3);
            }
            else if (st == kFileFailed)
            {
                g.DrawLine(&mark, bcx - 3, bcy - 3, bcx + 3, bcy + 3);   // ×
                g.DrawLine(&mark, bcx + 3, bcy - 3, bcx - 3, bcy + 3);
            }
            else
            {
                Gdiplus::SolidBrush sq(Gdiplus::Color(255, 255, 255, 255));
                g.FillRectangle(&sq, bcx - 3, bcy - 3, 6.0f, 6.0f);       // 停（白方块）
            }
        }

        // ---- 进度条（上传 / 下载 / 失败 / 已取消）----
        if (FileStateHasProgress(st))
        {
            const int barX = bubbleX + kFilePadH;
            const int barY = bubbleY + kFileHeaderH;
            const int barW = bubbleW - kFilePadH * 2;
            {
                Gdiplus::GraphicsPath track;
                AddRoundRect(track, (Gdiplus::REAL)barX, (Gdiplus::REAL)barY,
                             (Gdiplus::REAL)barW, (Gdiplus::REAL)kFileProgressH, 2.5f);
                Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 0xE3, 0xE0, 0xDC));
                g.FillPath(&trackBrush, &track);
            }
            int pct = m_msg.fileProgress;
            if (pct < 0)   pct = 0;
            if (pct > 100) pct = 100;
            const int fillW = barW * pct / 100;
            if (fillW > 0)
            {
                COLORREF pc = (st == kFileUploading)   ? RGB(0xFF, 0x7A, 0x45)   // 橙
                            : (st == kFileDownloading) ? RGB(0x2F, 0x80, 0xED)   // 蓝
                            : (st == kFileFailed)      ? RGB(0xE5, 0x39, 0x35)   // 红
                            :                            RGB(0xB0, 0xAD, 0xA8);  // 灰（已取消）
                Gdiplus::GraphicsPath fill;
                AddRoundRect(fill, (Gdiplus::REAL)barX, (Gdiplus::REAL)barY,
                             (Gdiplus::REAL)fillW, (Gdiplus::REAL)kFileProgressH, 2.5f);
                Gdiplus::SolidBrush fb(Gdiplus::Color(255,
                    GetRValue(pc), GetGValue(pc), GetBValue(pc)));
                g.FillPath(&fb, &fill);
            }
        }

        // ---- 底部操作改为文字超链接（无按钮底 / 边框）。其文字与命中矩形在下面的
        //      GDI 文字层按各自文字宽度从左到右排布、就地填 m_fileBtnRect / m_fileBtnCount；
        //      此处先清空（仅文件行需要）。----
        m_fileBtnCount = 0;
        for (int bi = 0; bi < 3; ++bi)
            ::SetRectEmpty(&m_fileBtnRect[bi]);
    }

    // ---- 文字层（GDI）----

    // 头像字母（已画真图则跳过，避免字形盖在真图上）。
    if (!drewAvatarImage && !avatarText.IsEmpty())
    {
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        DrawGdiText(hdc, (LPCWSTR)avatarText, avRc, 16, true, kAvatarText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 群聊发送者名（对方气泡上方）。
    if (showSender)
    {
        RECT snRc = { bubbleX + 2, y + kRowVMargin,
                      cx + cw, y + kRowVMargin + kSenderNameHeight };
        DrawGdiText(hdc, (LPCWSTR)m_msg.senderName, snRc, 11, false,
                    RGB(0x8A, 0x8A, 0x8A),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 引用块（气泡内部顶端）：左侧一条竖线 + 两行文字（人名 / 被引用内容）。
    // 位置与 TextOriginInHost、Measure 里的算法必须一致 —— 三处都按
    // 「气泡内上边距之下、正文之上」这一块来算，改一处就要一起改。
    if (m_msg.hasQuote)
    {
        const int qLeft  = bubbleX + kTextPadH;
        const int qTop    = bubbleY + kTextPadV;
        const int qRight = bubbleX + bubbleW - kTextPadH;

        // 左侧竖线：标明这一块是引用而不是正文。
        RECT barRc = { qLeft, qTop, qLeft + kQuoteBarW, qTop + kQuoteBlockH };
        HBRUSH barBrush = ::CreateSolidBrush(kQuoteName);
        if (barBrush != NULL)
        {
            ::FillRect(hdc, &barRc, barBrush);
            ::DeleteObject(barBrush);
        }

        // 被引用的是图片时，竖线右侧先画一张方形缩略图，文字再往右让开。
        int qTextL = qLeft + kQuoteBarW + kQuoteBarGap;
        if (m_msg.quoteMsgType == kMsgImage)
        {
            const Gdiplus::Rect thumbRc(qTextL, qTop, kQuoteThumbSize, kQuoteThumbSize);
            Gdiplus::Graphics gq(hdc);
            gq.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            // 与图片气泡同一套缓存：命中就画，未命中先入队后台解码 + 缩放，
            // 完成后 ChatImageCache 会 PostMessage(WM_CHAT_IMAGE_READY) 让聊天窗重绘。
            std::wstring thumbPath((LPCWSTR)m_msg.quoteImagePath);
            Gdiplus::Bitmap* thumb = thumbPath.empty()
                ? NULL
                : ChatImageCache::Inst().Get(thumbPath, kQuoteThumbSize, kQuoteThumbSize);
            if (thumb != NULL)
            {
                // 显式裁到目标方框内再画。缓存返回的位图未必正好是这个尺寸，
                // 不裁的话长宽比不同的图会画出方框、溢出到气泡外（与图片气泡同样处理）。
                gq.SetClip(thumbRc);
                gq.DrawImage(thumb, thumbRc);
                gq.ResetClip();
            }
            else
            {
                if (!thumbPath.empty())
                {
                    ChatImageCache::Inst().Request(thumbPath, kQuoteThumbSize, kQuoteThumbSize);
                    // 每帧都会走到这里，故只在第一次入队时记一条，避免刷屏。
                    // 若这条只出现一两次之后缩略图仍不出现，说明解码没回来或失败；
                    // 若反复出现，说明每次重绘都当成新请求 —— 那是 key 对不上。
                    if (!m_quoteThumbLogged)
                    {
                        m_quoteThumbLogged = true;
                        LOG_INFO("引用缩略图入队解码: %dx%d, path=%s.",
                                 kQuoteThumbSize, kQuoteThumbSize,
                                 (LPCSTR)CW2A(m_msg.quoteImagePath, CP_UTF8));
                    }
                }
                // 未就绪（或本端还没下到这张图）：先画一个浅灰占位块，
                // 下载 / 解码完成后重绘自然换成真图。
                Gdiplus::SolidBrush ph(Gdiplus::Color(255, 0xEC, 0xEA, 0xE6));
                gq.FillRectangle(&ph, thumbRc);
            }
            qTextL += kQuoteThumbSize + kQuoteThumbGap;
        }
        RECT qnRc = { qTextL, qTop, qRight, qTop + kQuoteLineH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.quoteName, qnRc, 11, false, kQuoteName,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 第二行：被引用内容。过长省略号收尾 —— 引用块高度固定，绝不因内容长短变形。
        // 记下引用块矩形供左键命中用。转成行内相对坐标 —— 绘制可能发生在渲染缓存里，
        // 那时 m_rcItem 被临时移到 (0,0)，存宿主坐标会失效。
        m_quoteRectRel.left   = qLeft - m_rcItem.left;
        m_quoteRectRel.top    = qTop - m_rcItem.top;
        m_quoteRectRel.right  = qRight - m_rcItem.left;
        m_quoteRectRel.bottom = qTop + kQuoteBlockH - m_rcItem.top;

        RECT qcRc = { qTextL, qTop + kQuoteLineH + 4, qRight, qTop + kQuoteBlockH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.quoteText, qcRc, 11, false, kQuoteText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    // 气泡内容文字。
    if (m_msg.kind == kMsgText)
    {
        RECT txRc = { bubbleX + kTextPadH, bubbleY + kTextPadV,
                      bubbleX + bubbleW - kTextPadH, bubbleY + bubbleH - kTextPadV };
        if (m_msg.hasQuote)
        {
            // 引用块占据气泡内部顶端，正文从它下方开始。这个偏移与 Measure 里
            // 计入气泡高度、以及 TextOriginInHost 里算选区原点的那两处必须一致。
            txRc.top += kQuoteBlockH + kQuoteGap;
        }
        // 按本条消息的字体设置渲染（默认时等同原 kBubbleTextPx / 默认色 / 不加粗）。
        const BubbleTextStyle st = ResolveTextStyle(m_msg);
        // 所有文字气泡（纯文字 / 含表情 / 含 @）走同一条手写折行排版，与 Measure
        // 产出选区模型的那次排版是同一套规则、同一套字体。分四层，自下而上：
        //   一、@ 片段的圆角底色块
        //   二、选中文字的高亮底
        //   三、正文（表情画成小图，文字用正文色）
        //   四、@ 片段按矩形裁剪、用高亮色重画那几个字
        // 底色块在最下面，选区高亮压在它上面，文字再压在两者之上，谁都不会被盖住。
        POINT origin;
        origin.x = txRc.left;
        origin.y = txRc.top;

        std::vector<balloonchat::ChatSegment> segs =
            balloonchat::ParseSendBody(m_msg.text);

        DrawMentionBlocks(hdc, m_selLayout, m_atsFlat, m_msg.fromMe, origin);
        DrawTextSelection(hdc, origin);
        DrawInlineContent(hdc, segs, txRc, st, kFaceDisplayPx);
        DrawMentionHighlightText(hdc, segs, m_selLayout, m_atsFlat, m_msg.fromMe,
                                 txRc, st, kFaceDisplayPx);
    }
    else if (m_msg.kind == kMsgEmoji)
    {
        RECT emRc = { bubbleX, bubbleY, bubbleX + bubbleW, bubbleY + bubbleH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.text, emRc, kEmojiPx, false, kBubbleText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
    else if (m_msg.kind == kMsgImage)
    {
        // 覆盖文字：上传中显示进度、下载中显示"下载中"、失败显示"发送失败"；
        // 正常态回退到占位标签（真实图片占位标签一般为空，不画字）。
        CString overlay;
        if (m_msg.imageState == 1)
        {
            overlay.Format(_T("上传中 %d%%"), m_msg.imageProgress);
        }
        else if (m_msg.imageState == 3)
        {
            overlay = _T("下载中…");
        }
        else if (m_msg.imageState == 2)
        {
            overlay = _T("发送失败");
        }
        else
        {
            overlay = m_msg.imageCaption;
        }

        if (!overlay.IsEmpty())
        {
            RECT capRc = { bubbleX + 4, bubbleY + 4,
                           bubbleX + 4 + m_msg.imageW, bubbleY + 4 + m_msg.imageH };
            // 有遮罩（上传中 / 失败）时用白字；正常占位文字用浅灰。
            COLORREF capColor = (m_msg.imageState != 0)
                ? RGB(255, 255, 255) : RGB(0x9A, 0x97, 0x92);
            DrawGdiText(hdc, (LPCWSTR)overlay, capRc, 12, false, capColor,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }
    }
    else if (m_msg.kind == kMsgFile)
    {
        const int  st      = m_msg.fileState;
        const bool deleted = (st == kFileDeleted);
        const int  iconX   = bubbleX + kFilePadH;
        const int  iconY   = bubbleY + kFileIconTop;
        const int  textX   = iconX + kFileIconSize + 12;          // 名 / 状态左
        const int  textR   = bubbleX + bubbleW - kFilePadH;       // 名 / 状态右

        // 类型字（已删除处是垃圾桶，不画类型字）。
        if (!deleted)
        {
            CString typeUpper = m_msg.fileType;
            typeUpper.MakeUpper();
            RECT ftRc = { iconX, iconY, iconX + kFileIconSize, iconY + kFileIconSize };
            DrawGdiText(hdc, (LPCWSTR)typeUpper, ftRc, 10, true, RGB(255, 255, 255),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }

        // 文件名（已删除：灰 + 删除线）。
        const COLORREF nameColor = deleted ? RGB(0x9A, 0x97, 0x92) : kBubbleText;
        RECT fnRc = { textX, bubbleY + kFileNameTop, textR, bubbleY + kFileNameTop + kFileNameH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.fileName, fnRc, 14, false, nameColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    false, false, deleted /*strike*/);

        // 状态行："{大小} · {状态}"：大小段灰，状态段按态着色；失败 / 删除前带小图标。
        const int statusY = bubbleY + kFileStatusTop;
        const int statusB = statusY + kFileStatusH;
        CString sizePrefix = m_msg.fileSize + _T(" · ");
        RECT spRc = { textX, statusY, textR, statusB };
        DrawGdiText(hdc, (LPCWSTR)sizePrefix, spRc, 11, false, RGB(0x8A, 0x8A, 0x8A),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        int sx = textX + MeasureTextWidth(hdc, (LPCWSTR)sizePrefix, 11);

        if (st == kFileFailed)
        {
            // 警告三角（红底白叹号）。
            const Gdiplus::REAL tcx = (Gdiplus::REAL)(sx + 6);
            const Gdiplus::REAL tcy = (Gdiplus::REAL)((statusY + statusB) / 2);
            Gdiplus::PointF tri[3] = {
                Gdiplus::PointF(tcx, tcy - 6),
                Gdiplus::PointF(tcx - 6, tcy + 5),
                Gdiplus::PointF(tcx + 6, tcy + 5) };
            Gdiplus::SolidBrush warn(Gdiplus::Color(255, 0xE5, 0x39, 0x35));
            g.FillPolygon(&warn, tri, 3);
            Gdiplus::Pen excl(Gdiplus::Color(255, 255, 255, 255), 1.4f);
            g.DrawLine(&excl, tcx, tcy - 2, tcx, tcy + 1);
            sx += 16;
        }
        else if (deleted)
        {
            // 信息圆（灰底白 i）。
            const Gdiplus::REAL icx = (Gdiplus::REAL)(sx + 6);
            const Gdiplus::REAL icy = (Gdiplus::REAL)((statusY + statusB) / 2);
            Gdiplus::SolidBrush info(Gdiplus::Color(255, 0x9A, 0x97, 0x92));
            g.FillEllipse(&info, icx - 6, icy - 6, 12.0f, 12.0f);
            Gdiplus::Pen ip(Gdiplus::Color(255, 255, 255, 255), 1.4f);
            g.DrawLine(&ip, icx, icy - 1, icx, icy + 3);
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, 255, 255, 255));
            g.FillRectangle(&dot, icx - 0.7f, icy - 4.0f, 1.4f, 1.4f);
            sx += 16;
        }

        CString  statusText;
        COLORREF statusColor = RGB(0x8A, 0x8A, 0x8A);
        bool     statusBold  = false;
        if (st == kFileUploading)
        {
            statusText.Format(_T("传送中 %d%%"), m_msg.fileProgress);
            statusColor = RGB(0xFF, 0x6A, 0x3D);
            statusBold  = true;
        }
        else if (st == kFileDownloading)
        {
            statusText.Format(_T("下载中 %d%%"), m_msg.fileProgress);
        }
        else if (st == kFileNormal)
        {
            statusText  = m_msg.fromMe ? _T("传送完成") : _T("已完成");
            statusColor = RGB(0xFF, 0x7A, 0x45);
            statusBold  = true;
        }
        else if (st == kFileFailed)
        {
            statusText  = _T("传输失败");
            statusColor = RGB(0xE5, 0x39, 0x35);
            statusBold  = true;
        }
        else if (st == kFileCancelled)
        {
            statusText = _T("已取消");
        }
        else if (st == kFilePendingAccept)
        {
            //还没开始下载，提示用户需要自己决定收不收
            statusText  = _T("等待接收");
            statusColor = RGB(0xFF, 0x7A, 0x45);
            statusBold  = true;
        }
        else if (st == kFileRejected)
        {
            statusText  = _T("已拒绝接收");
            statusColor = RGB(0x9A, 0x97, 0x92);
        }
        else   // deleted
        {
            statusText  = _T("文件已删除，无法下载");
            statusColor = RGB(0x9A, 0x97, 0x92);
        }

        // 本端发出的文件：只要对方有过动作，状态行就改说对方那边的进展 —— 自己传完了
        // 不等于对方收了，后者才是发送方真正关心的。覆盖上面按 fileState 算出的文字。
        if (m_msg.fromMe && m_msg.filePeerState != 0)
        {
            if (m_msg.filePeerState == kFilePeerReceiving)
            {
                statusText  = _T("对方正在接收");
                statusColor = RGB(0xFF, 0x7A, 0x45);
                statusBold  = true;
            }
            else if (m_msg.filePeerState == kFilePeerReceived)
            {
                statusText  = _T("对方已接收");
                statusColor = RGB(0x3B, 0xA5, 0x5D);   // 绿：这条文件已经安全送达
                statusBold  = true;
            }
            else if (m_msg.filePeerState == kFilePeerRejected)
            {
                statusText  = _T("对方已拒绝接收");
                statusColor = RGB(0x9A, 0x97, 0x92);
                statusBold  = false;
            }
        }

        RECT stRc = { sx, statusY, textR, statusB };
        DrawGdiText(hdc, (LPCWSTR)statusText, stRc, 11, statusBold, statusColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

        // ---- 底部操作：文字超链接（无按钮底 / 边框）。按各自文字宽度从左到右排布，
        //      整行高度作命中矩形（便于点击），就地填 m_fileBtnRect / m_fileBtnAction。----
        int acts[balloonchatwnd::kMaxChatFileActions];
        m_fileBtnCount = FileButtonActions(st, m_msg.fromMe, acts);
        if (m_fileBtnCount > 0)
        {
            const int kLinkPx  = 13;   // 超链接文字字号
            const int kLinkGap = 18;   // 相邻超链接水平间距
            const int rowTop    = bubbleY + kFileHeaderH
                                + (FileStateHasProgress(st) ? kFileProgressH : 0)
                                + kFileBtnTopGap;
            const int rowBottom = rowTop + kFileBtnH;
            int linkX = bubbleX + kFilePadH;
            for (int bi = 0; bi < m_fileBtnCount; ++bi)
            {
                const int act = acts[bi];
                CString   label;
                COLORREF  lblColor = RGB(0xFF, 0x7A, 0x45);   // 默认主题橙（可点链接）
                switch (act)
                {
                case kChatFileOpen:
                    label = _T("打开文件");
                    break;
                case kChatFileOpenFolder:
                    label = _T("打开所在文件夹");
                    break;
                case kChatFileSaveAs:
                    label = _T("另存为");
                    break;
                case kChatFileCancel:
                    label    = _T("取消");
                    lblColor = RGB(0x8A, 0x8A, 0x8A);   // 取消：灰色链接
                    break;
                case kChatFileAccept:
                    //动作相同（都是开始下载），文案随来源变：对方发来的是决定"收不收"，
                    //自己发出的谈不上接收，只是把它从服务器取回本地。主动作用主题橙突出。
                    label = m_msg.fromMe ? _T("下载") : _T("接收");
                    break;
                case kChatFileReject:
                    label    = _T("拒绝");
                    lblColor = RGB(0x8A, 0x8A, 0x8A);   // 次要动作：灰色链接
                    break;
                case kChatFileRetry:
                default:
                    // \x21BB = ↻ 循环箭头。是"重发"还是"重下"按手头数据判定，而非只看
                    // fromMe：本端刚发出的那笔有 clientMsgId + 原始本地路径，可以重发；
                    // 而历史里自己发过的文件这两样都没有（服务端历史只回 fileid/md5），
                    // 只能从服务器重新下载。失败态用红，其余（已取消 / 已拒绝）用主题橙。
                    label    = CanResendFile() ? _T("\x21BB 重新发送") : _T("\x21BB 重新下载");
                    lblColor = (st == kFileFailed) ? RGB(0xE5, 0x39, 0x35) : RGB(0xFF, 0x7A, 0x45);
                    break;
                }
                const int linkW = MeasureTextWidth(hdc, (LPCWSTR)label, kLinkPx);
                RECT linkRc = { linkX, rowTop, linkX + linkW, rowBottom };
                // 悬停的链接：颜色加深（×4/5）+ 加下划线，作 hover 反馈。
                const bool hover = (bi == m_fileHoverIdx);
                const COLORREF drawColor = hover
                    ? RGB(GetRValue(lblColor) * 4 / 5, GetGValue(lblColor) * 4 / 5,
                          GetBValue(lblColor) * 4 / 5)
                    : lblColor;
                //文字垂直居中于按钮行；命中矩形取满行高，整列可点。
                DrawGdiText(hdc, (LPCWSTR)label, linkRc, kLinkPx, false, drawColor,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP,
                            false /*italic*/, hover /*underline*/, false /*strike*/);
                m_fileBtnRect[bi]   = linkRc;
                m_fileBtnAction[bi] = act;
                linkX += linkW + kLinkGap;
            }
        }
    }

    // ---- "已读"回执（自己消息）----
    if (isMe)
    {
        const int rcY = bubbleY + bubbleH + 2;
        const int txtW = MeasureTextWidth(hdc, L"已读", 10);
        const int chkSize = 11;
        const int receiptW = chkSize + 3 + txtW;
        const int receiptX = bubbleX + bubbleW - receiptW;
        DrawChatIcon(g, kChatIconCheck, (Gdiplus::REAL)receiptX,
                     (Gdiplus::REAL)(rcY + 2), (Gdiplus::REAL)chkSize,
                     Gdiplus::Color(255, GetRValue(kReceiptText),
                         GetGValue(kReceiptText), GetBValue(kReceiptText)), 2.0f);
        RECT rcRc = { receiptX + chkSize + 3, rcY,
                      bubbleX + bubbleW, rcY + kReceiptHeight };
        DrawGdiText(hdc, L"已读", rcRc, 10, false, kReceiptText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // ---- Pin 标记（气泡外侧上角的一枚小图钉）----
    // 画在气泡<u>外侧</u>而不是气泡里：不占内容宽度、不改行高，故 SetPinned 只需重绘
    // 不必重排。左右按发送方向取反 —— 自己的气泡靠右、图钉放它左边，对方的气泡靠左、
    // 图钉放它右边，两种情形下图钉都落在中间那片空白里，不会压到头像。
    if (m_msg.pinned && IsRealMessage())
    {
        const Gdiplus::REAL pinX = (Gdiplus::REAL)(isMe
            ? (bubbleX - kPinIconGap - kPinIconSize)
            : (bubbleX + bubbleW + kPinIconGap));
        DrawChatIcon(g, kChatIconPin, pinX, (Gdiplus::REAL)contentTop,
                     (Gdiplus::REAL)kPinIconSize, kPinIconColor, 1.4f);
    }

    // ---- 多选勾选圈（行首勾选列内，垂直居中）----
    if (multi && IsRealMessage())
    {
        const Gdiplus::REAL chkX = (Gdiplus::REAL)(x + (kCheckColW - kCheckSize) / 2);
        const Gdiplus::REAL chkY = (Gdiplus::REAL)(y + (h - kCheckSize) / 2);
        if (m_selected)
        {
            Gdiplus::SolidBrush fill(kAccent);
            g.FillEllipse(&fill, chkX, chkY,
                          (Gdiplus::REAL)kCheckSize, (Gdiplus::REAL)kCheckSize);
            DrawChatIcon(g, kChatIconCheck, chkX + 3.0f, chkY + 3.0f,
                         (Gdiplus::REAL)(kCheckSize - 6),
                         Gdiplus::Color(255, 255, 255, 255), 2.0f);
        }
        else
        {
            Gdiplus::Pen ring(kCheckRing, 1.6f);
            g.DrawEllipse(&ring, chkX + 0.8f, chkY + 0.8f,
                          (Gdiplus::REAL)kCheckSize - 1.6f,
                          (Gdiplus::REAL)kCheckSize - 1.6f);
        }
    }
}

} // namespace balloonchatwnd
