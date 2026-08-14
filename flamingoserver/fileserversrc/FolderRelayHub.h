/**
 *  文件夹传输多通道中转配对表，FolderRelayHub.h
 *
 *  单聊「发送文件夹」的文件字节走 fileserver 中转（信令走 chatserver）。与远程桌面 /
 *  音视频那种「一次会话只有两条连接」不同，一次文件夹传输的每一端都要建 1 条控制通道
 *  （lane 0，走清单、目录就绪回执、中止与完成）与 N 条数据通道（lane 1..N，只走文件
 *  字节），且发送端第 k 条通道只与接收端第 k 条通道配对。因此配对键是 (sessionid, lane)
 *  而不是 imgserver RelayHub 的 sessionid，其余语义与它一致。设计出处见
 *  docs/单聊文件夹传输方案.md 第 4.2、5.2、6.2 节。
 *
 *  本表保存的是<u>对端会话</u>而不是对端连接：协议加密之后转发不是字节级透传，收到的
 *  包体要用本端会话的密钥解密、再用对端会话的密钥重新加密，故拿到连接还不够，必须拿到
 *  对端的会话对象（同 imgserversrc/RelayHub.h）。
 *
 *  「任一条通道断开即整个文件夹传输失败」是方案定死的语义（不做自动重连），所以本表除了
 *  按通道配对，还要能按 sessionid 一次取出该会话当前登记的<u>全部</u>通道，供调用方连带
 *  关闭。为此内部用两层 map（sessionid → lane → 该通道的端），一个会话的全部通道天然聚在
 *  一处，不必再单独维护一份「sessionid → 全部 lane」的索引，也就不存在两份索引对不上的
 *  风险。一次传输至多 2 ×（1 + 8）= 18 端，规模很小，查找用有序 map 足够。
 *
 *  只保存会话的<u>弱引用</u>：会话对象由 FileServer 的会话列表持有，连接出错时随之销毁；
 *  转发前再提升（lock），提升失败即视作对端已走。表内条目由 leaveByConn / removeSession
 *  摘除，摘空的通道条目与会话条目立即删掉，不留空壳。
 *
 *  会话类型与连接类型做成模板参数，理由与 fileserversrc/FileSessionRemoval.h 相同：本表
 *  是纯逻辑，测试里代入假类型就能覆盖全部分支，不必构造真实的 TcpConnection / FileSession
 *  （见 tests/test_folderrelayhub.cpp）。接入时以 FileSession（或持有它的中转端点类）与
 *  std::shared_ptr<TcpConnection> 实例化即可。本模块只有头文件、没有对应的 .cpp：类模板的
 *  定义必须在每个实例化点可见，若把成员函数放进 .cpp，就只能在那里对具体类型做显式实例化，
 *  头文件反过来又要依赖 FileSession 与 TcpConnection，可测试性即告丧失。
 *
 *  线程安全：全部公开方法内部以互斥量保护，可被多个 I/O 工作线程并发访问。返回值都是
 *  按值返回的共享指针 / 容器，调用方在锁外使用，因而关闭对端连接时不会持有本表的锁。
 *  本模块自身不打日志，登记 / 摘除 / 连带关闭的日志由调用方（中转端点类）在拿到结果后
 *  输出，分工与 FileSessionRemoval.h 一致。
 *
 *  典型用法：
 *      //接入 fileserver 时先定下实例化的类型
 *      typedef fileserver::FolderRelayHubT<FileSession, std::shared_ptr<TcpConnection>> FolderRelayHub;
 *
 *      //中转握手帧校验通过后登记本端；返回 false 时须拒绝并关闭本连接
 *      if (!hub.join(sessionid, lane, role, session))
 *          return false;
 *
 *      //转发一帧：按本端的 (sessionid, lane) 取配对端
 *      FolderRelayHub::SessionPtr peer = hub.getPeer(sessionid, lane, conn);
 *
 *      //本条通道断开：先摘掉自己，再把同会话其余通道一并关闭
 *      std::string sid;
 *      hub.leaveByConn(conn, &sid);
 *      std::vector<FolderRelayHub::SessionPtr> rest;
 *      hub.getSessionEndpoints(sid, rest);
 *      //……逐个关闭 rest 里的会话，最后清掉整条会话……
 *      hub.removeSession(sid);
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <cstddef>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>

namespace fileserver {

//控制通道的通道序号。一次文件夹传输的第 0 条通道固定为控制通道，只走清单、目录就绪
//回执、中止与完成，不走文件字节，出处见 docs/单聊文件夹传输方案.md 第 4.2 节
constexpr int kFolderControlLane = 0;

//数据通道条数的硬上限：一次传输至多 8 条数据通道。上限的由来是 Windows 上 select 的
//FD_SETSIZE 限制（每个 I/O 线程至多 64 条连接，见方案第 3.6 节），一次传输在服务端占用
//2 ×（N + 1）条连接，故不能让客户端随意加通道。服务端配置项 folderlanes 默认 4、越界按
//本值截断，出处见 docs/单聊文件夹传输方案.md 第 4.2 节
constexpr int kFolderMaxDataLanes = 8;

//通道序号的合法上界（含）。控制通道占 0，数据通道顺排 1..kFolderMaxDataLanes，
//故一次传输的每一端至多 kFolderMaxLane + 1 = 9 条通道
constexpr int kFolderMaxLane = kFolderControlLane + kFolderMaxDataLanes;

//一条通道上至多登记的端数：发送端与接收端各一条连接，角色相反
constexpr size_t kFolderLaneEndpointLimit = 2;

/**
 *  文件夹传输的多通道中转配对表。
 *
 *  按 (sessionid, lane) 把发送端与接收端的同序号通道两两配对，供转发时定位对端；并按
 *  sessionid 聚合一次传输的全部通道，供「任一通道断开即整会话连带关闭」使用。本类不做
 *  令牌校验、不认识角色的具体取值（只要求同一通道上的两端角色不同），也不负责关闭连接
 *  ——这些都由调用方在拿到本表的返回值之后自行完成。
 *
 *  @tparam SessionType       中转会话类型，只需提供 getConnectionPtr()；本表只持有其
 *                            弱引用，不延长其生命周期
 *  @tparam ConnectionPtrType 连接指针类型，即 SessionType::getConnectionPtr() 的返回类型，
 *                            只需支持相等比较与布尔取值（std::shared_ptr 与裸指针都满足）
 */
