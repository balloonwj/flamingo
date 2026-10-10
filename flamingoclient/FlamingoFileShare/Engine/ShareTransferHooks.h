/**
 *  传输子会话四种角色的扩展点实现，ShareTransferHooks.h
 *
 *  传输子会话复用文件夹传输的发送端与接收端状态机（docs/文件共享方案.md 5.4），差别都放在本文件的扩展点里
 *  （接口见 Source/Feature/FolderTransfer/FolderTransferHooks.h）：
 *
 *  | 类                      | 谁、哪个方向          | 状态机       | 做什么 |
 *  | CVisitorDownloadHooks   | 访问者，下载          | 接收端       | 连好后发 st_job，收到 st_job_ack 之前每 kShareJobResendMs 重发；落点为用户选的目录，顶层同名自动改名，
 *  |                         |                       |              | 有续传记录的条目沿用原名并按记录与 .part 文件续传 |
 *  | CVisitorUploadHooks     | 访问者，上传          | 发送端       | 发清单之前发 st_job 并等 st_job_ack（同样定时重发） |
 *  | COwnerDownloadHooks     | 分享者，下载          | 发送端       | 等 st_job，按其中的条目校验路径、生成清单，回 st_job_ack |
 *  | COwnerUploadHooks       | 分享者，上传          | 接收端       | 等 st_job，校验上传权限与目标目录，回 st_job_ack；顶层同名拒绝（807）、半成品设隐藏、改名不覆盖 |
 *
 *  st_job 总是由访问者先发：fileserver 在中转两端都报到之前会丢弃数据帧，访问者定时重发直到收到应答，
 *  分享者只处理第一个 st_job，之后重复到达的按第一次的结果再应答一次。
 *
 *  各方法的调用线程见 FolderTransferHooks.h；本文件的类内部用互斥量保护跨线程读写的成员。
 *  单元测试见 FlamingoFileShare/Tests/ShareTransferHooksTests.cpp（标签 [sharehooks]）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ShareLocalStore.h"
#include "SharePermission.h"
#include "../../Source/Feature/FolderTransfer/FolderRecvSession.h"   //FolderReceiveState
#include "../../Source/Feature/FolderTransfer/FolderTransferHooks.h"

namespace fileshare {

//访问者在收到 st_job_ack 之前重发 st_job 的间隔，单位：毫秒（与 sf_hello 的重发间隔一致）
const int kShareJobResendMs = 300;

//等对端 st_job 或 st_job_ack 的最长时间，单位：毫秒。超时按「分享者暂时无法响应」处理
const int kShareJobWaitMs = 15000;

/**
 *  访问者一端、下载：文件夹传输接收端的扩展点。
 */
class CVisitorDownloadHooks : public foldertransfer::IFolderRecvHooks
{
public:
    /**
     *  @param shareId   共享 id
     *  @param items     要下载的条目（相对共享根，同一目录下）
     *  @param targetDir 本机目标目录（须已存在）
     *  @param store     本机库（不拥有，可为 NULL：不续传、不查记录）；只在 PrepareRoot 里读写
     *  @param state     本次传输的接收状态（与会话共用），用于写入续传进度
     */
    CVisitorDownloadHooks(int64_t shareId, const std::vector<std::string>& items, const std::wstring& targetDir,
                          CShareLocalStore* store, const std::shared_ptr<foldertransfer::FolderReceiveState>& state);

    //IFolderRecvHooks
    virtual int OnLanesReady(foldertransfer::IFolderControlSender& sender) override;
    virtual int OnControlIdle(foldertransfer::IFolderControlSender& sender) override;
    virtual int OnControlFrame(foldertransfer::IFolderControlSender& sender, const std::string& frame) override;
    virtual int PrepareRoot(const std::string& folderName, std::vector<std::string>& dirs,
                            std::vector<foldertransfer::FolderRelayFileEntry>& files, std::wstring& rootPath) override;
    virtual void OnPartFileOpened(const std::wstring& partPath) override;
    virtual bool ReplaceExistingOnFinish() const override;

    /**
     *  传输结束后（UI 线程）取各文件的信息，用于保存或删除续传记录。PrepareRoot 之前调用时为空。
     *  @param index 文件序号
     */
    size_t GetFileCount() const;
    const std::string& GetShareRelPath(size_t index) const;   //文件在共享内的相对路径（续传记录的键）
    const std::wstring& GetLocalPath(size_t index) const;     //本机落地的正式路径
    int64_t GetFileSize(size_t index) const;                  //字节数
    int64_t GetFileMtime(size_t index) const;                 //源文件修改时间（Unix 毫秒）

