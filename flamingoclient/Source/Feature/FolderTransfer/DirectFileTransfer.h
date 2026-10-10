/**
 *  DirectFileTransfer.h
 *
 *  单聊单文件 P2P 直传的纯逻辑部分（2026-10-04，docs/单聊文件P2P直传与P2P徽章方案.md）：
 *    · 发送端、接收端对邀请应答与状态推送的处置判定（继续等、改为离线文件、已拒绝、完成、失败……），
 *      聊天窗据此改文件气泡、决定要不要改走上传服务器；
 *    · 用一个文件构造传输清单（复用文件夹传输的发送会话：根目录取文件所在目录，清单里只有这一个文件）；
 *    · 接收端的落地目录（下载缓存目录下 direct-<会话号>，每次直传一个独立目录，天然不重名）与接收端扩展点
 *      DirectFileRecvHooks（文件直接落在该目录下，不套同名文件夹）。
 *
 *  直传复用文件夹传输的信令（cmd_folder_* 带 kind=1）与传输状态机，本文件不碰网络与窗口，供单元测试直接断言。
 *
 *  典型用法：
 *      //发送端收到 1810 的应答
 *      if (foldertransfer::DecideDirectSendOnInviteResponse(resp.m_code) == foldertransfer::kDirectSendToOffline)
 *          ...（改走上传服务器）
 *
 *  balloonwj@qq.com   2026-10-04
 */

#pragma once

#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>

#include "FolderTransferHooks.h"   //IFolderRecvHooks / FolderManifest / FolderRelayFileEntry

