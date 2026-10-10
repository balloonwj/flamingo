/**
 *  「代码片段」演示窗口，见 CodeSnippetDemoWindow.h。
 *
 *  本文件只给调试入口 --codesnippet 使用，界面文字与示例代码不做多语言处理，已列入
 *  Tests/I18nGuardTests.cpp 的 kExemptPaths。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "CodeSnippetDemoWindow.h"

#include <string.h>
#include <memory>
#include <string>
#include <vector>
#include <typeinfo>   // --compose-selftest：日志里写命中控件的类型名
#include <gdiplus.h>    // 现状气泡、头像的圆角（ChatPaintUtil.h 要求先引入）

#include "../../App/CustomMsgDef.h"                       // WM_CLOSE_MAINDLG
#include "../../Common/Log/IULog.h"
#include "../../Common/Code/CodeLexer.h"
#include "../BalloonChatWnd/ChatCodeBlock.h"              // 代码卡片的排版与绘制
#include "../BalloonChatWnd/ChatInlineText.h"             // 现状对照：文字气泡的排版与绘制
#include "../BalloonChatWnd/ChatFaceTag.h"                // kFaceDisplayPx
#include "../BalloonChatWnd/ChatPaintUtil.h"              // AddRoundRect / DrawGdiText
#include "../BalloonChat/BalloonChatMsgFormat.h"          // ParseSendBody
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "CodeComposeLayout.h"   // --compose-selftest：发送代码窗口的控件编号
#include "CodeNotesPane.h"       // --compose-selftest：批注栏与编辑卡片的控件编号
#include "CodeViewerWindow.h"    // --compose-selftest：打开代码查看窗并检查它的批注栏

using namespace balloonwjui;

namespace ballooncode {

namespace {

using balloonchatwnd::chatpaint::AddRoundRect;
using balloonchatwnd::chatpaint::DrawGdiText;

// ---- 控件 id（避开标题栏按钮固定使用的 1~3）----
const UINT kIdLanguageCaption = 101;   // 「语言」小标题
const UINT kIdLanguage        = 102;   // 语言下拉框
const UINT kIdLineNumbers     = 103;   // 「行号」勾选框
const UINT kIdWrap            = 104;   // 「自动换行」勾选框
const UINT kIdStatus          = 105;   // 状态行
const UINT kIdEditorCaption   = 106;   // 粘贴区上方的说明
const UINT kIdEditor          = 107;   // 粘贴区
const UINT kIdPaste           = 108;   // 「从剪贴板粘贴」
const UINT kIdSample          = 109;   // 「载入示例」
const UINT kIdClear           = 110;   // 「清空」
const UINT kIdScroll          = 111;   // 右侧滚动视图
const UINT kIdPreview         = 112;   // 右侧预览

// 预览区发给窗口的通知码。按开发约定文档 UI 约定第 4 条，另开取值段并且连 ctrlId 一起判断
const UINT kPreviewNotifyRelayout = DUIN_CUSTOM + 120;   // 预览区的内容高度可能变了，请窗口重排滚动视图
const UINT kPreviewNotifyCopied   = DUIN_CUSTOM + 121;   // 点了「复制」，请窗口在一段时间后恢复「复制」字样

// ---- 定时器（编号取得较大，避开 balloonui 宿主窗口自己的定时器）----
const UINT_PTR kTimerRefresh   = 0xC0D1;   // 粘贴区文字变化后延时刷新预览
const UINT_PTR kTimerCopied    = 0xC0D2;   // 「已复制」恢复为「复制」
const UINT_PTR kTimerSelfTest  = 0xC0D3;   // --compose-selftest：发送代码窗口显示出来之后再开始自测

// --compose-selftest 的参数
const UINT kSelfTestDelayMs   = 800;   // 打开发送代码窗口之后等多久再开始（毫秒）
const int  kSelfTestLineFirst = 12;    // 第一条批注加在第几行（第一段示例里没有批注的行，从 1 起）
const int  kSelfTestLineSecond = 16;   // 第二条批注加在第几行
const int  kSelfTestSteps     = 3;     // 一共几步：加两条，再回到第一条的行
const int  kSelfTestDragPx    = 60;    // 拖动分隔条的距离（像素，向左拖，批注栏变宽）
const char* const kSelfTestViewerKey = "compose-selftest";   // 自测打开的代码查看窗用的消息标识
const long long kSelfTestSelfId    = 100;      // 第八部分（批注模式）：本人 userid
const long long kSelfTestOtherId   = 200;      // 第八部分：原消息的发送者 userid
const long long kSelfTestBaseMsgId = 900001;   // 第八部分：原消息的 msgid
const char* const kSelfTestAnnotateKey = "compose-selftest-annotate";   // 第八部分打开的可追加批注的查看窗的消息标识

//在控件中心按下再松开鼠标左键：坐标是宿主客户区坐标，直接 SendMessage 给宿主窗口
void SelfTestClick(HWND hwnd, balloonwjui::DuiControl* ctrl)
{
    if (ctrl == NULL)
        return;
    const RECT& rc = ctrl->GetRect();
    const LPARAM pt = MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    ::SendMessage(hwnd, WM_MOUSEMOVE, 0, pt);
    ::SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    ::SendMessage(hwnd, WM_LBUTTONUP, 0, pt);
}

//处理完当前线程消息队列里的消息（投递给自己的延后消息、定时器等），模拟两次操作之间的空闲
void SelfTestPump()
{
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
}

//从 from（宿主客户区坐标）按下左键，横向拖动 dx 像素后松开
void SelfTestDrag(HWND hwnd, POINT from, int dx)
{
    const LPARAM start = MAKELPARAM(from.x, from.y);
    const LPARAM end = MAKELPARAM(from.x + dx, from.y);
    ::SendMessage(hwnd, WM_MOUSEMOVE, 0, start);
    ::SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, start);
    ::SendMessage(hwnd, WM_MOUSEMOVE, MK_LBUTTON, end);
    ::SendMessage(hwnd, WM_LBUTTONUP, 0, end);
}

//窗口的整窗宽度（像素）
int SelfTestWindowWidth(HWND hwnd)
{
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    return rc.right - rc.left;
}

//第 index 张批注卡片上部位 part 的控件
balloonwjui::DuiControl* SelfTestCardPart(balloonwjui::DuiControl* root, int index, int part)
{
    return root->FindCtrlById(kCodeNotesIdCardBase + (UINT)index * kCodeNotesIdStride + (UINT)part);
}

/**
 *  自测的检查项：每一项写一行日志（通过写 ok，没通过写 FAILED），最后汇总。
 */
class SelfTestChecks
{
public:
    SelfTestChecks()
        : m_total(0)
        , m_failed(0)
    {
    }

    /**
     *  记一项检查。
     *  @param ok   是否通过
     *  @param what 检查的内容（英文，写进日志）
     */
    void Check(bool ok, const char* what)
    {
        ++m_total;
        if (!ok)
            ++m_failed;
        LOG_INFO("compose selftest check %s: %s.", ok ? "ok" : "FAILED", what);
    }

    //检查项总数
    int Total() const
    {
        return m_total;
    }

    //没通过的项数
    int Failed() const
    {
        return m_failed;
    }

private:
    int m_total;    //检查项总数
    int m_failed;   //没通过的项数
};
const UINT     kRefreshDelayMs = 200;      // 文字变化后多久刷新预览（毫秒）：连续输入时只刷新一次
const UINT     kCopiedShowMs   = 1500;     // 「已复制」显示多久（毫秒）

// ---- 窗口尺寸（整窗尺寸，含标题栏，见开发约定文档 UI 约定第 7 条）----
const int kWindowWidth   = 1180;
const int kWindowHeight  = 780;
const int kMinWidth      = 900;
const int kMinHeight     = 560;
const int kTitleBarH     = 32;
const COLORREF kCaptionColor = RGB(0x55, 0x55, 0x55);   // 标题文字与标题栏按钮

