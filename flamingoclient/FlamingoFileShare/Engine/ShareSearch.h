/**
 *  文件共享分享者一端的按文件名搜索，ShareSearch.h
 *
 *  访问者在浏览窗口里搜索时发 sf_search（docs/文件共享方案.md 5.3，第 2 期第 3 步），分享者在指定目录之下逐层遍历，
 *  名字里含有关键字（不区分大小写）的文件与文件夹都算命中，命中按批交给调用方发 sf_search_result：
 *    - 广度优先，先近后远；不进入重解析点（目录联接、符号链接可能指向共享根之外或形成环），重解析点本身也不报告；
 *    - 共享未含隐藏文件时，带隐藏或系统属性的条目不报告、也不进入；正在上传的半成品不报告；名字写不成合法相对路径的跳过；
 *    - 两个上限由服务端经 2222 下发（sharesearchmaxresults、sharesearchtimeout）：命中条数到上限、或耗时到上限时停止，
 *      最后一批标明 truncated；调用方可随时经取消标志中止（sf_cancel、会话结束）；
 *    - 命中攒够 kShareSearchFlushHits 条、或距上一批超过 kShareSearchFlushIntervalMs 就先交出一批，访问者边搜边看到结果；
 *      无论怎样结束（取消除外）最后都交出一批 done 为真的结果，访问者据此知道搜索已结束。
 *
 *  本模块只碰文件系统、不打日志、不发网络，可在任意线程调用（分享者在工作线程上执行）。
 *  单元测试见 FlamingoFileShare/Tests/ShareSearchTests.cpp（标签 [sharesearch]，在测试程序目录下建真实的文件与目录）。
 *
 *  典型用法（分享者的搜索任务）：
 *      ShareSearchParams p;
 *      ...
 *      const int outcome = ShareSearch::Run(p, &cancelFlag, &sink);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <string>
#include <vector>

#include "ShareControlFraming.h"   //ShareEntry

namespace fileshare {

//2222 没有带搜索上限时的默认值：命中条数、耗时（毫秒），与服务端配置项的默认值一致
const int kShareSearchDefaultMaxResults = 500;
const int kShareSearchDefaultTimeoutMs = 10000;

//命中攒够这么多条就先交出一批
const size_t kShareSearchFlushHits = 100;

//距上一批超过这么久（毫秒）就先交出已有的命中
const int kShareSearchFlushIntervalMs = 500;

//关键字的最大字符数（UTF-16 字符）
const size_t kShareSearchKeywordMaxChars = 255;

//每检查这么多个条目看一次取消标志与耗时（逐个看取时间开销太大）
const int kShareSearchCheckEvery = 64;

//拖出目录时整棵目录树的条目上限（方案 7.4 的 kShareDragOutMaxItems）。分享者按它加一列出，多出来的那一条说明超过了上限
const int kShareTreeMaxItems = 5000;

//一次搜索怎样结束，ShareSearch::Run 的返回值
enum share_search_outcome
{
    share_search_finished          = 0,   //遍历完了起点目录之下的全部条目
    share_search_truncated_count   = 1,   //命中条数到了上限，提前结束
    share_search_truncated_timeout = 2,   //耗时到了上限，提前结束
    share_search_canceled          = 3,   //取消标志被置位，中止（不再交出 done 的那一批）
    share_search_stopped           = 4,   //接收方不再需要结果（例如通道已断开），中止
};

//一次搜索的参数
struct ShareSearchParams
{
    std::wstring m_startAbs;       //起点目录的本机绝对路径（调用方已按 9.1 校验过）
    std::string  m_startRel;       //起点目录相对共享根的路径（UTF-8，'/' 分隔；共享根为空串），命中的相对路径以它开头
    std::wstring m_keyword;        //关键字（非空，已按 CheckKeyword 检查）
    bool         m_includeHidden;  //共享是否包含隐藏文件与系统文件
    int          m_maxResults;     //命中条数上限，须 > 0
    int          m_timeoutMs;      //耗时上限（毫秒），须 >= 0；0 表示检查第一批条目后即按超时结束（测试用）
    bool         m_matchAll;       //为真时每个条目都算命中（列整棵目录树），关键字不用

    ShareSearchParams()
        : m_includeHidden(false)
        , m_maxResults(kShareSearchDefaultMaxResults)
        , m_timeoutMs(kShareSearchDefaultTimeoutMs)
        , m_matchAll(false)
    {
    }
};

/**
 *  命中结果的接收方，由搜索任务实现（编帧后经控制通道发出）。在执行搜索的线程上回调。
 */
class IShareSearchSink
{
public:
    virtual ~IShareSearchSink()
    {
    }

    /**
     *  交出一批命中。
     *  @param hits      这一批命中：m_name 为相对共享根的路径，带目录标志、大小与修改时间
     *  @param done      是不是最后一批
     *  @param truncated 最后一批时：是否因条数或耗时上限而提前结束
     *  @return 继续搜索返回 true；返回 false 时搜索立即中止（share_search_stopped）
     */
    virtual bool OnSearchHits(const std::vector<ShareEntry>& hits, bool done, bool truncated) = 0;
};

/**
 *  ShareSearch —— 分享者一端的按文件名搜索；全部为静态方法，不可实例化。
 */
class ShareSearch
{
public:
    /**
     *  执行一次搜索。
     *  @param params 参数
     *  @param cancel 取消标志（可为 NULL）；置真后在下一次检查时中止
     *  @param sink   命中的接收方，不可为 NULL
     *  @return share_search_outcome
     */
    static int Run(const ShareSearchParams& params, const std::atomic<bool>* cancel, IShareSearchSink* sink);

    /**
     *  检查访问者送来的关键字并转成宽字符。
     *  @param utf8Keyword 关键字（UTF-8）
     *  @param wide        [出参] 宽字符关键字
     *  @return 非空、不超过 kShareSearchKeywordMaxChars、不含控制字符时返回 true
     */
    static bool CheckKeyword(const std::string& utf8Keyword, std::wstring& wide);

    /**
     *  名字是否含有关键字（不区分大小写）。
     *  @param name    条目名
     *  @param keyword 关键字
     *  @return 含有返回 true
     */
    static bool Matches(const std::wstring& name, const std::wstring& keyword);

private:
    ShareSearch() = delete;
};

} // namespace fileshare
