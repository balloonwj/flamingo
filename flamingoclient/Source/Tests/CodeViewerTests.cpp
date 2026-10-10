/**
 *  代码查看窗的单测（UI/BalloonCode/CodeViewerModel、CodeViewerLayout，docs/代码片段方案.md §3.3、§5.4），CodeViewerTests.cpp
 *
 *  [codeview]
 *    CV1 RTF：颜色表每种着色种类一项；全文统一的制表位；Tab 写成 \tab；反斜杠与花括号转义；非 ASCII 字符写成
 *        \uN?（超过 0x7FFF 的取负数）；中文改用第二种字体；着色区间按颜色表下标切换；字体名含非 ASCII 字符时退回默认
 *    CV2 行起点：每个逻辑行在 RichEdit 里的起始字符位置，以及由字符位置反查逻辑行
 *    CV3 另存为：各语言的扩展名与默认文件名，越界语言按纯文本
 *    CV4 窗口标题含语言名与行数
 *  [codeview][layoutfit]
 *    CV5 查看窗按默认尺寸与最小尺寸都排得下：工具条两个按钮保持原高、不越出右边，正文编辑框够宽够高，行号栏保持给定宽度
 *    CV6 有批注时：显示「批注 (N)」、批注栏展开在正文右侧，窗口加宽时正文保持原宽；最小尺寸下正文仍够用
 *    CV7 「批注 (N)」与「添加批注」（docs/代码片段方案.md 第 11.3 节）都显示时，默认尺寸与最小尺寸下工具条的控件依次排开、
 *        互不重叠、不越出窗口，最小尺寸下各控件与默认尺寸下一样宽（没有被压窄）
 *    CV8 底部一行（状态文字与「发送」，docs/代码片段方案.md 第 12 节）：显示时正文让出高度、「发送」保持原高且不越出窗口，
 *        隐藏后正文恢复原高（隐藏后再显示的排版，开发约定文档 UI 约定第 5 条）；默认尺寸与最小尺寸都检查
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "LayoutFitCheck.h"
#include "../Common/Code/CodeLexer.h"
#include "../UI/BalloonChatWnd/ChatCodeBlock.h"
#include "../UI/BalloonChatWnd/ChatCodeMessage.h"
#include "../UI/BalloonCode/CodeViewerModel.h"
#include "../UI/BalloonCode/CodeViewerLayout.h"
#include "../UI/BalloonCode/CodeNotesPane.h"

using namespace ballooncode;

namespace {

//text 里 needle 出现的次数
size_t CountOf(const std::string& text, const std::string& needle)
{
    size_t n = 0;
    size_t from = 0;
    while (true)
    {
        const size_t pos = text.find(needle, from);
        if (pos == std::string::npos)
            break;
        ++n;
        from = pos + needle.size();
    }
    return n;
}

//CV5：正文编辑框至少要有的宽与高（像素）
const int kEditMinUsableW = 300;
const int kEditMinUsableH = 200;

//CV5：测试用的行号栏宽度（像素）
const int kTestGutterW = 40;

//CV6：批注栏展开、窗口没能加宽时（最小尺寸），正文编辑框至少要有的宽（像素）
const int kEditMinUsableWithNotesW = 200;

//按给定整窗尺寸检查查看窗的控件树
void CheckViewerFitsAt(int windowWidth, int windowHeight)
{
    const int contentH = windowHeight - kCodeViewerTitleBarH;
    std::unique_ptr<balloonwjui::DuiControl> root = BuildCodeViewerBody(kTestGutterW);
    REQUIRE(root != nullptr);
    INFO("window " << windowWidth << " x " << windowHeight);

    const UINT buttons[] = { kCodeViewerIdCopy, kCodeViewerIdSave, kCodeViewerIdLanguage, kCodeViewerIdWrap };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        INFO("control id = " << buttons[i]);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), buttons[i], windowWidth, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_height == r.m_roomyHeight);
        const RECT rc = root->FindCtrlById(buttons[i])->GetRect();
        CHECK(rc.right <= windowWidth);
        CHECK(rc.right > rc.left);
    }

    const layoutfit::FitResult edit = layoutfit::MeasureAt(root.get(), kCodeViewerIdEdit, windowWidth, contentH);
    REQUIRE(edit.m_found);
    const RECT rcEdit = root->FindCtrlById(kCodeViewerIdEdit)->GetRect();
    CHECK(rcEdit.right - rcEdit.left >= kEditMinUsableW);
    CHECK(rcEdit.bottom - rcEdit.top >= kEditMinUsableH);
    CHECK(rcEdit.bottom <= contentH);

    const RECT rcGutter = root->FindCtrlById(kCodeViewerIdGutter)->GetRect();
    CHECK(rcGutter.right - rcGutter.left == kTestGutterW);
    CHECK(rcGutter.right <= rcEdit.left);
}

} // anonymous namespace

TEST_CASE("CV1 RTF carries colors, tab stops, escapes and the fallback font", "[codeview]")
{
    //「int a;\n\tb = '{'; // \中！」：类型、Tab、花括号、反斜杠、中文与全角感叹号
    const std::wstring code = L"int a;\n\tb = '{'; // \\\x4E2D\xFF01";
    const balloonchatwnd::CodeBlockContent content = balloonchatwnd::PrepareCodeBlockContent(code, codesnippet::code_lang_cpp);
    const int kTabTwips = 440;
    const std::string rtf = BuildCodeRtf(content, kCodeViewerFontHalfPoints, kTabTwips, L"Microsoft YaHei");

    CHECK(rtf.compare(0, 6, "{\\rtf1") == 0);
    CHECK(rtf[rtf.size() - 1] == '}');
    //颜色表：「自动」之外每种着色种类一项
    const size_t tableBegin = rtf.find("{\\colortbl;");
    REQUIRE(tableBegin != std::string::npos);
    const size_t tableEnd = rtf.find('}', tableBegin);
    CHECK(CountOf(rtf.substr(tableBegin, tableEnd - tableBegin), "\\red") == (size_t)codesnippet::code_token_kind_count);
    //制表位：第一个是 440 缇，第二个是 880 缇
    CHECK(rtf.find("\\tx440\\tx880") != std::string::npos);
    CHECK(rtf.find("\\fs20") != std::string::npos);
    //Tab、换行、转义
    CHECK(CountOf(rtf, "\\tab ") == 1);
    //段落结束写成 \par 加换行（开头的 \pard 是段落设置，不算）
    CHECK(CountOf(rtf, "\\par\r\n") == 1);
    CHECK(rtf.find("'\\{'") != std::string::npos);
    CHECK(rtf.find("// \\\\") != std::string::npos);
    //非 ASCII：「中」= 20013，全角感叹号 0xFF01 超过 0x7FFF，取 65281 - 65536 = -255
    CHECK(rtf.find("\\u20013?") != std::string::npos);
    CHECK(rtf.find("\\u-255?") != std::string::npos);
    //中文前切到第二种字体
    CHECK(rtf.find("\\f1 \\u20013?") != std::string::npos);
    //类型 int 用颜色表里 code_token_type 那一项（下标 = 种类 + 1）
    const std::string typeColor = "\\cf" + std::to_string(codesnippet::code_token_type + 1) + " int";
    CHECK(rtf.find(typeColor) != std::string::npos);
    //整个 RTF 只含 ASCII 字符
    bool asciiOnly = true;
    for (size_t i = 0; i < rtf.size(); ++i)
    {
        if ((unsigned char)rtf[i] >= 0x80)
            asciiOnly = false;
    }
    CHECK(asciiOnly);

    //字体名含非 ASCII 字符：退回默认字体
    const std::string rtf2 = BuildCodeRtf(content, 0, 0, L"\x5FAE\x8F6F\x96C5\x9ED1");
    CHECK(rtf2.find("Microsoft YaHei;") != std::string::npos);
    CHECK(rtf2.find("\\tx") == std::string::npos);
    CHECK(rtf2.find("\\fs20") != std::string::npos);
}

TEST_CASE("CV2 line start positions and reverse lookup", "[codeview]")
{
    std::vector<long> starts;
    BuildLineStartPositions(L"a\nbc\n\nd", starts);
    REQUIRE(starts.size() == 4);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 2);
    CHECK(starts[2] == 5);
    CHECK(starts[3] == 6);
    CHECK(LineOfPosition(starts, 0) == 0);
    CHECK(LineOfPosition(starts, 1) == 0);
    CHECK(LineOfPosition(starts, 2) == 1);
    CHECK(LineOfPosition(starts, 4) == 1);
    CHECK(LineOfPosition(starts, 5) == 2);
    CHECK(LineOfPosition(starts, 6) == 3);
    CHECK(LineOfPosition(starts, 1000) == 3);
    CHECK(LineOfPosition(std::vector<long>(), 5) == 0);

    BuildLineStartPositions(L"", starts);
    REQUIRE(starts.size() == 1);
    CHECK(starts[0] == 0);
}

TEST_CASE("CV3 file extensions and default save names", "[codeview]")
{
    CHECK(std::wstring(codesnippet::CodeLanguageFileExtension(codesnippet::code_lang_cpp)) == L"cpp");
    CHECK(std::wstring(codesnippet::CodeLanguageFileExtension(codesnippet::code_lang_python)) == L"py");
    CHECK(std::wstring(codesnippet::CodeLanguageFileExtension(codesnippet::code_lang_plain)) == L"txt");
    CHECK(std::wstring(codesnippet::CodeLanguageFileExtension(-1)) == L"txt");
    CHECK(std::wstring(codesnippet::CodeLanguageFileExtension(codesnippet::code_lang_count)) == L"txt");
    for (int lang = 0; lang < codesnippet::code_lang_count; ++lang)
    {
        const std::wstring name = balloonchatwnd::CodeSaveFileName(lang);
        const std::wstring suffix = std::wstring(L".") + codesnippet::CodeLanguageFileExtension(lang);
        REQUIRE(name.size() > suffix.size());
        CHECK(name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0);
    }
}

TEST_CASE("CV4 window title carries the language and the line count", "[codeview]")
{
    const std::wstring title = CodeViewerTitle(codesnippet::code_lang_rust, 42);
    CHECK(title.find(L"Rust") != std::wstring::npos);
    CHECK(title.find(L"42") != std::wstring::npos);
    CHECK_FALSE(CodeViewerTitle(codesnippet::code_lang_plain, 1).empty());
}

TEST_CASE("CV5 code viewer fits at the default and the minimum size", "[codeview][layoutfit]")
{
    CheckViewerFitsAt(kCodeViewerWindowWidth, kCodeViewerWindowHeight);
    CheckViewerFitsAt(kCodeViewerMinWidth, kCodeViewerMinHeight);
    CHECK(kCodeViewerMinWidth <= kCodeViewerWindowWidth);
    CHECK(kCodeViewerMinHeight <= kCodeViewerWindowHeight);
}

TEST_CASE("CV6 code viewer with notes: the pane opens beside the code", "[codeview][layoutfit]")
{
    const int contentH = kCodeViewerWindowHeight - kCodeViewerTitleBarH;
    //没有批注（刚搭好的样子）：「批注」按钮隐藏，批注栏收起，正文占满
    std::unique_ptr<balloonwjui::DuiControl> plain = BuildCodeViewerBody(kTestGutterW);
    plain->ForceLayout(RECT{ 0, 0, kCodeViewerWindowWidth, contentH });
    CHECK_FALSE(plain->FindCtrlById(kCodeViewerIdNotes)->IsVisible());
    CHECK_FALSE(plain->FindCtrlById(kCodeViewerIdNotesPane)->IsVisible());
    const RECT plainEdit = plain->FindCtrlById(kCodeViewerIdEdit)->GetRect();

    //有批注：显示「批注 (N)」、展开批注栏，窗口加宽一个批注栏的宽度时正文保持原宽
    const int grow = ballooncode::kCodeNotesDefaultWidth + ballooncode::kCodeNotesSplitBarPx;
    std::unique_ptr<balloonwjui::DuiControl> root = BuildCodeViewerBody(kTestGutterW);
    balloonwjui::DuiControl* notes = root->FindCtrlById(kCodeViewerIdNotes);
    ballooncode::CodeNotesSplitter* split =
        dynamic_cast<ballooncode::CodeNotesSplitter*>(root->FindCtrlById(kCodeViewerIdSplit));
    REQUIRE(notes != NULL);
    REQUIRE(split != NULL);
    notes->SetVisible(true);
    split->SetCollapsed(false);
    root->ForceLayout(RECT{ 0, 0, kCodeViewerWindowWidth + grow, contentH });
    const RECT rcEdit = root->FindCtrlById(kCodeViewerIdEdit)->GetRect();
    const RECT rcPane = root->FindCtrlById(kCodeViewerIdNotesPane)->GetRect();
    CHECK(rcEdit.right - rcEdit.left == plainEdit.right - plainEdit.left);
    CHECK(rcPane.right - rcPane.left == ballooncode::kCodeNotesDefaultWidth);
    CHECK(rcPane.left >= rcEdit.right);
    CHECK(rcPane.right <= kCodeViewerWindowWidth + grow);
    CHECK(rcPane.bottom == rcEdit.bottom);
    //「批注 (N)」在「复制全部」左边，不重叠
    const RECT rcNotes = notes->GetRect();
    const RECT rcCopy = root->FindCtrlById(kCodeViewerIdCopy)->GetRect();
    CHECK(rcNotes.right > rcNotes.left);
    CHECK(rcNotes.right <= rcCopy.left);

    //最小尺寸下展开：正文仍够用
    split->SetCollapsed(false);
    root->ForceLayout(RECT{ 0, 0, kCodeViewerMinWidth, kCodeViewerMinHeight - kCodeViewerTitleBarH });
    const RECT narrowEdit = root->FindCtrlById(kCodeViewerIdEdit)->GetRect();
    CHECK(narrowEdit.right - narrowEdit.left >= kEditMinUsableWithNotesW);
}

TEST_CASE("CV7 the toolbar fits with the notes and add-note buttons", "[codeview][layoutfit]")
{
    //工具条从左到右的控件
    const UINT order[] = { kCodeViewerIdLanguageCaption, kCodeViewerIdLanguage, kCodeViewerIdWrap, kCodeViewerIdNotes,
                           kCodeViewerIdAnnotate, kCodeViewerIdCopy, kCodeViewerIdSave };
    const size_t count = sizeof(order) / sizeof(order[0]);
    //先按默认尺寸、再按最小尺寸排版
    const int widths[] = { kCodeViewerWindowWidth, kCodeViewerMinWidth };
    const int heights[] = { kCodeViewerWindowHeight, kCodeViewerMinHeight };
    std::vector<int> roomyWidths;
    for (size_t s = 0; s < sizeof(widths) / sizeof(widths[0]); ++s)
    {
        std::unique_ptr<balloonwjui::DuiControl> root = BuildCodeViewerBody(kTestGutterW);
        REQUIRE(root != nullptr);
        root->FindCtrlById(kCodeViewerIdNotes)->SetVisible(true);
        root->FindCtrlById(kCodeViewerIdAnnotate)->SetVisible(true);
        const RECT rcRoot = { 0, 0, widths[s], heights[s] - kCodeViewerTitleBarH };
        root->ForceLayout(rcRoot);
        int prevRight = 0;
        for (size_t i = 0; i < count; ++i)
        {
            INFO("window width " << widths[s] << ", control id " << order[i]);
            const RECT rc = root->FindCtrlById(order[i])->GetRect();
            CHECK(rc.left >= prevRight);
            CHECK(rc.right <= widths[s]);
            CHECK(rc.right > rc.left);
            prevRight = rc.right;
            if (s == 0)
                roomyWidths.push_back(rc.right - rc.left);
            else
                CHECK(rc.right - rc.left == roomyWidths[i]);
        }
    }
}

TEST_CASE("CV8 the bottom row with the status and the send button", "[codeview][layoutfit]")
{
    const int widths[] = { kCodeViewerWindowWidth, kCodeViewerMinWidth };
    const int heights[] = { kCodeViewerWindowHeight, kCodeViewerMinHeight };
    for (size_t s = 0; s < sizeof(widths) / sizeof(widths[0]); ++s)
    {
        INFO("window " << widths[s] << " x " << heights[s]);
        std::unique_ptr<balloonwjui::DuiControl> root = BuildCodeViewerBody(kTestGutterW);
        REQUIRE(root != nullptr);
        const RECT rcRoot = { 0, 0, widths[s], heights[s] - kCodeViewerTitleBarH };
        balloonwjui::DuiControl* bar = root->FindCtrlById(kCodeViewerIdAnnotateBar);
        balloonwjui::DuiControl* split = root->FindCtrlById(kCodeViewerIdSplit);
        balloonwjui::DuiControl* send = root->FindCtrlById(kCodeViewerIdAnnotateSend);
        REQUIRE(bar != NULL);
        REQUIRE(split != NULL);
        REQUIRE(send != NULL);
        //默认隐藏：正文占满
        CHECK_FALSE(bar->IsVisible());
        root->ForceLayout(rcRoot);
        const int fullBottom = split->GetRect().bottom;

        //显示：正文让出高度，「发送」在正文下方、保持原高、不越出窗口
        bar->SetVisible(true);
        root->ForceLayout(rcRoot);
        const RECT rcSplit = split->GetRect();
        const RECT rcSend = send->GetRect();
        CHECK(rcSplit.bottom < fullBottom);
        CHECK(rcSend.top >= rcSplit.bottom);
        CHECK(rcSend.bottom <= rcRoot.bottom);
        CHECK(rcSend.right <= widths[s]);
        CHECK(rcSend.right > rcSend.left);
        const layoutfit::FitResult fit = layoutfit::MeasureAt(root.get(), kCodeViewerIdAnnotateSend, widths[s],
                                                              heights[s] - kCodeViewerTitleBarH);
        REQUIRE(fit.m_found);
        CHECK(fit.m_height == fit.m_roomyHeight);

        //再隐藏：正文恢复原高
        bar->SetVisible(false);
        root->ForceLayout(rcRoot);
        CHECK(split->GetRect().bottom == fullBottom);
    }
}
