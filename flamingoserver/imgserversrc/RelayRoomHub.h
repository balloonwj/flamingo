/**
 *  屏幕共享房间中转的成员表，RelayRoomHub.h
 *
 *  imgserver 现有的 RelayHub 把同一 sessionid 的两端两两配对（远程桌面、音视频通话）。屏幕共享是一个房间挂
 *  多名成员：发起人坐 0 号座位，观看者坐 1~254 号，任一成员发来的帧要转发给房间里其余所有人。本表按
 *  房间号 → 座位号 → 成员会话 记录登记情况，供转发时取「房间里的其他人」、发关键帧请求时按座位号取某个成员。
 *  与 RelayHub 并列，不改动它的两端配对语义。设计见 docs/屏幕共享与录制方案.md 5.4。
 *
 *  本表保存的是<u>会话</u>而不是连接：协议加密之后转发不是字节级透传，要用接收方会话的密钥重新加密
 *  （同 RelayHub.h 的说明）。只保存会话的<u>弱引用</u>：会话由 RelayServer 的会话列表持有，连接出错时随之
 *  销毁；转发前再提升（lock），提升失败即视作此人已走。
 *
 *  会话类型与连接指针类型做成模板参数，理由同 fileserversrc/FolderRelayHub.h：本表是纯逻辑，测试里代入
 *  假类型就能覆盖全部分支，见 tests/test_relayroomhub.cpp（Suite RRH）。本模块只有头文件。
 *
 *  线程安全：全部公开方法内部以互斥量保护，可被多个 I/O 工作线程并发访问；返回值按值返回，调用方在锁外使用。
 *  本模块不打日志，登记 / 摘除的日志由调用方输出。
 *
 *  典型用法：
 *      typedef imgserver::RelayRoomHubT<RelaySession, std::shared_ptr<TcpConnection>> RelayRoomHub;
 *      //握手帧里的令牌校验通过后登记本端（座位号取自令牌的 peeruid 字段）；返回 false 时须拒绝并关闭本连接
 *      if (!hub.join(roomId, seat, role, session))
 *          return false;
 *      //转发一帧：取房间里除自己以外的成员
 *      std::vector<RelayRoomHub::MemberRef> others;
 *      hub.getOthers(roomId, conn, others);
 *      //连接断开：按连接摘除
 *      hub.leaveByConn(conn, &roomId, &seat);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <cstddef>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <utility>
#include "../utils/RelayToken.h"

namespace imgserver {

//发起人的座位号
constexpr int kRoomHostSeat = 0;
//座位号上限（含）：中转帧头的发送者座位号只有 1 字节，255 留给服务端生成的帧
constexpr int kRoomMaxSeat = 254;

//房间的种类，由成员令牌里的角色决定；同一房间里只能有一种
enum room_kind
{
    room_kind_none        = 0,   //不是房间中转的角色（远程桌面、一对一通话、文件夹传输、P2P 回落等）
    room_kind_screenshare = 1,   //屏幕共享房间：发起人 7、观看者 8、只推流的共享者 14（第 22 期）
    room_kind_groupcall   = 2,   //群通话房间：成员 13
};

/**
 *  屏幕共享房间中转的成员表。
 *
 *  @tparam SessionType       中转会话类型，只需提供 getConnectionPtr()；本表只持有其弱引用
 *  @tparam ConnectionPtrType 连接指针类型，即 SessionType::getConnectionPtr() 的返回类型，
 *                            只需支持相等比较与布尔取值（std::shared_ptr 与裸指针都满足）
 */
template <typename SessionType, typename ConnectionPtrType>
class RelayRoomHubT
{
public:
    //登记进本表的会话指针类型
    typedef std::shared_ptr<SessionType> SessionPtr;

    //取成员时返回的一项：座位号与（已提升的）会话
    struct MemberRef
    {
        int        m_seat;      //座位号
        SessionPtr m_session;   //成员会话，非空
        int        m_role;      //登记时的角色（第 22 期起转发时据此跳过「只推流」的连接）
        int64_t    m_userId;    //登记时的 userid（第 22 期，来自令牌）；转发时据此跳过发送者本人的其他连接。0 表示不知道
    };

    RelayRoomHubT()
    {
    }

    ~RelayRoomHubT()
    {
    }

    RelayRoomHubT(const RelayRoomHubT& rhs) = delete;
    RelayRoomHubT& operator=(const RelayRoomHubT& rhs) = delete;

    /**
     *  判断座位号是否落在合法区间 [kRoomHostSeat, kRoomMaxSeat] 内。
     */
    static bool isValidSeat(int seat)
    {
        return seat >= kRoomHostSeat && seat <= kRoomMaxSeat;
    }

