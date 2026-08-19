/**
 *  未知发送方触发通讯录刷新的节流策略，UnknownSenderRefreshPolicy.h
 *
 *  用途：收到一条消息、而发送方不在本端通讯录缓存里时，本端需要重新拉一次通讯录
 *  （cmd_get_contacts），否则那条消息只能显示成"用户<userid>"这种兜底名字。但是
 *  "不认识就拉"不能无条件执行 —— 一串陌生人消息会把通讯录请求打成风暴，而且对方
 *  若是已被删除的账号，拉多少次都还是不认识。本类就是这道闸门。
 *
 *  规则有三条：
 *    1. 同一个未知 userid 只触发一次刷新，此后再收到他的消息都不再拉；
 *    2. 两次刷新之间有冷却时间，冷却期内换一个陌生人也不拉 —— 上一次刷新本来
 *       就会把这期间进组织架构的人一并带回来；
 *    3. 传入的时间戳一旦倒退（系统时钟回拨、GetTickCount 回绕），冷却立即作废，
 *       不让策略被卡在"永远还在冷却中"的状态里。
 *
 *  本类不碰窗口、不发网络请求、不读时钟 —— 当前时间由调用方传进来，因此可以
 *  完全用单元测试覆盖（见 Tests/UnknownSenderRefreshPolicyTests.cpp）。
 *
 *  典型用法（主面板收到一条新消息时）：
 *      if (FindCachedContact(fromUserId) == NULL
 *          && m_unknownSenderPolicy.ShouldRefreshContacts(fromUserId, ::GetTickCount64()))
 *      {
 *          CIUSocket::GetInstance().RequestContacts();
 *      }
 *
 *  balloonwj@qq.com   2026-08-11
 */
#ifndef __UNKNOWN_SENDER_REFRESH_POLICY_H__
#define __UNKNOWN_SENDER_REFRESH_POLICY_H__

#include <stdint.h>
#include <set>

namespace balloonmain {

//两次通讯录刷新之间的最小间隔，单位毫秒。
//取 10 秒：一次 cmd_get_contacts 的往返通常在百毫秒级，10 秒足够让上一次刷新的
//结果到货并填进缓存，后来的陌生人多半已经在里面了；同时又短到用户察觉不出延迟 ——
//真有连续几个新同事同时上来说话，最坏也就是第二个人的名字晚 10 秒补上。
const int64_t kContactsRefreshCooldownMs = 10000;

/**
 *  未知发送方触发通讯录刷新的节流器。
 *
 *  一个主面板持有一个实例，与通讯录缓存同生命周期。调用方负责先查缓存、确认
 *  发送方确实不认识，再来问本类要不要刷新。
 */
class UnknownSenderRefreshPolicy
{
public:
    UnknownSenderRefreshPolicy();

    /**
     *  判断"收到一条来自未知发送方的消息"是否应当触发一次通讯录刷新。
     *  调用前调用方须自行确认 userid 确实不在通讯录缓存里。
     *  返回 true 时本类已把本次刷新记账（记下时刻与该 userid），调用方**必须**
     *  真的去发 cmd_get_contacts，否则记账与实际行为就对不上了。
     *  @param userid 消息发送方的用户ID；<= 0 一律返回 false（无法定位的发送方，
     *                刷新通讯录也解决不了）。
     *  @param nowMs  当前时刻，单位毫秒，取自任意单调时钟（如 ::GetTickCount64()）。
     *                只用于算冷却间隔，绝对值本身无意义。
     *  @return true 表示应当立即发起一次通讯录刷新。
     */
    bool ShouldRefreshContacts(int64_t userid, int64_t nowMs);

    /**
     *  清空全部记账，让下一个未知发送方立即又能触发刷新。
     *  调用点只有"每次登录成功"这一处（会话控制器的登录成功分支）：掉线重连期间组织
     *  架构可能变过、变更推送也可能错过，上一轮攒下的"这些人问过了"不该继续压着
     *  本轮的补拉；换账号登录同理。
     *  注意通讯录刷新到货**不**该调用本方法：刷完仍然不认识的 userid（例如已离职
     *  被删除的账号）应当继续保持"已问过"，否则他每发一条消息都会再拉一次通讯录。
     */
    void Reset();

private:
    //上一次判定"应当刷新"的时刻，单位毫秒，取自调用方传入的时钟；0 表示还没刷过。
    int64_t m_lastRefreshMs;

    //已经因其触发过一次刷新的发送方 userid 集合。只增不减（直到 Reset），
    //保证每个陌生人最多只让本端拉一次通讯录。
    std::set<int64_t> m_askedUserIds;
};

} // namespace balloonmain

#endif // __UNKNOWN_SENDER_REFRESH_POLICY_H__
