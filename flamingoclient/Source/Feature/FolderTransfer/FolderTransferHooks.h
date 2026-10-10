/**
 *  文件夹传输状态机（FolderSendSession / FolderRecvSession）供上层定制的扩展点，FolderTransferHooks.h
 *
 *  单聊文件夹传输不使用这些扩展点：参数里的指针为 NULL 时，两个状态机的行为与此前完全相同。
 *  文件共享的传输子会话（docs/文件共享方案.md 5.4）复用这两个状态机，需要在三处与单聊文件夹传输不同：
 *    1. 前置交换：全部通道连好之后、交换清单之前，双方先在控制通道（lane 0）上交换文件共享自己的帧
 *       （访问者发 st_job，分享者回 st_job_ack）。这些帧的首字节不属于文件夹传输的帧类型（ft_* 取 1~9，
 *       文件共享的帧取 32 起），状态机据首字节把它们转给扩展点，不当作错误；
 *    2. 清单与落点：下载时分享者收到 st_job 才知道要发哪些条目，清单由扩展点在前置交换之后给出；
 *       清单可以没有根文件夹名（多个顶层条目直接落在目标目录下），落点与同名规则由接收端的扩展点决定；
 *    3. 半成品与覆盖：上传时分享者把半成品文件设为隐藏，完成改名时不覆盖已有文件。
 *
 *  扩展点对象由上层持有，生命周期须长于会话（至少到收到完成或失败回调、或 Stop 返回）。各方法的调用线程见注释。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

#include "FolderRelayFraming.h"   //FolderRelayFileEntry / folder_frame_type
#include "FolderManifest.h"       //FolderManifest

namespace foldertransfer {

/**
 *  判断一个应用帧的首字节是否属于文件夹传输的帧类型（ft_manifest..ft_complete）。
 *  @param firstByte 应用帧的首字节
 *  @return 属于返回 true；不属于（扩展点的自定义帧）返回 false
 */
inline bool IsFolderFrameTypeByte(unsigned char firstByte)
{
    return firstByte >= static_cast<unsigned char>(ft_manifest) && firstByte <= static_cast<unsigned char>(ft_complete);
}

/**
 *  控制通道（lane 0）的发送器：扩展点经它发自定义帧。线程安全。
 */
class IFolderControlSender
{
public:
    virtual ~IFolderControlSender()
    {
    }

    /**
     *  经 lane 0 发一个应用帧。
     *  @param frame 帧包体（首字节为自定义帧类型）
     *  @return 已发出返回 true
     */
    virtual bool SendControl(const std::string& frame) = 0;
};

/**
 *  发送端（FolderSendSession）的扩展点。
 */
class IFolderSendHooks
{
public:
    virtual ~IFolderSendHooks()
    {
    }

    /**
     *  全部通道连好之后、发清单之前，在驱动线程上调用一次，可以阻塞（会话被取消时由 OnStopping 唤醒，须尽快返回）。
     *  实现方经 sender 交换自定义帧（对端的自定义帧经 OnControlFrame 送达），并可改写本次要发送的清单与本地根目录。
     *  @param sender   lane 0 发送器，只在本调用期间有效
     *  @param manifest [入出参] 本次要发送的清单；进入时为 Start 参数里的那份
     *  @param rootPath [入出参] 本地根目录；读源文件时与清单里的相对路径拼接
     *  @return 0 继续发清单；否则为错误码，会话以它失败（并向对端发 ft_abort）
     */
    virtual int RunPreamble(IFolderControlSender& sender, FolderManifest& manifest, std::wstring& rootPath) = 0;

    /**
     *  lane 0 收到首字节不属于 ft_* 的帧。在中转接收线程上调用，只能做轻量工作。
     *  @param data 应用帧字节（借用，只在本次调用期间有效）
     *  @param len  字节数
     */
    virtual void OnControlFrame(const unsigned char* data, size_t len) = 0;

    /**
     *  会话被取消或停止（Cancel / Stop），在调用方线程上调用：阻塞中的 RunPreamble 应尽快返回。
     */
    virtual void OnStopping() = 0;