    /**
     *  角色所属的房间种类：屏幕共享（7 / 8）与群通话（13）的成员不能出现在同一个房间里。
     *  @param role 令牌里的角色，取值见 RelayToken::role_*
     *  @return 取值见 enum room_kind；不是房间角色时为 room_kind_none
     */
    static int roomKindOfRole(int role)
    {
        //屏幕共享：发起人、观看者与主持人以外共享者的「只推流」连接（第 22 期）
        if (role == RelayToken::role_screenshare_host || role == RelayToken::role_screenshare_member ||
            role == RelayToken::role_screenshare_publisher)
            return room_kind_screenshare;
        if (role == RelayToken::role_groupcall_member)
            return room_kind_groupcall;
        return room_kind_none;
    }

    /**
     *  把本端登记到房间的指定座位上。
     *  屏幕共享的发起人（RelayToken::role_screenshare_host）只能坐 0 号，观看者（role_screenshare_member）与群通话成员
     *  （role_groupcall_member，docs/协议设计-v2.md 21.8）不能坐 0 号；其它角色一律拒绝。房间里已有别的种类的成员
     *  （屏幕共享与群通话混用）时拒绝。座位已被占用时拒绝，已有成员不受影响。登记被拒时表内不会留下任何新条目。
     *  调用方须保证一条连接至多登记一次（握手只处理一次）。
     *  @param roomId  房间号（来自令牌的 sessionid），空串直接拒绝
     *  @param seat    座位号（来自令牌的 peeruid 字段），越界直接拒绝
     *  @param role    本端角色，取值见 RelayToken::role_screenshare_* 与 role_groupcall_member
     *  @param session 本端会话；只保存其弱引用，空指针直接拒绝
     *  @param userId  本端的 userid（来自令牌的 uid 字段，第 22 期）；0 表示不知道
     *  @return 登记成功返回 true
     */
    bool join(const std::string& roomId, int seat, int role, const SessionPtr& session, int64_t userId = 0)
    {
        if (roomId.empty() || !isValidSeat(seat) || !session)
            return false;

        //发起人只能坐 0 号，观看者与群通话成员只能坐 1~254 号；其它角色不属于房间中转
        const int kind = roomKindOfRole(role);
        if (kind == room_kind_none)
            return false;
        if (role == RelayToken::role_screenshare_host)
        {
            if (seat != kRoomHostSeat)
                return false;
        }
        else if (seat == kRoomHostSeat)
        {
            return false;
        }

        std::lock_guard<std::mutex> guard(m_mutex);
        //先只读地判定座位是否已被占用、房间种类是否一致，避免因为拒绝而在表里留下空的房间条目
        typename RoomMap::iterator room = m_rooms.find(roomId);
        if (room != m_rooms.end() && room->second.find(seat) != room->second.end())
            return false;
        if (room != m_rooms.end() && !room->second.empty() &&
            roomKindOfRole(room->second.begin()->second.m_role) != kind)
            return false;

        Member member;
        member.m_role = role;
        member.m_userId = userId;
        member.m_session = session;
        m_rooms[roomId][seat] = member;
        return true;
    }

    /**
     *  取房间里除 self 以外的成员，按座位号升序；会话已销毁或其连接已销毁的成员跳过。
     *  self 必须确实登记在该房间里，否则返回 false 且 out 为空 —— 一条连接不能凭一个猜来的房间号往别人的房间里灌数据。
     *  @param roomId 本端登记时用的房间号
     *  @param self   本端连接
     *  @param out    [出参] 其他成员
     *  @param selfUserId [出参] 可为 NULL；本端登记时的 userid（第 22 期，转发时据此跳过本人的其他连接）
     *  @return self 登记在该房间里返回 true
     */
    bool getOthers(const std::string& roomId, const ConnectionPtrType& self, std::vector<MemberRef>& out,
                   int64_t* selfUserId = NULL)
    {
        out.clear();
        if (selfUserId != NULL)
            *selfUserId = 0;
        if (!self)
            return false;

        std::lock_guard<std::mutex> guard(m_mutex);
        typename RoomMap::iterator room = m_rooms.find(roomId);
        if (room == m_rooms.end())
            return false;

        bool selfFound = false;
        for (typename SeatMap::iterator it = room->second.begin(); it != room->second.end(); ++it)
        {
            SessionPtr session = it->second.m_session.lock();
            if (!session)
                continue;
            ConnectionPtrType conn = session->getConnectionPtr();
            if (!conn)
                continue;
            if (conn == self)
            {
                selfFound = true;
                if (selfUserId != NULL)
                    *selfUserId = it->second.m_userId;
                continue;
            }
            MemberRef ref;
            ref.m_seat = it->first;
            ref.m_session = session;
            ref.m_role = it->second.m_role;
            ref.m_userId = it->second.m_userId;
            out.push_back(ref);
        }

        //本端不在这个房间里：不能把房间里的人交给它
        if (!selfFound)
            out.clear();
        return selfFound;
    }

