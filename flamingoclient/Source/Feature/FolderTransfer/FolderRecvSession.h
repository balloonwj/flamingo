/**
 *  单聊「接收文件夹」的接收端状态机，FolderRecvSession.h
 *
 *  一次「接收文件夹」的完整过程按 docs/单聊文件夹传输方案.md 8.1 的时序推进：
 *    1. 用 cmd_folder_relay_info 下发的中转参数建 1 条控制通道（lane 0）与 N 条数据通道
 *       （lane 1..N），每条通道一个 FolderRelayClient 实例，共用同一枚令牌；
 *    2. 在控制通道上收 ft_manifest（文件多时分多段，用 FolderRelayManifestAssembler 重组），
 *       得到目录列表、文件列表与总计；
 *    3. <u>先把目录结构全部建好</u>：在用户选定的保存位置下建根目录（重名时自动改名成
 *       "xxx(2)" / "xxx(3)"），再按目录清单逐级建完全部子目录（<u>含空目录</u>）。这一步
 *       把"没有写权限、路径过长、磁盘只读、空间不足"这类环境问题挡在传字节之前；
 *    4. 目录全部建成功之后才在控制通道回 ft_manifest_ack，并带上每个文件的续传起点；
 *    5. 各数据通道并行收 ft_file_begin → 若干 ft_file_chunk → ft_file_end。收一片写一片盘
 *       （写到 "<目标名>.part"），边写边增量算 MD5，每片回一个 ft_chunk_ack；一个文件收完
 *       校验字节数与 MD5，通过则去掉 .part 后缀，回 ft_file_result；
 *    6. 控制通道收到 ft_complete 且全部文件都已校验通过，整体完成；
 *    7. 任一步出错或用户取消 → 在控制通道发 ft_abort（带错误码与描述）→ 停掉全部通道 →
 *       向上层回报失败原因。取消在分片边界生效，错误码为 error_code_folder_canceled。
 *
 *  <u>断点续传</u>：「每个文件已落盘字节数 + 增量 MD5 上下文 + 落地根目录路径」放在
 *  FolderReceiveState 里，由<u>上层</u>持有、以 std::shared_ptr 传进本会话。会话对象是一次性
 *  的（失败即作废），而本次登录期间用户点「重试」要复用同一份进度接着传，故进度不能存在
 *  会话里。重试时复用上次已建好的根目录（FolderReceiveState::GetRootPath），不再改名新建。
 *
 *  <u>线程模型</u>：Start() 只启动线程即返回，全部实际工作在工作线程上完成：
 *    · 会话线程一条（SessionProc）：建全部通道 → 启动数据通道工作线程 → 兼作控制通道的
 *      工作线程 → 收尾时回收全部线程并回报最终结果；
 *    · 数据通道工作线程每条数据通道一条（LaneProc）：只做本通道上的写盘、增量 MD5 与回执；
 *    · 另有 FolderRelayClient 自带的接收线程每条通道一条，它只把收到的应用帧拷进本通道的
 *      待处理队列就返回，绝不在其上做磁盘 I/O。
 *  <u>流控</u>：每条通道的待处理队列深度上限 kFolderRecvLaneQueueDepth 帧；队列满时接收线程
 *  在入队处阻塞，TCP 接收窗口随之收紧，发送端自然被压住。这与发送端"每条数据通道在途分片
 *  不超过 4 片"的窗口配合，两端都不会把内存撑爆。
 *
 *  <u>回调线程</u>：IFolderRecvSessionHandler 的三个回调<u>全部在工作线程上触发</u>，<u>不是</u>
 *  UI 线程。上层实现须自行把数据拷出后转投 UI 线程（典型做法是 PostMessage 到窗口），并且
 *  不得在回调里做耗时阻塞操作，也不得在回调里调用本会话的 Stop()（会自己等自己，必然死锁）。
 *  本模块<u>不</u>引用任何窗口类、<u>不</u>调用 PostMessage。
 *
 *  本文件分成两层：
 *    · 纯逻辑层（namespace folderrecv 与 FolderReceiveState）—— 根目录重名改名规则、相对路径
 *      落地前的安全校验与本地化、续传起点计算、乱序 / 重复分片的判定、接收进度状态。<u>全部
 *      写成内联</u>，不碰文件系统、不碰 socket，可脱离磁盘单测（见 Source/Tests/
 *      FolderRecvSessionTests.cpp，Suite FRS）。写成内联而不是放进 .cpp，是因为 .cpp 要用
 *      FolderRelayClient 与 Win32 文件 API，而测试工程不编译它们，.cpp 里的东西链接不到 ——
 *      这与 FolderRelayClient.h 里 folderrelay 命名空间的处理方式一致。
 *    · 会话层（FolderRecvSession）—— 通道、线程、磁盘 I/O，靠真机联调验证。
 *
 *  典型用法（上层）：
 *      std::shared_ptr<foldertransfer::FolderReceiveState> state(new foldertransfer::FolderReceiveState);
 *      // ... 用户点「接收」并选好保存位置，收到 cmd_folder_relay_info 之后：
 *      foldertransfer::FolderRecvSessionParams params;
 *      params.m_relayIp      = relayip;
 *      params.m_relayPort    = relayport;
 *      params.m_relayToken   = token;
 *      params.m_dataLanes    = lanes;
 *      params.m_saveParentDir = L"D:\\下载";
 *      session.Start(params, state, this);   // 立即返回，结果经回调回报
 *      // ... 用户点「取消」：
 *      session.Cancel();
 *      // ... 会话结束（收到完成 / 失败回调之后，在 UI 线程上）：
 *      session.Stop();
 *
 *  balloonwj@qq.com   2026-08-14
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "FolderRelayFraming.h"   // 应用帧编解码、清单重组器
#include "FolderRelayClient.h"    // 一条中转通道的连接
#include "FolderLaneTransport.h"  // 通道传输抽象：中转连接或 P2P 可靠通道（P2P 方案第 5 期）
#include "FolderManifest.h"       // 路径安全校验、长路径、UTF-8 与 UTF-16 互转
#include "FolderTransferHooks.h"  // IFolderRecvHooks：文件共享的传输子会话用来定制前置交换、落点与覆盖规则
#include "../../Common/Util/MD5Core.h"           // MD5_CTX / MD5Init / MD5Update / MD5Final（增量 MD5，与下载路径共用同一份实现）

namespace foldertransfer {

//==================== 常量 ====================

//半成品文件的后缀。收到的字节先写到 "<目标名>.part"，字节数与 MD5 都校验通过之后才
//去掉这个后缀，故只要目标名存在即为完整文件，崩在中途只会留下 .part。与文件下载路径
//里的 ".downloading"（net/FileStreamTransfer.cpp）同一思路，后缀另取是为了让用户一眼
//看出这是文件夹传输留下的半成品。
const wchar_t* const kFolderRecvPartSuffix = L".part";

//根目录重名时自动改名的最大尝试次数。第 1 次用原名，第 2..N 次用 "xxx(2)"…"xxx(1000)"。
//取 1000 是一道防卡死的闸：正常场景重名不过几次，真到 1000 个同名目录说明目标位置状态
//异常，此时如实报建目录失败远好过在原地试到天荒地老。
const int kFolderRecvMaxRenameAttempts = 1000;

//自动改名的第一个候选序号。原名不带后缀，故带括号的候选从 2 起，即 "xxx(2)"。
//出处：docs/单聊文件夹传输方案.md 第 9 节「重名」。
const int kFolderRecvRenameSuffixStart = 2;

//每条通道待处理帧队列的深度上限，单位：帧。取 8 —— 发送端每条数据通道在途分片不超过
//4 片（512 KiB × 4 = 2 MiB，见方案 5.3 末段），留一倍余量即可吸收突发，再大只是白占内存。
//队列满时 FolderRelayClient 的接收线程在入队处阻塞，TCP 接收窗口随之收紧，这就是本端的
//流控手段。
const size_t kFolderRecvLaneQueueDepth = 8;

//工作线程在队列上单次等待的时长，单位：毫秒。周期性醒来是为了检查停止标志，
//使 Cancel() 与 Stop() 不必等到下一帧到达才生效。
const int kFolderRecvQueueWaitMs = 200;

//两次进度回调之间的最小间隔，单位：毫秒。上万个小文件时每片都回调会把 UI 线程的消息
//队列压垮，故按时间节流；文件完成、整体完成这类关键节点不受此限制，一律立即上报。
const int kFolderRecvProgressIntervalMs = 200;

//MD5 摘要的字节数与其十六进制文本的字符数。MD5 固定 128 位，即 16 字节 / 32 个十六进制字符。
const int kFolderRecvMd5DigestBytes = 16;
const int kFolderRecvMd5HexChars    = 32;

//"磁盘上没有这个 .part 半成品文件" 的约定取值。续传起点计算用它区分「文件不存在」与
//「文件存在但长度为 0」——前者只能从头传，后者也从头传，但两者的日志含义不同。
const int64_t kFolderRecvNoPartFile = -1;

//==================== 纯逻辑层 ====================

//接收端记录的单个文件的落地进度。这是断点续传的最小单位：已落盘字节数与增量 MD5 上下文
//必须<u>始终一致</u>——m_bytesWritten 个字节既已写进 .part 文件、也已喂进 m_md5ctx，两者
//不同步就无法在重试时接着算 MD5。
struct FolderRecvFileState
{
    std::string m_relpath;        //相对根目录的路径（UTF-8，正斜杠分隔），取自清单，用于与新清单核对
    int64_t     m_size;           //清单里给出的文件字节数，单位：字节
    int64_t     m_bytesWritten;   //已落盘且已计入 m_md5ctx 的字节数，单位：字节；与 .part 文件长度保持一致
    bool        m_completed;      //是否已通过字节数与 MD5 校验并去掉 .part 后缀（重试时整个跳过）
    bool        m_hashing;        //m_md5ctx 是否已 MD5Init 并与 m_bytesWritten 同步；false 表示尚未开始算
    MD5_CTX     m_md5ctx;         //增量 MD5 上下文，生命周期与本记录相同，跨会话保留供重试续算

    FolderRecvFileState()
        : m_size(0)
        , m_bytesWritten(0)
        , m_completed(false)
        , m_hashing(false)
    {
        memset(&m_md5ctx, 0, sizeof(m_md5ctx));
    }
};

namespace folderrecv {

//路径存在性探测接口。把「这个路径是不是已经被占用」抽成接口，是为了让根目录改名规则
//成为可单测的纯逻辑 —— 生产实现走 Win32 的 GetFileAttributesW（定义在 .cpp 里），测试里
//给一个内存实现即可，不必真去磁盘上造同名目录。
class IFolderPathProbe
{
public:
    virtual ~IFolderPathProbe() {}

    /**
     *  判断一个路径当前是否已存在（文件或目录都算存在）。
     *  @param path 待探测的完整路径（宽字符）
     *  @return 已存在返回 true
     */
    virtual bool PathExists(const std::wstring& path) = 0;
};

