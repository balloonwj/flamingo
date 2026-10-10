/**
 *  ContactWindowSet.h
 *
 *  联系人资料窗的多窗口管理：一人一窗，多个人的资料窗可以同时开着（2026-10-07）。主面板按值持有
 *  一个 CContactWindowSet，所有「打开某人的资料」「刷新某人的资料窗」「把连接态推给全部资料窗」都经
 *  它按 userid 找到那一个窗口，或者遍历当前开着的全部窗口。
 *
 *  规则：
 *    - 再次打开同一个人：激活已有窗口（还原、前置），不重建界面，正在编辑的内容保留；「修改备注」入口
 *      激活已有窗口时只让备注名进入编辑态；
 *    - 打开另一个人：新建一个窗口，左上角相对最近打开、仍开着的那个资料窗错开 kContactWindowCascadeStep
 *      像素，超出工作区时回到起点（工作区居中的位置），并保证整个窗口落在工作区内
 *      （ComputeContactWindowCascade）；
 *    - 窗口关闭（DestroyWindow）后 C++ 对象暂时留在表里，下一次查找、打开或遍历时惰性回收（与主面板
 *      m_chatWindows 的做法相同）；主面板登出与析构时 DestroyAll 统一销毁并释放。
 *
 *  典型用法（主面板）：
 *      CBalloonContactWindow* w = m_contactWindows.Open(m_hWnd, userid, model, false);
 *      if (w != NULL)
 *          w->ApplyConnectionState(m_connUiState);
 *      CBalloonContactWindow* self = m_contactWindows.Find(SelfUserId());
 *
 *  本类不是线程安全的，只在 UI 线程使用。单元测试见 Tests/ContactWindowSetTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#pragma once

#include <map>
#include <vector>

#include "BalloonContactWindow.h"

class CFlamingoClient;

namespace ballooncontact {

// 新开的资料窗相对上一个资料窗错开的距离（逻辑像素，横向与纵向相同）。取 24：标题栏 44 像素高，
// 错开 24 像素后上一个窗口的标题文字仍露在外面，看得出下面还压着一个窗口
const int kContactWindowCascadeStep = 24;

/**
 *  算新资料窗的左上角（纯函数，屏幕坐标）。
 *
 *  规则：起点是窗口在工作区里居中时的左上角；没有上一个窗口时用起点；有上一个窗口时取它的左上角
 *  向右下各错开 step 像素，这样摆会超出工作区的右边或下边时回到起点。最后把结果夹进工作区：
 *  窗口比工作区还大时左上角对齐工作区的左上角。
 *
 *    workArea：       工作区矩形（屏幕坐标），一般取 MONITORINFO::rcWork；宽或高 <= 0 时原样返回
 *                     起点（此时为工作区左上角）。
 *    windowSize：     新窗口的整窗尺寸（像素）。
 *    hasPrevious：    是否有上一个仍开着的资料窗。
 *    previousTopLeft：上一个资料窗的左上角（屏幕坐标），hasPrevious 为 false 时不用。
 *    step：           错开距离（像素），一般传 kContactWindowCascadeStep；<= 0 时按 0 处理。
 *  返回：新窗口的左上角（屏幕坐标）。无副作用。
 */
POINT ComputeContactWindowCascade(const RECT& workArea, SIZE windowSize, bool hasPrevious,
                                  POINT previousTopLeft, int step);

/**
 *  CContactWindowSet —— 主面板持有的联系人资料窗集合（按 userid 一人一窗）。说明见文件头。
 */
class CContactWindowSet
{
public:
    /**
     *  构造。
     *    pFMGClient：所属客户端对象，新建资料窗时转交给窗口，本类只引用、不持有；可为 NULL（单元测试）。
     */
    explicit CContactWindowSet(CFlamingoClient* pFMGClient);

    // 析构：等同于 DestroyAll，销毁仍开着的窗口并释放全部对象。
    ~CContactWindowSet();

    /**
     *  打开某人的资料窗：已开着时激活它（还原、前置，不重建界面；beginRemarkEdit 为 true 时只让备注名
     *  进入编辑态），否则新建一个窗口、按 ComputeContactWindowCascade 摆放后显示。
     *    hostPanel：      主面板 HWND，窗口里的动作经它回投；可为 NULL（单元测试，此时窗口按调试入口处理）。
     *    peerUserId：     被查看的人的 userid；0 时什么都不做、返回 NULL。
     *    model：          已填好数据的资料模型；激活已有窗口时不使用（不冲掉正在编辑的内容）。
     *    beginRemarkEdit：true 表示打开或激活后让「备注名」进入编辑态（右键菜单「修改备注」）。
     *  返回：该人的资料窗（归本类所有，调用方只借用）；建窗失败返回 NULL。
     *  副作用：先回收已关闭的窗口对象。
     */
    CBalloonContactWindow* Open(HWND hostPanel, UINT peerUserId, const ContactModel& model, bool beginRemarkEdit);

    /**
     *  按 userid 找开着的资料窗。
     *    peerUserId：被查看的人的 userid；0 返回 NULL。
     *  返回：开着的窗口（借用）；没开或已关闭返回 NULL。副作用：先回收已关闭的窗口对象。
     */
    CBalloonContactWindow* Find(UINT peerUserId);

    /**
     *  取当前开着的全部资料窗，顺序为打开的先后（最近打开或激活的在最后）。
     *    outWindows：[出参] 窗口列表（借用），函数内先清空。
     *  副作用：先回收已关闭的窗口对象。
     */
    void GetOpenWindows(std::vector<CBalloonContactWindow*>& outWindows);

    /**
     *  当前开着的资料窗个数。副作用：先回收已关闭的窗口对象。
     *  返回：个数。
     */
    size_t OpenCount();

    /**
     *  回收已关闭（HWND 已销毁）的窗口对象。
     *  返回：本次回收的个数。
     */
    size_t ReapClosed();

    /**
     *  销毁仍开着的全部资料窗并释放全部对象（登出、主面板析构时调用）。之后集合为空，可以继续使用。
     */
    void DestroyAll();

private:
    // 最近打开或激活、仍开着的那个资料窗；一个都没有时返回 NULL。调用前须已回收已关闭的对象。
    CBalloonContactWindow* FindLastOpened() const;

    // 把 userid 移到打开顺序的末尾（最近打开或激活）。
    void TouchOrder(UINT peerUserId);

    // 从打开顺序里去掉 userid。
    void RemoveFromOrder(UINT peerUserId);

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端对象，新建窗口时转交；只引用不持有
    std::map<UINT, CBalloonContactWindow*> m_windows;   // userid → 资料窗（堆分配，本类持有）；
                                                         // 已关闭的对象在下次查找或打开时回收
    std::vector<UINT> m_openOrder;    // 打开或激活的先后顺序（userid），最近的在末尾；新窗口据此错开摆放
};

} // namespace ballooncontact
