/**
 *  屏幕共享结束提示的名单与文字，ShareEndedSummary.h
 *
 *  2026-10-05 用户要求：共享结束时不再弹 MessageBox，改为右下角弹窗，显示时长、分享人与参与者。本文件是其中的纯逻辑部分：
 *    - ShareParticipantLog：一场共享从开始主持或加入起，把房间成员状态推送（1725）里出现过的人按第一次出现的先后记下，
 *      共享过屏幕的人（发起人，以及第 22 期起经同意共享自己屏幕的人）记为分享人；中途离开的人也保留；
 *    - FormatShareEndedTexts：把时长与两份名单写成弹窗上的几行文字，参与者太多时只写前几个再加「等 N 人」。
 *  界面见 ShareEndedToast.h，接入见 ShareController.cpp 的 onStateNotify / onEndedNotify。单元测试见
 *  Source/Tests/ShareEndedTests.cpp。
 *
 *  典型用法：
 *      log.Reset(roomId, inCallShare);         // 开始主持或发出加入请求时
 *      log.Observe(stateNotify);                // 每收到一次 1725
 *      texts = FormatShareEndedTexts(seconds, names(log.Sharers()), names(log.Participants()), kMaxNames);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "ShareProtocolClient.h"

namespace screenshare {

//一场共享里出现过的人
class ShareParticipantLog
{
public:
    ShareParticipantLog();

    /**
     *  开始记录一场新的共享（清空先前的名单）。
     *  @param roomId 房间号；为空表示不再记录任何房间
     *  @param inCall 是否通话里的共享（结束时按通话里的规则决定要不要弹提示）
     */
    void Reset(const std::string& roomId, bool inCall);

    //当前记录的房间号；没有时为空
    const std::string& RoomId() const;

    //是否通话里的共享
    bool InCall() const;

    /**
     *  记下发起人（本人开始主持、发起成功时调用，2026-10-15）。服务端发起成功时不给发起人推 1725，没人加入的共享
     *  一条推送都收不到，不记的话结束提示的分享人一行是空的（2026-10-15 实机检查发现）。与 Observe 的口径相同：
     *  以第一次记下的为准，已经有发起人时不改；之后的 1725 也不会重复记他。
     *  @param hostId 发起人 userid；为 0 时忽略
     */
    void SetInitiator(int64_t hostId);

    /**
     *  记下一次成员状态推送里的人：房间号与当前记录的不同时忽略。发起人与 m_sharing 为真的成员记为分享人。
     *  @param n 解析好的 1725
     */
    void Observe(const ShareStateNotify& n);

    //分享人：发起人在最前，其余按第一次共享的先后
    std::vector<int64_t> Sharers() const;

    //参与者：出现过、但没有共享过屏幕的人，按第一次出现的先后
    std::vector<int64_t> Participants() const;

private:
    std::string          m_roomId;    //当前记录的房间号
    bool                 m_inCall;    //是否通话里的共享
    int64_t              m_hostId;    //发起人 userid（第一次看到的主持人，主持人移交后不变）；还没收到 1725 时为 0
    std::vector<int64_t> m_people;    //出现过的人，按第一次出现的先后
    std::vector<int64_t> m_sharers;   //共享过屏幕的人（不含发起人），按第一次共享的先后
};

//结束提示上的几行文字（都已翻译）
struct ShareEndedTexts
{
    std::wstring m_duration;       //「时长 01:37」
    std::wstring m_sharers;        //「分享人：张勇、韩晓燕888」
    std::wstring m_participants;   //「参与者：甲、乙 等 12 人」；没有参与者时为「参与者：无」
};

/**
 *  名单里一个人的写法（2026-10-05 用户要求）：本人写「你」，其余写显示名。
 *  @param userId      这个人的 userid
 *  @param selfId      本人的 userid
 *  @param displayName 这个人的显示名（已取好）
 *  @return 名单里显示的文字（已翻译）
 */
std::wstring ShareEndedNameOf(int64_t userId, int64_t selfId, const std::wstring& displayName);

/**
 *  写出结束提示的文字。
 *  @param durationSeconds 共享时长（秒），负数按 0
 *  @param sharers         分享人的名字，按显示顺序
 *  @param participants    参与者的名字，按显示顺序
 *  @param maxNames        参与者最多写几个名字，超出时写「等 N 人」（N 为参与者总人数）；不大于 0 时按 1
 *  @return 三行文字
 */
ShareEndedTexts FormatShareEndedTexts(int64_t durationSeconds, const std::vector<std::wstring>& sharers,
                                      const std::vector<std::wstring>& participants, int maxNames);

} // namespace screenshare
