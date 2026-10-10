/**
 *  聊天记录导出增强的单元测试（Suite HE），HistoryExportJobTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryZipWriter.h、HistoryExportJob.h 与 HistoryExportLayout.h
 *  （docs/聊天记录管理器方案.md §12.7）：
 *    HE1 zip 往返：内存条目、跨多个读块的大文件、空条目；用更新器的 ZipReader 读回中央目录，zlib 解压后内容与
 *        CRC-32 一致，条目名按 UTF-8 正确还原中文；
 *    HE2 zip 的失败与跳过：目录不存在时打不开；源文件不存在时只跳过这一个条目，其余照写；
 *    HE3 时间范围换算、会话文件夹名（整理非法字符、重名加序号、空名兜底）；
 *    HE4 导出到文件夹：每个会话一个子文件夹，文档带 UTF-8 BOM，附件进 _files，源文件不在的计入失败；
 *    HE5 单个会话导出到文档路径：附件放在同目录的 _files；
 *    HE6 打包为 zip：条目路径「会话/文档」「会话/_files/附件」；目标目录不存在时失败且不留下文件；
 *    HE7 进度回调与取消；
 *    HE8 导出浮层的内容：控件、初始状态、切换范围后勾选区与说明的显隐、最小尺寸下按钮排得下。
 *  用临时目录。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <fstream>
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistoryZipWriter.h"
#include "../UI/BalloonHistory/HistoryExportJob.h"
#include "../UI/BalloonHistory/HistoryExportLayout.h"
#include "../../FlamingoUpdater/ZipReader.h"
#include "../third-party/zlib1.2.11/zlib.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//大文件的字节数：比 zip 写入器的读块（256 KB）大，跨三个块
const int kBigBytes = 600 * 1024;
//UTF-8 字节顺序标记
const char kBom[] = "\xEF\xBB\xBF";
const size_t kBomLen = 3;

//递归按 id 查控件
DuiControl* FindById(DuiControl* node, UINT id)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->GetCtrlId() == id)
    {
        return node;
    }
    for (const auto& child : node->Children())
    {
        DuiControl* hit = FindById(child.get(), id);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

//一个临时目录：<临时目录>he_<进程号>_<名字>\，析构时删掉
struct TempDir
{
    std::wstring m_path;

    explicit TempDir(const wchar_t* name)
    {
        wchar_t tmp[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, tmp);
        wchar_t sub[64] = { 0 };
        ::swprintf_s(sub, L"he_%u_%s\\", static_cast<unsigned int>(::GetCurrentProcessId()), name);
        m_path = std::wstring(tmp) + sub;
        Remove(m_path);
        ::CreateDirectoryW(m_path.c_str(), NULL);
    }

    ~TempDir()
    {
        Remove(m_path);
    }

    static void Remove(const std::wstring& dir)
    {
        std::wstring base = dir;
        while (!base.empty() && base[base.size() - 1] == L'\\')
        {
            base.erase(base.size() - 1);
        }
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW((base + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..")
                {
                    continue;
                }
                const std::wstring full = base + L"\\" + n;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    Remove(full);
                }
                else
                {
                    ::DeleteFileW(full.c_str());
                }
            } while (::FindNextFileW(h, &fd));
            ::FindClose(h);
        }
        ::RemoveDirectoryW(base.c_str());
    }
};

//写一个文件
void WriteAll(const std::wstring& path, const std::string& bytes)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), (std::streamsize)bytes.size());
}

//读一个文件的全部字节；不存在时为空串
std::string ReadAll(const std::wstring& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

//文件是否存在
bool Exists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

//一段有规律但不全相同的数据（压缩后明显变小，又不至于是一个字节重复）
std::string PatternBytes(int n)
{
    std::string s;
    s.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        s.push_back((char)('a' + (i * 7 + i / 13) % 26));
    }
    return s;
}

//解压一个条目并返回原始内容
std::string InflateEntry(const std::string& zip, const iupdate::ZipEntry& e)
{
    size_t offset = 0;
    std::wstring err;
    REQUIRE(iupdate::LocateEntryData((const unsigned char*)zip.data(), zip.size(), e, offset, err));
    //多留一个字节：空条目时输出缓冲区也不能是空指针（zlib 对空指针返回 Z_STREAM_ERROR）
    std::string out((size_t)e.m_uncompressedSize + 1, '\0');
    z_stream zs;
    ::memset(&zs, 0, sizeof(zs));
    const int kRawWindowBits = -15;
    REQUIRE(::inflateInit2(&zs, kRawWindowBits) == Z_OK);
    zs.next_in = (Bytef*)(zip.data() + offset);
    zs.avail_in = e.m_compressedSize;
    zs.next_out = (Bytef*)&out[0];
    zs.avail_out = (uInt)out.size();
    const int rc = ::inflate(&zs, Z_FINISH);
    const size_t produced = out.size() - zs.avail_out;
    ::inflateEnd(&zs);
    CHECK(rc == Z_STREAM_END);
    out.resize(produced);
    return out;
}

//进度回调的测试实现：记下调用次数，到第 m_cancelAt 步时要求中止（0 表示不中止）
class CountingProgress : public IHistoryExportProgress
{
public:
    explicit CountingProgress(int cancelAt)
        : m_calls(0)
        , m_lastDone(0)
        , m_lastTotal(0)
        , m_cancelAt(cancelAt)
    {
    }

    void OnExportProgress(int done, int total)
    {
        ++m_calls;
        m_lastDone = done;
        m_lastTotal = total;
    }

    bool ExportCancelled()
    {
        return m_cancelAt > 0 && m_lastDone >= m_cancelAt;
    }

    int m_calls;       //回调次数
    int m_lastDone;    //最近一次的已完成步数
    int m_lastTotal;   //最近一次的总步数
    int m_cancelAt;    //第几步之后要求中止
};

//两个会话的导出任务：会话 A 有两个附件（一个源文件不存在），会话 B 没有附件
HistoryExportJob TwoConvJob(const std::wstring& srcDir)
{
    WriteAll(srcDir + L"img.png", "PNGDATA");
    HistoryExportJob job;
    HistoryExportConvJob a;
    a.m_folderName = L"产品研发群";
    a.m_docName = L"聊天记录.html";
    a.m_docText = L"<html>你好</html>";
    a.m_messageCount = 3;
    HistoryExportAttachment img;
    img.m_source = srcDir + L"img.png";
    img.m_relPath = L"聊天记录_files/1.png";
    a.m_attachments.push_back(img);
    HistoryExportAttachment gone;
    gone.m_source = srcDir + L"gone.bin";
    gone.m_relPath = L"聊天记录_files/gone.bin";
    a.m_attachments.push_back(gone);
    job.m_convs.push_back(a);
    HistoryExportConvJob b;
    b.m_folderName = L"张三";
    b.m_docName = L"聊天记录.html";
    b.m_docText = L"<html>B</html>";
    b.m_messageCount = 2;
    job.m_convs.push_back(b);
    return job;
}

} // namespace

TEST_CASE("HE1 zip 往返：内存条目、大文件、空条目，读回内容与 CRC 一致，中文名正确", "[histexport]")
{
    TempDir dir(L"he1");
    const std::string html = PatternBytes(10000);
    const std::string big = PatternBytes(kBigBytes);
    WriteAll(dir.m_path + L"big.bin", big);
    const std::wstring zipPath = dir.m_path + L"out.zip";

    CHistoryZipWriter zip;
    REQUIRE(zip.Open(zipPath));
    REQUIRE(zip.AddData("产品研发群/聊天记录.html", html));
    REQUIRE(zip.AddFile("产品研发群/聊天记录_files/大文件.bin", dir.m_path + L"big.bin"));
    REQUIRE(zip.AddData("空.txt", std::string()));
    CHECK(zip.EntryCount() == 3);
    REQUIRE(zip.Close());
    CHECK_FALSE(zip.Failed());

    const std::string bytes = ReadAll(zipPath);
    CHECK((uint64_t)bytes.size() == zip.BytesWritten());
    //压缩生效：zip 比原始数据小得多
    CHECK(bytes.size() < html.size() + big.size());
    std::vector<iupdate::ZipEntry> entries;
    std::wstring err;
    REQUIRE(iupdate::ParseCentralDirectory((const unsigned char*)bytes.data(), bytes.size(), entries, err));
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].m_name == L"产品研发群/聊天记录.html");
    CHECK(entries[1].m_name == L"产品研发群/聊天记录_files/大文件.bin");
    CHECK(entries[2].m_name == L"空.txt");
    CHECK(entries[0].m_method == iupdate::zip_method_deflate);
    CHECK(entries[1].m_uncompressedSize == (uint32_t)kBigBytes);
    CHECK(entries[2].m_uncompressedSize == 0);

    CHECK(InflateEntry(bytes, entries[0]) == html);
    const std::string bigBack = InflateEntry(bytes, entries[1]);
    CHECK(bigBack == big);
    CHECK(entries[1].m_crc32 == (uint32_t)::crc32(0L, (const Bytef*)big.data(), (uInt)big.size()));
    CHECK(InflateEntry(bytes, entries[2]).empty());
}

TEST_CASE("HE2 zip 的失败与跳过：目录不存在打不开，源文件不存在只跳过这一个", "[histexport]")
{
    TempDir dir(L"he2");
    CHistoryZipWriter bad;
    CHECK_FALSE(bad.Open(dir.m_path + L"no_such_dir\\out.zip"));
    CHECK(bad.Failed());

    CHistoryZipWriter zip;
    REQUIRE(zip.Open(dir.m_path + L"out.zip"));
    CHECK_FALSE(zip.AddFile("a/missing.bin", dir.m_path + L"missing.bin"));
    CHECK_FALSE(zip.Failed());
    REQUIRE(zip.AddData("a/ok.txt", "ok"));
    REQUIRE(zip.Close());
    CHECK(zip.EntryCount() == 1);
    CHECK_FALSE(zip.TooLarge());
    //关闭之后再写都失败
    CHECK_FALSE(zip.AddData("late.txt", "x"));
    CHECK_FALSE(zip.Close());
}

TEST_CASE("HE3 时间范围换算与会话文件夹名", "[histexport]")
{
    CHECK(ExportRangeToTimeRange(0) == 0);
    CHECK(ExportRangeToTimeRange(5) == 5);
    CHECK(ExportRangeToTimeRange(kHistoryExportRangeFilter) == -1);
    CHECK(ExportRangeToTimeRange(-1) == -1);

    std::set<std::wstring> used;
    CHECK(ExportFolderName(L"产品研发群", used) == L"产品研发群");
    CHECK(ExportFolderName(L"产品研发群", used) == L"产品研发群 (2)");
    CHECK(ExportFolderName(L"a/b:c", used) == L"a_b_c");
    CHECK(ExportFolderName(L"", used) == L"chat");
    CHECK(ExportFolderName(L"CHAT", used) == L"CHAT (2)");
}

TEST_CASE("HE4 导出到文件夹：每个会话一个子文件夹，文档带 BOM，附件进 _files", "[histexport]")
{
    TempDir src(L"he4src");
    TempDir out(L"he4out");
    HistoryExportJob job = TwoConvJob(src.m_path);
    job.m_zip = false;
    job.m_singleFile = false;
    job.m_target = out.m_path;
    const HistoryExportResult r = RunHistoryExportJob(job, NULL);
    CHECK(r.m_ok);
    CHECK(r.m_convs == 2);
    CHECK(r.m_messages == 5);
    CHECK(r.m_attachmentsDone == 1);
    CHECK(r.m_attachmentsFailed == 1);

    const std::string doc = ReadAll(out.m_path + L"产品研发群\\聊天记录.html");
    REQUIRE(doc.size() > kBomLen);
    CHECK(doc.compare(0, kBomLen, kBom) == 0);
    CHECK(doc.find("<html>") == kBomLen);
    CHECK(ReadAll(out.m_path + L"产品研发群\\聊天记录_files\\1.png") == "PNGDATA");
    CHECK_FALSE(Exists(out.m_path + L"产品研发群\\聊天记录_files\\gone.bin"));
    CHECK(Exists(out.m_path + L"张三\\聊天记录.html"));
}

TEST_CASE("HE5 单个会话导出到文档路径：附件放在同目录的 _files", "[histexport]")
{
    TempDir src(L"he5src");
    TempDir out(L"he5out");
    HistoryExportJob job = TwoConvJob(src.m_path);
    job.m_convs.resize(1);
    job.m_convs[0].m_docName = L"消息记录_产品研发群.html";
    job.m_convs[0].m_attachments[0].m_relPath = L"消息记录_产品研发群_files/1.png";
    job.m_zip = false;
    job.m_singleFile = true;
    job.m_target = out.m_path + L"消息记录_产品研发群.html";
    const HistoryExportResult r = RunHistoryExportJob(job, NULL);
    CHECK(r.m_ok);
    CHECK(Exists(out.m_path + L"消息记录_产品研发群.html"));
    CHECK(ReadAll(out.m_path + L"消息记录_产品研发群_files\\1.png") == "PNGDATA");
    //不建会话子文件夹
    CHECK_FALSE(Exists(out.m_path + L"产品研发群"));
}

TEST_CASE("HE6 打包为 zip：条目路径按会话分目录；目标目录不存在时失败且不留文件", "[histexport]")
{
    TempDir src(L"he6src");
    TempDir out(L"he6out");
    HistoryExportJob job = TwoConvJob(src.m_path);
    job.m_zip = true;
    job.m_target = out.m_path + L"export.zip";
    const HistoryExportResult r = RunHistoryExportJob(job, NULL);
    CHECK(r.m_ok);
    CHECK(r.m_attachmentsDone == 1);
    CHECK(r.m_attachmentsFailed == 1);
    const std::string bytes = ReadAll(job.m_target);
    std::vector<iupdate::ZipEntry> entries;
    std::wstring err;
    REQUIRE(iupdate::ParseCentralDirectory((const unsigned char*)bytes.data(), bytes.size(), entries, err));
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].m_name == L"产品研发群/聊天记录.html");
    CHECK(entries[1].m_name == L"产品研发群/聊天记录_files/1.png");
    CHECK(entries[2].m_name == L"张三/聊天记录.html");
    const std::string doc = InflateEntry(bytes, entries[0]);
    CHECK(doc.compare(0, kBomLen, kBom) == 0);

    job.m_target = out.m_path + L"no_such_dir\\export.zip";
    const HistoryExportResult bad = RunHistoryExportJob(job, NULL);
    CHECK_FALSE(bad.m_ok);
    CHECK_FALSE(Exists(job.m_target));

    //没有会话时不算成功
    HistoryExportJob empty;
    empty.m_zip = true;
    empty.m_target = out.m_path + L"empty.zip";
    CHECK_FALSE(RunHistoryExportJob(empty, NULL).m_ok);
}

TEST_CASE("HE7 进度回调与取消", "[histexport]")
{
    TempDir src(L"he7src");
    TempDir out(L"he7out");
    HistoryExportJob job = TwoConvJob(src.m_path);
    job.m_singleFile = false;
    job.m_target = out.m_path;
    CountingProgress all(0);
    CHECK(RunHistoryExportJob(job, &all).m_ok);
    //两个文档 + 两个附件
    CHECK(all.m_calls == 4);
    CHECK(all.m_lastDone == 4);
    CHECK(all.m_lastTotal == 4);

    TempDir out2(L"he7out2");
    job.m_target = out2.m_path;
    CountingProgress stop(1);
    const HistoryExportResult r = RunHistoryExportJob(job, &stop);
    CHECK(r.m_cancelled);
    CHECK_FALSE(r.m_ok);
    CHECK(stop.m_calls == 1);
    CHECK_FALSE(Exists(out2.m_path + L"张三"));
}

TEST_CASE("HE8 导出浮层的内容：控件、初始状态、切换范围后的显隐、按钮排得下", "[histexport]")
{
    HistoryExportOptions opts;
    std::unique_ptr<DuiControl> root = BuildHistoryExportPopupContent(opts);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, kHistoryExportPopupW, kHistoryExportPopupH });
    CHECK(root->GetCtrlId() == (UINT)kIdExpRoot);
    const UINT ids[] = { kIdExpScopeCurrent, kIdExpScopeSelected, kIdExpConvScroll, kIdExpConvInfo, kIdExpRangeCombo,
                         kIdExpFmtHtml, kIdExpFmtText, kIdExpImages, kIdExpFiles, kIdExpZip, kIdExpOk, kIdExpCancel };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id = " << ids[i]);
        CHECK(FindById(root.get(), ids[i]) != nullptr);
    }
    //默认：当前会话、网页、带图片、不带文件、不打包、按当前筛选条件
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdExpScopeCurrent))->GetVariant() == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdExpFmtHtml))->GetVariant() == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdExpImages))->IsChecked());
    CHECK_FALSE(dynamic_cast<DuiButton*>(FindById(root.get(), kIdExpFiles))->IsChecked());
    CHECK_FALSE(dynamic_cast<DuiButton*>(FindById(root.get(), kIdExpZip))->IsChecked());
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(FindById(root.get(), kIdExpRangeCombo));
    REQUIRE(combo != nullptr);
    CHECK(combo->GetCount() == kHistoryExportRangeCount);
    CHECK(combo->GetCurSel() == kHistoryExportRangeFilter);
    CHECK_FALSE(FindById(root.get(), kIdExpConvBox)->IsVisible());
    CHECK(FindById(root.get(), kIdExpCurrentHint)->IsVisible());

    //切到「选择会话」：勾选区出现、说明隐藏，列表有高度
    opts.m_scope = kHistoryExportScopeSelected;
    opts.m_html = false;
    ApplyHistoryExportPopupState(root.get(), opts);
    CHECK(FindById(root.get(), kIdExpConvBox)->IsVisible());
    CHECK_FALSE(FindById(root.get(), kIdExpCurrentHint)->IsVisible());
    const RECT list = FindById(root.get(), kIdExpConvScroll)->GetRect();
    CHECK(list.bottom - list.top > 0);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdExpScopeSelected))->GetVariant() == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdExpFmtText))->GetVariant() == DuiButton::Variant::Primary);

    //两个按钮都在浮层里、宽度为正
    const RECT rootRc = root->GetRect();
    for (UINT id = kIdExpOk; id <= (UINT)kIdExpCancel; ++id)
    {
        INFO("button = " << id);
        const RECT rc = FindById(root.get(), id)->GetRect();
        CHECK(rc.right - rc.left > 0);
        CHECK(rc.bottom <= rootRc.bottom);
        CHECK(rc.right <= rootRc.right);
    }
}
