/**
 *  FaceImagePickerPopup.h
 *
 *  单聊 / 群聊窗"表情"按钮点开后的图片表情选择浮层 —— 装 Bin/Face 目录下的
 *  PNG 表情（face0.png … face142.png，配 Face/FaceConfig.xml）。子类化 balloonui
 *  DuiPopupHost（无边框 WS_POPUP 浮层，自带 ESC / 失焦自动关闭），内部直接复用
 *  balloonui 现成控件 DuiEmojiPanel（图标网格 + hover + 点击）承载表情格子。
 *
 *  与旧的 Unicode emoji 浮层 CEmojiPickerPopup 的关系：本浮层<u>取代</u>它 ——
 *  「表情」按钮改弹本浮层（Bin/Face 图片表情），原 Unicode emoji 不再用。
 *
 *  点中某表情经回调把 (faceId, 文件绝对路径) 交回聊天窗，由聊天窗用
 *  DuiRichEdit::InsertTaggedImage 以"带 faceId 标记的内联图"插入输入框
 *  （所见即所得）；发送时再由聊天窗据该标记把表情内联进文本消息。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"
#include "../FaceList.h"   // CFaceList / CFaceInfo（表情库：id / tip / 文件路径）

#include <vector>

namespace balloonwjui { class DuiEmojiPanel; }

/**
 *  CFaceImagePickerPopup —— 图片表情选择浮层。
 */
class CFaceImagePickerPopup : public balloonwjui::DuiPopupHost
{
public:
    // 选中某表情时的回调。
    //   userdata：SetPickCallback 传入的上下文（一般为聊天窗 this）。
    //   faceId：  选中表情的 id（FaceConfig.xml 的 id，等于 faceN.png 的 N）。
    //   facePath：该表情 PNG 的绝对路径（供调用方 InsertTaggedImage 插入）。
    typedef void (*PickCallback)(void* userdata, int faceId, const wchar_t* facePath);

    CFaceImagePickerPopup();
    ~CFaceImagePickerPopup();

    // 设置"选中表情"回调。
    //   cb：回调函数指针；nullptr 取消。
    //   userdata：回调上下文（一般为聊天窗 this）。
    void SetPickCallback(PickCallback cb, void* userdata);

    // 在 anchor 屏幕矩形上方打开表情面板（工具栏在窗口底部，优先朝上弹）。
    //   anchorScreen：锚矩形（一般取「表情」按钮的屏幕矩形）。
    //   owner：owner 窗口句柄；仅用于生命周期归属，通知路由见 .cpp。
    void OpenAt(const RECT& anchorScreen, HWND owner);

    BEGIN_MSG_MAP(CFaceImagePickerPopup)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnPanelMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnPanelMouseLeave)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    // 鼠标在浮层内移动：按命中的表情格驱动 tooltip —— DuiToolTipMgr 是"控件级"
    // 注册（hover 控件变化才换文字），而本面板是一个控件内 143 个格子，格子间移动
    // 不触发宿主的 tooltip 派发；故由本浮层（自身即 DuiHost）按格子粒度补：格子
    // 变化时 OnLeave 关旧 tip → Register 新文字 → OnHover 重新计时弹出。
    // bHandled 置 FALSE 透传给基类（hover 高亮等既有派发不受影响）。
    LRESULT OnPanelMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 鼠标离开浮层：宿主基类已负责关 tip，这里只重置"上次 tip 格"记录。
    LRESULT OnPanelMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次打开时构建一次面板：加载表情库 + 逐个把 PNG 解码成 HBITMAP 喂给
    // DuiEmojiPanel，并按选中索引建立"索引 → faceId / 路径"映射。
    void BuildContentOnce();

    // DuiEmojiPanel 选中回调（静态 thunk）。userdata 为本浮层 this，index 为选中项
    // 在面板里的序号（与 m_faceInfos 同序）。
    static void OnPanelPick(void* userdata, const wchar_t* sequence, int index);

private:
    PickCallback m_pickCb;     // 选中回调
    void*        m_pickUd;     // 回调上下文
    bool         m_built;      // 控件树是否已构建

    CFaceList    m_faceList;   // 本浮层自有的表情库（从 Face/FaceConfig.xml 加载）

    // 面板每个格子对应的表情信息指针（与面板格子同序）。指针所有权在 m_faceList，
    // 本 vector 只引用、不释放。
    std::vector<CFaceInfo*> m_faceInfos;

    // 逐个表情解码出的 HBITMAP，供 DuiEmojiPanel 显示。DuiEmojiPanel 要求 icon 为
    // caller-owned 且比面板存活久，故由本浮层持有，析构时统一 DeleteObject。
    std::vector<HBITMAP>    m_faceBitmaps;

    // 面板控件裸指针（所有权在 DuiHost 内容树），供鼠标命中测试 / tooltip 注册。
    balloonwjui::DuiEmojiPanel* m_pPanel;

    // 上次显示 tooltip 的表情格索引（-1 = 无）。格子变化时才重新注册 / 计时，
    // 避免同格内移动反复重启 tooltip 延时。
    int m_lastTipIdx;
};
