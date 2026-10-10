/**
 *  Markdown 编辑器的单元测试，用例 ED1 ~ ED25（docs/Markdown消息方案.md 第 11.5 节、第 12 节与第 13 节）：
 *
 *    - ED1  打开文件时识别编码：UTF-8（有无 BOM）、UTF-16LE（BOM）、ANSI，纯 ASCII 按 UTF-8
 *    - ED2  保存为不带 BOM 的 UTF-8，换行一律写成 CRLF，读回后与原文相同
 *    - ED3  编辑框文字与源文本互转：\r、\r\n 统一成 \n；反过来写成 \r\n
 *    - ED4  源文本与聊天正文互转：字面的 /f["5"] 转义后能原样还原，网址里的 // 不受影响
 *    - ED5  从源文本粗取标题：去掉 # 与空白、跳过空行、超长截断
 *    - ED6  窗口标题：未命名、只显示文件名、未保存时加 *
 *    - ED7  底部发送对象的文字：未登录、未选择、一个、多个、名字为空时显示 id
 *    - ED8  发送前检查：未登录、空文档、超限、没有对象、可以发送，以及对应的说明文字
 *    - ED9  控件树：各控件都在，按默认尺寸与最小尺寸排得下（底部按钮不被压扁、预览开关不越界）
 *    - ED10 导出 .md 与文件读写：内容与「保存」相同，读文件的大小上限，菜单文字
 *    - ED11 选会话窗口的 Markdown 选对象模式：标题「选择发送对象」、按钮「确定」、预览为标题加摘要、可以多选
 *    - ED12 预览窗口正文上方的「发送者 · 时间」
 *    - ED13 预览窗口的控件树按默认尺寸与最小尺寸排得下
 *    - ED14 导出 .html 与 .png：内容、PNG 尺寸、超长文档报告太高，DLL 不可用时两种格式不出现
 *    - ED15 图片式 PDF：结构完整、交叉引用表的偏移正确；实际导出多页
 *    - ED16 通过系统 PDF 打印机静默导出（隐藏用例 [.mdpdf]，需在不可见桌面里单独运行）
 *    - ED17 导出 Word 文档（.docx）：ZIP 包里有正文部件；DLL 不可用时导出菜单里没有 Word
 *    - ED18 先按建窗时的大尺寸排过版、再缩到设计尺寸，之后调用 ResetMarkdownEditorSplit，左右两栏各占一半
 *           （附不调用时左栏偏宽的对照）
 *    - ED19 选发送对象窗口用的标题与摘要按卡片规则取（MarkdownCardTextFromSource），摘要里不重复标题；DLL 不可用时返回 false
 *    - ED20 MarkdownEarlySendTracker：首屏渲染前发出的文档都确认入库、且首屏已渲染时才重新拉取一次，覆盖确认早于 / 晚于首屏、
 *           部分被拒绝、全部被拒绝、首屏之后发出、重新请求后再次记录
 *    - ED21 预览与导出的排版样式（MakePreviewMarkdownStyle）设了撑满宽度，文档宽度等于排版宽度；不设时收缩
 *    - ED22 把示例文档按预览窗口的样式导出成 PNG 供人工核对（隐藏用例 [.mdpng]，输出到环境变量 MD_SHOT_DIR 所指目录）
 *    - ED23 复制用的 CF_HTML 头部（MarkdownClipboard 的 WrapCfHtml）：StartHTML 指向 <html>，片段偏移正好包住片段，
 *           EndHTML 等于总长，中文按 UTF-8 字节计；缺少片段标记时为空
 *    - ED24 预览复制要放进剪贴板的内容：有选区时纯文字与这一段的 HTML 各自正确，选区为空不复制；函数表只到第三版
 *           （旧版 DLL）时只有纯文字；「复制全文（带格式）」的纯文字去掉标记、HTML 为全文
 *    - ED25 查找条（第 13 节）：默认隐藏；显示查找行、两行都显示、隐藏、隐藏后再显示，各行排好、编辑区让出或收回高度，
 *           底部「发送」排得下；查找按键的对应
 *
 *  框架 Catch2 v3，标签 [mdeditor]。ED10 在系统临时目录里读写文件，用完删除。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "../UI/BalloonMarkdown/MarkdownEditorModel.h"
#include "../UI/BalloonMarkdown/MarkdownEditorLayout.h"
#include "../UI/BalloonMarkdown/MarkdownExport.h"
#include "../UI/BalloonMarkdown/MarkdownClipboard.h"   // ED23、ED24：复制用的 CF_HTML
#include "../UI/BalloonMarkdown/MarkdownFindReplace.h"   // ED25：查找按键的对应
#include "../UI/BalloonMarkdown/MarkdownPdf.h"
#include "../UI/BalloonChatWnd/ChatMarkdown.h"
#include "../UI/BalloonChatWnd/ChatMarkdownFormatBar.h"
#include "../net/Protocol.h"
#include "LayoutFitCheck.h"
#include "../UI/BalloonMain/ForwardRequest.h"       // ED11：选会话窗口的 Markdown 选对象模式
#include "../UI/BalloonMain/ForwardTargetModel.h"
#include "../Feature/Markdown/MarkdownDocument.h"   // ED14：用编进测试程序的函数表代替 DLL
#include "../../FlamingoMarkdown/MdApiTable.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"

using namespace balloonmd;
using namespace balloonwjui;

namespace {

//UTF-16 转 UTF-8
std::string U8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

//UTF-16 转系统 ANSI 代码页
std::string Ansi(const std::wstring& text)
{
    const int len = ::WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

//系统临时目录里的一个文件路径
std::wstring TempFilePath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

//两个发送对象
std::vector<MarkdownSendTarget> MakeTargets(size_t count)
{
    std::vector<MarkdownSendTarget> targets;
    for (size_t i = 0; i < count; ++i)
    {
        MarkdownSendTarget t;
        t.m_isGroup = (i % 2) == 1;
        t.m_id = static_cast<int64_t>(100 + i);
        t.m_name = (i == 0) ? L"张三" : L"研发群";
        targets.push_back(t);
    }
    return targets;
}

} // namespace

TEST_CASE("ED1 打开文件时识别编码", "[mdeditor]")
{
    const std::wstring text = L"# 标题\r\n正文 **加粗**";
    int encoding = -1;

    //UTF-8，没有 BOM
    CHECK(DecodeMarkdownFileBytes(U8(text), &encoding) == text);
    CHECK(encoding == md_file_utf8);

    //UTF-8，带 BOM：BOM 被去掉
    const std::string bom = std::string("\xEF\xBB\xBF") + U8(text);
    CHECK(DecodeMarkdownFileBytes(bom, &encoding) == text);
    CHECK(encoding == md_file_utf8_bom);

    //UTF-16LE，带 BOM
    std::string utf16("\xFF\xFE", 2);
    utf16.append(reinterpret_cast<const char*>(text.c_str()), text.size() * sizeof(wchar_t));
    CHECK(DecodeMarkdownFileBytes(utf16, &encoding) == text);
    CHECK(encoding == md_file_utf16le);

    //ANSI：中文按系统代码页编码后不是合法的 UTF-8，按代码页解码回原文
    const std::string ansi = Ansi(text);
    if (ansi != U8(text))
    {
        CHECK(DecodeMarkdownFileBytes(ansi, &encoding) == text);
        CHECK(encoding == md_file_ansi);
    }

    //纯 ASCII 按 UTF-8；空文件得到空串；出参可以为 NULL
    CHECK(DecodeMarkdownFileBytes("# a", &encoding) == L"# a");
    CHECK(encoding == md_file_utf8);
    CHECK(DecodeMarkdownFileBytes(std::string(), NULL).empty());
}

TEST_CASE("ED2 保存为不带 BOM 的 UTF-8，换行写成 CRLF", "[mdeditor]")
{
    const std::wstring editor = L"# 标题\r正文\r\n- 列表\n结尾";
    const std::string saved = EncodeMarkdownForSave(editor);
    CHECK(saved == U8(L"# 标题\r\n正文\r\n- 列表\r\n结尾"));
    //没有 BOM
    REQUIRE(saved.size() >= 3);
    CHECK_FALSE((saved[0] == '\xEF' && saved[1] == '\xBB' && saved[2] == '\xBF'));
    //读回来与原文相同（换行已统一）
    CHECK(DecodeMarkdownFileBytes(saved, NULL) == L"# 标题\r\n正文\r\n- 列表\r\n结尾");
    CHECK(EncodeMarkdownForSave(std::wstring()).empty());
}

TEST_CASE("ED3 编辑框文字与源文本互转", "[mdeditor]")
{
    CHECK(EditorTextToMarkdownUtf8(L"a\rb\r\nc\nd") == "a\nb\nc\nd");
    CHECK(MarkdownUtf8ToEditorText("a\nb\r\nc\rd") == L"a\r\nb\r\nc\r\nd");
    CHECK(EditorTextToMarkdownUtf8(L"中文") == U8(L"中文"));
    CHECK(MarkdownUtf8ToEditorText(std::string()).empty());
}

TEST_CASE("ED4 源文本与聊天正文互转能还原", "[mdeditor]")
{
    const std::string sources[] =
    {
        U8(L"# 标题\n见 https://example.com/a//b"),
        U8(L"字面的 /f[\"5\"] 不是表情"),
        U8(L"//f[\"7\"] 与 /x"),
        std::string("plain text"),
    };
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i)
    {
        INFO("source " << i);
        const std::string body = MarkdownBodyFromSourceUtf8(sources[i]);
        const int len = ::MultiByteToWideChar(CP_UTF8, 0, body.data(), static_cast<int>(body.size()), NULL, 0);
        std::wstring wide(static_cast<size_t>(len), L'\0');
        if (len > 0)
            ::MultiByteToWideChar(CP_UTF8, 0, body.data(), static_cast<int>(body.size()), &wide[0], len);
        CHECK(balloonchatwnd::MarkdownSourceFromBody(CString(wide.c_str())) == sources[i]);
    }
    //字面的表情标记在正文里被转义
    CHECK(MarkdownBodyFromSourceUtf8(U8(L"/f[\"5\"]")).find("//f[") != std::string::npos);
    //网址里的 // 原样保留
    CHECK(MarkdownBodyFromSourceUtf8("http://a.b") == "http://a.b");
    CHECK(MarkdownBodyFromSourceUtf8(std::string()).empty());
}

TEST_CASE("ED5 从源文本粗取标题", "[mdeditor]")
{
    CHECK(MarkdownTitleFromSource(U8(L"# 发布说明\n正文")) == L"发布说明");
    CHECK(MarkdownTitleFromSource(U8(L"\n\n   ##   Title  \nbody")) == L"Title");
    CHECK(MarkdownTitleFromSource(U8(L"第一行文字\n第二行")) == L"第一行文字");
    CHECK(MarkdownTitleFromSource(std::string()).empty());
    CHECK(MarkdownTitleFromSource("\n  \n#\n").empty());
    //超长截断并加省略号
    const std::wstring longTitle(kMarkdownTitleMaxChars + 10, L'字');
    const std::wstring got = MarkdownTitleFromSource(U8(longTitle));
    CHECK(got.size() == kMarkdownTitleMaxChars + 1);
    CHECK(got[got.size() - 1] == L'\x2026');
}

TEST_CASE("ED6 窗口标题", "[mdeditor]")
{
    CHECK(MarkdownEditorCaption(std::wstring(), false) == L"Markdown 编辑器 - 未命名");
    CHECK(MarkdownEditorCaption(std::wstring(), true) == L"Markdown 编辑器 - *未命名");
    CHECK(MarkdownEditorCaption(L"D:\\docs\\notes.md", false) == L"Markdown 编辑器 - notes.md");
    CHECK(MarkdownEditorCaption(L"D:/docs/notes.md", true) == L"Markdown 编辑器 - *notes.md");
}

TEST_CASE("ED7 底部发送对象的文字", "[mdeditor]")
{
    CHECK(MarkdownSendTargetsText(false, MakeTargets(1)) == L"登录后可发送");
    CHECK(MarkdownSendTargetsText(true, MakeTargets(0)) == L"未选择发送对象");
    CHECK(MarkdownSendTargetsText(true, MakeTargets(1)) == L"发送给：张三");
    CHECK(MarkdownSendTargetsText(true, MakeTargets(3)) == L"发送给：张三 等 3 个");
    std::vector<MarkdownSendTarget> noName = MakeTargets(1);
    noName[0].m_name.clear();
    CHECK(MarkdownSendTargetsText(true, noName) == L"发送给：100");
}

TEST_CASE("ED8 发送前检查", "[mdeditor]")
{
    const std::string doc = U8(L"# 标题\n正文");
    CHECK(CheckMarkdownSend(false, doc, 1) == md_send_offline);
    CHECK(CheckMarkdownSend(true, std::string(), 1) == md_send_empty);
    CHECK(CheckMarkdownSend(true, " \n\t\r\n", 1) == md_send_empty);
    CHECK(CheckMarkdownSend(true, std::string(MESSAGE_CONTENT_MAX_BYTES + 1, 'a'), 1) == md_send_too_large);
    CHECK(CheckMarkdownSend(true, doc, 0) == md_send_no_target);
    CHECK(CheckMarkdownSend(true, doc, 2) == md_send_ok);
    //超限优先于没有对象：先告诉用户文档本身发不出去
    CHECK(CheckMarkdownSend(true, std::string(MESSAGE_CONTENT_MAX_BYTES + 1, 'a'), 0) == md_send_too_large);

    CHECK(MarkdownSendCheckText(md_send_ok).empty());
    CHECK_FALSE(MarkdownSendCheckText(md_send_offline).empty());
    CHECK_FALSE(MarkdownSendCheckText(md_send_empty).empty());
    CHECK_FALSE(MarkdownSendCheckText(md_send_too_large).empty());
    CHECK_FALSE(MarkdownSendCheckText(md_send_no_target).empty());
}

TEST_CASE("ED9 控件树各控件都在，按默认尺寸与最小尺寸排得下", "[mdeditor]")
{
    std::unique_ptr<DuiControl> root = BuildMarkdownEditorBody();
    REQUIRE(root != NULL);
    const UINT ids[] =
    {
        kMdEdIdOpen, kMdEdIdSave, kMdEdIdExport, kMdEdIdFormatBar, kMdEdIdPreviewToggle, kMdEdIdSplitter,
        kMdEdIdEditor, kMdEdIdPreviewScroll, kMdEdIdPreview, kMdEdIdTargets, kMdEdIdPickTargets, kMdEdIdSend,
        kMdEdIdStatus,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id " << ids[i]);
        CHECK(root->FindCtrlById(ids[i]) != NULL);
    }
    for (int i = 0; i < balloonchatwnd::kMarkdownEditCommandCount; ++i)
        CHECK(root->FindCtrlById(kMdEdIdFormatFirst + i) != NULL);
    for (int i = 0; i < balloonchatwnd::kMarkdownEditCommandCount; ++i)
        CHECK(MarkdownFormatTipSource(i)[0] != L'\0');
    CHECK(MarkdownFormatTipSource(-1)[0] == L'\0');

    //分隔条左右排：左边编辑框、右边预览
    DuiSplitter* split = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kMdEdIdSplitter));
    REQUIRE(split != NULL);
    CHECK(split->GetOrientation() == DuiSplitter::Vertical);
    REQUIRE(split->GetPane(0) != NULL);
    REQUIRE(split->GetPane(1) != NULL);
    CHECK(split->GetPane(0)->GetCtrlId() == kMdEdIdEditor);
    CHECK(split->GetPane(1)->GetCtrlId() == kMdEdIdPreviewScroll);

    const int sizes[2][2] =
    {
        { kMdEdWindowWidth, kMdEdWindowHeight },
        { kMdEdMinWidth, kMdEdMinHeight },
    };
    for (int s = 0; s < 2; ++s)
    {
        const int width = sizes[s][0];
        const int contentH = sizes[s][1] - kMdEdTitleBarH;
        INFO("window " << width << "x" << sizes[s][1]);
        //底部「发送」不被压扁、下方留白与空间充裕时相同
        const layoutfit::FitResult send = layoutfit::MeasureAt(root.get(), kMdEdIdSend, width, contentH);
        REQUIRE(send.m_found);
        CHECK(send.m_bottom <= contentH);
        CHECK(send.m_height == send.m_roomyHeight);
        CHECK(send.m_bottomGap == send.m_roomyBottomGap);
        //编辑区与预览区都有高度，宽度不小于最小值
        const RECT ed = root->FindCtrlById(kMdEdIdEditor)->GetRect();
        const RECT pv = root->FindCtrlById(kMdEdIdPreviewScroll)->GetRect();
        CHECK(ed.bottom - ed.top > 0);
        CHECK(ed.right - ed.left >= kMdEdPaneMinPx);
        CHECK(pv.right - pv.left >= kMdEdPaneMinPx);
        //「预览」开关完整显示在顶部一行之内；显示的格式按钮都在状态文字左边
        DuiControl* toggle = root->FindCtrlById(kMdEdIdPreviewToggle);
        CHECK(toggle->GetRect().right <= toggle->GetParent()->GetRect().right);
        const RECT status = root->FindCtrlById(kMdEdIdStatus)->GetRect();
        for (int i = 0; i < balloonchatwnd::kMarkdownEditCommandCount; ++i)
        {
            DuiControl* btn = root->FindCtrlById(kMdEdIdFormatFirst + i);
            if (btn->IsVisible())
                CHECK(btn->GetRect().right <= status.left);
        }
    }
    //默认宽度下全部格式按钮都显示（第 13 节起共 15 个）
    layoutfit::MeasureAt(root.get(), kMdEdIdSend, kMdEdWindowWidth, kMdEdWindowHeight - kMdEdTitleBarH);
    balloonchatwnd::ChatMarkdownFormatBar* bar =
        dynamic_cast<balloonchatwnd::ChatMarkdownFormatBar*>(root->FindCtrlById(kMdEdIdFormatBar));
    REQUIRE(bar != NULL);
    CHECK(bar->GetHiddenCount() == 0);
}

TEST_CASE("ED10 导出 .md 与文件读写", "[mdeditor]")
{
    const std::wstring path = TempFilePath(L"flamingo_md_export_test.md");
    const std::string source = U8(L"# 标题\n正文");
    std::wstring error;
    REQUIRE(ExportMarkdownToFile(path, source, md_export_md, &error));
    CHECK(error.empty());
    std::string bytes;
    bool tooLarge = true;
    REQUIRE(ReadWholeFile(path, kMarkdownOpenMaxBytes, bytes, &tooLarge));
    CHECK_FALSE(tooLarge);
    //与「保存」写出的内容相同：UTF-8 不带 BOM，换行为 CRLF
    CHECK(bytes == U8(L"# 标题\r\n正文"));
    CHECK(bytes == EncodeMarkdownForSave(MarkdownUtf8ToEditorText(source)));

    //超过上限时读失败并报告 tooLarge
    CHECK_FALSE(ReadWholeFile(path, 3, bytes, &tooLarge));
    CHECK(tooLarge);
    ::DeleteFileW(path.c_str());
    //文件不存在时读失败，tooLarge 为 false
    CHECK_FALSE(ReadWholeFile(path, kMarkdownOpenMaxBytes, bytes, &tooLarge));
    CHECK_FALSE(tooLarge);

    CHECK(IsMarkdownExportAvailable(md_export_md));
    for (int format = md_export_md; format <= md_export_count; ++format)
        CHECK_FALSE(MarkdownExportMenuText(format).empty());
    CHECK(MarkdownExportMenuText(0).empty());
    CHECK(MarkdownExportMenuText(md_export_count + 1).empty());
}

TEST_CASE("ED11 选会话窗口的 Markdown 选对象模式：标题、按钮、预览与多选", "[mdeditor]")
{
    balloonmain::ForwardRequest r;
    r.m_markdownPick = true;
    r.m_title = L"发布说明";
    r.m_markdownDigest = L"本次更新包含三项修复";
    CHECK(balloonmain::ForwardPickerTitle(r) == L"选择发送对象");
    CHECK(balloonmain::ForwardPickerConfirmText(r) == L"确定");
    CHECK(balloonmain::ForwardPickerContentTitle(r) == L"文档");
    CHECK(balloonmain::ForwardPickerTargetTitle(r) == L"发送给");
    //可以多选：一篇文档可以同时发给几个会话
    CHECK_FALSE(balloonmain::ForwardPickerSingleSelect(r));
    CHECK(balloonmain::BuildForwardPreviewText(r) == L"[Markdown] 发布说明\n本次更新包含三项修复");
    r.m_markdownDigest.clear();
    CHECK(balloonmain::BuildForwardPreviewText(r) == L"[Markdown] 发布说明");

    //普通转发不受影响
    balloonmain::ForwardRequest plain;
    CHECK(balloonmain::ForwardPickerConfirmText(plain) == L"发送");
    CHECK(balloonmain::ForwardPickerTitle(plain) == L"转发");
}

TEST_CASE("ED12 预览窗口正文上方的「发送者 · 时间」", "[mdeditor]")
{
    //期望的时间按本机时区算（与实现同一口径）
    const long long ms = 1759717200000LL;   //2025-10-06 02:20:00 UTC
    const time_t seconds = static_cast<time_t>(ms / 1000);
    struct tm local;
    REQUIRE(localtime_s(&local, &seconds) == 0);
    wchar_t when[32] = { 0 };
    swprintf_s(when, L"%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min);

    CHECK(MarkdownPreviewMeta(L"张三", ms) == std::wstring(L"张三 · ") + when);
    CHECK(MarkdownPreviewMeta(L"张三", 0) == L"张三");
    CHECK(MarkdownPreviewMeta(std::wstring(), ms) == when);
    CHECK(MarkdownPreviewMeta(std::wstring(), 0).empty());
}

TEST_CASE("ED13 预览窗口的控件树按默认尺寸与最小尺寸排得下", "[mdeditor]")
{
    std::unique_ptr<DuiControl> root = BuildMarkdownPreviewBody();
    REQUIRE(root != NULL);
    const UINT ids[] = { kMdPvIdMeta, kMdPvIdCopy, kMdPvIdExport, kMdPvIdOpenEditor, kMdPvIdScroll, kMdPvIdPreview };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id " << ids[i]);
        CHECK(root->FindCtrlById(ids[i]) != NULL);
    }
    const int sizes[2][2] =
    {
        { kMdPvWindowWidth, kMdPvWindowHeight },
        { kMdPvMinWidth, kMdPvMinHeight },
    };
    for (int s = 0; s < 2; ++s)
    {
        const int width = sizes[s][0];
        const int contentH = sizes[s][1] - kMdPvTitleBarH;
        INFO("window " << width << "x" << sizes[s][1]);
        //「在编辑器中打开」不被压扁且在顶部一行之内；正文有高度
        const layoutfit::FitResult open = layoutfit::MeasureAt(root.get(), kMdPvIdOpenEditor, width, contentH);
        REQUIRE(open.m_found);
        CHECK(open.m_height == open.m_roomyHeight);
        DuiControl* openBtn = root->FindCtrlById(kMdPvIdOpenEditor);
        CHECK(openBtn->GetRect().right <= openBtn->GetParent()->GetRect().right);
        const RECT body = root->FindCtrlById(kMdPvIdScroll)->GetRect();
        CHECK(body.bottom - body.top > 0);
        CHECK(body.bottom <= contentH);
        //「发送者 · 时间」还留有宽度
        const RECT meta = root->FindCtrlById(kMdPvIdMeta)->GetRect();
        CHECK(meta.right - meta.left > 0);
    }
}

TEST_CASE("ED14 导出 .html 与 .png", "[mdeditor]")
{
    //用测试程序里编进来的函数表代替 DLL
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    const std::string source = U8(L"# 发布说明\n\n- **修复**登录超时\n\n```\ncode\n```");

    //HTML：标题写进 <title>，正文标签齐全
    const std::wstring htmlPath = TempFilePath(L"flamingo_md_export_test.html");
    std::wstring error;
    CHECK(IsMarkdownExportAvailable(md_export_html));
    REQUIRE(ExportMarkdownToFile(htmlPath, source, md_export_html, &error));
    std::string html;
    REQUIRE(ReadWholeFile(htmlPath, kMarkdownOpenMaxBytes, html, NULL));
    CHECK(html.find(U8(L"<title>发布说明</title>")) != std::string::npos);
    CHECK(html.find(U8(L"<h1>发布说明</h1>")) != std::string::npos);
    CHECK(html.find("<pre><code>code</code></pre>") != std::string::npos);
    ::DeleteFileW(htmlPath.c_str());

    //PNG：文件头是 PNG 签名，宽度为排版宽度加两侧留白
    const std::wstring pngPath = TempFilePath(L"flamingo_md_export_test.png");
    CHECK(IsMarkdownExportAvailable(md_export_png));
    REQUIRE(ExportMarkdownToFile(pngPath, source, md_export_png, &error));
    std::string png;
    REQUIRE(ReadWholeFile(pngPath, kMarkdownOpenMaxBytes, png, NULL));
    REQUIRE(png.size() > 24);
    CHECK(png.compare(0, 8, "\x89PNG\r\n\x1A\n", 8) == 0);
    //IHDR 里的宽度（大端 4 字节，位于第 16 ~ 19 字节）
    const unsigned width = (static_cast<unsigned char>(png[16]) << 24) | (static_cast<unsigned char>(png[17]) << 16)
                           | (static_cast<unsigned char>(png[18]) << 8) | static_cast<unsigned char>(png[19]);
    CHECK(width == static_cast<unsigned>(kMarkdownImageContentWidth + kMarkdownImagePad * 2));
    ::DeleteFileW(pngPath.c_str());

    //位图：尺寸与 PNG 一致；超长文档报告太高
    SIZE size = { 0, 0 };
    bool tooTall = true;
    HBITMAP bmp = RenderMarkdownBitmap(source, kMarkdownImageContentWidth, size, &tooTall);
    REQUIRE(bmp != NULL);
    CHECK_FALSE(tooTall);
    CHECK(size.cx == kMarkdownImageContentWidth + kMarkdownImagePad * 2);
    CHECK(size.cy > kMarkdownImagePad * 2);
    ::DeleteObject(bmp);
    std::string huge;
    for (int i = 0; i < 3000; ++i)
        huge += "line\n\n";
    CHECK(RenderMarkdownBitmap(huge, kMarkdownImageContentWidth, size, &tooTall) == NULL);
    CHECK(tooTall);

    //DLL 不可用时网页与图片不出现在导出菜单里，.md 仍可导出
    markdown::SetMarkdownApiOverrideForTest(true, NULL);
    CHECK_FALSE(IsMarkdownExportAvailable(md_export_html));
    CHECK_FALSE(IsMarkdownExportAvailable(md_export_png));
    CHECK(IsMarkdownExportAvailable(md_export_md));
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

namespace {

//在 PDF 内容里找第 k 个对象的起始位置应当出现的文字「k 0 obj」
bool ObjectAt(const std::string& pdf, size_t offset, unsigned k)
{
    char head[32] = { 0 };
    _snprintf_s(head, _TRUNCATE, "%u 0 obj", k);
    return pdf.compare(offset, strlen(head), head) == 0;
}

//一页纯色图片
PdfPageImage SolidPage(int w, int h, unsigned char value)
{
    PdfPageImage page;
    page.m_width = w;
    page.m_height = h;
    page.m_rgb.assign(static_cast<size_t>(w) * h * 3, value);
    return page;
}

} // namespace

TEST_CASE("ED15 图片式 PDF：结构完整、交叉引用表的偏移指向对应的对象；实际导出", "[mdeditor]")
{
    std::vector<PdfPageImage> pages;
    pages.push_back(SolidPage(40, 30, 0xFF));
    pages.push_back(SolidPage(40, 30, 0x80));
    std::string pdf;
    REQUIRE(BuildImagePdf(pages, pdf));
    CHECK(pdf.compare(0, 8, "%PDF-1.4") == 0);
    CHECK(pdf.find("/Count 2") != std::string::npos);
    CHECK(pdf.find("/MediaBox [0 0 595 842]") != std::string::npos);
    CHECK(pdf.find("/Filter /FlateDecode") != std::string::npos);
    CHECK(pdf.compare(pdf.size() - 6, 6, "%%EOF\n") == 0);
    //startxref 指向 xref 表；xref 里第 k 项指向「k 0 obj」
    const size_t sx = pdf.rfind("startxref\n");
    REQUIRE(sx != std::string::npos);
    const size_t xrefAt = static_cast<size_t>(strtoul(pdf.c_str() + sx + strlen("startxref\n"), NULL, 10));
    REQUIRE(pdf.compare(xrefAt, 5, "xref\n") == 0);
    const unsigned objects = 2 + 2 * 3;   //目录、页面树，加每页三个对象
    const size_t firstEntry = pdf.find('\n', pdf.find('\n', xrefAt) + 1) + 1;   //跳过「xref」「0 N」两行
    const size_t entryBytes = 20;
    for (unsigned k = 1; k <= objects; ++k)
    {
        INFO("object " << k);
        const size_t entry = firstEntry + entryBytes * k;
        const size_t offset = static_cast<size_t>(strtoul(pdf.c_str() + entry, NULL, 10));
        CHECK(ObjectAt(pdf, offset, k));
    }
    //参数不对时失败
    std::vector<PdfPageImage> none;
    CHECK_FALSE(BuildImagePdf(none, pdf));
    std::vector<PdfPageImage> bad(1, SolidPage(4, 4, 0));
    bad[0].m_rgb.resize(5);
    CHECK_FALSE(BuildImagePdf(bad, pdf));

    //实际导出（不需要打印机）：每页一张图，页数随文档长度增加
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    std::string longDoc = U8(L"# 长文档\n\n");
    for (int i = 0; i < 200; ++i)
        longDoc += "paragraph line\n\n";
    const std::wstring path = TempFilePath(L"flamingo_md_export_images.pdf");
    std::wstring error;
    REQUIRE(ExportMarkdownPdfAsImages(path, longDoc, &error));
    std::string file;
    REQUIRE(ReadWholeFile(path, 64 * 1024 * 1024, file, NULL));
    CHECK(file.compare(0, 8, "%PDF-1.4") == 0);
    CHECK(file.find("/Count 1 ") == std::string::npos);   //两百段文字放不进一页
    ::DeleteFileW(path.c_str());
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

TEST_CASE("ED16 通过系统 PDF 打印机静默导出（隐藏用例，需在不可见桌面里单独运行）", "[.mdpdf]")
{
    if (!IsPdfPrinterAvailable())
    {
        WARN("Microsoft Print to PDF is not installed, skipped.");
        return;
    }
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    const std::wstring path = TempFilePath(L"flamingo_md_export_printer.pdf");
    std::wstring error;
    REQUIRE(ExportMarkdownPdfViaPrinter(path, U8(L"# 打印测试\n\n**粗体** 与 `代码`\n\n| a | b |\n| - | - |\n| 1 | 2 |"), &error));
    std::string file;
    REQUIRE(ReadWholeFile(path, 64 * 1024 * 1024, file, NULL));
    CHECK(file.compare(0, 5, "%PDF-") == 0);
    //矢量 PDF：嵌入了字体并带 Unicode 对照表，文字可选中
    CHECK(file.find("/ToUnicode") != std::string::npos);
    ::DeleteFileW(path.c_str());
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

TEST_CASE("ED17 导出 Word 文档（.docx）", "[mdeditor]")
{
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    CHECK(IsMarkdownExportAvailable(md_export_docx));
    const std::wstring path = TempFilePath(L"flamingo_md_export_test.docx");
    std::wstring error;
    REQUIRE(ExportMarkdownToFile(path, U8(L"# 标题\n\n- 一\n- 二"), md_export_docx, &error));
    std::string file;
    REQUIRE(ReadWholeFile(path, kMarkdownOpenMaxBytes, file, NULL));
    //ZIP 包：以本地文件头签名开头，里面有 word/document.xml
    CHECK(file.compare(0, 4, "PK\x03\x04", 4) == 0);
    CHECK(file.find("word/document.xml") != std::string::npos);
    ::DeleteFileW(path.c_str());
    //DLL 不可用时导出菜单里没有 Word
    markdown::SetMarkdownApiOverrideForTest(true, NULL);
    CHECK_FALSE(IsMarkdownExportAvailable(md_export_docx));
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

TEST_CASE("ED18 先按建窗时的大尺寸排过版、再缩到设计尺寸，左右两栏仍各占一半", "[mdeditor]")
{
    //建窗时的默认尺寸（CW_USEDEFAULT 通常比设计尺寸宽得多），只用来模拟窗口的第一次排版
    const int kCreateWidth = 1600;
    const int kCreateHeight = 900;
    //两栏宽度允许的差（像素）：轨道宽度为奇数时一半除不尽
    const int kHalfTolerancePx = 1;
    const RECT created = { 0, 0, kCreateWidth, kCreateHeight - kMdEdTitleBarH };
    const RECT designed = { 0, 0, kMdEdWindowWidth, kMdEdWindowHeight - kMdEdTitleBarH };

    //对照（2026-10-06 实机核对时的现象）：不重新挂起比例时，第一次排版换算出的像素宽度保留下来，左栏明显偏宽
    {
        std::unique_ptr<DuiControl> root = BuildMarkdownEditorBody();
        REQUIRE(root != NULL);
        root->ForceLayout(created);
        root->ForceLayout(designed);
        DuiSplitter* split = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kMdEdIdSplitter));
        REQUIRE(split != NULL);
        const RECT left = split->GetPane(0)->GetRect();
        const RECT right = split->GetPane(1)->GetRect();
        CHECK((left.right - left.left) > (right.right - right.left) + kHalfTolerancePx);
    }

    //修正：缩到设计尺寸之后调用 ResetMarkdownEditorSplit（与窗口里 ResizeClient 之后调用的顺序相同）
    std::unique_ptr<DuiControl> root = BuildMarkdownEditorBody();
    REQUIRE(root != NULL);
    root->ForceLayout(created);
    root->ForceLayout(designed);
    CHECK(ResetMarkdownEditorSplit(root.get()));
    DuiSplitter* split = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kMdEdIdSplitter));
    REQUIRE(split != NULL);
    const RECT left = split->GetPane(0)->GetRect();
    const RECT right = split->GetPane(1)->GetRect();
    const int leftW = left.right - left.left;
    const int rightW = right.right - right.left;
    INFO("left " << leftW << ", right " << rightW);
    CHECK(leftW > 0);
    CHECK(std::abs(leftW - rightW) <= kHalfTolerancePx);

    //参数不对时不做任何事
    CHECK_FALSE(ResetMarkdownEditorSplit(NULL));
    std::unique_ptr<DuiControl> preview = BuildMarkdownPreviewBody();
    CHECK_FALSE(ResetMarkdownEditorSplit(preview.get()));
}

TEST_CASE("ED19 选发送对象窗口用的标题与摘要按卡片规则取，摘要里不重复标题", "[mdeditor]")
{
    //用测试程序里编进来的函数表代替 DLL
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    std::wstring title;
    std::wstring summary;
    REQUIRE(balloonchatwnd::MarkdownCardTextFromSource(U8(L"# 本周进展\n\n完成了 **两件事**\n\n- 编辑器"), title, summary));
    CHECK(title == L"本周进展");
    //2026-10-06 实机核对时选择窗里标题出现了两次：摘要曾用整篇纯文本，第一行就是标题
    CHECK(summary.find(L"本周进展") == std::wstring::npos);
    CHECK(summary.find(L"完成了 两件事") != std::wstring::npos);

    //DLL 不可用：返回 false，输出置空，调用方自行退回到从源文本粗取
    markdown::SetMarkdownApiOverrideForTest(true, NULL);
    title = L"x";
    summary = L"y";
    CHECK_FALSE(balloonchatwnd::MarkdownCardTextFromSource(U8(L"# 标题"), title, summary));
    CHECK(title.empty());
    CHECK(summary.empty());
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

TEST_CASE("ED20 首屏渲染前发出的 Markdown 文档：都确认入库且首屏已渲染时才重新拉取", "[mdeditor]")
{
    //2026-10-06 实机核对的时序：发送 → 首屏到达并渲染（卡片被清掉）→ 发送确认到达
    SECTION("确认晚于首屏：确认到达时才需要重新拉取")
    {
        balloonchatwnd::MarkdownEarlySendTracker t;
        t.OnSent("a");
        CHECK(t.PendingCount() == 1);
        t.OnFirstPageRendered();
        CHECK_FALSE(t.TakeReloadNeeded());   //还没确认，快照里可能还没有它
        CHECK(t.OnAck("a", true));
        CHECK(t.TakeReloadNeeded());
        CHECK(t.PendingCount() == 0);
        CHECK_FALSE(t.TakeReloadNeeded());   //只拉一次
    }
    SECTION("确认早于首屏：首屏渲染完时需要重新拉取")
    {
        balloonchatwnd::MarkdownEarlySendTracker t;
        t.OnSent("a");
        CHECK(t.OnAck("a", true));
        CHECK_FALSE(t.TakeReloadNeeded());   //首屏还没渲染，卡片仍在列表里
        t.OnFirstPageRendered();
        CHECK(t.TakeReloadNeeded());
    }
    SECTION("两条都要确认后才拉；被拒绝的那条不再等")
    {
        balloonchatwnd::MarkdownEarlySendTracker t;
        t.OnSent("a");
        t.OnSent("b");
        t.OnFirstPageRendered();
        CHECK(t.OnAck("a", true));
        CHECK_FALSE(t.TakeReloadNeeded());
        CHECK(t.OnAck("b", false));
        CHECK(t.TakeReloadNeeded());
    }
    SECTION("全部被拒绝：没有入库的消息，不拉")
    {
        balloonchatwnd::MarkdownEarlySendTracker t;
        t.OnSent("a");
        t.OnFirstPageRendered();
        CHECK(t.OnAck("a", false));
        CHECK_FALSE(t.TakeReloadNeeded());
    }
    SECTION("首屏渲染之后发出的不记录；别的消息的确认不认领")
    {
        balloonchatwnd::MarkdownEarlySendTracker t;
        t.OnFirstPageRendered();
        t.OnSent("a");
        CHECK(t.PendingCount() == 0);
        CHECK_FALSE(t.OnAck("a", true));
        CHECK_FALSE(t.OnAck("x", true));
        CHECK_FALSE(t.TakeReloadNeeded());
        t.OnSent("");
        CHECK(t.PendingCount() == 0);
    }
    SECTION("重新请求最新一页后回到首屏未渲染：其间发出的再次记录")
    {
        balloonchatwnd::MarkdownEarlySendTracker t;
        t.OnFirstPageRendered();
        t.OnReloadRequested();
        t.OnSent("a");
        CHECK(t.PendingCount() == 1);
        CHECK(t.OnAck("a", true));
        CHECK_FALSE(t.TakeReloadNeeded());
        t.OnFirstPageRendered();
        CHECK(t.TakeReloadNeeded());
    }
}

TEST_CASE("ED21 预览与导出的排版样式撑满宽度：文档宽度等于给定的排版宽度", "[mdeditor]")
{
    //排版宽度与字号（像素）：宽度远大于「一行短字」的内容宽度
    const int kLayoutWidth = 600;
    const int kFontPx = 14;
    const flamingomd::MdStyle style = balloonchatwnd::MakePreviewMarkdownStyle(kFontPx, RGB(0x2A, 0x2A, 0x2A));
    CHECK(style.m_cbSize >= flamingomd::kMdStyleV2Size);
    CHECK((style.m_layoutFlags & flamingomd::md_layout_fill_width) != 0);

    //用测试程序里编进来的函数表代替 DLL，实际排一次
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    {
        markdown::CMarkdownDocument doc;
        REQUIRE(doc.Parse(U8(L"# 短\n\n一行"), balloonchatwnd::MarkdownImageLinkPrefix()));
        HDC dc = ::CreateCompatibleDC(NULL);
        REQUIRE(dc != NULL);
        SIZE size = { 0, 0 };
        CHECK(doc.Layout(dc, style, kLayoutWidth, size));
        CHECK(size.cx == kLayoutWidth);
        //对照：不设撑满时收缩到内容宽度（聊天气泡的行为）
        flamingomd::MdStyle shrink = style;
        shrink.m_layoutFlags = 0;
        CHECK(doc.Layout(dc, shrink, kLayoutWidth, size));
        CHECK(size.cx < kLayoutWidth);
        ::DeleteDC(dc);
    }
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

TEST_CASE("ED22 把示例文档按预览窗口的样式导出成 PNG 供人工核对（默认不运行，输出到 MD_SHOT_DIR）", "[.mdpng]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"MD_SHOT_DIR", dir, MAX_PATH) == 0)
    {
        WARN("MD_SHOT_DIR is not set, skipped.");
        return;
    }
    //导出 PNG 与预览窗口用同一套排版样式（MakePreviewMarkdownStyle，撑满宽度）与同一个绘制函数，
    //看这张图就能核对预览窗口里标题细线、代码块、引用、表格是否铺满
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    const std::string source = U8(L"# 本周进展\n\n完成了 **Markdown 编辑器** 与消息卡片，见 [方案](https://example.com)。\n\n"
                                  L"## 导出\n\n- 编辑器左右分栏\n- [x] 导出 md、html、png、pdf、docx\n\n"
                                  L"> 引用的一段话。\n\n| 项目 | 状态 |\n| --- | :---: |\n| 卡片 | 完成 |\n\n"
                                  L"```cpp\nint main() { return 0; } // 语法着色\n```\n\n---\n\n短行");
    const std::wstring path = std::wstring(dir) + L"\\md_preview_fill.png";
    std::wstring error;
    const bool ok = ExportMarkdownToFile(path, source, md_export_png, &error);
    INFO("error: " << U8(error));
    CHECK(ok);
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

namespace {

//CF_HTML 头部里某个字段的数值（十位数字）；没有这个字段时返回 -1
long CfHtmlField(const std::string& data, const char* name)
{
    const std::string key = std::string(name) + ":";
    const size_t at = data.find(key);
    if (at == std::string::npos)
        return -1;
    return std::strtol(data.c_str() + at + key.size(), NULL, 10);
}

//CF_HTML 里 StartFragment 与 EndFragment 之间的字节
std::string CfHtmlFragment(const std::string& data)
{
    const long s = CfHtmlField(data, "StartFragment");
    const long e = CfHtmlField(data, "EndFragment");
    REQUIRE(s > 0);
    REQUIRE(e >= s);
    REQUIRE(static_cast<size_t>(e) <= data.size());
    return data.substr(static_cast<size_t>(s), static_cast<size_t>(e - s));
}

//piece 在文档渲染后文字里的下标：GetText 的换行是 \r\n，渲染后文字里是 \n，扣掉前面的 \r
int FlatIndexOfDoc(const markdown::CMarkdownDocument& doc, const std::wstring& piece)
{
    const std::wstring crlf = doc.GetText(0, doc.GetTextLength());
    const size_t at = crlf.find(piece);
    REQUIRE(at != std::wstring::npos);
    int cr = 0;
    for (size_t i = 0; i < at; ++i)
    {
        if (crlf[i] == L'\r')
            ++cr;
    }
    return static_cast<int>(at) - cr;
}

} // namespace

TEST_CASE("ED23 复制用的 CF_HTML 头部：StartHTML 指向 <html>，片段偏移正好包住片段，EndHTML 等于总长，中文按 UTF-8 "
          "字节计；缺少片段标记时为空", "[mdeditor]")
{
    const std::string html = U8(L"<!DOCTYPE html>\n<html>\n<head><title>T</title></head>\n<body>\n"
                                L"<!--StartFragment--><p>中文 abc</p><!--EndFragment-->\n</body>\n</html>\n");
    const std::string data = WrapCfHtml(html);
    REQUIRE_FALSE(data.empty());
    CHECK(data.compare(0, 12, "Version:0.9\r") == 0);
    const long startHtml = CfHtmlField(data, "StartHTML");
    const long endHtml = CfHtmlField(data, "EndHTML");
    REQUIRE(startHtml > 0);
    CHECK(data.compare(static_cast<size_t>(startHtml), 6, "<html>") == 0);
    CHECK(endHtml == static_cast<long>(data.size()));
    CHECK(CfHtmlFragment(data) == U8(L"<p>中文 abc</p>"));
    //四个偏移都写成十位数字
    CHECK(data.find("StartHTML:0000000") != std::string::npos);
    //文档类型声明去掉了，其后的内容原样保留
    CHECK(data.find("<!DOCTYPE") == std::string::npos);
    CHECK(data.substr(static_cast<size_t>(startHtml)) == html.substr(strlen("<!DOCTYPE html>\n")));

    //缺少片段标记、内容为空：不生成
    CHECK(WrapCfHtml("<html><body><p>x</p></body></html>").empty());
    CHECK(WrapCfHtml(std::string()).empty());
}

TEST_CASE("ED24 预览复制的内容：有选区时纯文字与这一段的 HTML 各自正确；旧版 DLL 只有纯文字；全文带格式", "[mdeditor]")
{
    const std::string source = U8(L"# 标题\n\n**粗体** 文字\n\n- 甲\n- 乙");
    const std::wstring prefix = balloonchatwnd::MarkdownImageLinkPrefix();
    markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    {
        markdown::CMarkdownDocument doc;
        REQUIRE(doc.Parse(source, prefix));
        const int length = doc.GetTextLength();

        //全选：纯文字与以前相同（\r\n 换行），HTML 含各块
        MarkdownClipboardData all;
        REQUIRE(BuildMarkdownSelectionClipboard(doc, 0, length, all));
        CHECK(all.m_text == doc.GetText(0, length));
        CHECK(all.m_text.find(L"\r\n") != std::wstring::npos);
        REQUIRE_FALSE(all.m_cfHtml.empty());
        const std::string allFrag = CfHtmlFragment(all.m_cfHtml);
        CHECK(allFrag.find(U8(L"<h1>标题</h1>")) != std::string::npos);
        CHECK(allFrag.find(U8(L"<strong>粗体</strong>")) != std::string::npos);
        CHECK(allFrag.find("<ul>") != std::string::npos);

        //只选「粗体」两个字：HTML 里只有这两个字（加粗），没有标题与列表
        const int bold = FlatIndexOfDoc(doc, L"粗体");
        MarkdownClipboardData part;
        REQUIRE(BuildMarkdownSelectionClipboard(doc, bold, bold + 2, part));
        CHECK(part.m_text == L"粗体");
        const std::string partFrag = CfHtmlFragment(part.m_cfHtml);
        CHECK(partFrag.find(U8(L"<p><strong>粗体</strong></p>")) != std::string::npos);
        CHECK(partFrag.find("<h1>") == std::string::npos);
        CHECK(partFrag.find("<ul>") == std::string::npos);

        //选区为空：不复制
        MarkdownClipboardData none;
        CHECK_FALSE(BuildMarkdownSelectionClipboard(doc, 2, 2, none));
        CHECK(none.m_text.empty());
        CHECK(none.m_cfHtml.empty());
    }

    //「复制全文（带格式）」：纯文字去掉标记（\r\n 换行），HTML 为全文
    MarkdownClipboardData whole;
    REQUIRE(BuildMarkdownWholeClipboard(source, whole));
    CHECK(whole.m_text.find(L"标题\r\n") == 0);
    CHECK(whole.m_text.find(L"#") == std::wstring::npos);
    CHECK(whole.m_text.find(L"**") == std::wstring::npos);
    const std::string wholeFrag = CfHtmlFragment(whole.m_cfHtml);
    CHECK(wholeFrag.find(U8(L"<h1>标题</h1>")) != std::string::npos);
    CHECK(wholeFrag.find("<ul>") != std::string::npos);

    //函数表只到第三版（旧版 DLL）：只有纯文字，与以前相同
    flamingomd::MdApi v3 = *flamingomd::GetMdApiTable();
    v3.m_cbSize = flamingomd::kMdApiV3Size;
    markdown::SetMarkdownApiOverrideForTest(true, &v3);
    {
        markdown::CMarkdownDocument doc;
        REQUIRE(doc.Parse(source, prefix));
        MarkdownClipboardData old;
        REQUIRE(BuildMarkdownSelectionClipboard(doc, 0, doc.GetTextLength(), old));
        CHECK_FALSE(old.m_text.empty());
        CHECK(old.m_cfHtml.empty());
        MarkdownClipboardData oldWhole;
        REQUIRE(BuildMarkdownWholeClipboard(source, oldWhole));
        CHECK_FALSE(oldWhole.m_text.empty());
        CHECK(oldWhole.m_cfHtml.empty());
    }
    markdown::SetMarkdownApiOverrideForTest(false, NULL);
}

TEST_CASE("ED25 查找条：默认隐藏；显示查找行、再显示替换行、隐藏、隐藏后再显示，各行按当前矩形排好，编辑区让出或收回高度，"
          "默认尺寸与最小尺寸下底部「发送」都排得下；Ctrl+F / Ctrl+H / F3 / Shift+F3 的按键对应", "[mdeditor]")
{
    std::unique_ptr<DuiControl> root = BuildMarkdownEditorBody();
    REQUIRE(root != NULL);
    DuiControl* findRow = root->FindCtrlById(kMdEdIdFindRow);
    DuiControl* replaceRow = root->FindCtrlById(kMdEdIdReplaceRow);
    DuiControl* splitter = root->FindCtrlById(kMdEdIdSplitter);
    REQUIRE(findRow != NULL);
    REQUIRE(replaceRow != NULL);
    REQUIRE(splitter != NULL);
    const UINT ids[] =
    {
        kMdEdIdFindEdit, kMdEdIdFindPrev, kMdEdIdFindNext, kMdEdIdFindMatchCase, kMdEdIdFindCount, kMdEdIdFindClose,
        kMdEdIdReplaceEdit, kMdEdIdReplaceOne, kMdEdIdReplaceAll,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id " << ids[i]);
        CHECK(root->FindCtrlById(ids[i]) != NULL);
    }
    //默认隐藏
    CHECK_FALSE(findRow->IsVisible());
    CHECK_FALSE(replaceRow->IsVisible());

    //依次：只显示查找行、两行都显示、全部隐藏、隐藏之后再显示两行（UI 约定第 5 条要覆盖的路径）
    struct FindBarState
    {
        bool m_find;      //是否显示查找行
        bool m_replace;   //是否显示替换行
    };
    const FindBarState states[] = { { true, false }, { true, true }, { false, false }, { true, true } };
    const int sizes[2][2] =
    {
        { kMdEdWindowWidth, kMdEdWindowHeight },
        { kMdEdMinWidth, kMdEdMinHeight },
    };
    for (int s = 0; s < 2; ++s)
    {
        const int width = sizes[s][0];
        const int contentH = sizes[s][1] - kMdEdTitleBarH;
        ShowMarkdownFindBar(root.get(), false, false);
        layoutfit::MeasureAt(root.get(), kMdEdIdSend, width, contentH);
        const RECT hiddenSplit = splitter->GetRect();
        for (size_t k = 0; k < sizeof(states) / sizeof(states[0]); ++k)
        {
            INFO("window " << width << "x" << sizes[s][1] << ", state " << k);
            ShowMarkdownFindBar(root.get(), states[k].m_find, states[k].m_replace);
            CHECK(findRow->IsVisible() == states[k].m_find);
            CHECK(replaceRow->IsVisible() == (states[k].m_find && states[k].m_replace));

            //不重新整体排版，直接读显隐之后的矩形：各行与编辑区左右对齐、有高度，编辑区在最后一行之下
            const RECT split = splitter->GetRect();
            CHECK(split.bottom == hiddenSplit.bottom);
            if (!states[k].m_find)
            {
                CHECK(split.top == hiddenSplit.top);
                continue;
            }
            const RECT fr = findRow->GetRect();
            CHECK(fr.left == split.left);
            CHECK(fr.right == split.right);
            CHECK(fr.bottom > fr.top);
            CHECK(split.top > fr.bottom);
            CHECK(split.top > hiddenSplit.top);
            CHECK(split.bottom - split.top > 0);
            const RECT close = root->FindCtrlById(kMdEdIdFindClose)->GetRect();
            CHECK(close.right <= fr.right);
            CHECK(close.left > root->FindCtrlById(kMdEdIdFindCount)->GetRect().left);
            if (states[k].m_replace)
            {
                const RECT rr = replaceRow->GetRect();
                CHECK(rr.left == split.left);
                CHECK(rr.right == split.right);
                CHECK(rr.top >= fr.bottom);
                CHECK(split.top > rr.bottom);
                CHECK(root->FindCtrlById(kMdEdIdReplaceAll)->GetRect().right <= rr.right);
            }

            //底部「发送」不被压扁、下方留白与空间充裕时相同
            const layoutfit::FitResult send = layoutfit::MeasureAt(root.get(), kMdEdIdSend, width, contentH);
            REQUIRE(send.m_found);
            CHECK(send.m_bottom <= contentH);
            CHECK(send.m_height == send.m_roomyHeight);
            CHECK(send.m_bottomGap == send.m_roomyBottomGap);
        }
    }

    //按键对应：多按或少按修饰键都不算
    CHECK(MarkdownFindKeyFor('F', true, false, false) == md_find_key_open_find);
    CHECK(MarkdownFindKeyFor('H', true, false, false) == md_find_key_open_replace);
    CHECK(MarkdownFindKeyFor(VK_F3, false, false, false) == md_find_key_next);
    CHECK(MarkdownFindKeyFor(VK_F3, false, true, false) == md_find_key_prev);
    CHECK(MarkdownFindKeyFor('F', true, true, false) == md_find_key_none);
    CHECK(MarkdownFindKeyFor('F', false, false, false) == md_find_key_none);
    CHECK(MarkdownFindKeyFor('H', true, false, true) == md_find_key_none);
    CHECK(MarkdownFindKeyFor(VK_F3, true, false, false) == md_find_key_none);
    CHECK(MarkdownFindKeyFor(VK_F3, false, false, true) == md_find_key_none);
    CHECK(MarkdownFindKeyFor('B', true, false, false) == md_find_key_none);
}