/**
 *  把父目录与一个名字拼成完整路径。会先去掉父目录末尾多余的分隔符，故
 *  "D:\\下载\\" 与 "D:\\下载" 得到同样的结果。
 *  @param parentDir 父目录路径（宽字符），不应为空
 *  @param name      要拼接的名字（宽字符），不应含分隔符
 *  @return 拼好的路径；parentDir 为空时直接返回 name
 */
inline std::wstring JoinFolderPath(const std::wstring& parentDir, const std::wstring& name)
{
    if (parentDir.empty())
        return name;

    std::wstring path = parentDir;
    while (!path.empty() && (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/'))
        path.erase(path.size() - 1);

    //父目录被削成空串（传进来的就是纯分隔符）时不再补分隔符，避免拼出绝对路径
    if (path.empty())
        return name;

    path += L'\\';
    path += name;
    return path;
}

/**
 *  按重名改名规则造出第 attempt 个候选目录名。
 *  规则：第 1 个候选就是原名本身，第 2 个起在名字<u>末尾</u>追加 "(序号)"，即
 *  "报告" → "报告"、"报告(2)"、"报告(3)"…… 目录没有扩展名，故后缀一律加在末尾。
 *  @param baseName 原始文件夹名（宽字符），不含任何路径部分
 *  @param attempt  候选序号，从 1 起；小于 1 时按 1 处理
 *  @return 候选目录名
 */
inline std::wstring BuildIndexedFolderName(const std::wstring& baseName, int attempt)
{
    if (attempt <= 1)
        return baseName;

    //第 2 个候选起才带括号后缀，序号从 kFolderRecvRenameSuffixStart 开始与 attempt 同步递增
    const int suffix = kFolderRecvRenameSuffixStart + (attempt - 2);

    std::wstring name = baseName;
    name += L'(';
    name += std::to_wstring(suffix);
    name += L')';
    return name;
}

/**
 *  在保存位置下挑一个未被占用的根目录路径。首选原名，被占用则依次试
 *  "xxx(2)" / "xxx(3)"……直到找到空位或用尽 kFolderRecvMaxRenameAttempts 次尝试。
 *  <u>只挑名字，不建目录</u>——建目录由调用方在拿到结果后做，两件事分开才好单测。
 *  @param parentDir  用户选定的保存位置（宽字符），非空
 *  @param folderName 发送端给出的根文件夹名（宽字符），非空；调用方须先确认它通过了
 *                    路径安全校验，本函数不重复校验
 *  @param probe      路径存在性探测器（引用，不接管所有权，生命周期须覆盖本次调用）
 *  @param outPath    [出参] 挑中的完整路径；返回 false 时被清空
 *  @return 挑到空位返回 true；参数为空、或候选全被占用返回 false（调用方按建目录失败处理）
 */
inline bool ChooseUniqueRootFolderPath(const std::wstring& parentDir,
                                       const std::wstring& folderName,
                                       IFolderPathProbe& probe,
                                       std::wstring& outPath)
{
    outPath.clear();
    if (parentDir.empty() || folderName.empty())
        return false;

    for (int attempt = 1; attempt <= kFolderRecvMaxRenameAttempts; ++attempt)
    {
        const std::wstring candidate = JoinFolderPath(parentDir, BuildIndexedFolderName(folderName, attempt));
        if (!probe.PathExists(candidate))
        {
            outPath = candidate;
            return true;
        }
    }
    return false;
}

/**
 *  把清单里的一条相对路径校验并转换成可以直接拼到根目录后面的本地相对路径。
 *
 *  这是接收端<u>唯一</u>的落地入口：先过 CheckFolderRelativePath（与发送端共用同一份判断，
 *  拒绝 ".."、绝对路径、盘符前缀、非法字符、保留设备名、超长），再转成 UTF-16，最后把正
 *  斜杠换成反斜杠。<u>不做</u>安全校验就落地会让对端借一条相对路径把文件写到用户选定目录
 *  之外（见 docs/单聊文件夹传输方案.md 第 9 节）。
 *
 *  @param relPath  清单里的相对路径（UTF-8，正斜杠分隔）
 *  @param outLocal [出参] 本地写法的相对路径（宽字符，反斜杠分隔）；返回值非
 *                  kFolderPathOk 时被清空
 *  @return 通过校验并转换成功返回 kFolderPathOk；否则返回具体的判定结果，调用方据此
 *          按 error_code_folder_mkdir_fail / write_fail 上报并中止整个传输
 */
inline FolderPathVerdict ResolveLocalRelativePath(const std::string& relPath, std::wstring& outLocal)
{
    outLocal.clear();

    const FolderPathVerdict verdict = CheckFolderRelativePath(relPath);
    if (verdict != kFolderPathOk)
        return verdict;

    std::wstring wide;
    if (!Utf8ToUtf16(NormalizeFolderRelativePath(relPath), wide))
    {
        //UTF-8 字节序列非法：这条路径根本不是合法文本，按含非法字符处理
        return kFolderPathIllegalChar;
    }

    for (size_t i = 0; i < wide.size(); ++i)
    {
        if (wide[i] == L'/')
            wide[i] = L'\\';
    }
    outLocal = wide;
    return kFolderPathOk;
}

/**
 *  取一条相对路径的父目录部分（仍是相对路径）。
 *  用于在建目录阶段把「文件所在目录」也一并建出来 —— 清单里的目录列表正常已覆盖它们，
 *  这里再取一次是防御性的，避免一份目录列表不完整的清单让写文件时找不到落点。
 *  @param relPath 相对路径（UTF-8，正斜杠分隔）
 *  @return 父目录的相对路径；relPath 位于根目录下（不含分隔符）时返回空串
 */
inline std::string ParentOfRelativePath(const std::string& relPath)
{
    const size_t pos = relPath.find_last_of(kFolderPathSeparator);
    if (pos == std::string::npos)
        return std::string();

    return relPath.substr(0, pos);
}

/**
 *  算出一个文件本次应当从哪个偏移开始接收（即回给发送端的续传起点）。
 *
 *  口径（与 docs/单聊文件夹传输方案.md 8.3 一致）：
 *    · 内存记录标记为已完成 → 返回清单里的文件字节数。发送端看到「起点 == 文件大小」
 *      即知道这个文件不必再传，整个跳过；
 *    · 内存记录里已落盘字节数为 0（全新文件，含首次传输）→ 返回 0，从头传；
 *    · 磁盘上的 .part 半成品短于内存记录的字节数 → 返回 0。此时 MD5 上下文覆盖的字节比
 *      磁盘上真实存在的还多，接着算必然校验不过，只能整个重来；
 *    · 内存记录的字节数超过清单里的文件大小 → 返回 0。记录自相矛盾（清单变了或记录被写坏），
 *      同样整个重来；
 *    · 其余（半成品，磁盘长度不短于记录）→ 返回内存记录的字节数。正常情况下它就等于 .part
 *      文件的长度，两者由写盘路径保持同步；磁盘上多出来的尾巴（上一次写了一半没计入记录的
 *      部分）由调用方在打开文件时截断掉。
 *
 *  @param record       该文件的内存记录
 *  @param partFileBytes 磁盘上 .part 半成品文件的当前字节数；文件不存在时传
 *                       kFolderRecvNoPartFile
 *  @return 续传起点，单位：字节，取值范围 [0, record.m_size]
 */
inline int64_t ComputeFileResumeOffset(const FolderRecvFileState& record, int64_t partFileBytes)
{
    //已经校验通过并落地：整个跳过
    if (record.m_completed)
        return record.m_size;

    //没有任何已落盘字节，或记录自相矛盾：从头传
    if (record.m_bytesWritten <= 0)
        return 0;
    if (record.m_bytesWritten > record.m_size)
        return 0;

    //MD5 上下文没跟上已落盘字节数：无法续算，从头传
    if (!record.m_hashing)
        return 0;

    //磁盘上的半成品比记录还短：记录不可信，从头传
    if (partFileBytes == kFolderRecvNoPartFile || partFileBytes < record.m_bytesWritten)
        return 0;

    return record.m_bytesWritten;
}

//一片来料相对于「已落盘字节数」的判定结果。数据通道上正常是顺序到达的，本判定处理的是
//发送端重发、乱序、或对端行为异常这几种情况。
enum FolderChunkVerdict
{
    //可以收下：本片有一段（或全部）落在已落盘字节之后，写盘时须先跳过 outSkipBytes 个
    //已有字节，再把其后的部分写下去（正好接续时 outSkipBytes 为 0）
    kFolderChunkAccept = 0,
    //整片都落在已落盘区间之内，是发送端的重复投递：直接丢弃，回一个当前位点的回执即可，
    //不写盘、不喂 MD5，否则会把同一段字节算两遍
    kFolderChunkDuplicate = 1,
    //本片起点越过了已落盘字节，中间留下空洞：增量 MD5 无法跳着算，只能整体判失败
    kFolderChunkGap = 2,
    //本片末尾越过了清单里给出的文件大小：对端给的数据与清单不符，判失败
    kFolderChunkOverrun = 3,
    //偏移或长度为负数：包体被改坏或对端实现有误，判失败
    kFolderChunkBadArgument = 4,
};

/**
 *  判定一片来料该怎么处理。纯函数，接收端每收到一个 ft_file_chunk 都先过它。
 *  @param writtenBytes  该文件当前已落盘（且已计入 MD5）的字节数，须 >= 0
 *  @param chunkOffset   本片在文件中的起始偏移
 *  @param chunkLen      本片字节数（允许为 0，表示空片）
 *  @param fileSize      清单里给出的文件字节数
 *  @param outSkipBytes  [出参] 本片开头须跳过的字节数（这部分已经落过盘了）；返回值
 *                       不是 kFolderChunkAccept 时置 0
 *  @return 见 FolderChunkVerdict
 */
inline FolderChunkVerdict ClassifyFolderChunk(int64_t writtenBytes, int64_t chunkOffset,
                                              int64_t chunkLen, int64_t fileSize,
                                              int64_t& outSkipBytes)
{
    outSkipBytes = 0;

    if (writtenBytes < 0 || chunkOffset < 0 || chunkLen < 0 || fileSize < 0)
        return kFolderChunkBadArgument;

    const int64_t chunkEnd = chunkOffset + chunkLen;
    if (chunkEnd > fileSize)
        return kFolderChunkOverrun;

    //起点越过已落盘字节：中间空了一段，收下就会算错 MD5
    if (chunkOffset > writtenBytes)
        return kFolderChunkGap;

    //整片都已落过盘：重复投递，丢弃
    if (chunkEnd <= writtenBytes)
        return kFolderChunkDuplicate;

    //其余情形本片尾部有新字节：跳过开头已有的部分（正好接续时为 0）后收下
    outSkipBytes = writtenBytes - chunkOffset;
    return kFolderChunkAccept;
}

/**
 *  数据通道的对端断开时，是否应判整个会话失败。纯函数。
 *  发送端在最后一个文件校验通过、发出 ft_complete 之后会立即关闭全部通道；接收端各通道的线程
 *  各自处理断开，数据通道可能先于控制通道处理完 ft_complete 就看到断开。此时数据通道上已不会
 *  再有数据，不应判失败，整体是否完成由控制通道（收到 ft_complete、或控制通道断开）决定。
 *  @param manifestReady  清单是否已收齐（未收齐时文件总数未知，断开一律判失败）
 *  @param completedFiles 已校验通过并落地的文件个数
 *  @param totalFiles     清单里的文件总数
 *  @return 应判失败返回 true；全部文件都已完成时返回 false
 */
inline bool IsDataLaneCloseFatal(bool manifestReady, int64_t completedFiles, int64_t totalFiles)
{
    if (!manifestReady)
        return true;
    return completedFiles < totalFiles;
}

} // namespace folderrecv

//接收状态：一次「接收文件夹」在<u>本次登录期间</u>的全部落地进度。
//
//由<u>上层</u>持有（以 std::shared_ptr 传进会话），生命周期长于会话对象 —— 一次传输失败后
//用户点「重试」会新建一个 FolderRecvSession，但必须复用同一份状态才能接着传，而不是从零
//开始。按方案第 2 节，重试窗口只在本次登录期间有效，故本对象不做持久化，退出登录随之消失。
//
//<u>线程安全</u>：内部一把互斥量，全部方法都可在任意线程调用。热路径（每片一次）只有
//StoreFileBytes 一个方法，它只改两个整数，不会成为多通道并行的瓶颈；增量 MD5 上下文由
//数据通道工作线程在本地推进，只在文件开始 / 结束 / 通道收尾时经 StoreFileProgress 写回，
//故 512 KiB 的 MD5 运算不在锁内进行。
class FolderReceiveState
{
public:
    FolderReceiveState()
        : m_receivedBytes(0)
        , m_completedFiles(0)
    {
    }

    FolderReceiveState(const FolderReceiveState&) = delete;
    FolderReceiveState& operator=(const FolderReceiveState&) = delete;

    /**
     *  记下本次落地的根目录（含自动改名之后的最终名字）。首次传输时由会话在建完根目录后
     *  调用，之后重试一律复用它、不再改名新建。
     *  @param path 根目录的完整路径（宽字符）
     */
    void SetRootPath(const std::wstring& path)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_rootPath = path;
    }

    //取本次落地的根目录完整路径；尚未建过根目录时为空串。
    std::wstring GetRootPath() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_rootPath;
    }

    //是否已经建过根目录（即本次是重试而非首次）。
    bool HasRootPath() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return !m_rootPath.empty();
    }

    /**
     *  用收到的清单初始化或核对文件记录。
     *  首次调用按清单建立全部记录；重试时逐条核对相对路径与字节数，完全一致则保留已有进度，
     *  只要有一条对不上就整体丢弃重来 —— 清单变了说明对端换了内容，沿用旧进度会写出一个内容
     *  错乱的文件。
     *  @param files 清单里的文件列表（下标即协议里的文件序号）
     *  @return 沿用了已有进度返回 true；记录被整体重置（首次，或清单与上次不一致）返回 false
     */
    bool SyncWithManifest(const std::vector<FolderRelayFileEntry>& files)
    {
        std::lock_guard<std::mutex> guard(m_mtx);

        bool reusable = (m_files.size() == files.size()) && !m_files.empty();
        if (reusable)
        {
            for (size_t i = 0; i < files.size(); ++i)
            {
                if (m_files[i].m_relpath != files[i].m_relpath || m_files[i].m_size != files[i].m_size)
                {
                    reusable = false;
                    break;
                }
            }
        }
        if (reusable)
            return true;

        m_files.clear();
        m_files.resize(files.size());
        for (size_t i = 0; i < files.size(); ++i)
        {
            m_files[i].m_relpath = files[i].m_relpath;
            m_files[i].m_size    = files[i].m_size;
        }
        m_receivedBytes  = 0;
        m_completedFiles = 0;
        return false;
    }

    //已登记的文件条数（等于清单里的文件总数）。
    size_t GetFileCount() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_files.size();
    }

    /**
     *  取某个文件记录的快照（含 MD5 上下文的副本）。
     *  @param index 文件序号（清单文件列表的下标）
     *  @param out   [出参] 记录快照；返回 false 时不被修改
     *  @return 序号有效返回 true
     */
    bool GetFileRecord(int64_t index, FolderRecvFileState& out) const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (!IsValidIndexLocked(index))
            return false;

        out = m_files[static_cast<size_t>(index)];
        return true;
    }

    /**
     *  只更新某个文件的已落盘字节数（每收一片调一次的热路径）。
     *  @param index        文件序号
     *  @param bytesWritten 已落盘且已计入 MD5 的字节数
     */
    void StoreFileBytes(int64_t index, int64_t bytesWritten)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (!IsValidIndexLocked(index))
            return;

        FolderRecvFileState& record = m_files[static_cast<size_t>(index)];
        m_receivedBytes += (bytesWritten - record.m_bytesWritten);
        record.m_bytesWritten = bytesWritten;
    }

    /**
     *  把工作线程上推进到一半的进度与增量 MD5 上下文写回状态对象。文件开始、文件结束、
     *  以及通道异常收尾时各调一次，使重试能接着算 MD5。
     *  @param index        文件序号
     *  @param bytesWritten 已落盘且已计入 MD5 的字节数
     *  @param ctx          与 bytesWritten 同步的增量 MD5 上下文
     */
    void StoreFileProgress(int64_t index, int64_t bytesWritten, const MD5_CTX& ctx)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (!IsValidIndexLocked(index))
            return;

        FolderRecvFileState& record = m_files[static_cast<size_t>(index)];
        m_receivedBytes += (bytesWritten - record.m_bytesWritten);
        record.m_bytesWritten = bytesWritten;
        record.m_md5ctx       = ctx;
        record.m_hashing      = true;
    }

    /**
     *  把某个文件整个重置为「从零开始」（磁盘半成品与内存记录对不上时）。
     *  @param index 文件序号
     */
    void ResetFile(int64_t index)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (!IsValidIndexLocked(index))
            return;

        FolderRecvFileState& record = m_files[static_cast<size_t>(index)];
        if (record.m_completed)
            --m_completedFiles;

        m_receivedBytes -= record.m_bytesWritten;
        record.m_bytesWritten = 0;
        record.m_completed    = false;
        record.m_hashing      = false;
        memset(&record.m_md5ctx, 0, sizeof(record.m_md5ctx));
    }

    /**
     *  标记某个文件已通过字节数与 MD5 校验并已去掉 .part 后缀。重复调用不会把完成计数算两遍。
     *  @param index 文件序号
     */
    void MarkFileCompleted(int64_t index)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (!IsValidIndexLocked(index))
            return;

        FolderRecvFileState& record = m_files[static_cast<size_t>(index)];
        if (record.m_completed)
            return;

        m_receivedBytes += (record.m_size - record.m_bytesWritten);
        record.m_bytesWritten = record.m_size;
        record.m_completed    = true;
        ++m_completedFiles;
    }

    //已校验通过并落地的文件个数。
    int64_t GetCompletedFileCount() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_completedFiles;
    }

    //全部文件已落盘字节数之和，供进度显示。
    int64_t GetReceivedBytes() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_receivedBytes;
    }

    /**
     *  按当前进度算出 ft_manifest_ack 里的续传位点表。
     *  位点表是稀疏的：偏移为 0 的文件不列进去（协议约定未列出的一律按 0 处理），故首次
     *  传输产出的是一张空表。已完成的文件列出「偏移 == 文件大小」，发送端据此整个跳过。
     *  <u>调用前须先对有进度的文件做过磁盘核对</u>（见 FolderRecvSession 的 ReconcileFile），
     *  否则内存记录可能与磁盘上的 .part 对不上。
     *  @param out [出参] 续传位点表；函数内部**先清空**再写入
     */
    void BuildResumePoints(std::vector<FolderRelayResumePoint>& out) const
    {
        out.clear();

        std::lock_guard<std::mutex> guard(m_mtx);
        for (size_t i = 0; i < m_files.size(); ++i)
        {
            const FolderRecvFileState& record = m_files[i];
            const int64_t offset = record.m_completed ? record.m_size : record.m_bytesWritten;
            if (offset <= 0)
                continue;

            FolderRelayResumePoint point;
            point.m_index  = static_cast<int64_t>(i);
            point.m_offset = offset;
            out.push_back(point);
        }
    }

    //整体清空（根目录路径与全部文件记录）。新发起一次传输、或会话已过期时调用。
    void Reset()
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_rootPath.clear();
        m_files.clear();
        m_receivedBytes  = 0;
        m_completedFiles = 0;
    }