// ---- 客户区版式（逻辑像素）----
const int kPadH          = 16;    // 客户区左右内边距
const int kPadTop        = 4;     // 客户区上内边距（标题栏下方）
const int kPadBottom     = 16;    // 客户区下内边距
const int kRootGap       = 10;    // 各行之间的间距
const int kToolRowH      = 30;    // 第一行的高
const int kToolGap       = 8;     // 第一行里各控件之间的间距
const int kCaptionW      = 36;    // 「语言」小标题宽
const int kLanguageW     = 150;   // 语言下拉框宽
const int kLineNumbersW  = 64;    // 「行号」勾选框宽
const int kWrapW         = 92;    // 「自动换行」勾选框宽
const int kColumnsGap    = 14;    // 左右两栏之间的间距
const int kLeftWeight    = 5;     // 左栏（粘贴区）的宽度权重
const int kRightWeight   = 6;     // 右栏（预览）的宽度权重
const int kEditorCaptionH = 20;   // 粘贴区说明行高
const int kButtonRowH    = 32;    // 粘贴区下方按钮行高
const int kButtonGap     = 8;     // 按钮之间的间距
const int kPasteW        = 112;   // 「从剪贴板粘贴」宽
const int kSampleW       = 92;    // 「载入示例」宽
const int kClearW        = 64;    // 「清空」宽
const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorGray  = RGB(0x8A, 0x8A, 0x8A);

// ---- 粘贴区 ----
const int      kEditorFontPx     = 13;           // 粘贴区字号（像素），与代码卡片相同
const wchar_t* const kEditorFontName = L"Consolas";

// ---- 预览区版式：按聊天窗的几何（ChatBubble.cpp 的同名常量）----
const COLORREF kPreviewBg       = RGB(0xFA, 0xF9, 0xF6);              // 消息区底色
const Gdiplus::Color kSelfBubble(255, 0xFF, 0xE6, 0xD8);              // 自己气泡底（浅橙）
const Gdiplus::Color kAvatarColor(255, 0xFF, 0x7A, 0x45);             // 头像底色（主题橙）
const COLORREF kBubbleText      = RGB(0x2A, 0x2A, 0x2A);              // 气泡文字
const COLORREF kSectionColor    = RGB(0x8A, 0x8A, 0x8A);              // 分段说明文字
const int    kEdgePad           = 16;     // 头像距消息区边
const int    kAvatarSize        = 34;     // 头像边长
const int    kAvatarGap         = 10;     // 头像与气泡之间的间距
const Gdiplus::REAL kAvatarRadius = 7.0f; // 头像圆角
const Gdiplus::REAL kBubbleRadius = 12.0f;// 气泡圆角
const int    kBubbleTextPx      = 14;     // 气泡正文字号
const int    kTextPadH          = 12;     // 文字气泡水平内边距
const int    kTextPadV          = 9;      // 文字气泡垂直内边距
const int    kMinTextWidth      = 40;     // 文字区最小宽度
const double kBubbleMaxWidthRatio = 0.62; // 文字气泡最大宽 = 消息区宽 × 此比例
const int    kAvatarTextPx      = 13;     // 头像里「我」的字号
const int    kSectionTextPx     = 12;     // 分段说明的字号
const int    kSectionH          = 22;     // 分段说明行高
const int    kSectionGap        = 6;      // 分段说明与下方内容的间距
const int    kBlockGap          = 26;     // 两段之间的间距
const int    kPreviewPadTop     = 14;     // 预览区上内边距
const int    kPreviewPadBottom  = 20;     // 预览区下内边距
const int    kPlainPreviewMaxLines = 80;  // 现状对照最多画几行：几千行的代码当文字画一遍没有意义，只会拖慢刷新
const int    kMinPreviewWidth   = 200;    // 预览区按这个宽度以下排版没有意义，按它算

/**
 *  示例代码，「载入示例」按顺序轮流填入。含 Tab 缩进、空格缩进、中文注释与长行，用来核对格式是否保持。
 */
// --compose 时预填的两条示例批注所在的行（第一段示例代码里的行号，从 1 起）
const int kDemoNote1Line = 5;    //函数声明那一行
const int kDemoNote2From = 8;    //for 循环取 *p 的那几行
const int kDemoNote2To   = 10;

const wchar_t* const kSamples[] =
{
    //C/C++：Tab 缩进、中文注释、转义字符、#include、长行
    L"#include <stdio.h>\n"
    L"#include \"ChatSession.h\"\n"
    L"\n"
    L"// 统计一段文字里每个小写字母出现的次数\n"
    L"static int CountLetters(const char* text, int counts[26])\n"
    L"{\n"
    L"\tint total = 0;\n"
    L"\tfor (const char* p = text; *p != '\\0'; ++p)\n"
    L"\t{\n"
    L"\t\tif (*p >= 'a' && *p <= 'z')\n"
    L"\t\t{\n"
    L"\t\t\t++counts[*p - 'a'];   /* 小写字母 */\n"
    L"\t\t\t++total;\n"
    L"\t\t}\n"
    L"\t}\n"
    L"\tprintf(\"total = %d, ratio = %.2f\\n\", total, total / 100.0);\n"
    L"\treturn total;\n"
    L"}\n"
    L"\n"
    L"class CChatSession : public CSessionBase\n"
    L"{\n"
    L"public:\n"
    L"    explicit CChatSession(int64_t userId) : m_userId(userId), m_name(L\"张三\") {}\n"
    L"    std::vector<std::wstring> RecentMessages(size_t limit = 50) const;   // 最近的消息，按时间倒序\n"
    L"\n"
    L"private:\n"
    L"    int64_t      m_userId;    // 用户 id\n"
    L"    std::wstring m_name;      // 显示名\n"
    L"    static constexpr const char* kLongMessage = \"这是一行特别长的字符串，用来看超过卡片宽度时怎么显示：The quick brown fox jumps over the lazy dog.\";\n"
    L"};\n",

    //Python：空格缩进、装饰器、三引号、f-string
    L"#!/usr/bin/env python3\n"
    L"# -*- coding: utf-8 -*-\n"
    L"\"\"\"下载文件并校验 MD5\"\"\"\n"
    L"import hashlib\n"
    L"from pathlib import Path\n"
    L"\n"
    L"\n"
    L"class Downloader:\n"
    L"    def __init__(self, base_url: str, retries: int = 3):\n"
    L"        self.base_url = base_url\n"
    L"        self.retries = retries\n"
    L"\n"
    L"    @staticmethod\n"
    L"    def md5_of(path: Path) -> str:\n"
    L"        h = hashlib.md5()\n"
    L"        with open(path, \"rb\") as f:\n"
    L"            for chunk in iter(lambda: f.read(8192), b\"\"):\n"
    L"                h.update(chunk)\n"
    L"        return h.hexdigest()\n"
    L"\n"
    L"\n"
    L"if __name__ == \"__main__\":\n"
    L"    d = Downloader(\"https://example.com/files\")\n"
    L"    print(f\"retries = {d.retries}, md5 = {Downloader.md5_of(Path('a.bin'))}\")\n",

    //SQL：大写关键字、Tab 缩进、行注释
    L"-- 查询最近 7 天发言最多的 10 个用户\n"
    L"SELECT u.f_user_id, u.f_nickname, COUNT(*) AS msg_count\n"
    L"FROM t_chatmsg m\n"
    L"\tINNER JOIN t_user u ON u.f_user_id = m.f_sender_id\n"
    L"WHERE m.f_create_time >= DATE_SUB(NOW(), INTERVAL 7 DAY)\n"
    L"\tAND m.f_recalled = 0\n"
    L"\tAND u.f_nickname <> 'it''s a test'\n"
    L"GROUP BY u.f_user_id, u.f_nickname\n"
    L"ORDER BY msg_count DESC\n"
    L"LIMIT 10;\n",

    //JSON：键名与字符串值分开着色
    L"{\n"
    L"    \"clientmsgid\": \"f47ac10b-58cc-4372-a567-0e02b2c3d479\",\n"
    L"    \"ctype\": 1,\n"
    L"    \"cid\": \"100001\",\n"
    L"    \"msgtype\": 1,\n"
    L"    \"content\": { \"text\": \"int main()\\n{\\n\\treturn 0;\\n}\", \"code\": { \"lang\": \"cpp\" } },\n"
    L"    \"mentions\": [],\n"
    L"    \"mentionall\": false,\n"
    L"    \"ratio\": 1.5e-3\n"
    L"}\n",

    //Shell：$ 变量、Tab 缩进、#! 行
    L"#!/bin/bash\n"
    L"# 构建并启动三个服务\n"
    L"set -e\n"
    L"cd \"$(dirname \"$0\")/flamingoserver\"\n"
    L"cmake -DCMAKE_BUILD_TYPE=Release . && make -j\"$(nproc)\"\n"
    L"for svc in chatserver fileserver imgserver; do\n"
    L"\tif pgrep -x \"$svc\" > /dev/null; then\n"
    L"\t\techo \"${svc} is already running\"   # 已经在运行\n"
    L"\telse\n"
    L"\t\t./\"$svc\" -d && echo \"started $svc\"\n"
    L"\tfi\n"
    L"done\n",

    //TypeScript：模板字符串、箭头函数、泛型
    L"// 节流：一段时间内只执行一次\n"
    L"export function throttle<T extends (...args: any[]) => void>(fn: T, wait = 200): T {\n"
    L"    let last = 0;\n"
    L"    let timer: number | undefined;\n"
    L"    return function (this: unknown, ...args: any[]) {\n"
    L"        const now = Date.now();\n"
    L"        if (now - last >= wait) {\n"
    L"            last = now;\n"
    L"            fn.apply(this, args);\n"
    L"        } else if (timer === undefined) {\n"
    L"            timer = window.setTimeout(() => {\n"
    L"                timer = undefined;\n"
    L"                last = Date.now();\n"
    L"                fn.apply(this, args);\n"
    L"            }, wait - (now - last));\n"
    L"        }\n"
    L"    } as T;\n"
    L"}\n"
    L"console.log(`throttle ready: ${typeof throttle}`);\n",

    //XML：处理指令、注释、标签与属性
    L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    L"<!-- 聊天窗口的布局片段 -->\n"
    L"<window title=\"代码片段\" width=\"640\" height=\"480\">\n"
    L"    <vbox padding=\"12\" gap=\"8\">\n"
    L"        <label id=\"401\" text=\"语言\" />\n"
    L"        <combobox id=\"402\" editable=\"false\" />\n"
    L"        <richedit id=\"403\" multi-line=\"true\" word-wrap=\"false\" />\n"
    L"    </vbox>\n"
    L"</window>\n",
};