template <typename SessionType, typename ConnectionPtrType>
class FolderRelayHubT
{
public:
    //登记进本表的会话指针类型，与调用方持有会话的方式一致
    typedef std::shared_ptr<SessionType> SessionPtr;

    FolderRelayHubT()
    {
    }

    ~FolderRelayHubT()
    {
    }

    FolderRelayHubT(const FolderRelayHubT& rhs) = delete;
    FolderRelayHubT& operator=(const FolderRelayHubT& rhs) = delete;

    /**
     *  判断通道序号是否落在合法区间内。中转握手帧里的 lane 由客户端自报，登记之前必须先
     *  过这一关，否则一枚令牌就能开出任意多条连接（见方案第 9 节「令牌」一条）。
     *  @param lane 待判定的通道序号，取自握手帧
     *  @return 落在 [kFolderControlLane, kFolderMaxLane] 内返回 true，否则返回 false
     */
    static bool isValidLane(int lane)
    {
        return lane >= kFolderControlLane && lane <= kFolderMaxLane;
    }

    /**
     *  把本端登记到指定会话的指定通道上。
     *
     *  同一个 (sessionid, lane) 至多两端且两端角色必须不同，故同一个
     *  (sessionid, lane, role) 重复登记一律被拒。登记被拒时表内不会留下任何新条目。
     *  角色的<u>具体取值</u>（RelayToken::role_folder_sender / role_folder_receiver）由
     *  调用方在校验中转令牌时确认，本表只关心同一通道上的两端是否互不相同。
     *
     *  调用方须保证一条连接至多登记一次（做法参照 RelaySession 的「已配对」标志），本表
     *  不为此再做一次全表扫描；否则 leaveByConn 只摘掉首个匹配项，其余条目会残留。
     *
     *  @param sessionid 会话 id（来自中转令牌），空串视为非法、直接拒绝
     *  @param lane      本连接的通道序号，取值范围见 isValidLane，越界直接拒绝
     *  @param role      本端角色，取值见 RelayToken::role_folder_*；同一通道上已有相同
     *                   角色时拒绝
     *  @param session   本端会话；本表只保存其弱引用，不延长其生命周期，空指针直接拒绝
     *  @return 登记成功返回 true；参数非法、通道已满两端、同角色重复登记返回 false
     */
    bool join(const std::string& sessionid, int lane, int role, const SessionPtr& session)
    {
        //会话 id 为空、通道序号越界、会话指针为空都属于非法登记
        if (sessionid.empty())
            return false;

        if (!isValidLane(lane))
            return false;

        if (!session)
            return false;

        std::lock_guard<std::mutex> guard(m_mutex);

        //先只读地判定能否登记，避免因为拒绝而在表里留下空的会话 / 通道条目
        typename SessionMap::iterator sessionIter = m_table.find(sessionid);
        if (sessionIter != m_table.end())
        {
            typename LaneMap::iterator laneIter = sessionIter->second.find(lane);
            if (laneIter != sessionIter->second.end())
            {
                EndpointList& endpoints = laneIter->second;

                //一条通道至多两端：发送端与接收端各一
                if (endpoints.size() >= kFolderLaneEndpointLimit)
                    return false;

                //同角色重复登记视为非法（同一通道上发送端与接收端各且仅一条）
                for (size_t i = 0; i < endpoints.size(); ++i)
                {
                    if (endpoints[i].m_role == role)
                        return false;
                }
            }
        }

        Endpoint endpoint;
        endpoint.m_role = role;
        endpoint.m_session = session;
        m_table[sessionid][lane].push_back(endpoint);
        return true;
    }

