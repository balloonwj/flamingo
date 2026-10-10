/**
 *  拖出数据对象单元测试（Suite SDD，标签 [sharedrag]），ShareDragDataTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 5 步；不用鼠标，以程序方式向外壳的放置目标放置）：
 *    - SDD1 异步放置：放置调用很快返回，委托方在放置开始时被调用一次，假的「下载」在另一条线程上稍后才写出文件，
 *           外壳等文件出现后照常复制：目录、子目录、文件与内容都正确；复制结束后放置方调用了 EndOperation；
 *    - SDD2 不支持异步的放置方（在 UI 线程上直接取内容、读流）：读的时候边等边抽消息，文件写出后读到完整内容；
 *    - SDD3 下载失败：读流返回失败；没有委托方时同样失败；
 *    - SDD4 描述表：条目数、名字、目录属性、大小、修改时间；条目不是文件时取内容回 DV_E_LINDEX；
 *    - SDD5 最后一个引用释放时临时目录被删除。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../UI/ShareDragData.h"
#include "../Engine/ShareWinPath.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的临时目录名（每个用例开始前整个删掉）
const wchar_t* const kSddTestDir = L"sdd_test";

//假的「下载」在开始后等多久才写出文件，单位：毫秒（要明显长于外壳第一次放置时的初始化耗时，实测约 500 毫秒）
const DWORD kFakeDownloadDelayMs = 2000;

//等异步复制结束的最长时间，单位：毫秒
const DWORD kCopyWaitMs = 20000;

//测试文件的内容长度
const size_t kFileABytes = 5000;

//修改时间（Unix 毫秒）
const int64_t kMtime = 1700000000000LL;

//本套用例的临时目录（每次调用都先清空再建）
std::wstring FreshBase()
{
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSddTestDir;
    sharetestfs::DeleteTree(base);
    sharetestfs::MakeDirs(base);
    return base;
}

ShareDragItem Item(const wchar_t* dropName, bool isDir, int64_t size)
{
    ShareDragItem it;
    it.m_dropName = dropName;
    it.m_isDir = isDir;
    it.m_size = size;
    it.m_mtime = kMtime;
    return it;
}

//pack、pack\a.txt、pack\sub、pack\sub\b.txt
std::vector<ShareDragItem> SampleItems()
{
    std::vector<ShareDragItem> v;
    v.push_back(Item(L"pack", true, 0));
    v.push_back(Item(L"pack\\a.txt", false, static_cast<int64_t>(kFileABytes)));
    v.push_back(Item(L"pack\\sub", true, 0));
    v.push_back(Item(L"pack\\sub\\b.txt", false, static_cast<int64_t>(kFileABytes / 2)));
    return v;
}

/**
 *  假的委托方：放置开始时在另一条线程上稍后写出条目表里的文件（模拟下载），或者直接报告失败。
 */
class FakeDelegate : public IShareDragDelegate
{
public:
    explicit FakeDelegate(bool fail)
        : m_fail(fail)
        , m_calls(0)
    {
    }

    ~FakeDelegate()
    {
        if (m_worker.joinable())
            m_worker.join();
    }

    virtual bool OnDragDownloadStart(const std::shared_ptr<CShareDragJob>& job) override
    {
        ++m_calls;
        m_worker = std::thread(&FakeDelegate::Download, this, job);
        return true;
    }

    std::atomic<int> m_calls;   //被调用的次数

private:
    void Download(std::shared_ptr<CShareDragJob> job)
    {
        ::Sleep(kFakeDownloadDelayMs);
        if (m_fail)
        {
            job->MarkFinished(1);
            return;
        }
        const std::vector<ShareDragItem>& items = job->GetItems();
        for (size_t i = 0; i < items.size(); ++i)
        {
            const std::wstring path = job->GetTempDir() + L"\\" + items[i].m_dropName;
            if (items[i].m_isDir)
                sharetestfs::MakeDirs(path);
            else
                sharetestfs::WriteFileBytes(path, static_cast<size_t>(items[i].m_size));
        }
        job->MarkFinished(0);
    }

    bool        m_fail;    //为真时不写文件、直接报告失败
    std::thread m_worker;  //模拟下载的线程
};

