/**
 *  flamingoAdmin —— 跨界面复用的业务控件与绘制 / 数据辅助。
 *
 *  Phase 3 起，仪表板、企业管理等界面共享一批「设计稿同款」控件与小工具：
 *    · PageHeader  —— 页头（eyebrow + 标题 + 副标题 + 右侧操作按钮区）。
 *    · StatTile    —— 统计卡（标签 + 大号数值 + 变化量）。
 *    · SegControl  —— 分段筛选控件（一排互斥的小标签）。
 *    · CardBox     —— 白色圆角卡片容器（在 DuiVBox 上加卡片底）。
 *  以及一组自绘辅助函数（卡片底、徽章、级别圆点、头像）与数据辅助函数
 *  （姓名缩写、头像配色、级别配色、状态文案）。
 *
 *  这些控件与函数全部在 balloonui 之外实现，仅「调用 / 子类化」balloonui，
 *  不改动 balloonui 库本身（遵循开发约定文档的 balloonui 改动审批规则）。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Controls/Layout/DuiLayout.h"
#include "Mock/MockData.h"

#include <memory>
#include <vector>

// balloonui 控件前置声明 —— StatTile 内部持有 3 个 DuiLabel 裸指针,所有权在
// DuiVBox 基类的 m_children 里;.cpp 再 include 完整的 DuiLabel 头。
namespace balloonwjui {
class DuiLabel;
}

namespace flamingoadmin { namespace ui {

// ===========================================================================
// 强调色：统计卡的「变化量」文字与部分徽章按此着色。
// ===========================================================================
enum class Accent
{
    None,   // 无强调：用中性灰
    Ok,     // 正向 / 成功：绿
    Warn,   // 警告：橙
    Info,   // 信息：蓝
};

// ---------------------------------------------------------------------------
// 数据辅助
// ---------------------------------------------------------------------------

// 取姓名 / 名称的「缩写」，用于头像。规则同设计稿 initials()：
//   · 含中文：长度 <= 2 取全部，否则取末 2 字；
//   · 纯英文：取前两个单词的首字母。
//   name：原始姓名 / 名称。返回：1~2 个字符的缩写；空名返回 "?"。
CString Initials(const CString& name);

// 由任意种子串确定性地派生一个头像底色（同设计稿 avatarColor()）。
//   seed：种子串（一般是姓名）。返回：8 色调色板中的一个。
COLORREF AvatarColor(const CString& seed);

// 取级别（Severity）对应的语义色（圆点 / 徽章用）。
COLORREF SeverityColor(Severity sev);

// 取强调色（Accent）对应的颜色。
COLORREF AccentColor(Accent accent);

// 取企业入驻状态的中文文案。
LPCTSTR EnterpriseStatusLabel(EnterpriseStatus status);

// ---------------------------------------------------------------------------
// 自绘辅助
// ---------------------------------------------------------------------------

// 画一张「卡片底」：白色圆角填充 + 1px 浅灰描边。
void DrawCard(HDC hdc, const RECT& rc);

// 画一个级别圆点（直径固定 8px），圆心在 (cx, cy)。
void DrawSeverityDot(HDC hdc, int cx, int cy, Severity sev);

// 量算一个徽章的宽度（像素）。
//   text：徽章文字；hasDot：是否在文字左侧带一个小圆点。
int ChipWidth(HDC hdc, LPCTSTR text, bool hasDot);

// 画一个徽章（圆角小标签）。
//   rc：徽章矩形（高度建议 20px，宽度用 ChipWidth 量算）。
//   textClr / bgClr：文字色 / 底色。
//   hasDot：是否带圆点；dotClr：圆点色（hasDot 为 false 时忽略）。
void DrawChip(HDC hdc, const RECT& rc, LPCTSTR text,
              COLORREF textClr, COLORREF bgClr, bool hasDot, COLORREF dotClr);

// 量算企业状态徽章的宽度（像素）。
int StatusChipWidth(HDC hdc, EnterpriseStatus status);

// 画一个企业状态徽章：左边缘 left、垂直中心 cy。返回徽章实际占用宽度。
int DrawStatusChip(HDC hdc, int left, int cy, EnterpriseStatus status);

// 画一个头像：rc 内圆角方块底色 + 居中白色缩写。
//   bgColor：底色；text：缩写文字（一般来自 Initials）。
void DrawAvatar(HDC hdc, const RECT& rc, COLORREF bgColor, LPCTSTR text);

// ===========================================================================
// PageHeader —— 页头。
//
// 左侧三行文字：eyebrow（品牌色小标签）+ 标题（大号）+ 副标题（灰色）。
// 右侧可放若干操作按钮（通过 AddAction 加入，从左到右排列、垂直居中）。
// ===========================================================================
class PageHeader : public balloonwjui::DuiControl
{
public:
    // 设置三行文字。subtitle 可为空字符串（则不画副标题）。
    void SetTexts(LPCTSTR eyebrow, LPCTSTR title, LPCTSTR subtitle);

    // 追加一个右侧操作控件（一般是 FlatButton）。
    //   action：操作控件（被 move 进来，所有权转移给本控件）。
    //   width：该控件的固定宽度（像素）。
    void AddAction(std::unique_ptr<balloonwjui::DuiControl> action, int width);

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    CString          m_eyebrow;       // 品牌色小标签
    CString          m_title;         // 主标题
    CString          m_subtitle;      // 副标题
    std::vector<int> m_actionWidths;  // 各操作控件的宽度，下标与子控件顺序一致
};

// ===========================================================================
// StatTile —— 统计卡。
//
// 卡片底 + 三行内容：标签（小灰字）/ 数值（大号深色）/ 变化量（小字，按
// Accent 着色）。
//
// 实现方式：StatTile 是 balloonwjui::DuiVBox 的子类，构造时把 4 个卡片样式
// setter (底色 / 圆角 / 描边色 / 描边宽度) 全部开起来——卡片底由 DuiVBox 基类
// 在 OnPaint 里自己画，本类不再 override OnPaint。三行内容是 3 个 DuiLabel
// 子控件，按 padding 16 / 上 14 + Fixed(15) / Fixed(30) marginTop 4 / Fixed(16)
// 自顶向下排版，完整复用 balloonui 的文字绘制与布局能力。
//
// 不可交互（不接收 hover / click）。
// API 与历史自绘版完全一致：SetContent 一个函数即可更新全部三行。
// ===========================================================================
class StatTile : public balloonwjui::DuiVBox
{
public:
    // 构造时把 4 个卡片样式 setter 全部开起来并就位 3 个 DuiLabel 子控件。
    StatTile();

    // 设置卡片内容。
    //   label：指标名（小灰字，恒可见）。
    //   value：指标值（已格式化的字符串；大号深色，恒可见）。
    //   delta：变化量描述；为空串则隐藏整个变化量行（DuiLabel::SetVisible(false)），
    //          后续再传非空 delta 时自动恢复显示。
    //   accent：变化量文字的强调色（仅 delta 非空时生效）。
    void SetContent(LPCTSTR label, LPCTSTR value, LPCTSTR delta, Accent accent);

private:
    // 3 个 DuiLabel 子控件的裸指针；所有权在 DuiVBox 基类的 m_children 里。
    // 由 StatTile() 构造期一次性 AddChild 进去，整生命周期不会被换掉。
    balloonwjui::DuiLabel* m_lblLabel = nullptr;   // 指标名（第 1 行，小灰字）
    balloonwjui::DuiLabel* m_lblValue = nullptr;   // 指标值（第 2 行，大号深色）
    balloonwjui::DuiLabel* m_lblDelta = nullptr;   // 变化量（第 3 行，按 Accent 着色，可隐藏）
};

// ===========================================================================
// SegControl —— 分段筛选控件。
//
// 一排互斥的小标签嵌在一个浅灰圆角槽里，选中项有白色凸起底。点击某段
// 即切换选中并发 DUIN_VALUECHANGED（extra = 段索引）。
// ===========================================================================
class SegControl : public balloonwjui::DuiControl
{
public:
    // 追加一个分段。须在挂到父之前按顺序全部加完。
    void AddSegment(LPCTSTR label);

    // 设置 / 读取当前选中段索引。
    void SetActiveIndex(int index);
    int  GetActiveIndex() const { return m_active; }

    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // 算出每个分段的矩形（host 客户区坐标）。供 OnPaint / OnLButtonUp 共用。
    void SegmentRects(std::vector<RECT>& out) const;

    std::vector<CString> m_labels;     // 各分段文字
    int                  m_active = 0; // 当前选中段索引
};

// 注:历史 CardBox 已删除 —— 改用 balloonwjui::DuiVBox + 4 个卡片样式 setter
//    (SetBgColor / SetCornerRadius / SetBorderColor / SetBorderWidth)。

}} // namespace flamingoadmin::ui
