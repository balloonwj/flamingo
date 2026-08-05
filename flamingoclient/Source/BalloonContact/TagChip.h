/**
 *  TagChip.h
 *
 *  联系人资料对话框「标签」区里的一枚标签胶囊（自绘控件）。对照设计稿
 *  dialogs.jsx ContactInfoDialog 的 tags 区：accent-soft 浅橙底、accent
 *  橙字、全圆角胶囊，胶囊内文字右侧带一个「×」删除按钮。
 *
 *  胶囊本身可点击 —— 当且仅当点击落在右侧「×」热区内时，向父窗口冒一个
 *  DUIN_CLICK 通知；父窗口据本控件的 ctrlId 定位是哪一枚标签并删除它。
 *  点在文字区不发通知（标签胶囊整体没有"打开"语义）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

namespace ballooncontact {

/**
 *  TagChip —— 一枚标签胶囊（自绘）。继承 DuiControl，自己画圆角胶囊 +
 *  文字 + 删除「×」，并自行 hit-test「×」热区。
 */
class TagChip : public balloonwjui::DuiControl
{
public:
    TagChip();

    // 设置 / 读取胶囊上的标签文字。SetTagText 会触发重绘。
    //   text：标签文本；nullptr 视为空。
    void    SetTagText(LPCTSTR text);
    CString GetTagText() const { return m_text; }

    // 计算胶囊在当前文字下的理想宽度（逻辑像素）—— 左右内边距 + 文字宽
    // + 间距 + 「×」字形宽。窗口按此值给胶囊布局，让换行流式排布精确。
    //   hdcMeasure：用于测量文字的 DC（可为窗口客户区 DC）。
    //   返回：胶囊理想宽度（像素），含两侧内边距。
    int     CalcDesiredWidth(HDC hdcMeasure) const;

    // ---- DuiControl 覆写 ----

    // 自绘胶囊（浅橙底 + 橙字 + 「×」）。
    void    OnPaint(HDC hdc, const RECT& rcDirty) override;

    // 鼠标按下：消费该事件，确保后续 OnLButtonUp 能稳定回到本控件
    //（host 的"按下 / 松开同一控件"配对逻辑依赖按下被消费）。
    //   pt：鼠标 host-客户区坐标；mkFlags：键鼠修饰位（未用）。
    //   返回：恒为 true（已消费）。
    bool    OnLButtonDown(POINT pt, UINT mkFlags) override;

    // 鼠标松开：若落在「×」热区内则向父冒 DUIN_CLICK（请求删除本标签）。
    //   pt：鼠标 host-客户区坐标；mkFlags：键鼠修饰位（未用）。
    //   返回：true 表示本控件已消费该事件。
    bool    OnLButtonUp(POINT pt, UINT mkFlags) override;

    // 普通箭头光标。
    bool    OnSetCursor(POINT pt) override { return false; }

private:
    // 计算「×」删除热区的矩形（host-客户区坐标）。胶囊未布局时返回空矩形。
    RECT    DeleteZoneRect() const;

private:
    CString m_text;   // 标签文字内容
};

} // namespace ballooncontact