    /**
     *  取指定通道上 self 的配对端，供转发时定位。
     *
     *  调用方传入的 sessionid 与 lane 是本端登记时用的那一对，故该通道上排除掉自己之后剩下
     *  的那一端即配对端。会话对象已销毁（弱引用提升失败）或其连接已销毁的端一律跳过，视作
     *  已走。
     *
     *  本端必须确实登记在该通道上才会给出对端：连接已被 leaveByConn 摘除、或报了一个自己
     *  并未登记的通道序号时，一律返回空。这样一条连接就无法凭一对猜来的 (sessionid, lane)
     *  拿到别人的会话往里灌数据，通道断开之后也不会再取到残留的对端。
     *
     *  @param sessionid 本端登记时用的会话 id
     *  @param lane      本端登记时用的通道序号
     *  @param self      本端连接，用于在该通道的两端中区分自己与对端；空指针一律返回空，
     *                   以免把自己当成对端返回。本函数不获取、也不释放该指针的所有权
     *  @return 配对端会话；本端不在该通道上、对端未登记 / 已销毁 / 其连接已销毁、或该会话与
     *          通道根本不存在时返回空指针
     */
    SessionPtr getPeer(const std::string& sessionid, int lane, const ConnectionPtrType& self)
    {
        //本端连接为空时无法与对端区分，直接按「没有对端」处理
        if (!self)
            return SessionPtr();

        std::lock_guard<std::mutex> guard(m_mutex);
        typename SessionMap::iterator sessionIter = m_table.find(sessionid);
        if (sessionIter == m_table.end())
            return SessionPtr();

        typename LaneMap::iterator laneIter = sessionIter->second.find(lane);
        if (laneIter == sessionIter->second.end())
            return SessionPtr();

        EndpointList& endpoints = laneIter->second;
        SessionPtr peerSession;
        bool selfRegistered = false;
        for (size_t i = 0; i < endpoints.size(); ++i)
        {
            SessionPtr session = endpoints[i].m_session.lock();

            //弱引用提升失败：该端的会话对象已销毁，视作已走
            if (!session)
                continue;

            //连接对象已销毁的端既不可能是本端，也无从往它上面转发
            ConnectionPtrType conn = session->getConnectionPtr();
            if (!conn)
                continue;

            //按连接区分自己与对端：两端各有各的会话对象与连接
            if (conn == self)
                selfRegistered = true;
            else
                peerSession = session;
        }

        //本端不在该通道上（从未登记，或已被摘除）时不给对端
        if (!selfRegistered)
            return SessionPtr();

        return peerSession;
    }

