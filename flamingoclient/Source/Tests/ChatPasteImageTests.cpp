/**
 *  ChatPasteImageTests.cpp
 *
 *  聊天输入框粘贴剪贴板里的位图（bugs.md BUG-106：粘贴图片后点「发送」没有反应）。
 *    · PIM0（实验，默认不运行）：不碰系统剪贴板，用 IRichEditOle::ImportDataObject 把一张 CF_DIB 位图送进输入框
 *      （与 RichEdit 收到 WM_PASTE 时导入剪贴板数据走同一条路），看导入结果、对象个数与 EnumContent 枚举出的内容，
 *      以实证判断粘贴进来的位图在发送时变成了什么（2026-10-08 实测：对象 0 个，读出来是一个空格）；
 *    · PIM1：粘贴判定表；PIM2：位图转 PNG；PIM3：按「发送到对话框」带编号插入的 PNG 在发送时聚合成 1 个图片部分
 *      （docs/截图独立模块方案.md 第 7.3 节，2026-10-08 用户确认）。
 *
 *  用 Catch2 v3（amalgamated）。
 *
 *  balloonwj@qq.com   2026-10-08
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <memory>

#include <objidl.h>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatInputRichEdit.h"
#include "../UI/BalloonChatWnd/ChatPasteImage.h"
#include "../UI/BalloonChatWnd/ChatRichContentSplit.h"
#include "../UI/BalloonChatWnd/ChatFaceTag.h"

using namespace balloonchatwnd;

namespace {

const int kPimInputW = 400;          // 输入框宽（像素）
const int kPimInputH = 120;          // 输入框高（像素）
const LONG kPimDibW = 4;             // 测试位图宽
const LONG kPimDibH = 3;             // 测试位图高
const WORD kPimDibBits = 32;         // 测试位图每像素位数
const uint32_t kPimPixel = 0xFF3366CCu;   // 测试位图的颜色

// 造一张 32 位自下而上的 CF_DIB（BITMAPINFOHEADER 紧跟像素），调用方负责 GlobalFree
HGLOBAL MakeDib()
{
    const size_t pixels = static_cast<size_t>(kPimDibW) * kPimDibH;
    const size_t bytes = sizeof(BITMAPINFOHEADER) + pixels * sizeof(uint32_t);
    HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (h == NULL)
        return NULL;
    BITMAPINFOHEADER* bih = static_cast<BITMAPINFOHEADER*>(::GlobalLock(h));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = kPimDibW;
    bih->biHeight = kPimDibH;
    bih->biPlanes = 1;
    bih->biBitCount = kPimDibBits;
    bih->biCompression = BI_RGB;
    uint32_t* px = reinterpret_cast<uint32_t*>(bih + 1);
    for (size_t i = 0; i < pixels; ++i)
        px[i] = kPimPixel;
    ::GlobalUnlock(h);
    return h;
}

// 只提供 CF_DIB 的最小 IDataObject（模拟剪贴板里只有一张位图）
class CPimDibDataObject : public IDataObject
{
public:
    CPimDibDataObject()
        : m_ref(1)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == NULL)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject)
        {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++m_ref;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG n = --m_ref;
        if (n == 0)
            delete this;
        return n;
    }

    STDMETHODIMP GetData(FORMATETC* fmt, STGMEDIUM* medium) override
    {
        if (fmt == NULL || medium == NULL)
            return E_POINTER;
        if (fmt->cfFormat != CF_DIB || (fmt->tymed & TYMED_HGLOBAL) == 0)
            return DV_E_FORMATETC;
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = MakeDib();
        medium->pUnkForRelease = NULL;
        return (medium->hGlobal != NULL) ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetDataHere(FORMATETC* /*fmt*/, STGMEDIUM* /*medium*/) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP QueryGetData(FORMATETC* fmt) override
    {
        if (fmt == NULL)
            return E_POINTER;
        return (fmt->cfFormat == CF_DIB && (fmt->tymed & TYMED_HGLOBAL) != 0) ? S_OK : DV_E_FORMATETC;
    }

    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* /*in*/, FORMATETC* out) override
    {
        if (out != NULL)
            out->ptd = NULL;
        return DATA_S_SAMEFORMATETC;
    }

    STDMETHODIMP SetData(FORMATETC* /*fmt*/, STGMEDIUM* /*medium*/, BOOL /*release*/) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override
    {
        if (direction != DATADIR_GET)
            return E_NOTIMPL;
        FORMATETC fmt = { CF_DIB, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return ::SHCreateStdEnumFmtEtc(1, &fmt, out);
    }

    STDMETHODIMP DAdvise(FORMATETC* /*fmt*/, DWORD /*flags*/, IAdviseSink* /*sink*/, DWORD* /*conn*/) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHODIMP DUnadvise(DWORD /*conn*/) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHODIMP EnumDAdvise(IEnumSTATDATA** /*out*/) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    ~CPimDibDataObject()
    {
    }

    ULONG m_ref;   // 引用计数
};

