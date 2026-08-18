/**
 *  远程桌面抓屏封装（被控端），RdScreenCapturer.h
 *
 *  对上层推流器（RdControlledStreamer）暴露"产出引擎帧字节"的简单接口：会话开始发一
 *  次 BITMAPINFO 帧与首屏帧，之后周期性发增量帧。内部三件套：
 *    · IScreenCapturer 抓取后端——优先 DXGI Desktop Duplication，初始化失败则降级 GDI；
 *      两者都产出 BGRA32 top-down；
 *    · CScreenFrameEncoder 编码器——把 BGRA32 转成 16 位 RGB555 DIB + 现有 DIFF 协议帧
 *      （rd_frame_bitmapinfo / first / next），协议与控制端解码器零改；
 *    · CCursorInfo——取当前系统光标类型索引，连同光标位置写进增量帧（画面本身不含光标，
 *      由观看窗本地绘制，与旧引擎一致）。
 *
 *  本类只负责"抓 + 编码成字节"，不碰网络与 UI。抓取在调用方推流线程进行（该线程已切到
 *  Per-Monitor-V2，故 GDI / DXGI 均抓物理像素）。
 *
 *  balloonwj@qq.com   2026-06-08
 *  2026-06-11 抓取从旧 CScreenSpy 改为 IScreenCapturer（DXGI 主 + GDI 降级）+ 独立编码器
 */
#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include "IScreenCapturer.h"
#include "ScreenFrameEncoder.h"
#include "CaptureFallbackPolicy.h"
#include "CursorInfo.h"

namespace remotedesktop {

class RdScreenCapturer
{
public:
    /**
     *  @param bitCount  兼容旧签名保留；当前内部编码固定 16 位 RGB555，参数被忽略
     *  @param preferGdi true 时跳过 DXGI、直接用 GDI 后端（仅调试 / 强制验证降级路径用，
     *                   生产默认 false：优先 DXGI、失败再降级 GDI）
     */
    explicit RdScreenCapturer(int bitCount = 16, bool preferGdi = false);
    ~RdScreenCapturer();

    RdScreenCapturer(const RdScreenCapturer& rhs) = delete;
    RdScreenCapturer& operator=(const RdScreenCapturer& rhs) = delete;

    //抓取后端是否就绪（DXGI 或 GDI 任一 Init 成功）。
    bool isReady() const;

    //产出 BITMAPINFO 引擎帧（rd_frame_bitmapinfo + BITMAPINFO 字节）。首次调用会先抓一帧建立尺寸。
    void captureBitmapInfo(std::vector<BYTE>& out);
    //产出首屏引擎帧（rd_frame_first + 整屏 DIB）。
    void captureFirst(std::vector<BYTE>& out);
    //产出增量引擎帧（rd_frame_next + 头部 + 变化数据块 + 光标位置/类型）。
    void captureNext(std::vector<BYTE>& out);

    //主屏宽 / 高（像素）。
    int screenWidth() const;
    int screenHeight() const;

    /**
     *  调试用：本地走通整条抓屏链路（抓取后端 → 编码器 → 解码器 → 绘制），把解码还原的
     *  画面存成 32 位 bmp，供 --dxgipipe 入口不连服务器就肉眼确认 DXGI 抓 → 16 位编码 →
     *  DIFF → 解码出图正确（方向 / 颜色 / 16 位量化效果 / 光标）。
     *  @param path      输出 bmp 文件路径
     *  @param msg       [出参] 人类可读的结果说明
     *  @param preferGdi true 时强制走 GDI 后端（验证降级路径出图）；false 走默认 DXGI 优先
     *  @return 成功返回 true
     */
    static bool DebugPipelineToBmp(const wchar_t* path, std::wstring& msg, bool preferGdi = false);

private:
    //抓一帧并喂给编码器。needFrame=true（首帧）时多次重试以确保抓到；否则单次（超时即用上一帧）。
    //内部据降级状态机在 AccessLost / Fatal 时重建 DXGI 或切换 GDI。
    bool captureOneFrame(bool needFrame);
    //销毁当前后端并重建 DXGI duplication；成功令 m_capturer 指向新 DXGI 后端。返回是否成功。
    bool rebuildDxgi();
    //销毁当前后端并切到 GDI；成功令 m_capturer 指向 GDI 后端并把状态机置为 GDI。返回是否成功。
    bool fallbackToGdi();

    IScreenCapturer*       m_capturer;    //抓取后端（DXGI 优先 / GDI 降级；本类拥有，析构 delete）
    CScreenFrameEncoder    m_encoder;     //BGRA32 → 现有协议帧编码器
    CCursorInfo            m_cursorInfo;  //当前系统光标类型识别（取索引写进增量帧）
    CCaptureFallbackPolicy m_policy;      //降级决策状态机（DXGI 故障时重建 / 切 GDI）
};

} // namespace remotedesktop
