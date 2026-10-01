/**
 *  HistoryImageGrid.h
 *
 *  消息记录窗口「图片」分类的缩略图网格排版，以及「文件」分类"下载本页全部文件"要下载哪些、
 *  这一批下载结束后怎样汇总提示（HistoryBatchDownload）。
 *  都是纯计算：不建控件、不碰窗口，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程共用（见
 *  Tests/HistoryImageGridTests.cpp）。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  网格的排法：按消息的先后（最新在前）从左到右、从上到下排正方形缩略图，列数随可用宽度变化；
 *  每遇到新的一天，先放一行日期标题，再从新的一行开始排，与列表模式的日期分组一致。
 *
 *  典型用法：
 *      HistoryGridLayout layout = LayoutImageGrid(dayKeys, listWidth, metrics);
 *      //按 layout.m_cells[i] 摆第 i 张缩略图，按 layout.m_headers 摆日期标题
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <stdint.h>
#include <windows.h>
#include <set>
#include <string>
#include <vector>

namespace balloonhistory {

//网格的尺寸参数（像素）
struct HistoryGridMetrics
{
    int m_cellSize;     //缩略图边长
    int m_gap;          //缩略图之间（横向与纵向）的间距
    int m_headerH;      //日期标题行的高度
    int m_padding;      //网格四周的内边距

    HistoryGridMetrics()
        : m_cellSize(0)
        , m_gap(0)
        , m_headerH(0)
        , m_padding(0)
    {
    }
};

//一行日期标题的位置
struct HistoryGridHeader
{
    size_t m_firstIndex;   //这一组第一张缩略图在输入里的下标
    RECT   m_rc;           //标题的位置（相对网格左上角）
};

//网格排版的结果
struct HistoryGridLayout
{
    int                            m_columns;   //每行几张
    std::vector<RECT>              m_cells;     //与输入一一对应：第 i 张缩略图的位置（相对网格左上角）
    std::vector<HistoryGridHeader> m_headers;   //各组的日期标题，按出现顺序
    int                            m_height;    //网格总高度（含四周内边距）

    HistoryGridLayout()
        : m_columns(0)
        , m_height(0)
    {
    }
};

//给定宽度下每行能放几张缩略图；至少 1 张。
//  width：可用宽度（含四周内边距）；metrics：尺寸参数。
int GridColumnCount(int width, const HistoryGridMetrics& metrics);

/**
 *  排网格。
 *    dayKeys：每张缩略图所属的"天"（同一天取同一个值，如本地日期的 YYYYMMDD），按显示顺序。
 *             相邻两张的值不同就是新的一组，先放标题再另起一行。
 *    width：可用宽度（含四周内边距）。
 *    metrics：尺寸参数。
 *  @return 排版结果；dayKeys 为空时没有缩略图与标题，高度为上下内边距之和
 */
HistoryGridLayout LayoutImageGrid(const std::vector<int64_t>& dayKeys, int width, const HistoryGridMetrics& metrics);

//「文件」分类里一行文件消息的下载状态
struct HistoryFileRowState
{
    int64_t m_msgid;        //消息 msgid
    bool    m_isFile;       //是否为文件消息（图片、文件夹记录等其它类型为 false）
    bool    m_hasLocal;     //本地是否已有这个文件
    bool    m_downloading;  //是否正在下载

    HistoryFileRowState()
        : m_msgid(0)
        , m_isFile(false)
        , m_hasLocal(false)
        , m_downloading(false)
    {
    }
};

//"下载本页全部文件"要下载哪些：文件消息、msgid 已分配、本地没有、也没在下载的，按输入顺序。
std::vector<int64_t> PickFilesToDownload(const std::vector<HistoryFileRowState>& rows);

//一笔下载结束后，按批次规则得出的判定
struct HistoryBatchFinish
{
    bool m_inBatch;     //这一笔属于当前批次。为 false 时调用方按单条下载的方式处理（失败即提示）
    bool m_batchDone;   //这一笔结束后整批都已结束，调用方应汇总提示一次；下面三个计数只在此时有意义
    int  m_succeeded;   //整批中下载成功的笔数
    int  m_failed;      //整批中下载失败的笔数（不含用户取消的）
    int  m_cancelled;   //整批中被用户取消的笔数

