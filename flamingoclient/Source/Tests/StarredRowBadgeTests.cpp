/**
 *  StarredRowBadgeTests.cpp
 *
 *  通讯录行 / 最近会话行上「特别关注」星标的渲染测试。
 *
 *  这一层没有可断言的纯逻辑（标记本身只是一个 bool），能出问题的全在绘制：星标的
 *  横坐标取自 DuiTreeView::GetItemLabelTextRect，而那个矩形要跟着缩进层级、图标
 *  尺寸、右侧状态徽标与辅助文字一起变。故这里走离屏位图 + 数像素：把树画到内存
 *  DIB 上，数金色（#F5A623）像素。标记行必须大于 0，未标记行必须等于 0。
 *
 *  做法与 GroupMentionInteractionTests.cpp 里的 R 组渲染回归同源。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonMain/MainPanelTreeView.h"

using namespace balloonmain;

namespace {

// 星标颜色，与 MainPanelTreeView.cpp 的 kStarIconColor 同值。
const COLORREF kStarGold = RGB(0xF5, 0xA6, 0x23);

// 测试用树的尺寸（逻辑像素）。宽度按主面板实际的会话列表取，够放下
// 「头像 + 名字 + 星 + 时间 + 状态徽标」一整行而不被挤掉。
const int kTreeW = 320;
const int kTreeH = 200;
const int kRowH  = 48;

// 造一张 32bpp 的纯色位图，当作行右端的状态徽标用（只关心它占多宽）。
HBITMAP MakeSolidBitmap(int size, COLORREF color)
{
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = size;
    bi.bmiHeader.biHeight      = -size;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HDC screen = ::GetDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    if (bmp == NULL || bits == NULL)
    {
        return NULL;
    }

    // premultiplied alpha：alpha 取满，三通道直接写颜色即可。
    const unsigned int px = 0xFF000000u
                          | ((unsigned int)GetRValue(color) << 16)
                          | ((unsigned int)GetGValue(color) << 8)
                          |  (unsigned int)GetBValue(color);
    unsigned int* p = (unsigned int*)bits;
    for (int i = 0; i < size * size; ++i)
    {
        p[i] = px;
    }
    return bmp;
}

// 把树画到白底离屏位图上，数出指定颜色的像素个数。
// @return 像素个数；画布建不出来返回 -1。
int PaintAndCountColor(MainPanelTreeView& tree, int w, int h, COLORREF want)
{
    HDC screenDC = ::GetDC(NULL);
    if (screenDC == NULL)
    {
        return -1;
    }
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return -1;
    }

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return -1;
    }
    ::SelectObject(memDC, bmp);

    RECT all = { 0, 0, w, h };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);

    tree.SetRect(all);
    tree.OnPaint(memDC, all);

    const unsigned int target = ((unsigned int)GetRValue(want) << 16)
                              | ((unsigned int)GetGValue(want) << 8)
                              |  (unsigned int)GetBValue(want);
    const unsigned int* px = (const unsigned int*)bits;
    int count = 0;
    for (int i = 0; i < w * h; ++i)
    {
        if ((px[i] & 0x00FFFFFFu) == target)
        {
            ++count;
        }
    }

    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return count;
}

} // anonymous namespace

TEST_CASE("SRB1 通讯录行：标记后画出金色星，取消后消失", "[starred][rowbadge]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);

    const int root = tree.AddRoot(_T("我的好友"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("苏沐辰"));
    tree.SetItemSubLabel(node, _T("资深设计师"));

    // 没标记时不该有任何金色像素
    CHECK(PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold) == 0);

    tree.SetItemStarred(node, true);
    const int lit = PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold);
    INFO("标记后的金色像素数 = " << lit);
    CHECK(lit > 0);

    // 取消后必须干干净净，不留残影
    tree.SetItemStarred(node, false);
    CHECK(PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold) == 0);
}

TEST_CASE("SRB2 会话行：右端排满时间与状态徽标，星标照样画得出来", "[starred][rowbadge]")
{
    // 这一条盯的是不用会话行版式时的行形态：顶层节点（无父）+ 副标签 + 右侧辅助文字 +
    // 行右端的状态徽标。星标的横坐标要在这几样都在场时仍然算得对。2026-10-07 起主面板的
    // 会话行改用 SetItemSummary（标记画在第二行）、也不再设行右端状态徽标，这里只核对基类
    // 接口组合下名字之后的星标，会话行版式的标记见 MainPanelRowMarkTests 的 CRL 组。
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);

    const int node = tree.AddRoot(_T("苏沐辰"));
    tree.SetItemSubLabel(node, _T("好的好的，等会发你"));
    tree.SetItemRightText(node, _T("14:32  (3)"));

    HBITMAP statusBmp = MakeSolidBitmap(12, RGB(0x16, 0xA3, 0x4A));
    REQUIRE(statusBmp != NULL);
    tree.SetItemStatusIcon(node, statusBmp);

    CHECK(PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold) == 0);

    tree.SetItemStarred(node, true);
    const int lit = PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold);
    INFO("会话行标记后的金色像素数 = " << lit);
    CHECK(lit > 0);

    tree.SetItemStatusIcon(node, NULL);
    ::DeleteObject(statusBmp);
}

TEST_CASE("SRB3 多行时只有被标记的那些行有星", "[starred][rowbadge]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);

    std::vector<int> nodes;
    for (int i = 0; i < 3; ++i)
    {
        CString name;
        name.Format(_T("同事%d"), i);
        nodes.push_back(tree.AddRoot(name));
    }

    // 只标第一行与第三行
    tree.SetItemStarred(nodes[0], true);
    tree.SetItemStarred(nodes[2], true);
    const int two = PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold);
    REQUIRE(two > 0);

    // 再标中间那行，金色像素应当变多（三颗星比两颗星多）
    tree.SetItemStarred(nodes[1], true);
    const int three = PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold);
    CHECK(three > two);

    // 全部取消后归零
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        tree.SetItemStarred(nodes[i], false);
    }
    CHECK(PaintAndCountColor(tree, kTreeW, kTreeH, kStarGold) == 0);
}

TEST_CASE("SRB4 标签文字矩形随缩进 / 图标 / 右侧占位变化", "[starred][rowbadge]")
{
    // 星标的落点全靠这个矩形，故单独盯一下它的几何：
    //   · 子节点比根节点多一级缩进 → 文字起点更靠右；
    //   · 右侧有辅助文字时可用宽度收窄 → 文字右边界更靠左。
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    RECT all = { 0, 0, kTreeW, kTreeH };
    tree.SetRect(all);

    const int root = tree.AddRoot(_T("分组"));
    tree.SetExpanded(root, true);
    const int child = tree.AddChild(root, _T("同一个名字"));

    RECT rcRoot = { 0, 0, 0, 0 };
    RECT rcChild = { 0, 0, 0, 0 };
    REQUIRE(tree.GetItemLabelTextRect(root, rcRoot));
    REQUIRE(tree.GetItemLabelTextRect(child, rcChild));
    CHECK(rcChild.left > rcRoot.left);   // 多一级缩进

    // 同一个节点加上右侧辅助文字之后，文字可用宽度变窄
    const int wideRight = rcChild.right;
    tree.SetItemRightText(child, _T("00:00  (99)"));
    REQUIRE(tree.GetItemLabelTextRect(child, rcChild));
    CHECK(rcChild.right <= wideRight);

    // 不存在的节点、空标签都要老实返回 false，而不是给一个瞎猜的矩形
    RECT dummy = { 0, 0, 0, 0 };
    CHECK_FALSE(tree.GetItemLabelTextRect(9999, dummy));
    const int empty = tree.AddRoot(_T(""));
    CHECK_FALSE(tree.GetItemLabelTextRect(empty, dummy));
}
