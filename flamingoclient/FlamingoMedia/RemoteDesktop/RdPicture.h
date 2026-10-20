/**
 *  远程桌面控制端的画面接口，RdPicture.h
 *
 *  2026-10-15 远程桌面改用 H.264 后（docs/远程桌面H264与共享画质提升方案.md 4.7），控制端的画面有两种来源：旧格式的
 *  RdScreenDecoder（16 位 DIB + DIFF）与新的 RdVideoPicture（H.264，解码在自己的线程上）。观看窗与画布只认本接口，
 *  会话中途可以由旧格式切到 H.264（被控端收到带能力位的请求后切换），也可以切回旧格式（被控端编码器中途失败）。
 *  选用哪一个由 RdPictureSelector 决定：最近一次就绪的那一种。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#pragma once

#include <windows.h>

namespace remotedesktop {

//控制端的画面
class IRdPicture
{
public:
    virtual ~IRdPicture()
    {
    }

    //是否有画面可画
    virtual bool ready() const = 0;

    //源宽 / 高（被控端主屏的物理像素）：等比显示的宽高比与鼠标坐标的换算都按它，与编码尺寸无关
    virtual int width() const = 0;
    virtual int height() const = 0;

    /**
     *  把当前画面画到 hdc 的 dest 矩形（按需缩放；调用方已设好缩放模式）。没有画面时不画。
     *  @param hdc  目标设备上下文
     *  @param dest 目标矩形（窗口客户区坐标）
     */
    virtual void drawTo(HDC hdc, const RECT& dest) const = 0;

    //被控端最近一次报告的指针位置（被控端屏幕坐标）与指针类型索引
    virtual POINT cursorPos() const = 0;
    virtual BYTE cursorType() const = 0;
};

//画面来源
enum rd_picture_kind
{
    rd_picture_none   = 0,   //还没有画面
    rd_picture_legacy = 1,   //旧格式（RdScreenDecoder）
    rd_picture_video  = 2,   //H.264（RdVideoPicture）
};

//选用哪一种画面：最近一次就绪（旧格式收到关键帧且就绪、H.264 解出画面）的那一种；只在 UI 线程上使用
class RdPictureSelector
{
public:
    RdPictureSelector();

    //旧格式收到了关键帧（BITMAPINFO + 首屏）且已就绪：被控端在发旧格式（含中途从 H.264 退回）
    //@return 选用的画面因此变了返回 true
    bool OnLegacyKeyframe();

    //H.264 解出了一帧画面
    //@return 选用的画面因此变了返回 true
    bool OnVideoFrame();

    //当前选用的画面，取值见 rd_picture_kind
    int Active() const;

    //会话结束或重新连接：回到没有画面
    void Reset();

private:
    int m_active;   //当前选用的画面，取值见 rd_picture_kind
};

} // namespace remotedesktop
