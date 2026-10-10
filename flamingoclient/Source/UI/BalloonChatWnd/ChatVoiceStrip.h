/**
 *  ChatVoiceStrip.h
 *
 *  可以单独放进任意布局里的语音条控件：外观与聊天窗的语音气泡一致（底色、喇叭、波形、时长、未听红点、播放中
 *  声波轮换），绘制全部转调 ChatVoicePaint.h 的共用函数，不另起一套。消息记录窗与合并记录查看窗的语音行用它
 *  （语音消息第 4 期）。
 *
 *  单击语音条时与聊天气泡一样向宿主窗口投递 WM_CHAT_VOICE_ACTION（wParam 为 kVoiceActionToggle，lParam 为堆上的
 *  VoiceActionRequest，宿主处理后 delete），本控件不碰播放与下载。播放中、帧号、已听、下载状态的变化由宿主经
 *  SetVoiceMessage 交回来。
 *
 *  存在理由：balloonui 里没有能画语音条的控件，聊天气泡的绘制又与整行消息（头像、名字、气泡方向）绑在一起。
 *  本类只包一层 DuiControl，是 UI 约定第 2 条里的自绘控件，2026-10-04 已征得用户同意（与聊天文件卡片控件
 *  ChatFileCardControl 同一种做法）。
 *
 *  典型用法：
 *      std::unique_ptr<ChatVoiceStrip> strip(new ChatVoiceStrip());
 *      strip->SetVoiceMessage(msg, true);
 *      layout->AddChild(std::move(strip), Hint().Fixed(kVoiceStripH, VoiceStripWidth(msg)));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ChatMockData.h"      // ChatMessage
#include "ChatVoiceBubble.h"   // VoiceActionRequest

namespace balloonchatwnd {

//语音条控件的高度（像素），与聊天窗单行文字气泡在默认字号下的高度相近
const int kVoiceStripH = 40;

/**
 *  语音条控件需要的宽度：语音条本身（随时长变化，见 VoiceBubbleWidth）加上右侧红点留出的空间。
 *  @param m 语音消息
 *  @return 宽度（像素）
 */
int VoiceStripWidth(const ChatMessage& m);

/**
 *  语音条在控件里的矩形：从控件左边开始、宽为 VoiceBubbleWidth、高为控件高度；红点画在它右侧的留白里。
 *  @param item 控件矩形
 *  @param m    语音消息
 *  @return 语音条矩形；点击只在它里面才算点中
 */
RECT VoiceStripBarRect(const RECT& item, const ChatMessage& m);

/**
 *  单击语音条时投给宿主的请求（WM_CHAT_VOICE_ACTION 的 lParam）。
 *  @param m 语音消息
 *  @return 带 msgid 与 clientMsgId 的请求；调用方负责 new 一份交给 PostMessage
 */
VoiceActionRequest MakeVoiceToggleRequest(const ChatMessage& m);

/**
 *  语音条控件。宽高由布局给定，应为 VoiceStripWidth(消息) × kVoiceStripH。
 */
class ChatVoiceStrip : public balloonwjui::DuiControl
{
public:
    ChatVoiceStrip();

    /**
     *  设置显示的语音消息并重画。
     *  @param m       语音消息（kind 应为 kMsgVoice），按值保存
     *  @param showDot 是否按已听状态画未听红点；合并记录查看窗没有已听状态，传 false
     */
    void SetVoiceMessage(const ChatMessage& m, bool showDot);

    /**
     *  取当前显示的语音消息。
     *  @return 消息的引用，生命周期同本控件
     */
    const ChatMessage& VoiceMessage() const;

    //DuiControl 覆写：画底色与语音条内容。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    //DuiControl 覆写：光标在语音条上时显示手型。返回是否已处理。
    bool OnSetCursor(POINT pt) override;
    //DuiControl 覆写：点在语音条上时向宿主投递播放 / 停止请求。返回是否已处理。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：双击时什么也不做（双击的第一下按下已经切换了播放），只拦下来不交给所在行（行的双击是
    //「定位到聊天窗」）。返回 true。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

private:
    //向宿主窗口投递 WM_CHAT_VOICE_ACTION（kVoiceActionToggle）
    void PostToggle();

    ChatMessage m_msg;      //显示的语音消息
    bool        m_showDot;  //是否画未听红点
};

} // namespace balloonchatwnd
