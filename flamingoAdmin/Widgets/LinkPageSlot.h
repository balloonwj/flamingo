/**
 *  flamingoAdmin —— FlamingoLink.dll 页面窗口的占位控件。
 *
 *  企业详情页「在线用户」页签的内容不是 balloonui 控件，而是 FlamingoLink.dll 建的一个原生子窗口
 *  （docs/FlamingoLink方案.md 第 8 节）。本控件放在控件树里占住那块区域：它从空的竖直布局容器
 *  子类化而来，本身不画任何东西，只覆写排版，在每次排版时把自己的矩形（宿主客户区坐标，也就是
 *  主窗口客户区坐标）报给注册的回调，由控制台转交 DLL 去移动页面窗口。
 *  页面窗口显示与否不由本控件决定，见 AdminFrame::SyncLinkPage。
 *
 *  典型用法：
 *      auto slot = std::make_unique<ui::LinkPageSlot>();
 *      slot->SetBoundsCallback(&MyHost::OnSlotBounds, this);
 *      parent->AddChild(std::move(slot));
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"

namespace flamingoadmin { namespace ui {

// FlamingoLink.dll 页面窗口的占位控件。
class LinkPageSlot : public balloonwjui::DuiVBox
{
public:
    // 排版回调。
    //   user：SetBoundsCallback 时传入的指针。
    //   rc：本控件排版后的矩形（宿主客户区坐标）。
    typedef void (*BoundsFn)(void* user, const RECT& rc);

    LinkPageSlot();

    // 注册排版回调。fn 为空表示不通知。
    //   user：原样传回，所有权归调用方。
    void SetBoundsCallback(BoundsFn fn, void* user);

    // 最近一次排版得到的矩形（宿主客户区坐标）；尚未排版时为空矩形。
    const RECT& LastBounds() const { return m_rcItem; }

    // 覆写排版：先按基类排版，再把矩形报给回调。
    //   rcAvail：父控件分给本控件的矩形。
    void Layout(const RECT& rcAvail) override;

private:
    BoundsFn m_onBounds;      // 排版回调，可为空
    void*    m_onBoundsUser;  // 排版回调的用户数据，所有权归调用方
};

}} // namespace flamingoadmin::ui
