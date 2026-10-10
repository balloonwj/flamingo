/**
 *  ChatVoicePaint.cpp
 *
 *  语音条的绘制，见 ChatVoicePaint.h。几何与配色原先写在 ChatBubble.cpp 里，第 4 期抽到这里与语音条控件共用。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include <gdiplus.h>

#include "ChatVoicePaint.h"
#include "ChatVoiceBubble.h"   // FormatVoiceDuration / ShowVoiceUnplayedDot / ChatVoiceState
#include "ChatIcons.h"         // DrawChatIcon / kChatIconVoiceWave1~3
#include "ChatPaintUtil.h"     // DrawGdiText

namespace balloonchatwnd {

namespace {

//喇叭图标声波的线宽（以图标 viewBox 24 为基准）
const Gdiplus::REAL kVoiceIconStroke = 1.6f;
//图标与波形的颜色（深灰）
const Gdiplus::Color kVoiceInkColor(255, 0x55, 0x55, 0x55);
//时长文字的颜色
const COLORREF kVoiceDurationColor = RGB(0x55, 0x55, 0x55);
//未听红点的颜色：与发送失败的红色感叹号同一种红（ImagePlaceholder.h 的 kImagePlaceholderCaptionRed）
const Gdiplus::Color kVoiceDotColor(255, 0xD9, 0x53, 0x4F);

} // namespace

int VoiceWaveBarCount(int barWidth)
{
    //波形区：喇叭图标右边（左内边距 + 图标 + 间距）到时长文字左边（右内边距 + 时长区 + 间距）
    const int waveW = barWidth - (kVoicePadH + kVoiceIconSize + kVoiceGap) - (kVoicePadH + kVoiceDurationW + kVoiceGap);
    //最后一根柱子后面不需要间距，故分子先补一个间距
    const int bars = (waveW + kVoiceBarGap) / (kVoiceBarW + kVoiceBarGap);
    return bars > 0 ? bars : 0;
}

void PaintVoiceBarShapes(Gdiplus::Graphics& g, const RECT& bar, const ChatMessage& m, bool drawDot)
{
    const int barW = bar.right - bar.left;
    const int barH = bar.bottom - bar.top;
    if (barW <= 0 || barH <= 0)
    {
        return;
    }
    //喇叭：不在播放时画满 3 道声波；播放中按帧轮换 1～3 道
    const ChatIconKind icon = !m.voicePlaying ? kChatIconVoiceWave3
        : (m.voicePlayFrame == 0 ? kChatIconVoiceWave1
           : (m.voicePlayFrame == 1 ? kChatIconVoiceWave2 : kChatIconVoiceWave3));
    const int iconX = bar.left + kVoicePadH;
    DrawChatIcon(g, icon, (Gdiplus::REAL)iconX, (Gdiplus::REAL)(bar.top + (barH - kVoiceIconSize) / 2),
                 (Gdiplus::REAL)kVoiceIconSize, kVoiceInkColor, kVoiceIconStroke);

    //波形：图标右边到时长左边之间，按可用宽度从消息的波形值里等距取样
    const int waveL = iconX + kVoiceIconSize + kVoiceGap;
    const int bars = VoiceWaveBarCount(barW);
    const size_t levels = m.voiceWave.size();
    if (bars > 0 && levels > 0)
    {
        Gdiplus::SolidBrush barBrush(kVoiceInkColor);
        const int midY = bar.top + barH / 2;
        for (int i = 0; i < bars; ++i)
        {
            const size_t idx = static_cast<size_t>(i) * levels / static_cast<size_t>(bars);
            const int level = m.voiceWave[idx < levels ? idx : levels - 1];
            const int h = kVoiceBarMinH + (kVoiceBarMaxH - kVoiceBarMinH) * level / kVoiceLevelMax;
            g.FillRectangle(&barBrush, (Gdiplus::REAL)(waveL + i * (kVoiceBarW + kVoiceBarGap)),
                            (Gdiplus::REAL)(midY - h / 2), (Gdiplus::REAL)kVoiceBarW, (Gdiplus::REAL)h);
        }
    }

    //对方发来、本机还没听过：语音条右侧外边画红点
    if (drawDot && ShowVoiceUnplayedDot(m.fromMe, m.voicePlayed))
    {
        Gdiplus::SolidBrush dotBrush(kVoiceDotColor);
        g.FillEllipse(&dotBrush, (Gdiplus::REAL)(bar.right + kVoiceDotGap),
                      (Gdiplus::REAL)(bar.top + (barH - kVoiceDotSize) / 2),
                      (Gdiplus::REAL)kVoiceDotSize, (Gdiplus::REAL)kVoiceDotSize);
    }
}

void PaintVoiceBarText(HDC hdc, const RECT& bar, const ChatMessage& m)
{
    RECT durRc = { bar.right - kVoicePadH - kVoiceDurationW, bar.top, bar.right - kVoicePadH, bar.bottom };
    chatpaint::DrawGdiText(hdc, FormatVoiceDuration(m.voiceDuration).c_str(), durRc, kVoiceDurationPx, false,
                           kVoiceDurationColor, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace balloonchatwnd
