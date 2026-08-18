/**
 *  单聊「接收文件夹」接收端状态机的纯逻辑单元测试（Suite FRS），FolderRecvSessionTests.cpp
 *
 *  FolderRecvSession 里能脱离网络与磁盘验证的部分都集中在 folderrecv 命名空间与
 *  FolderReceiveState 上，本文件覆盖的就是它们：
 *    · 根目录重名自动改名（FRS1~FRS4）—— 首选原名，被占用则依次 "xxx(2)" / "xxx(3)"，
 *      候选耗尽时如实失败；路径拼接不受父目录末尾分隔符影响。
 *    · 相对路径落地前的安全校验（FRS5~FRS8）—— 这是防目录穿越的关键：".."、绝对路径、
 *      盘符前缀、保留设备名一律拒绝，正常路径通过校验并把正斜杠换成本地的反斜杠。
 *    · 续传起点计算（FRS9~FRS12）—— 全新文件从 0、半成品从 .part 长度续、已完成的文件
 *      跳过，以及磁盘与内存记录对不上时退回从零。
 *    · 乱序 / 重复分片的判定（FRS13~FRS18）—— 正好接续、整片重复、部分重叠、留下空洞、
 *      越过文件末尾、描述符为负数各锁一条。
 *    · 接收状态对象（FRS19~FRS22）—— 按清单初始化与核对、进度与完成计数、续传位点表。
 *
 *  真正碰 socket 与磁盘的部分（建通道、建目录、写盘、改名）不在本文件覆盖范围，靠后续
 *  两账号真机联调验证。本文件不起任何连接，也不在磁盘上留下任何文件。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "catch_amalgamated.hpp"

#include <set>
#include <string>
#include <vector>

#include "../Feature/FolderTransfer/FolderRecvSession.h"

using namespace foldertransfer;

namespace {

//路径存在性探测的测试替身：把「哪些路径已被占用」放在内存里，不必真去磁盘上造同名目录。
class FakePathProbe : public folderrecv::IFolderPathProbe
{
public:
    //登记一条已被占用的路径。
    void AddExisting(const std::wstring& path)
    {
        m_existing.insert(path);
    }

    virtual bool PathExists(const std::wstring& path)
    {
        return m_existing.find(path) != m_existing.end();
    }

private:
    std::set<std::wstring> m_existing;   //被视为已存在的路径集合
};

//造一条清单文件条目，省去测试里反复填结构体。
FolderRelayFileEntry MakeEntry(const std::string& relpath, int64_t size)
{
    FolderRelayFileEntry entry;
    entry.m_relpath = relpath;
    entry.m_size    = size;
    return entry;
}

//造一条接收记录。@param hashing 传 true 表示 MD5 上下文已与已落盘字节数同步
FolderRecvFileState MakeRecord(int64_t size, int64_t written, bool completed, bool hashing)
{
    FolderRecvFileState record;
    record.m_size         = size;
    record.m_bytesWritten = written;
    record.m_completed    = completed;
    record.m_hashing      = hashing;
    return record;
}

} // namespace

// ==================== 根目录重名自动改名 ====================

TEST_CASE("FRS1 根目录改名：目标位置没有同名目录时直接用原名")
{
    FakePathProbe probe;
    std::wstring  chosen;

    REQUIRE(folderrecv::ChooseUniqueRootFolderPath(L"D:\\下载", L"报告", probe, chosen));
    REQUIRE(chosen == L"D:\\下载\\报告");
}

TEST_CASE("FRS2 根目录改名：重名时依次改成 xxx(2) / xxx(3)")
{
    FakePathProbe probe;
    probe.AddExisting(L"D:\\下载\\报告");

    std::wstring chosen;
    REQUIRE(folderrecv::ChooseUniqueRootFolderPath(L"D:\\下载", L"报告", probe, chosen));
    REQUIRE(chosen == L"D:\\下载\\报告(2)");

    //再占掉 (2)，下一个候选应当是 (3)，而不是跳号或回到原名
    probe.AddExisting(L"D:\\下载\\报告(2)");
    REQUIRE(folderrecv::ChooseUniqueRootFolderPath(L"D:\\下载", L"报告", probe, chosen));
    REQUIRE(chosen == L"D:\\下载\\报告(3)");

    //中间那个被占、后面那个空着时，仍然取第一个空位
    probe.AddExisting(L"D:\\下载\\报告(3)");
    probe.AddExisting(L"D:\\下载\\报告(5)");
    REQUIRE(folderrecv::ChooseUniqueRootFolderPath(L"D:\\下载", L"报告", probe, chosen));
    REQUIRE(chosen == L"D:\\下载\\报告(4)");
}

TEST_CASE("FRS3 根目录改名：候选全被占用、或参数为空时如实失败")
{
    FakePathProbe probe;
    probe.AddExisting(L"C:\\tmp\\folder");
    for (int i = kFolderRecvRenameSuffixStart;
         i < kFolderRecvRenameSuffixStart + kFolderRecvMaxRenameAttempts; ++i)
    {
        std::wstring occupied = L"C:\\tmp\\folder(";
        occupied += std::to_wstring(i);
        occupied += L")";
        probe.AddExisting(occupied);
    }

    std::wstring chosen = L"脏值，必须被清掉";
    REQUIRE_FALSE(folderrecv::ChooseUniqueRootFolderPath(L"C:\\tmp", L"folder", probe, chosen));
    REQUIRE(chosen.empty());

    FakePathProbe empty;
    REQUIRE_FALSE(folderrecv::ChooseUniqueRootFolderPath(L"", L"folder", empty, chosen));
    REQUIRE_FALSE(folderrecv::ChooseUniqueRootFolderPath(L"C:\\tmp", L"", empty, chosen));
}

TEST_CASE("FRS4 路径拼接与候选名构造：父目录末尾的分隔符不影响结果")
{
    REQUIRE(folderrecv::JoinFolderPath(L"D:\\下载", L"报告") == L"D:\\下载\\报告");
    REQUIRE(folderrecv::JoinFolderPath(L"D:\\下载\\", L"报告") == L"D:\\下载\\报告");
    REQUIRE(folderrecv::JoinFolderPath(L"D:/下载//", L"报告") == L"D:/下载\\报告");
    REQUIRE(folderrecv::JoinFolderPath(L"", L"报告") == L"报告");

    REQUIRE(folderrecv::BuildIndexedFolderName(L"报告", 1) == L"报告");
    REQUIRE(folderrecv::BuildIndexedFolderName(L"报告", 2) == L"报告(2)");
    REQUIRE(folderrecv::BuildIndexedFolderName(L"报告", 9) == L"报告(9)");
    //目录没有扩展名，后缀一律加在末尾，不插到点号前面
    REQUIRE(folderrecv::BuildIndexedFolderName(L"a.b", 2) == L"a.b(2)");
    //序号小于 1 按 1 处理，不产出 "(0)" 或 "(-1)" 这种名字
    REQUIRE(folderrecv::BuildIndexedFolderName(L"报告", 0) == L"报告");
}

// ==================== 相对路径落地前的安全校验 ====================

TEST_CASE("FRS5 落地校验：正常的多层相对路径通过，并换成本地的反斜杠写法")
{
    std::wstring local;
    REQUIRE(folderrecv::ResolveLocalRelativePath("sub/dir/a.txt", local) == kFolderPathOk);
    REQUIRE(local == L"sub\\dir\\a.txt");

    //中文与空格照常通过
    REQUIRE(folderrecv::ResolveLocalRelativePath("子目录/我的 报告.docx", local) == kFolderPathOk);
    REQUIRE(local == L"子目录\\我的 报告.docx");

    //规范化顺带做掉：重复分隔符合并、首尾分隔符去掉
    REQUIRE(folderrecv::ResolveLocalRelativePath("sub//dir/", local) == kFolderPathOk);
    REQUIRE(local == L"sub\\dir");
}

TEST_CASE("FRS6 落地校验：含 \"..\" 的路径一律拒绝，绝不落地")
{
    std::wstring local = L"脏值，必须被清掉";

    REQUIRE(folderrecv::ResolveLocalRelativePath("../outside.txt", local) == kFolderPathDotSegment);
    REQUIRE(local.empty());
    REQUIRE(folderrecv::ResolveLocalRelativePath("sub/../../outside.txt", local) == kFolderPathDotSegment);
    REQUIRE(folderrecv::ResolveLocalRelativePath("sub\\..\\outside.txt", local) == kFolderPathDotSegment);
    REQUIRE(folderrecv::ResolveLocalRelativePath("..", local) == kFolderPathDotSegment);
}

TEST_CASE("FRS7 落地校验：绝对路径、盘符前缀、保留设备名、空路径一律拒绝")
{
    std::wstring local;

    REQUIRE(folderrecv::ResolveLocalRelativePath("/etc/passwd", local) == kFolderPathRooted);
    REQUIRE(folderrecv::ResolveLocalRelativePath("\\\\server\\share\\a.txt", local) == kFolderPathRooted);
    REQUIRE(folderrecv::ResolveLocalRelativePath("C:\\windows\\system32\\a.dll", local) == kFolderPathDrive);
    REQUIRE(folderrecv::ResolveLocalRelativePath("sub/CON", local) == kFolderPathReservedName);
    REQUIRE(folderrecv::ResolveLocalRelativePath("a<b.txt", local) == kFolderPathIllegalChar);
    REQUIRE(folderrecv::ResolveLocalRelativePath("", local) == kFolderPathEmpty);

    //拒绝的判定与 FolderManifest 的公共判断完全一致（收发两端共用同一份口径）
    REQUIRE(folderrecv::ResolveLocalRelativePath("../x", local) == CheckFolderRelativePath("../x"));
}

TEST_CASE("FRS8 相对路径的父目录提取")
{
    REQUIRE(folderrecv::ParentOfRelativePath("sub/dir/a.txt") == "sub/dir");
    REQUIRE(folderrecv::ParentOfRelativePath("sub/a.txt") == "sub");
    //位于根目录下的文件没有父目录
    REQUIRE(folderrecv::ParentOfRelativePath("a.txt").empty());
    REQUIRE(folderrecv::ParentOfRelativePath("").empty());
}

// ==================== 续传起点计算 ====================

TEST_CASE("FRS9 续传起点：全新文件从 0 开始")
{
    //首次传输：既没有落盘字节，磁盘上也没有 .part 半成品
    const FolderRecvFileState fresh = MakeRecord(1024, 0, false, false);
    REQUIRE(folderrecv::ComputeFileResumeOffset(fresh, kFolderRecvNoPartFile) == 0);

    //磁盘上有个空的 .part（上次刚建好就断了）同样从 0 开始
    REQUIRE(folderrecv::ComputeFileResumeOffset(fresh, 0) == 0);

    //0 字节文件也从 0 开始
    const FolderRecvFileState emptyFile = MakeRecord(0, 0, false, false);
    REQUIRE(folderrecv::ComputeFileResumeOffset(emptyFile, kFolderRecvNoPartFile) == 0);
}

TEST_CASE("FRS10 续传起点：半成品从 .part 的长度接着传")
{
    //正常的续传场景：内存记录的已落盘字节数与 .part 文件长度一致
    const FolderRecvFileState half = MakeRecord(1000000, 524288, false, true);
    REQUIRE(folderrecv::ComputeFileResumeOffset(half, 524288) == 524288);

    //上一次多写了一段还没来得及计入记录：以记录为准（多出来的尾巴由打开文件时截断掉），
    //绝不能取 .part 的实际长度，否则增量 MD5 会漏算那一段
    REQUIRE(folderrecv::ComputeFileResumeOffset(half, 524288 + 4096) == 524288);
}

TEST_CASE("FRS11 续传起点：已完成的文件整个跳过")
{
    const FolderRecvFileState done = MakeRecord(1000000, 1000000, true, true);
    //返回值等于文件大小，发送端据此知道这个文件不必再传
    REQUIRE(folderrecv::ComputeFileResumeOffset(done, kFolderRecvNoPartFile) == 1000000);
    //已完成时不再看磁盘上的 .part（它早就被改成最终名字了）
    REQUIRE(folderrecv::ComputeFileResumeOffset(done, 123) == 1000000);
}

TEST_CASE("FRS12 续传起点：内存记录与磁盘对不上时退回从 0 重传")
{
    //磁盘上的半成品比内存记录还短：MD5 上下文覆盖的字节比磁盘上真实存在的多，只能重来
    const FolderRecvFileState half = MakeRecord(1000000, 524288, false, true);
    REQUIRE(folderrecv::ComputeFileResumeOffset(half, 100) == 0);

    //.part 文件整个不见了
    REQUIRE(folderrecv::ComputeFileResumeOffset(half, kFolderRecvNoPartFile) == 0);

    //MD5 上下文没跟上已落盘字节数：无法续算
    const FolderRecvFileState noHash = MakeRecord(1000000, 524288, false, false);
    REQUIRE(folderrecv::ComputeFileResumeOffset(noHash, 524288) == 0);

    //记录自相矛盾：已落盘字节数超过了清单里的文件大小
    const FolderRecvFileState broken = MakeRecord(1000, 2000, false, true);
    REQUIRE(folderrecv::ComputeFileResumeOffset(broken, 2000) == 0);
}

// ==================== 乱序 / 重复分片的判定 ====================

TEST_CASE("FRS13 分片判定：正好接在已落盘字节之后，整片收下")
{
    int64_t skip = -1;
    REQUIRE(folderrecv::ClassifyFolderChunk(0, 0, 4096, 8192, skip) == folderrecv::kFolderChunkAccept);
    REQUIRE(skip == 0);

    REQUIRE(folderrecv::ClassifyFolderChunk(4096, 4096, 4096, 8192, skip) == folderrecv::kFolderChunkAccept);
    REQUIRE(skip == 0);

    //恰好补齐到文件末尾也是合法的
    REQUIRE(folderrecv::ClassifyFolderChunk(8000, 8000, 192, 8192, skip) == folderrecv::kFolderChunkAccept);
    REQUIRE(skip == 0);
}

TEST_CASE("FRS14 分片判定：整片都已落过盘时判为重复，丢弃不写")
{
    int64_t skip = -1;
    //上一片的原样重发
    REQUIRE(folderrecv::ClassifyFolderChunk(8192, 4096, 4096, 8192, skip) == folderrecv::kFolderChunkDuplicate);
    REQUIRE(skip == 0);

    //整个文件都收完之后又来一片
    REQUIRE(folderrecv::ClassifyFolderChunk(8192, 0, 8192, 8192, skip) == folderrecv::kFolderChunkDuplicate);

    //长度为 0 的空片落在已落盘区间内，同样按重复处理
    REQUIRE(folderrecv::ClassifyFolderChunk(4096, 1024, 0, 8192, skip) == folderrecv::kFolderChunkDuplicate);
}

TEST_CASE("FRS15 分片判定：与已落盘部分部分重叠时，跳过重叠的开头收下尾部")
{
    int64_t skip = -1;
    //已落盘 6144 字节，来料覆盖 [4096, 8192)：开头 2048 字节是旧的，其后 2048 字节是新的
    REQUIRE(folderrecv::ClassifyFolderChunk(6144, 4096, 4096, 8192, skip) == folderrecv::kFolderChunkAccept);
    REQUIRE(skip == 2048);

    //只多出一个字节也照样收
    REQUIRE(folderrecv::ClassifyFolderChunk(8191, 0, 8192, 8192, skip) == folderrecv::kFolderChunkAccept);
    REQUIRE(skip == 8191);
}

TEST_CASE("FRS16 分片判定：起点越过已落盘字节会留下空洞，一律拒绝")
{
    int64_t skip = -1;
    //已落盘 4096 字节却来了 [8192, ...) 这一片，中间 4096 字节没有着落
    REQUIRE(folderrecv::ClassifyFolderChunk(4096, 8192, 4096, 65536, skip) == folderrecv::kFolderChunkGap);
    REQUIRE(skip == 0);

    //只差一个字节的空洞同样拒绝：增量 MD5 无法跳着算
    REQUIRE(folderrecv::ClassifyFolderChunk(4096, 4097, 16, 65536, skip) == folderrecv::kFolderChunkGap);
}

TEST_CASE("FRS17 分片判定：末尾越过清单给出的文件大小时拒绝")
{
    int64_t skip = -1;
    REQUIRE(folderrecv::ClassifyFolderChunk(4096, 4096, 8192, 8192, skip) == folderrecv::kFolderChunkOverrun);
    REQUIRE(skip == 0);

    //越界优先于空洞判定：两种毛病同时存在时给出稳定的结论
    REQUIRE(folderrecv::ClassifyFolderChunk(0, 9000, 4096, 8192, skip) == folderrecv::kFolderChunkOverrun);

    //0 字节文件收到任何非空片都算越界
    REQUIRE(folderrecv::ClassifyFolderChunk(0, 0, 1, 0, skip) == folderrecv::kFolderChunkOverrun);
}

TEST_CASE("FRS18 分片判定：描述符里出现负数时拒绝")
{
    int64_t skip = -1;
    REQUIRE(folderrecv::ClassifyFolderChunk(0, -1, 4096, 8192, skip) == folderrecv::kFolderChunkBadArgument);
    REQUIRE(skip == 0);
    REQUIRE(folderrecv::ClassifyFolderChunk(0, 0, -4096, 8192, skip) == folderrecv::kFolderChunkBadArgument);
    REQUIRE(folderrecv::ClassifyFolderChunk(-1, 0, 4096, 8192, skip) == folderrecv::kFolderChunkBadArgument);
    REQUIRE(folderrecv::ClassifyFolderChunk(0, 0, 4096, -1, skip) == folderrecv::kFolderChunkBadArgument);
}

// ==================== 接收状态对象 ====================

TEST_CASE("FRS19 接收状态：按清单初始化，进度与完成计数随写入推进")
{
    std::vector<FolderRelayFileEntry> files;
    files.push_back(MakeEntry("a.txt", 1000));
    files.push_back(MakeEntry("sub/b.bin", 2000));

    FolderReceiveState state;
    //首次调用一定是「整体重置」，故返回 false
    REQUIRE_FALSE(state.SyncWithManifest(files));
    REQUIRE(state.GetFileCount() == 2);
    REQUIRE(state.GetReceivedBytes() == 0);
    REQUIRE(state.GetCompletedFileCount() == 0);

    FolderRecvFileState record;
    REQUIRE(state.GetFileRecord(1, record));
    REQUIRE(record.m_relpath == "sub/b.bin");
    REQUIRE(record.m_size == 2000);
    REQUIRE_FALSE(state.GetFileRecord(2, record));
    REQUIRE_FALSE(state.GetFileRecord(-1, record));

    //写进度：已落盘字节数是增量维护的，不会把同一个文件算两遍
    state.StoreFileBytes(0, 400);
    state.StoreFileBytes(0, 1000);
    state.StoreFileBytes(1, 500);
    REQUIRE(state.GetReceivedBytes() == 1500);

    //标记完成：剩下的字节自动补齐，完成计数加一，重复标记不会算两次
    state.MarkFileCompleted(1);
    state.MarkFileCompleted(1);
    REQUIRE(state.GetCompletedFileCount() == 1);
    REQUIRE(state.GetReceivedBytes() == 3000);

    //根目录路径：首次为空，记下之后可复用
    REQUIRE_FALSE(state.HasRootPath());
    state.SetRootPath(L"D:\\下载\\报告(2)");
    REQUIRE(state.HasRootPath());
    REQUIRE(state.GetRootPath() == L"D:\\下载\\报告(2)");
}

TEST_CASE("FRS20 接收状态：清单与上次一致才沿用进度，否则整体重置")
{
    std::vector<FolderRelayFileEntry> files;
    files.push_back(MakeEntry("a.txt", 1000));
    files.push_back(MakeEntry("b.txt", 2000));

    FolderReceiveState state;
    REQUIRE_FALSE(state.SyncWithManifest(files));
    state.StoreFileBytes(0, 600);

    //重试：清单一模一样，沿用已有进度
    REQUIRE(state.SyncWithManifest(files));
    REQUIRE(state.GetReceivedBytes() == 600);

    //文件大小变了：内容已经不是同一份，旧进度必须丢弃
    std::vector<FolderRelayFileEntry> resized = files;
    resized[1].m_size = 2048;
    REQUIRE_FALSE(state.SyncWithManifest(resized));
    REQUIRE(state.GetReceivedBytes() == 0);

    //文件数变了同样整体重置
    state.StoreFileBytes(0, 100);
    std::vector<FolderRelayFileEntry> shorter;
    shorter.push_back(MakeEntry("a.txt", 1000));
    REQUIRE_FALSE(state.SyncWithManifest(shorter));
    REQUIRE(state.GetFileCount() == 1);
    REQUIRE(state.GetReceivedBytes() == 0);
}

TEST_CASE("FRS21 接收状态：续传位点表只列偏移非 0 的文件")
{
    std::vector<FolderRelayFileEntry> files;
    files.push_back(MakeEntry("a.txt", 1000));
    files.push_back(MakeEntry("b.txt", 2000));
    files.push_back(MakeEntry("c.txt", 3000));

    FolderReceiveState state;
    state.SyncWithManifest(files);

    //首次传输：全部从 0 开始，位点表是空的（协议约定未列出的一律按 0 处理）
    std::vector<FolderRelayResumePoint> points;
    state.BuildResumePoints(points);
    REQUIRE(points.empty());

    //一个半成品、一个已完成、一个还没开始
    state.StoreFileBytes(1, 512);
    state.MarkFileCompleted(2);

    state.BuildResumePoints(points);
    REQUIRE(points.size() == 2);
    REQUIRE(points[0].m_index == 1);
    REQUIRE(points[0].m_offset == 512);
    //已完成的文件列出「偏移 == 文件大小」，发送端据此整个跳过
    REQUIRE(points[1].m_index == 2);
    REQUIRE(points[1].m_offset == 3000);
}

TEST_CASE("FRS22 接收状态：ResetFile 把一个文件退回从零，Reset 清空全部")
{
    std::vector<FolderRelayFileEntry> files;
    files.push_back(MakeEntry("a.txt", 1000));
    files.push_back(MakeEntry("b.txt", 2000));

    FolderReceiveState state;
    state.SyncWithManifest(files);
    state.SetRootPath(L"D:\\下载\\报告");
    state.StoreFileBytes(0, 300);
    state.MarkFileCompleted(1);
    REQUIRE(state.GetReceivedBytes() == 2300);
    REQUIRE(state.GetCompletedFileCount() == 1);

    //把已完成的那个退回从零：完成计数与已落盘字节数都要跟着回退
    state.ResetFile(1);
    REQUIRE(state.GetCompletedFileCount() == 0);
    REQUIRE(state.GetReceivedBytes() == 300);

    FolderRecvFileState record;
    REQUIRE(state.GetFileRecord(1, record));
    REQUIRE(record.m_bytesWritten == 0);
    REQUIRE_FALSE(record.m_completed);
    REQUIRE_FALSE(record.m_hashing);

    //整体清空：根目录与全部记录一起没
    state.Reset();
    REQUIRE_FALSE(state.HasRootPath());
    REQUIRE(state.GetFileCount() == 0);
    REQUIRE(state.GetReceivedBytes() == 0);
}
