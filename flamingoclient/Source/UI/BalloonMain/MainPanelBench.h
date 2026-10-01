/**
 *  主面板基准测试：把压测数据灌进主面板，再按固定场景驱动它（滚轮滚动、鼠标扫过各行、
 *  收到新消息、联系人上下线、头像下载完成、搜索框逐字输入、切换页签、整窗重画），逐项
 *  计时并写入日志，用于评估与对比主面板的界面流畅度。
 *
 *  每个场景的计时口径一致："投递输入 + 处理完由它引起的全部消息（含重画）"的墙钟耗时，
 *  即 UI 线程为这一次输入付出的全部代价。连续操作要做到流畅，这个值应当明显小于
 *  16.7 毫秒（60 帧每秒的一帧）。
 *
 *  只供调试入口使用：
 *      Flamingo.exe --mainpanel --stress           只灌数据，便于人工体验
 *      Flamingo.exe --mainpanel --stress --bench   灌数据后自动跑完全部场景
 *  设置环境变量 FLAMINGO_BENCH_OUT=<文件路径> 时，另把结果摘要写入该文件（ASCII）。
 *  日志与摘要一律为英文，每行以 "bench:" 开头。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <string>
#include <vector>

#include "MainPanelStressData.h"

class CBalloonMainPanel;
class CFlamingoClient;

namespace balloonmain {

/**
 *  CMainPanelBench —— 主面板基准测试的执行者。
 *
 *  是 CBalloonMainPanel 的友元：切换页签、收起搜索下拉这类动作没有公开接口，基准测试
 *  需要直接调用。除此之外只走主面板的公开入口（与网络层投递数据的路径相同）。
 *  所有方法都必须在主面板所在的 UI 线程调用。
 */
class CMainPanelBench
{
public:
    /**
     *  构造。
     *    pPanel：  被测主面板，须已创建窗口；本类只借用，不持有。
     *    pClient： 客户端对象，用于在测试期间临时静音（避免收消息时连响几十次提示音）；
     *              本类只借用，不持有，可为 NULL。
     */
    CMainPanelBench(CBalloonMainPanel* pPanel, CFlamingoClient* pClient);

    /**
     *  生成压测数据并灌进主面板，依次走通讯录、群列表、会话列表三条正式填充路径，
     *  每一步单独计时并写日志。
     *    spec：数据规模与种子。
     *  副作用：替换主面板当前显示的全部列表内容。
     */
    void LoadStressData(const StressDataSpec& spec);

    /**
     *  跑完全部基准场景。须先调用 LoadStressData。
     *  副作用：会切换页签、滚动列表、在搜索框里输入文字、修改若干会话的未读与排序，
     *          结束后切回消息页并清空搜索框；测试期间临时静音，结束后恢复原设置。
     *  结果写日志；环境变量 FLAMINGO_BENCH_OUT 非空时另写一份摘要文件。
     */
    void RunScenarios();

private:
    // 一个场景的统计结果。
    struct Sample
    {
        std::string m_name;    // 场景名（英文）
        int         m_count;   // 采样次数
        double      m_avgMs;   // 平均每次耗时（毫秒）
        double      m_maxMs;   // 最慢一次的耗时（毫秒）
        double      m_totalMs; // 合计耗时（毫秒）
    };

    // 处理完 UI 线程消息队列里已经到期的全部消息，再同步完成所有待重画区域的绘制。
    // 遇到 WM_QUIT 时原样重新投递并提前返回，不吞掉退出请求。
    void DrainMessages();

    // 在给定的墙钟时长内持续处理消息（空闲时阻塞等待），返回这段时间里 UI 线程实际消耗的
    // CPU 时间（毫秒）。用来测量滚动条淡入淡出这类由定时器驱动、不由输入直接触发的开销。
    double PumpForAndMeasureCpuMs(double wallMs);

    // 记录一条统计结果并写日志。durations 为逐次耗时（毫秒），为空时不记录。
    void Record(const std::string& name, const std::vector<double>& durations);

    // 记录一条单值结果（如一次性的填充耗时、GDI 对象数）并写日志。
    void RecordValue(const std::string& name, double value, const char* unit);

    // 把全部结果写入 FLAMINGO_BENCH_OUT 指定的文件；环境变量为空时什么都不做。
    void WriteSummaryFile();

    // 取某个页签对应的列表面板在窗口客户区中的矩形；找不到时返回空矩形。
    RECT PaneRect(unsigned int paneId) const;

    // 取某棵列表树的行高（像素）；找不到时返回 0。
    int TreeRowHeight(unsigned int treeId) const;

    // 在指定面板中心向窗口发送 steps 次滚轮消息，逐次计时，结果记到 name 名下。
    //   delta：每次的滚轮增量，负值向下滚。
    void BenchWheel(const std::string& name, unsigned int paneId, int steps, int delta);

    // 各个场景。
    void BenchFullRepaint();
    void BenchHover();
    void BenchScrollFadeIdle();
    void BenchIncomingMessages();
    void BenchPresenceChanges();
    void BenchAvatarDownloads();
    void BenchSearchTyping();
    void BenchTabSwitch();

    // 当前进程的 GDI 对象数与 USER 对象数，写入结果。label 用于区分采样时机。
    void RecordGuiResources(const std::string& label);

    // 核对会话树的行顺序是否与主面板排好序的会话列表逐项一致（收到消息改为就地挪动
    // 那一行之后，用它确认没有挪错）。返回不一致的行数；0 表示完全一致。
    int CountConversationOrderMismatches() const;

    // 联系人树一个可见行的快照，用来比对"只重建收藏分区"与"整棵重建"的结果。
    struct TreeRowSnap
    {
        CString m_label;      // 行文字
        int     m_children;   // 直接子节点个数
        bool    m_expanded;   // 是否展开
        bool    m_favorite;   // 是否画收藏徽章
    };

    // 取联系人树当前全部可见行的快照。
    std::vector<TreeRowSnap> SnapshotContactsTree() const;

    // 两份快照逐行比对，返回不一致的行数（行数不同也计入）。
    static int CountSnapshotMismatches(const std::vector<TreeRowSnap>& a,
                                       const std::vector<TreeRowSnap>& b);

    // 核对收藏分区的节点映射：分区每一行恰好登记在一张表里、两张表里没有已删除节点的
    // 残留、分区行数与根节点的子节点个数一致。返回发现的问题个数；0 表示一致。
    int CountFavoriteMappingErrors() const;

    // 部门树里某联系人的各行（分区外）中，收藏徽章与期望不符的行数。
    int CountFavoriteBadgeErrors(int64_t userid, bool expected) const;

    // 收藏分区相关的检查（FSR1~FSR3）：灌入数据后与整棵重建的结果比对，收藏切换的
    // 耗时，以及切换后的结果比对、徽章与映射核对。
    void BenchFavoritesConsistency();

private:
    CBalloonMainPanel* m_pPanel;     // 被测主面板，借用不持有
    CFlamingoClient*   m_pClient;    // 客户端对象（静音用），借用不持有，可为 NULL
    StressDataSet      m_data;       // 已灌入的压测数据；消息、上下线等场景从中挑选对象
    std::vector<std::string> m_lines;   // 已产生的结果行，供写摘要文件
};

} // namespace balloonmain