    /**
     *  按连接摘除一端（连接断开时调用）。
     *
     *  摘除后若该通道还剩另一端，则返回它供调用方连带关闭；本端是该通道最后一端时返回空。
     *  摘空的通道条目与会话条目会立即从表中删除，不留空壳。一条连接至多对应一端，故命中
     *  首个匹配项后立即返回。
     *
     *  注意：这里返回的只是<u>同一条通道</u>上的对端。按方案的语义，任一条通道断开即整个
     *  文件夹传输失败，调用方还须用 getSessionEndpoints 取出同会话其余通道一并关闭，最后
     *  用 removeSession 清掉整条会话。
     *
     *  @param self         已断开的连接，按相等比较定位。空指针一律视为无匹配，以免把连接
     *                      指针恰好为空的端误当成匹配项摘掉。本函数不获取、也不释放该指针
     *                      的所有权
     *  @param sessionidOut [出参] 可传空指针表示不关心；非空且命中时，回填该连接所属的
     *                      会话 id，供调用方接着做整会话连带关闭。未命中时不改写它
     *  @return 同一通道上的对端会话（供连带关闭）；无对端、对端已销毁、或本连接不在表中时
     *          返回空指针
     */
    SessionPtr leaveByConn(const ConnectionPtrType& self, std::string* sessionidOut = NULL)
    {
        //空连接不参与匹配，否则会与连接指针已为空的端相等而误摘
        if (!self)
            return SessionPtr();

        std::lock_guard<std::mutex> guard(m_mutex);
        for (typename SessionMap::iterator sessionIter = m_table.begin();
             sessionIter != m_table.end(); ++sessionIter)
        {
            LaneMap& lanes = sessionIter->second;
            for (typename LaneMap::iterator laneIter = lanes.begin(); laneIter != lanes.end(); ++laneIter)
            {
                EndpointList& endpoints = laneIter->second;
                for (size_t i = 0; i < endpoints.size(); ++i)
                {
                    SessionPtr session = endpoints[i].m_session.lock();

                    //会话对象已销毁的端无从比对连接，跳过后继续找，不能就此中断查找
                    if (!session)
                        continue;

                    ConnectionPtrType conn = session->getConnectionPtr();
                    if (!conn || conn != self)
                        continue;

                    //同一通道上的另一端（至多一个），供调用方连带关闭
                    SessionPtr peer;
                    for (size_t j = 0; j < endpoints.size(); ++j)
                    {
                        if (j != i)
                            peer = endpoints[j].m_session.lock();
                    }

                    if (sessionidOut != NULL)
                        *sessionidOut = sessionIter->first;

                    endpoints.erase(endpoints.begin() + i);

                    //摘空的通道条目与会话条目立即删掉，否则长期运行下表里会积满空壳
                    if (endpoints.empty())
                        lanes.erase(laneIter);

                    if (lanes.empty())
                        m_table.erase(sessionIter);

                    return peer;
                }
            }
        }

        return SessionPtr();
    }

