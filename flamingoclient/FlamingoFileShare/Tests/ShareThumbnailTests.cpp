/**
 *  图片缩略图生成单元测试（Suite STN，标签 [sharethumb]），ShareThumbnailTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 4 步的 4a）：
 *    - STN1 纯函数：按扩展名判断图片（不区分大小写，宽字符与 UTF-8 两种写法）；按长边等比缩小、不放大、至少 1 像素；
 *           长边上下限；
 *    - STN2 生成：PNG 按长边缩小后编成 JPEG，解码回来尺寸一致；图片比长边小时保持原尺寸；不是图片回 3；不存在回 809；
 *           源文件超过大小上限回 811；
 *    - STN3 照片按 EXIF 拍摄方向转正：方向为 6（顺时针 90 度）的 400×200 图片，缩略图是竖的；
 *    - STN4 透明图片铺白底；
 *    - STN5 编出来的 JPEG 一律不超过大小上限：满屏噪点的大图，要么降质后放得下，要么回 811。
 *  图片在测试程序目录下用 GDI+ 现做。本文件由 FlamingoUnit 编译，被测源文件 Engine/ShareThumbnail.cpp 同时编进 DLL。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>

#include "../Engine/ShareThumbnail.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的临时目录名（每个用例开始前整个删掉）
const wchar_t* const kStnTestDir = L"stn_test";

//测试图片的尺寸与缩略图长边（像素）
const int kWideW = 400;
const int kWideH = 200;
const int kThumbEdge = 100;
const int kNoiseEdge = 4096;

//EXIF 拍摄方向的属性号与「顺时针 90 度」的取值
const PROPID kOrientationTag = 0x0112;
const unsigned short kOrientationRotate90 = 6;

//判定「接近白色」的分量下限
const int kNearWhite = 240;

//STN2：超过源文件大小上限的文件（稀疏文件，不占盘）
const int64_t kTooBigBytes = kShareThumbMaxSourceBytes + 1;

//本套用例的 GDI+ 初始化（构造时启动、析构时关闭）
class GdiplusScope
{
public:
    GdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~GdiplusScope()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    bool      m_ok;     //是否启动成功
    ULONG_PTR m_token;  //GdiplusStartup 给的令牌
};

//取某种格式的编码器
bool EncoderOf(const wchar_t* mime, CLSID& clsid)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, mime) == 0)
        {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//本套用例的临时目录（每次调用都先清空再建）
std::wstring FreshDir()
{
    const std::wstring dir = sharetestfs::ExeDir() + L"\\" + kStnTestDir;
    sharetestfs::DeleteTree(dir);
    sharetestfs::MakeDirs(dir);
    return dir;
}

//做一张纯色图片存成指定格式
bool SaveSolid(const std::wstring& path, int w, int h, Gdiplus::ARGB color, const wchar_t* mime, Gdiplus::PixelFormat format)
{
    Gdiplus::Bitmap bmp(w, h, format);
    Gdiplus::Graphics g(&bmp);
    g.Clear(Gdiplus::Color(color));
    CLSID clsid;
    return EncoderOf(mime, clsid) && bmp.Save(path.c_str(), &clsid, NULL) == Gdiplus::Ok;
}

//把 JPEG 数据解码，返回尺寸与中心点颜色
bool Decode(const std::string& jpeg, int& w, int& h, Gdiplus::Color& center)
{
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, jpeg.size());
    if (mem == NULL)
        return false;
    memcpy(::GlobalLock(mem), jpeg.data(), jpeg.size());
    ::GlobalUnlock(mem);
    IStream* stream = NULL;
    if (FAILED(::CreateStreamOnHGlobal(mem, TRUE, &stream)))
        return false;
    bool ok = false;
    {
        Gdiplus::Bitmap bmp(stream);
        if (bmp.GetLastStatus() == Gdiplus::Ok)
        {
            w = static_cast<int>(bmp.GetWidth());
            h = static_cast<int>(bmp.GetHeight());
            bmp.GetPixel(w / 2, h / 2, &center);
            ok = true;
        }
    }
    stream->Release();
    return ok;
}

} // namespace

TEST_CASE("STN1 image names and fitting", "[fileshare][sharethumb]")
{
    CHECK(ShareThumbnail::IsImageName(std::wstring(L"a.JPG")));
    CHECK(ShareThumbnail::IsImageName(std::wstring(L"D:\\x\\b.png")));
    CHECK(ShareThumbnail::IsImageName(std::string("docs/c.tiff")));
    CHECK_FALSE(ShareThumbnail::IsImageName(std::wstring(L"a.txt")));
    CHECK_FALSE(ShareThumbnail::IsImageName(std::wstring(L"noext")));
    CHECK_FALSE(ShareThumbnail::IsImageName(std::wstring(L"dir.png\\file")));

    int w = 0;
    int h = 0;
    ShareThumbnail::FitSize(kWideW, kWideH, kThumbEdge, w, h);
    CHECK(w == kThumbEdge);
    CHECK(h == kThumbEdge / 2);
    ShareThumbnail::FitSize(kWideH, kWideW, kThumbEdge, w, h);
    CHECK(w == kThumbEdge / 2);
    CHECK(h == kThumbEdge);
    //不放大
    ShareThumbnail::FitSize(kWideW, kWideH, kWideW * 2, w, h);
    CHECK(w == kWideW);
    CHECK(h == kWideH);
    //极扁的图：至少 1 像素
    ShareThumbnail::FitSize(kNoiseEdge, 1, kThumbEdge, w, h);
    CHECK(w == kThumbEdge);
    CHECK(h == 1);

    CHECK(ShareThumbnail::ClampEdge(0) == kShareThumbMinEdge);
    CHECK(ShareThumbnail::ClampEdge(kShareThumbMaxEdge + 1) == kShareThumbMaxEdge);
    CHECK(ShareThumbnail::ClampEdge(kThumbEdge) == kThumbEdge);
}

TEST_CASE("STN2 make thumbnails", "[fileshare][sharethumb]")
{
    GdiplusScope gdi;
    REQUIRE(gdi.m_ok);
    const std::wstring dir = FreshDir();
    const std::wstring png = dir + L"\\wide.png";
    REQUIRE(SaveSolid(png, kWideW, kWideH, 0xFFFF0000, L"image/png", PixelFormat24bppRGB));

    std::string jpeg;
    int w = 0;
    int h = 0;
    REQUIRE(ShareThumbnail::Make(png, kThumbEdge, jpeg, w, h) == error_code_ok);
    CHECK(w == kThumbEdge);
    CHECK(h == kThumbEdge / 2);
    int dw = 0;
    int dh = 0;
    Gdiplus::Color center;
    REQUIRE(Decode(jpeg, dw, dh, center));
    CHECK(dw == w);
    CHECK(dh == h);
    CHECK(center.GetR() > kNearWhite);
    CHECK(center.GetG() < kNearWhite / 2);

    //比长边小：保持原尺寸
    REQUIRE(ShareThumbnail::Make(png, kWideW * 2, jpeg, w, h) == error_code_ok);
    CHECK(w == kWideW);
    CHECK(h == kWideH);

    //不是图片、不存在、源文件过大
    REQUIRE(sharetestfs::WriteFileBytes(dir + L"\\a.txt", 1));
    CHECK(ShareThumbnail::Make(dir + L"\\a.txt", kThumbEdge, jpeg, w, h) == error_code_bad_request);
    CHECK(jpeg.empty());
    CHECK(ShareThumbnail::Make(dir + L"\\none.png", kThumbEdge, jpeg, w, h) == error_code_share_path_not_found);
    const std::wstring big = dir + L"\\big.png";
    HANDLE f = ::CreateFileW(big.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    REQUIRE(f != INVALID_HANDLE_VALUE);
    DWORD ret = 0;
    ::DeviceIoControl(f, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
    LARGE_INTEGER li;
    li.QuadPart = kTooBigBytes;
    ::SetFilePointerEx(f, li, NULL, FILE_BEGIN);
    ::SetEndOfFile(f);
    ::CloseHandle(f);
    CHECK(ShareThumbnail::Make(big, kThumbEdge, jpeg, w, h) == error_code_share_io_fail);
    //文件内容不是图片
    REQUIRE(sharetestfs::WriteFileBytes(dir + L"\\fake.jpg", kThumbEdge));
    CHECK(ShareThumbnail::Make(dir + L"\\fake.jpg", kThumbEdge, jpeg, w, h) == error_code_share_io_fail);
}

TEST_CASE("STN3 exif orientation", "[fileshare][sharethumb]")
{
    GdiplusScope gdi;
    REQUIRE(gdi.m_ok);
    const std::wstring dir = FreshDir();
    const std::wstring path = dir + L"\\photo.jpg";
    {
        Gdiplus::Bitmap bmp(kWideW, kWideH, PixelFormat24bppRGB);
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(0xFF3060A0));
        unsigned short orientation = kOrientationRotate90;
        Gdiplus::PropertyItem item;
        item.id = kOrientationTag;
        item.length = sizeof(orientation);
        item.type = PropertyTagTypeShort;
        item.value = &orientation;
        REQUIRE(bmp.SetPropertyItem(&item) == Gdiplus::Ok);
        CLSID clsid;
        REQUIRE(EncoderOf(L"image/jpeg", clsid));
        REQUIRE(bmp.Save(path.c_str(), &clsid, NULL) == Gdiplus::Ok);
    }
    std::string jpeg;
    int w = 0;
    int h = 0;
    REQUIRE(ShareThumbnail::Make(path, kThumbEdge, jpeg, w, h) == error_code_ok);
    CHECK(w == kThumbEdge / 2);
    CHECK(h == kThumbEdge);
}

TEST_CASE("STN4 transparent images get a white background", "[fileshare][sharethumb]")
{
    GdiplusScope gdi;
    REQUIRE(gdi.m_ok);
    const std::wstring dir = FreshDir();
    const std::wstring path = dir + L"\\clear.png";
    REQUIRE(SaveSolid(path, kWideW, kWideH, 0x00000000, L"image/png", PixelFormat32bppARGB));
    std::string jpeg;
    int w = 0;
    int h = 0;
    REQUIRE(ShareThumbnail::Make(path, kThumbEdge, jpeg, w, h) == error_code_ok);
    int dw = 0;
    int dh = 0;
    Gdiplus::Color center;
    REQUIRE(Decode(jpeg, dw, dh, center));
    CHECK(center.GetR() > kNearWhite);
    CHECK(center.GetG() > kNearWhite);
    CHECK(center.GetB() > kNearWhite);
}

TEST_CASE("STN5 jpeg size never exceeds the limit", "[fileshare][sharethumb]")
{
    GdiplusScope gdi;
    REQUIRE(gdi.m_ok);
    const std::wstring dir = FreshDir();
    const std::wstring path = dir + L"\\noise.png";
    {
        Gdiplus::Bitmap bmp(kNoiseEdge, kNoiseEdge, PixelFormat24bppRGB);
        Gdiplus::Rect rect(0, 0, kNoiseEdge, kNoiseEdge);
        Gdiplus::BitmapData data;
        REQUIRE(bmp.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &data) == Gdiplus::Ok);
        //线性同余伪随机数填满每个字节：JPEG 几乎压不动
        unsigned seed = 12345u;
        for (int y = 0; y < kNoiseEdge; ++y)
        {
            BYTE* row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
            for (int x = 0; x < kNoiseEdge * 3; ++x)
            {
                seed = seed * 1103515245u + 12345u;
                row[x] = static_cast<BYTE>(seed >> 16);
            }
        }
        bmp.UnlockBits(&data);
        CLSID clsid;
        REQUIRE(EncoderOf(L"image/png", clsid));
        REQUIRE(bmp.Save(path.c_str(), &clsid, NULL) == Gdiplus::Ok);
    }
    std::string jpeg;
    int w = 0;
    int h = 0;
    const int code = ShareThumbnail::Make(path, kShareThumbMaxEdge, jpeg, w, h);
    CHECK((code == error_code_ok || code == error_code_share_io_fail));
    if (code == error_code_ok)
        CHECK(jpeg.size() <= kShareThumbMaxBytes);
    else
        CHECK(jpeg.empty());
    //缩小到小图时一定放得下
    CHECK(ShareThumbnail::Make(path, kThumbEdge, jpeg, w, h) == error_code_ok);
    CHECK(jpeg.size() <= kShareThumbMaxBytes);
}