//本线程上处理消息，直到条件成立或超时
bool PumpUntil(const std::atomic<bool>* flag, const CShareDragJob* job, DWORD waitMs)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < waitMs)
    {
        if ((flag != NULL && flag->load()) || (job != NULL && job->IsOperationEnded()))
            return true;
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(1);
    }
    return false;
}

int64_t FileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!::GetFileAttributesExW(ShareToLongPath(path).c_str(), GetFileExInfoStandard, &d))
        return -1;
    return (static_cast<int64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

//取 FILECONTENTS 的格式
FORMATETC ContentsFormat(LONG index)
{
    FORMATETC fe;
    fe.cfFormat = static_cast<CLIPFORMAT>(::RegisterClipboardFormatW(CFSTR_FILECONTENTS));
    fe.ptd = NULL;
    fe.dwAspect = DVASPECT_CONTENT;
    fe.lindex = index;
    fe.tymed = TYMED_ISTREAM;
    return fe;
}

//本用例的 COM 初始化
class OleScope
{
public:
    OleScope()
        : m_hr(::OleInitialize(NULL))
    {
    }

    ~OleScope()
    {
        if (SUCCEEDED(m_hr))
            ::OleUninitialize();
    }

    HRESULT m_hr;   //OleInitialize 的结果
};

} // namespace

