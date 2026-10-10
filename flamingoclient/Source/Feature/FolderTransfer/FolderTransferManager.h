/**
 *  单聊「发送 / 接收文件夹」的客户端会话管理器，FolderTransferManager.h
 *
 *  本单例是文件夹传输在客户端的<u>唯一</u>协调者：把信令（chatserver 主连接上的
 *  cmd_folder_* 1810~1817）、目录扫描、两个传输状态机（FolderSendSession /
 *  FolderRecvSession）与界面（聊天窗里的文件夹气泡）串成一条完整链路。界面只负责
 *  「用户点了什么」与「把状态画出来」，会话生命周期一概不碰。
 *
 *  职责边界：
 *    · <u>本机同时可跑多笔</u>文件夹传输（发送与接收各算一笔），笔数上限来自配置项
 *      flamingo.ini 的 [app] foldermaxconcurrent（默认 5，见 kFolderMaxConcurrentDefault）。
 *      名额已满时再发起会被本单例拦下，调用方据返回值提示用户；服务端另有一道同样的
 *      校验（chatserver.conf 的 foldermaxperuser），<u>服务端是权威方</u>，本端这一道只是
 *      为了在用户点下去之前就给出即时提示，而不是等服务端拒绝之后才说。
 *    · 发送侧：扫描目录（每笔一条工作线程）→ 发 1810 → 记下服务端分配的会话 id → 等 1816
 *      中转参数 → 起 FolderSendSession → 进度回投界面 → 完成 / 失败发 1814 →
 *      支持「取消」（应答前发 1813、传输中按失败流程）与「重试」（发 1817，收到新的
 *      1816 后<u>复用同一份清单</u>重跑）。
 *    · 接收侧：收到 1811 推送 → 回投界面出邀请气泡 → 用户点「接收」并选好保存位置后
 *      发 1812 → 等 1816 → 起 FolderRecvSession → 完成 / 失败发 1814；点「拒绝」发
 *      1812(accept=0)。接收进度对象 FolderReceiveState 由<u>本单例</u>持有并在失败后
 *      保留，重试时原样交给新会话 —— 这是断点续传能接着传而不是从零开始的关键。
 *    · 失败会话在本端保留 kFolderRetryWindowMs（10 分钟，与服务端 folderretrywindow
 *      默认值同口径）。本单例记下失败时刻，界面据 IsWithinRetryWindow 决定把气泡上的
 *      「重试」画成可用还是置灰。<u>名额的释放与重试记录的保留是两件事</u>：一笔失败之后
 *      名额立刻放开（用户可以马上另发一笔），而它的清单 / 会话 id / 接收进度照旧留够
 *      保留期供重试续传。
 *    · 退出登录 / 掉线时 Reset()，把全部会话、清单、接收进度与保留期计时一并清掉 ——
 *      按产品定稿，重试窗口只在本次登录期间有效。
 *
 *  <u>线程模型</u>：两个状态机的回调发生在它们自己的工作线程上，本单例<u>不</u>在回调里
 *  碰任何窗口，一律把数据拷进堆上结构体后 PostMessage 给代理窗口（SetNotifyTarget 注入），
 *  由代理窗口按既有四层通路转投主窗口 → 主面板 → 聊天窗。除下面标注「可在任意线程调用」
 *  的几个之外，其余公开方法都应当在 UI 线程调用。
 *
 *  典型用法（发送端，聊天窗侧）：
 *      std::string localId;
 *      const int verdict = foldertransfer::FolderTransferManager::GetInstance().StartSend(
 *          peerUserId, folderPath, localId);
 *      if (verdict == foldertransfer::kFolderStartBusy)
 *          ...（提示"同时进行的文件夹传输已达上限，请等其中一笔结束"）
 *      else if (verdict == foldertransfer::kFolderStartOk)
 *          ...（用 localId 建一条 kFolderInviting 气泡，等 FMG_MSG_V2_FOLDER_SCANNED
 *              补齐规模统计、等 FMG_MSG_V2_FOLDER_INVITE_ACK 换成服务端会话 id）
 *
 *  balloonwj@qq.com   2026-08-14
 */

#pragma once

#include <windows.h>

#include <stdint.h>
#include <string>
#include <list>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>

//清单结构（重试续传时要原样复用同一份清单，故本单例须持有它的完整类型）。
//该头文件是纯逻辑、不引 winsock2.h，可以安全地被引入过 windows.h 的编译单元包含。
#include "FolderManifest.h"

//协议数据类型（net::FolderInviteResponse / FolderInviteNotify / FolderSessionResponse /
//FolderStateNotify / FolderRelayInfo）。这个头文件同样不引 net/Protocol.h —— 原先是
//为了不与 v1 的 net/Msg.h 撞枚举名（v1 协议头已于 2026-08-14 删除，该顾虑不复存在），
//现在的理由是不把整个协议头传染给本头文件的使用方。
#include "../../net/FolderProtocolClient.h"

//单聊单文件直传的两个扩展点（2026-10-04 起）：纯逻辑头文件，不引 winsock2.h
#include "DirectFileTransfer.h"

//P2P 通道工厂的具体类型（取链路路径用）：定义在 FolderP2PTransport.h，它带进 <winsock2.h>，此处只作前置声明
namespace balloonp2p {
class FolderP2PTransportFactory;
}