private:
    //序号是否落在已登记的记录范围内。调用方须已持有 m_mtx。
    bool IsValidIndexLocked(int64_t index) const
    {
        return index >= 0 && static_cast<size_t>(index) < m_files.size();
    }

private:
    mutable std::mutex               m_mtx;             //保护以下全部成员；mutable 是为了让只读方法也能加锁
    std::wstring                     m_rootPath;        //本次落地的根目录完整路径（含改名后的名字）；空串表示尚未建过
    std::vector<FolderRecvFileState> m_files;           //按清单序号排列的文件记录，生命周期与本对象相同
    int64_t                          m_receivedBytes;   //全部文件已落盘字节数之和，单位：字节（增量维护，避免每次进度都遍历）
    int64_t                          m_completedFiles;  //已校验通过并落地的文件个数
};

//==================== 与上层的接口 ====================

//一次进度更新的快照。
struct FolderRecvProgress
{
    int64_t     m_receivedBytes;   //已落盘字节数合计，单位：字节
    int64_t     m_totalBytes;      //清单里的总字节数，单位：字节
    int64_t     m_doneFiles;       //已校验通过并落地的文件个数
    int64_t     m_totalFiles;      //清单里的文件总数
    std::string m_currentFile;     //触发本次更新的文件的相对路径（UTF-8），供界面显示「正在接收 xxx」

