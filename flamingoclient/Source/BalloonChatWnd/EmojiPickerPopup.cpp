/**
 *  EmojiPickerPopup.cpp —— 单聊窗 emoji 选择浮层实现。
 *
 *  网格用 balloonui 现成控件搭：DuiVBox（行容器）套 DuiHBox（每行），每格一个
 *  DuiButton（Text 变体、无底无边、hover 浅底）显示一个 emoji。每个单元格分配
 *  一个唯一 ctrlId，点击时据 ctrlId 取回对应 emoji 串经回调交回单聊窗。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "EmojiPickerPopup.h"

#include <memory>

#include "../IULog.h"
#include "../../balloonui/balloonui/DuiNotify.h"
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox / Hint
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"     // 末行补位用的空 spacer

// ---- 文件作用域常量 -----------------------------------------------------

// 内置常用 emoji（UTF-16，用 \U / \u 转义书写，不依赖源文件编码）。受 GDI 限制
// 呈现为单色字形。约 48 个，按 kCols 列铺成网格。
static const wchar_t* const kEmojis[] =
{
    L"\U0001F600", L"\U0001F601", L"\U0001F602", L"\U0001F923",
    L"\U0001F60A", L"\U0001F60D", L"\U0001F618", L"\U0001F60E",
    L"\U0001F914", L"\U0001F60F", L"\U0001F634", L"\U0001F62D",
    L"\U0001F621", L"\U0001F973", L"\U0001F605", L"\U0001F643",
    L"\U0001F609", L"\U0001F622", L"\U0001F631", L"\U0001F97A",
    L"\U0001F929", L"\U0001F61C", L"\U0001F644", L"\U0001F62C",
    L"\U0001F44D", L"\U0001F44E", L"\U0001F44C", L"\U0001F44F",
    L"\U0001F64F", L"\U0001F4AA", L"\U0001F91D", L"\U0000270C",
    L"\U00002764", L"\U0001F494", L"\U0001F389", L"\U0001F38A",
    L"\U0001F525", L"\U00002B50", L"\U00002728", L"\U0001F4AF",
    L"\U0001F381", L"\U00002615", L"\U0001F339", L"\U0001F31F",
    L"\U0001F440", L"\U0001F4A4", L"\U0001F917", L"\U0001F4A9",
};
static const int kEmojiCount = (int)(sizeof(kEmojis) / sizeof(kEmojis[0]));

// 网格布局参数（逻辑像素）。
static const int kCols      = 8;    // 每行列数
static const int kCellSize  = 30;   // 单元格边长
static const int kCellGap   = 2;    // 单元格间距
static const int kGridPad   = 8;    // 网格四周内距
static const int kEmojiPt   = 15;   // emoji 字号（磅）—— 比正文大，便于辨认

// emoji 单元格控件 id 基址：第 i 个 emoji 的 ctrlId = kCellIdBase + i。基址取一个
// 与其它控件 id 不冲突的较大值即可（弹窗内唯一）。
static const UINT kCellIdBase = 9100;

// =======================================================================
// 构造 / 析构
// =======================================================================

CEmojiPickerPopup::CEmojiPickerPopup()
    : m_pickCb(NULL)
    , m_pickUd(NULL)
    , m_built(false)
{
}

CEmojiPickerPopup::~CEmojiPickerPopup()
{
    // 控件树由 DuiHost 基类析构释放；本类只持映射，无需额外清理。
}

void CEmojiPickerPopup::SetPickCallback(PickCallback cb, void* userdata)
{
    m_pickCb = cb;
    m_pickUd = userdata;
}

// =======================================================================
// 控件树构建
// =======================================================================

void CEmojiPickerPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kGridPad);
    root->SetGap(kCellGap);

    // 逐行铺单元格：每行一个 DuiHBox，行内 kCols 个 DuiButton。
    int idx = 0;
    while (idx < kEmojiCount)
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kCellGap);

        for (int col = 0; col < kCols; ++col)
        {
            if (idx >= kEmojiCount)
            {
                // 末行不足一整行：补一个空标签作弹簧，把已有格子左对齐。
                std::unique_ptr<balloonwjui::DuiLabel> spacer(new balloonwjui::DuiLabel());
                row->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));
                break;
            }

            UINT cellId = kCellIdBase + (UINT)idx;
            m_cellEmoji[cellId] = std::wstring(kEmojis[idx]);

            std::unique_ptr<balloonwjui::DuiButton> cell(new balloonwjui::DuiButton());
            cell->SetButtonType(balloonwjui::DuiButton::StylePushButton);
            // Text 变体：透明底、无边、hover 才有浅底 —— 适合做无框 emoji 格子。
            cell->SetVariant(balloonwjui::DuiButton::Variant::Text);
            cell->SetCtrlId(cellId);
            cell->SetText(kEmojis[idx]);
            cell->SetTextPointSize(kEmojiPt, false);
            row->AddChild(std::move(cell), balloonwjui::DuiLayout::Hint().Fixed(kCellSize));

            ++idx;
        }

        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kCellSize));
    }

    SetContent(std::move(root));

    // 面板尺寸：宽 = 列数*格宽 + 列间距 + 左右内距；高 = 行数*格高 + 行间距 + 上下内距。
    int rows = (kEmojiCount + kCols - 1) / kCols;
    int w = kCols * kCellSize + (kCols - 1) * kCellGap + 2 * kGridPad;
    int h = rows * kCellSize + (rows - 1) * kCellGap + 2 * kGridPad;
    SetSize(w, h);
    // 工具栏在窗口底部，故面板优先朝上弹出（越界时 DuiPopupHost 自动翻边）。
    SetEdge(balloonwjui::DuiPopupHost::EdgeAbove);

    m_built = true;
}

// =======================================================================
// 打开
// =======================================================================

void CEmojiPickerPopup::OpenAt(const RECT& anchorScreen, HWND /*owner*/)
{
    BuildContentOnce();

    // owner 故意传 NULL：DuiHost::SendNotify 以 GetParent(m_hWnd) 为通知目标，
    // 传 owner 会让子控件点击通知被发到单聊窗而非本弹窗；传 NULL 让通知自路由
    // 回本弹窗的 OnChildNotify（与搜索弹窗同一处理）。
    Show(anchorScreen, NULL);
    LOG_INFO(_T("emoji 选择面板已打开。"));
}

// =======================================================================
// 子控件通知
// =======================================================================

LRESULT CEmojiPickerPopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 某 emoji 单元格被点击 → 据 ctrlId 取回 emoji 串回调（面板保持打开，便于
    // 连续选；关闭交给 ESC / 点窗口外失焦）。
    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        std::map<UINT, std::wstring>::iterator it = m_cellEmoji.find(pNotify->ctrlId);
        if (it != m_cellEmoji.end())
        {
            if (m_pickCb != NULL)
            {
                m_pickCb(m_pickUd, it->second.c_str());
            }
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}