    /**
     *  算完一个文件的整文件 MD5、即将随 ft_file_begin 发给接收端（2026-10-04 起，单文件直传据此把 MD5 报给服务端
     *  写进文件消息）。在数据通道工作线程上调用，只能做轻量工作。默认什么也不做。
     *  @param index  文件在清单里的序号
     *  @param md5Hex 32 位小写十六进制 MD5
     */
    virtual void OnFileMd5Computed(int64_t index, const std::string& md5Hex)
    {
        (void)index;
        (void)md5Hex;
    }
};

/**
 *  接收端（FolderRecvSession）的扩展点。除 OnPartFileOpened 外都在会话线程（兼控制通道工作线程）上调用。
 */
class IFolderRecvHooks
{
public:
    virtual ~IFolderRecvHooks()
    {
    }

    /**
     *  全部通道连好之后、开始处理控制帧之前调用一次。
     *  @param sender lane 0 发送器（会话期间有效）
     *  @return 0 继续；否则为错误码，会话以它失败
     */
    virtual int OnLanesReady(IFolderControlSender& sender) = 0;

    /**
     *  控制通道一段时间（kFolderRecvQueueWaitMs）没有收到帧时调用，供定时重发自定义帧。
     *  @return 0 继续；否则为错误码，会话以它失败
     */
    virtual int OnControlIdle(IFolderControlSender& sender) = 0;

    /**
     *  lane 0 收到首字节不属于 ft_* 的帧。
     *  @return 0 继续；否则为错误码，会话以它失败
     */
    virtual int OnControlFrame(IFolderControlSender& sender, const std::string& frame) = 0;

    /**
     *  清单收齐之后决定落地的根目录，替代默认的「在保存位置下建同名根目录、重名自动改名」。清单里的相对路径
     *  已经过逐段的安全校验；根文件夹名可以为空（顶层条目直接落在根目录下）。调用时接收状态已按清单初始化，
     *  实现方可以在这里为能续传的文件写入进度（随后状态机按 .part 文件的实际长度核对）。
     *  实现方可以改写 dirs 与 files 里的相对路径（例如顶层条目重名时改名）：协议按文件序号对应，改写只影响本端的
     *  落地位置；改写后的路径须仍是合法的相对路径（同样经过逐段校验的写法）。
     *  @param folderName 清单里的根文件夹名，可为空
     *  @param dirs       [入出参] 目录的相对路径
     *  @param files      [入出参] 文件条目
     *  @param rootPath   [出参] 已存在的根目录完整路径
     *  @return 0 继续；否则为错误码，会话以它失败（并向对端发 ft_abort）
     */
    virtual int PrepareRoot(const std::string& folderName, std::vector<std::string>& dirs,
                            std::vector<FolderRelayFileEntry>& files, std::wstring& rootPath) = 0;

    /**
     *  半成品文件刚打开。在数据通道工作线程上调用。
     *  @param partPath 半成品文件的完整路径
     */
    virtual void OnPartFileOpened(const std::wstring& partPath) = 0;

    /**
     *  文件校验通过、改成正式名时，正式名已存在是否覆盖。单聊文件夹传输总是覆盖（根目录是新建的）。
     *  @return 覆盖返回 true；不覆盖返回 false（此时改名失败，该文件按写入失败处理）
     */
    virtual bool ReplaceExistingOnFinish() const = 0;

    /**
     *  一个文件校验通过、已从半成品改成正式名。在数据通道工作线程上调用，早于把这个文件的结果告诉发送端：
     *  实现方在这里撤销对半成品做过的处理（例如隐藏属性），发送端得知完成之后再看就已经是正式文件的样子。
     *  默认什么也不做。
     *  @param partPath  原来的半成品路径（已不存在）
     *  @param finalPath 正式文件的完整路径
     */
    virtual void OnFileFinalized(const std::wstring& partPath, const std::wstring& finalPath)
    {
        (void)partPath;
        (void)finalPath;
    }

    /**
     *  一个文件的字节数与 MD5 都校验通过、已落成正式文件（2026-10-04 起，单文件直传据此拿到落地路径与 MD5）。
     *  在数据通道工作线程上调用，紧跟在 OnFileFinalized 之后，只能做轻量工作。默认什么也不做。
     *  @param finalPath 正式文件的完整路径
     *  @param md5Hex    校验通过的 32 位小写十六进制 MD5（发送端没给 MD5 时为空）
     */
    virtual void OnFileVerified(const std::wstring& finalPath, const std::string& md5Hex)
    {
        (void)finalPath;
        (void)md5Hex;
    }
};

} // namespace foldertransfer
