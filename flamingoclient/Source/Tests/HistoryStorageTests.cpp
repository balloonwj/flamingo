/**
 *  聊天记录管理器「本机存储占用」页的单元测试（Suite HS），HistoryStorageTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryStorageStats.h、HistoryStorageLayout.h 与 CV2MsgStore::QueryCacheRefs
 *  （docs/聊天记录管理器方案.md §12.6）：
 *    HS1 从内容 JSON 取缓存引用：图片、截图、文件、语音各取各的 md5，md5 不合法（含路径分隔符、长度不对）时拒绝；
 *    HS2 缓存路径：图片是文件、文件类是 md5 子目录、语音是 .ogg；目录为空或 md5 不合法时为空串；
 *    HS3 在临时目录里造一套缓存统计：各会话三类字节数、同一会话里重复引用只计一次、本机没有的不计、页首按 md5 去重、
 *        按合计倒序、聊天记录库含 -wal；
 *    HS4 清理目标：某个会话、全部会话，去重；
 *    HS5 删除：文件、文件夹整个删掉；不存在算成功；缓存目录之外、数据根之外的路径拒绝；
 *    HS6 本地库读出缓存引用：已撤回与非这几类消息不在其中；
 *    HS7 界面：控件编号、统计中按钮不可点、会话行与「清理缓存」按钮、局部更新；进入与离开存储页时右侧容器的显隐。
 *  用临时目录与临时 db 文件。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <fstream>
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistoryStorageStats.h"
#include "../UI/BalloonHistory/HistoryStorageLayout.h"
#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../UI/BalloonHistory/HistoryLibraryLayout.h"
#include "../UI/BalloonHistory/HistoryConversationList.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//消息类型（与 net/Protocol.h 一致）
const int kTypeText       = 1;
const int kTypeImage      = 2;
const int kTypeFile       = 3;
const int kTypeVoice      = 4;
const int kTypeScreenshot = 9;
//几个合法的 md5
const char kMd5A[] = "0123456789abcdef0123456789abcdef";
const char kMd5B[] = "fedcba9876543210fedcba9876543210";
const char kMd5C[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char kMd5X[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";   //本机没有缓存的那一份
//造出来的几个文件的字节数
const int kImageBytes = 100;
const int kFileBytes  = 300;
const int kFileExtra  = 20;    //文件缓存目录里的第二个文件
const int kVoiceBytes = 50;
const int kDbBytes    = 4000;
const int kWalBytes   = 96;
//排版用的右侧区域尺寸
const int kTestWidth  = 900;
const int kTestHeight = 700;

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

//写一个指定字节数的文件
void WriteBytes(const std::wstring& path, int bytes)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    for (int i = 0; i < bytes; ++i)
    {
        out.put('x');
    }
}

//一套临时的数据目录：<临时目录>hs_<进程号>\，下面是 Users\acc\ChatImage、ChatFile、ChatVoice
struct TempCache
{
    HistoryStorageDirs m_dirs;

    TempCache()
    {
        wchar_t tmp[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, tmp);
        wchar_t name[64] = { 0 };
        ::swprintf_s(name, L"hs_%u\\", static_cast<unsigned int>(::GetCurrentProcessId()));
        m_dirs.m_dataRoot = std::wstring(tmp) + name;
        RemoveTree(m_dirs.m_dataRoot);
        const std::wstring user = m_dirs.m_dataRoot + L"Users\\acc\\";
        ::CreateDirectoryW(m_dirs.m_dataRoot.c_str(), NULL);
        ::CreateDirectoryW((m_dirs.m_dataRoot + L"Users").c_str(), NULL);
        ::CreateDirectoryW(user.c_str(), NULL);
        m_dirs.m_imageDir = user + L"ChatImage\\";
        m_dirs.m_fileDir = user + L"ChatFile\\";
        m_dirs.m_voiceDir = user + L"ChatVoice\\";
        ::CreateDirectoryW(m_dirs.m_imageDir.c_str(), NULL);
        ::CreateDirectoryW(m_dirs.m_fileDir.c_str(), NULL);
        ::CreateDirectoryW(m_dirs.m_voiceDir.c_str(), NULL);
        m_dirs.m_dbPath = user + L"msgv2.db";
    }

    ~TempCache()
    {
        RemoveTree(m_dirs.m_dataRoot);
    }

    //造本组的缓存：图片 A、文件 B（目录里两个文件）、语音 C、聊天记录库与 -wal
    void Populate()
    {
        WriteBytes(m_dirs.m_imageDir + L"0123456789abcdef0123456789abcdef", kImageBytes);
        const std::wstring fileDir = m_dirs.m_fileDir + L"fedcba9876543210fedcba9876543210\\";
        ::CreateDirectoryW(fileDir.c_str(), NULL);
        WriteBytes(fileDir + L"报告.docx", kFileBytes);
        WriteBytes(fileDir + L"报告.docx.part", kFileExtra);
        WriteBytes(m_dirs.m_voiceDir + L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.ogg", kVoiceBytes);
        WriteBytes(m_dirs.m_dbPath, kDbBytes);
        WriteBytes(m_dirs.m_dbPath + L"-wal", kWalBytes);
    }

    //递归删除一个目录（测试收尾用）
    static void RemoveTree(const std::wstring& dir)
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
                    RemoveTree(full);
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

//一条缓存引用
HistoryCacheRef Ref(int ctype, int64_t cid, int kind, const char* md5)
{
    HistoryCacheRef r;
    r.m_ctype = ctype;
    r.m_cid = cid;
    r.m_kind = kind;
    r.m_md5 = md5;
    return r;
}

//本组的缓存引用：会话 A（单聊 200）引用图片 A 两次、文件 B、本机没有的语音 X；会话 B（群 300）引用图片 A、语音 C
std::vector<HistoryCacheRef> SampleRefs()
{
    std::vector<HistoryCacheRef> refs;
    refs.push_back(Ref(1, 200, kStorageImage, kMd5A));
    refs.push_back(Ref(1, 200, kStorageFile, kMd5B));
    refs.push_back(Ref(1, 200, kStorageImage, kMd5A));
    refs.push_back(Ref(1, 200, kStorageVoice, kMd5X));
    refs.push_back(Ref(2, 300, kStorageImage, kMd5A));
    refs.push_back(Ref(2, 300, kStorageVoice, kMd5C));
    return refs;
}

//路径是否存在
bool Exists(const std::wstring& path)
{
    std::wstring p = path;
    while (!p.empty() && p[p.size() - 1] == L'\\')
    {
        p.erase(p.size() - 1);
    }
    return ::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

TEST_CASE("HS1 从内容 JSON 取缓存引用；md5 不合法时拒绝", "[histstorage]")
{
    HistoryCacheRef r;
    REQUIRE(CacheRefFromContent(kTypeImage, std::string("{\"image\":{\"fileid\":\"1\",\"md5\":\"") + kMd5A + "\"}}", r));
    CHECK(r.m_kind == kStorageImage);
    CHECK(r.m_md5 == kMd5A);
    REQUIRE(CacheRefFromContent(kTypeScreenshot, std::string("{\"image\":{\"md5\":\"") + kMd5B + "\"}}", r));
    CHECK(r.m_kind == kStorageImage);
    REQUIRE(CacheRefFromContent(kTypeFile, std::string("{\"file\":{\"name\":\"a\",\"md5\":\"") + kMd5B + "\"}}", r));
    CHECK(r.m_kind == kStorageFile);
    CHECK(r.m_md5 == kMd5B);
    REQUIRE(CacheRefFromContent(kTypeVoice, std::string("{\"voice\":{\"md5\":\"") + kMd5C + "\"}}", r));
    CHECK(r.m_kind == kStorageVoice);

    //md5 不合法：含路径分隔符、长度不对、不是十六进制
    CHECK_FALSE(CacheRefFromContent(kTypeImage, "{\"image\":{\"md5\":\"..\\\\..\\\\x\"}}", r));
    CHECK_FALSE(CacheRefFromContent(kTypeImage, "{\"image\":{\"md5\":\"abc\"}}", r));
    CHECK_FALSE(CacheRefFromContent(kTypeImage, "{\"image\":{\"md5\":\"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz\"}}", r));
    //其它类型、缺字段、内容坏了
    CHECK_FALSE(CacheRefFromContent(kTypeText, "{\"text\":\"hi\"}", r));
    CHECK_FALSE(CacheRefFromContent(kTypeFile, std::string("{\"image\":{\"md5\":\"") + kMd5A + "\"}}", r));
    CHECK_FALSE(CacheRefFromContent(kTypeImage, "not json", r));
    CHECK_FALSE(CacheRefFromContent(kTypeImage, "", r));

    CHECK(IsValidCacheMd5(kMd5A));
    CHECK(IsValidCacheMd5("0123456789ABCDEF0123456789ABCDEF"));
    CHECK_FALSE(IsValidCacheMd5(""));
    CHECK_FALSE(IsValidCacheMd5("0123456789abcdef0123456789abcde/"));
}

TEST_CASE("HS2 缓存路径：图片是文件、文件类是子目录、语音是 .ogg", "[histstorage]")
{
    HistoryStorageDirs dirs;
    dirs.m_imageDir = L"D:\\data\\Users\\acc\\ChatImage";   //不带结尾反斜杠也行
    dirs.m_fileDir = L"D:\\data\\Users\\acc\\ChatFile\\";
    dirs.m_voiceDir = L"D:\\data\\Users\\acc\\ChatVoice\\";
    CHECK(StorageCachePath(dirs, kStorageImage, kMd5A) == L"D:\\data\\Users\\acc\\ChatImage\\0123456789abcdef0123456789abcdef");
    CHECK(StorageCachePath(dirs, kStorageFile, kMd5B) == L"D:\\data\\Users\\acc\\ChatFile\\fedcba9876543210fedcba9876543210\\");
    CHECK(StorageCachePath(dirs, kStorageVoice, kMd5C) == L"D:\\data\\Users\\acc\\ChatVoice\\aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.ogg");
    CHECK(StorageCachePath(dirs, kStorageKindCount, kMd5A).empty());
    CHECK(StorageCachePath(dirs, kStorageImage, "..").empty());
    dirs.m_voiceDir.clear();
    CHECK(StorageCachePath(dirs, kStorageVoice, kMd5C).empty());
}

TEST_CASE("HS3 统计：各会话三类字节数、重复只计一次、本机没有的不计、页首按 md5 去重、聊天记录库含 -wal", "[histstorage]")
{
    TempCache cache;
    cache.Populate();
    const HistoryStorageStats s = ComputeStorageStats(SampleRefs(), cache.m_dirs);

    //页首：图片 A 只算一份，文件目录两份文件合计，语音 X 本机没有
    CHECK(s.m_totalBytes[kStorageImage] == kImageBytes);
    CHECK(s.m_totalFiles[kStorageImage] == 1);
    CHECK(s.m_totalBytes[kStorageFile] == kFileBytes + kFileExtra);
    CHECK(s.m_totalFiles[kStorageFile] == 1);
    CHECK(s.m_totalBytes[kStorageVoice] == kVoiceBytes);
    CHECK(s.m_totalFiles[kStorageVoice] == 1);
    CHECK(s.CacheBytes() == kImageBytes + kFileBytes + kFileExtra + kVoiceBytes);
    CHECK(s.m_dbBytes == kDbBytes + kWalBytes);

    //各会话：按合计倒序；会话 A 的图片 A 引用了两次只算一次，语音 X 不计
    REQUIRE(s.m_convs.size() == 2);
    const HistoryStorageConv& a = s.m_convs[0];
    CHECK(a.m_ctype == 1);
    CHECK(a.m_cid == 200);
    CHECK(a.m_bytes[kStorageImage] == kImageBytes);
    CHECK(a.m_files[kStorageImage] == 1);
    CHECK(a.m_bytes[kStorageFile] == kFileBytes + kFileExtra);
    CHECK(a.m_bytes[kStorageVoice] == 0);
    CHECK(a.m_files[kStorageVoice] == 0);
    const HistoryStorageConv& b = s.m_convs[1];
    CHECK(b.m_cid == 300);
    CHECK(b.TotalBytes() == kImageBytes + kVoiceBytes);

    //目录大小、文件大小与不存在
    CHECK(StoragePathBytes(cache.m_dirs.m_fileDir) == kFileBytes + kFileExtra);
    CHECK(StoragePathBytes(cache.m_dirs.m_imageDir + L"0123456789abcdef0123456789abcdef") == kImageBytes);
    CHECK(StoragePathBytes(cache.m_dirs.m_imageDir + L"nope") == 0);
    CHECK(StoragePathBytes(L"") == 0);

    //目录为空（未登录）：什么都不计
    HistoryStorageDirs none;
    const HistoryStorageStats empty = ComputeStorageStats(SampleRefs(), none);
    CHECK(empty.CacheBytes() == 0);
    CHECK(empty.m_convs.empty());
    CHECK(empty.m_dbBytes == 0);
}

TEST_CASE("HS4 清理目标：某个会话、全部会话，去重", "[histstorage]")
{
    TempCache cache;
    const std::vector<HistoryCacheRef> refs = SampleRefs();
    std::vector<std::wstring> b = StorageCleanTargets(refs, 2, 300, cache.m_dirs);
    REQUIRE(b.size() == 2);
    CHECK(b[0] == StorageCachePath(cache.m_dirs, kStorageImage, kMd5A));
    CHECK(b[1] == StorageCachePath(cache.m_dirs, kStorageVoice, kMd5C));
    std::vector<std::wstring> a = StorageCleanTargets(refs, 1, 200, cache.m_dirs);
    CHECK(a.size() == 3);   //图片 A（两次引用去重）、文件 B、语音 X
    std::vector<std::wstring> all = StorageCleanTargets(refs, 1, 0, cache.m_dirs);
    CHECK(all.size() == 4);
    CHECK(StorageCleanTargets(refs, 2, 999, cache.m_dirs).empty());
}

TEST_CASE("HS5 删除：文件与文件夹删掉、不存在算成功、缓存目录之外拒绝", "[histstorage]")
{
    TempCache cache;
    cache.Populate();
    const HistoryStorageDirs& d = cache.m_dirs;
    const std::wstring image = StorageCachePath(d, kStorageImage, kMd5A);
    const std::wstring fileDir = StorageCachePath(d, kStorageFile, kMd5B);
    REQUIRE(Exists(image));
    REQUIRE(Exists(fileDir));
    CHECK(DeleteStoragePath(image, d));
    CHECK_FALSE(Exists(image));
    CHECK(DeleteStoragePath(fileDir, d));
    CHECK_FALSE(Exists(fileDir));
    //再删一次：不存在算成功
    CHECK(DeleteStoragePath(image, d));

    //缓存目录本身、账号目录里的别的文件、数据根之外：拒绝，文件还在
    CHECK_FALSE(DeleteStoragePath(d.m_voiceDir, d));
    CHECK(Exists(d.m_voiceDir));
    CHECK_FALSE(DeleteStoragePath(d.m_dbPath, d));
    CHECK(Exists(d.m_dbPath));
    wchar_t tmp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tmp);
    const std::wstring outside = std::wstring(tmp) + L"hs_outside.txt";
    WriteBytes(outside, 1);
    CHECK_FALSE(DeleteStoragePath(outside, d));
    CHECK(Exists(outside));
    ::DeleteFileW(outside.c_str());
    //上跳绕出边界
    CHECK_FALSE(DeleteStoragePath(d.m_imageDir + L"..\\..\\msgv2.db", d));
    CHECK(Exists(d.m_dbPath));
    CHECK_FALSE(DeleteStoragePath(L"", d));
}

TEST_CASE("HS6 本地库读出缓存引用：已撤回与其它类型不在其中", "[histstorage]")
{
    TempCache cache;
    CV2MsgStore store;
    REQUIRE(store.Open((cache.m_dirs.m_dataRoot + L"hs6.db").c_str()));
    HistoryRecord rec;
    rec.m_ctype = 2;
    rec.m_cid = 300;
    rec.m_fromid = 301;
    rec.m_timestamp = 1000;

    rec.m_msgid = 1;
    rec.m_msgtype = kTypeImage;
    rec.m_content = std::string("{\"image\":{\"md5\":\"") + kMd5A + "\"}}";
    REQUIRE(store.Upsert(rec));
    rec.m_msgid = 2;
    rec.m_msgtype = kTypeFile;
    rec.m_content = std::string("{\"file\":{\"md5\":\"") + kMd5B + "\"}}";
    REQUIRE(store.Upsert(rec));
    rec.m_msgid = 3;
    rec.m_msgtype = kTypeVoice;
    rec.m_content = std::string("{\"voice\":{\"md5\":\"") + kMd5C + "\"}}";
    REQUIRE(store.Upsert(rec));
    rec.m_msgid = 4;
    rec.m_msgtype = kTypeText;
    rec.m_content = "{\"text\":\"hi\"}";
    REQUIRE(store.Upsert(rec));
    rec.m_msgid = 5;
    rec.m_msgtype = kTypeScreenshot;
    rec.m_content = std::string("{\"image\":{\"md5\":\"") + kMd5X + "\"}}";
    REQUIRE(store.Upsert(rec));
    REQUIRE(store.MarkRecalled(2, 300, 5));

    std::vector<HistoryCacheRef> refs;
    REQUIRE(store.QueryCacheRefs(refs));
    REQUIRE(refs.size() == 3);
    for (size_t i = 0; i < refs.size(); ++i)
    {
        CHECK(refs[i].m_ctype == 2);
        CHECK(refs[i].m_cid == 300);
        CHECK(refs[i].m_md5 != kMd5X);
    }
    store.Close();
}

TEST_CASE("HS7 界面：控件、统计中按钮不可点、会话行与清理按钮、局部更新、进入与离开存储页", "[histstorage]")
{
    HistoryStorageViewData busy;
    busy.m_busy = true;
    busy.m_emptyText = _T("正在统计…");
    std::unique_ptr<DuiControl> page = BuildHistoryStorageBody(busy, 0);
    page->ForceLayout(RECT{ 0, 0, kTestWidth, kTestHeight });
    CHECK(page->GetCtrlId() == (UINT)kIdStorBody);
    REQUIRE(FindById(page.get(), kIdStorRefresh) != nullptr);
    CHECK_FALSE(FindById(page.get(), kIdStorRefresh)->IsEnabled());
    CHECK_FALSE(FindById(page.get(), kIdStorCleanAll)->IsEnabled());
    DuiLabel* empty = dynamic_cast<DuiLabel*>(FindById(page.get(), kIdStorEmpty));
    REQUIRE(empty != nullptr);
    CHECK(empty->GetText() == CString(L"正在统计…"));
    CHECK(FindById(page.get(), kIdStorRowBase) == nullptr);

    //统计完成：两行，各有「清理缓存」，按钮可点
    HistoryStorageViewData done;
    done.m_summary = _T("缓存合计 1 MB");
    done.m_canCleanAll = true;
    HistoryStorageRowView r;
    r.m_name = _T("产品研发群");
    r.m_imageText = _T("1 MB");
    r.m_fileText = _T("-");
    r.m_voiceText = _T("-");
    r.m_totalText = _T("1 MB");
    done.m_rows.push_back(r);
    done.m_rows.push_back(r);
    ApplyHistoryStorageData(page.get(), done);
    CHECK(FindById(page.get(), kIdStorRefresh)->IsEnabled());
    CHECK(FindById(page.get(), kIdStorCleanAll)->IsEnabled());
    CHECK(FindById(page.get(), kIdStorEmpty) == nullptr);
    REQUIRE(FindById(page.get(), kIdStorRowBase + 1) != nullptr);
    DuiControl* clean1 = FindById(page.get(), kIdStorCleanBase + 1);
    REQUIRE(clean1 != nullptr);
    CHECK(clean1->GetRect().right - clean1->GetRect().left > 0);
    DuiLabel* summary = dynamic_cast<DuiLabel*>(FindById(page.get(), kIdStorSummary));
    REQUIRE(summary != nullptr);
    CHECK(summary->GetText() == CString(L"缓存合计 1 MB"));
    //编号段不重叠
    CHECK((UINT)kIdStorRowBase + kIdStorRowCapacity <= (UINT)kIdStorCleanBase);
    CHECK((UINT)kIdLibCopyBase + kIdLibRowCapacity <= (UINT)kIdStorBody);

    //进入与离开存储页：右侧容器只显示一页，左栏入口跟着变
    HistoryViewData chat;
    FillMockHistory(chat);
    HistoryLibraryViewData lib;
    lib.m_kind = kHistoryLibraryFiles;
    std::unique_ptr<DuiControl> root = BuildHistoryManagerRoot(BuildHistoryManagerPane(kManagerTypeAll, 0),
                                                               BuildHistoryWindowBody(chat),
                                                               BuildHistoryLibraryBody(lib, 0),
                                                               kMgrPaneDefaultW, false);
    DuiLayout* host = dynamic_cast<DuiLayout*>(FindById(root.get(), kIdMgrRightHost));
    REQUIRE(host != nullptr);
    std::unique_ptr<DuiControl> storage = BuildHistoryStorageBody(done, 0);
    storage->SetVisible(false);
    host->AddChild(std::move(storage), DuiLayout::Hint().Weight(1));
    root->ForceLayout(RECT{ 0, 0, kTestWidth + kMgrPaneDefaultW, kTestHeight });
    REQUIRE(FindById(root.get(), kIdMgrStorage) != nullptr);
    CHECK(FindById(root.get(), kIdMgrStorage)->GetRect().bottom <= kTestHeight);

    ApplyHistoryManagerStorage(root.get(), true, kHistoryManagerTabChat);
    CHECK(FindById(root.get(), kIdStorBody)->IsVisible());
    CHECK_FALSE(FindById(root.get(), kIdHistoryBody)->IsVisible());
    CHECK_FALSE(FindById(root.get(), kIdLibBody)->IsVisible());
    CHECK(EqualRect(&FindById(root.get(), kIdStorBody)->GetRect(), &host->GetRect()));
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrStorage))->GetVariant() == DuiButton::Variant::Primary);

    //切到文件标签就离开了存储页
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabFiles);
    CHECK_FALSE(FindById(root.get(), kIdStorBody)->IsVisible());
    CHECK(FindById(root.get(), kIdLibBody)->IsVisible());
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrStorage))->GetVariant() == DuiButton::Variant::Ghost);

    //再进入、按离开回到聊天标签
    ApplyHistoryManagerStorage(root.get(), true, kHistoryManagerTabFiles);
    ApplyHistoryManagerStorage(root.get(), false, kHistoryManagerTabChat);
    CHECK_FALSE(FindById(root.get(), kIdStorBody)->IsVisible());
    CHECK(FindById(root.get(), kIdHistoryBody)->IsVisible());
}
