/**
 *  远程桌面抓屏封装（被控端），RdScreenCapturer.h
 *
 *  对上层推流器（RdControlledStreamer）暴露"产出引擎帧字节"的简单接口：每轮先 captureFrame 抓一帧，
 *  编码器报告需要关键帧（会话开始、画面尺寸变化）或控制端请求时组 BITMAPINFO 帧与首屏帧，否则组增量帧。内部三件套：
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

    /**
     *  抓一帧。2026-10-14 起抓帧与组帧分开：推流循环先抓，再按编码器的报告（needsKeyframe）决定
     *  发关键帧还是增量帧，画面尺寸变了的那一轮就能发出关键帧。
     *  还没抓到过画面时多等几轮以确保拿到第一帧；之后只抓一次，屏幕没有变化（超时）时沿用上一帧。
     *  2026-10-15 起抓到的画面先存成一份 BGRA（frameData），旧格式编码器在 needsKeyframe / buildKeyframe / buildNext
     *  之前才喂入：H.264 推流循环直接取这份画面编码，不经过旧格式编码器（docs/远程桌面H264与共享画质提升方案.md B3）。
     *  @return 有画面（这一次或之前抓到过）返回 true；抓取后端都不可用、从没抓到过时返回 false
     */
    bool captureFrame();

    //最近一次 captureFrame 是否抓到了新画面（屏幕没变化、超时沿用上一帧时为 false；2026-10-17 起抓到的画面与上一帧
    //完全相同时也为 false）
    bool lastCaptureNew() const;

    //抓取后端报告了新帧的累计次数（2026-10-17，推流统计日志用）
    unsigned int capturedFrames() const;

    //其中画面与上一帧完全相同、因而当作没有新画面的次数（2026-10-17，推流统计日志用）
    unsigned int identicalFrames() const;

    /**
     *  设置单次抓取等新画面的最长时间（2026-10-15）。旧格式循环用默认的 120 毫秒；H.264 循环每轮最多等 33 毫秒，
     *  屏幕不变时也能按约 30 次每秒更新指针。
     *  @param timeoutMs 毫秒，须大于 0
     */
    void setCaptureTimeout(UINT timeoutMs);

    //旧格式编码器是否需要发关键帧：第一次抓到画面、或画面尺寸变了之后为 true，组过关键帧之后为 false
    bool needsKeyframe();

    //最近一次抓到的画面（BGRA，自上而下，行跨度为 frameStride）；还没抓到过时为 NULL
    const BYTE* frameData() const;

    //画面的行跨度（字节）
    int frameStride() const;

    /**
     *  取当前的系统指针位置与类型索引（画面本身不含指针，由控制端另画）。
     *  @param pos  [出参] 指针位置（被控端屏幕坐标）
     *  @param type [出参] 指针类型索引（CCursorInfo）
     */
    void cursor(POINT& pos, BYTE& type);

    /**
     *  用最近一次抓到的画面组关键帧（不抓新画面）：BITMAPINFO 帧与首屏帧。组完之后的增量帧以这一屏为基准。
     *  @param bitmapInfo [出参] rd_frame_bitmapinfo 引擎帧
     *  @param first      [出参] rd_frame_first 引擎帧（整屏 DIB）
     */
    void buildKeyframe(std::vector<BYTE>& bitmapInfo, std::vector<BYTE>& first);

    /**
     *  用最近一次抓到的画面组增量帧（不抓新画面）：rd_frame_next + 头部 + 变化数据块，头部带当前的光标位置与类型。
     *  @param out [出参] 引擎帧
     */
    void buildNext(std::vector<BYTE>& out);

    //主屏宽 / 高（像素）。
    int screenWidth() const;
    int screenHeight() const;

    //最近一次抓到的画面的宽 / 高（像素；2026-10-12）。2026-10-14 起旧格式是否补发关键帧由 needsKeyframe 决定，
    //2026-10-15 起 H.264 循环用它们作源尺寸
    int frameWidth() const;
    int frameHeight() const;

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
    //抓一帧存进 m_bgra。needFrame=true（首帧）时多次重试以确保抓到；否则单次（超时即用上一帧）。
    //内部据降级状态机在 AccessLost / Fatal 时重建 DXGI 或切换 GDI。抓到新画面返回 true
    bool captureOneFrame(bool needFrame);
    //有还没喂给旧格式编码器的新画面时喂入（needsKeyframe / buildKeyframe / buildNext 之前调用）
    void feedLegacy();
    //销毁当前后端并重建 DXGI duplication；成功令 m_capturer 指向新 DXGI 后端。返回是否成功。
    bool rebuildDxgi();
    //销毁当前后端并切到 GDI；成功令 m_capturer 指向 GDI 后端并把状态机置为 GDI，同时安排定期重试 DXGI。返回是否成功。
    bool fallbackToGdi();
    //GDI 后端下试建 DXGI（2026-10-17）：成功则换掉 GDI 后端、状态机置为 DXGI；失败则延后下一次重试。返回是否切回了 DXGI
    bool retryDxgi();

    IScreenCapturer*       m_capturer;    //抓取后端（DXGI 优先 / GDI 降级；本类拥有，析构 delete）
    CScreenFrameEncoder    m_encoder;     //BGRA32 → 现有协议帧编码器
    CCursorInfo            m_cursorInfo;  //当前系统光标类型识别（取索引写进增量帧）
    CCaptureFallbackPolicy m_policy;      //降级决策状态机（DXGI 故障时重建 / 切 GDI）
    std::vector<BYTE>      m_bgra;        //最近一次抓到的画面（BGRA，行跨度 = 宽 × 4；2026-10-15）
    int                    m_bgraW;       //画面宽（像素）
    int                    m_bgraH;       //画面高（像素）
    bool                   m_hasFrame;    //是否抓到过画面
    bool                   m_lastNew;     //最近一次 captureFrame 是否抓到了新画面
    bool                   m_legacyPending; //有还没喂给旧格式编码器的新画面
    UINT                   m_timeoutMs;   //单次抓取等新画面的最长时间（毫秒），见 setCaptureTimeout
    unsigned int           m_capturedFrames;  //抓取后端报告了新帧的累计次数（本对象存续期间）
    unsigned int           m_identicalFrames; //其中画面与上一帧完全相同的次数
};

} // namespace remotedesktop