namespace foldertransfer {

//两个传输状态机与接收进度对象都定义在引入了 <winsock2.h> 的头文件里，那种头文件不能被
//已经引入过 <windows.h> 的编译单元（客户端绝大多数 .cpp 都是）再包含一次。故此处只作
//前置声明，成员一律用 std::shared_ptr 持有 —— shared_ptr 允许模板参数是不完整类型
//（析构走控制块里类型擦除后的删除器），故这些对象的构造与销毁都发生在
//FolderTransferManager.cpp 里，而本头文件的使用方不必看见它们的定义。
class FolderSendSession;
class FolderRecvSession;
class FolderReceiveState;

//本单例实现两个状态机回调接口的适配器。做成独立的类而不是让本单例直接继承那两个接口，
//同样是为了不把 <winsock2.h> 传染给本头文件的使用方。定义在 FolderTransferManager.cpp。
class FolderSendCallback;
class FolderRecvCallback;

//通道工厂（P2P 方案第 5 期）：定义在 FolderLaneTransport.h，它经 FolderRelayClient.h 带进 <winsock2.h>，同理只作前置声明
class IFolderLaneTransportFactory;

//交给扫描线程的一份入参（根目录、本地占位 id、对端 userid）。多笔传输可能同时在扫描，
//故入参随线程走、不再放在管理器的成员里。定义在 FolderTransferManager.cpp。
struct FolderScanTask;

// ======================== 常量 ========================

//失败会话在本端保留的时长，单位：毫秒。取 600000（10 分钟），与服务端配置项
//folderretrywindow 的默认值同口径。期内点「重试」是断点续传、不再打扰接收方；
//期满服务端已销毁会话，本端把「重试」置灰并提示只能重新发送。
const int64_t kFolderRetryWindowMs = 600000;

//本机同时可进行的文件夹传输笔数的默认值（发送与接收各算一笔）。配置项
//flamingo.ini 的 [app] foldermaxconcurrent 缺失或取值非法时回落到它。取 5，与服务端
//chatserver.conf 的 foldermaxperuser 默认值同口径（见 docs/单聊文件夹传输方案.md 第 2 节）。
const int kFolderMaxConcurrentDefault = 5;

//本机同时可进行的文件夹传输笔数的硬上限，配置值超出它即按非法处理、回落默认值。
//取 16 的依据：一笔传输在本端要建 folderlanes + 1 条中转连接（服务端默认 4 + 1 = 5 条）、
//每条连接一个收发线程，16 笔即约 80 条连接与 80 个线程；再往上本机的线程与带宽都摊不开，
//继续放大只会让每一笔都更慢，且服务端还有 foldermaxperuser 一道更严的闸门。
const int kFolderMaxConcurrentHardLimit = 16;

//本地占位会话 id 的前缀。发送端在拿到服务端分配的会话 id 之前就要把气泡画出来
//（扫描一个大目录可能耗时数秒），故先发一个本地 id 给界面定位气泡，等 1810 的应答
//到达再换成服务端那个。加前缀是为了在日志里一眼分清哪个是本地占位、哪个是真会话 id。
extern const char* const kFolderLocalIdPrefix;

//一条中转通道「连接 + 加密握手 + 报到」的总超时，单位：秒。取 20 秒 —— 中转握手是
//三帧往返，正常在百毫秒级完成；20 秒还没连上说明地址不可达或服务端已拒绝本次会话。
const int kFolderConnectTimeoutSec = 20;

/**
 *  把配置文件里读到的「本机同时可进行的传输笔数」规范成一个可用值。纯函数，可单测。
 *  @param rawValue 从 flamingo.ini 读出的原始值。读取接口对"键不存在"回的是调用方给的
 *                  默认值、对非数字与负数一律回 0，故本函数把「缺失」与「非法」收拢成
 *                  同一条越界分支处理
 *  @return 落在 [1, kFolderMaxConcurrentHardLimit] 之内的原值；越界一律回
 *          kFolderMaxConcurrentDefault（5）
 */
inline int NormalizeFolderMaxConcurrent(int rawValue)
{
    if (rawValue < 1 || rawValue > kFolderMaxConcurrentHardLimit)
        return kFolderMaxConcurrentDefault;
    return rawValue;
}

// ======================== 发起结果 ========================

//StartSend / AcceptInvite / RetryTransfer 的判定结果。界面按它给出对应提示，不要自行推断原因。
enum FolderStartVerdict
{
    kFolderStartOk          = 0,   //已受理：发送端已起扫描线程，接收端已发出同意应答，重试请求已发出
    kFolderStartBusy        = 1,   //本机同时进行的文件夹传输已达上限（发送与接收各算一笔）
    kFolderStartBadArgument = 2,   //参数非法：对端 userid 非正、目录路径为空、或会话 id 为空
    kFolderStartNoSession   = 3,   //会话已不存在（邀请已被对方撤回 / 已超时 / 已被本端处理过 / 已过保留期）
};

// ======================== 回投界面的负载结构 ========================

//目录扫描结果（FMG_MSG_V2_FOLDER_SCANNED 的 lParam，堆分配，界面处理后 delete）。
//扫描在工作线程上做，故规模统计不是 StartSend 当场就有的；界面先按本地 id 建一条气泡，
//收到本结构后再把文件数 / 目录数 / 总大小补上去。
struct FolderScanNotice
{
    std::string  m_localId;      //本地占位会话 id，界面据它定位刚建出来的那条气泡
    int64_t      m_peerUserId;   //对端 userid（气泡归属哪个会话窗）
    std::wstring m_rootPath;     //本次要发送的本地根目录完整路径（「打开文件夹」用）
    std::wstring m_folderName;   //根目录自身的名字（不含路径），气泡标题行显示它
    int64_t      m_dirCount;     //子目录总数（含空目录、不含根目录自身），单位：个
    int64_t      m_fileCount;    //文件总数，单位：个
    int64_t      m_totalBytes;   //全部文件字节数合计，单位：字节
    int          m_scanError;    //扫描失败类型，取值见 FolderScanError；kFolderScanOk(0) 表示成功
    std::string  m_errorPath;    //扫描出错处的相对路径（UTF-8）；成功时为空串

    FolderScanNotice()
        : m_peerUserId(0)
        , m_dirCount(0)
        , m_fileCount(0)
        , m_totalBytes(0)
        , m_scanError(kFolderScanOk)
    {
    }
};

//发起邀请的应答落到界面上的形态（由主面板在 UI 线程上就地填好后自行处理，
//不单独占一条窗口消息 —— 它是 FMG_MSG_V2_FOLDER_INVITE_ACK 处理过程中的产物）。
struct FolderBindNotice
{
    std::string m_localId;     //本地占位会话 id，界面据它定位气泡
    std::string m_sessionId;   //服务端分配的会话 id（仅 m_code 为 0 时有效），写回气泡供后续信令定位
    int         m_code;        //应答结果码，0 成功；非 0 见 net/Protocol.h 的 error_code
    int64_t     m_maxFiles;    //服务端配置的文件数上限；单位：个。仅规模超限（609/610）时有效
    int64_t     m_maxSize;     //服务端配置的总字节数上限；单位：字节。同上

    FolderBindNotice()
        : m_code(-1)           //-1：尚未填过，避免被误判为成功
        , m_maxFiles(0)
        , m_maxSize(0)
    {
    }
};

//一次传输进度（FMG_MSG_V2_FOLDER_SEND_PROGRESS / _RECV_PROGRESS 的 lParam，
//堆分配，界面处理后 delete）。收发两端共用同一个结构：字段含义完全对称，
//区别只在是「已发出并被对方确认落盘」还是「已收下并落盘」。
struct FolderProgressNotice
{
    std::string m_sessionId;    //本次传输的会话 id，界面据它定位气泡
    int         m_kind;         //传输种类，取值见 net::folder_transfer_kind（2026-10-04 起；单文件直传时界面更新文件气泡）
    int         m_linkPath;     //数据此刻走的 P2P 链路路径（1 局域网直连、2 打洞直连、3 中转）；0=没有走 P2P，供「P2P」徽章
    int64_t     m_doneBytes;    //已完成字节数（续传时含上次已落盘的部分），单位：字节
    int64_t     m_totalBytes;   //本次应传的总字节数，单位：字节
    int64_t     m_doneFiles;    //已完成的文件数，单位：个
    int64_t     m_totalFiles;   //文件总数，单位：个
    std::string m_currentFile;  //当前正在传的那个文件的相对路径（UTF-8）；可为空

