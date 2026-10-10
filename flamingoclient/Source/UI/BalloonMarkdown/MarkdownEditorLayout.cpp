/**
 *  MarkdownEditorLayout.cpp
 *
 *  Markdown 编辑器窗口的客户区控件树，说明见 MarkdownEditorLayout.h。Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "MarkdownEditorLayout.h"

#include <algorithm>
#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../BalloonChatWnd/ChatIconButton.h"
#include "../BalloonChatWnd/ChatIcons.h"
#include "../BalloonChatWnd/ChatMarkdownFormatBar.h"
#include "../BalloonChatWnd/ChatMarkdownPreview.h"
#include "../../Common/I18n/I18n.h"
#include "../../Common/I18n/I18nLayout.h"   // GrowFixedWidthToText：「区分大小写」勾选框按译文加宽

using namespace balloonwjui;

namespace balloonmd {

namespace {

// ---- 颜色 ----
const COLORREF kColorCaption = RGB(0x8A, 0x8A, 0x8A);   //状态文字、发送对象说明

// ---- 版式（逻辑像素）----
const int kPadH         = 12;    //客户区左右内边距
const int kPadTop       = 4;     //客户区上内边距（标题栏下方）
const int kPadBottom    = 12;    //客户区下内边距
const int kRootGap      = 8;     //各行之间的间距
const int kToolRowH     = 30;    //顶部一行的高度
const int kToolGap      = 4;     //顶部一行里控件之间的间距
const int kGroupGap     = 12;    //「导出」与格式按钮之间多留的间距
const int kFileButtonMinW = 56;  //「打开」「保存」的最小宽度（译文更长时按文字加宽）
const int kExportButtonMinW = 72; //「导出 ▾」的最小宽度
const int kPreviewToggleMinW = 64; //「预览」开关的最小宽度
const int kStatusW      = 140;   //顶部右侧状态文字的宽度
const int kButtonTextPad = 14;   //按钮文字两侧各留的空白
const int kButtonFontPx = 13;    //按钮文字的字号（量宽度用）
const int kFormatButtonW = 28;   //格式按钮宽
const int kFormatButtonH = 26;   //格式按钮高
const int kFormatGap    = 2;     //格式按钮之间的间距
const int kSplitterBarPx = 6;    //分隔条的粗细
const int kBottomRowH   = 34;    //底部一行的高度
const int kBottomGap    = 8;     //底部一行里控件之间的间距
const int kPickButtonMinW = 80;  //「选择…」的最小宽度
const int kFindRowH     = 30;    //查找条每一行的高度
const int kFindGap      = 6;     //查找条一行里控件之间的间距
const int kFindEditW    = 200;   //查找框、替换框的宽度（两行对齐；最小窗口宽度下查找行也要排得下，ED25）
const int kFindButtonMinW = 64;  //「上一个」「下一个」「替换」「全部替换」的最小宽度
const int kFindCaseW    = 110;   //「区分大小写」勾选框的设计宽度（含勾选框图形与文字；简体中文按默认字体放得下）
const int kFindCheckBoxPx = 24;  //「区分大小写」按译文加宽时，文字宽度之外还要留的宽度（勾选框图形加间距）
const int kFindCloseW   = 28;    //关闭按钮「×」的宽度
const int kSendButtonMinW = 88;  //「发送」的最小宽度

//格式按钮的图标，按 MarkdownEditCommand 的顺序
const int kFormatIcons[balloonchatwnd::kMarkdownEditCommandCount] =
{
    balloonchatwnd::kChatIconMdHeading,
    balloonchatwnd::kChatIconMdBold,
    balloonchatwnd::kChatIconMdItalic,
    balloonchatwnd::kChatIconMdUnderline,
    balloonchatwnd::kChatIconMdStrike,
    balloonchatwnd::kChatIconMdCode,
    balloonchatwnd::kChatIconMdCodeBlock,
    balloonchatwnd::kChatIconMdLink,
    balloonchatwnd::kChatIconMdQuote,
    balloonchatwnd::kChatIconMdBulletList,
    balloonchatwnd::kChatIconMdNumberList,
    balloonchatwnd::kChatIconMdTaskList,
    balloonchatwnd::kChatIconMdTable,
    balloonchatwnd::kChatIconMdRule,
    balloonchatwnd::kChatIconMdAlert,
};

//格式按钮的提示原文，按 MarkdownEditCommand 的顺序；文件作用域在语言确定之前初始化，故只做标记，使用处再翻译
const wchar_t* const kFormatTips[balloonchatwnd::kMarkdownEditCommandCount] =
{
    TR_NOOP(L"标题（Ctrl+1 ~ Ctrl+6）"),
    TR_NOOP(L"加粗（Ctrl+B）"),
    TR_NOOP(L"斜体（Ctrl+I）"),
    TR_NOOP(L"下划线（Ctrl+U）"),
    TR_NOOP(L"删除线（Alt+Shift+5）"),
    TR_NOOP(L"行内代码（Ctrl+Shift+`）"),
    TR_NOOP(L"代码块"),
    TR_NOOP(L"链接（Ctrl+K）"),
    TR_NOOP(L"引用"),
    TR_NOOP(L"无序列表"),
    TR_NOOP(L"有序列表"),
    TR_NOOP(L"任务列表"),
    TR_NOOP(L"表格"),
    TR_NOOP(L"分割线"),
    TR_NOOP(L"警告框"),
};

//按界面字体量一段文字的宽度（像素），给按钮定宽用（译文变长时按「只增不减」加宽，UI 约定第 8 条）
int MeasureTextPx(const std::wstring& text)
{
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
        return 0;
    HFONT font = ::CreateFontW(-kButtonFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                               i18n::UiFontName());
    HGDIOBJ old = ::SelectObject(hdc, font);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    ::SelectObject(hdc, old);
    ::DeleteObject(font);
    ::ReleaseDC(NULL, hdc);
    return sz.cx;
}

//按钮宽度：文字宽加两侧空白，不小于 minW
int ButtonWidth(const std::wstring& text, int minW)
{
    return (std::max)(minW, MeasureTextPx(text) + kButtonTextPad * 2);
}

//造一个按钮
std::unique_ptr<DuiControl> MakeButton(UINT id, const std::wstring& text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text.c_str());
    return std::unique_ptr<DuiControl>(button.release());
}

//造一个灰色单行文字
std::unique_ptr<DuiControl> MakeGrayLabel(UINT id, UINT align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(L"");
    label->SetTextColor(kColorCaption);
    label->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

//造查找条里的单行输入框
std::unique_ptr<DuiControl> MakeFindEdit(UINT id, const std::wstring& placeholder)
{
    std::unique_ptr<DuiEdit> edit(new DuiEdit());
    edit->SetCtrlId(id);
    edit->SetPlaceholder(placeholder.c_str());
    edit->SetBgColor(kMdWindowBackColor);
    return std::unique_ptr<DuiControl>(edit.release());
}

//查找行：查找框、上一个、下一个、区分大小写、匹配个数，最右边是关闭按钮。默认隐藏
std::unique_ptr<DuiControl> BuildFindRow()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetCtrlId(kMdEdIdFindRow);
    row->SetGap(kFindGap);
    row->AddChild(MakeFindEdit(kMdEdIdFindEdit, TR(L"查找")), Hint().Fixed(kFindEditW));
    const std::wstring prevText = TR(L"上一个");
    const std::wstring nextText = TR(L"下一个");
    std::unique_ptr<DuiControl> prev = MakeButton(kMdEdIdFindPrev, prevText, DuiButton::Variant::Default);
    DuiToolTipMgr::Inst().Register(prev.get(), TR(L"上一个（Shift+F3）"));
    row->AddChild(std::move(prev), Hint().Fixed(ButtonWidth(prevText, kFindButtonMinW)));
    std::unique_ptr<DuiControl> next = MakeButton(kMdEdIdFindNext, nextText, DuiButton::Variant::Default);
    DuiToolTipMgr::Inst().Register(next.get(), TR(L"下一个（F3）"));
    row->AddChild(std::move(next), Hint().Fixed(ButtonWidth(nextText, kFindButtonMinW)));
    const std::wstring caseText = TR(L"区分大小写");
    std::unique_ptr<DuiButton> matchCase(new DuiButton());
    matchCase->SetButtonType(DuiButton::StyleCheckbox);
    matchCase->SetCtrlId(kMdEdIdFindMatchCase);
    matchCase->SetText(caseText.c_str());
    matchCase->SetCheck(false, false);
    DuiButton* matchCaseRaw = matchCase.get();
    row->AddChild(std::unique_ptr<DuiControl>(matchCase.release()), Hint().Fixed(kFindCaseW));
    //译文比中文长时按文字宽度加宽（勾选框的文字用默认字体，简体中文界面不变）
    i18n::GrowFixedWidthToText(matchCaseRaw, caseText.c_str(), i18n::DefaultFontPixelHeight(), false, kFindCheckBoxPx);
    //匹配个数占去行内剩余的宽度，关闭按钮因此靠右；窗口变窄时由它让出空间
    row->AddChild(MakeGrayLabel(kMdEdIdFindCount, DT_LEFT), Hint().Weight(1));
    std::unique_ptr<DuiControl> close = MakeButton(kMdEdIdFindClose, L"\x00D7", DuiButton::Variant::Ghost);
    DuiToolTipMgr::Inst().Register(close.get(), TR(L"关闭（Esc）"));
    row->AddChild(std::move(close), Hint().Fixed(kFindCloseW));
    row->SetVisible(false);
    return std::unique_ptr<DuiControl>(row.release());
}

//替换行：替换框、替换、全部替换。默认隐藏
std::unique_ptr<DuiControl> BuildReplaceRow()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetCtrlId(kMdEdIdReplaceRow);
    row->SetGap(kFindGap);
    row->AddChild(MakeFindEdit(kMdEdIdReplaceEdit, TR(L"替换为")), Hint().Fixed(kFindEditW));
    const std::wstring oneText = TR(L"替换");
    const std::wstring allText = TR(L"全部替换");
    row->AddChild(MakeButton(kMdEdIdReplaceOne, oneText, DuiButton::Variant::Default),
                  Hint().Fixed(ButtonWidth(oneText, kFindButtonMinW)));
    row->AddChild(MakeButton(kMdEdIdReplaceAll, allText, DuiButton::Variant::Default),
                  Hint().Fixed(ButtonWidth(allText, kFindButtonMinW)));
    std::unique_ptr<DuiControl> spacer(new DuiControl());
    row->AddChild(std::move(spacer), Hint().Weight(1));
    row->SetVisible(false);
    return std::unique_ptr<DuiControl>(row.release());
}

} // namespace

std::unique_ptr<DuiControl> BuildMarkdownEditorBody()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kMdWindowBackColor);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    //顶部一行：文件操作、格式按钮、状态、预览开关
    std::unique_ptr<DuiHBox> tools(new DuiHBox());
    tools->SetGap(kToolGap);
    const std::wstring openText = TR(L"打开");
    const std::wstring saveText = TR(L"保存");
    const std::wstring exportText = TRF(L"{0} \x25BE", TR(L"导出"));
    tools->AddChild(MakeButton(kMdEdIdOpen, openText, DuiButton::Variant::Ghost),
                    Hint().Fixed(ButtonWidth(openText, kFileButtonMinW)));
    tools->AddChild(MakeButton(kMdEdIdSave, saveText, DuiButton::Variant::Ghost),
                    Hint().Fixed(ButtonWidth(saveText, kFileButtonMinW)));
    tools->AddChild(MakeButton(kMdEdIdExport, exportText, DuiButton::Variant::Ghost),
                    Hint().Fixed(ButtonWidth(exportText, kExportButtonMinW)));
    std::unique_ptr<DuiControl> groupSpacer(new DuiControl());
    tools->AddChild(std::move(groupSpacer), Hint().Fixed(kGroupGap));

    //格式按钮：窗口太窄时从表格按钮往前依次隐藏
    std::unique_ptr<balloonchatwnd::ChatMarkdownFormatBar> formatBar(new balloonchatwnd::ChatMarkdownFormatBar());
    formatBar->SetCtrlId(kMdEdIdFormatBar);
    formatBar->SetGap(kFormatGap);
    for (int i = 0; i < balloonchatwnd::kMarkdownEditCommandCount; ++i)
    {
        std::unique_ptr<balloonchatwnd::ChatIconButton> btn(new balloonchatwnd::ChatIconButton());
        btn->SetCtrlId(kMdEdIdFormatFirst + i);
        btn->SetIconKind(kFormatIcons[i]);
        balloonchatwnd::ChatIconButton* raw = btn.get();
        formatBar->AddChild(std::unique_ptr<DuiControl>(btn.release()),
                            Hint().Fixed(kFormatButtonW, kFormatButtonH).AlignC(DuiLayout::AlignCenter));
        DuiToolTipMgr::Inst().Register(raw, TR(kFormatTips[i]));
    }
    formatBar->SetCollapsibleCount(balloonchatwnd::kMarkdownEditCommandCount);
    tools->AddChild(std::unique_ptr<DuiControl>(formatBar.release()), Hint().Weight(1));
    tools->AddChild(MakeGrayLabel(kMdEdIdStatus, DT_RIGHT), Hint().Fixed(kStatusW));
    const std::wstring previewText = TR(L"预览");
    tools->AddChild(MakeButton(kMdEdIdPreviewToggle, previewText, DuiButton::Variant::Outlined),
                    Hint().Fixed(ButtonWidth(previewText, kPreviewToggleMinW)));
    root->AddChild(std::unique_ptr<DuiControl>(tools.release()), Hint().Fixed(kToolRowH));

    //查找条：默认隐藏，竖直布局排版时跳过隐藏的行
    root->AddChild(BuildFindRow(), Hint().Fixed(kFindRowH));
    root->AddChild(BuildReplaceRow(), Hint().Fixed(kFindRowH));

    //中间：左边编辑框、右边预览
    std::unique_ptr<DuiSplitter> split(new DuiSplitter());
    split->SetCtrlId(kMdEdIdSplitter);
    split->SetOrientation(DuiSplitter::Vertical);
    split->SetBarThickness(kSplitterBarPx);
    split->SetMinSizes(kMdEdPaneMinPx, kMdEdPaneMinPx);
    split->SetSplitFraction(kMdEdInitialSplitFraction);

    //编辑框：纯文字（粘贴时只取文字，须在写入文字之前切换）、多行、自动换行
    std::unique_ptr<DuiRichEdit> editor(new DuiRichEdit());
    editor->SetCtrlId(kMdEdIdEditor);
    editor->SetPlainTextMode(true);
    editor->SetMultiLine(true);
    editor->SetWordWrap(true);
    editor->SetShowBorder(true);
    editor->SetVScrollPolicy(DuiRichEdit::kScrollBarAuto);
    editor->SetMaxLength(0);
    editor->SetPlaceholder(TR(L"在这里输入 Markdown，右边实时预览"));
    split->SetPane(0, std::unique_ptr<DuiControl>(editor.release()));

    //预览：滚动视图里放预览控件，预览控件按排版结果报告高度
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetCtrlId(kMdEdIdPreviewScroll);
    std::unique_ptr<balloonchatwnd::ChatMarkdownPreview> preview(new balloonchatwnd::ChatMarkdownPreview());
    preview->SetCtrlId(kMdEdIdPreview);
    scroll->SetContent(std::unique_ptr<DuiControl>(preview.release()));
    scroll->SetAutoContentHeight(true);
    split->SetPane(1, std::unique_ptr<DuiControl>(scroll.release()));
    root->AddChild(std::unique_ptr<DuiControl>(split.release()), Hint().Weight(1));

    //底部：发送对象、选择、发送
    std::unique_ptr<DuiHBox> bottom(new DuiHBox());
    bottom->SetGap(kBottomGap);
    bottom->AddChild(MakeGrayLabel(kMdEdIdTargets, DT_LEFT), Hint().Weight(1));
    const std::wstring pickText = TR(L"选择…");
    const std::wstring sendText = TR(L"发送");
    bottom->AddChild(MakeButton(kMdEdIdPickTargets, pickText, DuiButton::Variant::Default),
                     Hint().Fixed(ButtonWidth(pickText, kPickButtonMinW)));
    bottom->AddChild(MakeButton(kMdEdIdSend, sendText, DuiButton::Variant::Primary),
                     Hint().Fixed(ButtonWidth(sendText, kSendButtonMinW)));
    root->AddChild(std::unique_ptr<DuiControl>(bottom.release()), Hint().Fixed(kBottomRowH));

    return std::unique_ptr<DuiControl>(root.release());
}

bool ResetMarkdownEditorSplit(DuiControl* root)
{
    if (root == NULL)
        return false;
    DuiSplitter* split = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kMdEdIdSplitter));
    if (split == NULL)
        return false;
    //分隔条已排过版时，SetSplitFraction 立即按当前矩形换算成像素并重新排版两侧
    split->SetSplitFraction(kMdEdInitialSplitFraction);
    return true;
}

void ShowMarkdownFindBar(DuiControl* root, bool find, bool replace)
{
    if (root == NULL)
        return;
    DuiControl* findRow = root->FindCtrlById(kMdEdIdFindRow);
    DuiControl* replaceRow = root->FindCtrlById(kMdEdIdReplaceRow);
    if (findRow == NULL || replaceRow == NULL)
        return;
    findRow->SetVisible(find);
    replaceRow->SetVisible(find && replace);
    //SetVisible 只置标志位，不重新排版：两行都是根容器的直接子控件，对根容器 ForceLayout 即可（UI 约定第 5 条）
    const RECT rc = root->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
        root->ForceLayout(rc);
}

std::unique_ptr<DuiControl> BuildMarkdownPreviewBody()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kMdWindowBackColor);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    //顶部一行：发送者与时间、复制、导出、在编辑器中打开
    std::unique_ptr<DuiHBox> tools(new DuiHBox());
    tools->SetGap(kToolGap);
    tools->AddChild(MakeGrayLabel(kMdPvIdMeta, DT_LEFT), Hint().Weight(1));
    const std::wstring copyText = TRF(L"{0} \x25BE", TR(L"复制"));
    const std::wstring exportText = TRF(L"{0} \x25BE", TR(L"导出"));
    const std::wstring editText = TR(L"在编辑器中打开");
    tools->AddChild(MakeButton(kMdPvIdCopy, copyText, DuiButton::Variant::Ghost),
                    Hint().Fixed(ButtonWidth(copyText, kExportButtonMinW)));
    tools->AddChild(MakeButton(kMdPvIdExport, exportText, DuiButton::Variant::Ghost),
                    Hint().Fixed(ButtonWidth(exportText, kExportButtonMinW)));
    tools->AddChild(MakeButton(kMdPvIdOpenEditor, editText, DuiButton::Variant::Outlined),
                    Hint().Fixed(ButtonWidth(editText, kPickButtonMinW)));
    root->AddChild(std::unique_ptr<DuiControl>(tools.release()), Hint().Fixed(kToolRowH));

    //正文：滚动视图里放预览控件
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetCtrlId(kMdPvIdScroll);
    std::unique_ptr<balloonchatwnd::ChatMarkdownPreview> preview(new balloonchatwnd::ChatMarkdownPreview());
    preview->SetCtrlId(kMdPvIdPreview);
    scroll->SetContent(std::unique_ptr<DuiControl>(preview.release()));
    scroll->SetAutoContentHeight(true);
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(root.release());
}

const wchar_t* MarkdownFormatTipSource(int command)
{
    if (command < 0 || command >= balloonchatwnd::kMarkdownEditCommandCount)
        return L"";
    return kFormatTips[command];
}

} // namespace balloonmd