    FolderRecvProgress()
        : m_receivedBytes(0)
        , m_totalBytes(0)
        , m_doneFiles(0)
        , m_totalFiles(0)
    {
    }
};

//接收会话的事件回调接口。
//
//<u>三个回调全部在本会话的工作线程上触发，不是 UI 线程</u>：进度回调来自各数据通道的工作
//线程（已由会话内部串行化，同一时刻只有一个回调在跑），完成与失败回调来自会话线程。实现方
//须把需要的数据拷出后自行转投 UI 线程，不得直接碰界面对象，也不得在回调里做耗时阻塞操作
//（会拖慢整条通道的收字节）。
//
//完成与失败<u>二者只会触发其一，且只触发一次</u>；触发之后不再有任何进度回调。
//<u>不得在回调里调用本会话的 Stop()</u> —— 那会让工作线程等自己结束，必然死锁；正确做法是
//转投 UI 线程之后再 Stop()。
class IFolderRecvSessionHandler
{
public:
    virtual ~IFolderRecvSessionHandler() {}

    /**
     *  进度更新。按 kFolderRecvProgressIntervalMs 节流，文件完成等关键节点不受节流限制。
     *  @param progress 进度快照，仅在本次回调内有效，需留存须自行拷贝
     */
    virtual void OnFolderRecvProgress(const FolderRecvProgress& progress) = 0;