namespace foldertransfer {

//单文件直传的 P2P 用途名：客户端在 2301 里报到它，服务端只有在双方都报到了它时才允许直传（旧版客户端不报，
//发送端随即改为上传服务器）。与文件夹传输的 folder 分开，直传有自己的链路代理
const char* const kDirectFileP2PPurpose = "file";

//发送端对一次直传该怎么处置
enum DirectFileSenderAction
{
    kDirectSendWait      = 0,   //继续等：邀请已送达、或正在传输
    kDirectSendToOffline = 1,   //改走上传服务器的离线文件：服务端判定不能直传（622 等）、对方离线、邀请超时、
                                //建不起 P2P 链路，或本端点了「发送离线文件」之后服务端的确认
    kDirectSendRejected  = 2,   //对方拒绝：气泡显示「对方已拒绝」，不再上传
    kDirectSendCompleted = 3,   //直传完成：气泡显示完成态
    kDirectSendFailed    = 4,   //传输中途失败：气泡给「重试」（保留期内续传）
    kDirectSendCanceled  = 5,   //本端撤回了邀请：气泡显示已取消，不再上传
    kDirectSendBlocked   = 6,   //离线发送同样会被拒的原因（文件名含敏感词）：显示失败，不改走离线
};

//接收端对一条直传邀请该怎么处置
enum DirectFileRecvAction
{
    kDirectRecvWait          = 0,   //继续等：正在响铃或正在传输
    kDirectRecvCompleted     = 1,   //已收完并校验通过
    kDirectRecvFailed        = 2,   //传输中途失败（发送端可在保留期内续传，届时会再收到中转参数）
    kDirectRecvRejected      = 3,   //本端已拒绝
    kDirectRecvSenderCanceled = 4,  //发送端撤回了邀请：气泡显示「对方已取消」
    kDirectRecvToOffline     = 5,   //改为离线文件：对方会随后发来一条普通离线文件消息，这条直传气泡显示「已改为离线文件」
};

/**
 *  发送端收到单文件直传邀请（1810）的应答后怎么处置。
 *  @param code 应答的结果码，取值见 net/Protocol.h 的 enum error_code
 *  @return 0 时 kDirectSendWait；error_code_file_sensitive_word（文件名含敏感词，离线发送同样会被拒）时
 *          kDirectSendBlocked；其余任何失败（622 不能直传、607 对方离线、620 名额已满等）都是 kDirectSendToOffline
 */
int DecideDirectSendOnInviteResponse(int code);

/**
 *  单聊发一个文件时要不要先试在线直传。
 *  @param peerKnownOffline 对方是否确知离线
 *  @param p2pRegistered    本进程是否登记了直传的业务方（P2P 模块已加载、诊断开关没关）
 *  @param forceOffline     调用方是否要求直接按离线文件上传：转发在线直传文件时把本机文件重新发出就是这种情况
 *                          （2026-10-05 用户确认「以离线文件重新上传」，转发出去的文件要留在服务器上）
 *  @return 三项都允许（对方没被确知离线、已登记、不要求离线）时返回 true；本函数无副作用
 */
bool ShouldTryDirectFileSend(bool peerKnownOffline, bool p2pRegistered, bool forceOffline);

/**
 *  发送端收到单文件直传的状态推送（1815）后怎么处置。
 *  @param state   会话状态，取值见 enum folder_transfer_state
 *  @param errCode 失败原因，只在 state 为已失败时有意义
 *  @return 响铃中、传输中 kDirectSendWait；已拒绝 kDirectSendRejected；已撤回 kDirectSendCanceled；对方离线、超时、
 *          已改为离线发送 kDirectSendToOffline；已失败且原因是 622（建不起 P2P 链路）kDirectSendToOffline，其余失败
 *          kDirectSendFailed；已完成 kDirectSendCompleted；未知状态按 kDirectSendWait
 */
int DecideDirectSendOnState(int state, int errCode);

/**
 *  接收端收到单文件直传的状态推送（1815）后怎么处置。
 *  @param state   会话状态，取值见 enum folder_transfer_state
 *  @param errCode 失败原因，只在 state 为已失败时有意义
 *  @return 响铃中、传输中 kDirectRecvWait；已完成 kDirectRecvCompleted；已拒绝 kDirectRecvRejected；已撤回
 *          kDirectRecvSenderCanceled；对方离线、超时、已改为离线发送、以及原因为 622 的失败 kDirectRecvToOffline；
 *          其余失败 kDirectRecvFailed；未知状态按 kDirectRecvWait
 */
int DecideDirectRecvOnState(int state, int errCode);

/**
 *  单文件直传失败结束时报给服务端（1814）的失败原因。
 *  @param errCode    会话报出的失败原因（error_code_folder_*）
 *  @param decidedP2P 这一轮是否已经走上 P2P 链路（FolderP2PTransportFactory::DecidedP2P）
 *  @return 用户取消照报 error_code_folder_canceled；从没走上 P2P 链路的报 error_code_folder_direct_unavailable（服务端推给
 *          双方，发送端随即改为离线文件）；走上过 P2P 链路之后的失败照报原因（保留期内可续传）
 */
int DirectFileCloseErrCode(int errCode, bool decidedP2P);

/**
 *  用一个本地文件构造直传的清单：根目录取文件所在目录，根名取文件名，清单里只有这一个文件（相对路径就是文件名），
 *  没有子目录。发送会话按「根目录 + 相对路径」读文件，正好读到这个文件。
 *  @param filePath    文件完整路径
 *  @param outRootPath [出参] 文件所在目录（不带末尾反斜杠）
 *  @param outManifest [出参] 清单
 *  @return 文件存在且不是目录时返回 true；路径为空、不存在、是目录或取不到大小时返回 false
 */
bool BuildSingleFileManifest(const std::wstring& filePath, std::wstring& outRootPath, FolderManifest& outManifest);

/**
 *  直传在接收端的落地目录：下载缓存目录（<主目录>Users\<账号>\ChatFile\）下的 direct-<会话号>\。每次直传一个独立目录，
 *  天然不重名；传完拿到 MD5 后再在本地文件登记库里登记，历史消息照样找得到文件。
 *  @param chatFileDir 下载缓存目录，带不带末尾反斜杠都可以
 *  @param sessionId   会话号（只含数字与连字符）
 *  @return 带末尾反斜杠的目录完整路径；任一参数为空时返回空串
 */
std::wstring DirectFileSaveDir(const std::wstring& chatFileDir, const std::string& sessionId);

/**
 *  单文件直传的发送端扩展点：不做前置交换、不改清单，只记下发送会话算出的文件 MD5，传完后由管理器取出，
 *  随 cmd_folder_close 报给服务端写进文件消息（接收端历史消息据此在本地文件登记库里找到文件）。
 *  线程：OnFileMd5Computed 在数据通道工作线程上调用，Md5 在 UI 线程上调用，两者以 m_mutex 互斥。
 */
class DirectFileSendHooks : public IFolderSendHooks
{
public:
    DirectFileSendHooks();

    //以下为 IFolderSendHooks 的实现

    /**
     *  前置交换：直传没有，直接返回。
     *  @param sender   lane 0 发送器（不使用）
     *  @param manifest [入出参] 清单（不改写）
     *  @param rootPath [入出参] 本地根目录（不改写）
     *  @return 恒为 0
     */
    virtual int RunPreamble(IFolderControlSender& sender, FolderManifest& manifest, std::wstring& rootPath) override;

    /**
     *  收到非 ft_* 的控制帧：直传不认识任何自定义帧，忽略。
     *  @param data 帧字节（不使用）
     *  @param len  字节数（不使用）
     */
    virtual void OnControlFrame(const unsigned char* data, size_t len) override;

