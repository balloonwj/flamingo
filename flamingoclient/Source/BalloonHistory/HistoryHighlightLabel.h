/**
 *  带关键字高亮的文本标签，HistoryHighlightLabel.h
 *
 *  「消息记录」窗列表里的内容预览用它渲染：搜索命中的那几段画一层淡黄底，其余照旧。
 *  搜出一屏结果之后，用户一眼就能看出每行是哪个词命中的，不必自己再扫一遍。
 *
 *  存在理由 —— balloonui 的 DuiLabel 整段只有一个颜色，画不出"某几段带底色"。本类按
 *  仓库 UI 约定走"子类化扩展"这一档：继承 DuiLabel，只在它画文字<u>之前</u>补几块底色
 *  矩形，文字排版 / 字体 / 对齐 / 省略号仍旧完全交给基类，不改 balloonui。
 *  （DuiLabel 自带的选区高亮是给鼠标拖选用的私有实现，接口上够不着，也不是一回事。）
 *
 *  典型用法：
 *      auto lbl = std::make_unique<HistoryHighlightLabel>();
 *      lbl->SetText(_T("看下这个季度报告"));
 *      lbl->SetHighlightRanges(FindHighlightRanges(text, keyword));   //见 HistoryView.h
 *
 *  只支持<u>单行</u>（消息记录的预览本就是单行截断）。SetWordWrap(true) 时底色矩形按
 *  单行算，会与实际折行对不上，故不要在折行模式下用本类。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#pragma once

#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "HistoryView.h"   // HighlightRange

namespace balloonhistory {

/**
 *  HistoryHighlightLabel —— 可把若干字符区间画成底色的单行文本标签。
 */
class HistoryHighlightLabel : public balloonwjui::DuiLabel
{
public:
    HistoryHighlightLabel();

    /**
     *  设置要高亮的字符区间（UTF-16 字符口径，与 FindHighlightRanges 的产出同源）。
     *  越界或长度非正的区间会被忽略，不必由调用方预先过滤。
     *    ranges：区间列表；传空表示不高亮。
     *  副作用：触发重绘。
     */
    void SetHighlightRanges(const std::vector<HighlightRange>& ranges);

    /**
     *  设置高亮底色。默认淡黄 RGB(0xFF, 0xF1, 0xA8)。
     *  副作用：触发重绘。
     */
    void SetHighlightColor(COLORREF color);

    /**
     *  DuiControl 覆写：先铺高亮底色，再交给 DuiLabel 画文字。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    //按当前字体与对齐方式算出各高亮区间在本控件客户区里的矩形，逐块填色。
    //测量用的字体与基类绘制时用的必须是同一个，否则底色会与文字错位 ——
    //故这里同样走 GetFont()，为空时退回 DuiResMgr 的默认字体（与基类一致）。
    void PaintHighlights(HDC hdc);

private:
    std::vector<HighlightRange> m_ranges;  //待高亮的字符区间；空表示不高亮
    COLORREF                    m_color;   //高亮底色
};

} // namespace balloonhistory
