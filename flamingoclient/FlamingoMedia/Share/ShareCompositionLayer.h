/**
 *  屏幕共享发起人本机标注层所用的 DirectComposition 显示层，ShareCompositionLayer.h
 *
 *  实现已于 2026-10-05 挪到 Source/Common/Graphics/CompositionLayer.h（graphics::CCompositionLayer），
 *  供 Flamingo.exe 的聊天字幕窗口与本 DLL 共用，两个工程编译同一份源文件。本文件只保留原来的类型名，
 *  ShareAnnotationOverlay 与 Tests/ShareCompositionLayerTests.cpp 等调用方不用改动。用法、背景与
 *  「能否排除在采集之外」的实测结论见那个头文件。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include "../../Source/Common/Graphics/CompositionLayer.h"

namespace screenshare {

//原类型名：即 graphics::CCompositionLayer
typedef ::graphics::CCompositionLayer CShareCompositionLayer;

} // namespace screenshare