    /**
     *  整体完成：控制通道收到 ft_complete 且全部文件都已通过校验并落地。
     *  @param rootPath   实际落地的根目录完整路径（含自动改名之后的名字），供界面上的
     *                    「打开文件夹」使用
     *  @param totalFiles 落地的文件总数
     *  @param totalBytes 落地的字节数合计
     */
    virtual void OnFolderRecvCompleted(const std::wstring& rootPath,
                                       int64_t totalFiles, int64_t totalBytes) = 0;

    /**
     *  整体失败：本端出错、对端发来 ft_abort、通道断开，或用户取消。
     *  已落盘的部分<u>保留在磁盘上</u>并记在 FolderReceiveState 里，供期内重试续传。
     *  @param errcode 错误码，取值见 Source/net/Protocol.h 的 error_code_folder_* 一组
     *  @param errmsg  错误描述（英文，供日志与排查；界面文案由上层按 errcode 另行给出）
     */
    virtual void OnFolderRecvFailed(int errcode, const std::string& errmsg) = 0;
};

//启动一次接收会话所需的参数，全部来自 cmd_folder_relay_info 与用户选择。
struct FolderRecvSessionParams
{
    std::string  m_sessionId;      //本次传输的会话 id，仅用于日志与排查
    std::string  m_relayIp;        //中转地址（relayip 字段）
    int          m_relayPort;      //中转端口（relayport 字段），就是 fileserver 现有的监听端口
    std::string  m_relayToken;     //中转令牌（token 字段），本端 N+1 条通道共用同一枚
    int          m_dataLanes;      //数据通道条数 N（lanes 字段），取值 1..kFolderMaxDataLanes
    std::wstring m_saveParentDir;  //用户选定的保存位置（根目录将建在它下面）；重试时若状态里已有根目录则忽略本字段
    //服务器长期公钥（32 字节），用于各条通道的协议加密握手；NULL 表示服务端未配置公钥、通道全程明文。
    //不接管所有权，指向的内存须在 Start 返回之后仍然有效。由调用方传入的理由同 FolderSendSessionParams
    const unsigned char* m_serverPubKey;
    //扩展点（不拥有，生命周期须长于会话）；NULL 表示单聊文件夹传输，行为不变（见 FolderTransferHooks.h）
    IFolderRecvHooks*    m_hooks;
    //通道工厂（不拥有，生命周期须长于会话）；NULL 表示各条通道都是到 fileserver 的中转连接，行为不变。
    //单聊文件夹传输在 1816 带 p2psessionid 时给出 P2P 的工厂（先等 P2P 链路，建链失败再退回中转）
    IFolderLaneTransportFactory* m_transportFactory;