// EnumContent 的收集回调：记下每一段是文字还是图片、文字内容与图片标记
struct PimSegment
{
    bool m_isImage;
    std::wstring m_text;
    DWORD_PTR m_tag;
};

void CollectPimSegment(bool isImage, LPCTSTR text, DWORD_PTR tag, void* ctx)
{
    std::vector<PimSegment>* out = static_cast<std::vector<PimSegment>*>(ctx);
    PimSegment s;
    s.m_isImage = isImage;
    s.m_text = (text != NULL) ? text : L"";
    s.m_tag = tag;
    out->push_back(s);
}

} // namespace

TEST_CASE("PIM0 实验：把一张 CF_DIB 位图导入聊天输入框，看导入结果、对象个数与枚举出的内容", "[.][pasteimage-repro]")
{
    std::unique_ptr<ChatInputRichEdit> input(new ChatInputRichEdit());
    input->SetMultiLine(true);
    input->SetWordWrap(true);
    input->SetShowBorder(false);
    RECT rc = { 0, 0, kPimInputW, kPimInputH };
    input->Layout(rc);

    ITextServices* svc = input->Test_GetTextServices();
    REQUIRE(svc != NULL);
    IRichEditOle* ole = NULL;
    LRESULT got = 0;
    svc->TxSendMessage(EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole), &got);
    REQUIRE(ole != NULL);

    CPimDibDataObject* data = new CPimDibDataObject();
    const HRESULT hrImport = ole->ImportDataObject(data, CF_DIB, NULL);
    const LONG objects = ole->GetObjectCount();
    data->Release();
    ole->Release();

    std::vector<PimSegment> segs;
    input->EnumContent(&CollectPimSegment, &segs);
    int images = 0;
    std::wstring text;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].m_isImage)
            ++images;
        else
            text += segs[i].m_text;
    }
    std::string codes;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char buf[16];
        sprintf_s(buf, "U+%04X ", (unsigned)text[i]);
        codes += buf;
    }
    //对照：同样排好版、什么也没放的输入框
    std::unique_ptr<ChatInputRichEdit> empty(new ChatInputRichEdit());
    empty->SetMultiLine(true);
    empty->SetWordWrap(true);
    empty->SetShowBorder(false);
    empty->Layout(rc);
    std::vector<PimSegment> emptySegs;
    empty->EnumContent(&CollectPimSegment, &emptySegs);
    WARN("ImportDataObject(CF_DIB) hr=0x" << std::hex << (unsigned)hrImport << std::dec << ", ole objects=" << objects
         << ", EnumContent segments=" << segs.size() << ", image segments=" << images << ", text chars=" << text.size()
         << " [" << codes << "]; empty input segments=" << emptySegs.size());
}

namespace {

// PIM2 / PIM3：一张 4×3、每个像素颜色不同的 32 位 DIB（自下而上），返回 CF_DIB 数据
std::vector<BYTE> MakeDistinctDib()
{
    const size_t pixels = static_cast<size_t>(kPimDibW) * kPimDibH;
    std::vector<BYTE> buf(sizeof(BITMAPINFOHEADER) + pixels * sizeof(uint32_t), 0);
    BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(&buf[0]);
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = kPimDibW;
    bih->biHeight = kPimDibH;
    bih->biPlanes = 1;
    bih->biBitCount = kPimDibBits;
    bih->biCompression = BI_RGB;
    uint32_t* px = reinterpret_cast<uint32_t*>(bih + 1);
    for (size_t i = 0; i < pixels; ++i)
        px[i] = 0xFF000000u | static_cast<uint32_t>(i * 0x101010u);
    return buf;
}

// 自下而上的 DIB 里，(x, y)（y 自上而下）那个像素的颜色
uint32_t DibPixelAt(const std::vector<BYTE>& dib, LONG x, LONG y)
{
    const uint32_t* px = reinterpret_cast<const uint32_t*>(&dib[sizeof(BITMAPINFOHEADER)]);
    const LONG row = kPimDibH - 1 - y;
    return px[static_cast<size_t>(row) * kPimDibW + x] & 0x00FFFFFFu;
}

// PIM2 / PIM3 用的临时 PNG 路径（带进程 id 与用例名），构造时删掉旧文件、析构时删除
class CPimTempPng
{
public:
    explicit CPimTempPng(const wchar_t* tag)
    {
        wchar_t dir[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, dir);
        wchar_t name[MAX_PATH] = { 0 };
        swprintf_s(name, L"flamingo_pim_%lu_%s.png", ::GetCurrentProcessId(), tag);
        m_path = std::wstring(dir) + name;
        ::DeleteFileW(m_path.c_str());
    }

    ~CPimTempPng()
    {
        ::DeleteFileW(m_path.c_str());
    }

    std::wstring m_path;   // 临时 PNG 的完整路径
};

// GDI+ 只启动一次、不关闭（与产品代码一致，见 WindowFlipTests.cpp 的说明）
void EnsureGdiplus()
{
    static ULONG_PTR s_token = 0;
    if (s_token == 0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&s_token, &input, NULL);
    }
}

