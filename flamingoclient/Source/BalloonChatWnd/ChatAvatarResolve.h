/**
 *  ChatAvatarResolve.h —— 聊天窗（单聊 / 群聊）头像呈现解析共用 helper。
 *
 *  把存储的 (facetype, customface) 归一成"气泡 / 成员列表 / 标题栏该怎么画头像"：
 *  字形头像 / 自动头像给出字形 + 底色；真图头像(facetype=2)在本地缓存命中时给出
 *  图片本地路径（供调用方 AlphaBlend 真图），未命中则入队下载并暂回退字形。
 *  真图缓存目录按"当前登录账号"分桶（Users\<selfId>\Avatar\<fileid>），故须传 selfId。
 *
 *  之所以独立成 helper：单聊气泡 / 群气泡 / 群成员列表 / 聊天窗标题栏四处都要做同一件
 *  事，集中一份避免各写一遍（参照 UI 约定"相似职责抽出复用，不写 ad-hoc 子类"）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（需要 CString /
 *  COLORREF）。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#pragma once

#include <string>

namespace balloonchatwnd {

/**
 *  头像呈现结果。m_imagePath 非空 → 画真图；为空 → 画 m_glyph 字形 + m_color 底色。
 */
struct AvatarRender
{
    CString  m_glyph;      // 字形头像要画的字（真图回退时也用它占位）
    COLORREF m_color;      // 字形头像底色
    CString  m_imagePath;  // 真图本地路径；空=画字形，非空=画真图
};

/**
 *  解析某用户的头像呈现。
 *    userId：       该头像所属用户的 userid（取色 / 占位回退用；0=未知）。
 *    facetype：     存储的头像类型（0 自动 / 1 字形 / 2 真图），见 AvatarSpec.h。
 *    customface：   存储的头像数据（字形头像="glyph|colorIdx"，真图=fileid），UTF-8。
 *    fallbackName： 回退显示名（自动头像取首字；可为 NULL）。
 *    selfId：       当前登录账号 userid，用于定位真图缓存目录；<=0 时真图一律回退字形、
 *                   不触发下载。
 *  返回：AvatarRender。真图未缓存时本函数会经 ImageTransfer 入队下载（按 fileid 去重），
 *        下载完成后由调用方（聊天窗 OnAvatarDownloaded）刷新对应气泡 / 列表。
 */
AvatarRender ResolveAvatarRender(int64_t userId, int facetype,
                                 const std::string& customface,
                                 LPCTSTR fallbackName, int64_t selfId);

} // namespace balloonchatwnd
