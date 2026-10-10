/**
 *  P2PBadge.h
 *
 *  「P2P」徽章：数据经点对点直连传输时，在传输进度、通话与远程桌面等界面上显示的一枚绿色圆角小标签，
 *  让用户分清这一次是直连还是经服务器（docs/单聊文件P2P直传与P2P徽章方案.md 第 4 节）。
 *
 *  只在链路实际路径为局域网直连或公网打洞直连时显示，经 imgserver 一对一中转时不显示（方案决定 4）。
 *  本文件被主程序、FlamingoMedia.dll、FlamingoFileShare.dll 共同编译，只依赖 <windows.h>、GDI+ 与 balloonui：
 *    · 自绘的地方（聊天气泡、文件共享浏览窗的传输行）在自己的 HDC 上调用 DrawP2PBadge；
 *    · 用 balloonui 控件拼装的窗口用 MakeP2PBadgeControl 取一枚配置好的 DuiBadge 放进布局（UI 约定第 2 条：优先复用库里的控件）。
 *
 *  典型用法：
 *      if (balloonmain::ShouldShowP2PBadge(stats.m_path))
 *      {
 *          SIZE sz = balloonmain::MeasureP2PBadge(hdc, font);
 *          RECT rc = { x, y, x + sz.cx, y + sz.cy };
 *          balloonmain::DrawP2PBadge(hdc, rc, font);
 *      }
 *
 *  balloonwj@qq.com   2026-10-04
 */

#pragma once

#include <windows.h>
#include <atlstr.h>   //CString：DuiBadge.h 要用，本文件不经预编译头，自己引入
#include <memory>
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"   //胶囊标签控件

namespace balloonmain {

//徽章文字：协议名，各语言一致，不翻译
const wchar_t* const kP2PBadgeText = L"P2P";

//徽章底色（绿）与文字色（白）
const COLORREF kP2PBadgeBgColor = RGB(34, 160, 107);
const COLORREF kP2PBadgeTextColor = RGB(255, 255, 255);

//文字左右各留的空白（像素）
const int kP2PBadgePadX = 5;
//文字上下各留的空白（像素）
const int kP2PBadgePadY = 1;

/**
 *  判定某条链路路径要不要显示徽章。
 *  @param linkPath 链路实际路径，取值见 FlamingoP2PApi.h 的 p2p_link_path（1 局域网直连、2 打洞直连、3 中转）；
 *                  没有 P2P 链路时传 0 或负数
 *  @return 1、2 返回 true；中转、没有链路与其它取值返回 false
 */
bool ShouldShowP2PBadge(int linkPath);

/**
 *  量出徽章的外框尺寸：文字宽高加上左右各 kP2PBadgePadX、上下各 kP2PBadgePadY 的空白。
 *  @param hdc  用来量文字的设备上下文（不改动其中选入的对象）
 *  @param font 徽章文字的字体（借用，不释放）；NULL 时用 hdc 当前选入的字体
 *  @return 外框宽高（像素）；hdc 为 NULL 时返回 0×0
 */
SIZE MeasureP2PBadge(HDC hdc, HFONT font);

/**
 *  在 rc 里画一枚徽章：两端为半圆的绿色圆角矩形（抗锯齿），白字「P2P」水平、垂直居中。
 *  @param hdc  目标设备上下文
 *  @param rc   徽章外框（通常取 MeasureP2PBadge 的尺寸）
 *  @param font 文字字体（借用，不释放）；NULL 时用 hdc 当前选入的字体
 */
void DrawP2PBadge(HDC hdc, const RECT& rc, HFONT font);

//画面左上角叠加徽章时，与画面左边、上边的距离（像素）
const int kP2PBadgeOverlayMargin = 8;
//叠加徽章文字的字号（像素，加粗）
const int kP2PBadgeOverlayFontPx = 11;

/**
 *  在一块画面（远程桌面、屏幕共享的观看画面）的左上角叠加一枚徽章（2026-10-04 起）。字体用界面字体，调用方不用准备。
 *  画面放不下徽章时不画。
 *  @param hdc  目标设备上下文
 *  @param area 画面所在矩形；徽章画在 (left + kP2PBadgeOverlayMargin, top + kP2PBadgeOverlayMargin)
 */
void DrawP2PBadgeOverlay(HDC hdc, const RECT& area);

//balloonui 窗口里的徽章控件宽、高（像素）：DuiBadge 没有按文字量宽度的方法，放进水平布局时用这个固定宽度
const int kP2PBadgeControlWidth = 36;
const int kP2PBadgeControlHeight = 18;

/**
 *  生成一枚按徽章样式配置好的 balloonui 胶囊标签（文字「P2P」、绿底白字、两端半圆、不截断、不因文字为空而隐藏），
 *  供用 balloonui 控件拼装的窗口（远程桌面全屏工具条与受控提示条、通话窗底栏、屏幕共享控制条、文件共享窗口）使用。
 *  调用方把它放进布局（宽度取 kP2PBadgeControlWidth），按链路路径用 SetVisible 控制显隐，显隐变化后对父容器 ForceLayout
 *  （见开发约定文档「UI 约定」第 5 条）。
 *  @return 新建的控件，所有权归调用方（加入布局后归布局）
 */
std::unique_ptr<balloonwjui::DuiBadge> MakeP2PBadgeControl();

/**
 *  按链路路径显示或隐藏一枚徽章控件；显隐有变化时对它的父容器按当前矩形 ForceLayout（SetVisible 不会重排，见开发约定文档
 *  「UI 约定」第 5 条），各窗口每次拿到新的链路路径时调一次即可。
 *  @param badge    MakeP2PBadgeControl 生成、已加入布局的控件（借用）；NULL 时什么也不做
 *  @param linkPath 链路实际路径（取值同 ShouldShowP2PBadge）
 *  @return 显隐有变化返回 true；没有变化或 badge 为 NULL 返回 false
 */
bool ApplyP2PBadgeVisibility(balloonwjui::DuiBadge* badge, int linkPath);

} // namespace balloonmain