const int kSampleCount = static_cast<int>(sizeof(kSamples) / sizeof(kSamples[0]));

//取前 maxLines 行；被截掉时 truncated 置 true
std::wstring FirstLines(const std::wstring& text, int maxLines, bool& truncated)
{
    truncated = false;
    int lines = 1;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != L'\n')
            continue;
        if (lines >= maxLines)
        {
            truncated = true;
            return text.substr(0, i);
        }
        ++lines;
    }
    return text;
}

} // anonymous namespace

/**
 *  右侧预览：按聊天窗的样式画两段——上面是「作为代码片段发送」（新方案），下面是「作为普通文字消息发送」
 *  （现状，作对照），都是自己发出的消息，靠右、带头像。代码卡片上的「复制」写剪贴板，「展开全部 / 收起」就地切换。
 */
class CodeSnippetPreview : public DuiControl
{
public:
    CodeSnippetPreview()
        : m_lang(codesnippet::code_lang_plain)
        , m_layoutWidth(-1)
        , m_height(0)
        , m_plainTruncated(false)
        , m_hover(balloonchatwnd::kCodePartNone)
    {
        m_textSize.cx = 0;
        m_textSize.cy = 0;
        ::SetRectEmpty(&m_bubbleRect);
        ::SetRectEmpty(&m_cardRect);
        ::SetRectEmpty(&m_avatar1);
        ::SetRectEmpty(&m_avatar2);
        m_section1Top = 0;
        m_section2Top = 0;
    }

    /**
     *  换一段代码。
     *  @param raw   粘贴区里的原文
     *  @param lang  语言，取值见 codesnippet::CodeLanguage
     *  @param notes 批注（发送代码窗口交回的），卡片据此标出被批注的行
     */
    void SetSource(const std::wstring& raw, int lang, const std::vector<codesnippet::CodeNote>& notes)
    {
        //代码或语言变了才收回折叠状态；只是开关行号、自动换行时保持用户展开的状态
        if (raw != m_raw || lang != m_lang)
            m_options.m_expanded = false;
        m_raw = raw;
        m_lang = lang;
        m_content = balloonchatwnd::PrepareCodeBlockContent(raw, lang, notes);
        const std::wstring plain = FirstLines(m_content.m_code, kPlainPreviewMaxLines, m_plainTruncated);
        m_textSegs = balloonchat::ParseSendBody(CString(plain.c_str()));
        m_layoutWidth = -1;
    }

    /**
     *  改显示选项。
     *  @param lineNumbers 是否显示行号
     *  @param wrap        长行是否折行
     */
    void SetOptions(bool lineNumbers, bool wrap)
    {
        m_options.m_lineNumbers = lineNumbers;
        m_options.m_wrap = wrap;
        m_layoutWidth = -1;
    }

    //把「已复制」恢复为「复制」，重排并重画
    void ClearCopied()
    {
        if (!m_options.m_copied)
            return;
        m_options.m_copied = false;
        m_layoutWidth = -1;
        NotifyParent(kPreviewNotifyRelayout);
    }

    /**
     *  按给定宽度排版，返回内容总高度（像素）。滚动视图排版时调用。
     */
    int MeasureHeight(int width)
    {
        EnsureLayout(width);
        return m_height;
    }

    //DuiControl 覆写：画底色、两段说明、现状气泡与代码卡片
    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        (void)rcDirty;
        const RECT& rc = GetRect();
        EnsureLayout(rc.right - rc.left);

        HBRUSH bg = ::CreateSolidBrush(kPreviewBg);
        ::FillRect(hdc, &rc, bg);
        ::DeleteObject(bg);

        const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
        RECT section = { rc.left + kEdgePad, rc.top + m_section1Top, rc.right - kEdgePad,
                         rc.top + m_section1Top + kSectionH };
        std::wstring text = L"① 新方案：作为代码片段发出去的样子（";
        text += (m_lang == codesnippet::code_lang_plain) ? L"纯文本" : codesnippet::CodeLanguageDisplayName(m_lang);
        text += L"）";
        DrawGdiText(hdc, text.c_str(), section, kSectionTextPx, false, kSectionColor, kOneLine);
        ::SetRect(&section, rc.left + kEdgePad, rc.top + m_section2Top, rc.right - kEdgePad,
                  rc.top + m_section2Top + kSectionH);
        text = L"② 对照：现在作为普通文字消息发出去的样子（用的是聊天窗气泡的真实排版代码）";
        if (m_plainTruncated)
            text += L"，只画前 80 行";
        DrawGdiText(hdc, text.c_str(), section, kSectionTextPx, false, kSectionColor, kOneLine);

        PaintAvatar(hdc, m_avatar1);
        PaintAvatar(hdc, m_avatar2);

        if (!m_content.m_code.empty())
        {
            //现状气泡：与 ChatBubble 相同的圆角底色与正文排版
            {
                Gdiplus::Graphics g(hdc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::GraphicsPath path;
                AddRoundRect(path, (Gdiplus::REAL)(rc.left + m_bubbleRect.left), (Gdiplus::REAL)(rc.top + m_bubbleRect.top),
                             (Gdiplus::REAL)(m_bubbleRect.right - m_bubbleRect.left),
                             (Gdiplus::REAL)(m_bubbleRect.bottom - m_bubbleRect.top), kBubbleRadius);
                Gdiplus::SolidBrush brush(kSelfBubble);
                g.FillPath(&brush, &path);
            }
            RECT txRc = { rc.left + m_bubbleRect.left + kTextPadH, rc.top + m_bubbleRect.top + kTextPadV,
                          rc.left + m_bubbleRect.right - kTextPadH, rc.top + m_bubbleRect.bottom - kTextPadV };
            balloonchatwnd::DrawInlinePlaced(hdc, m_textPlaced, txRc, TextStyle());

            balloonchatwnd::PaintCodeBlock(hdc, m_card, rc.left + m_cardRect.left, rc.top + m_cardRect.top, m_hover);
        }
        else
        {
            RECT hint = { rc.left + kEdgePad, rc.top + m_cardRect.top, rc.right - kEdgePad,
                          rc.top + m_cardRect.top + kSectionH };
            DrawGdiText(hdc, L"粘贴区还是空的：在左边粘贴代码，或点「载入示例」。", hint, kSectionTextPx, false,
                        kColorGray, kOneLine);
        }
    }

