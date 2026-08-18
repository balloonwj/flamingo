/**
 *  AuthBanner.h
 *
 *  登录 / 注册窗顶部的橙色品牌 Banner + 头像区 —— balloonui 纯绘制控件。
 *
 *  视觉对应 design/login-screen.png：
 *   · 控件矩形上部为 135° 线性渐变 #FFB793 → #FF7A45 区；
 *   · 渐变区右侧一只白色描边的火烈鸟轮廓（约 0.85 不透明）；
 *   · 渐变区左下角 "FlamingoIM" 标题 + "让沟通像火烈鸟一样优雅" 标语；
 *   · 下部居中一枚"白色圆环 + 橙色圆角头像 + 白色首字母"，上移压住
 *     渐变区下沿；
 *   · 头像下方居中一行用户名 + 一颗"在线"状态胶囊。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonauth {

/**
 *  AuthBanner
 *
 *  品牌 + 头像区绘制控件。所有尺寸均按控件自身矩形（DuiControl::m_rcItem）
 *  等比推算，自适应 DPI 与窗口尺寸。头像下方的"在线状态胶囊"<u>可点击</u>：
 *  命中胶囊时发 DUIN_BANNER_STATUS_CLICK 通知，登录窗据此弹出在线状态菜单
 *  （在线 / 离开 / 忙碌），与主面板状态胶囊同套交互。
 */
class AuthBanner : public balloonwjui::DuiControl
{
public:
    // 头像下方"在线状态胶囊"被点击时发给父 host 的通知码 —— 登录窗据此弹出在线状态
    // 选择菜单。取 DUIN_CUSTOM 之后的值，避免与 balloonui 内置通知码冲突。
    enum { DUIN_BANNER_STATUS_CLICK = balloonwjui::DUIN_CUSTOM + 1 };

    AuthBanner();

    // 设置头像下方显示的用户名 —— 同时决定头像里的占位字母（取用户名
    // 第一个非空字符）。空字符串表示当前无账号，头像画通用人形占位、
    // 不显示用户名。
    //   name：用户名 / 账号文本；NULL 视为空。
    void SetUserName(LPCTSTR name);

    // 设置头像方块的底色 —— 登录窗用它表达"已登录账号按账号稳定取色、
    // 未登录账号用默认橙色"。仅改底色，首字母 / 白环 / 阴影等画法不变。
    //   color：头像方块填充色（RGB）。默认构造为主题橙 RGB(0xFF,0x7A,0x45)。
    void SetAvatarColor(COLORREF color);

    // 设置状态胶囊显示的在线状态（v2 presence_type，见 net/Protocol.h），决定胶囊
    // 圆点颜色与文字（在线绿 / 离开橙 / 忙碌红）。登录窗在用户从状态菜单选中后调用，
    // 随后须 Invalidate 重绘。默认在线。
    //   presence：v2 在线状态。
    void SetPresence(int presence);

    // 状态胶囊命中矩形（host 客户区坐标，由 OnPaint 按实际绘制位置缓存）。供登录窗把
    // 状态菜单锚到胶囊正下方。空矩形(全 0)表示尚未绘制。
    const RECT& GetPillHitRect() const { return m_rcPillHit; }

    // DuiControl 覆写：绘制渐变背景 + 火烈鸟 + 标题 + 头像 + 用户名 +
    // 状态胶囊。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：鼠标抬起时，命中状态胶囊则发 DUIN_BANNER_STATUS_CLICK
    // 触发"弹出在线状态菜单"；其余区域不触发。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    CString  m_userName;     // 头像下方用户名；空表示无账号
    COLORREF m_avatarColor;  // 头像方块底色；默认主题橙，可按账号改
    int      m_presence;     // 状态胶囊显示的在线状态(v2 presence_type)；默认在线
    RECT     m_rcPillHit;    // 状态胶囊命中矩形(host 客户区坐标)，供登录窗锚定状态菜单
};

} // namespace balloonauth
