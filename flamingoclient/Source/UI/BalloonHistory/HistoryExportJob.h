/**
 *  聊天记录导出增强：导出选项、导出任务与它的执行，HistoryExportJob.h
 *
 *  聊天记录管理器点「↓ 导出」后弹出导出选项（docs/聊天记录管理器方案.md §12.7）：范围（当前会话 / 选择多个会话）、
 *  时间（全部 / 近 7 天 / 近 30 天 / 近 3 个月 / 近半年 / 近一年 / 按当前筛选条件）、格式（网页 / 纯文本）、附件
 *  （包含图片、包含本机已有的文件）、是否打包为 zip。窗口在界面线程上查库、解析名字，把每个会话整理成一份
 *  HistoryExportConvJob（网页或文本的全文 + 要带上的附件），合成 HistoryExportJob 后交给后台线程执行
 *  RunHistoryExportJob：写文件、复制附件，或写进 zip（CHistoryZipWriter）。
 *
 *  输出的布局：
 *    · 单个会话、不打包：与原来相同，文档写到用户选的路径，附件放在同目录的「<文档名>_files」文件夹；
 *    · 多个会话、不打包：用户选一个文件夹，每个会话一个子文件夹「<会话名>/」，里面是文档与「<文档名>_files」；
 *    · 打包：zip 里每个会话一个目录「<会话名>/」，结构同上。
 *
 *  执行部分只碰磁盘、不碰窗口与本地库，可以单测（Tests/HistoryExportJobTests.cpp，标签 [histexport]）。
 *  在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <vector>
#include "V2MsgQuery.h"   // HistoryConversationKey

namespace balloonhistory {

//导出哪些会话
enum HistoryExportScope
{
    kHistoryExportScopeCurrent  = 0,   //当前会话（右侧正在看的那一个）
    kHistoryExportScopeSelected = 1,   //在导出浮层里勾选的多个会话
};

//导出的时间范围：0～5 与 HistoryTimeRange 的前六项（全部 / 近 7 天 / 近 30 天 / 近 3 个月 / 近半年 / 近一年）一一对应，
//这一项表示「按当前筛选条件」（类型、关键字、发送人、时间都与右侧列表相同；只对当前会话有意义）
const int kHistoryExportRangeFilter = 6;
//时间下拉的项数
const int kHistoryExportRangeCount = 7;

//导出选项
struct HistoryExportOptions
{
    int                                 m_scope;          //取值见 HistoryExportScope
    std::vector<HistoryConversationKey> m_conversations;  //勾选的会话（m_scope 为选择多个会话时）
    int                                 m_range;          //时间范围，见 kHistoryExportRangeFilter 上方的说明
    bool                                m_html;           //true 网页，false 纯文本
    bool                                m_includeImages;  //是否带上本机已有的图片原图
    bool                                m_includeFiles;   //是否带上本机已有的文件
    bool                                m_zip;            //是否打包为一个 zip

    HistoryExportOptions()
        : m_scope(kHistoryExportScopeCurrent)
        , m_range(kHistoryExportRangeFilter)
        , m_html(true)
        , m_includeImages(true)
        , m_includeFiles(false)
        , m_zip(false)
    {
    }
};

//一个附件：本机的源文件，以及它在会话文件夹里的相对路径（用 '/' 分隔，如「聊天记录_files/123.png」）
struct HistoryExportAttachment
{
    std::wstring m_source;    //源文件完整路径
    std::wstring m_relPath;   //相对路径
};

//一个会话要导出的内容
struct HistoryExportConvJob
{
    std::wstring                         m_folderName;    //会话文件夹名（多个会话或打包时用；已整理、不重名）
    std::wstring                         m_docName;       //文档文件名（如「聊天记录.html」）
    std::wstring                         m_docText;       //文档全文（网页或纯文本）
    std::vector<HistoryExportAttachment> m_attachments;   //要带上的附件
    int                                  m_messageCount;  //导出的消息条数（结果提示用）

    HistoryExportConvJob()
        : m_messageCount(0)
    {
    }
};

//一次导出任务
struct HistoryExportJob
{
    //输出方式：true 打包为 zip（m_target 是 zip 文件路径）；false 时看 m_singleFile
    bool                              m_zip;
    //不打包时：true 表示单个会话，m_target 是文档的完整路径，附件放在同目录；false 表示 m_target 是文件夹，
    //每个会话一个子文件夹
    bool                              m_singleFile;
    std::wstring                      m_target;   //输出路径，含义见上
    std::vector<HistoryExportConvJob> m_convs;    //各会话的内容

    HistoryExportJob()
        : m_zip(false)
        , m_singleFile(true)
    {
    }
};

//导出的结果
struct HistoryExportResult
{
    bool         m_ok;                 //文档都写成功了（附件个别失败不影响）
    bool         m_tooLarge;           //打包时超过了 zip 不写 ZIP64 的上限（4 GB）
    bool         m_cancelled;          //执行途中被取消（窗口关闭）
    int          m_convs;              //写成的会话个数
    int          m_messages;           //导出的消息条数合计
    int          m_attachmentsDone;    //带上的附件个数
    int          m_attachmentsFailed;  //没带上的附件个数（源文件读不了、复制失败）
    std::wstring m_target;             //输出路径（与任务相同，提示与「打开所在文件夹」用）

    HistoryExportResult()
        : m_ok(false)
        , m_tooLarge(false)
        , m_cancelled(false)
        , m_convs(0)
        , m_messages(0)
        , m_attachmentsDone(0)
        , m_attachmentsFailed(0)
    {
    }
};

/**
 *  IHistoryExportProgress —— 执行导出时的进度回调。在执行导出的那个线程上调用，实现方负责线程安全
 *  （窗口的实现只做 PostMessage 与读原子变量）。
 */
class IHistoryExportProgress
{
public:
    virtual ~IHistoryExportProgress()
    {
    }

    /**
     *  完成了一步（写完一个文档或带上一个附件）。
     *  @param done   已完成的步数
     *  @param total  总步数（文档数 + 附件数）
     */
    virtual void OnExportProgress(int done, int total) = 0;

    //是否要求中止（窗口关闭时）。返回 true 后执行函数尽快结束，已写的部分不再清理
    virtual bool ExportCancelled() = 0;
};

/**
 *  把时间下拉的下标换成导出用的时间范围：0～5 原样，其余（含「按当前筛选条件」）返回 -1 表示沿用右侧的筛选。
 *  @param range  导出选项里的 m_range
 *  @return HistoryTimeRange 的取值（0～5），或 -1
 */
int ExportRangeToTimeRange(int range);

/**
 *  按「<名字>」生成会话文件夹名：整理掉 Windows 不允许的字符（SanitizeExportFileName），与已用过的重名时
 *  加「 (2)」……（比较不分大小写）。
 *  @param name  会话名
 *  @param used  [入出参] 已用过的文件夹名（小写）
 *  @return 文件夹名
 */
std::wstring ExportFolderName(const std::wstring& name, std::set<std::wstring>& used);

/**
 *  执行导出任务（写文件、复制附件或写 zip）。可以在后台线程上调用。
 *  @param job       导出任务
 *  @param progress  进度回调（可为 NULL）
 *  @return 结果
 */
HistoryExportResult RunHistoryExportJob(const HistoryExportJob& job, IHistoryExportProgress* progress);

} // namespace balloonhistory
