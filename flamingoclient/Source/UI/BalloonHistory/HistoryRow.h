/**
 *  「消息记录」列表里的一行（可悬停 / 可双击），HistoryRow.h
 *
 *  在 DuiHBox 的基础上补三件事：
 *    · 鼠标进入时铺一层极淡的悬停底色，离开时撤掉；
 *    · 悬停时才显示行内的「Pin / 取消」「定位」两枚操作按钮 —— 常驻的话一屏八行
 *      就是十六枚按钮，右侧糊成一道"按钮墙"，而它们绝大多数时候都不被点；
 *    · 双击整行等同于点「定位」，给没注意到悬停按钮的用户留一条更快的路。
 *
 *  两个约定要留意：
 *    · 按钮<u>只切可见性、不让出位置</u>。位置由外部用固定宽度预留好，否则鼠标一进来
 *      预览文字就被挤短、看着抖一下。
 *    · 已 Pin 的行，它的「取消」按钮由外部设成常驻（`SetAlwaysVisibleButton`）——
 *      已经钉住是一个需要能一眼看见、且随时可撤的状态，藏起来反而找不到。
 *
 *  存在理由：balloonui 的 DuiHBox 只是布局容器，没有"悬停态"这一说。本类按仓库 UI
 *  约定走"次选：子类化扩展"这一档，覆写 OnMouseEnter / OnMouseLeave / OnLButtonDblClk
 *  三个虚函数补齐差异，**不改 balloonui**。
 *
 *  双击要真的触发，宿主窗须调过 DuiHost::EnableDoubleClick(true)。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#pragma once

#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonhistory {

/**
 *  HistoryPinMark —— 一枚小图钉，画在已 Pin 那一行的时间右边。
 *
 *  行内那枚常驻的「取消」按钮在最右侧，扫一列行时不容易看出"这条钉着"；把标记放到
 *  名字与时间旁边，纵向扫下来一眼就能挑出来。图形复用聊天气泡那套矢量图标
 *  （ChatIcons 的 kChatIconPin），两处形状一致。
 *
 *  图钉可以点击取消 Pin（2026-09-30 起）：单击由基类 DuiControl::OnLButtonUp 以本控件
 *  的 ctrlId 发 DUIN_CLICK，窗口据 id 认出是哪一行（见 ResolveHistoryPinClick）；
 *  光标停在上面时显示手型，并弹出「取消 Pin」悬停提示。
 */
class HistoryPinMark : public balloonwjui::DuiControl
{
public:
    //构造时向 DuiToolTipMgr 登记「取消 Pin」悬停提示（本控件只出现在已 Pin 的行上，
    //提示文字固定）。登记在基类析构时自动撤销。
    HistoryPinMark();

    //设置图钉颜色。默认暖琥珀 RGB(0xE6, 0x8A, 0x2E)，与聊天气泡上那枚同色。
    void SetIconColor(COLORREF color);

    //DuiControl 覆写：在本控件矩形内居中画一枚图钉。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    //DuiControl 覆写：把光标设为手形并返回 true，提示图钉可以点击。
    bool OnSetCursor(POINT pt) override;

    //DuiControl 覆写：忽略双击，返回 true 且不发任何通知。双击的第一下已经作为单击
    //发出了取消请求；若照基类那样再发 DUIN_DBLCLK，窗口会把它当成双击整行去定位，
    //取消 Pin 的同时窗口跳到聊天窗。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

private:
    COLORREF m_color;   //图钉颜色
};

/**
 *  HistoryRowThumb —— 行内左侧那一小块内容预览：图片画缩略图、文件画类型色块。
 *
 *  纯文字的 `[图片]` `[文件] xxx` 在一列里辨识度太低，扫过去全是方括号。这一小块
 *  让图片与文件在视觉上直接分开。
 *
 *  图片走 `ChatImageCache`（聊天气泡在用的那套后台解码 + 按"路径 + 展示尺寸"缓存）：
 *  命中就画，未命中画占位并请求后台解码，完成后由宿主窗收到 WM_CHAT_IMAGE_READY 重绘；
 *  解码失败（文件损坏）画破图。
 *
 *  本地缓存里没有原图时**不会自动下载** —— 消息记录窗是浏览界面，为一个 36px 的缩略图
 *  拉整张原图不划算。此时按窗口算好的状态（见 ImagePlaceholder.h 的 HistoryThumbState）
 *  画"需要下载 / 下载中 / 破图"占位；可点击时鼠标悬停底色加深、光标变手形，单击以本控件
 *  的 ctrlId 向窗口发 DUIN_CLICK，由窗口发起下载（2026-09-30 起）。
 */
class HistoryRowThumb : public balloonwjui::DuiControl
{
public:
    //本块画什么。
    enum Kind
    {
        kNone  = 0,   //不画（文本等消息）
        kImage = 1,   //图片缩略图
        kFile  = 2,   //文件类型色块
    };

    HistoryRowThumb();

    /**
     *  显示一张图片的缩略图或占位。
     *    path：     图片本地缓存文件的完整路径；可以为空（调试入口、消息缺少 MD5），
     *               此时 state 为 kHistoryThumbImage 也只铺底色。
     *    state：    显示状态，取值见 ImagePlaceholder.h 的 balloonchatwnd::HistoryThumbState。
     *    clickable：单击是否发起下载。为 true 时悬停加深底色、光标变手形，单击发 DUIN_CLICK。
     *  本控件不会自己触发下载，下载由窗口收到 DUIN_CLICK 后发起。
     */
    void SetImage(const std::wstring& path, int state, bool clickable);

