/**
 *  屏幕共享的标注工具条，说明见 ShareAnnotationBar.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareAnnotationBar.h"

#include <commdlg.h>   // ChooseColor（自定义颜色，第 19 期）
#include <memory>
#include "ShareAnnotationSession.h"
#include "ShareMessages.h"
#include "ShareMouseEffects.h"   // RenderMouseColorSwatch（颜色菜单项与颜色按钮的色块）
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/ImageEx.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../Source/Common/I18n/I18n.h"         // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText：译文较长时加宽按钮
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // DuiToolTipMgr：只有图标的按钮的悬停提示
#include "ShareAnnotationMenuIcons.h"   // 分组按钮与菜单项的效果预览图标（2026-10-05）
#include "ShareBarButtonArt.h"          // MakeShareBarIconBitmap：「鼠标」的箭头与「固定」的图钉（2026-10-07）

#pragma comment(lib, "comdlg32.lib")

namespace screenshare {

namespace {

//工具条尺寸（逻辑像素）：标题栏高（只显示「标注」两个字）、内容横条高
const int kBarTitleH = 18;
const int kBarContentH = 46;
//分组按钮的设计宽度（简体中文界面下按组里最长的文字定），按 AnnotBarGroup 的取值排：画笔、线条、箭头、形状、标号、文字、
//橡皮、颜色、粗细、线型、填充、选择、拓扑
const int kGroupBtnW[kAnnotGroupCount] = { 82, 66, 98, 104, 62, 62, 92, 76, 58, 58, 58, 62, 92 };
//撤销 / 清空、权限、退出按钮宽
const int kSmallBtnW = 56;
const int kPermBtnW = 112;
const int kExitBtnW = 80;
//「保存为图片」按钮的宽（第 23 期）
const int kSaveBtnW = 96;
//按钮上下留白、横条左右内边距、控件间距、分组之间额外的间距
const int kBtnVMargin = 8;
const int kRowPad = 10;
const int kRowGap = 6;
const int kGroupGap = 12;
//第几个分组之前多留一点距离：颜色组（工具组与样式组之间）
const int kStyleGroupFirst = annot_group_color;
//距参照矩形底边的距离：优先放在参照矩形下方，放不下时放进参照矩形底部
const int kBottomMargin = 24;
const int kBelowGap = 8;
//底色与标题文字色（与控制条一致）
const COLORREF kBarBgColor = RGB(44, 48, 56);
const COLORREF kBarTextColor = RGB(255, 255, 255);
//纯色背景图的边长（像素）
const int kSolidBitmapSize = 8;
//32 位 DIB 的位深与每像素字节数
const WORD kBitsPerPixel32 = 32;
const int kBytesPerPixel = 4;
const BYTE kOpaque = 255;
//分组按钮前面图标与文字之间的距离（像素）；按钮因此加宽「图标边长 + 该距离」
const int kIconTextGap = 4;
//放不下工作区时的分组按钮宽（只有图标与下拉符号）
const int kCompactGroupW = 46;
//放不下工作区时分组按钮上的文字：只有下拉符号
const wchar_t* const kCompactDropMark = L"\u25BE";
//「收起」与「鼠标」按钮宽（只有图标）
const int kCollapseBtnW = 32;
const int kPointerBtnW = 32;
//「选择」按钮宽（2026-10-08）：只有图标，与「鼠标」一样宽
const int kSelectBtnW = 32;
//收起后的小把手（2026-10-07）：图标按钮宽与左右留白（像素）
const int kHandleBtnW = 36;
const int kHandlePadH = 8;
//「鼠标」「固定」图标的颜色：白底的默认按钮上为深灰，主色按钮上为白色
const COLORREF kIconDarkColor = RGB(60, 64, 72);
const COLORREF kIconLightColor = RGB(255, 255, 255);

//纯色位图（不透明）
HBITMAP makeSolidBitmap(COLORREF c, int side)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = side;
    bi.bmiHeader.biHeight = -side;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* p = static_cast<BYTE*>(bits);
    for (int i = 0; i < side * side; ++i)
    {
        p[i * kBytesPerPixel + 0] = GetBValue(c);
        p[i * kBytesPerPixel + 1] = GetGValue(c);
        p[i * kBytesPerPixel + 2] = GetRValue(c);
        p[i * kBytesPerPixel + 3] = kOpaque;
    }
    return bmp;
}

//色块位图（透明底，四周留边、对勾表示当前色），颜色按钮前面用
HBITMAP makeSwatchBitmap(uint32_t rgb)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kShareMouseSwatchSize;
    bi.bmiHeader.biHeight = -kShareMouseSwatchSize;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    RenderMouseColorSwatch(static_cast<uint8_t*>(bits), kShareMouseSwatchSize, kShareMouseSwatchSize * kBytesPerPixel,
                           RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF), false);
    return bmp;
}

//菜单里颜色项的图标（当前色带对勾）；建不出来时返回 NULL
CImageEx* makeSwatchIcon(uint32_t rgb, bool checked)
{
    std::unique_ptr<CImageEx> img(new CImageEx());
    //高度取负值：自上而下的位图，GetBits 指向第一行、GetPitch 为正
    if (!img->Create(kShareMouseSwatchSize, -kShareMouseSwatchSize, kBitsPerPixel32, CImage::createAlphaChannel))
        return NULL;
    RenderMouseColorSwatch(static_cast<uint8_t*>(img->GetBits()), kShareMouseSwatchSize, img->GetPitch(),
                           RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF), checked);
    return img.release();
}

//画一条命令的效果图标（预乘透明度的 32 位 DIB）；没有图标或建不出来时返回 NULL。调用方负责 DeleteObject
HBITMAP makeChoiceBitmap(const AnnotMenuChoice& choice, const AnnotBarState& state, int size)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    if (!RenderAnnotChoiceIcon(choice, state, false, size, static_cast<uint8_t*>(bits), size * kBytesPerPixel))
    {
        ::DeleteObject(bmp);
        return NULL;
    }
    return bmp;
}

//「收起」按钮的双左箭头图标；调用方负责 DeleteObject
HBITMAP makeCollapseBitmap(int size)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    RenderAnnotCollapseGlyph(size, static_cast<uint8_t*>(bits), size * kBytesPerPixel);
    return bmp;
}

//菜单项的效果图标（当前选中的画成浅蓝底）；没有图标时返回 NULL
CImageEx* makeChoiceIcon(const AnnotMenuItem& item, const AnnotBarState& state)
{
    AnnotMenuChoice c;
    if (!AnnotMenuItemIconChoice(item, c))
        return NULL;
    std::unique_ptr<CImageEx> img(new CImageEx());
    //高度取负值：自上而下的位图，GetBits 指向第一行、GetPitch 为正
    if (!img->Create(kAnnotMenuIconSize, -kAnnotMenuIconSize, kBitsPerPixel32, CImage::createAlphaChannel))
        return NULL;
    //子菜单的父项不画选中底：打勾的是子菜单里的某一项
    const bool checked = item.m_children.empty() && item.m_checked;
    if (!RenderAnnotChoiceIcon(c, state, checked, kAnnotMenuIconSize, static_cast<uint8_t*>(img->GetBits()),
                               img->GetPitch()))
        return NULL;
    return img.release();
}

//把菜单模型逐项交给 DuiMenu；子菜单与图标由调用方持有到菜单关闭之后
void appendMenu(balloonwjui::DuiMenu& menu, const std::vector<AnnotMenuItem>& items, const AnnotBarState& state,
                std::vector<std::unique_ptr<balloonwjui::DuiMenu> >& subMenus, std::vector<std::unique_ptr<CImageEx> >& icons)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const AnnotMenuItem& it = items[i];
        //分隔线
        if (it.m_separator)
        {
            menu.AppendSeparator();
            continue;
        }
        //子菜单：父项用子菜单里当前选中那一项的图标
        if (!it.m_children.empty())
        {
            std::unique_ptr<balloonwjui::DuiMenu> sub(new balloonwjui::DuiMenu());
            appendMenu(*sub, it.m_children, state, subMenus, icons);
            std::unique_ptr<CImageEx> icon(makeChoiceIcon(it, state));
            menu.AppendSubMenu(it.m_id, it.m_text.c_str(), sub.get(), icon.get());
            if (icon)
                icons.push_back(std::move(icon));
            subMenus.push_back(std::move(sub));
            continue;
        }
        //不可点的分组标题、提示
        if (!it.m_enabled)
        {
            menu.AppendDisabled(it.m_id, it.m_text.c_str());
            continue;
        }
        //颜色项：色块图标，当前色在色块里打对勾
        if (it.m_swatch >= 0)
        {
            std::unique_ptr<CImageEx> icon(makeSwatchIcon(static_cast<uint32_t>(it.m_swatch), it.m_checked));
            menu.AppendItem(it.m_id, it.m_text.c_str(), icon.get());
            if (icon)
                icons.push_back(std::move(icon));
            continue;
        }
        //其余项：效果图标（当前选中的画在图标里）；没有图标的照旧用勾选
        std::unique_ptr<CImageEx> icon(makeChoiceIcon(it, state));
        if (icon)
        {
            menu.AppendItem(it.m_id, it.m_text.c_str(), icon.get());
            icons.push_back(std::move(icon));
            continue;
        }
        menu.AppendChecked(it.m_id, it.m_text.c_str(), it.m_checked);
    }
}

std::unique_ptr<balloonwjui::DuiButton> makeButton(const wchar_t* text, UINT id)
{
    std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
    b->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    b->SetVariant(balloonwjui::DuiButton::Variant::Default);
    b->SetText(text);
    b->SetCtrlId(id);
    return b;
}

} // namespace

CShareAnnotationBar::CShareAnnotationBar()
    : m_notifyWnd(NULL)
    , m_hostMode(false)
    , m_viewersAllowed(true)
    , m_drawEnabled(true)
    , m_localMode(false)
    , m_undoButton(NULL)
    , m_permButton(NULL)
    , m_swatch(NULL)
    , m_bgBitmap(NULL)
    , m_expandIcon(NULL)
    , m_collapseIcon(NULL)
    , m_expandButton(NULL)
    , m_toolsButton(NULL)
    , m_handleUi(false)
    , m_pointerMode(false)
    , m_pointerButton(NULL)
    , m_collapseButton(NULL)
    , m_hideButton(NULL)
    , m_hideIcon(NULL)
    , m_selectButton(NULL)
    , m_selectIcon(NULL)
    , m_toolsIcon(NULL)
    , m_pointerIcon(NULL)
    , m_pointerIconOn(NULL)
    , m_pinIcon(NULL)
    , m_compact(false)
{
    m_groupIcons.assign(kAnnotGroupCount, static_cast<HBITMAP>(NULL));
    memset(&m_anchor, 0, sizeof(m_anchor));
    memset(&m_expandedRect, 0, sizeof(m_expandedRect));
    memset(&m_monitorRect, 0, sizeof(m_monitorRect));
    //取色框的自定义一栏先放白色
    for (int i = 0; i < kCustomColorSlots; ++i)
        m_customColors[i] = RGB(255, 255, 255);
}

CShareAnnotationBar::~CShareAnnotationBar()
{
    Hide();
    //对象析构时不能再推迟：Hide 在菜单弹出期间只隐藏了窗口，这里直接销毁
    if (IsWindow())
        DestroyWindow();
    if (m_swatch != NULL)
        ::DeleteObject(m_swatch);
    if (m_bgBitmap != NULL)
        ::DeleteObject(m_bgBitmap);
    for (size_t i = 0; i < m_groupIcons.size(); ++i)
    {
        if (m_groupIcons[i] != NULL)
            ::DeleteObject(m_groupIcons[i]);
    }
    if (m_expandIcon != NULL)
        ::DeleteObject(m_expandIcon);
    if (m_collapseIcon != NULL)
        ::DeleteObject(m_collapseIcon);
    if (m_pointerIcon != NULL)
        ::DeleteObject(m_pointerIcon);
    if (m_pointerIconOn != NULL)
        ::DeleteObject(m_pointerIconOn);
    if (m_pinIcon != NULL)
        ::DeleteObject(m_pinIcon);
    if (m_hideIcon != NULL)
        ::DeleteObject(m_hideIcon);
    if (m_selectIcon != NULL)
        ::DeleteObject(m_selectIcon);
    if (m_toolsIcon != NULL)
        ::DeleteObject(m_toolsIcon);
}

void CShareAnnotationBar::EnsureWindow()
{
    //窗口还在：包括菜单或取色框弹出期间被隐藏、等待销毁的窗口，直接复用并取消推迟的销毁（否则 m_hWnd 不为空，再 Create
    //会触发 atlwin.h 的断言）
    if (IsWindow())
    {
        if (m_popup.TakePendingDestroy())
            LOG_INFO("screenshare: annotation bar reused while its menu is closing.");
        return;
    }
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (Create(NULL, CWindow::rcDefault, TR(L"标注"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR("screenshare: create annotation bar failed.");
        return;
    }
    SetTitle(TR(L"标注"));
    SetTitleBarHeight(kBarTitleH);
    SetButtons(false, false, false);
    SetResizable(false);
    SetBorderPx(0);
    if (m_bgBitmap == NULL)
        m_bgBitmap = makeSolidBitmap(kBarBgColor, kSolidBitmapSize);
    if (m_bgBitmap != NULL)
    {
        RECT insets = { 0, 0, 0, 0 };
        SetBgImage(m_bgBitmap, insets);
        SetTitleBarTransparent(true);
        SetTitleTextColor(kBarTextColor);
    }
}

int CShareAnnotationBar::groupWidth(int group) const
{
    //放不下工作区：只有图标与下拉符号
    if (m_compact)
        return kCompactGroupW;
    //前面的图标（颜色组为色块）占的宽度
    const int iconW = ((group == annot_group_color) ? kShareMouseSwatchSize : kAnnotButtonIconSize) + kIconTextGap;
    //这一组按钮上可能出现的文字：组名，以及选了组里每一项（含子菜单里的）之后的写法
    int w = (std::max)(kGroupBtnW[group], i18n::DefaultButtonWidthForText(
                                              AnnotGroupButtonText(group, AnnotBarState()).c_str(), kGroupBtnW[group]));
    std::vector<AnnotMenuItem> items = BuildAnnotGroupMenu(group, m_state);
    for (size_t i = 0; i < items.size(); ++i)
    {
        //子菜单的项也算，追加到末尾逐个处理（先拷出来：往同一个 vector 里插入它自己元素的内容，扩容时原迭代器会失效）
        const std::vector<AnnotMenuItem> children = items[i].m_children;
        items.insert(items.end(), children.begin(), children.end());
        AnnotMenuChoice c;
        if (!DecodeAnnotMenuChoice(items[i].m_id, c))
            continue;
        AnnotBarState probe = m_state;
        if (!ApplyAnnotChoice(c, probe))
            continue;
        w = (std::max)(w, i18n::DefaultButtonWidthForText(AnnotGroupButtonText(group, probe).c_str(), kGroupBtnW[group]));
    }
    return w + iconW;
}

int CShareAnnotationBar::toolsWidth() const
{
    if (m_compact)
        return kCompactGroupW;
    int w = 0;
    for (int g = 0; g < kAnnotGroupCount; ++g)
    {
        if (AnnotIsToolGroup(g))
            w = (std::max)(w, groupWidth(g));
    }
    return w;
}

int CShareAnnotationBar::barWidth() const
{
    //最左边的「鼠标」（2026-10-07）、「收起」与「隐藏」（2026-10-07 晚）按钮
    int w = 2 * kRowPad + kGroupGap + kGroupGap + kSmallBtnW + kRowGap + kExitBtnW + 2 * (kCollapseBtnW + kRowGap) +
            kPointerBtnW + kRowGap;
    //保存为图片（第 23 期）：本机模式没有
    if (!m_localMode)
        w += kSaveBtnW + kRowGap;
    //九个工具组合成一个「绘图工具」按钮，左边紧挨「选择」（2026-10-08）；样式组各一个按钮
    w += toolsWidth() + kRowGap + kSelectBtnW + kRowGap;
    for (int g = 0; g < kAnnotGroupCount; ++g)
    {
        if (!AnnotIsToolGroup(g))
            w += groupWidth(g) + kRowGap;
    }
    if (m_hostMode)
        w += kSmallBtnW + kRowGap;
    if (m_hostMode && !m_localMode)
        w += kPermBtnW + kRowGap;
    return w;
}

void CShareAnnotationBar::buildCollapsedUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kBarBgColor);
    root->SetPadding(0);
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(kHandlePadH, 0, kHandlePadH, 0);
    const balloonwjui::DuiLayout::Hint btnHint = balloonwjui::DuiLayout::Hint().Margin(0, kBtnVMargin, 0, kBtnVMargin);
    //小把手：一个只显示当前工具图标的按钮；鼠标移上去临时展开，点一下展开并固定
    std::unique_ptr<balloonwjui::DuiButton> expand = makeButton(L"", kIdExpand);
    balloonwjui::DuiToolTipMgr::Inst().Register(expand.get(), TR(L"展开工具条"));
    m_expandButton = expand.get();
    row->AddChild(std::move(expand), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kHandleBtnW));
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
    m_handleUi = true;
    //小把手太窄，标题栏不写字
    SetTitle(L"");
    refresh();
}

void CShareAnnotationBar::BuildUi()
{
    //按组的取值下标存按钮指针，摆放顺序另按 kAnnotGroupOrder
    m_groupButtons.assign(kAnnotGroupCount, NULL);
    m_undoButton = NULL;
    m_permButton = NULL;
    m_expandButton = NULL;
    m_toolsButton = NULL;
    m_pointerButton = NULL;
    m_collapseButton = NULL;
    m_hideButton = NULL;
    m_selectButton = NULL;
    if (m_collapse.State() == top_bar_collapsed)
    {
        buildCollapsedUi();
        return;
    }
    m_handleUi = false;
    SetTitle(TR(L"标注"));

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kBarBgColor);
    root->SetPadding(0);
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(kRowPad, 0, kRowPad, 0);
    row->SetGap(kRowGap);
    const balloonwjui::DuiLayout::Hint btnHint = balloonwjui::DuiLayout::Hint().Margin(0, kBtnVMargin, 0, kBtnVMargin);

    //最左边的「鼠标」（2026-10-07）：只有箭头图标，点了以后不拦截鼠标，可以操作桌面上的程序，标注保留
    if (m_pointerIcon == NULL)
        m_pointerIcon = MakeShareBarIconBitmap(share_bar_icon_mouse, kAnnotButtonIconSize, kIconDarkColor);
    if (m_pointerIconOn == NULL)
        m_pointerIconOn = MakeShareBarIconBitmap(share_bar_icon_mouse, kAnnotButtonIconSize, kIconLightColor);
    std::unique_ptr<balloonwjui::DuiButton> pointer = makeButton(L"", kIdPointer);
    pointer->SetLeadingIconSize(kAnnotButtonIconSize);
    balloonwjui::DuiToolTipMgr::Inst().Register(pointer.get(), TR(L"鼠标：可以操作桌面上的程序，标注保留"));
    m_pointerButton = pointer.get();
    row->AddChild(std::move(pointer), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kPointerBtnW));

    //「收起」：只有双左箭头图标，悬停时提示；临时展开时变成「固定」（图钉）
    if (m_collapseIcon == NULL)
        m_collapseIcon = makeCollapseBitmap(kAnnotButtonIconSize);
    if (m_pinIcon == NULL)
        m_pinIcon = MakeShareBarIconBitmap(share_bar_icon_pushpin, kAnnotButtonIconSize, kIconDarkColor);
    std::unique_ptr<balloonwjui::DuiButton> collapse = makeButton(L"", kIdCollapse);
    collapse->SetLeadingIconSize(kAnnotButtonIconSize);
    m_collapseButton = collapse.get();
    row->AddChild(std::move(collapse), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kCollapseBtnW));

    //「隐藏」（2026-10-07 晚）：只有眼睛加斜线的图标，悬停时说明鼠标推到屏幕边缘会重新出现
    if (m_hideIcon == NULL)
        m_hideIcon = MakeShareBarIconBitmap(share_bar_icon_hide, kAnnotButtonIconSize, kIconDarkColor);
    std::unique_ptr<balloonwjui::DuiButton> hide = makeButton(L"", kIdHide);
    hide->SetLeadingIcon(m_hideIcon);
    hide->SetLeadingIconSize(kAnnotButtonIconSize);
    balloonwjui::DuiToolTipMgr::Inst().Register(hide.get(), TopBarHideText());
    m_hideButton = hide.get();
    row->AddChild(std::move(hide), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kCollapseBtnW));

    bool toolsAdded = false;
    for (int i = 0; i < kAnnotGroupCount; ++i)
    {
        const int g = kAnnotGroupOrder[i];
        //九个工具组：在第一个工具组的位置放一个「绘图工具」按钮，其余跳过
        if (AnnotIsToolGroup(g))
        {
            if (toolsAdded)
                continue;
            toolsAdded = true;
            //「选择」（2026-10-08）：只有图标，紧挨「绘图工具」左边；点一下进入选择，再点一下回到上一个画图工具
            if (m_selectIcon == NULL)
            {
                AnnotMenuChoice selectChoice;
                selectChoice.m_command = annot_bar_tool;
                selectChoice.m_value = kAnnotSelect;
                m_selectIcon = makeChoiceBitmap(selectChoice, m_state, kAnnotButtonIconSize);
            }
            std::unique_ptr<balloonwjui::DuiButton> select = makeButton(L"", kIdSelect);
            select->SetLeadingIcon(m_selectIcon);
            select->SetLeadingIconSize(kAnnotButtonIconSize);
            balloonwjui::DuiToolTipMgr::Inst().Register(select.get(), TR(L"选择：移动、缩放或删除已画的对象"));
            m_selectButton = select.get();
            row->AddChild(std::move(select), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kSelectBtnW));
            std::unique_ptr<balloonwjui::DuiButton> tools =
                makeButton(AnnotToolsButtonText(m_state).c_str(), static_cast<UINT>(kIdTools));
            balloonwjui::DuiToolTipMgr::Inst().Register(tools.get(), TR(L"绘图工具"));
            m_toolsButton = tools.get();
            row->AddChild(std::move(tools), balloonwjui::DuiLayout::Hint(btnHint).Fixed(toolsWidth()));
            continue;
        }
        std::unique_ptr<balloonwjui::DuiButton> b =
            makeButton(AnnotGroupButtonText(g, m_state).c_str(), static_cast<UINT>(kIdGroupBase + g));
        //只有图标时，悬停提示组名
        if (m_compact)
            balloonwjui::DuiToolTipMgr::Inst().Register(b.get(), AnnotGroupButtonText(g, m_state).c_str());
        m_groupButtons[g] = b.get();
        //工具组与样式组之间多留一点距离
        balloonwjui::DuiLayout::Hint h = balloonwjui::DuiLayout::Hint(btnHint).Fixed(groupWidth(g));
        if (g == kStyleGroupFirst)
            h.Margin(kGroupGap, kBtnVMargin, 0, kBtnVMargin);
        row->AddChild(std::move(b), h);
    }
    std::unique_ptr<balloonwjui::DuiButton> undo = makeButton(TR(L"撤销"), kIdUndo);
    m_undoButton = undo.get();
    row->AddChild(std::move(undo), balloonwjui::DuiLayout::Hint().Fixed(kSmallBtnW).Margin(kGroupGap, kBtnVMargin, 0,
                                                                                            kBtnVMargin));
    if (m_hostMode)
    {
        std::unique_ptr<balloonwjui::DuiButton> clear = makeButton(TR(L"清空"), kIdClear);
        row->AddChild(std::move(clear), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kSmallBtnW));
        //本机模式（录屏画笔、白板）只有本人在画，没有他人的标注权限可管
        if (!m_localMode)
        {
            std::unique_ptr<balloonwjui::DuiButton> perm = makeButton(TR(L"禁止他人标注"), kIdPermission);
            m_permButton = perm.get();
            row->AddChild(std::move(perm), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kPermBtnW));
        }
    }
    //保存为图片（第 23 期）：本机模式（录屏画笔、录制用的白板）的笔迹会录进视频，不需要另存
    if (!m_localMode)
    {
        std::unique_ptr<balloonwjui::DuiButton> saveBtn = makeButton(TR(L"保存为图片"), kIdSave);
        row->AddChild(std::move(saveBtn), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kSaveBtnW));
    }
    std::unique_ptr<balloonwjui::DuiButton> exitBtn = makeButton(TR(L"退出标注"), kIdExit);
    exitBtn->SetVariant(balloonwjui::DuiButton::Variant::Danger);
    row->AddChild(std::move(exitBtn), balloonwjui::DuiLayout::Hint(btnHint).Fixed(kExitBtnW));

    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
    refreshColorSwatch();
    refresh();
}

void CShareAnnotationBar::refreshColorSwatch()
{
    //按钮只引用位图、不复制：先换上新位图，再释放旧的，按钮任何时候都不持有已释放的句柄
    HBITMAP old = m_swatch;
    m_swatch = makeSwatchBitmap(m_state.m_color);
    if (static_cast<int>(m_groupButtons.size()) > annot_group_color && m_groupButtons[annot_group_color] != NULL)
    {
        m_groupButtons[annot_group_color]->SetLeadingIcon(m_swatch);
        m_groupButtons[annot_group_color]->SetLeadingIconSize(kShareMouseSwatchSize);
    }
    if (old != NULL)
        ::DeleteObject(old);
}

void CShareAnnotationBar::refreshGroupIcons()
{
    //按钮只引用位图、不复制：先建好新的一套并换上，再释放旧的，按钮任何时候都不持有已释放的句柄
    std::vector<HBITMAP> fresh(kAnnotGroupCount, static_cast<HBITMAP>(NULL));
    for (int g = 0; g < kAnnotGroupCount; ++g)
    {
        AnnotMenuChoice c;
        if (AnnotGroupButtonChoice(g, m_state, c))
            fresh[g] = makeChoiceBitmap(c, m_state, kAnnotButtonIconSize);
        if (g < static_cast<int>(m_groupButtons.size()) && m_groupButtons[g] != NULL && fresh[g] != NULL)
        {
            m_groupButtons[g]->SetLeadingIcon(fresh[g]);
            m_groupButtons[g]->SetLeadingIconSize(kAnnotButtonIconSize);
            m_groupButtons[g]->SetLeadingIconGap(kIconTextGap);
        }
    }
    //「展开工具条」前面：当前工具所在组的图标（不属于任何组时用画笔组的）
    HBITMAP freshExpand = NULL;
    const int activeGroup = AnnotToolGroup(m_state.m_tool);
    AnnotMenuChoice c;
    if (AnnotGroupButtonChoice(activeGroup >= 0 ? activeGroup : static_cast<int>(annot_group_pen), m_state, c))
        freshExpand = makeChoiceBitmap(c, m_state, kAnnotButtonIconSize);
    //小把手：「鼠标」状态下显示箭头，否则显示当前工具的图标
    if (m_expandButton != NULL && (m_pointerMode ? m_pointerIcon : freshExpand) != NULL)
    {
        m_expandButton->SetLeadingIcon(m_pointerMode ? m_pointerIcon : freshExpand);
        m_expandButton->SetLeadingIconSize(kAnnotButtonIconSize);
        m_expandButton->SetLeadingIconGap(kIconTextGap);
    }
    //「绘图工具」按钮与「展开工具条」共用当前工具的图标；选择状态下（2026-10-08）「绘图工具」改用最近一次用的画图工具的图标
    HBITMAP freshTools = NULL;
    if (m_state.m_tool == kAnnotSelect)
    {
        AnnotBarState shown = m_state;
        shown.m_tool = AnnotSelectButtonTarget(m_state);
        AnnotMenuChoice tc;
        if (AnnotGroupButtonChoice(AnnotToolGroup(shown.m_tool), shown, tc))
            freshTools = makeChoiceBitmap(tc, shown, kAnnotButtonIconSize);
    }
    HBITMAP toolsIcon = (freshTools != NULL) ? freshTools : freshExpand;
    if (m_toolsButton != NULL && toolsIcon != NULL)
    {
        m_toolsButton->SetLeadingIcon(toolsIcon);
        m_toolsButton->SetLeadingIconSize(kAnnotButtonIconSize);
        m_toolsButton->SetLeadingIconGap(kIconTextGap);
    }
    if (m_toolsIcon != NULL)
        ::DeleteObject(m_toolsIcon);
    m_toolsIcon = freshTools;
    for (size_t i = 0; i < m_groupIcons.size(); ++i)
    {
        if (m_groupIcons[i] != NULL)
            ::DeleteObject(m_groupIcons[i]);
    }
    m_groupIcons = fresh;
    if (m_expandIcon != NULL)
        ::DeleteObject(m_expandIcon);
    m_expandIcon = freshExpand;
}

void CShareAnnotationBar::refresh()
{
    refreshGroupIcons();
    const int activeGroup = AnnotToolGroup(m_state.m_tool);
    for (int g = 0; g < static_cast<int>(m_groupButtons.size()); ++g)
    {
        if (m_groupButtons[g] == NULL)
            continue;
        m_groupButtons[g]->SetText(m_compact ? kCompactDropMark : AnnotGroupButtonText(g, m_state).c_str());
        m_groupButtons[g]->SetVariant(g == activeGroup ? balloonwjui::DuiButton::Variant::Primary
                                                       : balloonwjui::DuiButton::Variant::Default);
        m_groupButtons[g]->SetEnabled(m_drawEnabled);
    }
    //「绘图工具」：写当前工具，主色表示正在用的工具；「鼠标」状态与选择状态下不在画图，改为默认样式
    const bool selecting = (m_state.m_tool == kAnnotSelect);
    if (m_toolsButton != NULL)
    {
        m_toolsButton->SetText(m_compact ? kCompactDropMark : AnnotToolsButtonText(m_state).c_str());
        m_toolsButton->SetVariant((m_pointerMode || selecting) ? balloonwjui::DuiButton::Variant::Default
                                                               : balloonwjui::DuiButton::Variant::Primary);
        m_toolsButton->SetEnabled(m_drawEnabled);
    }
    //「选择」（2026-10-08）：选择状态下为主色
    if (m_selectButton != NULL)
    {
        m_selectButton->SetVariant((!m_pointerMode && selecting) ? balloonwjui::DuiButton::Variant::Primary
                                                                 : balloonwjui::DuiButton::Variant::Default);
        m_selectButton->SetEnabled(m_drawEnabled);
    }
    //「鼠标」：在「鼠标」状态下为主色、白色箭头
    if (m_pointerButton != NULL)
    {
        m_pointerButton->SetVariant(m_pointerMode ? balloonwjui::DuiButton::Variant::Primary
                                                  : balloonwjui::DuiButton::Variant::Default);
        m_pointerButton->SetLeadingIcon(m_pointerMode ? m_pointerIconOn : m_pointerIcon);
    }
    //「收起 / 固定」：一直展开时收起（双左箭头），临时展开时固定（图钉）
    if (m_collapseButton != NULL)
    {
        const bool peek = (m_collapse.State() == top_bar_peek || m_collapse.State() == top_bar_hidden_peek);
        m_collapseButton->SetLeadingIcon(peek ? m_pinIcon : m_collapseIcon);
        balloonwjui::DuiToolTipMgr::Inst().Register(m_collapseButton, peek ? TR(L"固定工具条") : TR(L"收起工具条"));
    }
    if (m_undoButton != NULL)
        m_undoButton->SetEnabled(m_drawEnabled);
    if (m_permButton != NULL)
        m_permButton->SetText(m_viewersAllowed ? TR(L"禁止他人标注") : TR(L"允许他人标注"));
    if (IsWindow())
        Invalidate();
}

bool CShareAnnotationBar::IsHostMode() const
{
    return m_hostMode;
}

void CShareAnnotationBar::Show(const RECT& anchor, HWND notifyWnd, bool hostMode)
{
    m_notifyWnd = notifyWnd;
    if (IsWindow() && hostMode != m_hostMode)
        Hide();
    m_notifyWnd = notifyWnd;
    m_hostMode = hostMode;
    EnsureWindow();
    if (!IsWindow())
        return;
    //隐藏只在一次标注期间有效（2026-10-07 晚）：重新进入标注时工具条重新显示；收起状态照旧保留
    if (m_collapse.State() == top_bar_hidden || m_collapse.State() == top_bar_hidden_peek)
        m_collapse.Reset();
    //整条放不下参照矩形所在显示器的工作区时，分组按钮只显示图标与下拉符号
    m_compact = false;
    RECT work = monitorWorkArea(anchor);
    if (barWidth() > work.right - work.left)
        m_compact = true;
    BuildUi();
    //发起人的工具条浮在被共享的内容上，默认不能被采进共享画面；「允许截图」打开时（2026-10-08）允许
    if (hostMode)
        ApplyBarCaptureState(m_hWnd);
    MoveTo(anchor);
    ShowWindow(SW_SHOWNA);
    UpdateWindow();
    //收起或临时展开（收起与否在本对象存续期间保留）：检查鼠标是否移到小把手或工具条上
    if (m_collapse.State() != top_bar_pinned)
        SetTimer(kTopBarTimerId, kTopBarTickMs, NULL);
}

RECT CShareAnnotationBar::monitorWorkArea(const RECT& anchor)
{
    HMONITOR mon = ::MonitorFromRect(&anchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

LRESULT CShareAnnotationBar::OnCollapseToggle(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    //点了「隐藏」（2026-10-07 晚）：工具条与小把手都不显示
    if (wParam == kCollapseParamHide)
    {
        if (m_collapse.OnHideClicked())
            applyCollapse();
        return 0;
    }
    //点了「收起 / 固定」：一直展开时收起成小把手，临时展开时固定
    if (wParam == kCollapseParamToggle)
    {
        if (m_collapse.OnToggleClicked())
            applyCollapse();
        return 0;
    }
    //点了小把手：展开并固定
    SetCollapsed(false);
    return 0;
}

void CShareAnnotationBar::MoveTo(const RECT& anchor)
{
    m_anchor = anchor;
    if (!IsWindow())
        return;
    const int w = barWidth();
    const int h = kBarTitleH + kBarContentH;
    const RECT work = monitorWorkArea(anchor);
    //所在显示器的整块矩形：隐藏后的屏幕边缘感应区据此计算
    {
        HMONITOR mon = ::MonitorFromRect(&anchor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        m_monitorRect = ::GetMonitorInfo(mon, &mi) ? mi.rcMonitor : work;
    }
    int x = anchor.left + ((anchor.right - anchor.left) - w) / 2;
    //参照矩形下方放得下（共享单个窗口时常见）就放在下方，不挡住要标注的内容；否则（整屏共享、全屏观看）放进底部
    int y = anchor.bottom + kBelowGap;
    if (y + h > work.bottom)
        y = anchor.bottom - kBottomMargin - h;
    //限制在工作区内
    if (x + w > work.right)
        x = work.right - w;
    if (x < work.left)
        x = work.left;
    if (y + h > work.bottom)
        y = work.bottom - h;
    if (y < work.top)
        y = work.top;
    m_expandedRect.left = x;
    m_expandedRect.top = y;
    m_expandedRect.right = x + w;
    m_expandedRect.bottom = y + h;
    //隐藏（2026-10-07 晚）：只记下位置，窗口保持不可见
    if (m_collapse.State() == top_bar_hidden)
    {
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_expandedRect.left, m_expandedRect.top, w, h,
                       SWP_NOACTIVATE | SWP_HIDEWINDOW);
        return;
    }
    //收起时摆小把手，否则摆整条
    const RECT rc = (m_collapse.State() == top_bar_collapsed) ? handleRect() : m_expandedRect;
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

RECT CShareAnnotationBar::handleRect() const
{
    //小把手贴在整条展开时的矩形底边正中，高度与整条相同（标题栏 + 一行按钮）
    const int w = kHandleBtnW + 2 * kHandlePadH;
    const int cx = (m_expandedRect.left + m_expandedRect.right) / 2;
    RECT rc;
    rc.left = cx - w / 2;
    rc.right = rc.left + w;
    rc.bottom = m_expandedRect.bottom;
    rc.top = rc.bottom - (kBarTitleH + kBarContentH);
    return rc;
}

void CShareAnnotationBar::applyCollapse()
{
    if (!IsWindow())
        return;
    const int state = m_collapse.State();
    //收起与展开用不同的界面：变了才重建（临时展开与一直展开都是整条，只刷新「收起 / 固定」按钮）
    const bool wantHandle = (state == top_bar_collapsed);
    if (wantHandle != m_handleUi)
        BuildUi();
    else
        refresh();
    MoveTo(m_anchor);
    Invalidate();
    //一直展开时不需要检查鼠标
    if (state == top_bar_pinned)
        KillTimer(kTopBarTimerId);
    else
        SetTimer(kTopBarTimerId, kTopBarTickMs, NULL);
}

int CShareAnnotationBar::CollapseState() const
{
    return m_collapse.State();
}

void CShareAnnotationBar::TickCollapse(int64_t nowMs, bool inside)
{
    //下拉菜单或取色框开着：鼠标多半在菜单上，不收起
    if (m_popup.IsActive())
        inside = true;
    if (m_collapse.OnTick(nowMs, inside, inside))
        applyCollapse();
}

void CShareAnnotationBar::TickCollapseAt(int64_t nowMs, POINT cursor)
{
    if (!IsWindow())
        return;
    //窗口矩形在收起时就是小把手、展开时就是工具条；隐藏时窗口不可见，不算在上面
    RECT rc = { 0, 0, 0, 0 };
    GetWindowRect(&rc);
    bool inside = IsWindowVisible() && ::PtInRect(&rc, cursor) != FALSE;
    //下拉菜单或取色框开着：鼠标多半在菜单上，不收起、不隐藏
    if (m_popup.IsActive())
        inside = true;
    const RECT edge = ComputeHiddenEdgeZone(m_expandedRect, m_monitorRect);
    const bool overEdge = ::PtInRect(&edge, cursor) != FALSE;
    if (m_collapse.OnTick(nowMs, inside, inside, overEdge))
        applyCollapse();
}

LRESULT CShareAnnotationBar::OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kTopBarTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    TickCollapseAt(static_cast<int64_t>(::GetTickCount64()), pt);
    return 0;
}

void CShareAnnotationBar::SetPointerMode(bool on)
{
    if (on == m_pointerMode)
        return;
    m_pointerMode = on;
    refresh();
}

bool CShareAnnotationBar::IsPointerMode() const
{
    return m_pointerMode;
}

void CShareAnnotationBar::Hide()
{
    //控件树随窗口一起销毁，指向其中控件的指针全部清空
    m_groupButtons.clear();
    m_undoButton = NULL;
    m_permButton = NULL;
    m_expandButton = NULL;
    m_toolsButton = NULL;
    m_pointerButton = NULL;
    m_collapseButton = NULL;
    m_hideButton = NULL;
    m_selectButton = NULL;
    m_handleUi = false;
    if (!IsWindow())
        return;
    KillTimer(kTopBarTimerId);
    //下拉菜单或取色框正在弹出：它们的消息循环还在本窗口的消息处理里，此时销毁窗口，ATL 要等那条消息返回才清 m_hWnd，
    //菜单也不会随之关闭。改为先关掉它们、只隐藏窗口，返回后由 OnShowMenu 销毁
    if (ShareBarShouldDeferDestroy(m_popup.IsActive()))
    {
        m_popup.Close();
        m_popup.SetPendingDestroy();
        ShowWindow(SW_HIDE);
        LOG_INFO("screenshare: annotation bar hidden while its menu is open, destroy deferred.");
        return;
    }
    DestroyWindow();
}

bool CShareAnnotationBar::IsPopupActive() const
{
    return m_popup.IsActive();
}

LRESULT CShareAnnotationBar::OnShowMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    //已经有菜单开着、窗口已隐藏（等待销毁或已退出标注）：不再弹
    if (m_popup.IsActive() || m_popup.IsPendingDestroy() || !IsWindowVisible())
        return 0;
    if (wParam == kMenuParamTools)
        showToolsMenu();
    else
        showGroupMenu(static_cast<int>(wParam));
    //菜单或取色框期间工具条被隐藏：现在才销毁窗口（本条消息是投递来的，返回后 ATL 即清空 m_hWnd）
    if (m_popup.TakePendingDestroy())
    {
        LOG_INFO("screenshare: annotation bar destroyed after its menu closed.");
        DestroyWindow();
    }
    return 0;
}

UINT_PTR CALLBACK CShareAnnotationBar::colorDialogHook(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    //对话框初始化：lParam 是 ChooseColor 的参数，lCustData 带着工具条对象
    if (msg == WM_INITDIALOG)
    {
        const CHOOSECOLOR* cc = reinterpret_cast<const CHOOSECOLOR*>(lParam);
        CShareAnnotationBar* self = (cc != NULL) ? reinterpret_cast<CShareAnnotationBar*>(cc->lCustData) : NULL;
        if (self != NULL)
            self->m_popup.SetDialogWindow(dialog);
        //返回非 0：由系统按默认规则设置键盘焦点
        return TRUE;
    }
    //其余消息交给默认的对话框过程
    return 0;
}

void CShareAnnotationBar::SetTool(int tool)
{
    m_state.m_tool = tool;
    refresh();
}

void CShareAnnotationBar::SetColor(uint32_t color)
{
    const bool changed = (color != m_state.m_color);
    m_state.m_color = color;
    if (changed)
        refreshColorSwatch();
    refresh();
}

void CShareAnnotationBar::SetState(const AnnotBarState& state)
{
    const bool colorChanged = (state.m_color != m_state.m_color);
    m_state = state;
    if (colorChanged)
        refreshColorSwatch();
    refresh();
}

void CShareAnnotationBar::SetViewersAllowed(bool allowed)
{
    m_viewersAllowed = allowed;
    refresh();
}

void CShareAnnotationBar::SetLocalMode(bool local)
{
    m_localMode = local;
}

void CShareAnnotationBar::SetCollapsed(bool collapsed)
{
    const int before = m_collapse.State();
    //回到一直展开；要收起时再按一次「收起」
    m_collapse.Reset();
    if (collapsed)
        m_collapse.OnToggleClicked();
    if (m_collapse.State() == before)
        return;
    applyCollapse();
}

bool CShareAnnotationBar::IsCollapsed() const
{
    return m_collapse.State() == top_bar_collapsed;
}

void CShareAnnotationBar::SetDrawEnabled(bool enabled)
{
    m_drawEnabled = enabled;
    refresh();
}

void CShareAnnotationBar::showGroupMenu(int group)
{
    if (group < 0 || group >= static_cast<int>(m_groupButtons.size()) || m_groupButtons[group] == NULL)
        return;
    balloonwjui::DuiMenu menu;
    std::vector<std::unique_ptr<balloonwjui::DuiMenu> > subMenus;   //子菜单，菜单关闭之后才释放
    std::vector<std::unique_ptr<CImageEx> > icons;                  //色块与效果图标，同上
    appendMenu(menu, BuildAnnotGroupMenu(group, m_state), m_state, subMenus, icons);
    //弹在按钮上方：工具条通常贴着屏幕底部（DuiMenu 自己会把落点限制在工作区内）
    const RECT& rc = m_groupButtons[group]->GetRect();
    const SIZE size = menu.MeasureSize();
    POINT pt = { rc.left, rc.top };
    ClientToScreen(&pt);
    pt.y -= size.cy;
    //记下正在弹出的菜单与子菜单：期间工具条被隐藏或点了条上的按钮时据此关掉它们
    m_popup.BeginMenu(&menu, subMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    m_popup.End();
    //菜单期间工具条被隐藏过（退出了标注）或通知窗口没了：丢弃这次选择
    if (!ShareBarShouldDeliverChoice(chosen, m_popup.WasInterrupted(), m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return;
    AnnotMenuChoice choice;
    if (DecodeAnnotMenuChoice(chosen, choice))
        applyChoice(choice);
}

void CShareAnnotationBar::showToolsMenu()
{
    if (m_toolsButton == NULL)
        return;
    balloonwjui::DuiMenu menu;
    std::vector<std::unique_ptr<balloonwjui::DuiMenu> > subMenus;   //子菜单，菜单关闭之后才释放
    std::vector<std::unique_ptr<CImageEx> > icons;                  //各项的效果图标，同上
    appendMenu(menu, BuildAnnotToolsMenu(m_state), m_state, subMenus, icons);
    //弹在按钮上方：工具条通常贴着屏幕底部（DuiMenu 自己会把落点限制在工作区内）
    const RECT& rc = m_toolsButton->GetRect();
    const SIZE size = menu.MeasureSize();
    POINT pt = { rc.left, rc.top };
    ClientToScreen(&pt);
    pt.y -= size.cy;
    //记下正在弹出的菜单与子菜单：期间工具条被隐藏或点了条上的按钮时据此关掉它们
    m_popup.BeginMenu(&menu, subMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    m_popup.End();
    //菜单期间工具条被隐藏过（退出了标注）或通知窗口没了：丢弃这次选择
    if (!ShareBarShouldDeliverChoice(chosen, m_popup.WasInterrupted(), m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return;
    AnnotMenuChoice choice;
    if (DecodeAnnotMenuChoice(chosen, choice))
        applyChoice(choice);
}

void CShareAnnotationBar::applyChoice(const AnnotMenuChoice& choice)
{
    if (m_notifyWnd == NULL || !::IsWindow(m_notifyWnd))
        return;
    AnnotMenuChoice c = choice;
    //自定义颜色：弹出系统取色框，选好后按普通颜色处理
    if (c.m_command == annot_bar_custom_color)
    {
        CHOOSECOLOR cc;
        memset(&cc, 0, sizeof(cc));
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = m_hWnd;
        cc.rgbResult = RGB((m_state.m_color >> 16) & 0xFF, (m_state.m_color >> 8) & 0xFF, m_state.m_color & 0xFF);
        cc.lpCustColors = m_customColors;
        //钩子在对话框建好时把它的窗口登记下来：取色期间工具条被隐藏（退出标注）时据此取消取色框
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ENABLEHOOK;
        cc.lpfnHook = colorDialogHook;
        cc.lCustData = reinterpret_cast<LPARAM>(this);
        m_popup.BeginDialog();
        const BOOL picked = ::ChooseColor(&cc);
        m_popup.End();
        if (!picked || m_popup.WasInterrupted() || m_notifyWnd == NULL || !::IsWindow(m_notifyWnd))
            return;
        c.m_command = annot_bar_color;
        c.m_value = static_cast<int>((GetRValue(cc.rgbResult) << 16) | (GetGValue(cc.rgbResult) << 8) |
                                     GetBValue(cc.rgbResult));
    }
    //先记进本地状态，界面立即跟着变；控制器按同一个函数记一遍，处理完后还会整体同步一次
    const uint32_t oldColor = m_state.m_color;
    //选了工具（或会切到某个工具的样式）：从「鼠标」状态回到画图（2026-10-07），控制器按同一个函数判断
    if (m_pointerMode && AnnotChoiceLeavesPointer(c))
        m_pointerMode = false;
    if (ApplyAnnotChoice(c, m_state))
    {
        if (m_state.m_color != oldColor)
            refreshColorSwatch();
        refresh();
    }
    ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, static_cast<WPARAM>(c.m_command), static_cast<LPARAM>(c.m_value));
}

LRESULT CShareAnnotationBar::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //下拉菜单开着时点了工具条上的按钮：工具条不抢焦点，点它不会让菜单失去焦点，这里先关掉菜单；点的是弹出菜单的按钮时
    //只关菜单，不再重新弹出
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && m_popup.IsActive())
    {
        m_popup.Close();
        const int clicked = static_cast<int>(n->ctrlId);
        if (clicked == kIdTools || (clicked >= kIdGroupBase && clicked < kIdGroupBase + kAnnotGroupCount))
            return 0;
    }
    //收起、展开、隐藏只改工具条自己，不需要通知窗口；放到 PostMessage 之后再做，避免在按钮的点击通知里销毁按钮本身
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) &&
        (n->ctrlId == static_cast<UINT>(kIdCollapse) || n->ctrlId == static_cast<UINT>(kIdExpand) ||
         n->ctrlId == static_cast<UINT>(kIdHide)))
    {
        WPARAM param = kCollapseParamExpand;
        if (n->ctrlId == static_cast<UINT>(kIdCollapse))
            param = kCollapseParamToggle;
        else if (n->ctrlId == static_cast<UINT>(kIdHide))
            param = kCollapseParamHide;
        PostMessage(kMsgCollapseToggle, param, 0);
        return 0;
    }
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK) || m_notifyWnd == NULL ||
        !::IsWindow(m_notifyWnd))
    {
        bHandled = FALSE;
        return 0;
    }
    const int id = static_cast<int>(n->ctrlId);
    //分组按钮：投给自己，等这次鼠标事件处理完再弹这一组的下拉菜单
    if (id >= kIdGroupBase && id < kIdGroupBase + kAnnotGroupCount)
    {
        PostMessage(kMsgShowMenu, static_cast<WPARAM>(id - kIdGroupBase), 0);
        return 0;
    }
    switch (id)
    {
    //「鼠标」（2026-10-07）：进入或退出「鼠标」状态，界面立即跟着变，控制器撤掉或盖回输入层
    case kIdPointer:
        m_pointerMode = !m_pointerMode;
        refresh();
        ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, annot_bar_pointer, m_pointerMode ? 1 : 0);
        return 0;
    //「绘图工具」：投给自己，等这次鼠标事件处理完再弹九个工具组的菜单；选择状态下（2026-10-08）直接回到上一个画图工具
    case kIdTools:
        if (m_state.m_tool == kAnnotSelect)
            ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, annot_bar_tool, AnnotSelectButtonTarget(m_state));
        else
            PostMessage(kMsgShowMenu, kMenuParamTools, 0);
        return 0;
    //「选择」（2026-10-08）：进入选择，或回到上一个画图工具
    case kIdSelect:
        ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, annot_bar_tool, AnnotSelectButtonTarget(m_state));
        return 0;
    //撤销
    case kIdUndo:
        ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, kCmdUndo, 0);
        return 0;
    //清空（发起人）
    case kIdClear:
        ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, kCmdClear, 0);
        return 0;
    //禁止 / 允许他人标注（发起人）：发出的是切换后的值
    case kIdPermission:
        ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, kCmdPermission, m_viewersAllowed ? 0 : 1);
        return 0;
    //退出标注
    case kIdExit:
        ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, kCmdExit, 0);
        return 0;
    //保存为图片（第 23 期）
    case kIdSave:
        ::PostMessage(m_notifyWnd, kMsgShareAnnotCommand, kCmdSave, 0);
        return 0;
    //其余控件（标题栏等）交给基类
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenshare
