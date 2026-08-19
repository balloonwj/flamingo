/**
 *  BalloonAboutDlg.h
 *
 *  "关于 FlamingoIM"对话框 —— 对照 design/关于.png，基于 balloonui DuiFrameWindow。
 *  自上而下：放大的 app 自身图标（IDR_MAINFRAME）+ 应用名 + 版本/Build + 标语；
 *  一条分隔线；五个功能行（检查更新 / 用户协议 / 隐私政策 / 开源许可 / 官方网站，
 *  行间分隔线）；底部版权页脚。两处特殊点：① 品牌图标用 app 自身图标，不用设计稿
 *  那个橙色色块；② 标题栏标题带当前进程 id，形如「关于 FlamingoIM [12345]」。
 *
 *  本期交互（已与用户确认）：
 *    · "官方网站"按钮 → ShellExecute 打开 flamingo.ini 的 [ui] websiteurl 配的网址
 *      （该键缺失时回退 AboutDlgModel.h 的默认 URL）；行上显示的就是这个网址原文。
 *    · "检查更新"按钮 → 提示"当前已是最新版本"（v2 自动更新后端已推迟）。
 *    · "用户协议 / 隐私政策 / 开源许可" → 提示"功能开发中"（占位）。
 *
 *  与主面板的关系：主面板系统菜单"关于 FlamingoIM"项点击时创建并显示本对话框；
 *  命令行调试入口另持一份实例供 --about 使用（跳过登录直接预览）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（经 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui { class DuiVBox; }

/**
 *  CBalloonAboutDlg —— "关于"对话框（非模态浮窗，owner = 主面板）。
 *  纯展示 + 几个外链 / 占位按钮，不依赖任何业务对象，故构造无参。
 */
class CBalloonAboutDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonAboutDlg();
    ~CBalloonAboutDlg();

    /**
     *  创建并显示对话框（已显示则前置）。每次打开都按当前进程 id 重置标题。
     *    owner：owner 窗口句柄（主面板）；内部 Create 传 NULL 让子控件
     *           通知自路由回本对话框（与改密 / 建群对话框同处理），owner 仅作语义留存。
     */
    void OpenAbout(HWND owner);

    BEGIN_MSG_MAP(CBalloonAboutDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 构建一次静态骨架（品牌区 + 五个功能行 + 页脚）。
    void BuildUiOnce();

    // 从 flamingo.ini 的 [ui] websiteurl 读"官方网站"地址填进 m_websiteUrl。
    // 该键缺失 / 读到空串时回退到 AboutDlgModel.h 的 kAboutWebsiteUrl。
    // 每次打开对话框都会重读一次（窗口关闭即销毁、重开会重建客户区），
    // 所以改完 ini 不重启程序也能生效。
    void LoadWebsiteUrlFromIni();

    // 往 root 加一条横向分隔线（1px）。
    void AddSeparator(balloonwjui::DuiVBox* root);

    // 往 root 加一行"居中文本"标签（品牌区的应用名 / 版本 / 标语 / 页脚版权用）。
    //   text   显示文本；font 字体（caller / DuiResMgr 持有，可为 NULL 走默认）；
    //   color  文字色；height 行高（逻辑像素）。
    void AddCenteredLabel(balloonwjui::DuiVBox* root, LPCTSTR text,
                          HFONT font, COLORREF color, int height);

    // 往 root 加一个"左标题 + 右链接式按钮"功能行（用户协议 / 隐私政策 / 开源许可 /
    // 官方网站）。链接式按钮走 DuiButton 的 Text 变体（橙字、无底无边、hover 浅底）。
    //   title     左侧标题文本；linkText 右侧按钮文本；
    //   ctrlId    右侧按钮命令 id（点击经 DUIN_CLICK 回到 OnDuiNotify 分发）；
    //   linkWidth 右侧按钮的<u>最小</u>宽度（逻辑像素）。实际宽度按 linkText 量出来的
    //             宽度撑开，故不必也不能靠它来"留够位置"；它只管住文字很短时按钮
    //             不要缩得各行右缘参差不齐。撑开后仍会受左侧标题最小宽度的限制，
    //             真放不下时由 DT_END_ELLIPSIS 在尾部收成省略号。
    void AddLinkRow(balloonwjui::DuiVBox* root, LPCTSTR title,
                    LPCTSTR linkText, UINT ctrlId, int linkWidth);

    // 往 root 加特殊的"检查更新"行：左两行（标题 + "当前已是最新版本"副标题），
    // 右一个普通描边按钮"检查"。
    void AddCheckUpdateRow(balloonwjui::DuiVBox* root);

private:
    bool m_built;          // 骨架是否已构建（跨"关闭再打开"只建一次）

    // 标题栏左上角小图标位图（app 图标 16x16 合成到标题栏底色上的 HBITMAP）。
    // DuiFrameWindow::SetIcon 仅借用，故由本类持有、析构时 DeleteObject。
    HBITMAP m_titleIconBmp;

    // 品牌区放大显示的 app 图标位图（app 图标 64x64 合成到对话框白底上的 HBITMAP）。
    // DuiAvatar::SetBitmap 仅借用，故由本类持有、析构时 DeleteObject。
    HBITMAP m_appIconBmp;

    // "官方网站"行的地址，来自 flamingo.ini 的 [ui] websiteurl（读不到则用
    // kAboutWebsiteUrl 兜底）。这一个串同时用作行右侧的显示文本和点击时
    // ShellExecute 打开的目标，故必须带协议头。每次打开对话框重读。
    CString m_websiteUrl;
};
