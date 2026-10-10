/**
 *  多人共享的画面清单与多画面布局（第 22 期，docs/屏幕共享与录制方案.md 第 2 节第 27、28 条），ShareScreens.h
 *
 *  房间里同时可以有几个人在共享屏幕，每块画面在中转里有自己的发送座位（主持人用自己的座位，其他共享者用服务端另分配的画面座位）。
 *  本文件只有纯函数：
 *    - CollectShareScreens：从房间状态取出正在共享的人与画面座位，主持人排第一、其余按画面座位升序（观看端各自按这个顺序摆放）；
 *    - ComputeScreenTiles：多画面布局。默认按块数平分：1 块铺满、2 块左右、3～4 块田字格（更多时按近似正方形的网格）；
 *      放大某一块时它占上方，其余缩成底部居中的一排小图（高为区域的 kScreenStripPercent%，宽按 16:9 且不超过平分）；
 *    - HitScreenTile：点中哪一块（双击放大、还原用）；
 *    - CollectOtherScreens：本人要看的画面（去掉本人自己的那一块）；PickScreenRect：某块画面的矩形（控制键鼠换算坐标）；
 *    - SyncPublishPhase / ShareMineStateForPhase：本人作为共享者的阶段校正与按钮状态；ShareRequestQueue：主持人一侧的申请队列。
 *  单元测试见 Source/Tests/ShareMultiTests.cpp（SSMC2、SSMC3、SSMC5～SSMC8）。
 *
 *  典型用法（观看窗画面控件里）：
 *      std::vector<ShareScreenInfo> screens = CollectShareScreens(state);
 *      std::vector<RECT> tiles = ComputeScreenTiles(clientRect, (int)screens.size(), m_enlarged, kGapPx);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <deque>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include "ShareProtocolClient.h"

namespace screenshare {

//一块正在共享的画面
struct ShareScreenInfo
{
    int64_t m_ownerId;      //画面主人（共享者）的 userid
    int     m_ownerSeat;    //画面主人的成员座位
    int     m_screenSeat;   //这块画面在中转里的发送座位
    bool    m_isHost;       //画面主人是不是主持人
    bool    m_isWhiteboard; //是不是白板（第 23 期）：只有笔迹、没有视频；m_ownerId 为建立者，m_ownerSeat 为建立者的成员座位

    ShareScreenInfo();
};

//放大时底部一排小图的高度占区域高度的百分比
const int kScreenStripPercent = 20;
//小图的宽高比（宽 : 高）
const int kScreenThumbAspectW = 16;
const int kScreenThumbAspectH = 9;

/**
 *  正在共享的画面，主持人排第一、其余按画面座位升序；白板（第 23 期）排在所有屏幕之后，按画面座位升序。
 *  @param state 房间状态（1725 的快照）
 *  @return 画面清单；没人在共享时为空
 */
std::vector<ShareScreenInfo> CollectShareScreens(const ShareStateNotify& state);

/**
 *  房间里已占用的共享名额（第 23 期）：正在共享屏幕的人数加白板数。
 */
int ShareSlotsUsed(const ShareStateNotify& state);

/**
 *  按画面座位找它在清单里的下标。
 *  @return 下标；找不到返回 -1
 */
int FindScreenBySeat(const std::vector<ShareScreenInfo>& screens, int screenSeat);

/**
 *  多画面布局。
 *  @param area     可用区域
 *  @param count    画面块数
 *  @param enlarged 放大的那一块的下标；-1 或越界、或只有 1 块时按平分
 *  @param gapPx    块与块之间的间距（像素）
 *  @return 与画面清单一一对应的矩形；count 不大于 0 时为空
 */
std::vector<RECT> ComputeScreenTiles(const RECT& area, int count, int enlarged, int gapPx);

/**
 *  主持人退出时可以把主持人移交给谁（第 22 期，第 30 条）：除自己以外正在共享屏幕的人，按画面座位升序。为空时直接结束整个共享。
 *  @param state  房间状态
 *  @param selfId 本人（主持人）
 */
std::vector<int64_t> HostTransferCandidates(const ShareStateNotify& state, int64_t selfId);