    //图片缩略图的显示状态（HistoryThumbState 的取值）；非图片时返回 kHistoryThumbImage。
    int GetImageState() const;

    //单击本缩略图是否会发起下载。
    bool IsClickable() const;

    /**
     *  按扩展名显示文件类型色块。
     *    ext：小写扩展名（不含点），如 "xlsx"；空串时色块里不写字，仍画底色。
     */
    void SetFile(const CString& ext);

    //DuiControl 覆写：按当前 Kind 与图片状态画缩略图、占位或类型色块。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    //DuiControl 覆写：可点击时记下悬停态并重绘（底色加深、下载标记变深）。返回 true，
    //不再向窗口冒 DUIN_MOUSEENTER / DUIN_MOUSELEAVE。
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;

    //DuiControl 覆写：可点击时把光标设为手形并返回 true；否则返回 false 交给默认处理。
    bool OnSetCursor(POINT pt) override;

    //DuiControl 覆写：可点击且松开点仍在本控件内时，以本控件 ctrlId 发 DUIN_CLICK 并
    //返回 true；不可点击时什么都不发，返回 false。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    //DuiControl 覆写：双击转交给所在的行（父控件），使窗口收到的是行的 ctrlId ——
    //"双击这一行打开看图窗"在缩略图上同样有效。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

private:
    Kind         m_kind;        //本块画什么
    std::wstring m_path;        //kImage 时的图片本地路径；可以为空
    CString      m_ext;         //kFile 时的小写扩展名
    int          m_imageState;  //kImage 时的显示状态（HistoryThumbState 的取值）
    bool         m_clickable;   //单击是否发起下载
    bool         m_hot;         //鼠标是否正悬停在本控件上（仅可点击时记录）
};

/**
 *  HistoryRow —— 带悬停态与双击的消息记录行。
 */
class HistoryRow : public balloonwjui::DuiHBox
{
public:
    HistoryRow();

    /**
     *  登记本行里"悬停才显示"的操作按钮。可登记多枚，未登记的子控件不受影响。
     *    ctrl：按钮控件（借用，不持有；其生命周期由本行的子控件表保证）。
     *  调用后该按钮立即被隐藏，等鼠标进入本行才现身。
     */
    void AddHoverOnlyControl(balloonwjui::DuiControl* ctrl);

    /**
     *  设置悬停底色。默认极淡的暖灰 RGB(0xF6, 0xF4, 0xF0)。
     */
    void SetHoverBgColor(COLORREF color);

    // ---- DuiControl 覆写 ----

    /**
     *  置 / 撤悬停态：铺底色并显示（隐藏）登记过的操作按钮。
     *
     *  <u>由窗口按鼠标位置驱动，而不是靠 OnMouseEnter / OnMouseLeave</u>。原因：
     *  DuiHost::TrackHover 用 HitTopMost 只把 enter/leave 发给<u>最深</u>的那个控件，
     *  鼠标停在行内的预览文字上时，收到事件的是那个 DuiLabel，行容器永远收不到。
     *  也不能靠覆写 HitTest 把整行截下来 —— 那样鼠标从文字移到按钮上时，行会先收到
     *  leave 把按钮藏起来，按钮就在光标底下消失了。按几何位置判"点在不在这一行里"
     *  才不受子控件切换的影响。
     *
     *    hovered：true 进入悬停态。
     *  重复设同一个值不做任何事（不重绘）。
     */
    void SetHovered(bool hovered);

    //当前是否处于悬停态。
    bool IsHovered() const { return m_hovered; }

    /**
     *  置 / 撤"定位高亮"：「查看上下文」「跳到这一天」翻到这一行时，铺一层淡黄底色标出它，
     *  直到用户下一次操作。定位高亮比悬停底色优先。
     *    located：true 显示定位高亮。重复设同一个值不做任何事（不重绘）。
     */
    void SetLocated(bool located);

    //当前是否处于定位高亮。
    bool IsLocated() const { return m_located; }

    //双击整行：向宿主冒 DUIN_DBLCLK（带本行的 ctrlId），由窗口去定位该消息。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

    //悬停时先铺一层底色再交基类画子控件。
    //底色<u>自己画</u>而不是用容器的卡片底色 —— DuiLayout 的 SetBgColor 只长在
    //DuiVBox 上（那是"卡片容器"那套），DuiHBox 没有，为一层底色去改 balloonui 不值当。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    //统一设置登记过的那些按钮的可见性。
    void SetHoverControlsVisible(bool visible);

private:
    //"悬停才显示"的按钮清单（借用，不持有）。用裸指针是因为所有权在 DuiLayout
    //自己的子控件表里，本类只是记住"这几个要跟着悬停走"。
    std::vector<balloonwjui::DuiControl*> m_hoverOnly;
    COLORREF                              m_hoverBg;   //悬停底色
    bool                                  m_hovered;   //鼠标是否正停在本行上
    bool                                  m_located;   //是否处于定位高亮（见 SetLocated）
};

} // namespace balloonhistory
