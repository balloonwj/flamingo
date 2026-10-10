/**
 *  可点击的纯文本标签，ClickableLabel.h
 *
 *  balloonui 的 DuiLabel 只有链接模式（ModeLink）在点击时发 DUIN_CLICK，而链接模式一律画下划线、用链接色；普通文本模式
 *  （ModeText）的 OnLButtonUp 直接返回、不发通知（DuiLabel.cpp），宿主的点击又不向上冒泡（DuiHost.cpp），于是普通文本
 *  标签点了没有反应。本类把 OnLButtonUp 转回 DuiControl 的实现（命中且启用即向父控件发 DUIN_CLICK），外观保持普通
 *  文本（颜色、对齐照常由 SetTextColor / SetTextAlign 决定），悬停时显示手型光标。
 *
 *  用到它的地方：群资料窗口的群名片行（整行点击就地编辑，原先是 BalloonGroupInfoLayout.cpp 里的局部类，2026-10-03
 *  移到这里共用）、文件共享 DLL 发起共享对话框的成员名字（点名字等于点该成员的勾选框）。只依赖 balloonui，主程序与
 *  文件共享 DLL 都可以包含。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  典型用法：
 *      std::unique_ptr<balloonmain::CClickableLabel> name(new balloonmain::CClickableLabel());
 *      name->SetText(L"张三");
 *      name->SetCtrlId(kIdName);
 *      row->AddChild(std::unique_ptr<DuiControl>(name.release()), Hint().Weight(1));
 *      //父窗口的 OnDuiNotify：pNotify->code == DUIN_CLICK && pNotify->ctrlId == kIdName
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

namespace balloonmain {

/**
 *  CClickableLabel —— 可点击的纯文本标签：外观同普通 DuiLabel，点击时向父控件发 DUIN_CLICK（extra 为 0）。
 */
class CClickableLabel : public balloonwjui::DuiLabel
{
public:
    /**
     *  鼠标左键抬起：跳过 DuiLabel 对普通文本模式的抑制，交给 DuiControl 的实现——落点在控件内且控件可用时发 DUIN_CLICK。
     *  @param pt      鼠标位置（宿主客户区坐标）
     *  @param mkFlags 按键状态（MK_*）
     *  @return 已处理返回 true
     */
    bool OnLButtonUp(POINT pt, UINT mkFlags) override
    {
        return balloonwjui::DuiControl::OnLButtonUp(pt, mkFlags);
    }

    /**
     *  设置光标：悬停在控件上时显示手型光标，提示可以点击。
     *  @param pt 鼠标位置（宿主客户区坐标）
     *  @return 已设置返回 true；不在控件上返回 false，由别的控件处理
     */
    bool OnSetCursor(POINT pt) override
    {
        RECT rc = GetRect();
        if (::PtInRect(&rc, pt))
        {
            ::SetCursor(::LoadCursor(NULL, IDC_HAND));
            return true;
        }
        return false;
    }
};

} // namespace balloonmain