    FolderProgressNotice()
        : m_kind(net::folder_kind_folder)
        , m_linkPath(net::kFolderLinkPathNone)
        , m_doneBytes(0)
        , m_totalBytes(0)
        , m_doneFiles(0)
        , m_totalFiles(0)
    {
    }
};

//一次传输的最终结果（FMG_MSG_V2_FOLDER_SEND_RESULT / _RECV_RESULT 的 lParam，
//堆分配，界面处理后 delete）。成功与失败共用本结构，靠 m_success 区分。
struct FolderResultNotice
{
    std::string  m_sessionId;    //本次传输的会话 id
    bool         m_success;      //true 全部文件传完并通过校验；false 任一步失败（含用户取消）
    int          m_errCode;      //失败原因，取值见 net/Protocol.h 的 error_code_folder_*（606~620）；成功时为 0
    std::string  m_errMsg;       //英文诊断串，仅供写日志；界面文案由 FolderErrorText 按 m_errCode 另行给出
    std::wstring m_localRoot;    //本地根目录完整路径（发送端为源目录、接收端为落地目录），供「打开文件夹」
    int64_t      m_totalFiles;   //本次涉及的文件总数，单位：个
    int64_t      m_totalBytes;   //本次涉及的字节数合计，单位：字节
    int          m_kind;         //传输种类，取值见 net::folder_transfer_kind（2026-10-04 起）
    int          m_linkPath;     //数据实际走的 P2P 链路路径；0=没有走 P2P。写进 1814，供「P2P」徽章
    std::string  m_md5;          //单文件直传时这个文件的 MD5（小写十六进制）；其余为空
    std::wstring m_localFile;    //单文件直传时本地文件的完整路径（发送端为源文件、接收端为落地文件）；其余为空

    FolderResultNotice()
        : m_success(false)
        , m_errCode(0)
        , m_totalFiles(0)
        , m_totalBytes(0)
        , m_kind(net::folder_kind_folder)
        , m_linkPath(net::kFolderLinkPathNone)
    {
    }
};

//一条<u>待应答</u>的文件夹邀请（本端是接收方，用户还没点「接收」/「拒绝」）。
//
//存在理由：按产品定稿，收到邀请<u>不再</u>弹出聊天窗（此前会凭空弹出一个空白窗口，
//是一次明显的打扰），只在会话列表记未读、闪托盘。于是邀请气泡必须"留得住" —— 用户
//稍后从会话列表点进会话时，聊天窗要能向本单例查回这些邀请并补画出「接收 / 拒绝」。
//本结构就是那份查询结果里的一条。已被拒绝 / 已取消 / 已超时 / 已被接收的会话查不出来，
//见 FolderTransferManager::GetPendingInvites。
struct FolderPendingInvite
{
    std::string m_sessionId;        //本次传输的会话 id（应答时须原样带回）
    int64_t     m_fromUserId;       //发起邀请的对端 userid
    std::string m_folderNameUtf8;   //根目录名（UTF-8，只有名字、不含路径），气泡标题行显示它
    int64_t     m_dirCount;         //子目录总数（含空目录、不含根目录自身），单位：个
    int64_t     m_fileCount;        //文件总数，单位：个
    int64_t     m_totalBytes;       //全部文件字节数合计，单位：字节
    //本端收到这条邀请的时刻（::GetTickCount64 取值，单位：毫秒）。用途是让界面能算出
    //"这条邀请已经挂了多久" —— 服务端的邀请超时是 folderinvitetimeout（默认 30 分钟），
    //而本端不自行判超时（以服务端推来的 1815 状态 6 为准），故本字段只作展示与排查用。
    int64_t     m_receivedAtMs;
    int         m_kind;             //传输种类，取值见 net::folder_transfer_kind（2026-10-04 起）：单文件直传时聊天窗补画文件气泡

    FolderPendingInvite()
        : m_fromUserId(0)
        , m_dirCount(0)
        , m_fileCount(0)
        , m_totalBytes(0)
        , m_receivedAtMs(0)
        , m_kind(net::folder_kind_folder)
    {
    }
};

// ======================== 会话表（纯逻辑，可单测） ========================

//本端在某一笔传输里扮演的角色。
enum FolderLocalRole
{
    kFolderRoleNone   = 0,   //未定：记录刚建出来、还没定角色（正常不会停留在这个值）
    kFolderRoleSender = 1,   //本端是发送方（发起邀请、扫描并发字节的一方）
    kFolderRoleRecv   = 2,   //本端是接收方（建目录、收字节落盘的一方）
};

/**
 *  一笔文件夹传输在本端的全部状态。
 *
 *  账面字段（角色、会话 id、清单、进度统计、失败时刻、待应答邀请）是纯数据，可脱离网络
 *  与磁盘单测；四个运行期对象（两个状态机与两个回调适配器）用 std::shared_ptr 持有，
 *  是因为它们的类型在本头文件里只有前置声明 —— shared_ptr 允许不完整类型，构造与销毁
 *  都在 FolderTransferManager.cpp 里完成。
 *
 *  <u>销毁顺序有硬约束</u>：状态机必须先于回调适配器销毁，否则尚未退出的工作线程会调到
 *  已析构的适配器上。故这四个指针一律经 FolderTransferManager::StopAndDestroyRuntime
 *  统一收尾，不要在别处零散地 reset。
 */
struct FolderSessionRecord
{
    int          m_role;          //本端角色，取值见 FolderLocalRole
    int          m_kind;          //传输种类，取值见 net::folder_transfer_kind（2026-10-04 起）。单文件直传不占文件夹传输的
                                  //并发名额（服务端另行限额），数据只走 P2P 链路
    int64_t      m_peerUserId;    //对端 userid
    std::string  m_localId;       //发送端的本地占位会话 id（服务端会话 id 到达前用它定位气泡）；接收端为空
    std::string  m_sessionId;     //服务端分配的会话 id；空=发送端尚未收到 1810 的应答
    //是否仍占用「本机同时可进行的传输笔数」名额：从发起 / 收到邀请起为 true，直到成功
    //结束、被拒绝 / 取消 / 超时、或失败为止。失败之后它转为 false（名额立刻放开，用户可以
    //另发一笔），而本记录的其余字段<u>照旧保留</u>供保留期内的重试续传，两者是两回事。
    bool         m_active;
    int64_t      m_failedAtMs;    //本笔失败的时刻（::GetTickCount64 取值，毫秒）；0=尚未失败

