/**
 *  「图片文字识别」入口的发起调度，说明见 OcrLauncher.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrLauncher.h"

#include "OcrModuleHost.h"

namespace ocr {

namespace {

//一圈的度数与 90° 的度数
const int kDegreesPerCircle = 360;
const int kDegreesPerQuarter = 90;

//注册的实现；只在界面线程读写
OcrFileLauncherFn g_fileLauncher = NULL;
OcrImageLauncherFn g_imageLauncher = NULL;

} // namespace

void SetOcrLaunchers(OcrFileLauncherFn fileLauncher, OcrImageLauncherFn imageLauncher)
{
    g_fileLauncher = fileLauncher;
    g_imageLauncher = imageLauncher;
}

bool IsOcrEntryVisible()
{
    return g_fileLauncher != NULL && COcrModuleHost::GetInstance().CanShowEntry();
}

bool LaunchOcrForFile(HWND owner, const std::wstring& path, int quarterTurns)
{
    if (g_fileLauncher == NULL || path.empty())
        return false;
    return g_fileLauncher(owner, path, quarterTurns);
}

bool LaunchOcrForImage(HWND owner, const OcrBgraImage& image)
{
    if (g_imageLauncher == NULL || image.m_width == 0 || image.m_height == 0 || image.m_pixels.empty())
        return false;
    return g_imageLauncher(owner, image);
}

int QuarterTurnsOf(int degreesClockwise)
{
    int degrees = degreesClockwise % kDegreesPerCircle;
    if (degrees < 0)
        degrees += kDegreesPerCircle;
    return degrees / kDegreesPerQuarter;
}

} // namespace ocr