bool FileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// RichInputSegment 的收集回调（与聊天窗 DoSend 用的 CollectInputSegment 相同）
void CollectRichSegment(bool isImage, LPCTSTR text, DWORD_PTR tag, void* ctx)
{
    std::vector<RichInputSegment>* out = static_cast<std::vector<RichInputSegment>*>(ctx);
    RichInputSegment seg;
    seg.isImage = isImage;
    seg.text = (text != NULL) ? text : _T("");
    seg.tag = tag;
    out->push_back(seg);
}

} // namespace

TEST_CASE("PIM1 粘贴判定：聊天内容 > 文字 > 图片 > 原来的粘贴；只有没有文字、只有图片时按图片处理", "[pasteimage]")
{
    //逐一列出三种内容有无的 8 种组合
    CHECK(DecideChatPaste(false, false, false) == kChatPasteDefault);
    CHECK(DecideChatPaste(false, false, true) == kChatPasteImage);
    CHECK(DecideChatPaste(false, true, false) == kChatPasteDefault);
    CHECK(DecideChatPaste(false, true, true) == kChatPasteDefault);
    CHECK(DecideChatPaste(true, false, false) == kChatPasteContent);
    CHECK(DecideChatPaste(true, false, true) == kChatPasteContent);
    CHECK(DecideChatPaste(true, true, false) == kChatPasteContent);
    CHECK(DecideChatPaste(true, true, true) == kChatPasteContent);
}

TEST_CASE("PIM2 位图转 PNG：读回宽高与像素一致；数据不完整时返回 false、不留文件", "[pasteimage]")
{
    EnsureGdiplus();
    const std::vector<BYTE> dib = MakeDistinctDib();
    CPimTempPng png(L"pim2");
    REQUIRE(SaveDibAsPng(&dib[0], dib.size(), png.m_path));
    REQUIRE(FileExists(png.m_path));
    {
        Gdiplus::Bitmap back(png.m_path.c_str());
        REQUIRE(back.GetLastStatus() == Gdiplus::Ok);
        CHECK(back.GetWidth() == static_cast<UINT>(kPimDibW));
        CHECK(back.GetHeight() == static_cast<UINT>(kPimDibH));
        for (LONG y = 0; y < kPimDibH; ++y)
        {
            for (LONG x = 0; x < kPimDibW; ++x)
            {
                Gdiplus::Color c;
                back.GetPixel(x, y, &c);
                INFO("pixel " << x << "," << y);
                CHECK((c.GetValue() & 0x00FFFFFFu) == DibPixelAt(dib, x, y));
            }
        }
    }

    //字节数不够：少一行像素
    CPimTempPng shortPng(L"pim2short");
    const size_t rowBytes = static_cast<size_t>(kPimDibW) * sizeof(uint32_t);
    CHECK_FALSE(SaveDibAsPng(&dib[0], dib.size() - rowBytes, shortPng.m_path));
    CHECK_FALSE(FileExists(shortPng.m_path));
    //宽为 0
    std::vector<BYTE> zero = dib;
    reinterpret_cast<BITMAPINFOHEADER*>(&zero[0])->biWidth = 0;
    CHECK_FALSE(SaveDibAsPng(&zero[0], zero.size(), shortPng.m_path));
    CHECK_FALSE(FileExists(shortPng.m_path));
    //空指针、只有半个头
    CHECK_FALSE(SaveDibAsPng(NULL, dib.size(), shortPng.m_path));
    CHECK_FALSE(SaveDibAsPng(&dib[0], sizeof(BITMAPINFOHEADER) / 2, shortPng.m_path));
    CHECK_FALSE(FileExists(shortPng.m_path));
}

TEST_CASE("PIM3 按「发送到对话框」带编号插入的 PNG：枚举出带编号的图片段，发送时聚合出 1 个图片部分", "[pasteimage]")
{
    EnsureGdiplus();
    const std::vector<BYTE> dib = MakeDistinctDib();
    CPimTempPng png(L"pim3");
    REQUIRE(SaveDibAsPng(&dib[0], dib.size(), png.m_path));

    std::unique_ptr<ChatInputRichEdit> input(new ChatInputRichEdit());
    input->SetMultiLine(true);
    input->SetWordWrap(true);
    input->SetShowBorder(false);
    RECT rc = { 0, 0, kPimInputW, kPimInputH };
    input->Layout(rc);
    //与聊天窗 StageImageInInput 相同：编号为这张图在待发列表里的下标
    const int index = 0;
    REQUIRE(input->InsertTaggedImage(png.m_path.c_str(), ImageTagFromIndex(index), 0));

    std::vector<RichInputSegment> segs;
    input->EnumContent(&CollectRichSegment, &segs);
    int images = 0;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].isImage)
        {
            ++images;
            CHECK(IsImageTag(segs[i].tag));
        }
    }
    CHECK(images == 1);
    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartImage);
    CHECK(parts[0].imageIndex == index);
}
