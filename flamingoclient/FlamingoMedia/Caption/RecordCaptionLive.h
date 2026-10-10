/**
 *  录屏时屏幕上的实时字幕（docs/语音字幕方案.md 14.12），RecordCaptionLive.h
 *
 *  独立录屏勾了「生成字幕」时，字幕草稿会话（ICaptionRecording）把每一路的中间结果与定稿交给本类
 *  （ICaptionRecordingObserver，在字幕 DLL 的识别线程上）；本类排队后投递到自己的消息窗口，在 UI 线程上更新字幕板
 *  （ShareCaptionBoard，与屏幕共享的实时字幕同一套显示规则），用浮层窗口（CShareCaptionOverlay）画在录制区域的底部。
 *  浮层置顶、鼠标穿透，并排除在屏幕采集之外，不会被录进视频；系统不支持排除采集（Windows 10 2004 之前）时浮层不显示，
 *  字幕照常写进草稿。
 *  录制条「字幕：关」时本类清掉显示，并丢弃之后收到的结果；与此同时字幕旁路改送静音（见 RecordCaptionTap.h），
 *  关闭期间不会产生新的字幕。
 *  字幕开着、麦克风与电脑声音却都没在录时（见 RecordCaptionHasSource），浮层上显示一行提示，告诉用户打开其中一路之后
 *  才会有字幕。
 *
 *  只在 UI 线程上调用，两个观察者回调除外。
 *
 *  典型用法（录屏控制器里）：
 *      m_captionLive.Init();
 *      m_captionLive.Start(selfName, micOn, systemOn);
 *      recording->SetObserver(&m_captionLive);
 *      m_captionLive.Tick(recordedRect(), true);      //录制定时器里
 *      m_captionLive.SetEnabled(false);              //录制条「字幕：关」
 *      recording->SetObserver(NULL);
 *      m_captionLive.Stop();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <mutex>
#include <string>
#include <vector>

#include "RecordCaptionLiveRules.h"
#include "ShareCaptionBoard.h"
#include "ShareCaptionOverlay.h"
#include "../../FlamingoCaption/CaptionApi.h"

namespace flamingomedia {

//录制实时字幕的消息窗口收到的消息：识别线程排进了新的结果，请 UI 线程取出（窗口私有消息，见开发约定文档 UI 约定第 6 条）
const UINT kMsgRecordCaptionPending = WM_APP + 0xE9;

/**
 *  CRecordCaptionLive —— 录屏时屏幕上的实时字幕。
 */
class CRecordCaptionLive : public CWindowImpl<CRecordCaptionLive, CWindow, CWinTraits<0, 0> >,
                           public caption::ICaptionRecordingObserver
{
public:
    CRecordCaptionLive();
    ~CRecordCaptionLive();

    /**
     *  建消息窗口；已建时直接返回 true。
     *  @return 消息窗口可用时返回 true
     */
    bool Init();

    //停止显示，销毁浮层与消息窗口；可重复调用
    void Shutdown();

    /**
     *  一次录制开始显示实时字幕（处于「字幕：开」状态）。
     *  @param selfName 本人显示名：两路都在录时作为麦克风一路的说话人
     *  @param micOn    麦克风一路当前是否在录
     *  @param systemOn 电脑声音一路当前是否在录
     */
    void Start(const std::wstring& selfName, bool micOn, bool systemOn);

    //录制结束或字幕变为不可用：清掉显示，之后收到的结果一律丢弃
    void Stop();

    //是否在一次录制的显示中（Start 之后、Stop 之前）
    bool IsActive() const;

    /**
     *  录制中两路声音的开关变了：影响之后的说话人标注（见 RecordCaptionSpeaker），以及「没有声音可识别」的提示（立即重画）。
     *  @param micOn    麦克风一路当前是否在录
     *  @param systemOn 电脑声音一路当前是否在录
     */
    void SetSources(bool micOn, bool systemOn);

    /**
     *  录制条「字幕：开 / 关」。关闭时清掉显示，之后收到的结果丢弃；重新打开后显示新的结果。
     *  @param on true 为打开
     */
    void SetEnabled(bool on);

    //当前是否处于「字幕：开」
    bool IsEnabled() const;

    /**
     *  录制定时器里调用：清掉过期的字幕，浮层跟随录制区域（被录的窗口可能移动或最小化）。
     *  @param area    录制区域（屏幕坐标）
     *  @param visible 录制区域当前是否可见（被录的窗口最小化或已关闭时为 false，浮层随之隐藏）
     */
    void Tick(const RECT& area, bool visible);

    //ICaptionRecordingObserver：识别线程上把结果排进队列，投递消息给 UI 线程
    virtual void OnRecordingPartial(int source, const wchar_t* text) override;
    virtual void OnRecordingFinal(int source, const wchar_t* text) override;

    BEGIN_MSG_MAP(CRecordCaptionLive)
        MESSAGE_HANDLER(kMsgRecordCaptionPending, OnPendingMsg)
    END_MSG_MAP()

private:
    //排队等 UI 线程处理的一条识别结果
    struct Pending
    {
        int          m_source;   //哪一路（CaptionSource 的取值）
        bool         m_final;    //是否为定稿
        std::wstring m_text;     //文字
    };

    //识别线程上排队；需要时投递一条消息
    void enqueue(int source, bool final, const wchar_t* text);
    //清空队列（Stop、关闭字幕时）
    void dropPending();
    //按当前状态重画或隐藏浮层
    void refresh();
    LRESULT OnPendingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    screenshare::ShareCaptionBoard   m_board;       //各路当前的字幕（UI 线程）；座位号取来源的取值
    screenshare::CShareCaptionOverlay m_overlay;    //浮层窗口（UI 线程）
    RecordCaptionSentences           m_sentences;   //各路的句子编号（UI 线程）
    bool                             m_active;      //是否在一次录制的显示中
    bool                             m_enabled;     //是否处于「字幕：开」
    bool                             m_micOn;       //麦克风一路当前是否在录（说话人标注用）
    bool                             m_systemOn;    //电脑声音一路当前是否在录（同上）
    std::wstring                     m_selfName;    //本人显示名
    RECT                             m_area;        //最近一次 Tick 给出的录制区域（屏幕坐标）
    bool                             m_areaVisible; //录制区域当前是否可见；第一次 Tick 之前为 false
    std::mutex                       m_mutex;       //保护以下两项（识别线程写，UI 线程读）
    std::vector<Pending>             m_pending;     //等 UI 线程处理的结果
    bool                             m_posted;      //是否已投递过还没处理的消息
};

} // namespace flamingomedia