    //—— 发送端字段 ——
    std::wstring m_rootPath;      //本次要发送的本地根目录完整路径
    std::wstring m_folderName;    //根目录自身的名字（不含路径）
    FolderManifest m_manifest;    //扫描出的清单。<u>重试时原样复用</u>，故不能随状态机销毁
    bool         m_manifestReady; //清单是否已扫好（扫描线程置位）
    int64_t      m_totalFiles;    //本笔的文件总数，单位：个（进度与结果里都要带）
    int64_t      m_totalBytes;    //本笔的字节数合计，单位：字节

    //—— 接收端字段 ——
    std::wstring m_saveParentDir; //用户选定的保存位置（根目录建在它下面）
    std::wstring m_recvRootPath;  //实际落地的根目录完整路径（含自动改名后的名字）
    //接收进度（已落盘字节数 + 增量 MD5 上下文 + 落地根目录）。由本记录持有并在失败后保留，
    //重试时原样交给新建的接收会话 —— 这是断点续传的关键，不能随状态机对象销毁。
    std::shared_ptr<FolderReceiveState> m_recvState;
    //本端为接收方且用户尚未点「接收」/「拒绝」时，这里存着那条邀请的展示信息；
    //<u>m_sessionId 为空即表示当前没有待应答的邀请</u>（用户已应答、会话已作废都会把它清空）。
    FolderPendingInvite m_invite;

    //—— 运行期对象（销毁顺序见类注释）——
    std::shared_ptr<FolderSendSession>  m_sendSession;    //发送端状态机；空=当前没有在传
    std::shared_ptr<FolderRecvSession>  m_recvSession;    //接收端状态机；空=当前没有在收
    std::shared_ptr<FolderSendCallback> m_sendCallback;   //发送端回调适配器，生命周期须长于状态机
    std::shared_ptr<FolderRecvCallback> m_recvCallback;   //接收端回调适配器，同上
    //本轮的通道工厂（P2P 方案第 5 期）：1816 带 p2psessionid 且 P2P 可用时才有，生命周期须长于状态机；空=各条通道走中转
    std::shared_ptr<IFolderLaneTransportFactory> m_transportFactory;
    //同一个工厂的具体类型（2026-10-04 起）：取链路实际路径（「P2P」徽章、1814 的 linkpath）与是否走上过 P2P 用；
    //与 m_transportFactory 同时设置、同时清空
    std::shared_ptr<balloonp2p::FolderP2PTransportFactory> m_p2pFactory;

    //—— 单文件直传字段（2026-10-04 起，m_kind 为 net::folder_kind_file 时才有）——
    //发送端扩展点：记下发送会话算出的 MD5，传完随 1814 报给服务端。整笔传输（含重试）共用一个，随记录销毁
    std::shared_ptr<DirectFileSendHooks> m_directSendHooks;
    //接收端扩展点：文件直接落在 direct-<会话号> 目录下，记下落地路径与校验通过的 MD5。整笔传输共用一个，随记录销毁
    std::shared_ptr<DirectFileRecvHooks> m_directRecvHooks;

    FolderSessionRecord()
        : m_role(kFolderRoleNone)
        , m_kind(net::folder_kind_folder)
        , m_peerUserId(0)
        , m_active(false)
        , m_failedAtMs(0)
        , m_manifestReady(false)
        , m_totalFiles(0)
        , m_totalBytes(0)
    {
    }
};

/**
 *  本机全部文件夹传输的会话表（纯逻辑，不含锁、不碰网络与磁盘，可单测）。
 *
 *  按会话 id 管理多笔传输，并把「同时可进行几笔」这道闸门收在一处：占名额的记录数
 *  （m_active 为 true 的条数）达到上限即视为忙。失败的记录名额立刻释放但<u>留在表里</u>，
 *  直到超出保留期被 PurgeExpired 清掉或被用户重试 —— 这样才既不挡住新的传输，又能在
 *  保留期内断点续传。
 *
 *  线程安全由使用方保证：FolderTransferManager 的全部访问都在它自己那把互斥量内完成，
 *  故本类自身不加锁。容器用 std::list 而不是 std::vector，是因为本类对外交的是记录指针，
 *  list 在插入 / 删除别的元素时不会让已有指针失效。
 */
class FolderSessionRegistry
{
public:
    typedef std::list<FolderSessionRecord> RecordList;

    FolderSessionRegistry()
        : m_maxConcurrent(kFolderMaxConcurrentDefault)
    {
    }

    /**
     *  设置同时可进行的传输笔数上限。入参先经 NormalizeFolderMaxConcurrent 规范化，
     *  故传进越界值不会让闸门失效，只会回落到默认值。
     *  @param value 期望的上限，单位：笔
     */
    void SetMaxConcurrent(int value)
    {
        m_maxConcurrent = NormalizeFolderMaxConcurrent(value);
    }

    //当前生效的上限，单位：笔。可用于界面提示文案。
    int GetMaxConcurrent() const
    {
        return m_maxConcurrent;
    }

    //当前占着名额的文件夹传输笔数（失败待重试的不计入）。单文件直传不占这个名额（服务端也是分开限额的），不计入
    int ActiveCount() const
    {
        int count = 0;
        for (RecordList::const_iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_active && it->m_kind != net::folder_kind_file)
                ++count;
        }
        return count;
    }