    //DuiControl 覆写：悬停在「复制」或底栏上时加下划线
    bool OnMouseMove(POINT pt, UINT mkFlags) override
    {
        (void)mkFlags;
        const int part = HitPart(pt);
        if (part != m_hover)
        {
            m_hover = part;
            Invalidate();
        }
        return true;
    }

    //DuiControl 覆写：光标离开时撤掉悬停效果
    bool OnMouseLeave() override
    {
        if (m_hover != balloonchatwnd::kCodePartNone)
        {
            m_hover = balloonchatwnd::kCodePartNone;
            Invalidate();
        }
        return true;
    }

    //DuiControl 覆写：在可点击部位上显示手形
    bool OnSetCursor(POINT pt) override
    {
        if (HitPart(pt) == balloonchatwnd::kCodePartNone)
            return false;
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }

    //DuiControl 覆写：点「复制」写剪贴板，点底栏展开或收起
    bool OnLButtonDown(POINT pt, UINT mkFlags) override
    {
        (void)mkFlags;
        const int part = HitPart(pt);
        if (part == balloonchatwnd::kCodePartCopy)
        {
            CopyCode();
            return true;
        }
        if (part == balloonchatwnd::kCodePartFooter)
        {
            m_options.m_expanded = !m_options.m_expanded;
            m_layoutWidth = -1;
            LOG_INFO("code snippet demo: card %s, lines=%d.", m_options.m_expanded ? "expanded" : "collapsed",
                     m_content.m_lineCount);
            NotifyParent(kPreviewNotifyRelayout);
            return true;
        }
        return false;
    }

private:
    //现状气泡正文的样式：与 ChatBubble 的默认外观相同
    static balloonchatwnd::InlineTextStyle TextStyle()
    {
        balloonchatwnd::InlineTextStyle st;
        st.sizePx = kBubbleTextPx;
        st.color = kBubbleText;
        return st;
    }

    //按宽度排版；宽度没变且内容没变时直接返回
    void EnsureLayout(int width)
    {
        if (width < kMinPreviewWidth)
            width = kMinPreviewWidth;
        if (width == m_layoutWidth)
            return;
        m_layoutWidth = width;

        HDC hdc = ::GetDC(NULL);
        const int maxBubbleW = static_cast<int>(width * kBubbleMaxWidthRatio);
        int maxTextW = maxBubbleW - kTextPadH * 2;
        if (maxTextW < kMinTextWidth)
            maxTextW = kMinTextWidth;
        m_textPlaced.clear();
        m_textSize = balloonchatwnd::LayoutInlineContentForStyle(hdc, m_textSegs, maxTextW,
                                                                 balloonchatwnd::kFaceDisplayPx, TextStyle(),
                                                                 &m_textPlaced);
        //代码卡片可以用满两侧头像之间的整列宽度，比文字气泡宽
        const int cardMaxW = width - 2 * (kEdgePad + kAvatarSize + kAvatarGap);
        balloonchatwnd::LayoutCodeBlock(hdc, m_content, m_options, cardMaxW, m_card);
        ::ReleaseDC(NULL, hdc);

        const int avatarLeft = width - kEdgePad - kAvatarSize;
        const int contentRight = avatarLeft - kAvatarGap;
        int y = kPreviewPadTop;

        //第一段：新方案的代码卡片，放在最上面，打开窗口不用滚动就能看到
        m_section1Top = y;
        y += kSectionH + kSectionGap;
        ::SetRect(&m_cardRect, contentRight - m_card.m_size.cx, y, contentRight, y + m_card.m_size.cy);
        ::SetRect(&m_avatar1, avatarLeft, y, avatarLeft + kAvatarSize, y + kAvatarSize);
        y += ((m_card.m_size.cy > kAvatarSize) ? m_card.m_size.cy : kAvatarSize) + kBlockGap;

        //第二段：现状的文字气泡，作对照
        m_section2Top = y;
        y += kSectionH + kSectionGap;
        const int bubbleW = m_textSize.cx + kTextPadH * 2;
        const int bubbleH = m_textSize.cy + kTextPadV * 2;
        ::SetRect(&m_bubbleRect, contentRight - bubbleW, y, contentRight, y + bubbleH);
        ::SetRect(&m_avatar2, avatarLeft, y, avatarLeft + kAvatarSize, y + kAvatarSize);
        y += ((bubbleH > kAvatarSize) ? bubbleH : kAvatarSize) + kPreviewPadBottom;
        m_height = y;
    }

    //画一个「我」的头像（圆角方块）
    void PaintAvatar(HDC hdc, const RECT& relative)
    {
        const RECT& rc = GetRect();
        RECT r = { rc.left + relative.left, rc.top + relative.top, rc.left + relative.right, rc.top + relative.bottom };
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath path;
            AddRoundRect(path, (Gdiplus::REAL)r.left, (Gdiplus::REAL)r.top, (Gdiplus::REAL)kAvatarSize,
                         (Gdiplus::REAL)kAvatarSize, kAvatarRadius);
            Gdiplus::SolidBrush brush(kAvatarColor);
            g.FillPath(&brush, &path);
        }
        DrawGdiText(hdc, L"我", r, kAvatarTextPx, true, kColorWhite,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    //光标（宿主客户区坐标）在代码卡片的哪个可点击部位上
    int HitPart(POINT pt) const
    {
        if (m_content.m_code.empty())
            return balloonchatwnd::kCodePartNone;
        const RECT& rc = GetRect();
        POINT local = { pt.x - (rc.left + m_cardRect.left), pt.y - (rc.top + m_cardRect.top) };
        const int part = balloonchatwnd::HitTestCodeBlock(m_card, local);
        //演示窗口里卡片正文不可点击（聊天窗里点正文打开代码查看窗）
        return (part == balloonchatwnd::kCodePartBody) ? balloonchatwnd::kCodePartNone : part;
    }

    //把代码原文写进剪贴板，顶部改为「已复制」
    void CopyCode()
    {
        const std::wstring text = balloonchatwnd::CodeTextForClipboard(m_content.m_code);
        HWND owner = (GetHost() != NULL) ? GetHost()->m_hWnd : NULL;
        bool copied = false;
        if (::OpenClipboard(owner))
        {
            ::EmptyClipboard();
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (memory != NULL)
            {
                void* p = ::GlobalLock(memory);
                if (p != NULL)
                {
                    memcpy(p, text.c_str(), bytes);
                    ::GlobalUnlock(memory);
                    //交给剪贴板之后所有权即归系统，不能再 GlobalFree
                    copied = (::SetClipboardData(CF_UNICODETEXT, memory) != NULL);
                }
                if (!copied)
                    ::GlobalFree(memory);
            }
            ::CloseClipboard();
        }
        LOG_INFO("code snippet demo: copied %u chars, ok=%d.", static_cast<unsigned>(text.size()), copied ? 1 : 0);
        if (!copied)
            return;
        m_options.m_copied = true;
        m_layoutWidth = -1;
        NotifyParent(kPreviewNotifyRelayout);
        NotifyParent(kPreviewNotifyCopied);
    }

private:
    std::wstring                               m_raw;             //粘贴区原文，用来判断代码是否变了
    int                                        m_lang;            //当前语言，取值见 codesnippet::CodeLanguage
    balloonchatwnd::CodeBlockContent           m_content;         //代码卡片的内容
    balloonchatwnd::CodeBlockOptions           m_options;         //代码卡片的显示选项（行号、折行、展开、已复制）
    balloonchatwnd::CodeBlockLayout            m_card;            //代码卡片的排版结果
    std::vector<balloonchat::ChatSegment>      m_textSegs;        //现状气泡的正文段
    std::vector<balloonchatwnd::InlinePlaced>  m_textPlaced;      //现状气泡正文的排版结果
    SIZE                                       m_textSize;        //现状气泡正文的尺寸
    int                                        m_layoutWidth;     //上次排版用的宽度；-1 表示内容或选项变了、需要重排
    int                                        m_height;          //内容总高度（像素）
    bool                                       m_plainTruncated;  //现状气泡是否只画了前 kPlainPreviewMaxLines 行
    int                                        m_hover;           //鼠标悬停的卡片部位，取值见 balloonchatwnd::CodeBlockPart
    int                                        m_section1Top;     //第一段（新方案的代码卡片）说明的顶边（相对本控件）
    int                                        m_section2Top;     //第二段（现状的文字气泡）说明的顶边（相对本控件）
    RECT                                       m_bubbleRect;      //现状气泡（相对本控件）
    RECT                                       m_cardRect;        //代码卡片（相对本控件）
    RECT                                       m_avatar1;         //第一段（代码卡片）的头像（相对本控件）
    RECT                                       m_avatar2;         //第二段（文字气泡）的头像（相对本控件）
};

namespace {

/**
 *  右侧的滚动视图：排版时先按视口宽度量出预览内容的高度，再交给 DuiScrollView 排版。
 *  预览内容的高度取决于宽度（折行、卡片宽度），DuiScrollView 的自动测高只问 GetDesiredSize，拿不到宽度，故在此补上。
 */
class PreviewScrollView : public DuiScrollView
{
public:
    //preview：预览控件，借用；调用方随后用 SetContent 把它交给本视图持有
    explicit PreviewScrollView(CodeSnippetPreview* preview)
        : m_preview(preview)
    {
    }

