/**
 *  「发送代码」窗口的单测（UI/BalloonCode/CodeComposeLayout，docs/代码片段方案.md §3.1、§5.5），CodeComposeTests.cpp
 *
 *  [codecompose][layoutfit]
 *    CC1 发送代码窗口按默认尺寸与最小尺寸都排得下：语言下拉框、「行号」勾选框与底部两个按钮保持原高、不越出右边，
 *        编辑框够宽够高、没有压到底部按钮行；行号栏在编辑框左侧
 *    CC3 行号栏隐藏后编辑框占满代码框，再显示时行号栏回到原宽（隐藏后再显示的排版，开发约定文档 UI 约定第 5 条）
 *    CC6 批注栏展开：窗口加宽一个批注栏的宽度时代码区保持原宽；最小尺寸下批注栏保持默认宽度、代码区仍够用；
 *        收起后代码区占满（docs/代码片段方案.md 第 10.5 节）
 *    CC7 「撤销」链接显示后状态文字让出位置，隐藏后状态文字恢复原宽
 *  [codecompose]
 *    CC2 状态文字：编辑框为空时为空串；超限时只给超限说明；自动识别时带语言名与行数，手选时只带行数
 *    CC4 行号栏宽度：两位数以内宽度相同，三位数起随位数加宽
 *    CC5 选区扩展到整行：跨行选区、选区结束在行首、只放了光标、选到末尾
 *    CC8 批注模式（docs/代码片段方案.md 第 11.3 节）：状态文字（还不能发送、已新增或修改 N 条、没给名字、超限）；
 *        在选中的行上添加批注时，有本人的批注改为修改它，只有别人的批注时新建；原消息里没写作者的批注补成原消息发送者。
 *        这几个纯函数由代码查看窗追加批注时使用（第 12 节）；查看窗里别人的卡片没有「修改」、没改时不能发送等
 *        由进程内自测 --compose-selftest 第八部分检查
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
#include "../Common/Code/CodeNotes.h"
#include "../UI/BalloonCode/CodeComposeLayout.h"
#include "../UI/BalloonCode/CodeNotesPane.h"

using namespace ballooncode;

namespace {

//CC1：编辑框至少要有的宽与高（像素）
const int kEditorMinUsableW = 300;
const int kEditorMinUsableH = 150;

//CC2：测试用的行数
const int kTestLines = 12;

//CC6：批注栏展开、窗口没能加宽时（最小尺寸），编辑框至少要有的宽（像素）
const int kEditorMinUsableWithNotesW = 150;

//按整窗尺寸排好一棵发送代码窗口的控件树
std::unique_ptr<balloonwjui::DuiControl> LayoutComposeAt(int windowWidth, int windowHeight)
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildCodeComposeBody();
    root->ForceLayout(RECT{ 0, 0, windowWidth, windowHeight - kCodeComposeTitleBarH });
    return root;
}

//控件宽度
int WidthOf(balloonwjui::DuiControl* c)
{
    return c->GetRect().right - c->GetRect().left;
}

//按给定整窗尺寸检查发送代码窗口的控件树
void CheckComposeFitsAt(int windowWidth, int windowHeight)
{
    const int contentH = windowHeight - kCodeComposeTitleBarH;
    std::unique_ptr<balloonwjui::DuiControl> root = BuildCodeComposeBody();
    REQUIRE(root != nullptr);
    INFO("window " << windowWidth << " x " << windowHeight);

    const UINT fixed[] = { kCodeComposeIdLanguage, kCodeComposeIdLineNumbers, kCodeComposeIdFormat,
                           kCodeComposeIdNotes, kCodeComposeIdCancel, kCodeComposeIdSend };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
    {
        INFO("control id = " << fixed[i]);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), fixed[i], windowWidth, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_height == r.m_roomyHeight);
        //底部按钮贴底：下方留白应与空间充裕时相同（顶部一行的控件下方留白随窗口高度变化，不比较）
        if (fixed[i] == kCodeComposeIdCancel || fixed[i] == kCodeComposeIdSend)
        {
            CHECK(r.m_bottomGap == r.m_roomyBottomGap);
        }
        const RECT rc = root->FindCtrlById(fixed[i])->GetRect();
        CHECK(rc.right <= windowWidth);
        CHECK(rc.right > rc.left);
    }

    //编辑框是弹性区里最下面的控件：要够大，而且不能压到底部按钮行
    const layoutfit::FitResult editor = layoutfit::MeasureAt(root.get(), kCodeComposeIdEditor, windowWidth, contentH);
    REQUIRE(editor.m_found);
    const RECT rcEditor = root->FindCtrlById(kCodeComposeIdEditor)->GetRect();
    const RECT rcSend = root->FindCtrlById(kCodeComposeIdSend)->GetRect();
    CHECK(rcEditor.right - rcEditor.left >= kEditorMinUsableW);
    CHECK(rcEditor.bottom - rcEditor.top >= kEditorMinUsableH);
    CHECK(rcEditor.bottom <= rcSend.top);
    CHECK(rcSend.bottom <= contentH);

    //行号栏在编辑框左侧，与编辑框等高
    const RECT rcGutter = root->FindCtrlById(kCodeComposeIdGutter)->GetRect();
    CHECK(rcGutter.right > rcGutter.left);
    CHECK(rcGutter.right <= rcEditor.left);
    CHECK(rcGutter.top == rcEditor.top);
    CHECK(rcGutter.bottom == rcEditor.bottom);
}

} // anonymous namespace

TEST_CASE("CC1 code compose window fits at the default and the minimum size", "[codecompose][layoutfit]")
{
    CheckComposeFitsAt(kCodeComposeWindowWidth, kCodeComposeWindowHeight);
    CheckComposeFitsAt(kCodeComposeMinWidth, kCodeComposeMinHeight);
    CHECK(kCodeComposeMinWidth <= kCodeComposeWindowWidth);
    CHECK(kCodeComposeMinHeight <= kCodeComposeWindowHeight);
}

TEST_CASE("CC3 hiding and showing the line number gutter", "[codecompose][layoutfit]")
{
    const int contentH = kCodeComposeWindowHeight - kCodeComposeTitleBarH;
    std::unique_ptr<balloonwjui::DuiControl> root = BuildCodeComposeBody();
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, kCodeComposeWindowWidth, contentH });
    balloonwjui::DuiControl* gutter = root->FindCtrlById(kCodeComposeIdGutter);
    balloonwjui::DuiControl* editor = root->FindCtrlById(kCodeComposeIdEditor);
    REQUIRE(gutter != NULL);
    REQUIRE(editor != NULL);
    REQUIRE(gutter->GetParent() != NULL);
    const RECT shownGutter = gutter->GetRect();
    const RECT shownEditor = editor->GetRect();
    const RECT row = gutter->GetParent()->GetRect();

    //隐藏：编辑框从代码框左边开始，占满整行
    gutter->SetVisible(false);
    gutter->GetParent()->ForceLayout(gutter->GetParent()->GetRect());
    CHECK(editor->GetRect().left == row.left);
    CHECK(editor->GetRect().right == shownEditor.right);

    //其间整窗重排一次，再显示：行号栏回到原来的位置与宽度
    root->ForceLayout(RECT{ 0, 0, kCodeComposeWindowWidth, contentH });
    gutter->SetVisible(true);
    gutter->GetParent()->ForceLayout(gutter->GetParent()->GetRect());
    CHECK(gutter->GetRect().left == shownGutter.left);
    CHECK(gutter->GetRect().right == shownGutter.right);
    CHECK(editor->GetRect().left == shownEditor.left);
}

TEST_CASE("CC6 the notes pane beside the code", "[codecompose][layoutfit]")
{
    //收起（窗口刚打开、没有批注时）：代码区占满，记下编辑框的宽度
    std::unique_ptr<balloonwjui::DuiControl> closed = LayoutComposeAt(kCodeComposeWindowWidth, kCodeComposeWindowHeight);
    CodeNotesSplitter* closedSplit = dynamic_cast<CodeNotesSplitter*>(closed->FindCtrlById(kCodeComposeIdSplit));
    REQUIRE(closedSplit != NULL);
    CHECK(closedSplit->IsCollapsed());
    CHECK_FALSE(closed->FindCtrlById(kCodeComposeIdNotesPane)->IsVisible());
    const int closedEditorW = WidthOf(closed->FindCtrlById(kCodeComposeIdEditor));

    //展开且窗口加宽一个批注栏的宽度：编辑框保持原宽，批注栏为默认宽度、不越出窗口
    const int grow = kCodeNotesDefaultWidth + kCodeNotesSplitBarPx;
    std::unique_ptr<balloonwjui::DuiControl> wide = BuildCodeComposeBody();
    CodeNotesSplitter* wideSplit = dynamic_cast<CodeNotesSplitter*>(wide->FindCtrlById(kCodeComposeIdSplit));
    REQUIRE(wideSplit != NULL);
    wideSplit->SetCollapsed(false);
    wide->ForceLayout(RECT{ 0, 0, kCodeComposeWindowWidth + grow, kCodeComposeWindowHeight - kCodeComposeTitleBarH });
    balloonwjui::DuiControl* pane = wide->FindCtrlById(kCodeComposeIdNotesPane);
    REQUIRE(pane != NULL);
    CHECK(pane->IsVisible());
    CHECK(WidthOf(pane) == kCodeNotesDefaultWidth);
    CHECK(pane->GetRect().right <= kCodeComposeWindowWidth + grow);
    CHECK(WidthOf(wide->FindCtrlById(kCodeComposeIdEditor)) == closedEditorW);
    //批注栏与代码框等高，在代码框右侧
    const RECT rcFrame = wide->FindCtrlById(kCodeComposeIdCodeFrame)->GetRect();
    CHECK(pane->GetRect().top == rcFrame.top);
    CHECK(pane->GetRect().bottom == rcFrame.bottom);
    CHECK(pane->GetRect().left >= rcFrame.right);

    //最小尺寸下展开（窗口放不下、没能加宽）：批注栏仍是默认宽度，编辑框变窄但仍够用，底部按钮不受影响
    std::unique_ptr<balloonwjui::DuiControl> narrow = BuildCodeComposeBody();
    dynamic_cast<CodeNotesSplitter*>(narrow->FindCtrlById(kCodeComposeIdSplit))->SetCollapsed(false);
    narrow->ForceLayout(RECT{ 0, 0, kCodeComposeMinWidth, kCodeComposeMinHeight - kCodeComposeTitleBarH });
    CHECK(WidthOf(narrow->FindCtrlById(kCodeComposeIdNotesPane)) == kCodeNotesDefaultWidth);
    CHECK(WidthOf(narrow->FindCtrlById(kCodeComposeIdEditor)) >= kEditorMinUsableWithNotesW);
    CHECK(narrow->FindCtrlById(kCodeComposeIdSend)->GetRect().right <= kCodeComposeMinWidth);

    //再收起：代码区占满，编辑框回到原宽（隐藏后的排版）
    wideSplit->SetCollapsed(true);
    wideSplit->ForceLayout(wideSplit->GetRect());
    CHECK_FALSE(pane->IsVisible());
    CHECK(WidthOf(wide->FindCtrlById(kCodeComposeIdEditor)) == closedEditorW + grow);
}

TEST_CASE("CC7 the undo link takes room from the status text", "[codecompose][layoutfit]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = LayoutComposeAt(kCodeComposeWindowWidth, kCodeComposeWindowHeight);
    balloonwjui::DuiControl* status = root->FindCtrlById(kCodeComposeIdStatus);
    balloonwjui::DuiControl* undo = root->FindCtrlById(kCodeComposeIdUndoNote);
    REQUIRE(status != NULL);
    REQUIRE(undo != NULL);
    CHECK_FALSE(undo->IsVisible());
    const int statusW = WidthOf(status);

    //显示「撤销」：状态文字变窄，链接在它右边、不越出窗口
    undo->SetVisible(true);
    undo->GetParent()->ForceLayout(undo->GetParent()->GetRect());
    CHECK(WidthOf(status) < statusW);
    CHECK(undo->GetRect().left >= status->GetRect().right);
    CHECK(undo->GetRect().right <= kCodeComposeWindowWidth);
    CHECK(WidthOf(undo) > 0);

    //隐藏：状态文字恢复原宽
    undo->SetVisible(false);
    undo->GetParent()->ForceLayout(undo->GetParent()->GetRect());
    CHECK(WidthOf(status) == statusW);
}

TEST_CASE("CC4 gutter width grows with the number of digits", "[codecompose]")
{
    const int fontPx = kCodeComposeFontPx;
    const int kTwoDigits = 99;
    const int kThreeDigits = 100;
    const int kFourDigits = 1000;
    CHECK(CodeComposeGutterWidth(1, fontPx) == CodeComposeGutterWidth(kTwoDigits, fontPx));
    CHECK(CodeComposeGutterWidth(kThreeDigits, fontPx) > CodeComposeGutterWidth(kTwoDigits, fontPx));
    CHECK(CodeComposeGutterWidth(kFourDigits, fontPx) > CodeComposeGutterWidth(kThreeDigits, fontPx));
}

TEST_CASE("CC5 a selection is expanded to whole lines", "[codecompose]")
{
    //三行：「ab\r」占 0~2、「cd\r」占 3~5、「ef」占 6~7
    const std::wstring text = L"ab\rcd\ref";
    long b = -1;
    long e = -1;
    //从第一行中间选到第二行中间：第一、二两行
    ExpandSelectionToLines(text, 1, 4, b, e);
    CHECK(b == 0);
    CHECK(e == 5);
    //选区结束在第三行行首：不含第三行
    ExpandSelectionToLines(text, 3, 6, b, e);
    CHECK(b == 3);
    CHECK(e == 5);
    //只放了光标：光标所在的那一行
    ExpandSelectionToLines(text, 7, 7, b, e);
    CHECK(b == 6);
    CHECK(e == 8);
    //选到末尾
    ExpandSelectionToLines(text, 4, 8, b, e);
    CHECK(b == 3);
    CHECK(e == 8);
}

TEST_CASE("CC2 status text of the code compose window", "[codecompose]")
{
    //编辑框为空：不显示状态
    CHECK(CodeComposeStatusText(true, codesnippet::code_lang_cpp, 0, false).empty());

    //超限：只给超限说明，不带语言与行数
    const std::wstring tooLarge = CodeComposeStatusText(true, codesnippet::code_lang_cpp, kTestLines, true);
    CHECK_FALSE(tooLarge.empty());
    CHECK(tooLarge.find(L"C++") == std::wstring::npos);

    //自动识别：带识别出的语言名与行数
    const std::wstring detected = CodeComposeStatusText(true, codesnippet::code_lang_cpp, kTestLines, false);
    CHECK(detected.find(codesnippet::CodeLanguageDisplayName(codesnippet::code_lang_cpp)) != std::wstring::npos);
    CHECK(detected.find(L"12") != std::wstring::npos);

    //手选：只带行数，语言已显示在下拉框里
    const std::wstring manual = CodeComposeStatusText(false, codesnippet::code_lang_cpp, kTestLines, false);
    CHECK(manual.find(L"12") != std::wstring::npos);
    CHECK(manual.find(codesnippet::CodeLanguageDisplayName(codesnippet::code_lang_cpp)) == std::wstring::npos);

    //纯文本也能给出非空的状态
    CHECK_FALSE(CodeComposeStatusText(true, codesnippet::code_lang_plain, kTestLines, false).empty());
}

TEST_CASE("CC8 annotation mode: status text and where a new note goes", "[codecompose]")
{
    //状态文字：没有新增或修改时提示还不能发送；有时写出条数；没给名字时不写「某某的代码」；超限时只给超限说明
    CHECK(CodeAnnotateStatusText(L"李四", codesnippet::code_lang_cpp, 42, 0, false)
          == L"李四的代码 · C/C++ · 42 行。添加或修改批注后才能发送");
    CHECK(CodeAnnotateStatusText(L"李四", codesnippet::code_lang_cpp, 42, 2, false)
          == L"李四的代码 · C/C++ · 42 行。已新增或修改 2 条批注");
    CHECK(CodeAnnotateStatusText(L"", codesnippet::code_lang_cpp, 3, 1, false) == L"C/C++ · 3 行。已新增或修改 1 条批注");
    const std::wstring large = CodeAnnotateStatusText(L"李四", codesnippet::code_lang_cpp, 42, 2, true);
    CHECK_FALSE(large.empty());
    CHECK(large.find(L"李四") == std::wstring::npos);

    //在选中的行上添加批注：这几行有本人的批注时改为修改它，只有别人的批注时新建
    const long long kSelf = 100;
    const long long kOther = 200;
    const long long kBaseSender = 300;
    std::vector<codesnippet::CodeNote> notes;
    notes.push_back(codesnippet::CodeNote(3, 4, L"other's", kOther));
    notes.push_back(codesnippet::CodeNote(6, 6, L"mine", kSelf));
    notes.push_back(codesnippet::CodeNote(8, 8, L"draft"));
    CHECK(codesnippet::FindEditableCodeNoteInRange(notes, 3, 3, kSelf) == -1);
    CHECK(codesnippet::FindEditableCodeNoteInRange(notes, 4, 6, kSelf) == 1);
    CHECK(codesnippet::FindEditableCodeNoteInRange(notes, 8, 8, kSelf) == 2);
    CHECK(codesnippet::FindEditableCodeNoteInRange(notes, 10, 12, kSelf) == -1);
    //平常写代码时 selfId 为 0：本机写的批注作者都是 0，都能修改；作者不为 0 的不能
    CHECK(codesnippet::FindEditableCodeNoteInRange(notes, 8, 8, 0) == 2);
    CHECK(codesnippet::FindEditableCodeNoteInRange(notes, 6, 6, 0) == -1);

    //原消息里没写作者的批注，作者补成原消息的发送者；其余不变
    const std::vector<codesnippet::CodeNote> filled = codesnippet::FillCodeNoteAuthors(notes, kBaseSender);
    REQUIRE(filled.size() == notes.size());
    CHECK(filled[0].m_authorId == kOther);
    CHECK(filled[1].m_authorId == kSelf);
    CHECK(filled[2].m_authorId == kBaseSender);
}
