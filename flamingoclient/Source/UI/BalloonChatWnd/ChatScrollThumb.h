/**
 *  ChatScrollThumb.h
 *
 *  聊天窗口一族共用的细滚动条：内容超出可视区时，在右侧画一条半透明灰色的圆角滑块，浮在内容
 *  之上、不占内容宽度；可按住滑块拖动，点轨道空白处时滑块中心先跳到点击处。本模块集中存放它的
 *  尺寸、颜色与几何计算（滑块位置、滚动位置限幅、由滑块位置反推滚动位置），聊天消息区
 *  （ChatMessageList）与群聊窗成员侧栏（GroupMemberList）共用，保证两处外观与手感一致。
 *
 *  几何计算是纯函数，可被 FlamingoUnit 直接测试（Tests/GroupMemberScrollTests.cpp）。
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 windows.h。
 *
 *  典型用法：
 *      const RECT thumb = ComputeChatScrollThumb(viewRect, viewH, contentH, scrollY);
 *      if (!::IsRectEmpty(&thumb)) PaintChatScrollThumb(g, thumb);
 *      //拖动时：scrollY = ChatScrollYFromThumbTop(pt.y - grabOffset, viewRect, viewH, contentH);
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <windows.h>

namespace Gdiplus { class Graphics; }

namespace balloonchatwnd {

const int kChatScrollBarWidth   = 5;    // 滑块宽度（像素）
const int kChatScrollBarMargin  = 2;    // 滑块离可视区右缘的距离（像素）；命中带在滑块左右各放宽这么多
const int kChatScrollMinThumb   = 36;   // 滑块最短高度（像素），内容很长时也不至于小到抓不住

// 滑块颜色：半透明灰（ARGB 分量）。
const unsigned char kChatScrollThumbAlpha = 120;
const unsigned char kChatScrollThumbGray  = 0x5A;

/**
 *  算滑块矩形。
 *    view：         可视区矩形（滑块贴着它的右缘、在它的纵向范围内移动）。
 *    viewHeight：   可视区高度（像素），通常等于 view 的高度。
 *    contentHeight：内容总高度（像素）。
 *    scrollY：      当前滚动位置（像素），越界时按 ClampChatScrollY 收回后再算。
 *  @return 滑块矩形；内容没有超出可视区或可视区高度不大于 0 时返回空矩形（全 0）。纯函数。
 */
RECT ComputeChatScrollThumb(const RECT& view, int viewHeight, int contentHeight, int scrollY);

/**
 *  把滚动位置收进合法范围 [0, contentHeight - viewHeight]；内容没有超出时恒为 0。
 *  @return 收回后的滚动位置。纯函数。
 */
int ClampChatScrollY(int scrollY, int viewHeight, int contentHeight);

/**
 *  由滑块上沿的目标位置反推滚动位置，拖动滑块时用；与 ComputeChatScrollThumb 互逆。
 *    thumbTop：     滑块上沿的目标纵坐标（与 view 同一坐标系），超出轨道时按轨道两端算。
 *    view / viewHeight / contentHeight：同 ComputeChatScrollThumb。
 *  @return 滚动位置，已收进合法范围；内容没有超出时为 0。纯函数。
 */
int ChatScrollYFromThumbTop(int thumbTop, const RECT& view, int viewHeight, int contentHeight);

/**
 *  判断一个点是否落在滚动条的命中带里：滑块所在的竖直窄带（左右各放宽
 *  kChatScrollBarMargin，便于点中），且纵向落在可视区内。没有滑块（thumb 为空）时恒为 false。
 *    pt：   待判断的点（与 view 同一坐标系）。
 *    thumb：ComputeChatScrollThumb 算出的滑块矩形。
 *    view： 可视区矩形。
 *  @return 落在命中带里返回 true。纯函数。
 */
bool HitChatScrollBand(POINT pt, const RECT& thumb, const RECT& view);

/**
 *  画滑块：半透明灰色、两端为半圆的竖条。
 *    g：      目标 Graphics；调用方应已开启抗锯齿。
 *    thumb：  滑块矩形；为空时不画。
 *    opacity：淡入淡出的不透明度 0～1（2026-10-04 起，取自 CChatScrollFade::Alpha），乘在滑块自身的半透明度上；
 *             不大于 0 时不画。默认 1。
 */
void PaintChatScrollThumb(Gdiplus::Graphics& g, const RECT& thumb, float opacity = 1.0f);

} // namespace balloonchatwnd