    //DuiControl 覆写：先量内容高度再排版
    void Layout(const RECT& rcAvail) override
    {
        const int width = rcAvail.right - rcAvail.left;
        if (m_preview != NULL && width > 0)
            SetContentHeight(m_preview->MeasureHeight(width));
        DuiScrollView::Layout(rcAvail);
    }

private:
    CodeSnippetPreview* m_preview;   //预览控件（借用，是本视图的内容子控件）
};

//造一个按钮
std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return std::unique_ptr<DuiControl>(button.release());
}

//造一个勾选框
std::unique_ptr<DuiControl> MakeCheckbox(UINT id, LPCTSTR text, bool checked)
{
    std::unique_ptr<DuiButton> box(new DuiButton());
    box->SetButtonType(DuiButton::StyleCheckbox);
    box->SetCtrlId(id);
    box->SetText(text);
    box->SetCheck(checked, false);
    return std::unique_ptr<DuiControl>(box.release());
}

//造一个单行文字
std::unique_ptr<DuiControl> MakeLabel(UINT id, LPCTSTR text, COLORREF color, UINT align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

} // anonymous namespace

CCodeSnippetDemoWindow::CCodeSnippetDemoWindow()
    : m_hHost(NULL)
    , m_editorFont(NULL)
    , m_sampleIndex(0)
    , m_lastAnnotateBaseMsgId(0)
    , m_lastAnnotateNewNotes(0)
    , m_pLanguage(NULL)
    , m_pLineNumbers(NULL)
    , m_pWrap(NULL)
    , m_pStatus(NULL)
    , m_pEditor(NULL)
    , m_pScroll(NULL)
    , m_pPreview(NULL)
{
}

CCodeSnippetDemoWindow::~CCodeSnippetDemoWindow()
{
    if (m_editorFont != NULL)
    {
        ::DeleteObject(m_editorFont);
        m_editorFont = NULL;
    }
}

bool CCodeSnippetDemoWindow::CreateAndShow(HWND hHost)
{
    m_hHost = hHost;
    if (IsWindow())
    {
        ShowWindow(::IsIconic(m_hWnd) ? SW_RESTORE : SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
        return true;
    }
    if (Create(NULL, CWindow::rcDefault, L"代码片段效果演示", WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("code snippet demo: Create failed.");
        return false;
    }
    SetTitle(L"代码片段效果演示（--codesnippet）");
    SetTitleBarHeight(kTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    SetButtons(true, true, true);
    SetResizable(true);
    SetMinSize(kMinWidth, kMinHeight);
    BuildClient();
    ResizeClient(kWindowWidth, kWindowHeight);
    CenterWindow(::GetDesktopWindow());
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);

    LoadNextSample();
    LOG_INFO("code snippet demo window opened.");

    //命令行另带 --compose：再弹出发送代码窗口（预填第一段示例与两条批注），核对它的外观与交互；
    //点「发送」后代码回填到粘贴区
    if (::_tcsstr(::GetCommandLine(), _T("--compose")) != NULL)
    {
        std::vector<codesnippet::CodeNote> notes;
        notes.push_back(codesnippet::CodeNote(kDemoNote1Line, kDemoNote1Line, L"counts 至少要有 26 个元素，调用方负责清零"));
        notes.push_back(codesnippet::CodeNote(kDemoNote2From, kDemoNote2To, L"text 为 NULL 时这里会崩溃，入口处应先判空"));
        m_compose.Open(m_hWnd, std::wstring(kSamples[0]), std::string(), notes);
        //再带 --compose-selftest：窗口显示出来之后自动走一遍添加批注
        if (::_tcsstr(::GetCommandLine(), _T("--compose-selftest")) != NULL)
            SetTimer(kTimerSelfTest, kSelfTestDelayMs, NULL);
    }
    return true;
}

void CCodeSnippetDemoWindow::BuildClient()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    //第一行：语言、行号、自动换行、状态
    std::unique_ptr<DuiHBox> tools(new DuiHBox());
    tools->SetGap(kToolGap);
    tools->AddChild(MakeLabel(kIdLanguageCaption, L"语言", kColorGray, DT_LEFT), Hint().Fixed(kCaptionW));
    std::unique_ptr<DuiComboBox> language(new DuiComboBox());
    language->SetCtrlId(kIdLanguage);
    language->SetEditable(false);
    language->AddString(L"自动识别");
    for (int lang = 0; lang < codesnippet::code_lang_count; ++lang)
    {
        const wchar_t* name = codesnippet::CodeLanguageDisplayName(lang);
        language->AddString(lang == codesnippet::code_lang_plain ? L"纯文本" : name);
    }
    language->SetCurSel(0, false);
    m_pLanguage = language.get();
    tools->AddChild(std::unique_ptr<DuiControl>(language.release()), Hint().Fixed(kLanguageW));
    tools->AddChild(MakeCheckbox(kIdLineNumbers, L"行号", true), Hint().Fixed(kLineNumbersW));
    tools->AddChild(MakeCheckbox(kIdWrap, L"自动换行", true), Hint().Fixed(kWrapW));
    tools->AddChild(MakeLabel(kIdStatus, L"", kColorGray, DT_LEFT), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(tools.release()), Hint().Fixed(kToolRowH));

    //主体：左边粘贴区，右边预览
    std::unique_ptr<DuiHBox> body(new DuiHBox());
    body->SetGap(kColumnsGap);

    std::unique_ptr<DuiVBox> left(new DuiVBox());
    left->SetGap(kToolGap);
    left->AddChild(MakeLabel(kIdEditorCaption, L"把代码粘贴到下面（Ctrl+V），右侧会自动刷新；也可以直接修改",
                             kColorGray, DT_LEFT),
                   Hint().Fixed(kEditorCaptionH));
    std::unique_ptr<DuiRichEdit> editor(new DuiRichEdit());
    editor->SetCtrlId(kIdEditor);
    editor->SetPlainTextMode(true);   //只能在写入文字之前切换
    editor->SetMultiLine(true);
    editor->SetWordWrap(false);
    editor->SetHScrollPolicy(DuiRichEdit::kScrollBarAuto);
    editor->SetShowBorder(true);
    editor->SetMaxLength(0);
    editor->SetPlaceholder(L"在这里粘贴代码");
    m_editorFont = ::CreateFontW(-kEditorFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                                 kEditorFontName);
    if (m_editorFont != NULL)
        editor->SetDefaultFontFromHFONT(m_editorFont);
    m_pEditor = editor.get();
    left->AddChild(std::unique_ptr<DuiControl>(editor.release()), Hint().Weight(1));
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kButtonGap);
    buttons->AddChild(MakeButton(kIdPaste, L"从剪贴板粘贴", DuiButton::Variant::Primary), Hint().Fixed(kPasteW));
    buttons->AddChild(MakeButton(kIdSample, L"载入示例", DuiButton::Variant::Default), Hint().Fixed(kSampleW));
    buttons->AddChild(MakeButton(kIdClear, L"清空", DuiButton::Variant::Default), Hint().Fixed(kClearW));
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    left->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowH));
    body->AddChild(std::unique_ptr<DuiControl>(left.release()), Hint().Weight(kLeftWeight));

    std::unique_ptr<CodeSnippetPreview> preview(new CodeSnippetPreview());
    preview->SetCtrlId(kIdPreview);
    m_pPreview = preview.get();
    std::unique_ptr<PreviewScrollView> scroll(new PreviewScrollView(m_pPreview));
    scroll->SetCtrlId(kIdScroll);
    scroll->SetContent(std::unique_ptr<DuiControl>(preview.release()));
    m_pScroll = scroll.get();
    body->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(kRightWeight));
    root->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Weight(1));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        m_pLineNumbers = dynamic_cast<DuiButton*>(content->FindCtrlById(kIdLineNumbers));
        m_pWrap = dynamic_cast<DuiButton*>(content->FindCtrlById(kIdWrap));
        m_pStatus = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdStatus));
    }
}

