/**
 *  文件列表子类单元测试（Suite SFL，标签 [sharedrag]），ShareFileListTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 5 步）：
 *    - SFL1 鼠标：按下交给监听者（带 Ctrl / Shift 标志）；移动没超过阈值不算拖动，超过后通知一次开始拖动、带的是
 *           按下时的位置，之后的移动与松开都不再通知；没有拖动时松开通知监听者；没有按着左键的移动结束这次按下；
 *    - SFL2 双击与按键：双击交给监听者；监听者处理了的按键不再交给基类，没处理的交给基类；没有监听者时退回基类行为。
 *  不建窗口，直接调用控件的鼠标与按键方法（没有宿主时不捕获鼠标）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/ShareFileList.h"

using namespace fileshare;

namespace {

//测试用的拖动阈值（像素）
const int kTestDragCx = 4;
const int kTestDragCy = 4;

//记录收到的回调
class RecordingListener : public IShareFileListListener
{
public:
    RecordingListener()
        : m_downs(0)
        , m_ups(0)
        , m_drags(0)
        , m_doubles(0)
        , m_lastFlags(0)
        , m_handleKeys(true)
    {
        m_dragPt.x = 0;
        m_dragPt.y = 0;
    }

    virtual void OnFileListMouseDown(POINT pt, UINT mkFlags) override
    {
        (void)pt;
        ++m_downs;
        m_lastFlags = mkFlags;
    }

    virtual void OnFileListMouseUp(POINT pt, UINT mkFlags) override
    {
        (void)pt;
        (void)mkFlags;
        ++m_ups;
    }

    virtual void OnFileListDragStart(POINT pressPt) override
    {
        ++m_drags;
        m_dragPt = pressPt;
    }

    virtual void OnFileListDoubleClick(POINT pt) override
    {
        (void)pt;
        ++m_doubles;
    }

    virtual bool OnFileListKeyDown(UINT vk) override
    {
        m_keys.push_back(vk);
        return m_handleKeys;
    }

    int               m_downs;       //按下次数
    int               m_ups;         //松开次数
    int               m_drags;       //开始拖动次数
    int               m_doubles;     //双击次数
    UINT              m_lastFlags;   //最近一次按下的按键标志
    POINT             m_dragPt;      //开始拖动时带的位置
    std::vector<UINT> m_keys;        //收到的按键
    bool              m_handleKeys;  //按键回调的返回值
};

POINT Pt(int x, int y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

} // namespace

TEST_CASE("SFL1 mouse press, drag threshold and release", "[fileshare][sharedrag]")
{
    CShareFileList list;
    RecordingListener rec;
    list.SetListener(&rec);
    list.SetDragThreshold(kTestDragCx, kTestDragCy);

    //按下（带 Ctrl）、小幅移动、松开：不算拖动
    CHECK(list.OnLButtonDown(Pt(10, 10), MK_LBUTTON | MK_CONTROL));
    CHECK(rec.m_downs == 1);
    CHECK((rec.m_lastFlags & MK_CONTROL) != 0);
    CHECK(list.IsPressed());
    list.OnMouseMove(Pt(12, 13), MK_LBUTTON);
    CHECK(rec.m_drags == 0);
    list.OnLButtonUp(Pt(12, 13), 0);
    CHECK(rec.m_ups == 1);
    CHECK_FALSE(list.IsPressed());

    //按下后移动超过阈值：通知一次，带按下时的位置；之后的移动与松开不再通知
    list.OnLButtonDown(Pt(20, 30), MK_LBUTTON);
    list.OnMouseMove(Pt(20, 30 + kTestDragCy), MK_LBUTTON);
    CHECK(rec.m_drags == 1);
    CHECK(rec.m_dragPt.x == 20);
    CHECK(rec.m_dragPt.y == 30);
    CHECK_FALSE(list.IsPressed());
    list.OnMouseMove(Pt(80, 90), MK_LBUTTON);
    list.OnLButtonUp(Pt(80, 90), 0);
    CHECK(rec.m_drags == 1);
    CHECK(rec.m_ups == 1);

    //没有按着左键的移动（松开消息被别处截走）：结束这次按下，不算拖动
    list.OnLButtonDown(Pt(5, 5), MK_LBUTTON);
    list.OnMouseMove(Pt(50, 50), 0);
    CHECK_FALSE(list.IsPressed());
    CHECK(rec.m_drags == 1);
    //没有按下时的移动与松开都不通知
    list.OnMouseMove(Pt(60, 60), MK_LBUTTON);
    list.OnLButtonUp(Pt(60, 60), 0);
    CHECK(rec.m_drags == 1);
    CHECK(rec.m_ups == 1);
}

TEST_CASE("SFL2 double click and keys", "[fileshare][sharedrag]")
{
    CShareFileList list;
    list.SetRowCount(3);
    RecordingListener rec;
    list.SetListener(&rec);

    CHECK(list.OnLButtonDblClk(Pt(1, 1), MK_LBUTTON));
    CHECK(rec.m_doubles == 1);

    //监听者处理了：返回 true，基类的选中行不变
    CHECK(list.OnKeyDown(VK_DOWN, 0));
    REQUIRE(rec.m_keys.size() == 1);
    CHECK(rec.m_keys[0] == VK_DOWN);
    CHECK(list.GetCurSel() == -1);

    //监听者不处理：交给基类（向下移动选中行）
    rec.m_handleKeys = false;
    list.OnKeyDown(VK_DOWN, 0);
    CHECK(rec.m_keys.size() == 2);
    CHECK(list.GetCurSel() == 0);

    //没有监听者：鼠标退回基类行为（按在行外不改选中行），不再记录
    list.SetListener(NULL);
    list.OnLButtonDown(Pt(-100, -100), MK_LBUTTON);
    CHECK_FALSE(list.IsPressed());
    CHECK(rec.m_downs == 0);
}
