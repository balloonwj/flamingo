/**
 *  观看者录制时当前那一块是白板，录像里也要有画面（标签 [boardrec]），ShareBoardRecordTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.1 节 ④-2（2026-10-07 用户确认）。白板在房间里只传笔迹、不传视频，观看者录制共享时
 *  当前放大的那一块若是白板，录像里原先录不到画面。改为录制期间由界面线程定时把这块白板按当前的可见区域画成一张图，录制线程
 *  取画面时用它。本文件覆盖：
 *    - BREC1 画面控件：当前那一块是白板时 RenderActiveBoard 画出白底与笔迹（笔迹颜色的像素存在、四个角是白色画布或画布外的
 *            底色），尺寸与像素数一致、不透明；当前那一块不是白板时返回 false；
 *    - BREC2 源文件守卫：观看窗录制期间开着刷新白板图的定时器，GetRecordFrame 在当前那块是白板时取这张图，录制结束时停掉
 *            定时器；
 *    - BREC3 源文件守卫（方案第 14.1 节 ⑤）：「保存为图片」后的「发送到本会话」先以图片消息发送，宿主拒绝时退回文件消息；
 *            exe 侧图片消息的上限与图片上传的上限相同（20 MB）。
 *  BREC2 在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareAudioTransport.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../../FlamingoMedia/Share/ShareViewerWindow.h"

using namespace screenshare;

namespace {

//白板的画面座位、建立者的成员座位、另一块共享画面的画面座位
const int kBrecBoard = 7;
const int kBrecOwner = 3;
const int kBrecScreen = 1;
//笔迹：纯红、线宽 12，从世界左上四分之一处画到中心
const uint32_t kBrecRed = 0xFF0000;
const int kBrecWidth = 12;
const uint16_t kBrecFrom = static_cast<uint16_t>(kAnnotCoordMax / 8);
const uint16_t kBrecTo = static_cast<uint16_t>(kAnnotCoordMax / 4);
//读文件时每次读的字节数、本文件在 flamingoclient 下的层数
const size_t kBrecReadChunk = 4096;
const int kBrecDepth = 3;

class BrecTransport : public ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        m_bodies.push_back(body);
        return true;
    }

    std::vector<std::string> m_bodies;   //发出的包体，按先后
};

//主持人在白板上画的一笔，经标注轨的帧交给会话
void brecDraw(ShareAnnotationSession& s)
{
    AnnotationMessage m;
    m.m_op = annot_op_begin;
    m.m_id = 1;
    m.m_tool = annot_tool_pen;
    m.m_color = kBrecRed;
    m.m_width = kBrecWidth;
    m.m_points.push_back(AnnotationPoint(kBrecFrom, kBrecFrom));
    m.m_points.push_back(AnnotationPoint(kBrecTo, kBrecTo));
    m.m_style.m_canvas = kBrecBoard;
    m.m_hasStyle = true;
    REQUIRE(s.OnRemoteFrame(kAnnotHostSeat, ShareAnnotationCodec::encode(m), 0));
    AnnotationMessage end;
    end.m_op = annot_op_end;
    end.m_id = 1;
    s.OnRemoteFrame(kAnnotHostSeat, ShareAnnotationCodec::encode(end), 0);
}

std::string brecReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kBrecReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string brecClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kBrecDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

std::string brecFunctionBody(const std::string& source, const std::string& signature, const std::string& nextPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + nextPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

} // namespace

TEST_CASE("BREC1 the active whiteboard renders into an image for recording", "[boardrec]")
{
    BrecTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kBrecOwner, 1);
    brecDraw(s);
    CShareVideoCanvas canvas;
    canvas.SetAnnotation(&s);
    std::vector<int> seats(1, kBrecBoard);
    std::vector<std::wstring> names(1, L"A");
    canvas.SetScreens(seats, names);
    std::vector<int> owners(1, kBrecOwner);
    canvas.SetBoards(seats, owners);
    REQUIRE(canvas.ActiveIsBoard());
    std::vector<uint8_t> img;
    int w = 0;
    int h = 0;
    REQUIRE(canvas.RenderActiveBoard(img, w, h));
    REQUIRE(w > 0);
    REQUIRE(h > 0);
    REQUIRE(img.size() == static_cast<size_t>(w) * h * 4);
    size_t red = 0;
    size_t opaque = 0;
    for (size_t i = 0; i + 3 < img.size(); i += 4)
    {
        //BGRA：红色笔迹
        if (img[i + 2] > 200 && img[i + 1] < 80 && img[i] < 80)
            ++red;
        if (img[i + 3] == 255)
            ++opaque;
    }
    CHECK(red > 0);
    CHECK(opaque == img.size() / 4);
    //左上角是白色的画布
    CHECK(img[0] > 200);
    CHECK(img[1] > 200);
    CHECK(img[2] > 200);

    //当前那一块不是白板：不画
    std::vector<int> screen(1, kBrecScreen);
    canvas.SetScreens(screen, names);
    canvas.SetBoards(std::vector<int>(), std::vector<int>());
    CHECK_FALSE(canvas.RenderActiveBoard(img, w, h));
}

TEST_CASE("BREC2 the viewer window feeds the board image to the recorder", "[boardrec]")
{
    const std::string root = brecClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = brecReadFile(root + "\\FlamingoMedia\\Share\\ShareViewerWindow.cpp");
    REQUIRE_FALSE(src.empty());
    const std::string get = brecFunctionBody(src, "bool CShareViewerWindow::GetRecordFrame(", "void CShareViewerWindow::");
    REQUIRE_FALSE(get.empty());
    CHECK(get.find("m_recordBoardActive") != std::string::npos);
    const std::string setRec = brecFunctionBody(src, "void CShareViewerWindow::SetRecording(bool on)", "void CShareViewerWindow::");
    REQUIRE_FALSE(setRec.empty());
    CHECK(setRec.find("SetTimer(kRecordBoardTimerId") != std::string::npos);
    CHECK(setRec.find("KillTimer(kRecordBoardTimerId)") != std::string::npos);
    const std::string refresh =
        brecFunctionBody(src, "void CShareViewerWindow::RefreshRecordBoardFrame()", "void CShareViewerWindow::");
    REQUIRE_FALSE(refresh.empty());
    CHECK(refresh.find("RenderActiveBoard(") != std::string::npos);
}

TEST_CASE("BREC3 saved annotation images are sent as image messages first", "[boardrec]")
{
    //方案第 14.1 节 ⑤：「保存为图片」后的「发送到本会话」先以图片消息发送，宿主拒绝（超过 20 MB 等）时退回文件消息
    const std::string root = brecClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string board = brecReadFile(root + "\\FlamingoMedia\\Share\\ShareControllerBoard.cpp");
    REQUIRE_FALSE(board.empty());
    const std::string body = brecFunctionBody(board, "LRESULT CShareController::OnSaveToastMsg(", "} // namespace");
    REQUIRE_FALSE(body.empty());
    const size_t image = body.find("m_host->SendImageToConversation(");
    const size_t file = body.find("m_host->SendFileToConversation(");
    REQUIRE(image != std::string::npos);
    REQUIRE(file != std::string::npos);
    CHECK(image < file);
    //exe 侧：超过图片消息的上限时返回 false，与 net/ImageTransfer.cpp 的 20 MB 一致
    const std::string panel = brecReadFile(root + "\\Source\\UI\\BalloonMain\\BalloonMainPanelForward.cpp");
    REQUIRE_FALSE(panel.empty());
    CHECK(panel.find("const int64_t kMaxImageMessageBytes = 20LL * 1024 * 1024;") != std::string::npos);
    const std::string transfer = brecReadFile(root + "\\Source\\net\\ImageTransfer.cpp");
    REQUIRE_FALSE(transfer.empty());
    CHECK(transfer.find("kMaxImageBytes = 20LL * 1024 * 1024;") != std::string::npos);
}