int CCodeSnippetDemoWindow::SelectedLanguage() const
{
    if (m_pLanguage == NULL)
        return -1;
    //第 0 项是「自动识别」，第 i 项（i >= 1）是语言 i - 1
    const int index = m_pLanguage->GetCurSel();
    if (index <= 0 || index > codesnippet::code_lang_count)
        return -1;
    return index - 1;
}

void CCodeSnippetDemoWindow::RefreshPreview()
{
    if (m_pEditor == NULL || m_pPreview == NULL || m_pScroll == NULL)
        return;
    const std::wstring raw((LPCWSTR)m_pEditor->GetText());
    const int detected = codesnippet::DetectCodeLanguage(raw);
    const bool looksLikeCode = codesnippet::LooksLikeCode(raw);
    const int chosen = SelectedLanguage();
    const int lang = (chosen >= 0) ? chosen : detected;

    const bool lineNumbers = (m_pLineNumbers == NULL) || m_pLineNumbers->IsChecked();
    const bool wrap = (m_pWrap == NULL) || m_pWrap->IsChecked();
    m_pPreview->SetOptions(lineNumbers, wrap);
    m_pPreview->SetSource(raw, lang, m_previewNotes);

    if (m_pStatus != NULL)
    {
        std::wstring status = L"自动识别为：";
        status += (detected == codesnippet::code_lang_plain) ? L"纯文本（识别不出）" : codesnippet::CodeLanguageDisplayName(detected);
        status += looksLikeCode ? L"　·　判断为代码，粘贴进聊天输入框时可以自动按代码片段发送"
                                : L"　·　判断为普通文字，粘贴进聊天输入框时不会自动转成代码片段";
        m_pStatus->SetText(status.c_str());
    }

    m_pScroll->ForceLayout(m_pScroll->GetRect());
    m_pScroll->Invalidate();
    LOG_INFO("code snippet demo: preview refreshed, chars=%u, detected=%s, chosen=%d, looks like code=%d.",
             static_cast<unsigned>(raw.size()), codesnippet::CodeLanguageId(detected), chosen, looksLikeCode ? 1 : 0);
}

void CCodeSnippetDemoWindow::PasteFromClipboard()
{
    if (m_pEditor == NULL)
        return;
    std::wstring text;
    if (::OpenClipboard(m_hWnd))
    {
        HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
        if (data != NULL)
        {
            const wchar_t* p = static_cast<const wchar_t*>(::GlobalLock(data));
            if (p != NULL)
            {
                text = p;
                ::GlobalUnlock(data);
            }
        }
        ::CloseClipboard();
    }
    if (text.empty())
    {
        if (m_pStatus != NULL)
            m_pStatus->SetText(L"剪贴板里没有文字");
        return;
    }
    m_pEditor->SetText(text.c_str());
    LOG_INFO("code snippet demo: pasted %u chars from the clipboard.", static_cast<unsigned>(text.size()));
    RefreshPreview();
}

