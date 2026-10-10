/**
 *  独立录屏的鼠标效果（屏幕共享第 16 期 16d），RecordMouseEffects.h
 *
 *  录屏条上与共享控制条同样的「鼠标光圈」「点击效果」「鼠标轨迹」开关，两边共用一份设置（exe 按账号保存）。
 *  共享中的录制取的是推流线程已经画好效果的画面，不经过这里；独立录屏（整屏、单个窗口、框选区域）由录制会话在写每一帧
 *  之前调用 RecordMouseDecorator::Decorate，把效果就地画在原始分辨率的画面上，再交给合成器缩放进画布。
 *
 *  光标的屏幕坐标按「录制画面在屏幕上的矩形」换算成画面坐标：整屏为显示器矩形，框选区域为显示器矩形加上相对显示器
 *  左上角的裁剪矩形（RecordSourceScreenRect），单个窗口为采集器报告的窗口矩形。这个矩形由 IRecordSourceGeometry 提供
 *  （RecordCaptureSource 实现），测试里换成固定矩形。
 *
 *  线程：SetSettings / Settings 任意线程；Decorate 只在录制线程上调用。
 *
 *  典型用法（录屏控制器）：
 *      m_mouseDecorator.SetGeometry(&m_capture);
 *      m_session.SetDecorator(&m_mouseDecorator);
 *      m_session.Start(opt, &m_capture, pip, this);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <windows.h>
#include <mutex>

#include "RecordSession.h"                     // IRecordFrameDecorator
#include "../Share/ShareCursorSampler.h"
#include "../Share/ShareMouseEffects.h"

namespace screenrecord {

/**
 *  录制画面在屏幕上的矩形：显示器矩形加上相对显示器左上角的裁剪矩形。
 *  @param monitorRect 显示器在虚拟桌面上的矩形（物理像素）
 *  @param crop        相对显示器左上角的裁剪矩形；为空矩形（right <= left）时不裁剪
 *  @return 录制画面对应的屏幕矩形
 */
RECT RecordSourceScreenRect(const RECT& monitorRect, const RECT& crop);

//录制画面来源的几何信息：鼠标效果据此把光标换算到画面坐标
class IRecordSourceGeometry
{
public:
    virtual ~IRecordSourceGeometry()
    {
    }

    //录制画面对应的屏幕矩形（物理像素）；任意线程可调用
    virtual RECT SourceScreenRect() const = 0;
};

//独立录屏的鼠标效果：录制线程写每一帧之前把效果就地画在画面上
class RecordMouseDecorator : public IRecordFrameDecorator
{
public:
    RecordMouseDecorator();
    virtual ~RecordMouseDecorator();

    /**
     *  登记录制画面的几何信息（开始录制之前调用）。
     *  @param geometry 不拥有，录制结束之前须一直有效；为 NULL 时不画
     */
    void SetGeometry(const IRecordSourceGeometry* geometry);

    /**
     *  换设置（任意线程）：录制线程下一帧生效。
     *  @param settings 新设置
     */
    void SetSettings(const screenshare::ShareMouseEffectSettings& settings);

    //当前设置（任意线程）
    screenshare::ShareMouseEffectSettings Settings() const;

    //清空轨迹、点击圈与按键状态（每次开始录制之前调用）；设置保留
    void Reset();

    /**
     *  见 IRecordFrameDecorator::Decorate：取光标、换算到画面坐标、更新效果状态并画上去。
     *  @param bgra   画面第一行像素，自上而下，行跨度 = width × 4；就地修改
     *  @param width  画面宽，单位像素
     *  @param height 画面高，单位像素
     *  @param nowMs  本机时刻（毫秒，单调递增）
     */
    virtual void Decorate(uint8_t* bgra, int width, int height, int64_t nowMs) override;

protected:
    /**
     *  取光标状态（录制线程）。默认用 ShareCursorSampler；测试子类覆写成固定位置。
     *  @param pt    [出参] 光标的屏幕坐标（物理像素）
     *  @param left  [出参] 逻辑左键是否按下
     *  @param right [出参] 逻辑右键是否按下
     *  @param nowMs 当前时刻（毫秒）
     */
    virtual void sampleCursor(POINT& pt, bool& left, bool& right, int64_t nowMs);

private:
    mutable std::mutex                    m_settingsMutex;    //保护以下两项
    screenshare::ShareMouseEffectSettings m_settings;         //最近一次 SetSettings 设的值
    bool                                  m_settingsChanged;  //设置变了、录制线程还没取走
    const IRecordSourceGeometry*          m_geometry;         //录制画面的几何信息（不拥有）
    screenshare::ShareMouseEffects        m_effects;          //效果状态（录制线程）
    screenshare::ShareCursorSampler       m_sampler;          //取光标（录制线程）
};

} // namespace screenrecord
