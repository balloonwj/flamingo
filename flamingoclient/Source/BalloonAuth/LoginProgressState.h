/**
 *  登录窗「登录中」状态机，LoginProgressState.h
 *
 *  把登录窗在「空闲 / 登录中 / 已登录」之间的流转、以及<u>迟到结果的作废判定</u>
 *  抽成纯逻辑，好让它能被单元测试覆盖——窗口那一层只负责按状态画界面。
 *
 *  <u>为什么需要轮次号</u>：登录请求是在后台线程里发的，那个线程阻塞在 socket 收发上
 *  （带超时），用户点「取消」并不能让它立刻停下来，它最终还是会把结果投回 UI 线程。
 *  等结果到达时，界面可能已经回到空闲、甚至用户又发起了第二次登录。于是每次发起
 *  登录都取一个递增的轮次号，线程带着发起时的号回来，与当前号对不上的一律丢弃。
 *  没有这个机制，表现会是「点了取消，界面却在几秒后自己跳出一个登录失败提示」，
 *  或者更糟：第二次登录正在进行时被第一次的失败结果打断。
 *
 *  本类<u>不涉及任何 UI 与线程原语</u>，只在 UI 线程内被调用，故无需加锁。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stdint.h>

//登录结果码 LOGIN_RESULT_CODE 的定义处。LoginErrorText 按它取文案。
#include "../net/IUProtocolData.h"

namespace auth {

//登录流程所处的阶段
enum login_phase
{
    //空闲：等待用户填账号口令并点登录。只有这个阶段允许发起登录
    login_phase_idle = 0,
    //登录中：请求已发出，正在等结果。此阶段输入框与登录按钮应禁用，并显示进度
    login_phase_logining = 1,
    //已登录成功：登录窗即将关闭，此后任何迟到的结果都不再处理
    login_phase_done = 2,
};

//无效轮次号。轮次号从 1 开始递增，0 专门用来表示「没有正在进行的登录」
const uint32_t kInvalidLoginGeneration = 0;

/**
 *  登录窗的状态机。一个登录窗一个实例，只在 UI 线程内使用。
 */
class LoginProgressState
{
public:
    LoginProgressState();

    //当前所处阶段
    login_phase phase() const { return m_phase; }

    //当前这一轮的轮次号；不在登录中时为 kInvalidLoginGeneration
    uint32_t currentGeneration() const { return m_generation; }

    /**
     *  能否发起登录。只有空闲阶段可以——登录中重复点击不该起第二个线程，
     *  已登录成功则窗口马上要关了。
     */
    bool canStartLogin() const { return m_phase == login_phase_idle; }

    /**
     *  发起一次登录：转入登录中，并分配一个新的轮次号。
     *  @return 本次的轮次号，调用方须把它交给登录线程、由结果原样带回；
     *          当前不允许发起时返回 kInvalidLoginGeneration 且状态不变。
     *  副作用：改变阶段与轮次号。
     */
    uint32_t startLogin();

    /**
     *  取消当前这一轮登录：立即回到空闲，使用户可以重新输入并再次登录。
     *
     *  <u>不会去中止后台线程</u>——它阻塞在 socket 上，中止不了，只能等它自己回来。
     *  取消的实质是把本轮的轮次号作废，那个迟到的结果到达时会被 shouldAcceptResult
     *  判为过期而丢弃。
     *
     *  @return 确实取消了返回 true；当前不在登录中（无事可取消）返回 false。
     *  副作用：改变阶段与轮次号。
     */
    bool cancelLogin();

    /**
     *  复位到空闲，使窗口重新摆到用户面前时一定能再次发起登录。
     *
     *  <u>为什么需要它</u>：自动登录那条路不经过登录窗界面，它直接调 DoLogin，
     *  状态机因而进入「登录中」；而自动登录失败时结果不归登录窗处理（那时窗口
     *  还没建出来），没有人把状态机推回空闲。随后登录窗被摆出来，用户点「登录」
     *  时阶段仍是「登录中」，那一下会被当成「取消登录」，表现为<u>第一次点登录
     *  毫无反应</u>，要再点一次才真的开始登。
     *
     *  轮次号计数器<u>不复位</u>：它只增不减，才能保证复位之前那一轮的迟到结果
     *  与复位之后的新一轮永远对不上号。
     *
     *  副作用：改变阶段与当前轮次号。
     */
    void reset();

    /**
     *  判断一个刚到达的登录结果是否应当被采纳。
     *  @param generation 结果携带的轮次号，即发起那次登录时拿到的号
     *  @return 仍在登录中、且轮次号与当前一致时返回 true；
     *          已取消、已有更新的一轮、或已登录成功，一律返回 false。
     */
    bool shouldAcceptResult(uint32_t generation) const;

    /**
     *  采纳一个成功结果：转入已登录。
     *  @param generation 结果携带的轮次号
     *  @return 该结果被采纳返回 true；应当丢弃时返回 false 且状态不变。
     */
    bool acceptSuccess(uint32_t generation);

    /**
     *  采纳一个失败结果：回到空闲，使用户能改完账号口令重试。
     *  @param generation 结果携带的轮次号
     *  @return 该结果被采纳返回 true；应当丢弃时返回 false 且状态不变。
     */
    bool acceptFailure(uint32_t generation);

private:
    login_phase m_phase;        //当前阶段
    uint32_t    m_generation;   //当前这一轮的轮次号；空闲时为 kInvalidLoginGeneration
    uint32_t    m_nextGeneration;   //下一次发起登录要用的轮次号，只增不减
};

/**
 *  取登录失败时就地显示在登录窗上的提示文案。
 *
 *  这些文案原先是 MessageBox 的内容，改为在窗口内显示之后仍由此处统一提供，
 *  免得散落在各个失败分支里。
 *
 *  @param nLoginResultCode 登录结果码，取值见 enum LOGIN_RESULT_CODE
 *  @return 指向静态字符串的指针，<u>恒不为空</u>：未知错误码返回一句兜底文案。
 *          调用方不得释放，可直接用于界面显示。
 *  副作用：无。
 */
const wchar_t* LoginErrorText(long nLoginResultCode);

} // namespace auth
