/**
 *  远程桌面被控端的控制把关，RdControlGate.h
 *
 *  2026-10-12 起远程桌面接通后默认只能查看（docs/远程桌面控制权限与观看工具条方案.md 第 4.2 节）。
 *  被控端收到的每一条键鼠输入都先经本类判定：
 *    - 未授权（默认）时一律丢弃；被控者同意申请或直接允许、服务端确认之后才打开；
 *    - 打开期间，落在本进程（Flamingo 自己）窗口上的输入丢弃，以免控制者操作被控者的 Flamingo；
 *      鼠标松开、按键抬起不受这一限制，否则在别处按下、拖到 Flamingo 窗口上松开的按钮会一直按着；
 *    - 记下已注入按下、尚未注入松开的键与鼠标按钮，关闭时逐个给出松开命令，由调用方补发注入，
 *      避免被控者的电脑上留下按住的键（做法同屏幕共享第 18 期的 CShareController::releaseInjectedInput）。
 *
 *  本类只做判定，不调用任何注入 API。Filter 在中转或 P2P 的接收线程上调用，SetAllowed / Reset 在 UI 线程上调用，
 *  内部用互斥量保护全部状态。单元测试见 Source/Tests/RdControlGateTests.cpp（标签 [rdctl]）。
 *
 *  典型用法（被控端推流器）：
 *      RdInputCommand cmd;
 *      if (RdInputInjector::parse(data, len, cmd) && m_gate.Filter(cmd))
 *          RdInputInjector::inject(data, len);
 *      //收回控制时
 *      std::vector<RdInputCommand> releases;
 *      m_gate.SetAllowed(false, cursorPos, releases);
 *      逐条把 releases 编码后注入;
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <mutex>
#include <vector>
#include "RdInputDef.h"

namespace remotedesktop {

//远程桌面被控端的控制把关：决定一条键鼠输入能否注入，并记录按住的键与按钮以便收回控制时补发松开
class RdControlGate
{
public:
    /**
     *  「这条输入是否落在本进程的窗口上」的判定函数。
     *  @param cmd 一条已解析的输入；鼠标命令按坐标处的窗口判定，键盘命令按前台窗口判定
     *  @return 落在本进程的窗口上返回 true
     *  在中转或 P2P 的接收线程上调用，实现须可在任意线程调用。
     */
    typedef bool (*OwnWindowProbe)(const RdInputCommand& cmd);

    //构造后为关闭状态，没有判定函数（视为都不落在本进程窗口上）
    RdControlGate();

    /**
     *  设置「落在本进程窗口上」的判定函数。
     *  @param probe 判定函数；NULL 表示不做这项判定。生产代码传 RdControlGate::IsOnOwnProcessWindow
     */
    void SetOwnWindowProbe(OwnWindowProbe probe);

    /**
     *  打开或关闭控制。
     *  @param allowed  true 打开（此后的输入按规则注入）；false 关闭（此后的输入一律丢弃）
     *  @param cursor   光标当前位置（被控端屏幕坐标），关闭时补发的鼠标松开命令落在这里；打开时不用
     *  @param releases [出参] 先清空；关闭时填入要补发的松开命令：先是按键抬起（按按下的逆序），再是鼠标按钮松开。
     *                  打开、或本来就是关闭时为空
     *  副作用：关闭时清空按下记录。
     */
    void SetAllowed(bool allowed, POINT cursor, std::vector<RdInputCommand>& releases);

    //当前是否允许注入
    bool IsAllowed() const;

    /**
     *  判定一条输入能否注入，并更新按下记录。
     *  @param cmd 一条已解析的输入（RdInputInjector::parse 的结果）
     *  @return 应当注入返回 true；关闭状态、落在本进程窗口上的按下或移动、未知类型返回 false
     */
    bool Filter(const RdInputCommand& cmd);

    //复位为关闭、清空按下记录，不给出松开命令。新会话开始时调用
    void Reset();

    /**
     *  生产用的「落在本进程窗口上」判定：鼠标命令取坐标处的窗口，键盘命令取前台窗口，
     *  看它所属进程是不是本进程。可在任意线程调用。
     */
    static bool IsOnOwnProcessWindow(const RdInputCommand& cmd);

private:
    //判定 message 是不是鼠标松开或按键抬起（这类输入不受「本进程窗口」限制）
    static bool IsReleaseMessage(const RdInputCommand& cmd);

    //按下记录的更新：按下时加入、松开时移除（调用方已持锁）
    void TrackLocked(const RdInputCommand& cmd);

    mutable std::mutex     m_mutex;          //保护以下全部成员：Filter 在接收线程上、其余在 UI 线程上调用
    bool                   m_allowed;        //是否允许注入；构造与 Reset 后为 false
    OwnWindowProbe         m_probe;          //「落在本进程窗口上」的判定函数；NULL 表示不判定
    std::vector<uint32_t>  m_heldKeys;       //已注入按下、尚未注入抬起的虚拟键码，按按下先后排列
    std::vector<uint32_t>  m_heldButtons;    //已注入按下、尚未注入松开的鼠标按钮，存对应的松开消息（WM_LBUTTONUP 等）
};

} // namespace remotedesktop
