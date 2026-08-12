/**
 *  PresenceBadge.h
 *
 *  在线状态徽标（presence badge）的统一绘制入口 —— 把"某个在线状态长什么样"这件事
 *  收在一处，供全客户端六个显示点共用：通讯录 / 收藏树的行状态点、主面板 Header 状态
 *  胶囊、登录窗 Banner 状态胶囊、聊天窗对方状态胶囊、状态菜单 / 托盘菜单的行图标、
 *  会话列表头像右下角的角标。
 *
 *  改版背景（2026-08-12 与用户确认）：改版前四个状态<u>共用同一个形状</u>（实心圆点），
 *  仅靠颜色区分，于是 10px 下离开的琥珀与忙碌的红几乎同色、红绿色盲用户完全分不出
 *  在线与忙碌、离线灰在浅色行底上近乎消失。现在改为<u>形状为主、颜色为辅</u>：
 *  在线是实心圆、离开是圆底加白色时钟指针、忙碌是圆底加白色横杠、离线是空心环，
 *  四者去掉颜色后仍能分辨。字形语言见设计参考图（snapshots/105915.png）。
 *
 *  典型用法（自绘控件里，调用方已开好抗锯齿的 Graphics）：
 *      balloonmain::DrawPresenceBadge(g, presence, false, x, y, 12.0f);
 *
 *  典型用法（需要一张位图交给 balloonui 控件时，如 DuiTreeView::SetItemStatusIcon）：
 *      HBITMAP hBadge = balloonmain::CreatePresenceBadgeBitmap(presence, 12);
 *      // …用完由调用方 DeleteObject
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>
 *  （GDI+ 头文件依赖它）。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <gdiplus.h>

namespace balloonmain {

/**
 *  取某个在线状态对应的徽标字形（MainPanelIcons.h 的 IconKind 取值）。
 *
 *    presence：v2 在线状态（presence_type，见 net/Protocol.h）。隐身与一切越界取值
 *              统一归到"离线"的空心环 —— 与 NormalizePresenceForMenu / GetPresenceVisual
 *              的兜底口径一致，保证"胶囊显示离线"与"图标画成离线环"不会互相矛盾。
 *
 *  返回：kIconPresenceOnline / kIconPresenceAway / kIconPresenceBusy /
 *        kIconPresenceOffline 之一。四个返回值两两不同 —— 这正是本次改版的核心保证：
 *        形状本身就足以区分状态，不依赖颜色。
 */
int GetPresenceBadgeIconKind(int presence);

/**
 *  在指定区域内画一个在线状态徽标。颜色自动取自 GetPresenceVisual(presence).dotColor，
 *  调用方不必再自己查配色表，六处显示点因此天然一致。
 *
 *    g：           目标 Graphics；调用方应<u>已</u>开启抗锯齿（SmoothingModeAntiAlias），
 *                  否则圆边会有明显锯齿。本函数不改 Graphics 的全局状态。
 *    presence：    v2 在线状态（presence_type）；越界取值按"离线"画，同上。
 *    markCurrent： 是否把它标记为"用户当前所处的状态"。true 时徽标缩到
 *                  kCurrentBadgeScale 并居中，外面再套一圈同色细环（单选按钮选中的
 *                  样子）。仅状态菜单 / 托盘菜单需要传 true，其余显示点一律 false。
 *                  代价是当前项那一行的字形会小一圈，但它的文字本来就带"（当前）"
 *                  后缀，两个通道合起来足够清楚。
 *    x / y：       绘制区域左上角（设备坐标）。
 *    size：        绘制区域边长（徽标按 viewBox 24 等比缩放到此尺寸）；<= 0 时不绘制。
 *                  实拍下限是 10 —— 再小时钟指针会糊成一团，列表里建议给到 12。
 */
void DrawPresenceBadge(Gdiplus::Graphics& g, int presence, bool markCurrent,
                       Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size);

/**
 *  造一张只画了状态徽标的 32bpp 预乘 alpha 位图，供只收 HBITMAP 的 balloonui 控件
 *  使用（当前唯一用处是 DuiTreeView::SetItemStatusIcon）。徽标外的像素为全透明
 *  (a=0, r=g=b=0)，正好满足 AlphaBlend(AC_SRC_OVER + AC_SRC_ALPHA) 对源位图的要求。
 *
 *    presence：v2 在线状态（presence_type）；越界取值按"离线"画。
 *    sizePx：  位图边长（逻辑像素，正方形），须 > 0；建议 >= 10，列表里用 12。
 *
 *  @return 新建的 HBITMAP，<u>所有权归调用方</u>，用完须 DeleteObject；
 *          参数非法或分配失败时返回 NULL。
 *
 *  注意：本函数每次调用都新建一张位图。四个状态各自的徽标是常量图形，调用方应当
 *        按状态<u>缓存复用</u>（如 CBalloonMainPanel 只造 4 张给整棵树共用），
 *        不要逐行创建 —— 树里有几百个联系人时那是几百次 DIB 分配。
 */
HBITMAP CreatePresenceBadgeBitmap(int presence, int sizePx);

} // namespace balloonmain
