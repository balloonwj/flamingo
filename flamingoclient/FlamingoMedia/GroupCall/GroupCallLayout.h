/**
 *  群通话窗口的宫格排布与格子排序，GroupCallLayout.h
 *
 *  纯函数，不依赖窗口与控件，单元测试见 flamingoclient/Source/Tests/GroupCallLayoutTests.cpp（标签 [groupcall]）：
 *    - GridShapeFor：按格子个数决定几行几列（1 人整幅、2 人左右两格、3~4 人 2×2、5~6 人 2×3、7~9 人 3×3）；
 *    - BuildGroupCallTiles：按房间快照排出宫格里的格子（本人在最前，其次开着摄像头的人、没开的人、振铃中的人），
 *      宫格最多 kGroupCallMaxGridTiles 格，多出的人列进底部的头像小条；
 *    - SubscribedCameraSeats：宫格里开着摄像头的其他人的座位号（第 5 期据此发订阅控制帧，只收看得见的画面）；
 *    - FormatGroupCallDuration / MakeGroupCallTitle 等文字的拼法；
 *    - DecideSelfMicOnState / IsGroupCallMicLocked / MakeGroupCallStatusLine：全员静音（房间状态，2026-10-05）下本人麦克风的
 *      处理、按钮是否可用与状态行文字。
 *  设计见 docs/群音视频通话方案.md 6.1。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "GroupCallProtocolClient.h"

namespace groupcall {

//宫格最多几格：与摄像头同时打开的上限一致，超过的人放进底部头像小条
const int kGroupCallMaxGridTiles = 9;

//宫格里的一格
struct GroupCallTile
{
    int64_t m_userId;    //userid
    int     m_seat;      //座位号；振铃中的人为 0
    bool    m_self;      //是否本人
    bool    m_ringing;   //是否还在振铃（被邀请、没接听）
    bool    m_camera;    //是否开着摄像头（本人按本地状态，其他人按快照）
    bool    m_mic;       //是否开着麦克风
    bool    m_online;    //是否在线（false 表示掉线、处于宽限期）

    GroupCallTile();
};

/**
 *  按格子个数决定宫格的行列数。
 *  @param count 格子个数；不大于 0 时按 1 处理，大于 kGroupCallMaxGridTiles 时按上限处理
 *  @param rows  [出参] 行数
 *  @param cols  [出参] 列数
 */
void GridShapeFor(int count, int& rows, int& cols);

/**
 *  排出宫格与底部小条里的格子。顺序：本人（已加入时）→ 开着摄像头的其他已加入成员（按座位号）→ 没开摄像头的其他
 *  已加入成员（按座位号）→ 振铃中的人（按快照里的先后）。前 maxGridTiles 个进宫格，其余进小条。
 *  @param state        最近一次房间快照
 *  @param selfId       本人 userid
 *  @param selfCamera   本人摄像头是否真的开着（以本地设备状态为准，快照可能还没刷新）
 *  @param selfMic      本人麦克风是否开着（同上）
 *  @param maxGridTiles 宫格最多几格（正常传 kGroupCallMaxGridTiles）
 *  @param grid         [出参] 宫格里的格子（先清空）
 *  @param strip        [出参] 底部小条里的人（先清空）
 */
void BuildGroupCallTiles(const GroupCallState& state, int64_t selfId, bool selfCamera, bool selfMic, int maxGridTiles,
                         std::vector<GroupCallTile>& grid, std::vector<GroupCallTile>& strip);

/**
 *  宫格里开着摄像头的其他已加入成员的座位号（升序）。窗口最小化时调用方传空的 grid，结果为空，即一路画面都不要。
 *  @param grid 宫格里的格子
 *  @return 座位号列表
 */
std::vector<int> SubscribedCameraSeats(const std::vector<GroupCallTile>& grid);

/**
 *  两组宫格的「结构」是否相同：格子个数、每格的人、是否振铃中、摄像头开关都一样时返回 true。
 *  结构相同时通话窗只刷新文字与画面，不必重建宫格控件。
 */
bool SameGridStructure(const std::vector<GroupCallTile>& a, const std::vector<GroupCallTile>& b);

