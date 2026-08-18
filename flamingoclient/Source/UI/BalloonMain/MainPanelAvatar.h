/**
 *  MainPanelAvatar.h
 *
 *  生成 "底色 + 单字" 风格的 monogram avatar HBITMAP(GDI+ AA 实现),
 *  给 DuiTreeView::SetItemIcon 用。位图自身就是带 AA 边缘的圆形 + AA 文字,
 *  圆外像素为完全透明,32bpp premultiplied alpha;须配合
 *  DuiTreeView::SetIconUsesAlpha(true) 启用 AlphaBlend 绘制路径,DuiTreeView
 *  不做 HRGN clip,圆形边缘无锯齿。
 *
 *  设计来源:design data.js 里每个联系人 / 群配的 (avatarText, avatarColor),
 *  以及 BalloonContactTreeAdapter 后续从 CBuddyInfo 字段算出的同样两字段。
 *
 *  本文件遵循 balloonui 头文件约定:包含它之前调用方必须已引入
 *  ATL/WTL (LPCTSTR / COLORREF) 与 <windows.h>。
 *
 *  balloonwj@qq.com   2026-05-26
 */

#pragma once

#include <windows.h>

namespace balloonmain {

/**
 *  生成一张 sizePx × sizePx 的 32bpp premultiplied alpha DIBSection: 圆内填
 *  bgColor、圆外完全透明、圆边缘 AA 渐变;中央用 GDI+ AA 字体绘制 text
 *  的前 1-2 个字符(文字色按底色 NTSC luma 自动切深字 / 白字)。
 *
 *  与 DuiTreeView 配合的典型用法:
 *      tv->SetIconSize(32);
 *      tv->SetIconUsesAlpha(true);            // 启用 AlphaBlend 绘制路径,圆形无锯齿
 *      HBITMAP hb = MakeMonogramAvatar(_T("苏"), RGB(255, 122, 69), 32);
 *      tv->SetItemIcon(nodeId, hb);   // 位图所有权归调用方
 *
 *    text:    avatar 上要画的字(取前 1-2 个字符);可为空,空时只画底色。
 *    bgColor: 底色 RGB。
 *    sizePx:  位图边长,>0;失败(分配失败 / sizePx<=0) 返回 NULL。
 *
 *    返回:   新分配的 HBITMAP, 所有权归调用方;不再使用时调 ::DeleteObject。
 */
HBITMAP MakeMonogramAvatar(LPCTSTR text, COLORREF bgColor, int sizePx);

/**
 *  生成一张 sizePx × sizePx 的 32bpp premultiplied alpha DIBSection: 与
 *  MakeMonogramAvatar 同样的"底色 + 居中 1-2 字"画法，区别只是形状为<b>圆角
 *  矩形</b>（圆角半径 radiusPx）而非正圆 —— 与主面板 banner / 列表项的方形
 *  头像风格一致，供「修改头像」对话框的系统头像磁贴、首字母预览复用。
 *  文字直接画入参 text 的前 1-2 个字符（不做大小写转换、不拆 emoji 代理对），
 *  文字色按底色 NTSC luma 自动切深字 / 白字。
 *
 *    text:     avatar 上要画的字（取前 1-2 个字符）；可为空，空时只画圆角底。
 *    bgColor:  底色 RGB。
 *    sizePx:   位图边长，>0；失败返回 NULL。
 *    radiusPx: 圆角半径（像素），内部夹到 [0, sizePx/2]。
 *
 *    返回:    新分配的 HBITMAP，所有权归调用方；用完调 ::DeleteObject。
 */
HBITMAP MakeMonogramSquareAvatar(LPCTSTR text, COLORREF bgColor, int sizePx, int radiusPx);

/**
 *  生成一枚 monogram 风格的 HICON（"底色圆 + 单字"），供系统托盘
 *  Shell_NotifyIcon 闪烁显示新消息发送方头像用。内部先调 MakeMonogramAvatar
 *  得到 32bpp 预乘 alpha 位图作图标的 color 面，再配一张同尺寸全 0 的单色 AND
 *  掩码，经 ::CreateIconIndirect 合成 —— 图标可见性完全由 color 面的 alpha 通道
 *  决定（圆内不透明、圆外透明），AND 掩码取全 0 仅为满足 API 要求。
 *
 *    text:    图标上要画的字（取前 1-2 个字符）；可为空，空时只画底色圆。
 *    bgColor: 底色 RGB（建议用 AvatarPalette 按发送方 id / 文本稳定取色，
 *             与主面板会话头像同色）。
 *    sizePx:  图标边长，像素，>0；托盘图标建议传 ::GetSystemMetrics(SM_CXSMICON)。
 *
 *    返回:   新建的 HICON，所有权归调用方；不再使用时调 ::DestroyIcon。
 *            失败（sizePx<=0 / 位图或图标创建失败）返回 NULL。
 */
HICON MakeMonogramIcon(LPCTSTR text, COLORREF bgColor, int sizePx);

/**
 *  在一枚已有图标的右下角叠加在线状态徽标，生成一枚新图标，供系统托盘显示
 *  "我当前是离开 / 忙碌"用。底图本身不变（当前是 Image\imonline.ico）。
 *
 *  徽标的字形与颜色不在这里另定：字形取自 GetPresenceBadgeIconKind、颜色取自
 *  GetPresenceVisual().dotColor —— 与主面板状态胶囊、联系人列表的状态点同源，
 *  托盘上看到的徽标与界面里的必然长得一样。徽标外面套一圈白环，避免它与底图
 *  深色部分糊在一起。
 *
 *    hBaseIcon: 底图图标；本函数只读它，所有权仍归调用方。
 *    presence:  要叠加的在线状态（v2 presence_type），通常取
 *               GetTrayOverlayPresence 的返回值；调用方须先排除 kNoTrayOverlay。
 *    sizePx:    生成图标的边长，像素，>0；托盘请传 ::GetSystemMetrics(SM_CXSMICON)。
 *
 *    返回:      新建的 HICON，所有权归调用方，用完调 ::DestroyIcon。
 *               入参非法或任何一步创建失败时返回 NULL，调用方据此回退到底图。
 */
HICON MakePresenceOverlayIcon(HICON hBaseIcon, int presence, int sizePx);

} // namespace balloonmain