    FolderRecvSessionParams()
        : m_relayPort(0)
        , m_dataLanes(1)
        , m_serverPubKey(NULL)
        , m_hooks(NULL)
        , m_transportFactory(NULL)
    {
    }
};

//一条通道的运行时上下文：中转连接、待处理帧队列，以及数据通道的工作线程。
//每条通道（含控制通道）各一个实例，由 FolderRecvSession 独占持有。
//做成独立的类而不是直接把 FolderRecvSession 当回调，是因为 FolderRelayClientHandler 的回调
//不带通道序号，多条通道共用一个 handler 就分不清帧是从哪条通道来的。
class FolderRecvLane : public FolderRelayClientHandler
{
public:
    /**
     *  @param laneIndex 本通道的序号：0 为控制通道，1..N 为数据通道
     *  @param transport 本通道的传输（接管所有权，不可为 NULL）
     */
    FolderRecvLane(int laneIndex, IFolderLaneTransport* transport);
    virtual ~FolderRecvLane();

    FolderRecvLane(const FolderRecvLane&) = delete;
    FolderRecvLane& operator=(const FolderRecvLane&) = delete;

    //本通道的传输（中转连接或 P2P 可靠通道）。由会话负责 Connect / Close。
    IFolderLaneTransport& GetClient();