    //当前在跑的单文件直传笔数（2026-10-04 起；失败待重试的不计入）。直传不占上面的文件夹名额，单独计数供日志
    int DirectActiveCount() const
    {
        int count = 0;
        for (RecordList::const_iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_active && it->m_kind == net::folder_kind_file)
                ++count;
        }
        return count;
    }

    //名额是否已满（达到上限即为满，界面据此拦下新的一笔）。
    bool IsFull() const
    {
        return ActiveCount() >= m_maxConcurrent;
    }

    //表里的记录条数（含失败待重试的那些）。仅供日志与断言。
    size_t Size() const
    {
        return m_records.size();
    }

    /**
     *  插入一条记录。
     *  @param record 要插入的记录（按值拷贝一份进表）
     *  @return 表内那份记录的地址（本类内的记录指针在别的插入 / 删除之后仍然有效）
     */
    FolderSessionRecord* Insert(const FolderSessionRecord& record)
    {
        m_records.push_back(record);
        return &m_records.back();
    }

    /**
     *  按服务端会话 id 查记录。
     *  @param sessionId 服务端分配的会话 id；空串一律查不到
     *  @return 命中的记录地址（不转移所有权）；查不到返回 NULL
     */
    FolderSessionRecord* FindBySessionId(const std::string& sessionId)
    {
        if (sessionId.empty())
            return NULL;
        for (RecordList::iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_sessionId == sessionId)
                return &(*it);
        }
        return NULL;
    }

    //同上，const 版本。
    const FolderSessionRecord* FindBySessionId(const std::string& sessionId) const
    {
        if (sessionId.empty())
            return NULL;
        for (RecordList::const_iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_sessionId == sessionId)
                return &(*it);
        }
        return NULL;
    }

    /**
     *  按发送端的本地占位会话 id 查记录。
     *  @param localId 本地占位 id；空串一律查不到
     *  @return 命中的记录地址；查不到返回 NULL
     */
    FolderSessionRecord* FindByLocalId(const std::string& localId)
    {
        if (localId.empty())
            return NULL;
        for (RecordList::iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_localId == localId)
                return &(*it);
        }
        return NULL;
    }

    /**
     *  按"服务端会话 id 或本地占位 id"查记录。界面上的气泡在服务端应答到达之前挂的是
     *  本地占位 id，之后才换成真会话 id，故凡是从界面传下来的 id 都应当用本方法查。
     *  @param id 会话 id 或本地占位 id
     *  @return 命中的记录地址；查不到返回 NULL
     */
    FolderSessionRecord* FindByAnyId(const std::string& id)
    {
        FolderSessionRecord* record = FindBySessionId(id);
        if (record != NULL)
            return record;
        return FindByLocalId(id);
    }

    /**
     *  登记一笔"邀请已发出、正在等 cmd_folder_invite(1810) 应答"的传输。
     *
     *  之所以要这么一张表：应答包体里没有任何能对回发起方的信息（既无本地占位 id 也无
     *  对端 userid），而本机可以有多笔传输同时在等应答。v2 帧头里的 seq 正好是这样一枚
     *  一一对应的标识 —— 服务端应答时原样带回请求的 seq，客户端据它精确命中，与
     *  cmd_request_upload / cmd_request_download 按 seq 找等待槽是同一套做法
     *  （见 net/FileTransfer 与 CIUSocket::RequestUpload）。
     *
     *  本表与记录表<u>各自独立</u>：用户在应答到达之前取消了那一笔时，记录被删掉而本表里
     *  这一项照旧留着，应答到达时按 seq 命中它、却按 id 查不到记录，调用方据此把服务端
     *  那边刚建好的会话撤掉。
     *  @param seq     本次邀请请求所用的帧 seq，须非 0（0 表示请求根本没发出去）
     *  @param localId 该笔的本地占位会话 id，须非空
     */
    void RegisterInviteSeq(uint32_t seq, const std::string& localId)
    {
        if (seq == 0 || localId.empty())
            return;
        m_inviteSeqMap[seq] = localId;
    }

    /**
     *  按帧 seq 取出并注销那笔"正在等应答"的传输（应答到达时调）。
     *  @param seq        应答帧带回的 seq
     *  @param outLocalId [出参] 该笔的本地占位会话 id；返回 false 时被清空
     *  @return 表里有这个 seq 返回 true；对不上（本端已 Reset、或应答重复到达）返回 false
     */
    bool TakeInviteSeq(uint32_t seq, std::string& outLocalId)
    {
        outLocalId.clear();
        std::map<uint32_t, std::string>::iterator it = m_inviteSeqMap.find(seq);
        if (it == m_inviteSeqMap.end())
            return false;
        outLocalId = it->second;
        m_inviteSeqMap.erase(it);
        return true;
    }

    //当前有几笔在等 1810 的应答。仅供日志与断言。
    size_t InviteAckPendingCount() const
    {
        return m_inviteSeqMap.size();
    }

    /**
     *  删除一条记录（按会话 id 或本地占位 id）。
     *  调用方须先把该记录的运行期对象取走并在锁外停掉，本方法只管账面。
     *  @param id 会话 id 或本地占位 id
     *  @return 删掉了返回 true；表里没有这条返回 false
     */
    bool Erase(const std::string& id)
    {
        if (id.empty())
            return false;
        for (RecordList::iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_sessionId == id || (!it->m_localId.empty() && it->m_localId == id))
            {
                m_records.erase(it);
                return true;
            }
        }
        return false;
    }

    //清空全部记录与等待应答表（退出登录 / 被踢下线）。同样只管账面。
    void Clear()
    {
        m_records.clear();
        m_inviteSeqMap.clear();
    }

    /**
     *  把一条记录标记为失败：名额立刻释放，其余字段（清单、会话 id、接收进度）照旧保留，
     *  供保留期内的重试续传。
     *  @param record 要标记的记录（表内那份的引用）
     *  @param nowMs  当前时刻（::GetTickCount64 取值，毫秒）
     */
    void MarkFailed(FolderSessionRecord& record, int64_t nowMs)
    {
        record.m_active = false;
        record.m_failedAtMs = nowMs;
        //失败之后这条邀请早已不是"待应答"，一并清掉，免得聊天窗还能查出来补画「接收 / 拒绝」。
        record.m_invite = FolderPendingInvite();
    }

    /**
     *  判定某笔失败的传输是否仍在保留期内（决定气泡上的「重试」画成可用还是置灰）。
     *  @param id    会话 id 或本地占位 id
     *  @param nowMs 当前时刻（::GetTickCount64 取值，毫秒）
     *  @return 该记录确实失败过、且失败至今不足 kFolderRetryWindowMs 返回 true；其余一律 false
     */
    bool IsWithinRetryWindow(const std::string& id, int64_t nowMs) const
    {
        if (id.empty())
            return false;
        for (RecordList::const_iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_sessionId != id && (it->m_localId.empty() || it->m_localId != id))
                continue;
            if (it->m_failedAtMs == 0)
                return false;
            return (nowMs - it->m_failedAtMs) < kFolderRetryWindowMs;
        }
        return false;
    }

    /**
     *  取出与某个对端之间<u>全部</u>待应答的邀请。同一个人可能先后发来两个文件夹，不同的人
     *  也可能同时发来邀请，故这里是多条语义 —— 聊天窗补画气泡时要把它们逐条画出来。
     *  @param peerUserId 会话对端 userid（即邀请的发起方），须为正
     *  @param out        [出参] 先被清空，再按收到的先后顺序追加
     *  @return 取到的条数
     */
    size_t CollectPendingInvites(int64_t peerUserId, std::vector<FolderPendingInvite>& out) const
    {
        out.clear();
        if (peerUserId <= 0)
            return 0;
        for (RecordList::const_iterator it = m_records.begin(); it != m_records.end(); ++it)
        {
            if (it->m_role != kFolderRoleRecv || !it->m_active)
                continue;
            //会话 id 为空即表示这条记录当前没有待应答的邀请（用户已应答，或会话已作废）
            if (it->m_invite.m_sessionId.empty())
                continue;
            if (it->m_invite.m_fromUserId != peerUserId)
                continue;
            out.push_back(it->m_invite);
        }
        return out.size();
    }

    /**
     *  清掉已失败且超出保留期的记录 —— 它们的服务端会话早已销毁，再留着只会让表越积越长。
     *  仍占名额的记录（含正在重试的）一律不动。
     *  @param nowMs 当前时刻（::GetTickCount64 取值，毫秒）
     *  @return 清掉的条数
     */
    size_t PurgeExpired(int64_t nowMs)
    {
        size_t removed = 0;
        RecordList::iterator it = m_records.begin();
        while (it != m_records.end())
        {
            if (!it->m_active && it->m_failedAtMs != 0
                && (nowMs - it->m_failedAtMs) >= kFolderRetryWindowMs)
            {
                it = m_records.erase(it);
                ++removed;
            }
            else
            {
                ++it;
            }
        }
        return removed;
    }

    //全部记录，供调用方遍历收尾（Reset 时要把每条记录的运行期对象都取走停掉）。
    RecordList& Records()
    {
        return m_records;
    }

    //同上，const 版本。
    const RecordList& Records() const
    {
        return m_records;
    }

