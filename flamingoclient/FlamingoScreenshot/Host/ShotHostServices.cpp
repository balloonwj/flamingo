/**
 *  ShotHostServices.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ShotHostServices.h"

namespace flamingoshot {

namespace {

//登记的宿主接口；NULL 表示尚未初始化或已关闭。只在宿主的界面线程上读写
IScreenshotHost* g_servicesHost = NULL;

} // namespace

void ShotHostServicesSetHost(IScreenshotHost* host)
{
    g_servicesHost = host;
}

bool ShotOcrAvailable()
{
    return g_servicesHost != NULL && g_servicesHost->IsOcrAvailable();
}

bool ShotLaunchOcr(HWND owner, const ShotBgraImage& image)
{
    if (g_servicesHost == NULL || image.m_pixels.empty() || image.m_width == 0 || image.m_height == 0)
    {
        return false;
    }
    return g_servicesHost->LaunchOcrForImage(owner, &image.m_pixels[0], image.m_width, image.m_height);
}

} // namespace flamingoshot
