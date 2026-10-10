/**
 *  文件共享弹窗通知的纯逻辑，ShareNoticeModel.h
 *
 *  第 4 期（docs/文件共享方案.md 第 14 节）：分享者收到别人操作其共享的「文件共享动态」弹窗，访问者的传输结束后收到
 *  「文件共享传输」弹窗。本文件只放不依赖窗口的部分，便于单元测试：
 *    - 两种通知的数据：ShareVisitorTransferResult（访问者一端一笔传输的结果）与 ShareOwnerActivity（访问者在分享者的
 *      共享上完成的一次操作），都由 CShareService 产生；
 *    - 是否通知的判定、每一条的标题与说明文字、操作链接；
 *    - 分享者一端的一次操作怎样从传输的条目或写操作的审计记录归纳出来；
 *    - 弹窗条目表 CShareNoticeList：新的在前，最多保留 kShareNoticeKeep 条，一次显示 kShareNoticeVisibleRows 条，
 *      其余计入底部的「还有 N 条」。
 *
 *  典型用法（CShareNoticeCenter 内部）：
 *      if (ShareNoticeModel::ShouldNotifyVisitorTransfer(result, browserForeground))
 *          m_list.Add(ShareNoticeModel::MakeVisitorEntry(result));
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <deque>
#include <string>
#include <vector>

#include "ShareProtocolCodec.h"   //ShareTransferEntry、ShareAuditEntry

namespace fileshare {

//弹窗条目表最多保留的条数：更早的丢弃（分享者「文件共享」窗口的最近操作、访问者浏览窗口的传输列表里仍有记录）
const size_t kShareNoticeKeep = 50;

//弹窗里一次显示的条数：更多的计入底部的「还有 N 条」
const size_t kShareNoticeVisibleRows = 4;

//弹窗里每一条的操作链接
enum share_notice_action
{
    share_notice_open_file   = 1,   //「打开」：用系统默认程序打开下载到本机的那一项（访问者下载成功、只有一项时）
    share_notice_open_folder = 2,   //「打开所在文件夹」：在资源管理器里打开下载到的目录并选中第一项（访问者下载成功）
    share_notice_reveal      = 3,   //「在文件夹中显示」：在资源管理器里选中分享者电脑上被上传、重命名、移动的那一项
    share_notice_view_share  = 4,   //「查看」：打开「文件共享」窗口并选中那个共享（分享者收到的下载、删除通知）
    share_notice_browse      = 5,   //「查看」：打开那个共享的浏览窗口并进入上传到的目录（访问者上传成功）
    share_notice_retry       = 6,   //「重试」：重新发起这次传输，下载从断点接着传、上传重新传（访问者传输失败）
};

//访问者一端一笔传输的结果：CShareService 在传输结束时交给结束监听（IShareTransferFinishListener）
struct ShareVisitorTransferResult
{
    uint32_t                        m_localId;        //本端传输编号（StartDownload / StartUpload 的返回值）
    int64_t                         m_shareId;        //共享 id
    int                             m_direction;      //方向，share_transfer_direction
    int                             m_code;           //0 成功，否则为错误码
    bool                            m_userCanceled;   //是否由访问者自己点「取消」结束（这种不弹窗）
    bool                            m_dragOut;        //是不是拖出到资源管理器的下载（这种不弹窗，结果看资源管理器）
    std::wstring                    m_label;          //显示的名称：一项时为它的名字，多项时为「第一项 等 N 项」
    std::string                     m_shareName;      //共享名称（UTF-8）；还不知道时为空
    std::wstring                    m_targetDir;      //下载：本机目标目录；上传为空
    std::wstring                    m_localTopPath;   //下载成功：第一个顶层条目落地后的本机完整路径（同名时已改名）；其余为空
    size_t                          m_topCount;       //顶层条目数
    std::string                     m_toDir;          //上传：共享内的目标目录（相对共享根，根为空串）；下载为空
    std::vector<ShareTransferEntry> m_downloadItems;  //下载：请求的条目（「重试」时原样再请求一次）
    std::vector<std::wstring>       m_uploadItems;    //上传：本机条目的完整路径（「重试」时重新上传）
    int64_t                         m_timeMs;         //结束时间（Unix 毫秒）

    ShareVisitorTransferResult()
        : m_localId(0)
        , m_shareId(0)
        , m_direction(0)
        , m_code(0)
        , m_userCanceled(false)
        , m_dragOut(false)
        , m_topCount(0)
        , m_timeMs(0)
    {
    }
};

//访问者在分享者的共享上完成的一次操作：CShareService 在分享者一端产生，只含成功的部分
struct ShareOwnerActivity
{
    int64_t      m_shareId;        //共享 id
    int64_t      m_visitorId;      //访问者 userid
    std::string  m_visitorName;    //访问者显示名的后备（服务端在 2222 里给的，UTF-8）；宿主取得到通讯录里的名字时不用它
    int          m_action;         //动作，share_audit_action 的 4（下载）、5（上传）、7（重命名）、8（删除）、9（移动）
    std::string  m_firstName;      //第一项的名字（UTF-8，不含目录）
    size_t       m_count;          //项数：下载、上传为顶层条目数，删除、移动为成功的条目数，重命名为 1
    std::string  m_targetName;     //重命名的新名字，或移动的目标目录（相对共享根，根为空串）；其余为空
    std::string  m_revealRelPath;  //要在资源管理器里选中的那一项（相对共享根）：上传、重命名、移动才有
    std::wstring m_revealPath;     //m_revealRelPath 在分享者电脑上的完整路径，由 CShareService 按本机记录拼出；拼不出时为空
    std::string  m_shareName;      //共享名称（UTF-8）；不知道时为空
    int64_t      m_timeMs;         //完成时间（Unix 毫秒）

    ShareOwnerActivity()
        : m_shareId(0)
        , m_visitorId(0)
        , m_action(0)
        , m_count(0)
        , m_timeMs(0)
    {
    }
};

//弹窗里一条的一个操作链接
struct ShareNoticeLink
{
    int          m_action;   //动作，share_notice_action
    std::wstring m_text;     //链接文字

    ShareNoticeLink()
        : m_action(0)
    {
    }
};

//弹窗里的一条
struct ShareNoticeEntry
{
    uint32_t                     m_id;       //条目编号：CShareNoticeList::Add 分配，从 1 起递增；Add 之前为 0
    std::wstring                 m_title;    //第一行，如「张三 删除了 a.docx 等 20 项」「下载完成：报告.docx」
    std::wstring                 m_time;     //第一行右端的时间（时:分）
    std::wstring                 m_detail;   //第二行：共享名称、保存位置或失败原因
    bool                         m_failed;   //第二行是否用红字（访问者的传输失败）
    bool                         m_detailIsPath;   //第二行是否为本机路径（访问者下载成功的「已保存到 …」）：放不下时省略
                                                   //路径中间、保留开头与最后的文件夹名；其余条目省略末尾
    std::vector<ShareNoticeLink> m_links;    //第二行右端的操作链接，按显示顺序

    ShareNoticeEntry()
        : m_id(0)
        , m_failed(false)
        , m_detailIsPath(false)
    {
    }
};

/**
 *  ShareNoticeModel —— 两种通知的判定与文字（全部为静态纯函数，只读参数，不访问窗口与文件系统）。
 */