void CCodeSnippetDemoWindow::RunComposeSelfTest()
{
    balloonwjui::DuiControl* root = m_compose.IsWindow() ? m_compose.GetClientContent() : NULL;
    balloonwjui::DuiRichEdit* editor = (root != NULL)
        ? dynamic_cast<balloonwjui::DuiRichEdit*>(root->FindCtrlById(kCodeComposeIdEditor)) : NULL;
    CodeNotesPane* pane = (root != NULL) ? dynamic_cast<CodeNotesPane*>(root->FindCtrlById(kCodeComposeIdNotesPane)) : NULL;
    CodeNotesSplitter* split = (root != NULL)
        ? dynamic_cast<CodeNotesSplitter*>(root->FindCtrlById(kCodeComposeIdSplit)) : NULL;
    balloonwjui::DuiButton* notesButton = (root != NULL)
        ? dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kCodeComposeIdNotes)) : NULL;
    balloonwjui::DuiControl* undo = (root != NULL) ? root->FindCtrlById(kCodeComposeIdUndoNote) : NULL;
    if (editor == NULL || pane == NULL || split == NULL || notesButton == NULL || undo == NULL)
    {
        LOG_ERROR("compose selftest: controls not found, window=%d.", m_compose.IsWindow() ? 1 : 0);
        return;
    }
    const HWND hwnd = m_compose.m_hWnd;
    SelfTestChecks checks;
    LOG_INFO("compose selftest: start, notes=%u, editing=%d.", (unsigned)pane->Notes().size(), pane->IsEditing() ? 1 : 0);

    //一、添加两条批注并确定，再回到第一条所在的行点「批注」：打开那一条查看和修改
    const int lines[kSelfTestSteps] = { kSelfTestLineFirst, kSelfTestLineSecond, kSelfTestLineFirst };
    for (int step = 0; step < kSelfTestSteps; ++step)
    {
        const size_t before = pane->Notes().size();
        const long cp = (long)editor->SendMessageToEngine(EM_LINEINDEX, (WPARAM)(lines[step] - 1), 0);
        editor->SetSel(cp, cp);
        SelfTestPump();
        SelfTestClick(hwnd, notesButton);
        SelfTestPump();
        LOG_INFO("compose selftest step %d: line %d, editing=%d, index=%d, lines %d-%d, prefilled %u chars, notes=%u.",
                 step, lines[step], pane->IsEditing() ? 1 : 0, pane->EditingIndex(), pane->EditingFrom(),
                 pane->EditingTo(), (unsigned)pane->EditingText().size(), (unsigned)pane->Notes().size());
        if (step == kSelfTestSteps - 1)
        {
            checks.Check(pane->IsEditing() && pane->EditingIndex() >= 0 && !pane->EditingText().empty(),
                         "notes button on an annotated line opens that note with its text");
            break;
        }
        checks.Check(pane->IsEditing() && pane->EditingIndex() == -1, "notes button starts a new note");
        if (pane->EditBox() != NULL)
            pane->EditBox()->SetText(L"selftest note");
        SelfTestClick(hwnd, root->FindCtrlById(kCodeNotesIdEditOk));
        SelfTestPump();
        checks.Check(!pane->IsEditing() && pane->Notes().size() == before + 1 && notesButton->IsEnabled(),
                     "ok confirms the note and the notes button is enabled again");
    }
    //「取消」：结束编辑，批注不变
    const size_t beforeCancel = pane->Notes().size();
    SelfTestClick(hwnd, root->FindCtrlById(kCodeNotesIdEditCancel));
    SelfTestPump();
    checks.Check(!pane->IsEditing() && pane->Notes().size() == beforeCancel, "cancel ends editing without changes");
    const std::vector<codesnippet::CodeNote> notes = pane->Notes();
    if (notes.size() < 2)
    {
        LOG_ERROR("compose selftest: expected at least 2 notes, got %u.", (unsigned)notes.size());
        return;
    }

    //二、光标放到第一条批注的行上：对应的卡片高亮
    const long firstCp = (long)editor->SendMessageToEngine(EM_LINEINDEX, (WPARAM)(notes[0].m_from - 1), 0);
    editor->SetSel(firstCp, firstCp);
    SelfTestPump();
    checks.Check(pane->Highlight() == 0, "caret on an annotated line highlights its card");

    //三、点第二张卡片上的「第 x～y 行」：代码里选中那几行
    SelfTestClick(hwnd, SelfTestCardPart(root, 1, kCodeNotesPartRange));
    SelfTestPump();
    long selBegin = 0;
    long selEnd = 0;
    editor->GetSel(selBegin, selEnd);
    const long expectBegin = (long)editor->SendMessageToEngine(EM_LINEINDEX, (WPARAM)(notes[1].m_from - 1), 0);
    const int lastLine = (int)editor->SendMessageToEngine(EM_EXLINEFROMCHAR, 0, selEnd) + 1;
    LOG_INFO("compose selftest: range link selection %ld-%ld, expected begin %ld, last line %d, note lines %d-%d.",
             selBegin, selEnd, expectBegin, lastLine, notes[1].m_from, notes[1].m_to);
    checks.Check(selBegin == expectBegin && lastLine == notes[1].m_to, "range link selects the note lines");

    //四、删除第一条，再「撤销」
    SelfTestClick(hwnd, SelfTestCardPart(root, 0, kCodeNotesPartDelete));
    SelfTestPump();
    checks.Check(pane->Notes().size() == notes.size() - 1 && undo->IsVisible(), "delete removes the note and shows undo");
    SelfTestClick(hwnd, undo);
    SelfTestPump();
    checks.Check(pane->Notes().size() == notes.size() && !undo->IsVisible(), "undo restores the deleted note");

    //五、把分隔条向左拖：批注栏变宽，宽度被记住
    const int paneBefore = split->RightWidth();
    const RECT bar = split->GetBarRect();
    const POINT barCenter = { (bar.left + bar.right) / 2, (bar.top + bar.bottom) / 2 };
    SelfTestDrag(hwnd, barCenter, -kSelfTestDragPx);
    SelfTestPump();
    const RECT paneRect = pane->GetRect();
    LOG_INFO("compose selftest: notes pane width %d -> %d (rect width %d).", paneBefore, split->RightWidth(),
             (int)(paneRect.right - paneRect.left));
    checks.Check(split->RightWidth() == paneBefore + kSelfTestDragPx
                 && paneRect.right - paneRect.left == split->RightWidth(), "dragging the bar widens the notes pane");

    //六、点「»」收起：窗口缩回；点「批注 (N)」展开：窗口按批注栏宽度加宽
    const int widthOpen = SelfTestWindowWidth(hwnd);
    SelfTestClick(hwnd, root->FindCtrlById(kCodeNotesIdCollapse));
    SelfTestPump();
    const int widthClosed = SelfTestWindowWidth(hwnd);
    checks.Check(split->IsCollapsed() && widthClosed < widthOpen, "collapsing the pane shrinks the window");
    SelfTestClick(hwnd, notesButton);
    SelfTestPump();
    const int widthReopened = SelfTestWindowWidth(hwnd);
    LOG_INFO("compose selftest: window width open %d, collapsed %d, reopened %d, pane %d.", widthOpen, widthClosed,
             widthReopened, split->RightWidth());
    checks.Check(!split->IsCollapsed() && widthReopened - widthClosed == split->RightWidth() + split->GetBarThickness(),
                 "expanding the pane widens the window by the pane width");

    //七、代码查看窗：有批注时批注栏展开、显示「批注 (N)」；点卡片选中代码行；点「批注 (N)」收起
    const std::wstring code = balloonchatwnd::NormalizeCodeNewlines(std::wstring(kSamples[0]));
    CCodeViewerWindow::Show(m_hWnd, code, codesnippet::code_lang_cpp, kSelfTestViewerKey, pane->Notes());
    SelfTestPump();
    CCodeViewerWindow* viewer = CCodeViewerWindow::FindOpened(kSelfTestViewerKey);
    checks.Check(viewer != NULL, "code viewer opened");
    if (viewer != NULL)
    {
        balloonwjui::DuiControl* vroot = viewer->GetClientContent();
        CodeNotesSplitter* vsplit = (vroot != NULL)
            ? dynamic_cast<CodeNotesSplitter*>(vroot->FindCtrlById(kCodeViewerIdSplit)) : NULL;
        balloonwjui::DuiControl* vnotes = (vroot != NULL) ? vroot->FindCtrlById(kCodeViewerIdNotes) : NULL;
        balloonwjui::DuiRichEdit* vedit = (vroot != NULL)
            ? dynamic_cast<balloonwjui::DuiRichEdit*>(vroot->FindCtrlById(kCodeViewerIdEdit)) : NULL;
        checks.Check(vsplit != NULL && !vsplit->IsCollapsed() && vnotes != NULL && vnotes->IsVisible(),
                     "viewer opens with the notes pane and the notes button");
        if (vsplit != NULL && vnotes != NULL && vedit != NULL)
        {
            SelfTestClick(viewer->m_hWnd, SelfTestCardPart(vroot, 0, kCodeNotesPartRange));
            SelfTestPump();
            long vb = 0;
            long ve = 0;
            vedit->GetSel(vb, ve);
            const std::vector<codesnippet::CodeNote> vnotesList = pane->Notes();
            const long vexpect = (long)vedit->SendMessageToEngine(EM_LINEINDEX, (WPARAM)(vnotesList[0].m_from - 1), 0);
            LOG_INFO("compose selftest: viewer range link selection %ld-%ld, expected begin %ld.", vb, ve, vexpect);
            checks.Check(vb == vexpect && ve > vb, "viewer range link selects the note lines");
            SelfTestClick(viewer->m_hWnd, vnotes);
            SelfTestPump();
            checks.Check(vsplit->IsCollapsed(), "viewer notes button collapses the pane");
        }
        viewer->PostMessage(WM_CLOSE);
        SelfTestPump();
    }

    //八、在代码查看窗里追加批注（docs/代码片段方案.md 第 12 节）：别人的批注只读、本人的可以修改；在别人批注的行上
    //点「添加批注」是新建一条；没有新增或修改时不能发送；发送后宿主收到原消息的 msgid 与两条「新」批注，查看窗关闭
    std::vector<codesnippet::CodeNote> original;
    original.push_back(codesnippet::CodeNote(kDemoNote1Line, kDemoNote1Line, L"note by the code's sender", kSelfTestOtherId));
    original.push_back(codesnippet::CodeNote(kDemoNote2From, kDemoNote2To, L"my earlier note", kSelfTestSelfId));
    CodeViewerExtras extras;
    extras.m_canAnnotate = true;
    extras.m_msgid = kSelfTestBaseMsgId;
    extras.m_senderId = kSelfTestOtherId;
    extras.m_fromMe = false;
    extras.m_selfId = kSelfTestSelfId;
    extras.m_senderName = L"Li Si";
    extras.m_authorNames[kSelfTestSelfId] = L"Me";
    extras.m_authorNames[kSelfTestOtherId] = L"Li Si";
    CCodeViewerWindow::Show(m_hWnd, code, codesnippet::code_lang_cpp, kSelfTestAnnotateKey, original, extras);
    SelfTestPump();
    CCodeViewerWindow* av = CCodeViewerWindow::FindOpened(kSelfTestAnnotateKey);
    balloonwjui::DuiControl* aroot = (av != NULL) ? av->GetClientContent() : NULL;
    balloonwjui::DuiRichEdit* aeditor = (aroot != NULL)
        ? dynamic_cast<balloonwjui::DuiRichEdit*>(aroot->FindCtrlById(kCodeViewerIdEdit)) : NULL;
    CodeNotesPane* apane = (aroot != NULL) ? dynamic_cast<CodeNotesPane*>(aroot->FindCtrlById(kCodeViewerIdNotesPane)) : NULL;
    balloonwjui::DuiControl* abar = (aroot != NULL) ? aroot->FindCtrlById(kCodeViewerIdAnnotateBar) : NULL;
    balloonwjui::DuiControl* asend = (aroot != NULL) ? aroot->FindCtrlById(kCodeViewerIdAnnotateSend) : NULL;
    balloonwjui::DuiControl* anotes = (aroot != NULL) ? aroot->FindCtrlById(kCodeViewerIdAnnotate) : NULL;
    if (aeditor == NULL || apane == NULL || abar == NULL || asend == NULL || anotes == NULL)
    {
        checks.Check(false, "annotating viewer controls found");
        LOG_INFO("compose selftest: done, %d checks, %d failed.", checks.Total(), checks.Failed());
        return;
    }
    const HWND ahwnd = av->m_hWnd;
    checks.Check(aeditor->IsReadOnly() && abar->IsVisible() && anotes->IsVisible(),
                 "the viewer shows the add note button and the send row, the code stays read-only");
    checks.Check(!asend->IsEnabled() && av->UnsentNoteCount() == 0, "the viewer cannot send before a note is added or changed");
    //卡片按起始行排列：第 0 张是对方的（只读），第 1 张是本人的（可以修改）
    checks.Check(SelfTestCardPart(aroot, 0, kCodeNotesPartEdit) == NULL
                 && SelfTestCardPart(aroot, 1, kCodeNotesPartEdit) != NULL
                 && SelfTestCardPart(aroot, 0, kCodeNotesPartAuthor) != NULL,
                 "only my own note card has edit links and cards show authors");

    //光标放到对方批注的那一行，点「批注」：新建一条，而不是修改对方的
    const long long annotLine = kDemoNote1Line;
    const long acp = (long)aeditor->SendMessageToEngine(EM_LINEINDEX, (WPARAM)(annotLine - 1), 0);
    aeditor->SetSel(acp, acp);
    SelfTestPump();
    SelfTestClick(ahwnd, anotes);
    SelfTestPump();
    checks.Check(apane->IsEditing() && apane->EditingIndex() == -1, "adding on the other author's line starts a new note");
    if (apane->EditBox() != NULL)
        apane->EditBox()->SetText(L"selftest reply on the same line");
    SelfTestClick(ahwnd, aroot->FindCtrlById(kCodeNotesIdEditOk));
    SelfTestPump();
    checks.Check(!apane->IsEditing() && apane->Notes().size() == original.size() + 1
                 && codesnippet::CountNewCodeNotes(apane->Notes()) == 1 && asend->IsEnabled(),
                 "the new note is marked new and sending becomes possible");

    //修改本人原有的那一条：点它的「修改」，改说明后确定
    int mine = -1;
    for (size_t i = 0; i < apane->Notes().size(); ++i)
    {
        if (apane->Notes()[i].m_authorId == kSelfTestSelfId && !apane->Notes()[i].m_isNew)
            mine = (int)i;
    }
    SelfTestClick(ahwnd, SelfTestCardPart(aroot, mine, kCodeNotesPartEdit));
    SelfTestPump();
    checks.Check(mine >= 0 && apane->IsEditing() && apane->EditingIndex() == mine, "edit link opens my own note");
    if (apane->EditBox() != NULL)
        apane->EditBox()->SetText(L"my earlier note, changed");
    SelfTestClick(ahwnd, aroot->FindCtrlById(kCodeNotesIdEditOk));
    SelfTestPump();
    checks.Check(codesnippet::CountNewCodeNotes(apane->Notes()) == 2, "changing my own note marks it new");

    //关窗前有两条没发出的批注（关窗时据此询问；询问框不在自测里弹出）
    checks.Check(av->UnsentNoteCount() == 2, "the viewer counts two unsent notes");

    //发送：宿主（本窗口）收到原消息的 msgid 与两条「新」批注，查看窗随即关闭
    m_lastAnnotateBaseMsgId = 0;
    m_lastAnnotateNewNotes = 0;
    SelfTestClick(ahwnd, asend);
    SelfTestPump();
    LOG_INFO("compose selftest: annotation sent, base msgid %lld, new notes %d.", m_lastAnnotateBaseMsgId,
             m_lastAnnotateNewNotes);
    checks.Check(m_lastAnnotateBaseMsgId == kSelfTestBaseMsgId && m_lastAnnotateNewNotes == 2,
                 "the host receives the base message id and the new notes");
    checks.Check(CCodeViewerWindow::FindOpened(kSelfTestAnnotateKey) == NULL, "the viewer closes after sending");
    LOG_INFO("compose selftest: done, %d checks, %d failed.", checks.Total(), checks.Failed());
}