    //本机落地后的顶层名字（改名之后的），用于界面显示
    const std::vector<std::wstring>& GetLocalTopNames() const;

private:
    //发一次 st_job 并记下时间
    bool SendJob(foldertransfer::IFolderControlSender& sender);

    //按续传记录给一个文件写入进度；记录与 .part 文件对不上时什么也不做
    void SeedResume(size_t index);

    int64_t                                              m_shareId;     //共享 id
    std::vector<std::string>                             m_items;       //要下载的条目
    std::string                                          m_itemParent;  //条目所在的目录（相对共享根）
    std::wstring                                         m_targetDir;   //本机目标目录
    CShareLocalStore*                                    m_store;       //本机库（不拥有，可为 NULL）
    std::shared_ptr<foldertransfer::FolderReceiveState>  m_state;       //接收状态
    int64_t                                              m_startMs;     //开始等 st_job_ack 的时间
    int64_t                                              m_lastSendMs;  //最近一次发 st_job 的时间
    bool                                                 m_acked;       //是否已收到 st_job_ack

    //PrepareRoot 写入、传输结束后读取（两者之间有事件队列的互斥量保证先后）
    std::vector<std::string>                             m_shareRelPaths;   //各文件在共享内的相对路径
    std::vector<std::wstring>                            m_localPaths;      //各文件本机的正式路径
    std::vector<int64_t>                                 m_sizes;           //各文件字节数
    std::vector<int64_t>                                 m_mtimes;          //各文件源修改时间
    std::vector<std::wstring>                            m_localTopNames;   //改名之后的顶层名字
};

/**
 *  访问者一端、上传：文件夹传输发送端的扩展点。
 */
class CVisitorUploadHooks : public foldertransfer::IFolderSendHooks
{
public:
    /**
     *  @param topNames 上传的顶层条目名（写进 st_job 的 items）
     *  @param toDir    共享内的目标目录（相对共享根）
     *  @param waitMs   等 st_job_ack 的最长时间（毫秒）；单元测试用小值
     */
    CVisitorUploadHooks(const std::vector<std::string>& topNames, const std::string& toDir, int waitMs = kShareJobWaitMs);

    //IFolderSendHooks
    virtual int RunPreamble(foldertransfer::IFolderControlSender& sender, foldertransfer::FolderManifest& manifest,
                            std::wstring& rootPath) override;
    virtual void OnControlFrame(const unsigned char* data, size_t len) override;
    virtual void OnStopping() override;

private:
    std::vector<std::string> m_topNames;   //顶层条目名
    std::string              m_toDir;      //目标目录
    int                      m_waitMs;     //等应答的最长时间

    std::mutex               m_mutex;      //保护下面三项
    std::condition_variable  m_cv;         //收到应答或被停止时通知
    bool                     m_acked;      //是否已收到 st_job_ack
    int                      m_ackCode;    //应答的结果码
    bool                     m_stopping;   //会话是否已被取消或停止
};

/**
 *  分享者一端、下载：文件夹传输发送端的扩展点。
 */
class COwnerDownloadHooks : public foldertransfer::IFolderSendHooks
{
public:
    /**
     *  @param share  本机的共享记录（拷贝保存）
     *  @param waitMs 等 st_job 的最长时间（毫秒）；单元测试用小值
     */
    COwnerDownloadHooks(const ShareLocalRecord& share, int waitMs = kShareJobWaitMs);

    //IFolderSendHooks
    virtual int RunPreamble(foldertransfer::IFolderControlSender& sender, foldertransfer::FolderManifest& manifest,
                            std::wstring& rootPath) override;
    virtual void OnControlFrame(const unsigned char* data, size_t len) override;
    virtual void OnStopping() override;

    //st_job 里的条目（传输结束后用于审计上报）；收到 st_job 之前为空
    std::vector<std::string> GetItems() const;

    //条目所在的目录（相对共享根）
    std::string GetItemParent() const;

    /**
     *  本次发送的文件（共享内的相对路径与字节数，下标即文件序号）；前置交换成功之前为空。
     */
    void GetFiles(std::vector<std::string>& paths, std::vector<int64_t>& sizes) const;

private:
    ShareLocalRecord                         m_share;      //共享记录
    int                                      m_waitMs;     //等 st_job 的最长时间