    HistoryBatchFinish()
        : m_inBatch(false)
        , m_batchDone(false)
        , m_succeeded(0)
        , m_failed(0)
        , m_cancelled(0)
    {
    }
};

//一批下载全部结束后的提示方式
enum HistoryBatchNoticeKind
{
    kHistoryBatchNoticeNone    = 0,   //不提示：整批尚未结束，或整批都被用户取消
    kHistoryBatchNoticeAllDone = 1,   //全部成功：在窗口内显示一条短暂的提示
    kHistoryBatchNoticeFailed  = 2,   //至少一笔失败：弹一个提示框，说明失败与成功各几个
};

/**
 *  记录「全部下载」发起的一批文件下载，在这一批全部结束后汇总提示一次。
 *
 *  单条下载失败时逐条弹框是合理的，但「全部下载」一次可能发起几十笔，逐条弹框会在桌面上叠出
 *  同样数量的模态框。窗口在「全部下载」里每发起一笔就 Add 一次；每笔下载结束时调用 OnFinished，
 *  属于本批的不再逐条提示，等本批最后一笔结束时按 BatchNoticeKind / BuildBatchNoticeText 提示一次。
 *  本批尚未结束时再次点「全部下载」，新发起的并入同一批。关窗或切换会话时调用 Clear 作废本批。
 *
 *  只在 UI 线程使用，不加锁。
 */
class HistoryBatchDownload
{
public:
    HistoryBatchDownload();

    /**
     *  把一笔已发起的下载记入当前批次。当前批次尚未结束时并入同一批；已在本批中的 msgid 不重复计入。
     *  @param msgid 已发起下载的那条消息的 msgid；不大于 0 时忽略
     */
    void Add(int64_t msgid);

    /**
     *  一笔下载结束。
     *  @param msgid     结束的那条消息的 msgid
     *  @param ok        是否下载成功
     *  @param cancelled 是否被用户取消（为 true 时不计为失败）
     *  @return 判定结果。不属于本批时 m_inBatch 为 false、计数无意义；本批最后一笔结束时
     *          m_batchDone 为 true 并带上整批计数，同时本批清空，之后的 Add 开始新的一批
     */
    HistoryBatchFinish OnFinished(int64_t msgid, bool ok, bool cancelled);

    /**
     *  作废当前批次（关窗、切换会话时调用）：之后陆续结束的下载都不再属于任何批次。
     *  @return 本批中尚未结束的 msgid（升序）。调用方据此停止跟踪这些下载，使它们结束时也不再提示
     */
    std::vector<int64_t> Clear();

    //当前批次中尚未结束的笔数；为 0 表示没有进行中的批次。
    size_t PendingCount() const;

private:
    std::set<int64_t> m_pending;     //本批中已发起、尚未结束的下载的 msgid；为空表示没有进行中的批次
    int               m_succeeded;   //本批已结束的下载中成功的笔数，本批结束或作废时清零
    int               m_failed;      //本批已结束的下载中失败的笔数（不含取消），本批结束或作废时清零
    int               m_cancelled;   //本批已结束的下载中被取消的笔数，本批结束或作废时清零
};

//整批结束后的提示方式。finish 不是"整批结束"的判定（m_batchDone 为 false）时返回 kHistoryBatchNoticeNone。
HistoryBatchNoticeKind BatchNoticeKind(const HistoryBatchFinish& finish);

/**
 *  整批结束后的提示文字。
 *    有失败且有成功："2 个文件下载失败，请检查网络后重试（另有 3 个已下载完成）。"
 *    只有失败：      "2 个文件下载失败，请检查网络后重试。"
 *    全部成功：      "5 个文件已下载完成"
 *  @return 提示文字；BatchNoticeKind 为 kHistoryBatchNoticeNone 时返回空串
 */
std::wstring BuildBatchNoticeText(const HistoryBatchFinish& finish);

} // namespace balloonhistory
