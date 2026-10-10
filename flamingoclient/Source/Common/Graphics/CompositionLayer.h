/**
 *  CompositionLayer.h
 *
 *  用 DirectComposition 在一个顶层窗口上显示一张逐像素透明的图层。
 *
 *  背景：逐像素透明的窗口通常用 UpdateLayeredWindow 实现，但那种窗口不能排除在屏幕采集之外
 *  （屏幕共享第 2h 期实测，SetWindowDisplayAffinity 返回错误 8）；颜色键分层窗口能排除，像素却只能全透明
 *  或全不透明，抗锯齿的边缘是台阶状的锯齿。本类改走 DirectComposition：窗口建成「不要重定向位图」
 *  （WS_EX_NOREDIRECTIONBITMAP）的分层窗口，整窗不透明度 255，内容是一条预乘透明度的交换链，由 DWM 按
 *  逐像素透明度合成。这种窗口既能逐像素透明，又能用 WDA_EXCLUDEFROMCAPTURE 排除在采集之外；不排除时
 *  它与普通窗口一样会被屏幕读取采集到（Tests/ShareCompositionLayerTests.cpp 的 SSAA1 两种情况都实测过）。
 *
 *  使用方：屏幕共享发起人本机的标注层（FlamingoMedia.dll，经 FlamingoMedia/Share/ShareCompositionLayer.h
 *  的类型别名使用）与聊天字幕窗口（Flamingo.exe，UI/BalloonSubtitle/SubtitleWindow）。本文件由 exe、
 *  FlamingoMedia 与 FlamingoUnit 三个工程编译同一份源文件（2026-10-05 由 FlamingoMedia/Share/ 挪到此处）。
 *
 *  用法（都在窗口所在的界面线程上调用）：
 *      graphics::CCompositionLayer layer;
 *      if (layer.Attach(hwnd))                    // 窗口创建时须带 WS_EX_NOREDIRECTIONBITMAP
 *          layer.Present(bgra, width, height);    // 每次内容变化时送一整帧（预乘透明度的 BGRA，自上而下）
 *      layer.Detach();                            // 窗口销毁前
 *
 *  DirectComposition 要求 Windows 8 及以上：dcomp.dll 的入口运行期再取，取不到时 Attach 返回 false，
 *  调用方改用其它显示方式，各模块在 Windows 7 上照常加载。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include <stdint.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct IDCompositionDevice;
struct IDCompositionTarget;
struct IDCompositionVisual;

namespace graphics {

//不要重定向位图的扩展样式 WS_EX_NOREDIRECTIONBITMAP（Windows 8 起；本仓库按 Windows 7 编译，WinUser.h 不定义它）。
//交给 CCompositionLayer 的窗口创建时须带这个样式
const DWORD kExStyleNoRedirectionBitmap = 0x00200000L;

//在一个顶层窗口上显示逐像素透明图层的 DirectComposition 封装；不可复制
class CCompositionLayer
{
public:
    CCompositionLayer();
    ~CCompositionLayer();

    /**
     *  为窗口建立 Direct3D 设备、DirectComposition 设备、合成目标与可视对象。
     *  @param hwnd 顶层窗口，创建时须带 WS_EX_NOREDIRECTIONBITMAP；不拥有
     *  @return 成功返回 true；系统低于 Windows 8、显卡与软件渲染都建不出设备、或任何一步失败时返回 false，
     *          此时内部已全部释放，调用方应改用其它显示方式
     */
    bool Attach(HWND hwnd);

    /**
     *  显示一整帧。尺寸与上一帧不同时重建交换链缓冲区。
     *  @param bgra   预乘透明度的 BGRA 像素，自上而下，每行 width * 4 字节；不拥有，调用返回后即可释放
     *  @param width  宽，像素，须大于 0
     *  @param height 高，像素，须大于 0
     *  @return 已提交给 DWM 返回 true；未 Attach、参数无效或设备丢失时返回 false
     */
    bool Present(const uint8_t* bgra, int width, int height);

    //释放全部 Direct3D 与 DirectComposition 对象；可重复调用。窗口销毁前调用
    void Detach();

    //是否已成功 Attach
    bool IsAttached() const;

private:
    CCompositionLayer(const CCompositionLayer&);
    CCompositionLayer& operator=(const CCompositionLayer&);

    //按尺寸建立或调整交换链，并挂到可视对象上
    bool ensureSwapChain(int width, int height);

    HMODULE              m_dcompDll;    //dcomp.dll，Attach 时加载、Detach 时释放；Windows 7 上为 NULL
    ID3D11Device*        m_device;      //Direct3D 11 设备（硬件，建不出时用 WARP 软件渲染）
    ID3D11DeviceContext* m_context;     //设备的立即上下文，用来把像素写进交换链缓冲区
    IDXGISwapChain1*     m_swapChain;   //供合成用的交换链（预乘透明度、两个缓冲区）；第一次 Present 时创建
    IDCompositionDevice* m_dcomp;       //DirectComposition 设备
    IDCompositionTarget* m_target;      //窗口的合成目标（内容放在窗口最上层）
    IDCompositionVisual* m_visual;      //唯一的可视对象，内容为 m_swapChain
    int                  m_width;       //交换链当前宽，像素；未创建时为 0
    int                  m_height;      //交换链当前高，像素；未创建时为 0
};

} // namespace graphics