/**
 *  本人作为共享者时「共享我的屏幕」按钮的状态（第 22 期）。
 *  @param phase 本人作为共享者的阶段，取值见 share_publish_phase
 *  @return 0 没有申请也没在共享，1 已申请，2 正在共享（与 share_viewer_mine_state 取值一致）
 */
int ShareMineStateForPhase(int phase);

//本人作为共享者（经主持人同意、或经移交成为主持人后自己开始共享，另连一条中转连接推屏幕的人）的阶段
enum share_publish_phase
{
    share_publish_idle      = 0,   //没有申请也没在共享
    share_publish_requested = 1,   //已发 1737 申请，等主持人答复
    share_publish_picking   = 2,   //已收到同意（1738 带「只推流」令牌），选择窗开着，等本人选共享内容
    share_publish_streaming = 3,   //推流中
};

/**
 *  按房间快照校正本人作为共享者的阶段：选择内容中或推流中，而快照里本人已不在房间、或已不在共享（被主持人停掉）时回到空闲。
 *  申请中不按快照判断：快照可能生成于服务端处理本人申请之前，由 1738 的拒绝与 1737 的失败应答结束申请。
 *  @param phase  当前阶段，取值见 share_publish_phase
 *  @param state  最新的房间快照
 *  @param selfId 本人 userid
 *  @return 校正后的阶段
 */
int SyncPublishPhase(int phase, const ShareStateNotify& state, int64_t selfId);

/**
 *  本人要看的画面（观看窗与主持人的「其他人的屏幕」窗口共用）：画面清单去掉本人自己的那一块，顺序同 CollectShareScreens。
 *  本人共享的画面经中转也会送到本人的观看连接，显示出来没有用处，共享整块屏幕时还会出现画面里套着画面。
 *  @param state  房间快照
 *  @param selfId 本人 userid
 */
std::vector<ShareScreenInfo> CollectOtherScreens(const ShareStateNotify& state, int64_t selfId);

/**
 *  某块画面实际所占的矩形（第 22 期，控制键鼠按被控制的那一块换算坐标）。
 *  @param tileSeats  画面清单的座位；为空表示没有清单，这时单画面就是要找的那一块
 *  @param videoRects 与 tileSeats 一一对应的矩形；没有清单时只有一个元素
 *  @param seat       要找的画面座位
 *  @param out        [出参] 矩形
 *  @return 找到且矩形不为空返回 true
 */
bool PickScreenRect(const std::vector<int>& tileSeats, const std::vector<RECT>& videoRects, int seat, RECT& out);

//主持人一侧排队等答复的共享申请（第 22 期）：右下角提示一次只显示队首那一个，答复后再显示下一个
class ShareRequestQueue
{
public:
    /**
     *  @param kind 排的是哪一种申请（第 23 期），取值见 enum share_sharing_kind：屏幕看成员项的 m_requesting，白板看 m_boardRequesting
     */
    explicit ShareRequestQueue(int kind = share_kind_screen);

    /**
     *  加入一个申请；已在队列里时不重复加入（名字以第一次为准）。
     *  @param userId 申请人
     *  @param name   申请人显示名
     *  @return 新加入返回 true
     */
    bool Add(int64_t userId, const std::wstring& name);

    /**
     *  去掉某人的申请（他撤回了、或已被答复）。
     *  @return 原来在队列里返回 true
     */
    bool Remove(int64_t userId);

    /**
     *  按房间快照去掉已离开房间、或已不在申请中（被处理掉了）的人；申请中看哪个字段由构造时的 kind 决定。
     *  @return 去掉的人，按原来的排队顺序
     */
    std::vector<int64_t> Prune(const ShareStateNotify& state);

    //队首的申请人；队列空时为 0
    int64_t FrontUser() const;
    //队首申请人的显示名；队列空时为空
    std::wstring FrontName() const;
    //排队的申请数
    size_t Size() const;
    //清空（离开房间或结束共享时）
    void Clear();

private:
    std::deque<std::pair<int64_t, std::wstring> > m_items;   //排队的申请：userid 与显示名，按到达顺序
    int                                           m_kind;    //排的是哪一种申请，取值见 enum share_sharing_kind
};

/**
 *  点中哪一块。
 *  @return 下标；没点中任何一块（如点在间距上）返回 -1
 */
int HitScreenTile(const std::vector<RECT>& tiles, POINT pt);

} // namespace screenshare
