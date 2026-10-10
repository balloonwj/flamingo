/**
 *  SubtitleView.h
 *
 *  字幕会话（CSubtitleSession）与字幕窗口之间的两个接口：
 *    · ISubtitleView：会话经它建出、更新、销毁窗口。正式实现是 CSubtitleWindow；单元测试换成
 *      不建窗口的假实现，只记录调用（见 Tests/SubtitleTests.cpp 的 SUBC 组）；
 *    · ISubtitleViewHost：窗口经它把用户在字幕上的操作（工具按钮、拖动、调宽、设置菜单）报告给会话。
 *  两边都只在界面线程上使用。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "../../Session/SubtitlePrefs.h"
#include "SubtitleModel.h"

namespace subtitle {

/**
 *  ISubtitleViewHost —— 接收字幕窗口上的用户操作。由 CSubtitleSession 实现。
 */
class ISubtitleViewHost
{
public:
    virtual ~ISubtitleViewHost() {}

    //用户点了「回到聊天窗」
    virtual void OnSubtitleBack() = 0;

    /**
     *  用户点了「退出字幕模式」。实现方会在本调用里销毁窗口（并 delete 视图对象），
     *  窗口调用本方法之后不得再访问自己的任何成员。
     */
    virtual void OnSubtitleClose() = 0;

    /**
     *  用户锁定或解锁了字幕。
     *    locked：新的锁定状态。
     */
    virtual void OnSubtitleLockChanged(bool locked) = 0;

    /**
     *  用户拖动或调宽结束，窗口已落在新位置。
     *    rc：窗口矩形，屏幕坐标、物理像素。
     */
    virtual void OnSubtitleMoved(const RECT& rc) = 0;

    /**
     *  用户在设置菜单里改了外观（字号、行数、共享时隐藏）。外观是所有会话共用的，实现方负责保存
     *  并让所有正在显示的字幕都换成新外观（包括发出本通知的这一条，经 ISubtitleView::SetAppearance）。
     *    ap：新的外观。
     */
    virtual void OnSubtitleAppearanceChanged(const SubtitleAppearance& ap) = 0;
};

//建立字幕窗口所需的输入
struct SubtitleViewInit
{
    ISubtitleViewHost* m_host;        //用户操作的接收方；不拥有，须比窗口活得久
    std::wstring       m_title;       //会话名（单聊为对方名字，群聊为群名），显示在工具条左侧
    SubtitleAppearance m_appearance;  //外观
    bool               m_hasSaved;    //这个会话是否保存过位置
    SubtitlePlacement  m_saved;       //保存的位置与锁定状态；m_hasSaved 为 false 时无意义
    std::vector<RECT>  m_occupied;    //其它会话的字幕此刻所在的矩形（屏幕坐标），新窗口尽量避开
    SubtitleLine       m_line;        //一开始显示的内容（通常是「等待新消息…」）

    SubtitleViewInit()
        : m_host(NULL)
        , m_hasSaved(false)
    {
    }
};

/**
 *  ISubtitleView —— 一条字幕的显示。由 CSubtitleWindow 实现。
 */
class ISubtitleView
{
public:
    virtual ~ISubtitleView() {}

    /**
     *  建出窗口并显示（不抢焦点）。
     *    init：输入。
     *  返回：成功返回 true；失败时不留下任何窗口。
     */
    virtual bool Create(const SubtitleViewInit& init) = 0;

    //销毁窗口；可重复调用
    virtual void Destroy() = 0;

    /**
     *  换成新的内容并重画。
     *    line：内容。
     */
    virtual void SetContent(const SubtitleLine& line) = 0;

    /**
     *  更换会话名（如群改名、联系人改备注之后）。
     *    title：新的会话名。
     */
    virtual void SetTitle(const std::wstring& title) = 0;

    /**
     *  换成新的外观：按新的字号与行数重算高度（保持底边不动，字幕向上长高）并重画，
     *  按「共享时隐藏」重新设置是否排除在屏幕采集之外。
     *    ap：新的外观。
     */
    virtual void SetAppearance(const SubtitleAppearance& ap) = 0;

    //窗口当前的矩形，屏幕坐标、物理像素；窗口不存在时为空矩形
    virtual RECT GetRect() const = 0;

    //当前是否锁定
    virtual bool IsLocked() const = 0;

    /**
     *  临时隐藏或恢复显示窗口（客户端锁屏期间隐藏，docs/锁屏方案.md 第 4.9 节），内容、位置与锁定状态不变，不抢焦点。
     *    hidden：true 隐藏，false 恢复显示并重新置于最上层。
     */
    virtual void SetHidden(bool hidden) = 0;
};

/**
 *  建立视图对象的工厂函数。
 *    ctx：工厂的上下文，原样传回。
 *  返回：新建的视图对象（还没有 Create），所有权归调用方；失败返回 NULL。
 */
typedef ISubtitleView* (*CreateSubtitleViewFn)(void* ctx);

} // namespace subtitle
