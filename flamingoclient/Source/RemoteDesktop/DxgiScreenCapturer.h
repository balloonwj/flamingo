/**
 *  远程桌面 DXGI Desktop Duplication 抓取器（被控端），DxgiScreenCapturer.h
 *
 *  IScreenCapturer 的 DXGI 实现：用 DXGI Desktop Duplication 让 GPU 复制桌面帧，
 *  产出 BGRA32 top-down。相比 GDI BitBlt 更高效、能抓到硬件加速 / 全屏内容。
 *
 *  抓帧时序（DXGI 同一时刻只能持一帧）：每次抓帧先释放上一帧（ReleaseFrame +
 *  Unmap），再 AcquireNextFrame → CopyResource 到复用的 staging texture → Map 按
 *  Pitch 逐行读 BGRA。出错码映射：WAIT_TIMEOUT→Timeout、ACCESS_LOST→AccessLost、
 *  其它失败→Fatal（见改造方案 §3、§7）。
 *
 *  本增量（增量 2）只做 happy path（能在普通桌面抓出一帧）；ACCESS_LOST 重建、
 *  屏幕旋转、运行期降级等留后续增量。提供静态调试方法 DebugCaptureToBmp 供
 *  --dxgicap 调试入口在本机显卡上验证能抓出图。
 *
 *  COM 接口用 ATL CComPtr 管理；链接 d3d11.lib / dxgi.lib（在 .cpp 内 #pragma 引）。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <windows.h>
#include <atlbase.h>      // CComPtr
#include <d3d11.h>
#include <dxgi1_2.h>      // IDXGIOutputDuplication / IDXGIOutput1
#include <string>
#include "IScreenCapturer.h"

namespace remotedesktop {

//DXGI Desktop Duplication 抓取器。
class CDxgiScreenCapturer : public IScreenCapturer
{
public:
    CDxgiScreenCapturer();
    virtual ~CDxgiScreenCapturer();

    // ==== IScreenCapturer ====
    virtual bool Init();
    virtual CaptureResult CaptureFrame(UINT timeoutMs, FrameView& out);
    virtual int ScreenWidth()  const;
    virtual int ScreenHeight() const;

    /**
     *  调试用：构造一个抓取器，循环抓到一帧后存成 32 位 bmp 文件。供 --dxgicap 入口
     *  在本机显卡上肉眼验证 DuplicateOutput 能否跑通。
     *  @param path 输出 bmp 文件路径
     *  @param msg  [出参] 人类可读的结果说明（成功 / 失败原因，用于弹框展示）
     *  @return 成功抓到并写出 bmp 返回 true
     */
    static bool DebugCaptureToBmp(const wchar_t* path, std::wstring& msg);

private:
    //先释放上一帧（Unmap + ReleaseFrame）。每次抓新帧前调用。
    void releaseFrameIfHolding();
    //确保 staging texture 存在且尺寸匹配（首帧建、尺寸不变复用）。
    bool ensureStaging(ID3D11Texture2D* frameTex);
    //释放全部 COM 资源。
    void cleanup();

    CComPtr<ID3D11Device>           m_device;        //D3D11 设备
    CComPtr<ID3D11DeviceContext>    m_context;       //立即上下文
    CComPtr<IDXGIOutputDuplication> m_dupl;          //桌面复制对象
    CComPtr<ID3D11Texture2D>        m_staging;       //CPU 可读 staging（复用）
    CComPtr<IDXGISurface>           m_stagingSurf;   //m_staging 的 IDXGISurface（Map 用）
    bool                            m_holdingFrame;  //是否持有未 ReleaseFrame 的帧
    bool                            m_mapped;        //是否有未 Unmap 的映射
    bool                            m_mappedFastlane;//当前映射是否走 fastlane（MapDesktopSurface）
    bool                            m_fastlane;      //桌面镜像在系统内存，可直接 Map（免 staging 拷贝）
    int                             m_width;         //目标屏幕宽（像素）
    int                             m_height;        //目标屏幕高（像素）
    int                             m_stagingW;      //当前 staging 宽（像素，判尺寸变化）
    int                             m_stagingH;      //当前 staging 高（像素）
};

} // namespace remotedesktop
