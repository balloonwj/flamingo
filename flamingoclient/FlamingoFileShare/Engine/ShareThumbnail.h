/**
 *  文件共享分享者一端的图片缩略图与预览图生成，ShareThumbnail.h
 *
 *  访问者在浏览窗口的缩略图视图里、或双击图片预览时发 sf_thumb（docs/文件共享方案.md 5.3、7.4，第 2 期第 4 步），
 *  分享者读出图片、按访问者要的长边缩小（不放大）、编成 JPEG 回 sf_thumb_result：
 *    - 只处理图片（按扩展名：jpg / jpeg / png / bmp / gif / tif / tiff），其余回 error_code_bad_request；
 *    - 源文件超过 kShareThumbMaxSourceBytes 不处理（解码一张巨大的图会占用分享者大量内存），回 error_code_share_io_fail；
 *    - 照片按 EXIF 里的拍摄方向转正；带透明的图片铺白底；动图只取第一帧；
 *    - 长边限制在 [kShareThumbMinEdge, kShareThumbMaxEdge]；JPEG 质量从 kShareThumbQualities 的第一档起，编出来超过
 *      kShareThumbMaxBytes（单帧上限 2 MiB 减去帧头的余量）就换下一档重试，最后一档仍超出则回 error_code_share_io_fail。
 *
 *  使用 GDI+：调用方须已在本进程里初始化 GDI+（CShareService 在 Attach 时初始化、Detach 时关闭）。可在任意线程调用
 *  （分享者在工作线程上执行），不打日志。
 *  单元测试见 FlamingoFileShare/Tests/ShareThumbnailTests.cpp（标签 [sharethumb]，在测试程序目录下用 GDI+ 现做图片）。
 *
 *  典型用法：
 *      std::string jpeg;
 *      int width = 0;
 *      int height = 0;
 *      int code = ShareThumbnail::Make(absPath, maxEdge, jpeg, width, height);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

namespace fileshare {

//缩略图长边的下限与上限（像素）：访问者给的 maxedge 超出时按边界处理
const int kShareThumbMinEdge = 16;
const int kShareThumbMaxEdge = 4096;

//源图片文件的大小上限（字节）：超过时不解码
const int64_t kShareThumbMaxSourceBytes = 64LL * 1024 * 1024;

//编出来的 JPEG 的大小上限（字节）：单帧上限 2 MiB，留出帧头与 JSON 的余量
const size_t kShareThumbMaxBytes = 2u * 1024u * 1024u - 16u * 1024u;

//依次尝试的 JPEG 质量（0~100）：编出来太大就换下一档
const int kShareThumbQualities[] = { 85, 70, 50, 30 };

/**
 *  ShareThumbnail —— 图片缩略图生成；全部为静态方法，不可实例化。
 */
class ShareThumbnail
{
public:
    /**
     *  生成一张缩略图（或预览图）。
     *  @param absPath 图片的本机绝对路径
     *  @param maxEdge 长边像素数；超出 [kShareThumbMinEdge, kShareThumbMaxEdge] 时按边界处理。图片本身更小时不放大
     *  @param jpeg    [出参] JPEG 数据
     *  @param width   [出参] 缩略图宽（像素）
     *  @param height  [出参] 缩略图高（像素）
     *  @return error_code_ok；不是图片为 error_code_bad_request；不存在为 809；源文件过大、解码或编码失败、
     *          最低质量仍超过大小上限为 811
     */
    static int Make(const std::wstring& absPath, int maxEdge, std::string& jpeg, int& width, int& height);

    /**
     *  按扩展名判断是不是本模块处理的图片（不区分大小写）。
     *  @param name 文件名或路径（宽字符或 UTF-8 都可，只看最后一个点之后的部分）
     *  @return 是图片返回 true
     */
    static bool IsImageName(const std::wstring& name);
    static bool IsImageName(const std::string& utf8Name);

    /**
     *  按长边缩放后的尺寸（不放大），纯函数。
     *  @param srcWidth  源宽
     *  @param srcHeight 源高
     *  @param maxEdge   长边上限（已按边界处理过）
     *  @param outWidth  [出参] 结果宽，至少 1
     *  @param outHeight [出参] 结果高，至少 1
     */
    static void FitSize(int srcWidth, int srcHeight, int maxEdge, int& outWidth, int& outHeight);

    //把访问者给的长边限制在 [kShareThumbMinEdge, kShareThumbMaxEdge]
    static int ClampEdge(int maxEdge);

private:
    ShareThumbnail() = delete;
};

} // namespace fileshare