    //本通道的序号（0 控制 / 1..N 数据）。
    int GetLaneIndex() const;

    /**
     *  从待处理队列里取一帧。<u>阻塞</u>直到有帧可取、或本通道被 Shutdown、或等待超时。
     *  @param out       [出参] 取出的应用帧字节
     *  @param waitMs    最长等待毫秒数
     *  @return 取到一帧返回 true；超时或已 Shutdown 返回 false
     */
    bool PopFrame(std::string& out, int waitMs);

    //唤醒并关闭队列：入队方不再阻塞、取帧方立即返回 false。幂等。
    void Shutdown();

    //本通道的中转连接是否已异常断开（FolderRelayClient 回调过 OnFolderRelayClosed）。
    bool IsPeerClosed() const;

private:
    //收到应用帧（在 FolderRelayClient 的接收线程上触发）：拷进队列即返回。
    //队列满时在此阻塞，借 TCP 接收窗口把发送端压住，这是本端的流控手段。
    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len);

    //中转连接异常断开（在 FolderRelayClient 的接收线程上触发）：置标志并唤醒工作线程。
    virtual void OnFolderRelayClosed();

private:
    const int               m_laneIndex;   //本通道序号，构造后不再变
    std::unique_ptr<IFolderLaneTransport> m_client;   //本通道的传输（中转连接或 P2P 可靠通道），生命周期与本对象相同
    mutable std::mutex      m_mtx;         //保护 m_queue / m_closed / m_peerClosed
    std::condition_variable m_notEmpty;    //队列由空变非空、或被关闭时通知取帧方
    std::condition_variable m_notFull;     //队列由满变不满、或被关闭时通知入队方
    std::deque<std::string> m_queue;       //待处理的应用帧，深度上限 kFolderRecvLaneQueueDepth
    bool                    m_closed;      //本通道是否已 Shutdown（收发双方据此立即返回）
    bool                    m_peerClosed;  //中转连接是否异常断开
};

//接收端状态机：一次「接收文件夹」的完整过程。
//
//一个实例只用一次 —— 失败之后不能再 Start()，重试请新建一个实例并传入<u>同一份</u>
//FolderReceiveState。全部方法都可在 UI 线程调用（Start 立即返回、Cancel 只置标志），
//唯独 Stop() 会等工作线程结束，故不可在本会话的回调里调用它。
class FolderRecvSession : private IFolderControlSender
{
public:
    FolderRecvSession();
    ~FolderRecvSession();

    FolderRecvSession(const FolderRecvSession&) = delete;
    FolderRecvSession& operator=(const FolderRecvSession&) = delete;

    /**
     *  启动接收。<u>立即返回</u>，建通道、收清单、建目录、收字节全部在工作线程上做，
     *  结果经 handler 的完成 / 失败回调回报。
     *  @param params  中转参数与保存位置，含义见 FolderRecvSessionParams
     *  @param state   接收状态（由上层持有并在重试时复用），非空
     *  @param handler 事件回调（非空，<u>不接管所有权</u>；其生命周期须长于本会话，
     *                 至少持续到 Stop() 返回）
     *  @return 参数校验通过并已启动工作线程返回 true；参数非法、状态或回调为空、
     *          或本实例已经启动过返回 false（此时不会有任何回调）
     *  副作用：启动 1 条会话线程与 N 条数据通道工作线程，并在其上建立 N+1 条 TCP 连接。
     */
    bool Start(const FolderRecvSessionParams& params,
               const std::shared_ptr<FolderReceiveState>& state,
               IFolderRecvSessionHandler* handler);

    /**
     *  请求取消。<u>立即返回</u>，取消在分片边界生效：正在写的那一片写完即停。
     *  按方案 8.4，取消等同于一次失败，错误码为 error_code_folder_canceled，会经
     *  OnFolderRecvFailed 回报。重复调用安全；会话已结束时调用无效果。
     */
    void Cancel();

    /**
     *  停止会话并回收全部线程与连接。<u>阻塞</u>直到全部工作线程结束，故须在 UI 线程
     *  （或任一非本会话工作线程）上调用，<u>绝不可</u>在本会话的回调里调用。幂等。
     *  析构函数会自动调用它，故上层忘记调用也不会漏掉线程回收。
     */
    void Stop();

    //会话是否仍在进行中（已 Start 且尚未走到完成 / 失败）。可在任意线程调用。
    bool IsRunning() const;

private:
    //会话线程主体：建全部通道 → 启动数据通道工作线程 → 兼作控制通道工作线程 → 收尾回报。
    void SessionProc();

    //数据通道工作线程主体：取帧 → 写盘 / 校验 / 回执。
    //@param lane 本线程负责的数据通道（不接管所有权，生命周期由会话保证）
    void LaneProc(FolderRecvLane* lane);

    //建立 1 条控制通道与 N 条数据通道。任一条失败即整体失败。
    //@return 全部建成返回 true
    bool ConnectAllLanes();

    //控制通道的处理循环：清单重组 → 建目录 → 回执 → 等 ft_complete。
    void ControlLoop();

    //处理控制通道上的一帧。@return 处理正常返回 true；已按失败收场返回 false
    bool HandleControlFrame(const std::string& frame);

    //处理数据通道上的一帧。@return 处理正常返回 true；已按失败收场返回 false
    bool HandleDataFrame(FolderRecvLane* lane, const std::string& frame);

    //清单收齐之后：复核路径安全、建目录树、算续传起点、回 ft_manifest_ack。
    //@return 成功返回 true；任一步失败时内部已调用 Fail()，返回 false
    bool OnManifestReady();

    //在保存位置下建根目录（首次改名新建 / 重试复用）。@return 成功返回 true
    bool PrepareRootDirectory();

    //按清单建完全部子目录（含空目录，以及各文件所在目录）。@return 成功返回 true
    bool CreateAllDirectories();