private:
    int        m_maxConcurrent;   //同时可进行的传输笔数上限，单位：笔；由配置项决定
    RecordList m_records;         //全部记录，按建立先后排列
    //「邀请请求的帧 seq → 该笔的本地占位 id」，登记的是已发出邀请、正在等 1810 应答的
    //那几笔。理由见 RegisterInviteSeq。
    std::map<uint32_t, std::string> m_inviteSeqMap;
};

// ======================== 单例 ========================

/**
 *  文件夹传输会话管理器（进程级单例）。
 *
 *  本机同时可维持多笔文件夹传输，全部记在 FolderSessionRegistry 里按会话 id 路由。
 *  所有权关系：本单例（经会话表）<u>持有</u>发送 / 接收状态机、回调适配器与接收进度对象，
 *  界面只持有会话 id 字符串。
 */
class FolderTransferManager
{
public:
    /**
     *  取进程级唯一实例。首次调用时构造；线程安全（C++11 的函数内静态变量初始化）。
     *  @return 单例引用，调用方不得释放。
     */
    static FolderTransferManager& GetInstance();

    /**
     *  从 flamingo.ini 读取本机同时可进行的传输笔数上限（[app] foldermaxconcurrent）。
     *  登录成功后调用一次即可；缺失或取值非法时回落 kFolderMaxConcurrentDefault 并记一条
     *  英文日志。未调用过时上限即为默认值，故漏调不会让功能不可用，只是配置不生效。
     *  副作用：改写会话表里的上限；已经在跑的传输不受影响。
     */
    void LoadConfig();

    /**
     *  当前生效的「同时可进行的传输笔数」上限，单位：笔。界面把它写进提示文案，
     *  免得用户看到"已达上限"却不知道上限是几。
     *  可在任意线程调用。
     */
    int GetMaxConcurrent() const;

    /**
     *  当前占着名额的传输笔数（失败待重试的不计入），单位：笔。供界面与日志。
     *  可在任意线程调用。
     */
    int GetActiveCount() const;

    /**
     *  注入回投界面用的窗口句柄。全部进度与结果都 PostMessage 到它，再由既有的
     *  代理窗口 → 主窗口 → 主面板 → 聊天窗四层通路送达气泡。
     *  @param hNotifyWnd 代理窗口句柄；NULL 表示界面尚未就绪，此时本单例把要投的
     *                    堆负载就地释放，不会泄漏
     *  可在任意线程调用。
     */
    void SetNotifyTarget(HWND hNotifyWnd);

    /**
     *  发起一次「发送文件夹」。<u>立即返回</u>：目录扫描（上万个文件时可能耗时数秒）
     *  在本单例为这一笔新起的工作线程上做，扫完经 FMG_MSG_V2_FOLDER_SCANNED 回投界面，
     *  同时向服务端发 cmd_folder_invite(1810)。
     *  @param peerUserId 接收方 userid，须为正
     *  @param rootPath   要发送的本地目录完整路径，须非空；不接受盘符根目录
     *  @param outLocalId [出参] 本次传输的本地占位会话 id；界面据它建气泡并等待
     *                    FMG_MSG_V2_FOLDER_INVITE_ACK 把它换成服务端会话 id。
     *                    返回值不是 kFolderStartOk 时被清空
     *  @return 见 FolderStartVerdict。返回 kFolderStartBusy 时调用方应提示
     *          "同时进行的文件夹传输已达上限，请等其中一笔结束"
     *  副作用：为本笔起一条扫描线程；成功时占住一个并发名额。
     */
    int StartSend(int64_t peerUserId, const std::wstring& rootPath, std::string& outLocalId);

    /**
     *  发起一次单聊单文件 P2P 直传（2026-10-04 起）：立即构造只有这一个文件的清单（不扫描、不算 MD5），发
     *  cmd_folder_invite(1810，kind=1)。应答、状态与进度都走文件夹传输的那组窗口消息，界面按 localId / 会话 id
     *  对回文件气泡。不占文件夹传输的并发名额。
     *  @param peerUserId 接收方 userid，须为正
     *  @param filePath   要发送的本地文件完整路径，须存在且不是目录
     *  @param localId    界面给这一笔取的本地 id（聊天窗用文件气泡的 clientMsgId），须非空且未被占用
     *  @return kFolderStartOk 已发出邀请；kFolderStartBadArgument 参数非法、文件不存在或邀请构造不出来
     */
    int StartSendFile(int64_t peerUserId, const std::wstring& filePath, const std::string& localId);

    /**
     *  接收方同意接收（气泡上点了「接收」并选好了保存位置）。发
     *  cmd_folder_response(1812, accept=1)，随后等服务端下发 cmd_folder_relay_info(1816)。
     *  @param sessionId     邀请推送里带来的会话 id，须对应一条仍在待应答的邀请
     *  @param saveParentDir 用户选定的保存位置（根目录将建在它下面），须非空
     *  @return 见 FolderStartVerdict
     *  副作用：为本笔新建一份接收进度对象（名额在收到邀请时就已占住，此处不再另占）。
     */
    int AcceptInvite(const std::string& sessionId, const std::wstring& saveParentDir);

    /**
     *  接收方拒绝接收（气泡上点了「拒绝」）。发 cmd_folder_response(1812, accept=0)，
     *  该笔立即结束、名额放开，不产生任何中转连接。
     *  @param sessionId 邀请推送里带来的会话 id；查不到对应的待应答邀请时只记日志
     */
    void RejectInvite(const std::string& sessionId);

    /**
     *  取消某一笔传输（任一端在气泡上点了「取消」）。
     *  尚未开始传字节时发 cmd_folder_cancel(1813) 撤回邀请；已经在传时停掉本端状态机，
     *  随后按失败流程发 cmd_folder_close(1814)，错误码为 error_code_folder_canceled。
     *  @param sessionId 本次传输的会话 id 或本地占位 id；查不到对应记录时只记日志
     */
    void CancelTransfer(const std::string& sessionId);

    /**
     *  同上，并可声明改为上传服务器发离线文件（2026-10-04 起，单文件直传的发送方在对方接收之前点「发送离线文件」）。
     *  @param sessionId 会话 id 或本地 id
     *  @param toOffline true 时撤回邀请带 tooffline=1：服务端向接收方推「已改为离线发送」、不留文件消息；false 同上一个重载
     */
    void CancelTransfer(const std::string& sessionId, bool toOffline);

