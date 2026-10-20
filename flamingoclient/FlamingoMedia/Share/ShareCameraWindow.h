/**
 *  屏幕共享发起人一侧的摄像头浮动窗口，ShareCameraWindow.h
 *
 *  发起人的屏幕正被共享，摄像头小窗不能占用被共享的画面，所以放在一个单独的小窗口里：贴在被共享显示器工作区的
 *  右边缘，置顶、不抢焦点、不进任务栏，并排除在一切屏幕采集之外（docs/屏幕共享与录制方案.md 5.2 第 3 步）。
 *  房间里有人开摄像头（含本人）时显示，都关掉后隐藏。内容是 CShareCameraPanel。
 *
 *  2026-10-12 起标题栏的 X 是「收起」（docs/远程桌面控制权限与观看工具条方案.md 第 2.2 节 D）：原来点 X 只把窗口藏起来，
 *  要等摄像头清单变化才重新出现，期间找不回来。现在点 X 缩成贴在右边缘的小把手（摄像头图标加人数），点把手展开；
 *  收起状态在这一场共享里一直保持（摄像头全部关掉又有人打开时仍是把手），Close 之后的下一场共享从展开开始。
 *  收起、展开都要换掉窗口内容（点的控件自己就在里面），按开发约定文档「UI 约定」第 13 条先投给自己再执行。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <vector>
#include "ShareCameraPanel.h"
#include "ShareBarButtonArt.h"   // ShareBarButtonArtCache：小把手按钮的底图（2026-10-12）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

namespace balloonwjui
{
    class DuiButton;
}

namespace screenshare {

//发起人一侧的摄像头浮动窗口
class CShareCameraWindow : public balloonwjui::DuiFrameWindow
{
public:
    //小把手按钮的控件 id（对外可见，单元测试按它找按钮；避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）
    enum
    {
        kIdHandle = 20,   //收起后的小把手：摄像头图标加人数，点它展开
    };

    //投给自己的「收起 / 展开」（2026-10-12）：wParam 为 1 收起、0 展开；等标题栏 X 或把手的点击处理完再换窗口内容
    //（窗口私有消息，登记见开发约定文档「UI 约定」第 6 条）
    static const UINT kMsgCameraCollapse = WM_APP + 0xD8;

    CShareCameraWindow();
    ~CShareCameraWindow();

    //画面来源（屏幕共享控制器）；不拥有
    void SetSource(IShareCameraFrames* source);

    /**
     *  更新要显示的小窗：有内容时显示（贴在 monitorRect 所在显示器工作区的右边缘），没有时隐藏。收起状态下只更新把手上的人数。
     *  @param tiles         小窗（含本人）
     *  @param overflowCount 超出同屏上限的人数
     *  @param monitorRect   被共享内容的矩形，窗口贴在它所在的显示器上
     */
    void Update(const std::vector<ShareCameraTile>& tiles, int overflowCount, const RECT& monitorRect);

    //关闭并销毁窗口，收起状态复位（下一场共享从展开开始）；可重复调用
    void Close();

    //当前是否收起成小把手（2026-10-12）
    bool IsCollapsed() const;

    BEGIN_MSG_MAP(CShareCameraWindow)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgCameraCollapse, OnCollapseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //标题栏的 X：不关窗也不隐藏，投给自己收起成小把手（摄像头仍开着）
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //点了小把手：投给自己展开
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //kMsgCameraCollapse：切换收起 / 展开并重新摆放
    LRESULT OnCollapseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void EnsureWindow();
    //按 m_collapsed 装上窗口内容（小窗面板或小把手）、设好标题栏，并摆到对应的位置
    void applyLayout();
    //把手上的人数（小窗个数加超出的人数）
    int peopleCount() const;

    CShareCameraPanel*  m_panel;    //小窗面板（控件树持有，本指针仅引用）；收起时为 NULL
    IShareCameraFrames* m_source;   //画面来源（不拥有）
    balloonwjui::DuiButton* m_handle;   //小把手按钮（控件树持有，本指针仅引用）；展开时为 NULL
    bool                m_collapsed;    //是否收起成小把手；Close 时复位
    std::vector<ShareCameraTile> m_tiles;   //最近一次 Update 的小窗清单，展开时据此重建面板
    int                 m_overflow;     //最近一次 Update 的超出人数
    RECT                m_expandedRect; //展开时的窗口矩形（屏幕坐标），Update 时算出
    ShareBarButtonArtCache m_art;       //小把手按钮的底图缓存（须比按钮活得久）
    HBITMAP             m_cameraIcon;   //小把手上的摄像头图标（本类生成、析构释放）
};

} // namespace screenshare
