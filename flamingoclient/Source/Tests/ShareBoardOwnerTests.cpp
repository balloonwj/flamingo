/**
 *  关闭他人标注权限后，白板建立者仍可在自己的白板上画（标签 [boardowner]），ShareBoardOwnerTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.1 节 ④-1（2026-10-07 用户确认）。主持人关闭他人的标注权限后，原先连其他人自己建的
 *  白板也不能画；改为白板建立者在自己的白板上仍可画，其他人仍不能画。白板的建立者由控制器按服务端的房间快照
 *  （whiteboards 的 owner 与画面座位、members 的成员座位）交给标注模型（SetBoardOwners），不以发送者自称为准。本文件覆盖：
 *    - SBWO1 模型：权限关闭时，建立者在自己白板上开始一笔被接受；画在别人的画面上、别人画在这块白板上仍被拒绝；主持人不受限；
 *    - SBWO2 模型：建立者在自己白板上的追加点、画完、画笔位置、白板视野、擦除、移动都被接受；画笔位置在别的画面上、
 *            移动的对象里有别的画面上的、别人发这块白板的视野，都被拒绝；
 *    - SBWO3 模型：没有交过建立者（或交了空表）时与原来相同，权限关闭后一律拒绝；
 *    - SBWO4 会话：本人是白板建立者，权限关闭后当前画面是自己的白板时能画，切到别的画面时不能画；权限打开时都能画。
 *  纯逻辑，不建窗口。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <map>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareAudioTransport.h"

using namespace screenshare;

namespace {

//白板建立者、另一位成员的成员座位
const int kSbwoOwner = 3;
const int kSbwoOther = 4;
//建立者的白板的画面座位、另一块共享画面的画面座位
const int kSbwoBoard = 7;
const int kSbwoScreen = 1;
//笔迹的颜色与线宽
const uint32_t kSbwoColor = 0xE53935;
const int kSbwoWidth = 4;
//坐标
const uint16_t kSbwoA = 1000;
const uint16_t kSbwoB = 2000;
//本机时刻（毫秒）
const int64_t kSbwoNow = 1000;

//建立者表：建立者的白板 → 建立者的成员座位
std::map<int, int> sbwoOwners()
{
    std::map<int, int> owners;
    owners[kSbwoBoard] = kSbwoOwner;
    return owners;
}

//在某块画面上开始一笔
AnnotationMessage sbwoBegin(uint32_t id, int canvas)
{
    AnnotationMessage m;
    m.m_op = annot_op_begin;
    m.m_id = id;
    m.m_tool = annot_tool_pen;
    m.m_color = kSbwoColor;
    m.m_width = kSbwoWidth;
    m.m_points.push_back(AnnotationPoint(kSbwoA, kSbwoA));
    m.m_points.push_back(AnnotationPoint(kSbwoB, kSbwoB));
    m.m_style.m_canvas = canvas;
    m.m_hasStyle = true;
    return m;
}

//给自己的一笔追加点或画完
AnnotationMessage sbwoOp(int op, uint32_t id)
{
    AnnotationMessage m;
    m.m_op = op;
    m.m_id = id;
    if (op == annot_op_append)
        m.m_points.push_back(AnnotationPoint(kSbwoB, kSbwoA));
    return m;
}

//画笔位置
AnnotationMessage sbwoPointer(int canvas)
{
    AnnotationMessage m;
    m.m_op = annot_op_pointer;
    m.m_canvas = canvas;
    m.m_tool = annot_tool_pen;
    m.m_color = kSbwoColor;
    m.m_points.push_back(AnnotationPoint(kSbwoA, kSbwoB));
    return m;
}

//白板视野
AnnotationMessage sbwoBoardView(int canvas)
{
    AnnotationMessage m;
    m.m_op = annot_op_board;
    m.m_canvas = canvas;
    m.m_board.m_width = kAnnotCoordMax / 2;
    return m;
}

//擦除某位作者的一笔
AnnotationMessage sbwoErase(int author, uint32_t id)
{
    AnnotationMessage m;
    m.m_op = annot_op_erase;
    m.m_author = author;
    m.m_id = id;
    return m;
}

//移动若干对象
AnnotationMessage sbwoMove(const std::vector<AnnotationRef>& targets)
{
    AnnotationMessage m;
    m.m_op = annot_op_move;
    m.m_targets = targets;
    m.m_dx = 10;
    m.m_dy = 10;
    return m;
}

//只记下发出的包体的传输（会话需要一个传输对象才能开始）
class SbwoTransport : public ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        m_bodies.push_back(body);
        return true;
    }

    std::vector<std::string> m_bodies;   //发出的包体，按先后
};

} // namespace

TEST_CASE("SBWO1 board owner can still draw on the own board when others may not", "[boardowner]")
{
    ShareAnnotationModel model;
    model.SetBoardOwners(sbwoOwners());
    model.SetViewersAllowed(false);
    //建立者在自己的白板上：接受
    CHECK(model.Apply(kSbwoOwner, sbwoBegin(1, kSbwoBoard), kSbwoNow));
    //建立者画在别的画面上：拒绝
    CHECK_FALSE(model.Apply(kSbwoOwner, sbwoBegin(2, kSbwoScreen), kSbwoNow));
    //别人画在这块白板上：拒绝
    CHECK_FALSE(model.Apply(kSbwoOther, sbwoBegin(3, kSbwoBoard), kSbwoNow));
    //主持人不受限
    CHECK(model.Apply(kAnnotHostSeat, sbwoBegin(4, kSbwoBoard), kSbwoNow));
    CHECK(model.Apply(kAnnotHostSeat, sbwoBegin(5, kSbwoScreen), kSbwoNow));
    CHECK(model.BoardOwnerOf(kSbwoBoard) == kSbwoOwner);
    CHECK(model.BoardOwnerOf(kSbwoScreen) == -1);
}

TEST_CASE("SBWO2 owner's other operations on the own board are accepted", "[boardowner]")
{
    ShareAnnotationModel model;
    model.SetBoardOwners(sbwoOwners());
    //权限关闭之前，主持人在共享画面上画一笔（用于移动时混进别的画面上的对象）
    REQUIRE(model.Apply(kAnnotHostSeat, sbwoBegin(50, kSbwoScreen), kSbwoNow));
    REQUIRE(model.Apply(kAnnotHostSeat, sbwoOp(annot_op_end, 50), kSbwoNow));
    model.SetViewersAllowed(false);
    REQUIRE(model.Apply(kSbwoOwner, sbwoBegin(1, kSbwoBoard), kSbwoNow));
    CHECK(model.Apply(kSbwoOwner, sbwoOp(annot_op_append, 1), kSbwoNow));
    CHECK(model.Apply(kSbwoOwner, sbwoOp(annot_op_end, 1), kSbwoNow));
    CHECK(model.Apply(kSbwoOwner, sbwoPointer(kSbwoBoard), kSbwoNow));
    CHECK_FALSE(model.Apply(kSbwoOwner, sbwoPointer(kSbwoScreen), kSbwoNow));
    CHECK(model.Apply(kSbwoOwner, sbwoBoardView(kSbwoBoard), kSbwoNow));
    CHECK_FALSE(model.Apply(kSbwoOther, sbwoBoardView(kSbwoBoard), kSbwoNow));
    //移动：只有自己白板上的对象时接受，混进别的画面上的对象时拒绝
    std::vector<AnnotationRef> own(1, AnnotationRef(kSbwoOwner, 1));
    CHECK(model.Apply(kSbwoOwner, sbwoMove(own), kSbwoNow));
    std::vector<AnnotationRef> mixed = own;
    mixed.push_back(AnnotationRef(kAnnotHostSeat, 50));
    CHECK_FALSE(model.Apply(kSbwoOwner, sbwoMove(mixed), kSbwoNow));
    //擦除：自己白板上的一笔接受，别的画面上的拒绝
    CHECK_FALSE(model.Apply(kSbwoOwner, sbwoErase(kAnnotHostSeat, 50), kSbwoNow));
    CHECK(model.Apply(kSbwoOwner, sbwoErase(kSbwoOwner, 1), kSbwoNow));
}

TEST_CASE("SBWO3 without known owners everybody but the host is refused", "[boardowner]")
{
    ShareAnnotationModel model;
    model.SetViewersAllowed(false);
    CHECK_FALSE(model.Apply(kSbwoOwner, sbwoBegin(1, kSbwoBoard), kSbwoNow));
    model.SetBoardOwners(sbwoOwners());
    model.SetBoardOwners(std::map<int, int>());
    CHECK_FALSE(model.Apply(kSbwoOwner, sbwoBegin(2, kSbwoBoard), kSbwoNow));
    CHECK(model.BoardOwnerOf(kSbwoBoard) == -1);
    //权限打开：照常接受
    model.SetViewersAllowed(true);
    CHECK(model.Apply(kSbwoOwner, sbwoBegin(3, kSbwoBoard), kSbwoNow));
}

TEST_CASE("SBWO4 the owner's session can draw on the own board only", "[boardowner]")
{
    SbwoTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSbwoOwner, 1);
    s.SetBoardOwners(sbwoOwners());
    s.SetCanvas(kSbwoBoard);
    CHECK(s.CanDraw());
    s.SetViewersAllowed(false);
    CHECK(s.CanDraw());
    s.SetCanvas(kSbwoScreen);
    CHECK_FALSE(s.CanDraw());
    s.SetViewersAllowed(true);
    CHECK(s.CanDraw());
    //别人的会话：权限关闭后在这块白板上不能画
    SbwoTransport t2;
    ShareAnnotationSession other;
    other.Start(&t2, kSbwoOther, 1);
    other.SetBoardOwners(sbwoOwners());
    other.SetViewersAllowed(false);
    other.SetCanvas(kSbwoBoard);
    CHECK_FALSE(other.CanDraw());
}
