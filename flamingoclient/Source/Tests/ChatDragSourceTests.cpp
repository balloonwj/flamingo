/**
 *  ChatDragSourceTests.cpp
 *
 *  从聊天记录往外拖内容（ChatDragSource）的测试：
 *    · DRG1：按下点能不能作为拖拽起点、移动距离够不够开始拖（纯函数）；
 *    · DRG2：拖拽数据对象里有哪些格式、内容对不对 —— 在内存里直接向数据对象查询，
 *      不碰系统剪贴板，也不发起真正的拖拽。
 *
 *  用 Catch2 v3（amalgamated）作框架。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，
 *  本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatDragSource.h"
#include "../UI/BalloonChatWnd/ChatClipboardIo.h"   // GetChatClipFormat
#include "../UI/BalloonChatWnd/ChatMockData.h"      // ChatMsgKind

#include <shlobj.h>   // DROPFILES / CFSTR_PREFERREDDROPEFFECT
#include <string>
#include <vector>

using namespace balloonchatwnd;

namespace {

// 测试用的系统拖动阈值（像素）：多数机器上 SM_CXDRAG / SM_CYDRAG 都是 4。
const int kTestDragThreshold = 4;

// 测试用的表情编号。
const int kFaceSmile = 5;

// 在作用域内初始化 OLE：数据对象由 Shell 创建，要求调用线程已初始化 COM。
class ScopedOle
{
public:
    ScopedOle()
    {
        m_ok = SUCCEEDED(::OleInitialize(NULL));
    }
    ~ScopedOle()
    {
        if (m_ok)
        {
            ::OleUninitialize();
        }
    }
private:
    bool m_ok;   //初始化是否成功；成功才需要配对反初始化
};

// 从数据对象里取某个 HGLOBAL 格式的原始字节；取不到返回空串。
std::string GetGlobalBytes(IDataObject* pdo, CLIPFORMAT cf)
{
    FORMATETC fe = { cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm;
    ::ZeroMemory(&sm, sizeof(sm));
    if (FAILED(pdo->GetData(&fe, &sm)))
    {
        return std::string();
    }
    std::string bytes;
    const void* p = ::GlobalLock(sm.hGlobal);
    if (p != NULL)
    {
        bytes.assign((const char*)p, ::GlobalSize(sm.hGlobal));
        ::GlobalUnlock(sm.hGlobal);
    }
    ::ReleaseStgMedium(&sm);
    return bytes;
}

bool HasFormat(IDataObject* pdo, CLIPFORMAT cf)
{
    FORMATETC fe = { cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return pdo->QueryGetData(&fe) == S_OK;
}

ChatClipSegment MakeText(const wchar_t* text)
{
    ChatClipSegment s;
    s.m_kind = kChatClipText;
    s.m_text = text;
    return s;
}

ChatClipSegment MakeFace(int faceId)
{
    ChatClipSegment s;
    s.m_kind   = kChatClipFace;
    s.m_faceId = faceId;
    return s;
}

ChatClipSegment MakeImage(const wchar_t* path)
{
    ChatClipSegment s;
    s.m_kind      = kChatClipImage;
    s.m_imagePath = path;
    return s;
}

} // anonymous namespace

TEST_CASE("DRG1 拖拽起点：文字气泡按在选区内、图片气泡按在就绪的图片上才算", "[chatdrag]")
{
    //文字气泡：选区内才算，选区外仍是重新选择文字。
    CHECK(IsChatDragOrigin(kMsgText, true, false, false));
    CHECK_FALSE(IsChatDragOrigin(kMsgText, false, false, false));
    //大表情气泡与文字同理。
    CHECK(IsChatDragOrigin(kMsgEmoji, true, false, false));
    CHECK_FALSE(IsChatDragOrigin(kMsgEmoji, false, false, false));

    //图片气泡：按在图片上、原图可用才算；按在头像或空白处、图片没就绪（上传中 / 下载中 /
    //失败 / 本地文件丢失）都不算。
    CHECK(IsChatDragOrigin(kMsgImage, false, true, true));
    CHECK_FALSE(IsChatDragOrigin(kMsgImage, false, false, true));
    CHECK_FALSE(IsChatDragOrigin(kMsgImage, false, true, false));

    //文件气泡等其余类型：一律不算。
    CHECK_FALSE(IsChatDragOrigin(kMsgFile, true, true, true));
}

TEST_CASE("DRG1 拖动阈值：任一方向超过系统阈值才开始拖", "[chatdrag]")
{
    CHECK_FALSE(IsBeyondDragThreshold(0, 0, kTestDragThreshold, kTestDragThreshold));
    CHECK_FALSE(IsBeyondDragThreshold(kTestDragThreshold, -kTestDragThreshold,
                                      kTestDragThreshold, kTestDragThreshold));   //恰好等于不算
    CHECK(IsBeyondDragThreshold(kTestDragThreshold + 1, 0, kTestDragThreshold, kTestDragThreshold));
    CHECK(IsBeyondDragThreshold(0, -(kTestDragThreshold + 1), kTestDragThreshold, kTestDragThreshold));
}

TEST_CASE("DRG2 拖出文字与表情：带纯文字与 Flamingo 格式，不带文件列表", "[chatdrag]")
{
    ScopedOle ole;
    std::vector<ChatClipSegment> segs;
    segs.push_back(MakeText(L"你好"));
    segs.push_back(MakeFace(kFaceSmile));

    IDataObject* pdo = CreateChatDragDataObject(segs);
    REQUIRE(pdo != NULL);

    CHECK(HasFormat(pdo, CF_UNICODETEXT));
    CHECK(HasFormat(pdo, (CLIPFORMAT)GetChatClipFormat()));
    CHECK_FALSE(HasFormat(pdo, CF_HDROP));

    //Flamingo 格式能解析回同样的片段。
    const std::string rich = GetGlobalBytes(pdo, (CLIPFORMAT)GetChatClipFormat());
    std::vector<ChatClipSegment> back;
    REQUIRE(ParseClipSegments(std::string(rich.c_str()), back));
    REQUIRE(back.size() == 2);
    CHECK(back[0].m_text == L"你好");
    CHECK(back[1].m_faceId == kFaceSmile);

    //纯文字与复制时的口径一致。
    const std::string textBytes = GetGlobalBytes(pdo, CF_UNICODETEXT);
    CHECK(std::wstring((const wchar_t*)textBytes.c_str()) == L"你好[表情]");

    pdo->Release();
}

TEST_CASE("DRG2 拖出一张图片：额外带文件列表，首选效果为复制", "[chatdrag]")
{
    ScopedOle ole;
    const wchar_t* const path = L"C:\\cache\\图片.png";
    std::vector<ChatClipSegment> segs;
    segs.push_back(MakeImage(path));

    IDataObject* pdo = CreateChatDragDataObject(segs);
    REQUIRE(pdo != NULL);

    CHECK(HasFormat(pdo, (CLIPFORMAT)GetChatClipFormat()));
    REQUIRE(HasFormat(pdo, CF_HDROP));

    //文件列表里恰好是这张图的路径。
    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm;
    ::ZeroMemory(&sm, sizeof(sm));
    REQUIRE(SUCCEEDED(pdo->GetData(&fe, &sm)));
    HDROP hDrop = (HDROP)::GlobalLock(sm.hGlobal);
    REQUIRE(hDrop != NULL);
    const UINT kQueryCount = 0xFFFFFFFF;   //DragQueryFile 取文件个数时传的下标
    CHECK(::DragQueryFileW(hDrop, kQueryCount, NULL, 0) == 1);
    wchar_t got[MAX_PATH] = { 0 };
    ::DragQueryFileW(hDrop, 0, got, MAX_PATH);
    CHECK(std::wstring(got) == path);
    ::GlobalUnlock(sm.hGlobal);
    ::ReleaseStgMedium(&sm);

    //首选效果：复制。
    const CLIPFORMAT cfEffect = (CLIPFORMAT)::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    const std::string effect = GetGlobalBytes(pdo, cfEffect);
    REQUIRE(effect.size() >= sizeof(DWORD));
    CHECK(*(const DWORD*)effect.data() == (DWORD)DROPEFFECT_COPY);

    pdo->Release();

    //片段为空时不创建。
    CHECK(CreateChatDragDataObject(std::vector<ChatClipSegment>()) == NULL);
}