    /**
     *  某一笔是不是单聊单文件直传（2026-10-04 起）。可在任意线程调用。
     *  @param sessionId 会话 id 或本地 id
     *  @return 表里有这一笔且是单文件直传返回 true
     */
    bool IsDirectFile(const std::string& sessionId) const;

    /**
     *  重试续传（发送端在失败气泡上点了「重试」）。发 cmd_folder_retry(1817)；服务端
     *  校验会话未过期、双方仍在线后重签令牌并再下发一次 cmd_folder_relay_info，本单例
     *  收到后<u>复用同一份清单</u>重跑，接收端则复用同一份接收进度接着写。
     *  @param sessionId 失败会话的会话 id
     *  @return 见 FolderStartVerdict：已发出重试请求为 kFolderStartOk；名额已被别的传输
     *          占满为 kFolderStartBusy（提示"已达上限"）；会话对不上或已超出保留期为
     *          kFolderStartNoSession（提示"已超时，请重新发送"）
     *  副作用：成功时重新占住一个并发名额。
     */
    int RetryTransfer(const std::string& sessionId);

    /**
     *  判定某笔失败的传输是否仍在保留期内（决定气泡上的「重试」画成可用还是置灰）。
     *  @param sessionId 会话 id 或本地占位 id
     *  @return 该笔确实失败过、且失败至今不足 kFolderRetryWindowMs 返回 true；其余一律 false
     *  可在任意线程调用。
     */
    bool IsWithinRetryWindow(const std::string& sessionId) const;

    /**
     *  本机同时进行的文件夹传输是否已达上限（发送与接收各算一笔）。
     *  注意语义不是"有没有在传"，而是"还能不能再来一笔"。
     *  @return 已达上限返回 true
     *  可在任意线程调用。
     */
    bool IsBusy() const;

    /**
     *  查询与某个对端之间<u>全部</u>待应答的文件夹邀请。
     *
     *  收到邀请不再弹窗之后，聊天窗可能是在邀请到达之后才被用户点开的，那一刻窗口里
     *  没有任何邀请气泡。故聊天窗在建好并渲染完首屏历史之后调本方法查一次，把查到的
     *  邀请逐条补画成带「接收 / 拒绝」的气泡。同一个人先后发来两个文件夹、或多笔并发时，
     *  这里会返回多条。
     *
     *  只有"仍在等用户应答"的邀请才查得出来：已被用户接收（已转入传输）、已被拒绝、
     *  已被发送方撤回、已超时、以及退出登录后 Reset 过的，一律不在结果里 —— 界面据此
     *  不画气泡，避免给用户一个点了必然失败的「接收」按钮。
     *  @param peerUserId 会话对端 userid（即邀请的发起方），须为正
     *  @param out        [出参] 先被清空，再按收到的先后顺序追加
     *  @return 取到的条数
     *  可在任意线程调用。
     */
    size_t GetPendingInvites(int64_t peerUserId, std::vector<FolderPendingInvite>& out) const;

    /**
     *  处理 cmd_folder_invite(1810) 的应答。成功则把服务端分配的会话 id 记到发起它的那一笔
     *  上；失败则就地释放该笔占用的名额（规模超限、对方不在线、服务器繁忙等）。
     *  应答包体不带任何能对回发起方的信息，故一律按<u>帧 seq</u> 定位（见
     *  FolderSessionRegistry::RegisterInviteSeq）。
     *  @param seq  应答帧带回的 seq，即发起这笔邀请时 CIUSocket::SendFolderInvite 返回的那个
     *  @param resp 应答解析结果
     *  @param out  [出参] 供界面定位并更新气泡的信息，见 FolderBindNotice；seq 对不上任何
     *              一笔时 m_localId 为空串，界面据此不动任何气泡
     *  副作用：seq 命中不了、而服务端已建好会话时，替它发一条 cmd_folder_cancel 撤掉，
     *          免得那笔会话一直挂到邀请超时。
     */
    void OnInviteAck(uint32_t seq, const net::FolderInviteResponse& resp, FolderBindNotice& out);

    /**
     *  处理 cmd_folder_invite_notify(1811) 推送：记下这条待应答的邀请，等用户在气泡上
     *  点「接收」/「拒绝」。名额已满时<u>直接回一个拒绝</u>并记日志 —— 让对方立刻知道结果，
     *  好过把邀请挂在那里等超时。
     *  @param notify 邀请推送解析结果
     *  @return 邀请已被接下、界面应当出邀请气泡返回 true；已就地拒绝返回 false
     */
    bool OnInviteNotify(const net::FolderInviteNotify& notify);

    /**
     *  处理 cmd_folder_response / cancel / close / retry 四条请求的通用应答。
     *  只在失败时有实质动作（例如同意接收却被服务端拒绝时释放对应那一笔的名额）。
     *  @param cmd  发出的那条命令的协议号（1812 / 1813 / 1814 / 1817）
     *  @param resp 应答解析结果
     */
    void OnSessionAck(int cmd, const net::FolderSessionResponse& resp);

    /**
     *  处理 cmd_folder_state_notify(1815) 推送：某一笔的会话状态变更。
     *  拒绝 / 撤回 / 对方离线 / 超时 / 失败 / 完成这几种都意味着那一笔已经结束，本单例据此
     *  停掉它的状态机并释放它的名额（失败时保留接收进度与清单供重试）。
     *  @param notify 状态推送解析结果
     */
    void OnStateNotify(const net::FolderStateNotify& notify);

    /**
     *  处理 cmd_folder_relay_info(1816) 推送：中转参数下发，双方据此建通道开传。
     *  按会话 id 找到对应那一笔，再按 role 字段决定起发送端还是接收端状态机；重试时同样
     *  走这里，区别只在于清单与接收进度都复用上一次那份。
     *  @param info 中转参数解析结果
     */
    void OnRelayInfo(const net::FolderRelayInfo& info);

    /**
     *  发送端某一笔整体结束（界面在 UI 线程收到 FMG_MSG_V2_FOLDER_SEND_RESULT 后调用）：
     *  停掉该笔的状态机并回收线程，向服务端发 cmd_folder_close(1814)，成功则整条记录删掉、
     *  失败则保留清单与失败时刻供重试。
     *
     *  之所以由界面在 UI 线程回调本方法、而不是在状态机的失败回调里就地收尾：
     *  Stop() 会等驱动线程结束，在回调线程里调等于让它 join 自己（见
     *  IFolderSendSessionHandler 的注释）。
     *  @param result 回投上来的那份结果，原样传回即可
     */
    void FinishSend(const FolderResultNotice& result);

