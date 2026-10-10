/**
 *  图片缩略图生成 ShareThumbnail 的实现，见 ShareThumbnail.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareThumbnail.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>

#include "ShareWinPath.h"
#include "../../Source/net/Protocol.h"   //error_code

namespace fileshare {

namespace {

//本模块处理的图片扩展名（小写，不带点）
const wchar_t* const kImageExts[] = { L"jpg", L"jpeg", L"png", L"bmp", L"gif", L"tif", L"tiff" };

//EXIF 拍摄方向的属性号，及其取值 1~8 对应的转正方式（下标 0 不用）
const PROPID kExifOrientation = 0x0112;
const Gdiplus::RotateFlipType kOrientationFix[] = {
    Gdiplus::RotateNoneFlipNone,   //0：不用
    Gdiplus::RotateNoneFlipNone,   //1：正常
    Gdiplus::RotateNoneFlipX,      //2：水平翻转
    Gdiplus::Rotate180FlipNone,    //3：旋转 180 度
    Gdiplus::RotateNoneFlipY,      //4：垂直翻转
    Gdiplus::Rotate90FlipX,        //5：转置
    Gdiplus::Rotate90FlipNone,     //6：顺时针 90 度
    Gdiplus::Rotate270FlipX,       //7：反转置
    Gdiplus::Rotate270FlipNone,    //8：逆时针 90 度
};
//拍摄方向的取值：1 为正常（不用转），最大为 8
const unsigned short kOrientationNormal = 1;
const unsigned short kOrientationMax = 8;

//透明图片铺的底色：白
const Gdiplus::ARGB kBackground = 0xFFFFFFFF;

//小写化
std::wstring Lower(const std::wstring& s)
{
    std::wstring out = s;
    if (!out.empty())
        ::CharLowerBuffW(&out[0], static_cast<DWORD>(out.size()));
    return out;
}

//把整个文件读进内存（支持超长路径）
int ReadWholeFile(const std::wstring& absPath, std::vector<char>& data)
{
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!::GetFileAttributesExW(ShareToLongPath(absPath).c_str(), GetFileExInfoStandard, &attrs)
        || (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return error_code_share_path_not_found;
    const int64_t size = (static_cast<int64_t>(attrs.nFileSizeHigh) << 32) | attrs.nFileSizeLow;
    if (size <= 0 || size > kShareThumbMaxSourceBytes)
        return error_code_share_io_fail;
    HANDLE h = ::CreateFileW(ShareToLongPath(absPath).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return error_code_share_io_fail;
    data.resize(static_cast<size_t>(size));
    DWORD got = 0;
    const BOOL ok = ::ReadFile(h, &data[0], static_cast<DWORD>(size), &got, NULL);
    ::CloseHandle(h);
    return (ok && got == static_cast<DWORD>(size)) ? error_code_ok : error_code_share_io_fail;
}

//在内存上建一个 IStream（数据拷贝一份进去）；失败返回 NULL
IStream* StreamFromBytes(const std::vector<char>& data)
{
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (mem == NULL)
        return NULL;
    void* p = ::GlobalLock(mem);
    if (p == NULL)
    {
        ::GlobalFree(mem);
        return NULL;
    }
    memcpy(p, &data[0], data.size());
    ::GlobalUnlock(mem);
    IStream* stream = NULL;
    //第二个参数为 TRUE：流释放时一并释放这块内存
    if (FAILED(::CreateStreamOnHGlobal(mem, TRUE, &stream)))
    {
        ::GlobalFree(mem);
        return NULL;
    }
    return stream;
}

//取 JPEG 编码器的 CLSID
bool JpegEncoder(CLSID& clsid)
{
    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0)
        return false;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/jpeg") == 0)
        {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//按某个质量编成 JPEG
bool EncodeJpeg(Gdiplus::Bitmap& bitmap, const CLSID& clsid, int quality, std::string& out)
{
    out.clear();
    IStream* stream = NULL;
    if (FAILED(::CreateStreamOnHGlobal(NULL, TRUE, &stream)))
        return false;
    ULONG q = static_cast<ULONG>(quality);
    Gdiplus::EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &q;
    bool ok = (bitmap.Save(stream, &clsid, &params) == Gdiplus::Ok);
    if (ok)
    {
        //流的当前位置就是写入的字节数
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        ULARGE_INTEGER end;
        end.QuadPart = 0;
        ok = SUCCEEDED(stream->Seek(zero, STREAM_SEEK_END, &end)) && SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, NULL));
        if (ok && end.QuadPart > 0)
        {
            out.resize(static_cast<size_t>(end.QuadPart));
            ULONG read = 0;
            ok = SUCCEEDED(stream->Read(&out[0], static_cast<ULONG>(out.size()), &read)) && read == out.size();
        }
    }
    stream->Release();
    return ok && !out.empty();
}

//按 EXIF 拍摄方向把图片转正（没有这个属性时不动）
void ApplyOrientation(Gdiplus::Bitmap& bitmap)
{
    const UINT size = bitmap.GetPropertyItemSize(kExifOrientation);
    if (size == 0)
        return;
    std::vector<BYTE> buf(size);
    Gdiplus::PropertyItem* item = reinterpret_cast<Gdiplus::PropertyItem*>(&buf[0]);
    if (bitmap.GetPropertyItem(kExifOrientation, size, item) != Gdiplus::Ok || item->value == NULL
        || item->type != PropertyTagTypeShort)
        return;
    const unsigned short orientation = *static_cast<unsigned short*>(item->value);
    if (orientation > kOrientationNormal && orientation <= kOrientationMax)
        bitmap.RotateFlip(kOrientationFix[orientation]);
}

} // namespace

int ShareThumbnail::Make(const std::wstring& absPath, int maxEdge, std::string& jpeg, int& width, int& height)
{
    jpeg.clear();
    width = 0;
    height = 0;
    if (!IsImageName(absPath))
        return error_code_bad_request;

    std::vector<char> data;
    const int readCode = ReadWholeFile(absPath, data);
    if (readCode != error_code_ok)
        return readCode;
    IStream* stream = StreamFromBytes(data);
    if (stream == NULL)
        return error_code_share_io_fail;

    int code = error_code_share_io_fail;
    {
        //源位图要在流释放之前析构：GDI+ 按需从流里读数据
        Gdiplus::Bitmap src(stream);
        if (src.GetLastStatus() == Gdiplus::Ok && src.GetWidth() > 0 && src.GetHeight() > 0)
        {
            ApplyOrientation(src);
            int w = 0;
            int h = 0;
            FitSize(static_cast<int>(src.GetWidth()), static_cast<int>(src.GetHeight()), ClampEdge(maxEdge), w, h);
            Gdiplus::Bitmap dst(w, h, PixelFormat24bppRGB);
            Gdiplus::Graphics g(&dst);
            g.Clear(Gdiplus::Color(kBackground));
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            const bool drawn = (g.DrawImage(&src, Gdiplus::Rect(0, 0, w, h)) == Gdiplus::Ok);
            CLSID clsid;
            if (drawn && JpegEncoder(clsid))
            {
                for (size_t i = 0; i < sizeof(kShareThumbQualities) / sizeof(kShareThumbQualities[0]); ++i)
                {
                    std::string out;
                    if (!EncodeJpeg(dst, clsid, kShareThumbQualities[i], out))
                        break;
                    if (out.size() <= kShareThumbMaxBytes)
                    {
                        jpeg.swap(out);
                        width = w;
                        height = h;
                        code = error_code_ok;
                        break;
                    }
                }
            }
        }
    }
    stream->Release();
    return code;
}

bool ShareThumbnail::IsImageName(const std::wstring& name)
{
    const size_t dot = name.find_last_of(L'.');
    const size_t sep = name.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep))
        return false;
    const std::wstring ext = Lower(name.substr(dot + 1));
    for (size_t i = 0; i < sizeof(kImageExts) / sizeof(kImageExts[0]); ++i)
    {
        if (ext == kImageExts[i])
            return true;
    }
    return false;
}

bool ShareThumbnail::IsImageName(const std::string& utf8Name)
{
    std::wstring wide;
    return ShareUtf8ToWide(utf8Name, wide) && IsImageName(wide);
}

void ShareThumbnail::FitSize(int srcWidth, int srcHeight, int maxEdge, int& outWidth, int& outHeight)
{
    outWidth = (srcWidth > 0) ? srcWidth : 1;
    outHeight = (srcHeight > 0) ? srcHeight : 1;
    const int longEdge = (outWidth > outHeight) ? outWidth : outHeight;
    if (maxEdge <= 0 || longEdge <= maxEdge)
        return;
    //按长边等比缩小，四舍五入，至少 1 像素
    const int64_t w = (static_cast<int64_t>(outWidth) * maxEdge * 2 + longEdge) / (static_cast<int64_t>(longEdge) * 2);
    const int64_t h = (static_cast<int64_t>(outHeight) * maxEdge * 2 + longEdge) / (static_cast<int64_t>(longEdge) * 2);
    outWidth = (w > 0) ? static_cast<int>(w) : 1;
    outHeight = (h > 0) ? static_cast<int>(h) : 1;
}

int ShareThumbnail::ClampEdge(int maxEdge)
{
    if (maxEdge < kShareThumbMinEdge)
        return kShareThumbMinEdge;
    if (maxEdge > kShareThumbMaxEdge)
        return kShareThumbMaxEdge;
    return maxEdge;
}

} // namespace fileshare