    /**
     *  取出一次传输当前登记着的全部通道端，供「任一通道断开即整会话连带关闭」使用。
     *
     *  典型调用时机是先用 leaveByConn 摘掉断开的那一端，再调用本函数拿到其余各端逐个关闭。
     *  会话对象已销毁的端（弱引用提升失败）不会出现在结果里，本就无须再关。
     *
     *  @param sessionid 会话 id；表中没有该会话时结果容器保持不变
     *  @param endpoints [出参] 结果<u>追加</u>到该容器尾部（便于按多个会话汇总），本函数
     *                   不清空它，故调用方通常传入空容器。顺序为通道序号升序、同一通道内
     *                   按登记先后
     */
    void getSessionEndpoints(const std::string& sessionid, std::vector<SessionPtr>& endpoints)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        typename SessionMap::iterator sessionIter = m_table.find(sessionid);
        if (sessionIter == m_table.end())
            return;

        LaneMap& lanes = sessionIter->second;
        for (typename LaneMap::iterator laneIter = lanes.begin(); laneIter != lanes.end(); ++laneIter)
        {
            EndpointList& laneEndpoints = laneIter->second;
            for (size_t i = 0; i < laneEndpoints.size(); ++i)
            {
                SessionPtr session = laneEndpoints[i].m_session.lock();
                if (session)
                    endpoints.push_back(session);
            }
        }
    }

    /**
     *  清除整条会话的全部通道条目。连带关闭做完之后调用，使后续各条连接断开时的
     *  leaveByConn 都变成无匹配的空操作；同时也是清理「会话对象已销毁、没走 leaveByConn」
     *  的残留条目的唯一手段。
     *  @param sessionid 会话 id
     *  @return 表中确有该会话并已清除返回 true；本就没有该会话返回 false
     */
    bool removeSession(const std::string& sessionid)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_table.erase(sessionid) > 0;
    }

    /**
     *  取当前登记着的会话条数，供监控与测试断言「摘除后不留空条目」用。
     *  @return 表中会话条目的个数
     */
    size_t getSessionCount()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_table.size();
    }

    /**
     *  取某次传输当前登记着的通道条数。
     *  @param sessionid 会话 id
     *  @return 该会话下的通道条目个数；没有该会话返回 0
     */
    size_t getLaneCount(const std::string& sessionid)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        typename SessionMap::iterator sessionIter = m_table.find(sessionid);
        if (sessionIter == m_table.end())
            return 0;

        return sessionIter->second.size();
    }

    /**
     *  取某条通道上当前登记着的端数（0、1 或 2），供监控与测试断言登记结果用。
     *  @param sessionid 会话 id
     *  @param lane      通道序号
     *  @return 该通道上的端数；会话或通道不存在返回 0
     */
    size_t getEndpointCount(const std::string& sessionid, int lane)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        typename SessionMap::iterator sessionIter = m_table.find(sessionid);
        if (sessionIter == m_table.end())
            return 0;

        typename LaneMap::iterator laneIter = sessionIter->second.find(lane);
        if (laneIter == sessionIter->second.end())
            return 0;

        return laneIter->second.size();
    }

private:
    //一条通道上的一端（一条中转连接）
    struct Endpoint
    {
        int                        m_role;      //本端角色，见 RelayToken::role_folder_*；同一通道上两端取值必不相同
        std::weak_ptr<SessionType> m_session;   //该端的会话（弱引用，不延长其生命周期）
    };

    //一条通道上的两端，按登记先后排列，至多 kFolderLaneEndpointLimit 个
    typedef std::vector<Endpoint>     EndpointList;
    //一次传输的全部通道：lane → 该通道的两端。有序容器使遍历顺序恒为通道序号升序
    typedef std::map<int, EndpointList> LaneMap;
    //全部进行中的传输：sessionid → 该次传输的全部通道
    typedef std::map<std::string, LaneMap> SessionMap;

    std::mutex m_mutex;   //保护配对表，允许多个 I/O 工作线程并发访问
    SessionMap m_table;   //配对表本体，随各条通道的登记 / 摘除增删，空条目不保留
};

} // namespace fileserver