    /**
     *  接收端某一笔整体结束（界面在 UI 线程收到 FMG_MSG_V2_FOLDER_RECV_RESULT 后调用）。
     *  语义与 FinishSend 对称：停会话、发 1814，成功删记录、失败保留接收进度供续传。
     *  @param result 回投上来的那份结果，原样传回即可
     */
    void FinishRecv(const FolderResultNotice& result);

    /**
     *  整体清理：停掉全部进行中的状态机、回收全部扫描线程、清空会话表（连同清单、
     *  接收进度与保留期计时）。退出登录、被踢下线、连接断开时调用 —— 按产品定稿，
     *  重试窗口只在本次登录期间有效。
     *  <u>阻塞</u>（要等工作线程退出），故须在 UI 线程或其它非本模块工作线程上调用。
     *  重复调用安全。
     */
    void Reset();

private:
    FolderTransferManager();
    ~FolderTransferManager();

    FolderTransferManager(const FolderTransferManager&) = delete;
    FolderTransferManager& operator=(const FolderTransferManager&) = delete;

    //一笔传输的四个运行期对象。从记录里整组取出、在锁外整组停掉并按固定顺序销毁，
    //这样"阻塞方法在锁外调"与"回调适配器比状态机后销毁"两条约束各只有一处实现。
    struct FolderRuntimeHandles
    {
        std::shared_ptr<FolderSendSession>  m_sendSession;
        std::shared_ptr<FolderRecvSession>  m_recvSession;
        std::shared_ptr<FolderSendCallback> m_sendCallback;
        std::shared_ptr<FolderRecvCallback> m_recvCallback;
        std::shared_ptr<IFolderLaneTransportFactory> m_transportFactory;   //通道工厂，最后销毁

        //各指针是否都为空（这一笔当前没有在跑的状态机）。
        bool Empty() const
        {
            return !m_sendSession && !m_recvSession && !m_sendCallback && !m_recvCallback && !m_transportFactory;
        }
    };

    //一条扫描线程的登记项。线程对象与"它是否已经跑完"的标志分开存，是因为 std::thread
    //自己判断不了这件事：join 会阻塞，而回收动作发生在 UI 线程上，不能等。
    struct FolderScanThreadEntry
    {
        std::shared_ptr<std::thread> m_thread;     //线程对象；未 join 前不得析构
        std::shared_ptr<bool>        m_finished;   //由扫描线程在退出前持锁置 true
    };

    //扫描线程主体：扫目录树 → 回投扫描结果 → 发 cmd_folder_invite。
    //@param task 本次扫描的入参，<u>接管所有权</u>（本函数返回前 delete）
    void ScanProc(FolderScanTask* task);

    //扫描线程主体的实质部分，与 ScanProc 分开只为让"置完成标志"这一步不必在每条返回路径上重写。
    void ScanProcBody(const FolderScanTask& task);

    //把一个堆上负载投给界面；投不出去（尚未 SetNotifyTarget、或窗口已销毁）就地释放。
    //@param message 窗口消息号（FMG_MSG_V2_FOLDER_*）
    //@param payload 堆对象指针，接管所有权
    void PostToUi(UINT message, void* payload);

    //回收已经跑完的扫描线程（阻塞很短：这些线程已到函数末尾）。每次 StartSend 之前调一次。
    void JoinFinishedScanThreads();

    //回收全部扫描线程（阻塞，可能要等一次大目录扫描跑完）。Reset 与析构时调。
    void JoinAllScanThreads();

    //把一条记录的四个运行期对象取出并从记录里摘掉。<u>调用方须已持有 m_mtx</u>。
    //@param record 表内那条记录
    //@param out    [出参] 取出的四个对象
    //@return 取到了至少一个返回 true
    bool DetachRuntimeLocked(FolderSessionRecord& record, FolderRuntimeHandles& out);

    //停掉并销毁一组运行期对象。<u>必须在锁外调</u>：Stop 会等工作线程退出，而那些线程的
    //回调要进 m_mtx。销毁顺序固定为先状态机、后回调适配器。
    //@param handles 要收尾的一组对象（本函数返回后其中的指针全部为空）
    static void StopAndDestroyRuntime(FolderRuntimeHandles& handles);

    //按会话 id 停掉那一笔的状态机（内部自行加锁与解锁，调用方不得持有 m_mtx）。
    void StopSessionById(const std::string& sessionId);

    //停掉表里全部记录的状态机（同上）。
    void StopAllSessions();

    //向服务端发 cmd_folder_close(1814)。成功与失败共用，失败时带错误码；linkPath / md5 见 CIUSocket 的同名重载
    void SendClose(const std::string& sessionId, bool success, int errCode, const std::string& errMsg,
                   int linkPath, const std::string& md5);

    //一笔的链路实际路径与是否走上过 P2P（锁内取出工厂、锁外查询：查询会进链路槽的锁）。查不到这一笔时都为假 / 0
    void QueryLinkState(const std::string& sessionId, int& outKind, int& outLinkPath, bool& outDecidedP2P);

private:
    //供两个回调适配器把结果转投给本单例，故声明为友元。它们只调下面几个私有方法。
    friend class FolderSendCallback;
    friend class FolderRecvCallback;

    //发送端状态机回调（在其工作线程上触发）：拷贝数据后 PostMessage，绝不碰窗口。
    //sessionId 由适配器在构造时记住，故多笔并存时各自的进度不会串到别人的气泡上。
    void OnSendProgressFromWorker(const std::string& sessionId,
                                  int64_t doneBytes, int64_t totalBytes,
                                  int64_t doneFiles, int64_t totalFiles,
                                  const std::string& currentFile);
    void OnSendCompletedFromWorker(const std::string& sessionId,
                                   int64_t totalFiles, int64_t totalBytes);
    void OnSendFailedFromWorker(const std::string& sessionId,
                                int errCode, const std::string& errMsg);

    //接收端状态机回调（在其工作线程上触发）：同上。
    void OnRecvProgressFromWorker(const std::string& sessionId,
                                  int64_t doneBytes, int64_t totalBytes,
                                  int64_t doneFiles, int64_t totalFiles,
                                  const std::string& currentFile);
    void OnRecvCompletedFromWorker(const std::string& sessionId, const std::wstring& rootPath,
                                   int64_t totalFiles, int64_t totalBytes);
    void OnRecvFailedFromWorker(const std::string& sessionId,
                                int errCode, const std::string& errMsg);

private:
    mutable std::mutex m_mtx;   //保护下面全部成员；工作线程与 UI 线程都会访问

    HWND                  m_hNotifyWnd;    //回投界面用的代理窗口句柄；NULL=界面尚未就绪
    FolderSessionRegistry m_registry;      //全部传输的会话表（含并发名额闸门与等待应答队列）

    std::list<FolderScanThreadEntry> m_scanThreads;   //在跑或待回收的扫描线程，每笔发送一条
};

} // namespace foldertransfer
