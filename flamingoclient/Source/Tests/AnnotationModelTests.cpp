/**
 *  AnnotationModelTests.cpp
 *
 *  第二期 a 测试：覆盖截图标注的纯数据模型 —— 撤销/重做栈 AnnotationStore（S）、
 *  当前工具状态 AnnotationToolState（T）、箭头头部几何 ComputeArrowHead（A）。
 *  实际绘制 / 鼠标交互 / 烤进输出图等只能目视核对，不在本文件范围内。
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../../FlamingoScreenshot/Capture/AnnotationModel.h"

#include <math.h>

using namespace balloonscreenshot;

namespace {

// 造一个带可辨识标记（lineWidth）的标注，便于断言"是哪一个"。
Annotation MakeMarked(int mark)
{
    Annotation a;
    a.tool = kAnnRect;
    a.lineWidth = mark;
    return a;
}

POINT Pt(int x, int y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

} // anonymous namespace

// =====================================================================
// S. 撤销 / 重做栈 AnnotationStore
// =====================================================================

TEST_CASE("S1 新建为空，不能撤销/重做", "[annot][store]")
{
    AnnotationStore s;
    CHECK(s.Count() == 0u);
    CHECK_FALSE(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
}

TEST_CASE("S2 Add 一个 → 列表+1、可撤销、不可重做", "[annot][store]")
{
    AnnotationStore s;
    s.Add(MakeMarked(1));
    CHECK(s.Count() == 1u);
    CHECK(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
}

TEST_CASE("S3 Add 两个后 Undo → 列表-1、可撤销也可重做", "[annot][store]")
{
    AnnotationStore s;
    s.Add(MakeMarked(1));
    s.Add(MakeMarked(2));
    s.Undo();
    CHECK(s.Count() == 1u);
    CHECK(s.CanUndo());
    CHECK(s.CanRedo());
    CHECK(s.Items().back().lineWidth == 1);   // 撤掉了 2，剩 1
}

TEST_CASE("S4 Undo 后 Redo → 恢复、不可再重做", "[annot][store]")
{
    AnnotationStore s;
    s.Add(MakeMarked(1));
    s.Add(MakeMarked(2));
    s.Undo();
    s.Redo();
    CHECK(s.Count() == 2u);
    CHECK_FALSE(s.CanRedo());
    CHECK(s.Items().back().lineWidth == 2);   // 2 被重做回来
}

TEST_CASE("S5 Undo 后再 Add 新标注 → 重做栈被清空", "[annot][store]")
{
    AnnotationStore s;
    s.Add(MakeMarked(1));
    s.Add(MakeMarked(2));
    s.Undo();                  // 撤掉 2，重做栈有 2
    REQUIRE(s.CanRedo());
    s.Add(MakeMarked(3));      // 新分支：2 的重做记录作废
    CHECK(s.Count() == 2u);
    CHECK_FALSE(s.CanRedo());
    CHECK(s.Items().back().lineWidth == 3);
}

TEST_CASE("S6 空栈 Undo / 无重做项 Redo → no-op 不崩", "[annot][store]")
{
    AnnotationStore s;
    s.Undo();                  // 空，不崩
    s.Redo();                  // 空，不崩
    CHECK(s.Count() == 0u);
    s.Add(MakeMarked(1));
    s.Redo();                  // 没有重做项
    CHECK(s.Count() == 1u);
}

TEST_CASE("S7 Clear 清空列表与重做栈", "[annot][store]")
{
    AnnotationStore s;
    s.Add(MakeMarked(1));
    s.Add(MakeMarked(2));
    s.Undo();
    s.Clear();
    CHECK(s.Count() == 0u);
    CHECK_FALSE(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
}

// =====================================================================
// T. 工具状态 AnnotationToolState
// =====================================================================

TEST_CASE("T1 默认：未选工具 / 红色 / 线宽 4", "[annot][tool]")
{
    AnnotationToolState t;
    CHECK(t.Tool() == kAnnNone);
    CHECK(t.Color() == RGB(0xFF, 0x4D, 0x4F));
    CHECK(t.LineWidth() == 4);
}

TEST_CASE("T2 设置工具 / 颜色 / 线宽后读回一致", "[annot][tool]")
{
    AnnotationToolState t;
    t.SetTool(kAnnArrow);
    t.SetColor(RGB(0x12, 0x34, 0x56));
    t.SetLineWidth(8);
    CHECK(t.Tool() == kAnnArrow);
    CHECK(t.Color() == RGB(0x12, 0x34, 0x56));
    CHECK(t.LineWidth() == 8);
}

TEST_CASE("T3 线宽 <=0 夹到 1", "[annot][tool]")
{
    AnnotationToolState t;
    t.SetLineWidth(0);
    CHECK(t.LineWidth() == 1);
    t.SetLineWidth(-5);
    CHECK(t.LineWidth() == 1);
}

// =====================================================================
// A. 箭头头部几何 ComputeArrowHead
// =====================================================================

TEST_CASE("A1 水平箭头：两翼关于轴对称、在箭尖之前、距箭尖约 headLen", "[annot][arrow]")
{
    POINT w1, w2;
    const int headLen = 20;
    ComputeArrowHead(Pt(0, 0), Pt(100, 0), headLen, w1, w2);

    CHECK(w1.x == w2.x);            // 同一 x（关于水平轴对称）
    CHECK(w1.y == -w2.y);           // y 互为相反数（对称于轴 y=0）
    CHECK(w1.x < 100);              // 翼点在箭尖之前（朝起点一侧）

    double d1 = ::sqrt((double)((w1.x - 100) * (w1.x - 100) + w1.y * w1.y));
    CHECK(d1 == Catch::Approx((double)headLen).margin(2.0));   // 距箭尖约 headLen
}

TEST_CASE("A2 退化：起止同点不崩、两翼取箭尖", "[annot][arrow]")
{
    POINT w1, w2;
    ComputeArrowHead(Pt(50, 50), Pt(50, 50), 20, w1, w2);
    CHECK(w1.x == 50);
    CHECK(w1.y == 50);
    CHECK(w2.x == 50);
    CHECK(w2.y == 50);
}

// =====================================================================
// MOS. 马赛克映射（第二期 b）
// =====================================================================

TEST_CASE("MOS1 块大小随线宽档位（2/4/6 → 6/12/18）", "[annot][mosaic]")
{
    CHECK(MosaicBlockForWidth(2) == 6);
    CHECK(MosaicBlockForWidth(4) == 12);
    CHECK(MosaicBlockForWidth(6) == 18);
}

TEST_CASE("MOS2 过小线宽夹到最小块 6", "[annot][mosaic]")
{
    CHECK(MosaicBlockForWidth(1) == 6);
    CHECK(MosaicBlockForWidth(0) == 6);
}

TEST_CASE("MOS3 降采样块数 = max(1, len/block)", "[annot][mosaic]")
{
    CHECK(MosaicDownscaleExtent(100, 10) == 10);
    CHECK(MosaicDownscaleExtent(25, 10) == 2);    // 整除取整
    CHECK(MosaicDownscaleExtent(5, 10) == 1);     // 不足一块也至少 1
    CHECK(MosaicDownscaleExtent(0, 10) == 1);     // 零长度也至少 1
    CHECK(MosaicDownscaleExtent(50, 0) == 50);    // block<=0 按 1 处理，不崩
}

// =====================================================================
// TXT. 文字字号映射（第二期 b）
// =====================================================================

TEST_CASE("TXT1 字号随线宽档位（2/4/6 → 16/22/28）", "[annot][text]")
{
    CHECK(TextFontSizeForWidth(2) == 16);
    CHECK(TextFontSizeForWidth(4) == 22);
    CHECK(TextFontSizeForWidth(6) == 28);
}

TEST_CASE("TXT2 字号夹到下限 16", "[annot][text]")
{
    CHECK(TextFontSizeForWidth(1) == 16);
    CHECK(TextFontSizeForWidth(0) == 16);
}
