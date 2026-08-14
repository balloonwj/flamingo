/**
 *  AvatarPickerTile.h
 *
 *  「修改头像」对话框里可点击 + 可选中的头像 / 色块磁贴。子类化 balloonui 的
 *  DuiAvatar（UI 约定第 2 条「次选：子类化扩展」）—— 复用其位图绘制与圆 / 圆角
 *  矩形裁剪能力承载一张预生成的 monogram 位图（系统头像磁贴）或纯色块（首字母
 *  底色选择器的色点），仅补两件 DuiAvatar 不提供的能力：
 *    · 点击：鼠标抬起时向父发 DUIN_CLICK（DuiAvatar 是纯绘制控件、不发通知）；
 *    · 选中态：选中时在外圈描一圈强调色边（设计稿里被选中的磁贴 / 色点有橙色环）。
 *
 *  系统头像磁贴与首字母色点共用本控件，避免给两处各写一份 ad-hoc 自绘控件
 *  （UI 约定：相似职责的控件抽到共用模块复用）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"

namespace balloonmain {

// 可点击 + 可选中的头像 / 色块磁贴（子类化 DuiAvatar）。
class AvatarPickerTile : public balloonwjui::DuiAvatar
{
public:
    AvatarPickerTile();
    ~AvatarPickerTile();

    /**
     *  设置磁贴位图，<b>本控件接管所有权</b>（析构或再次设置时 DeleteObject）。
     *  与 DuiAvatar::SetBitmap（仅借用、不持有）不同，这里收口位图生命周期，
     *  方便对话框侧"生成一张就丢给磁贴"。
     *  @param hbm 32bpp 预乘 alpha 位图；传 NULL 清空回到纯色 fallback。
     */
    void SetOwnedBitmap(HBITMAP hbm);

    /**
     *  设置选中态（仅变化时重绘）。选中时 OnPaint 会在外圈描强调色边。
     *  @param sel true 选中 / false 未选中。
     */
    void SetSelected(bool sel);
    bool IsSelected() const { return m_selected; }

    // 设置选中描边的强调色（默认品牌橙）。
    //   c：COLORREF。
    void SetAccentColor(COLORREF c) { m_accent = c; }

    // ---- DuiControl 覆写 ----

    // 抬起即点击：向父发 DUIN_CLICK（父按 ctrlId 区分是哪枚磁贴 / 色点）。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // 先让基类画头像 / 色块，选中时再叠画强调色描边环。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    HBITMAP  m_owned;      // 本控件持有的位图（NULL = 未持有）
    bool     m_selected;   // 是否选中
    COLORREF m_accent;     // 选中描边强调色
};

} // namespace balloonmain
