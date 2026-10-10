/**
 *  NotifyModeModel.h
 *
 *  会话消息提醒模式的<u>纯数据模型</u>（下称「模式表」）：记住本人每个会话（单聊或群）当前是
 *  哪一种提醒模式 —— 允许提醒 / 接收不提醒 / 收进消息盒子 / 屏蔽 / 特别关注，见
 *  docs/消息提醒模式与消息盒子方案.md §3。
 *
 *  之所以不把模式挂在会话行（net::ConversationItem）上：会话列表只含有消息的会话，而模式可以
 *  设给还没聊过的群与同事；挂在会话行上，这类设置重新登录后就读不回来，新消息到来时
 *  BumpConversation 新建的行也会把它丢掉。模式表独立存放，以 cmd_sync_conversations 应答的
 *  notifymodes 数组为准初始化，之后随本机修改与 1514 / 1525 推送更新。
 *
 *  只存非默认（不为 0）的会话；查不到的一律按「允许提醒」处理。
 *
 *  本模块不碰窗口、不碰网络，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。
 *  在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  用法：
 *      balloonmain::NotifyModeModel model;
 *      model.Reset(result.m_notifyModes);                      // 会话列表到达
 *      int mode = model.GetMode(conversation_group, groupId);  // 收到消息时判定
 *      if (model.CanSetStarred(ctype, cid)) { ... }            // 设特别关注前检查上限
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <stdint.h>
#include <map>
#include <utility>
#include <vector>

#include "../../net/ConversationsProtocolClient.h"   // net::NotifyModeEntry

namespace balloonmain {

/**
 *  特别关注的会话数上限（单聊与群聊合计）。必须与服务端 MessageProtocol.h 的
 *  NOTIFY_MODE_STARRED_MAX 一致：本端在设为特别关注之前先按它检查，满了直接提示、不发请求；
 *  服务端仍会再校验一次（超出回 error_code_starred_limit）。
 */
const int kNotifyModeStarredMax = 50;

/**
 *  NotifyModeModel —— 会话消息提醒模式的内存模型。
 *
 *  键为 (ctype, cid)：ctype 取 1 单聊、2 群聊（见 Protocol.h 的 conversation_type），cid 为
 *  对端 userid 或群 id。值为 Protocol.h 的 enum conversation_notify_mode。
 */
class NotifyModeModel
{
public:
    NotifyModeModel();

    /**
     *  用服务端下发的列表整体替换当前状态（会话列表到达时调用）。
     *    entries： cmd_sync_conversations 应答的 notifymodes（本人全部非默认模式的会话）。
     *              非法项（ctype 不是 1/2、cid <= 0、mode 越界）与模式为 0 的项被忽略。
     *  本方法会先清空原有状态。
     */
    void Reset(const std::vector<net::NotifyModeEntry>& entries);

    /**
     *  取某个会话当前的提醒模式。
     *    ctype： 1 单聊、2 群聊。
     *    cid：   单聊为对端 userid，群聊为群 id。
     *  返回：enum conversation_notify_mode 的取值；没有记录的会话返回 notify_mode_allow。
     */
    int GetMode(int ctype, int64_t cid) const;

    /**
     *  修改某个会话的提醒模式（本机乐观更新、1514 推送都走这里）。
     *    ctype / cid： 同 GetMode。
     *    mode：        目标模式；设为 notify_mode_allow 即删除该项。
     *  返回：状态确实发生变化返回 true；与原值相同、或参数非法（ctype 不是 1/2、cid <= 0、
     *        mode 越界）返回 false 且不做任何修改。
     */
    bool SetMode(int ctype, int64_t cid, int mode);

    /**
     *  批量应用一组模式（cmd_notify_modes_changed_notify 推送到达时调用），逐条同 SetMode。
     *    entries： 推送里改成功的条目。
     *  返回：至少有一条确实发生变化返回 true。
     */
    bool ApplyEntries(const std::vector<net::NotifyModeEntry>& entries);

    /**
     *  数处于某种模式的会话个数（单聊与群聊合计）。
     *    mode： 提醒模式；传 notify_mode_allow 恒返回 0（默认模式不存）。
     */
    int CountMode(int mode) const;

    /**
     *  判断能否把某个会话设为特别关注：它本来就是特别关注（重复设置不占新名额）、或者当前
     *  特别关注的会话数还不到 kNotifyModeStarredMax，返回 true；否则返回 false。
     *    ctype / cid： 同 GetMode。
     */
    bool CanSetStarred(int ctype, int64_t cid) const;

    /**
     *  把当前全部非默认模式导出成列表（按 ctype、cid 升序），供批量管理窗口等使用。
     *    out： [出参] 调用前会被清空。
     */
    void ExportEntries(std::vector<net::NotifyModeEntry>& out) const;

    /**
     *  清空全部状态（登出 / 切换账号时调用）。
     */
    void Clear();

private:
    //判断一组参数能否进模式表：ctype 为单聊或群聊、cid 为正、mode 在取值范围内
    static bool IsValid(int ctype, int64_t cid, int mode);

    typedef std::pair<int, int64_t> Key;   // (ctype, cid)

    std::map<Key, int> m_modes;   // 非默认模式的会话 → 模式；生命周期与主面板相同，登出时 Clear
};

} // namespace balloonmain