    //对有进度的文件做磁盘核对，使内存记录与 .part 半成品一致。@return 成功返回 true
    bool ReconcileResumeState();

    //数据通道上收到 ft_file_begin：准备好 .part 文件与增量 MD5 上下文。
    bool BeginFile(FolderRecvLane* lane, const FolderRelayFileBegin& info);

    //数据通道上收到 ft_file_chunk：判定、写盘、增量 MD5、回执。
    bool WriteChunk(FolderRecvLane* lane, const FolderRelayFileChunk& info,
                    const unsigned char* data, size_t len);

    //数据通道上收到 ft_file_end：校验字节数与 MD5，通过则去掉 .part 后缀并回 ft_file_result。
    bool FinishFile(FolderRecvLane* lane, const FolderRelayFileEnd& info);

    //关掉本通道当前打开的 .part 文件句柄，并把进度与 MD5 上下文写回接收状态。
    void CloseCurrentFile(FolderRecvLane* lane, bool storeProgress);

    //记下第一个失败原因、发 ft_abort、唤醒全部通道。可在任意工作线程调用，幂等。
    void Fail(int errcode, const std::string& errmsg);

    //在控制通道上发一个应用帧（回执 / 中止）。@return 发出返回 true
    bool SendControlFrame(const std::string& frame);

    //IFolderControlSender：经控制通道发扩展点的自定义帧
    virtual bool SendControl(const std::string& frame) override;

    //控制通道收到 ft_complete、或某个文件刚校验通过时调用：判断整体是否已完成。
    void MaybeFinish();

    //按节流间隔上报一次进度。@param force true 表示忽略节流立即上报
    void ReportProgress(const std::string& currentFile, bool force);

    //各工作线程的退出条件：已请求停止、已失败、或已整体完成。
    bool ShouldStop() const;

private:
    //每条数据通道当前正在接收的文件的运行时信息。放在会话里按通道序号索引，
    //使工作线程不必自己保管状态，也便于收尾时统一关句柄。
    struct LaneFileContext
    {
        int64_t      m_index;         //正在接收的文件序号；-1 表示本通道当前空闲
        int64_t      m_size;          //该文件的字节数（取自清单），单位：字节
        int64_t      m_written;       //本通道已写盘且已计入 MD5 的字节数，单位：字节
        std::string  m_relpath;       //该文件的相对路径（UTF-8），用于日志与进度显示
        std::wstring m_partPath;      //半成品文件的完整路径（宽字符，已含 .part 后缀）
        std::wstring m_finalPath;     //校验通过后要改成的最终完整路径（宽字符）
        std::string  m_expectMd5;     //发送端给出的内容 MD5（32 位小写十六进制），空串表示不校验
        void*        m_fileHandle;    //.part 文件的 Win32 句柄（HANDLE）；空闲时为 NULL。
                                      //用 void* 而不是 HANDLE 是为了不在本头文件里包含 windows.h
        MD5_CTX      m_md5ctx;        //本通道推进中的增量 MD5 上下文，文件结束时写回接收状态

        LaneFileContext()
            : m_index(-1)
            , m_size(0)
            , m_written(0)
            , m_fileHandle(NULL)
        {
            memset(&m_md5ctx, 0, sizeof(m_md5ctx));
        }
    };

    FolderRecvSessionParams               m_params;          //启动参数，Start 之后不再变
    std::shared_ptr<FolderReceiveState>   m_state;           //接收状态（与上层共享，重试时复用）
    IFolderRecvSessionHandler*            m_handler;         //事件回调（不拥有；生命周期由上层保证）

    std::vector<std::unique_ptr<FolderRecvLane> > m_lanes;   //下标即通道序号：0 控制通道，1..N 数据通道
    std::unique_ptr<std::thread>          m_sessionThread;   //会话线程（建通道 + 控制通道处理 + 收尾）
    std::vector<std::unique_ptr<std::thread> > m_laneThreads;//数据通道工作线程，与 m_lanes 的 1..N 一一对应
    std::vector<LaneFileContext>          m_laneFiles;       //下标即通道序号，各通道当前正在接收的文件

    FolderRelayManifestAssembler          m_assembler;       //清单分段重组器，只由控制通道线程访问
    std::vector<std::string>              m_dirs;            //清单里的目录相对路径列表（UTF-8），收齐后不再变
    std::vector<FolderRelayFileEntry>     m_files;           //清单里的文件列表（UTF-8），下标即文件序号，收齐后不再变
    std::string                           m_folderName;      //清单里的根文件夹名（UTF-8）
    int64_t                               m_totalBytes;      //清单里的总字节数，单位：字节
    std::wstring                          m_rootPath;        //本次落地的根目录完整路径（与接收状态里的同一个值）

    std::atomic<bool>                     m_started;         //是否已 Start（本实例只允许启动一次）
    std::atomic<bool>                     m_stopRequested;   //上层是否已请求停止（Stop）
    std::atomic<bool>                     m_canceled;        //用户是否已取消（Cancel）
    std::atomic<bool>                     m_failed;          //是否已记下失败原因（保证只记第一个）
    std::atomic<bool>                     m_allDone;         //是否已整体完成（ft_complete 且全部文件校验通过）
    std::atomic<bool>                     m_completeReceived;//控制通道是否已收到 ft_complete
    std::atomic<bool>                     m_manifestReady;   //清单是否已收齐并已回过 ft_manifest_ack
    std::atomic<bool>                     m_peerAborted;     //对端是否已发来 ft_abort（此时不必再回一个中止帧）
    std::atomic<bool>                     m_finished;        //会话线程是否已走完收尾并回报过最终结果

    std::mutex                            m_stopMtx;         //串行化 Stop()，使重复调用与并发调用都安全
    std::mutex                            m_failMtx;         //保护 m_errcode / m_errmsg
    int                                   m_errcode;         //第一个失败原因的错误码（error_code_folder_*）
    std::string                           m_errmsg;          //第一个失败原因的英文描述

    std::mutex                            m_reportMtx;       //串行化进度回调：多条数据通道会同时调 ReportProgress
    int64_t                               m_lastReportTick;  //上次进度回调的时刻（GetTickCount64 取值，毫秒）
};

} // namespace foldertransfer
