/**
 *  共享浏览窗口的文件列表：在 balloonui 的虚拟列表上补齐多选与拖动检测，ShareFileList.h
 *
 *  第 2 期第 5 步（docs/文件共享方案.md 7.4）。DuiVirtualList 只有单选、单击即改选中行，也不检测拖动；浏览窗口要的是
 *  资源管理器式的多选（Ctrl / Shift）与「按下后移动一段距离即开始拖出」。按 UI 约定第 2 条子类化它、覆写最少的虚函数：
 *    - 左键按下：不调用基类（基类会改它自己的选中行），把按下的位置与 Ctrl / Shift 交给监听者，由监听者维护选择集；
 *      同时捕获鼠标，移出窗口也能继续检测；
 *    - 移动：先交给基类（悬停的底色、滚动条淡入），按着左键且离按下的位置超过系统的拖动阈值（SM_CXDRAG / SM_CYDRAG）
 *      时释放捕获并通知监听者开始拖动，一次按下只通知一次；
 *    - 左键松开：没有开始拖动时通知监听者（推迟到松开时才改的选择在这时生效）；
 *    - 双击：交给监听者；
 *    - 按键：先交给监听者，监听者不处理的再交给基类。
 *  选中的底色由浏览窗口的画行回调按自己的选择集画，基类的选中行不再使用。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *  单元测试见 FlamingoFileShare/Tests/ShareFileListTests.cpp（标签 [sharedrag]）。
 *
 *  典型用法（浏览窗口建控件树时）：
 *      std::unique_ptr<CShareFileList> list(new CShareFileList());
 *      list->SetListener(this);
 *      list->SetPaintRowCallback(&PaintRowThunk, this);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"

namespace fileshare {

/**
 *  文件列表的监听者：浏览窗口实现。坐标都是宿主窗口的客户区坐标；全部回调在 UI 线程上。
 */
class IShareFileListListener
{
public:
    virtual ~IShareFileListListener()
    {
    }

    /**
     *  左键按下（在行上或空白处）。
     *  @param pt      按下的位置
     *  @param mkFlags 鼠标消息的按键标志（MK_CONTROL、MK_SHIFT 等）
     */
    virtual void OnFileListMouseDown(POINT pt, UINT mkFlags) = 0;

    /**
     *  左键松开，且这次按下没有开始拖动。
     *  @param pt      松开的位置
     *  @param mkFlags 按键标志
     */
    virtual void OnFileListMouseUp(POINT pt, UINT mkFlags) = 0;

    /**
     *  按下后移动超过拖动阈值：开始拖动。回调时列表已释放鼠标捕获；之后不会再收到这次按下的 OnFileListMouseUp。
     *  @param pressPt 按下时的位置
     */
    virtual void OnFileListDragStart(POINT pressPt) = 0;

    /**
     *  双击。
     *  @param pt 双击的位置
     */
    virtual void OnFileListDoubleClick(POINT pt) = 0;

    /**
     *  列表有焦点时按了键。
     *  @param vk 虚拟键码
     *  @return 已处理返回 true；返回 false 时交给虚拟列表的默认处理
     */
    virtual bool OnFileListKeyDown(UINT vk) = 0;
};

/**
 *  CShareFileList —— 支持多选与拖动检测的虚拟列表。只在 UI 线程使用；归控件树所有。
 */
class CShareFileList : public balloonwjui::DuiVirtualList
{
public:
    CShareFileList();

    /**
     *  设置监听者。
     *  @param listener 监听者（不拥有，须在本控件销毁之前一直有效），可为 NULL（此时鼠标与按键只交给基类）
     */
    void SetListener(IShareFileListListener* listener);

    /**
     *  按下后离按下位置多远算开始拖动（测试用；默认取系统的 SM_CXDRAG / SM_CYDRAG）。
     *  @param cx 横向阈值（像素，大于 0）
     *  @param cy 纵向阈值（像素，大于 0）
     */
    void SetDragThreshold(int cx, int cy);

    //是否正处于「左键已按下、尚未开始拖动也未松开」的状态（测试用）
    bool IsPressed() const;

    //DuiControl 覆写：见文件头的说明
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    bool OnKeyDown(UINT vk, UINT flags) override;

private:
    //释放鼠标捕获（还在捕获时）
    void ReleaseMouse();

private:
    IShareFileListListener* m_listener;   //监听者（不拥有）；可为 NULL
    bool                    m_pressed;    //左键已按下、尚未开始拖动也未松开
    POINT                   m_pressPt;    //按下的位置（宿主客户区坐标）
    int                     m_dragCx;     //横向拖动阈值（像素）
    int                     m_dragCy;     //纵向拖动阈值（像素）
};

} // namespace fileshare
