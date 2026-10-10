/**
 *  字幕编辑器的视频预览区（docs/语音字幕方案.md 第 7 节「左侧视频预览」），CaptionPreviewView.h
 *
 *  自绘控件（balloonui 没有显示视频帧的控件，按 UI 约定第 2 条只在这里与时间轴两处自绘）：
 *    - 画编辑器交来的当前帧（已按本控件大小缩放、四周补黑）；
 *    - 在视频画面上叠加当前时刻的字幕，绘制代码与生成硬字幕用的是同一份（CaptionRenderer），所见即所得；
 *      字幕图层按「文字 + 尺寸 + 外观」缓存，文字不变时每次重画只做叠加；
 *    - 没有画面时（正在准备预览、预览不可用）显示一行提示文字；
 *    - 单击发 DUIN_CLICK（由 balloonui 基类 DuiControl::OnLButtonUp 在鼠标抬起时发出），编辑器据此切换播放与暂停。
 *      本类不再在鼠标按下时另发一次：2026-10-06 之前按下、抬起各发一次，一次单击切换两次播放，表现为按钮闪一下、视频不动。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      m_preview->SetFrame(frame, w, h, videoRect);
 *      m_preview->SetCaption(speaker, text, style, pip);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../Model/CaptionDraft.h"

namespace caption {

/**
 *  CaptionPreviewView —— 视频预览区。
 */
class CaptionPreviewView : public balloonwjui::DuiControl
{
public:
    CaptionPreviewView();

    /**
     *  交来当前帧（拷贝保存）。
     *  @param bgra      自上而下的 BGRA，width × height 个像素
     *  @param width     帧宽（像素），一般等于本控件宽
     *  @param height    帧高
     *  @param videoRect 视频画面在帧里的矩形（四周是补的黑边），字幕按它定位
     */
    void SetFrame(const std::vector<uint32_t>& bgra, int width, int height, const RECT& videoRect);

    //清掉当前帧，显示提示文字
    void ClearFrame();

    /**
     *  当前要叠加的字幕。文字为空表示这一刻没有字幕。
     *  @param speaker 说话人（按外观的「显示说话人」决定画不画）
     *  @param text    文字
     *  @param style   外观
     *  @param pip     画中画位置（排版时避开）
     */
    void SetCaption(const std::wstring& speaker, const std::wstring& text, const CaptionStyle& style,
                    const CaptionPip& pip);

    //没有画面时显示的提示文字
    void SetMessage(const std::wstring& text);

    /**
     *  录音模式（docs/语音字幕方案.md 14.13，屏幕共享方案第 24 期只录声音）：文件里没有画面时打开。打开后不再等画面，
     *  提示文字写在上方，当前字幕用大字居中显示（说话人按外观的「显示说话人」写在字幕上方）。
     *  @param on 是否是没有画面的录音
     */
    void SetAudioOnly(bool on);

    //是否是录音模式
    bool IsAudioOnly() const;

    //绘制：帧 + 字幕；没有帧时为黑底与提示文字
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;


private:
    //按当前字幕与视频矩形重画字幕图层（文字或尺寸变化时）
    void ensureLayer();
    /**
     *  录音模式下画当前字幕（14.13）：说话人小字一行、字幕大字可换行，合起来在 top 以下的区域竖直居中。
     *  @param hdc 绘制目标
     *  @param rc  本控件的矩形
     *  @param top 字幕区的上沿（提示带的下沿）
     */
    void paintAudioCaption(HDC hdc, const RECT& rc, LONG top);

    std::vector<uint32_t> m_frame;        //当前帧
    int                   m_frameW;       //帧宽
    int                   m_frameH;       //帧高
    RECT                  m_videoRect;    //视频画面在帧里的矩形
    std::vector<uint32_t> m_compose;      //帧与字幕合成后的像素（绘制用）
    std::wstring          m_speaker;      //当前字幕的说话人
    std::wstring          m_text;         //当前字幕的文字；为空表示没有字幕
    CaptionStyle          m_style;        //外观
    CaptionPip            m_pip;          //画中画位置
    std::vector<uint32_t> m_layer;        //字幕图层（与视频矩形同大，预乘透明度）
    RECT                  m_layerBounds;  //图层里实际画到的区域（图层坐标）
    bool                  m_layerValid;   //图层是否与当前字幕、尺寸、外观一致
    std::wstring          m_message;      //没有画面时的提示文字
    bool                  m_audioOnly;    //是否是没有画面的录音（14.13）
};

} // namespace caption