class ShareNoticeModel
{
public:
    /**
     *  访问者一笔传输结束后要不要弹窗：自己点「取消」的、拖出到资源管理器的、浏览窗口正在前台的都不弹，其余（成功与失败）都弹。
     *  @param result            传输结果
     *  @param browserForeground 这个共享的浏览窗口此刻是否在前台（存在、可见、没有最小化，且是前台窗口或其所有者）
     *  @return 要弹返回 true
     */
    static bool ShouldNotifyVisitorTransfer(const ShareVisitorTransferResult& result, bool browserForeground);

    /**
     *  分享者一端的哪些动作要通知：下载、上传、重命名、删除、移动；新建文件夹与服务端记录的动作不通知。
     *  @param action share_audit_action
     *  @return 要通知返回 true
     */
    static bool IsOwnerActionNotified(int action);

    /**
     *  访问者一笔传输的弹窗条目：标题为「下载完成：名称」一类，说明为保存位置、上传到的位置或失败原因；
     *  操作链接按结果给出（见 share_notice_action）。
     *  @param result 传输结果
     *  @return 条目（m_id 为 0，由 CShareNoticeList::Add 分配）
     */
    static ShareNoticeEntry MakeVisitorEntry(const ShareVisitorTransferResult& result);

    /**
     *  分享者一次操作的弹窗条目：标题为「张三 删除了 a.docx 等 20 项」一类，说明为共享名称；上传、重命名、移动的链接为
     *  「在文件夹中显示」（拼不出本机路径时改为「查看」），下载、删除为「查看」。
     *  @param activity    操作
     *  @param visitorName 访问者的显示名（宿主取的通讯录名字；取不到时调用方传 m_visitorName 转成的文字）
     *  @return 条目（m_id 为 0）
     */
    static ShareNoticeEntry MakeOwnerEntry(const ShareOwnerActivity& activity, const std::wstring& visitorName);