    /**
     *  会话被取消或停止：直传没有阻塞中的前置交换，什么也不做。
     */
    virtual void OnStopping() override;

    /**
     *  记下文件的 MD5（直传的清单里只有一个文件）。
     *  @param index  文件序号（不使用）
     *  @param md5Hex 32 位十六进制 MD5，记录时统一转成小写
     */
    virtual void OnFileMd5Computed(int64_t index, const std::string& md5Hex) override;

    /**
     *  取出记下的 MD5。
     *  @return 32 位小写十六进制 MD5；发送会话还没算出来时为空
     */
    std::string Md5() const;

private:
    mutable std::mutex m_mutex;   //保护 m_md5
    std::string        m_md5;     //发送会话算出的文件 MD5（小写）；还没算出来时为空
};

/**
 *  单文件直传的接收端扩展点：文件直接落在构造时给定的目录下（不按根名再建一层文件夹），校验通过后覆盖同名文件
 *  （目录是本次直传独占的，不会误伤别的文件）。其余回调什么也不做。
 *  线程：PrepareRoot 等回调在接收会话的线程上调用，本对象不持有可变状态之外的东西。
 */
class DirectFileRecvHooks : public IFolderRecvHooks
{
public:
    /**
     *  @param saveDir 落地目录（DirectFileSaveDir 的返回值）；PrepareRoot 时按需逐级创建
     */
    explicit DirectFileRecvHooks(const std::wstring& saveDir);

    //以下为 IFolderRecvHooks 的实现

    /**
     *  全部通道连好之后调用：直传没有自定义的控制帧，什么也不做。
     *  @param sender lane 0 发送器（不使用）
     *  @return 恒为 0
     */
    virtual int OnLanesReady(IFolderControlSender& sender) override;

    /**
     *  控制通道一段时间没有收到帧：直传不需要定时重发，什么也不做。
     *  @param sender lane 0 发送器（不使用）
     *  @return 恒为 0
     */
    virtual int OnControlIdle(IFolderControlSender& sender) override;

    /**
     *  收到非 ft_* 的控制帧：直传不认识任何自定义帧，忽略。
     *  @param sender lane 0 发送器（不使用）
     *  @param frame  帧内容（不使用）
     *  @return 恒为 0
     */
    virtual int OnControlFrame(IFolderControlSender& sender, const std::string& frame) override;

    /**
     *  决定落地根目录：逐级创建构造时给定的目录，文件直接落在它下面（清单里的相对路径就是文件名，不改写）。
     *  @param folderName 清单里的根名（直传时是文件名，不使用）
     *  @param dirs       [入出参] 目录的相对路径（直传时为空，不改写）
     *  @param files      [入出参] 文件条目（不改写）
     *  @param rootPath   [出参] 落地目录完整路径（不带末尾反斜杠）
     *  @return 0 继续；目录建不起来时返回 error_code_folder_mkdir_fail
     */
    virtual int PrepareRoot(const std::string& folderName, std::vector<std::string>& dirs,
                            std::vector<FolderRelayFileEntry>& files, std::wstring& rootPath) override;

    /**
     *  半成品文件刚打开：不做处理。
     *  @param partPath 半成品文件的完整路径（不使用）
     */
    virtual void OnPartFileOpened(const std::wstring& partPath) override;

    /**
     *  校验通过改正式名时覆盖同名文件：目录是本次直传独占的。
     *  @return 恒为 true
     */
    virtual bool ReplaceExistingOnFinish() const override;

    /**
     *  文件校验通过、已落成正式文件：记下落地路径与 MD5。
     *  @param finalPath 正式文件的完整路径
     *  @param md5Hex    校验通过的 MD5，记录时统一转成小写
     */
    virtual void OnFileVerified(const std::wstring& finalPath, const std::string& md5Hex) override;

    /**
     *  取出校验通过的 MD5（UI 线程在接收完成后调用）。
     *  @return 32 位小写十六进制 MD5；还没收完时为空
     */
    std::string Md5() const;

    /**
     *  取出落地文件的完整路径。
     *  @return 正式文件的完整路径；还没收完时为空
     */
    std::wstring FinalPath() const;

private:
    std::wstring       m_saveDir;     //落地目录（带或不带末尾反斜杠都可以，PrepareRoot 时统一去掉）
    mutable std::mutex m_mutex;       //保护 m_md5 与 m_finalPath：数据通道工作线程写、UI 线程读
    std::string        m_md5;         //校验通过的文件 MD5（小写）；还没收完时为空
    std::wstring       m_finalPath;   //落地文件的完整路径；还没收完时为空
};

} // namespace foldertransfer