void CCodeSnippetDemoWindow::LoadNextSample()
{
    if (m_pEditor == NULL)
        return;
    m_previewNotes.clear();
    m_pEditor->SetText(kSamples[m_sampleIndex]);
    m_sampleIndex = (m_sampleIndex + 1) % kSampleCount;
    if (m_pLanguage != NULL)
        m_pLanguage->SetCurSel(0, false);
    RefreshPreview();
}

LRESULT CCodeSnippetDemoWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //粘贴区文字变了：延时刷新，连续输入时只刷新一次
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kIdEditor)
    {
        SetTimer(kTimerRefresh, kRefreshDelayMs, NULL);
        return 0;
    }
    //换了语言、开关了行号或自动换行：立即刷新
    if (pNotify->code == DUIN_VALUECHANGED
        && (pNotify->ctrlId == kIdLanguage || pNotify->ctrlId == kIdLineNumbers || pNotify->ctrlId == kIdWrap))
    {
        RefreshPreview();
        return 0;
    }
    //预览区内容高度可能变了（展开、收起、复制字样变化）：重排滚动视图
    if (pNotify->code == kPreviewNotifyRelayout && pNotify->ctrlId == kIdPreview)
    {
        if (m_pScroll != NULL)
        {
            m_pScroll->ForceLayout(m_pScroll->GetRect());
            m_pScroll->Invalidate();
        }
        return 0;
    }
    //点了卡片上的「复制」：一段时间后恢复字样
    if (pNotify->code == kPreviewNotifyCopied && pNotify->ctrlId == kIdPreview)
    {
        SetTimer(kTimerCopied, kCopiedShowMs, NULL);
        return 0;
    }
    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //「从剪贴板粘贴」：用剪贴板里的文字替换粘贴区
        case kIdPaste:
            PasteFromClipboard();
            return 0;

        //「载入示例」：轮流填入下一段示例代码
        case kIdSample:
            LoadNextSample();
            return 0;

        //「清空」：清空粘贴区
        case kIdClear:
            if (m_pEditor != NULL)
                m_pEditor->SetText(L"");
            RefreshPreview();
            return 0;

        //其它控件的点击（含标题栏按钮）：交给框架
        default:
            break;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeSnippetDemoWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //粘贴区文字变化后的延时刷新
    if (wParam == kTimerRefresh)
    {
        KillTimer(kTimerRefresh);
        RefreshPreview();
        return 0;
    }
    //--compose-selftest：发送代码窗口已经显示，开始自测
    if (wParam == kTimerSelfTest)
    {
        KillTimer(kTimerSelfTest);
        RunComposeSelfTest();
        return 0;
    }
    //「已复制」显示时间到
    if (wParam == kTimerCopied)
    {
        KillTimer(kTimerCopied);
        if (m_pPreview != NULL)
            m_pPreview->ClearCopied();
        return 0;
    }
    //balloonui 宿主窗口自己的定时器：交给框架
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeSnippetDemoWindow::OnComposeSend(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    CodeComposeResult* pResult = (CodeComposeResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_previewNotes = pResult->m_notes;
    //自测第八部分据此检查批注模式交回的结果
    m_lastAnnotateBaseMsgId = pResult->m_annotate.m_baseMsgId;
    m_lastAnnotateNewNotes = codesnippet::CountNewCodeNotes(pResult->m_notes);
    if (m_pEditor != NULL)
        m_pEditor->SetText(pResult->m_code.c_str());
    //下拉框第 0 项为「自动识别」，第 i 项为语言 i - 1
    if (m_pLanguage != NULL)
        m_pLanguage->SetCurSel(pResult->m_manualLang ? pResult->m_lang + 1 : 0, false);
    RefreshPreview();
    LOG_INFO("code snippet demo: compose window handed %u chars, lang=%d, manual=%d, notes=%u.",
             (unsigned)pResult->m_code.size(), pResult->m_lang, pResult->m_manualLang ? 1 : 0,
             (unsigned)pResult->m_notes.size());
    delete pResult;
    return 0;
}

LRESULT CCodeSnippetDemoWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //发送代码窗口是独立的顶层窗口，随本窗口关闭
    m_compose.Close();
    //调试入口下本窗口是唯一可见窗口，关闭即请宿主退出进程
    if (m_hHost != NULL && ::IsWindow(m_hHost))
        ::PostMessage(m_hHost, WM_CLOSE_MAINDLG, 0, 0);
    if (IsWindow())
        DestroyWindow();
    return 0;
}

LRESULT CCodeSnippetDemoWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    KillTimer(kTimerRefresh);
    KillTimer(kTimerCopied);
    //调试入口退出时直接销毁本窗口、不经过 WM_CLOSE：发送代码窗口在这里一并关闭
    m_compose.Close();
    //控件树随窗口销毁，借用的指针一并作废；编辑框字体等控件销毁后在析构函数里释放
    m_pLanguage = NULL;
    m_pLineNumbers = NULL;
    m_pWrap = NULL;
    m_pStatus = NULL;
    m_pEditor = NULL;
    m_pScroll = NULL;
    m_pPreview = NULL;
    bHandled = FALSE;
    return 0;
}

} // namespace ballooncode