    mutable std::mutex                       m_mutex;      //保护下面各项
    std::condition_variable                  m_cv;         //收到 st_job 或被停止时通知
    bool                                     m_jobReceived;//是否已收到 st_job
    int                                      m_jobDirection;//st_job 的方向
    std::vector<std::string>                 m_items;      //st_job 的条目
    std::vector<std::string>                 m_filePaths;  //清单里各文件在共享内的相对路径（审计用）
    std::vector<int64_t>                     m_fileSizes;  //清单里各文件的字节数
    bool                                     m_answered;   //是否已经应答过
    int                                      m_answerCode; //应答的结果码
    foldertransfer::IFolderControlSender*    m_sender;     //RunPreamble 期间及之后可用的发送器（会话的成员，生命周期同会话）
    bool                                     m_stopping;   //会话是否已被取消或停止
};

/**
 *  分享者一端、上传：文件夹传输接收端的扩展点。
 */
class COwnerUploadHooks : public foldertransfer::IFolderRecvHooks
{
public:
    /**
     *  @param share  本机的共享记录（拷贝保存）
     *  @param perms  访问者此刻的写权限位（来自 2222）
     *  @param waitMs 等 st_job 的最长时间（毫秒）
     */
    COwnerUploadHooks(const ShareLocalRecord& share, int perms, int waitMs = kShareJobWaitMs);

    //IFolderRecvHooks
    virtual int OnLanesReady(foldertransfer::IFolderControlSender& sender) override;
    virtual int OnControlIdle(foldertransfer::IFolderControlSender& sender) override;
    virtual int OnControlFrame(foldertransfer::IFolderControlSender& sender, const std::string& frame) override;
    virtual int PrepareRoot(const std::string& folderName, std::vector<std::string>& dirs,
                            std::vector<foldertransfer::FolderRelayFileEntry>& files, std::wstring& rootPath) override;
    virtual void OnPartFileOpened(const std::wstring& partPath) override;
    virtual bool ReplaceExistingOnFinish() const override;

    //IFolderRecvHooks：文件已改成正式名，去掉半成品时设的隐藏属性并从正在上传的登记表里注销
    virtual void OnFileFinalized(const std::wstring& partPath, const std::wstring& finalPath) override;

    /**
     *  会话中途权限变化（UI 线程）：之后的检查按新权限进行。
     */
    void SetPerms(int perms);

    //已打开过的半成品文件（传输结束后清理与注销用）
    std::vector<std::wstring> GetPartFiles() const;

    //st_job 的目标目录（相对共享根）与本机的目标目录；收到 st_job 之前为空
    std::string GetToDir() const;
    std::wstring GetTargetDir() const;

    //清单的顶层名字（审计上报用）；PrepareRoot 之前为空
    std::vector<std::string> GetTopNames() const;

    /**
     *  本次接收的文件（共享内的相对路径与字节数，下标即文件序号）；PrepareRoot 之前为空。
     */
    void GetFiles(std::vector<std::string>& paths, std::vector<int64_t>& sizes) const;

private:
    ShareLocalRecord          m_share;        //共享记录
    SharePermission           m_permission;   //共享类型与访问者的写权限位
    int                       m_waitMs;       //等 st_job 的最长时间
    int64_t                   m_startMs;      //开始等 st_job 的时间

    mutable std::mutex        m_mutex;        //保护下面各项（OnPartFileOpened 在数据通道线程上调用）
    bool                      m_answered;     //是否已经应答过 st_job
    int                       m_answerCode;   //应答的结果码
    std::string               m_toDir;        //目标目录（相对共享根）
    std::wstring              m_targetDir;    //目标目录的本机路径
    std::vector<std::string>  m_topNames;     //清单的顶层名字
    std::vector<std::string>  m_filePaths;    //各文件在共享内的相对路径（审计用）
    std::vector<int64_t>      m_fileSizes;    //各文件的字节数
    std::vector<std::wstring> m_partFiles;    //已打开过的半成品文件
};

/**
 *  编码一个 st_job 帧。
 *  @return 成功返回 true
 */
bool BuildJobFrame(int direction, const std::vector<std::string>& items, const std::string& toDir, std::string& frame);

/**
 *  编码一个 st_job_ack 帧。
 */
bool BuildJobAckFrame(int code, std::string& frame);

} // namespace fileshare