    /**
     *  按座位号取房间里的某个成员（发关键帧请求时用）。
     *  @return 该成员的会话；房间或座位不存在、会话已销毁时返回空指针
     */
    SessionPtr getMember(const std::string& roomId, int seat)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        typename RoomMap::iterator room = m_rooms.find(roomId);
        if (room == m_rooms.end())
            return SessionPtr();
        typename SeatMap::iterator it = room->second.find(seat);
        if (it == room->second.end())
            return SessionPtr();
        return it->second.m_session.lock();
    }

    /**
     *  按连接摘除一名成员（连接断开时调用）。房间摘空后整个房间条目一并删除。
     *  @param conn         已断开的连接
     *  @param outRoomId    [出参] 可为 NULL；摘除成功时回填其所在房间号
     *  @param outSeat      [出参] 可为 NULL；摘除成功时回填其座位号
     *  @return 该连接确实登记过并已摘除时返回 true；重复摘除返回 false
     */
    bool leaveByConn(const ConnectionPtrType& conn, std::string* outRoomId, int* outSeat)
    {
        if (!conn)
            return false;

        //遍历全表：摘除该连接的同时，顺带清掉会话已经销毁的残留条目，免得它们一直占着座位
        bool found = false;
        std::lock_guard<std::mutex> guard(m_mutex);
        for (typename RoomMap::iterator room = m_rooms.begin(); room != m_rooms.end();)
        {
            for (typename SeatMap::iterator it = room->second.begin(); it != room->second.end();)
            {
                SessionPtr session = it->second.m_session.lock();
                if (!session)
                {
                    room->second.erase(it++);
                    continue;
                }
                if (!found && session->getConnectionPtr() == conn)
                {
                    found = true;
                    if (outRoomId != NULL)
                        *outRoomId = room->first;
                    if (outSeat != NULL)
                        *outSeat = it->first;
                    room->second.erase(it++);
                    continue;
                }
                ++it;
            }
            //房间摘空后整个条目一并删除，不留空壳
            if (room->second.empty())
                m_rooms.erase(room++);
            else
                ++room;
        }
        return found;
    }

    //当前的房间数（测试用：确认摘空后不留空房间）
    size_t roomCount()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_rooms.size();
    }

    //某房间当前登记的成员数（含会话已销毁、尚未摘除的）；房间不存在时为 0
    size_t memberCount(const std::string& roomId)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        typename RoomMap::const_iterator room = m_rooms.find(roomId);
        return room == m_rooms.end() ? 0 : room->second.size();
    }

    /**
     *  取某房间登记的全部座位与 userid，按座位号升序（云端录制开始时写座位记录用，第 26 期）。
     *  只读登记信息，会话已销毁、尚未摘除的成员也列出 —— 录像里多一条座位归属无害，合成时没有这个座位的帧就不显示。
     *  @param roomId 房间号
     *  @param out    [出参] (座位号, userid) 列表；房间不存在时为空
     */
    void listSeats(const std::string& roomId, std::vector<std::pair<int, int64_t> >& out)
    {
        out.clear();
        std::lock_guard<std::mutex> guard(m_mutex);
        typename RoomMap::const_iterator room = m_rooms.find(roomId);
        if (room == m_rooms.end())
            return;
        for (typename SeatMap::const_iterator it = room->second.begin(); it != room->second.end(); ++it)
            out.push_back(std::make_pair(it->first, it->second.m_userId));
    }

private:
    //房间里的一名成员
    struct Member
    {
        int                        m_role;      //角色，见 RelayToken::role_screenshare_* 与 role_groupcall_member
        int64_t                    m_userId;    //userid（第 22 期，来自令牌）；0 表示不知道
        std::weak_ptr<SessionType> m_session;   //成员会话（弱引用）

        Member() : m_role(0), m_userId(0)
        {
        }
    };

    typedef std::map<int, Member> SeatMap;           //座位号 → 成员（有序，取成员时自然按座位号升序）
    typedef std::map<std::string, SeatMap> RoomMap;  //房间号 → 座位表

    std::mutex m_mutex;   //保护 m_rooms
    RoomMap    m_rooms;   //全部房间
};

} // namespace imgserver