/**
 *  宫格与小条里开着摄像头的已加入成员人数（含本人），振铃中的不算。摄像头码率按它调整。
 *  @param grid  宫格里的格子
 *  @param strip 底部小条里的人
 */
int CountGroupCallCameras(const std::vector<GroupCallTile>& grid, const std::vector<GroupCallTile>& strip);

/**
 *  本人摄像头这一路的目标码率（第 5 期码率自适应）：开摄像头的人越多，每一路越低，每个人收到的画面总码率因此不随人数
 *  线性上涨。1~2 路 400 kbps（与屏幕共享的摄像头相同），3~4 路 300 kbps，5 路及以上 200 kbps。
 *  @param camerasOn 房间里开着摄像头的人数（含本人）；不大于 0 时按 1 路处理
 *  @return 目标码率，单位 bit/s
 */
int GroupCallCameraBitrateFor(int camerasOn);

/**
 *  通话时长的文字：不足 1 小时为「MM:SS」，满 1 小时为「H:MM:SS」。
 *  @param elapsedMs 已进行的毫秒数；负数按 0
 */
std::wstring FormatGroupCallDuration(int64_t elapsedMs);

/**
 *  通话窗的标题：「群名 · 群语音通话」或「群名 · 群视频通话」；群名为空时只写后半句。
 *  @param groupName 群名
 *  @param media     媒体类型，取值见 enum gc_media
 */
std::wstring MakeGroupCallTitle(const std::wstring& groupName, int media);

/**
 *  来电窗的提示：「某某 邀请你加入群语音通话」（群视频时写群视频通话）。
 *  @param inviterName 邀请人显示名
 *  @param media       媒体类型
 */
std::wstring MakeGroupCallInvitePrompt(const std::wstring& inviterName, int media);

/**
 *  来电窗第二行：「群名 · N 人正在通话」；N 为 0 时只写群名。
 */
std::wstring MakeGroupCallInviteDetail(const std::wstring& groupName, int joinedCount);

//收到房间快照后本人麦克风的处理（2026-10-05 全员静音改为房间状态，docs/音视频通话优化方案.md）
enum gc_self_mic_action
{
    gc_self_mic_keep          = 0,   //不用动：本地麦克风本来就关着，或快照里本人的麦克风仍开着，或没有静音的原因
    gc_self_mic_muted_by_peer = 1,   //被发起人、群主或管理员单独静音（事件 9 指向本人）：关掉本地麦克风，之后可以自己重新打开
    gc_self_mic_muted_by_all  = 2,   //全员静音中（刚开启，或本人在全员静音期间加入）：关掉本地麦克风，解除之前不能自己打开
};

/**
 *  收到 1745 后决定本人麦克风要不要关、按哪种原因提示。只有快照里本人（已加入）的麦克风已是关、而本地还开着时才需要关。
 *  @param state      刚收到的房间快照
 *  @param selfId     本人 userid
 *  @param canManage  本人是否有管理权（发起人、群主、管理员）；有管理权的人不受全员静音约束
 *  @param localMicOn 本地麦克风此刻是否开着
 *  @return 取值见 enum gc_self_mic_action
 */
int DecideSelfMicOnState(const GroupCallState& state, int64_t selfId, bool canManage, bool localMicOn);

/**
 *  本人的麦克风按钮是否因全员静音而不可用：房间处于全员静音、本人又没有管理权。
 *  @param allMuted  房间是否处于全员静音（快照的 allmuted）
 *  @param canManage 本人是否有管理权（发起人、群主、管理员）
 */
bool IsGroupCallMicLocked(bool allMuted, bool canManage);

/**
 *  通话窗状态行的文字：临时提示优先；否则本人麦克风被全员静音锁住时写「已开启全员静音」，有人在说话时接在后面；
 *  其余情况只写谁在说话。
 *  @param notice    还在显示期内的临时提示；没有时为空
 *  @param micLocked 本人麦克风是否被全员静音锁住（IsGroupCallMicLocked 的结果）
 *  @param talkers   正在说话的其他人的名字（已连好）；没有时为空
 *  @return 状态行文字；为空时状态行不显示
 */
std::wstring MakeGroupCallStatusLine(const std::wstring& notice, bool micLocked, const std::wstring& talkers);

} // namespace groupcall