    /**
     *  分享者一次操作的标题文字。
     *  @param who    访问者的显示名
     *  @param action share_audit_action
     *  @param first  第一项的名字
     *  @param count  项数
     *  @param target 重命名的新名字，或移动的目标目录（相对共享根，根为空串）
     *  @return 标题；不通知的动作返回空串
     */
    static std::wstring OwnerTitle(const std::wstring& who, int action, const std::wstring& first, size_t count,
                                   const std::wstring& target);

    /**
     *  访问者的传输失败时显示的原因。浏览窗口的错误文字表（ShareBrowseModel::ErrorText）之外补上传输特有的几种：
     *  同名、磁盘空间不足、同时传输的人数已满、传输被中止。
     *  @param code 错误码（非 0）
     *  @return 原因文字
     */
    static std::wstring FailureReason(int code);

    /**
     *  弹窗里的时间：本地时间的「时:分」。
     *  @param unixMs Unix 毫秒；不大于 0 时返回空串
     *  @return 如「09:05」
     */
    static std::wstring FormatClock(int64_t unixMs);

    /**
     *  共享内一个位置的说法：共享名称加上各级目录，如「「项目资料 / 设计 / 图标」」；共享名称不知道时只写目录，
     *  两者都没有时为「共享的根目录」。
     *  @param shareName 共享名称（UTF-8），可为空
     *  @param dir       相对共享根的目录（UTF-8，'/' 分隔），根为空串
     *  @return 位置文字
     */
    static std::wstring LocationText(const std::string& shareName, const std::string& dir);

    /**
     *  弹窗底部的「还有 N 条」。
     *  @param count 没显示出来的条数，大于 0
     *  @return 文字
     */
    static std::wstring OverflowText(size_t count);

    /**
     *  从一次写操作请求的审计记录归纳出分享者的一次操作：只计成功的条目；重命名、删除、移动才算，新建文件夹与全部失败的
     *  请求不算。不填访问者名字、本机路径与共享名称（由 CShareService 补上）。
     *  @param audits 一次请求的全部审计记录（同一访问者、同一动作）
     *  @param out    [出参] 操作
     *  @return 要通知返回 true
     */
    static bool MakeWriteActivity(const std::vector<ShareAuditEntry>& audits, ShareOwnerActivity& out);

    /**
     *  从分享者一端一次成功传输的条目归纳出一次操作。不填共享、访问者、本机路径与时间（由 CShareService 补上）。
     *  @param direction share_transfer_download 或 share_transfer_upload
     *  @param paths     下载：访问者请求的条目（相对共享根，单文件共享为一个空串）；上传：顶层条目的名字
     *  @param toDir     上传：共享内的目标目录（相对共享根，根为空串）；下载不用
     *  @param out       [出参] 操作
     *  @return paths 为空或方向不对时返回 false
     */
    static bool MakeTransferActivity(int direction, const std::vector<std::string>& paths, const std::string& toDir,
                                     ShareOwnerActivity& out);

    /**
     *  相对路径的最后一段（UTF-8）。
     *  @param path 相对路径，'/' 分隔
     *  @return 最后一段；路径为空时返回空串
     */
    static std::string LastSegment(const std::string& path);
};

/**
 *  CShareNoticeList —— 一个弹窗里的条目表：新的在前，最多保留 kShareNoticeKeep 条，超出时丢弃最早的。只在 UI 线程使用。
 */
class CShareNoticeList
{
public:
    CShareNoticeList();

    /**
     *  加一条到最前面，并分配编号。超出 kShareNoticeKeep 条时丢弃最早的。
     *  @param entry 条目（m_id 被忽略）
     *  @return 分配的编号（大于 0）
     */
    uint32_t Add(const ShareNoticeEntry& entry);

    /**
     *  删掉一条。
     *  @param id 编号
     *  @return 有这一条并已删除返回 true
     */
    bool Remove(uint32_t id);

    //清空全部条目（编号继续递增，不会与已删除的重复）
    void Clear();

    //条目数
    size_t Size() const;

    /**
     *  弹窗里显示的条目：最新的 kShareNoticeVisibleRows 条，新的在前。
     *  @return 条目的副本
     */
    std::vector<ShareNoticeEntry> Visible() const;

    //没有显示出来的条数（底部「还有 N 条」的 N）
    size_t OverflowCount() const;

    /**
     *  按编号找一条。
     *  @param id 编号
     *  @return 条目（归本对象所有，下一次修改之前有效）；没有时返回 NULL
     */
    const ShareNoticeEntry* Find(uint32_t id) const;

private:
    std::deque<ShareNoticeEntry> m_entries;   //条目，新的在前
    uint32_t                     m_nextId;    //下一个编号，从 1 起递增
};

} // namespace fileshare
