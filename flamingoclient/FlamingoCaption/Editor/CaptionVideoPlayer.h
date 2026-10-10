/**
 *  字幕编辑器的视频预览播放器（docs/语音字幕方案.md 第 7 节、14.7），CaptionVideoPlayer.h
 *
 *  基于 Media Foundation 的 IMFMediaEngine 帧服务模式（0e 验证可用）：引擎自己解码、自己放声音，本类在编辑器的
 *  定时器里查询有没有新帧（OnVideoStreamTick），有就用 TransferVideoFrame 按预览区的大小（保持宽高比、四周补黑）
 *  画到一块 D3D11 纹理上，再拷到 CPU 可读的纹理、读出 BGRA 像素交给预览控件。
 *  跳转要像渲染循环那样持续查询新帧（0e 实测只等「跳转完成」事件拿不到新帧），编辑器在跳转后会连续调用 GrabFrame。
 *  分段式 MP4 上跳转不可靠（14.7），编辑器打开录像前会先原样转封装一份普通 MP4 交给本类。
 *
 *  引擎事件（可以播放、跳转完成、播放结束、出错）在引擎的线程上收到，转成 PostMessage 投给编辑器窗口：
 *  wParam 为事件（CaptionPlayerEvent），lParam 不用。只在 UI 线程上调用本类的方法；调用前 UI 线程须已初始化 COM
 *  与 Media Foundation（字幕模块初始化时已做）。
 *
 *  典型用法：
 *      m_player.Open(path, m_hWnd, kMsgCaptionPlayer);
 *      ...（收到 caption_player_ready）
 *      m_player.Play();
 *      if (m_player.GrabFrame(w, h, m_frame, false)) m_preview->SetFrame(m_frame, w, h);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>

#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IMFDXGIDeviceManager;
struct IMFMediaEngine;

namespace caption {

//投给编辑器窗口的播放器事件（消息的 wParam）
enum CaptionPlayerEvent
{
    caption_player_ready  = 1,   //可以播放：首帧已就绪，时长与画面尺寸可读
    caption_player_seeked = 2,   //跳转完成
    caption_player_ended  = 3,   //播放到结尾
    caption_player_error  = 4,   //出错（打不开、解码失败），预览不可用
};

class CaptionPlayerNotify;

/**
 *  CaptionVideoPlayer —— 视频预览播放器。
 */
class CaptionVideoPlayer
{
public:
    CaptionVideoPlayer();
    ~CaptionVideoPlayer();

    CaptionVideoPlayer(const CaptionVideoPlayer&) = delete;
    CaptionVideoPlayer& operator=(const CaptionVideoPlayer&) = delete;

    /**
     *  打开录像（异步：结果经 caption_player_ready / caption_player_error 通知）。已打开时先关闭。
     *  @param path      录像路径
     *  @param notifyWnd 接收事件的窗口
     *  @param msg       事件的消息号
     *  @return 引擎建立并已开始打开时返回 true；建不起 D3D11 设备或媒体引擎时返回 false
     */
    bool Open(const std::wstring& path, HWND notifyWnd, UINT msg);

    //关闭并释放引擎与纹理；可重复调用
    void Close();

    //是否已可播放（收到过 caption_player_ready）
    bool IsReady() const;

    //开始 / 暂停播放
    void Play();
    void Pause();
    bool IsPlaying() const;

    /**
     *  跳到某个时刻（毫秒，夹在 0 与时长之间）。随后编辑器应连续调用 GrabFrame 取跳转后的帧。
     */
    void Seek(int64_t ms);

    //当前播放位置（毫秒）
    int64_t PositionMs() const;

    //时长（毫秒）；还没就绪时为 0
    int64_t DurationMs() const;

    //画面尺寸（像素）；还没就绪时返回 false
    bool VideoSize(uint32_t& width, uint32_t& height) const;

    //静音
    void SetMuted(bool muted);

    /**
     *  取当前帧：按 width × height 缩放（保持宽高比、四周补黑），写成自上而下的 BGRA。
     *  @param width  目标宽（像素）
     *  @param height 目标高（像素）
     *  @param bgra   [出参] width × height 个像素
     *  @param force  为 true 时即使没有新帧也重新取一次（预览区尺寸变了、跳转后补取）
     *  @return 取到了（新的）帧返回 true；没有新帧或出错返回 false
     */
    bool GrabFrame(uint32_t width, uint32_t height, std::vector<uint32_t>& bgra, bool force);

    /**
     *  视频画面在 width × height 预览区里的矩形（保持宽高比、居中），叠字幕时据此定位。
     *  @return 还没就绪时返回整个区域
     */
    RECT VideoRectIn(uint32_t width, uint32_t height) const;

private:
    //按需要（尺寸变化时）重建渲染纹理与 CPU 可读纹理
    bool ensureTextures(uint32_t width, uint32_t height);
    void releaseTextures();

    ID3D11Device*         m_device;        //D3D11 设备（本类持有）
    ID3D11DeviceContext*  m_context;       //设备上下文（本类持有）
    IMFDXGIDeviceManager* m_dxgiManager;   //交给媒体引擎的 DXGI 设备管理器（本类持有）
    IMFMediaEngine*       m_engine;        //媒体引擎（本类持有）
    CaptionPlayerNotify*  m_notify;        //引擎事件回调（引用计数，本类持有一份引用）
    ID3D11Texture2D*      m_renderTex;     //TransferVideoFrame 的目标纹理
    ID3D11Texture2D*      m_stagingTex;    //CPU 可读的纹理
    uint32_t              m_texWidth;      //两块纹理的宽
    uint32_t              m_texHeight;     //两块纹理的高
    LONGLONG              m_lastPts;       //上一次取到的帧的时间戳（100 纳秒）；-1 表示还没取过
};

} // namespace caption
