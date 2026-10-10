/**
 *  FlamingoMedia.dll 与 FlamingoCaption.dll 的衔接（docs/语音字幕方案.md 第 4 节）：字幕模块由 exe 按需加载，
 *  本 DLL 经 IMediaHost::IsCaptionAvailable / GetCaptionService 取用。本对象把这两项能力与「可用状态变化」的通知
 *  集中在一处：录屏、屏幕共享等控制器向它询问能否使用、取字幕模块，并登记为观察者，在字幕变为不可用时停止识别、
 *  隐藏字幕入口（第 2 节 D5）。
 *
 *  线程模型：只在 UI 线程上使用。
 *
 *  典型用法：
 *      CMediaCaptionBridge::Instance().Attach(host);                 //CMediaModule::Init
 *      if (CMediaCaptionBridge::Instance().IsAvailable()) { ... }    //决定是否显示「生成字幕」
 *      caption::ICaptionModule* m = CMediaCaptionBridge::Instance().Acquire();
 *      CMediaCaptionBridge::Instance().AddObserver(this);            //控制器构造时登记，析构时 RemoveObserver
 *      CMediaCaptionBridge::Instance().SetAvailable(false);          //CMediaModule::OnCaptionAvailabilityChanged
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <vector>

#include "../Api/FlamingoMediaApi.h"
#include "../../FlamingoCaption/CaptionApi.h"

namespace flamingomedia {

/**
 *  字幕可用状态的观察者：录屏、屏幕共享控制器实现它。
 */
class ICaptionAvailabilityObserver
{
public:
    /**
     *  字幕可用状态变了（UI 线程）。
     *  @param available 变化后是否可用
     */
    virtual void OnCaptionAvailabilityChanged(bool available) = 0;

protected:
    ~ICaptionAvailabilityObserver()
    {
    }
};

/**
 *  CMediaCaptionBridge —— 本 DLL 内唯一的字幕衔接对象。
 */
class CMediaCaptionBridge
{
public:
    /**
     *  取 DLL 内唯一的对象。
     *  @return 对象（静态存储）
     */
    static CMediaCaptionBridge& Instance();

    /**
     *  登记宿主（模块 Init 时调用）。
     *  @param host 宿主接口（借用，不拥有）
     */
    void Attach(IMediaHost* host);

    /**
     *  解除宿主并清空观察者（模块 Shutdown 时调用）。此后 IsAvailable 返回 false、Acquire 返回 NULL。
     */
    void Detach();

    /**
     *  字幕当前能否使用（不触发加载），转问宿主。
     *  @return 已登记宿主且宿主报告可用时返回 true
     */
    bool IsAvailable() const;

    /**
     *  取字幕模块，第一次取时由 exe 加载字幕 DLL。
     *  @return 字幕模块（归 exe 所有）；不可用或加载失败时返回 NULL
     */
    caption::ICaptionModule* Acquire();

    /**
     *  exe 通知可用状态变了（CMediaModule::OnCaptionAvailabilityChanged 转来），逐个通知观察者。
     *  @param available 变化后是否可用
     */
    void SetAvailable(bool available);

    /**
     *  登记观察者；重复登记同一个只算一次。
     *  @param observer 观察者（借用，不拥有），须在销毁前 RemoveObserver
     */
    void AddObserver(ICaptionAvailabilityObserver* observer);

    /**
     *  注销观察者；没有登记过时什么都不做。
     *  @param observer 观察者
     */
    void RemoveObserver(ICaptionAvailabilityObserver* observer);

private:
    CMediaCaptionBridge();
    CMediaCaptionBridge(const CMediaCaptionBridge&) = delete;
    CMediaCaptionBridge& operator=(const CMediaCaptionBridge&) = delete;

private:
    IMediaHost*                                  m_host;        //宿主接口（借用）；Attach 之前与 Detach 之后为 NULL
    std::vector<ICaptionAvailabilityObserver*>   m_observers;   //登记的观察者（借用）
};

} // namespace flamingomedia
