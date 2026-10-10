/**
 *  SecretImageOverlay.h
 *
 *  密聊图片的大图浮层（docs/密聊方案.md 4.10；做法由用户 2026-10-07 选定为「聊天窗内浮层」）。
 *  挂在单聊窗客户区根容器的末尾，显示时铺满整个客户区、盖一层半透明深色底，中间是「只看不存」模式的看图画布
 *  （复用看图窗的 ImageCanvas：滚轮缩放、拖动平移、1:1、适应窗口、旋转；没有另存、复制、转发、识别文字），
 *  顶部一行提示「按 Esc 或点击空白处关闭」。浮层与聊天窗是同一个窗口，随聊天窗的防截图设置一起受保护；
 *  不另开窗口、不写任何文件，图片字节只交给画布解码成位图，关闭时释放。
 *
 *  典型用法（单聊窗）：
 *      std::unique_ptr<balloonchatwnd::SecretImageOverlay> overlay(new balloonchatwnd::SecretImageOverlay());
 *      m_secretImageOverlay = overlay.get();
 *      rootBox->AddChild(std::move(overlay), balloonwjui::DuiLayout::Hint().Fixed(0));
 *      m_secretImageOverlay->Open(bytes);   // 双击密聊图片时
 *      m_secretImageOverlay->Close();       // 退出密聊时
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonimageviewer { class ImageCanvas; }
namespace balloonwjui { class DuiLabel; }

namespace balloonchatwnd {

/**
 *  SecretImageOverlay —— 单聊窗里查看密聊图片的浮层控件。平时隐藏、不参与命中；Open 之后铺满父容器并取得键盘焦点。
 */
class SecretImageOverlay : public balloonwjui::DuiControl
{
public:
    SecretImageOverlay();
    ~SecretImageOverlay() override;

    /**
     *  显示一张图片。
     *  @param bytes 整张图片文件的字节（PNG / JPEG 等）；画布解码后不再引用，调用方随后可以清零
     *  @return 解码成功并已显示返回 true；解码失败时浮层保持隐藏，返回 false
     *  副作用：重排自身、取得键盘焦点（Esc 关闭）、重绘宿主窗口
     */
    bool Open(const std::vector<unsigned char>& bytes);

    /**
     *  关闭：释放画布上的位图并隐藏，焦点交还给 SetRestoreFocus 登记的控件。没有打开时什么都不做。
     */
    void Close();

    /**
     *  浮层当前是否打开着。
     */
    bool IsOpen() const;

    /**
     *  登记关闭后把键盘焦点交还给哪个控件（通常是输入框）。
     *  @param ctrl 控件（借用，不拥有）；NULL 表示不交还
     */
    void SetRestoreFocus(balloonwjui::DuiControl* ctrl);

    // ---- DuiControl 覆写 ----

    // 忽略父容器分给的矩形（按 Fixed(0) 只有 0 高），始终铺满父容器的完整矩形，画布按四周留白摆在中间
    void Layout(const RECT& rcAvail) override;
    // 先盖一层半透明深色底，再画子控件（画布与提示行）
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    // 按在画布之外的空白处：抬起时仍在空白处就关闭
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    // 滚轮落在画布之外：吞掉，不让下面的消息列表滚动
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    // Esc 关闭
    bool OnKeyDown(UINT vk, UINT flags) override;
    // 需要宿主窗口的 Win32 焦点才能收到 Esc
    bool NeedsWin32Focus() const override { return true; }

private:
    // 画布上单击了图片以外的空白处（ImageCanvas::BlankClickFn）：关闭浮层。ctx 为本控件
    static void OnCanvasBlankClick(void* ctx);

private:
    balloonimageviewer::ImageCanvas* m_canvas;      // 只看不存模式的看图画布（子控件，所有权在控件树）
    balloonwjui::DuiLabel*           m_hint;        // 顶部提示行「按 Esc 或点击空白处关闭」（子控件，所有权在控件树）
    balloonwjui::DuiControl*         m_restoreFocus; // 关闭后交还焦点的控件（借用）；NULL 表示不交还
    bool                             m_pressed;     // 左键是否按在画布之外的空白处、尚未抬起
};

} // namespace balloonchatwnd