TEST_CASE("SDD1 async drop into a folder", "[fileshare][sharedrag]")
{
    OleScope ole;
    const std::wstring base = FreshBase();
    const std::wstring target = base + L"\\target";
    REQUIRE(sharetestfs::MakeDirs(target));
    FakeDelegate delegate(false);
    std::shared_ptr<CShareDragJob> job(new CShareDragJob(base + L"\\temp", SampleItems()));
    IDataObject* data = CreateShareDragDataObject(job, &delegate);

    REQUIRE(SUCCEEDED(ShareDropOnFolder(data, target)));
    //异步：放置调用不等下载结束就返回，此时下载已经开始
    CHECK(delegate.m_calls.load() == 1);
    CHECK_FALSE(job->IsFinished());

    CHECK(PumpUntil(NULL, job.get(), kCopyWaitMs));
    CHECK(delegate.m_calls.load() == 1);
    CHECK(FileSize(target + L"\\pack\\a.txt") == static_cast<int64_t>(kFileABytes));
    CHECK(FileSize(target + L"\\pack\\sub\\b.txt") == static_cast<int64_t>(kFileABytes / 2));
    const DWORD subAttrs = ::GetFileAttributesW((target + L"\\pack\\sub").c_str());
    CHECK((subAttrs != INVALID_FILE_ATTRIBUTES && (subAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0));
    data->Release();
    job.reset();
    sharetestfs::DeleteTree(base);
}

TEST_CASE("SDD2 synchronous read pumps messages until the file arrives", "[fileshare][sharedrag]")
{
    OleScope ole;
    const std::wstring base = FreshBase();
    FakeDelegate delegate(false);
    std::shared_ptr<CShareDragJob> job(new CShareDragJob(base + L"\\temp", SampleItems()));
    IDataObject* data = CreateShareDragDataObject(job, &delegate);

    FORMATETC fe = ContentsFormat(1);
    STGMEDIUM sm;
    REQUIRE(data->GetData(&fe, &sm) == S_OK);
    REQUIRE(sm.tymed == TYMED_ISTREAM);
    CHECK(delegate.m_calls.load() == 1);
    std::vector<char> buf(kFileABytes + 1);
    ULONG got = 0;
    const HRESULT hr = sm.pstm->Read(&buf[0], static_cast<ULONG>(buf.size()), &got);
    CHECK(hr == S_FALSE);
    CHECK(got == kFileABytes);
    STATSTG st;
    REQUIRE(sm.pstm->Stat(&st, STATFLAG_NONAME) == S_OK);
    CHECK(st.cbSize.QuadPart == kFileABytes);
    ::ReleaseStgMedium(&sm);
    data->Release();
    job.reset();
    sharetestfs::DeleteTree(base);
}

TEST_CASE("SDD3 failed download makes reads fail", "[fileshare][sharedrag]")
{
    OleScope ole;
    const std::wstring base = FreshBase();
    {
        FakeDelegate delegate(true);
        std::shared_ptr<CShareDragJob> job(new CShareDragJob(base + L"\\temp", SampleItems()));
        IDataObject* data = CreateShareDragDataObject(job, &delegate);
        FORMATETC fe = ContentsFormat(1);
        STGMEDIUM sm;
        REQUIRE(data->GetData(&fe, &sm) == S_OK);
        char buf[16];
        ULONG got = 0;
        CHECK(FAILED(sm.pstm->Read(buf, sizeof(buf), &got)));
        CHECK(got == 0);
        ::ReleaseStgMedium(&sm);
        data->Release();
    }
    //没有委托方：放置一开始就标记失败
    std::shared_ptr<CShareDragJob> job(new CShareDragJob(base + L"\\temp2", SampleItems()));
    IDataObject* data = CreateShareDragDataObject(job, NULL);
    FORMATETC fe = ContentsFormat(3);
    STGMEDIUM sm;
    REQUIRE(data->GetData(&fe, &sm) == S_OK);
    CHECK(job->IsFinished());
    CHECK(job->GetCode() != 0);
    char buf[16];
    ULONG got = 0;
    CHECK(FAILED(sm.pstm->Read(buf, sizeof(buf), &got)));
    ::ReleaseStgMedium(&sm);
    data->Release();
    sharetestfs::DeleteTree(base);
}

TEST_CASE("SDD4 file group descriptor", "[fileshare][sharedrag]")
{
    OleScope ole;
    std::shared_ptr<CShareDragJob> job(new CShareDragJob(std::wstring(), SampleItems()));
    IDataObject* data = CreateShareDragDataObject(job, NULL);
    FORMATETC fe;
    fe.cfFormat = static_cast<CLIPFORMAT>(::RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
    fe.ptd = NULL;
    fe.dwAspect = DVASPECT_CONTENT;
    fe.lindex = -1;
    fe.tymed = TYMED_HGLOBAL;
    CHECK(data->QueryGetData(&fe) == S_OK);
    STGMEDIUM sm;
    REQUIRE(data->GetData(&fe, &sm) == S_OK);
    const FILEGROUPDESCRIPTORW* group = static_cast<const FILEGROUPDESCRIPTORW*>(::GlobalLock(sm.hGlobal));
    REQUIRE(group->cItems == 4);
    CHECK(std::wstring(group->fgd[0].cFileName) == L"pack");
    CHECK((group->fgd[0].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    CHECK(std::wstring(group->fgd[1].cFileName) == L"pack\\a.txt");
    CHECK(group->fgd[1].nFileSizeLow == kFileABytes);
    CHECK((group->fgd[1].dwFlags & FD_WRITESTIME) != 0);
    CHECK((group->fgd[1].ftLastWriteTime.dwLowDateTime != 0 || group->fgd[1].ftLastWriteTime.dwHighDateTime != 0));
    CHECK(std::wstring(group->fgd[3].cFileName) == L"pack\\sub\\b.txt");
    ::GlobalUnlock(sm.hGlobal);
    ::ReleaseStgMedium(&sm);

    //目录没有内容、下标越界
    FORMATETC dirContents = ContentsFormat(0);
    CHECK(data->GetData(&dirContents, &sm) == DV_E_LINDEX);
    FORMATETC outOfRange = ContentsFormat(4);
    CHECK(data->GetData(&outOfRange, &sm) == DV_E_LINDEX);
    data->Release();
}

TEST_CASE("SDD5 temp dir is removed with the last reference", "[fileshare][sharedrag]")
{
    const std::wstring base = FreshBase();
    const std::wstring temp = base + L"\\temp";
    REQUIRE(sharetestfs::MakeDirs(temp + L"\\pack"));
    REQUIRE(sharetestfs::WriteFileBytes(temp + L"\\pack\\a.txt", kFileABytes));
    {
        std::shared_ptr<CShareDragJob> job(new CShareDragJob(temp, SampleItems()));
        IDataObject* data = CreateShareDragDataObject(job, NULL);
        job.reset();
        //数据对象仍持有任务：目录还在
        CHECK(::GetFileAttributesW(temp.c_str()) != INVALID_FILE_ATTRIBUTES);
        data->Release();
    }
    CHECK(::GetFileAttributesW(temp.c_str()) == INVALID_FILE_ATTRIBUTES);
    sharetestfs::DeleteTree(base);
}
