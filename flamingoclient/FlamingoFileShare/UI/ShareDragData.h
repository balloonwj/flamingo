/**
 *  把共享里的条目拖到资源管理器（或桌面）上：拖出任务、数据对象与拖放源，ShareDragData.h
 *
 *  做法依据 docs/文件共享方案.md 7.4 末段的实证（第 2 期第 5 步的最小样例）：
 *    - 数据对象提供 CFSTR_FILEDESCRIPTORW（每个条目一项：落地名字、是否目录、大小、修改时间）与 CFSTR_FILECONTENTS
 *      （每个文件一个 IStream），并实现 IDataObjectAsyncCapability、在交给 DoDragDrop 之前就把异步模式设为真；
 *    - 数据对象与流都聚合自由线程封送器：外壳在自己的后台线程上取数据、读流，流可以在那里等待；
 *    - 放置开始时（异步放置为 StartOperation，实测在 UI 线程上、Drop 之内；不支持异步的放置方为第一次取文件内容时）经
 *      IShareDragDelegate 请浏览窗口把条目下载到本机临时目录；流的 Read 等到对应文件下载完成再读。在 UI 线程上被调用时
 *      （放置方不支持异步）等待期间抽取消息，下载事件照常处理；
 *    - 下载失败或被取消时流返回失败，资源管理器自己提示复制失败；
 *    - 拖出任务的临时目录在最后一个引用释放时整个删掉（数据对象、流与浏览窗口都持有它）。
 *
 *  实现文件不包含 stdafx.h：IDataObjectAsyncCapability 等声明要求 _WIN32_IE >= 0x0700（见 ShareRecycleShell.h 的说明）。
 *  本头文件只用基本类型与 STL，可被任何文件包含。不打日志。
 *  单元测试见 FlamingoFileShare/Tests/ShareDragDataTests.cpp（标签 [sharedrag]，以程序方式向外壳的放置目标放置，不用鼠标）。
 *
 *  典型用法（浏览窗口开始拖动时）：
 *      std::shared_ptr<CShareDragJob> job(new CShareDragJob(tempDir, items));
 *      IDataObject* data = CreateShareDragDataObject(job, this);
 *      IDropSource* source = CreateShareDropSource();
 *      DWORD effect = 0;
 *      ::DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
 *      source->Release();
 *      data->Release();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <objidl.h>
#include <oleidl.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "../Engine/ShareDragModel.h"   //ShareDragItem

namespace fileshare {

//等待文件下载完成时每次检查的间隔，单位：毫秒
const DWORD kShareDragPollMs = 50;

/**
 *  CShareDragJob —— 一次拖出的共享状态：条目表、临时目录、下载是否已开始与结果。线程安全；不可拷贝。
 *  析构时删除整个临时目录。
 */
class CShareDragJob
{
public:
    /**
     *  @param tempDir 下载到的本机临时目录（已存在或之后由下载建出；析构时整个删掉）
     *  @param items   拖出条目表
     */
    CShareDragJob(const std::wstring& tempDir, const std::vector<ShareDragItem>& items);
    ~CShareDragJob();

    CShareDragJob(const CShareDragJob&) = delete;
    CShareDragJob& operator=(const CShareDragJob&) = delete;

    //下载到的临时目录
    const std::wstring& GetTempDir() const;

    //拖出条目表
    const std::vector<ShareDragItem>& GetItems() const;

    //第一次调用返回 true（调用方负责开始下载），之后都返回 false
    bool TryBeginDownload();

    //下载结束：code 为 0 表示成功；可在任意线程调用，只记第一次
    void MarkFinished(int code);

    //下载是否已结束、结束时的结果码（未结束时为 0）
    bool IsFinished() const;
    int GetCode() const;

    //放置方已调用 EndOperation（异步放置的复制已结束）
    void MarkOperationEnded();
    bool IsOperationEnded() const;

private:
    std::wstring               m_tempDir;      //临时目录
    std::vector<ShareDragItem> m_items;        //条目表；构造后只读
    std::atomic<bool>          m_begun;        //下载是否已开始
    std::atomic<bool>          m_marking;      //MarkFinished 是否已被调用过（只记第一次）
    std::atomic<bool>          m_finished;     //下载是否已结束（结果码写好之后才置真）
    std::atomic<int>           m_code;         //结束时的结果码
    std::atomic<bool>          m_opEnded;      //放置方是否已调用 EndOperation
};

/**
 *  拖出的委托方：开始下载。由浏览窗口实现，只在 UI 线程上被调用。
 */
class IShareDragDelegate
{
public:
    virtual ~IShareDragDelegate()
    {
    }

    /**
     *  放置开始了：把条目表里的顶层条目下载到 job 的临时目录，结束时调 job->MarkFinished。
     *  @param job 拖出任务
     *  @return 已开始下载返回 true；失败时返回 false（数据对象随即把任务标记为失败）
     */
    virtual bool OnDragDownloadStart(const std::shared_ptr<CShareDragJob>& job) = 0;
};

/**
 *  建一个拖出用的数据对象（引用计数为 1，调用方用完 Release）。异步模式已预先打开。
 *  @param job      拖出任务（数据对象与它给出的流各持有一份引用）
 *  @param delegate 委托方（不拥有）；只在建数据对象的线程（UI 线程）上、放置开始时被调用一次
 *  @return 数据对象
 */
IDataObject* CreateShareDragDataObject(const std::shared_ptr<CShareDragJob>& job, IShareDragDelegate* delegate);

/**
 *  建一个拖放源（引用计数为 1，调用方用完 Release）：按 Esc 取消、松开左键放置、光标用系统默认的。
 */
IDropSource* CreateShareDropSource();

/**
 *  以程序方式把数据对象放到本机目录上：取该目录在外壳里的放置目标，依次调用 DragEnter 与 Drop，
 *  与在资源管理器里松开鼠标走同一条路径。供单元测试与联调测试使用；须在已 OleInitialize 的线程上调用。
 *  @param data   数据对象（不转移所有权）
 *  @param folder 目标目录的本机绝对路径（须已存在）
 *  @return Drop 的结果；取放置目标失败时为对应的错误码。异步放置时返回后复制仍在进行
 */
HRESULT ShareDropOnFolder(IDataObject* data, const std::wstring& folder);

/**
 *  建一个只带文件列表（CF_HDROP）的数据对象，与从资源管理器拖出文件时放置目标收到的格式相同。供测试拖入上传使用。
 *  @param paths 本机条目的完整路径
 *  @return 数据对象（引用计数为 1，调用方用完 Release）；失败时为 NULL
 */
IDataObject* ShareCreateFileListData(const std::vector<std::wstring>& paths);

} // namespace fileshare
