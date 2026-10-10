/**
 *  聊天记录管理器文件页与图片页的数据层单元测试（Suite HL），HistoryLibraryTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryLibraryQuery.h 与 CV2MsgStore 为它新增的接口（docs/聊天记录管理器方案.md §11.5、§11.6）：
 *    HLQ1 扩展名与类别：大小写、多个点、没有扩展名，与服务端扩展名表一致；
 *    HLQ2 两部分是否参与查询：来源、类型「文件夹」、限定到单聊；
 *    HLQ3 从内容 JSON 补齐文件、文件夹、图片的大小与文件 ID；
 *    HLS1 文件页：全部、方向、来源、类型、会话、只看单聊 / 群聊、文件名（普通与通配符、% 按普通字符）、
 *         时间、分页、排序；已撤回与覆盖区间之外的不出现；群文件以文件消息发到群里时只列一条；
 *    HLS2 图片页：只有图片与截图，方向与会话筛选；
 *    HLS3 按会话统计；
 *    HLS4 群文件缓存表：整体替换（保留本机路径、删掉服务端已没有的）、单条写入与删除、按群删除、登记本机路径；
 *    HLM1 浏览模型（HistoryLibraryModel）的默认值与两页的每页条数；
 *    HLM2 分页：只换页时不重新统计总数、页号夹取、改筛选回到第 1 页、通配符只在有关键字时影响页号；
 *    HLM3 按会话计数、「全部会话」之和、恢复默认筛选；
 *    TGE14 帖子卡片里的图片（docs/帖子群方案.md 14.5）：消息记录窗口的「图片」分类收录带图片的帖子卡片与补充卡片，
 *          聊天记录管理器的图片页按张展开（数量、顺序、序号、下载信息、按会话统计），已删除的、没有图片的、
 *          内容不是合法 JSON 的卡片都不收录，也不让查询出错。
 *  用临时 db 文件跑真实 SQLite。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistoryLibraryQuery.h"
#include "../UI/BalloonHistory/HistoryLibraryModel.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"   // HistoryFilter：消息记录窗口的「图片」分类

using namespace balloonhistory;

namespace {

//本人 userid
const int64_t kSelf = 100;
//会话类型与消息类型（与 net/Protocol.h 一致）
const int kSingle = 1;
const int kGroup  = 2;
const int kTypeText       = 1;
const int kTypeImage      = 2;
const int kTypeFile       = 3;
const int kTypeScreenshot = 9;
const int kTypeFolder     = 18;
//「近 N 天」换算用的当前时间；本组数据的时间都很小，只用全部时间与自定义区间
const int64_t kNowMs = 100000;
//每页条数：放得下全部数据
const int kBigPage = 100;

//取一个临时目录下的 db 全路径，并先删掉同名旧文件与 WAL 残留（文件名带进程号）
std::wstring FreshLibraryDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//构造一条文件消息：内容 JSON 与 BuildContentJson 的格式相同，搜索文本是文件名
HistoryRecord FileMsg(int64_t msgid, int ctype, int64_t cid, int64_t fromid, int64_t ts,
                      const std::string& name, const std::string& fileid, int64_t size)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = fromid;
    rec.m_msgtype = kTypeFile;
    rec.m_timestamp = ts;
    rec.m_searchText = name;
    rec.m_content = "{\"file\":{\"fileid\":\"" + fileid + "\",\"name\":\"" + name + "\",\"size\":\""
                  + std::to_string((long long)size) + "\",\"md5\":\"md5" + fileid + "\"}}";
    return rec;
}

//构造一条文件夹传输记录
HistoryRecord FolderMsg(int64_t msgid, int ctype, int64_t cid, int64_t fromid, int64_t ts,
                        const std::string& name, int64_t totalSize)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = fromid;
    rec.m_msgtype = kTypeFolder;
    rec.m_timestamp = ts;
    rec.m_searchText = name;
    rec.m_content = "{\"folder\":{\"sessionid\":\"s1\",\"foldername\":\"" + name + "\",\"totaldirs\":1,"
                    "\"totalfiles\":3,\"totalsize\":" + std::to_string((long long)totalSize) + ",\"result\":1}}";
    return rec;
}

//构造一条图片或截图消息
HistoryRecord ImageMsg(int64_t msgid, int ctype, int64_t cid, int64_t fromid, int64_t ts, int msgtype)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = fromid;
    rec.m_msgtype = msgtype;
    rec.m_timestamp = ts;
    rec.m_content = "{\"image\":{\"fileid\":\"I" + std::to_string((long long)msgid)
                  + "\",\"md5\":\"im\",\"w\":10,\"h\":10,\"size\":\"77\"}}";
    return rec;
}

//构造群文件缓存表的一行
GroupFileCacheItem GroupFile(int64_t groupid, const std::string& entryid, const std::string& fileid,
                             const std::string& name, int filetype, int64_t uploader, int64_t ts)
{
    GroupFileCacheItem g;
    g.m_groupid = groupid;
    g.m_entryid = entryid;
    g.m_fileid = fileid;
    g.m_name = name;
    g.m_ext = FileExtFromName(name);
    g.m_size = 4096;
    g.m_md5 = "gmd5";
    g.m_filetype = filetype;
    g.m_uploaderid = uploader;
    g.m_uploaderName = "uploader";
    g.m_uploadTime = ts;
    return g;
}

/**
 *  测试数据（本人 userid = 100）：
 *    聊天文件  11 (1,200) 本人  1000 季度工作报告.docx      12 (1,200) 对方 2000 设计稿.zip
 *              13 (1,201) 对方  3000 文件夹「资料夹」         14 (2,300) 本人 4000 季度报告.pdf
 *              15 (2,300) 对方  5000 录屏.mp4（fileid G1，与群文件 E1 是同一个文件）
 *              16 (2,300) 对方  6000 增长 50%.xlsx
 *              17 (1,200) 已撤回 7000                       18 (1,202) 覆盖区间之外 500
 *    图片      21 (1,200) 对方  8000 图片                    22 (2,300) 本人 9000 截图
 *    文字      23 (1,200) 9500
 *    群文件    E1 群 300 录屏.mp4 对方 5000（与 15 重复）     E2 群 300 群公告附件.rar 本人 5500
 *              E3 群 301 规范.doc 对方 2500                   E4 群 301 未知.xyz 对方 100
 */
void SeedLibrary(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(FileMsg(11, kSingle, 200, kSelf, 1000, "季度工作报告.docx", "F11", 1000)));
    REQUIRE(store.Upsert(FileMsg(12, kSingle, 200, 200, 2000, "设计稿.zip", "F12", 2000)));
    REQUIRE(store.Upsert(FolderMsg(13, kSingle, 201, 201, 3000, "资料夹", 5000)));
    REQUIRE(store.Upsert(FileMsg(14, kGroup, 300, kSelf, 4000, "季度报告.pdf", "F14", 4000)));
    REQUIRE(store.Upsert(FileMsg(15, kGroup, 300, 301, 5000, "录屏.mp4", "G1", 5000)));
    REQUIRE(store.Upsert(FileMsg(16, kGroup, 300, 301, 6000, "增长 50%.xlsx", "F16", 6000)));
    REQUIRE(store.Upsert(FileMsg(17, kSingle, 200, 200, 7000, "撤回.docx", "F17", 7000)));
    REQUIRE(store.MarkRecalled(kSingle, 200, 17));
    REQUIRE(store.Upsert(FileMsg(18, kSingle, 202, 202, 500, "旧文件.txt", "F18", 500)));
    CoverRange cover;
    cover.m_valid = true;
    cover.m_lo = 19;
    cover.m_hi = 19;
    cover.m_reachedOldest = false;
    REQUIRE(store.SetCoverage(kSingle, 202, cover));
    REQUIRE(store.Upsert(ImageMsg(21, kSingle, 200, 200, 8000, kTypeImage)));
    REQUIRE(store.Upsert(ImageMsg(22, kGroup, 300, kSelf, 9000, kTypeScreenshot)));
    HistoryRecord text;
    text.m_msgid = 23;
    text.m_ctype = kSingle;
    text.m_cid = 200;
    text.m_fromid = 200;
    text.m_msgtype = kTypeText;
    text.m_timestamp = 9500;
    text.m_searchText = "季度报告写好了";
    REQUIRE(store.Upsert(text));

    std::vector<GroupFileCacheItem> g300;
    g300.push_back(GroupFile(300, "E1", "G1", "录屏.mp4", kHistoryFileCatVideo, 301, 5000));
    g300.push_back(GroupFile(300, "E2", "G2", "群公告附件.rar", kHistoryFileCatArchive, kSelf, 5500));
    REQUIRE(store.ReplaceGroupFileCache(300, g300));
    std::vector<GroupFileCacheItem> g301;
    g301.push_back(GroupFile(301, "E3", "G3", "规范.doc", kHistoryFileCatDocument, 302, 2500));
    g301.push_back(GroupFile(301, "E4", "G4", "未知.xyz", kHistoryFileCatOther, 302, 100));
    REQUIRE(store.ReplaceGroupFileCache(301, g301));
}

//文件页的默认条件：全部、每页放得下全部数据
HistoryLibraryFilter FilesFilter()
{
    HistoryLibraryFilter f;
    f.m_kind = kHistoryLibraryFiles;
    f.m_selfUserId = kSelf;
    f.m_pageSize = kBigPage;
    return f;
}

//把一页结果写成「msgid 或 条目 ID」的序列，便于整体比较顺序，如 "16,E2,15"
std::string Keys(const std::vector<HistoryLibraryItem>& items)
{
    std::string s;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!s.empty())
        {
            s += ",";
        }
        s += items[i].m_isGroupFile ? items[i].m_entryid : std::to_string((long long)items[i].m_msgid);
    }
    return s;
}

//取一页并返回其键序列；同时核对总数与本页条数一致（每页放得下全部数据时）
std::string PageKeys(CV2MsgStore& store, const HistoryLibraryFilter& f)
{
    std::vector<HistoryLibraryItem> items;
    REQUIRE(store.QueryLibraryPage(f, kNowMs, items));
    int64_t total = -1;
    REQUIRE(store.CountLibrary(f, kNowMs, total));
    if (f.m_pageSize >= kBigPage)
    {
        CHECK(total == (int64_t)items.size());
    }
    return Keys(items);
}

} // namespace

TEST_CASE("HLQ1 扩展名与类别：大小写、多个点、没有扩展名，与服务端扩展名表一致", "[histfiles]")
{
    CHECK(FileExtFromName("a.DOCX") == "docx");
    CHECK(FileExtFromName("x.tar.gz") == "gz");
    CHECK(FileExtFromName(".gitignore").empty());
    CHECK(FileExtFromName("noext").empty());
    CHECK(FileExtFromName("a.").empty());
    CHECK(FileExtFromName("报告.PDF") == "pdf");

    CHECK(FileCategoryFromName("季度工作报告.docx") == kHistoryFileCatDocument);
    CHECK(FileCategoryFromName("说明.md") == kHistoryFileCatDocument);
    CHECK(FileCategoryFromName("照片.JPG") == kHistoryFileCatImage);
    CHECK(FileCategoryFromName("录屏.mkv") == kHistoryFileCatVideo);
    CHECK(FileCategoryFromName("歌.m4a") == kHistoryFileCatAudio);
    CHECK(FileCategoryFromName("包.7z") == kHistoryFileCatArchive);
    CHECK(FileCategoryFromName("x.tar.gz") == kHistoryFileCatArchive);
    CHECK(FileCategoryFromName("设计.psd") == kHistoryFileCatOther);
    CHECK(FileCategoryFromName("noext") == kHistoryFileCatOther);
}

TEST_CASE("HLQ2 两部分是否参与查询：来源、类型「文件夹」、限定到单聊", "[histfiles]")
{
    HistoryLibraryFilter f = FilesFilter();
    CHECK(LibraryIncludesChat(f));
    CHECK(LibraryIncludesGroupFiles(f));

    f.m_source = kHistoryLibrarySourceChat;
    CHECK(LibraryIncludesChat(f));
    CHECK_FALSE(LibraryIncludesGroupFiles(f));

    f.m_source = kHistoryLibrarySourceGroup;
    CHECK_FALSE(LibraryIncludesChat(f));
    CHECK(LibraryIncludesGroupFiles(f));

    //文件夹只在聊天里有
    f = FilesFilter();
    f.m_category = kHistoryFileCatFolder;
    CHECK_FALSE(LibraryIncludesGroupFiles(f));

    //限定到某个单聊，或全部会话里只看单聊：没有群文件
    f = FilesFilter();
    f.m_ctype = kSingle;
    f.m_cid = 200;
    CHECK_FALSE(LibraryIncludesGroupFiles(f));
    f.m_ctype = kGroup;
    f.m_cid = 300;
    CHECK(LibraryIncludesGroupFiles(f));
    f = FilesFilter();
    f.m_ctypeOnly = kSingle;
    CHECK_FALSE(LibraryIncludesGroupFiles(f));
    f.m_ctypeOnly = kGroup;
    CHECK(LibraryIncludesGroupFiles(f));

    //图片页只有聊天消息；文件名筛选只在文件页生效
    f = FilesFilter();
    f.m_kind = kHistoryLibraryImages;
    f.m_keyword = "abc";
    CHECK(LibraryIncludesChat(f));
    CHECK_FALSE(LibraryIncludesGroupFiles(f));
    CHECK_FALSE(LibraryHasKeyword(f));
    f.m_kind = kHistoryLibraryFiles;
    CHECK(LibraryHasKeyword(f));
    f.m_keyword = "**";
    f.m_wildcard = true;
    CHECK_FALSE(LibraryHasKeyword(f));

    //两部分都不查时语句为空
    f = FilesFilter();
    f.m_source = kHistoryLibrarySourceGroup;
    f.m_category = kHistoryFileCatFolder;
    int binds = -1;
    CHECK(BuildLibraryPageSql(f, kNowMs, binds).empty());
    CHECK(BuildLibraryCountSql(f, kNowMs, binds).empty());

    //文件名筛选生效时两部分各一个 LIKE 占位
    f = FilesFilter();
    f.m_keyword = "报告";
    BuildLibraryPageSql(f, kNowMs, binds);
    CHECK(binds == 2);
    f.m_source = kHistoryLibrarySourceChat;
    BuildLibraryCountSql(f, kNowMs, binds);
    CHECK(binds == 1);
}

TEST_CASE("HLQ3 从内容 JSON 补齐文件、文件夹、图片的大小与文件 ID", "[histfiles]")
{
    HistoryLibraryItem file;
    file.m_msgtype = kTypeFile;
    file.m_content = FileMsg(1, kSingle, 2, 3, 4, "a.docx", "F1", 1234).m_content;
    FillLibraryItemFromContent(file);
    CHECK(file.m_name == "a.docx");
    CHECK(file.m_size == 1234);
    CHECK(file.m_fileid == "F1");
    CHECK(file.m_md5 == "md5F1");
    CHECK_FALSE(file.m_direct);

    HistoryLibraryItem direct;
    direct.m_content = "{\"file\":{\"fileid\":\"\",\"name\":\"p2p.bin\",\"size\":\"9\",\"md5\":\"x\",\"direct\":1}}";
    FillLibraryItemFromContent(direct);
    CHECK(direct.m_direct);
    CHECK(direct.m_name == "p2p.bin");

    HistoryLibraryItem folder;
    folder.m_content = FolderMsg(1, kSingle, 2, 3, 4, "资料夹", 5000).m_content;
    FillLibraryItemFromContent(folder);
    CHECK(folder.m_name == "资料夹");
    CHECK(folder.m_size == 5000);

    HistoryLibraryItem image;
    image.m_content = ImageMsg(21, kSingle, 2, 3, 4, kTypeImage).m_content;
    FillLibraryItemFromContent(image);
    CHECK(image.m_size == 77);
    CHECK(image.m_fileid == "I21");

    //名称已有时不覆盖；内容坏了时什么都不改；群文件行不处理
    HistoryLibraryItem named;
    named.m_name = "已有";
    named.m_content = file.m_content;
    FillLibraryItemFromContent(named);
    CHECK(named.m_name == "已有");
    HistoryLibraryItem bad;
    bad.m_content = "not json";
    FillLibraryItemFromContent(bad);
    CHECK(bad.m_size == 0);
    HistoryLibraryItem group;
    group.m_isGroupFile = true;
    group.m_content = file.m_content;
    FillLibraryItemFromContent(group);
    CHECK(group.m_size == 0);

    //转成记录
    HistoryLibraryItem chat;
    chat.m_msgid = 11;
    chat.m_ctype = kGroup;
    chat.m_cid = 300;
    chat.m_fromid = 301;
    chat.m_msgtype = kTypeFile;
    chat.m_name = "a.docx";
    chat.m_content = file.m_content;
    chat.m_timestamp = 99;
    const HistoryRecord rec = RecordFromLibraryItem(chat);
    CHECK(rec.m_msgid == 11);
    CHECK(rec.m_ctype == kGroup);
    CHECK(rec.m_cid == 300);
    CHECK(rec.m_fromid == 301);
    CHECK(rec.m_msgtype == kTypeFile);
    CHECK(rec.m_searchText == "a.docx");
    CHECK(rec.m_content == file.m_content);
    CHECK(rec.m_timestamp == 99);
}

TEST_CASE("HLS1 文件页：各种筛选组合、排序、分页、去重与可见范围", "[histfiles]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshLibraryDb(L"hls1.db").c_str()));
    SeedLibrary(store);

    //全部：按时间倒序；17 已撤回、18 在覆盖区间之外不出现；E1 与 15 是同一个文件，只列 15
    HistoryLibraryFilter f = FilesFilter();
    CHECK(PageKeys(store, f) == "16,E2,15,14,13,E3,12,11,E4");

    //方向
    f.m_direction = kHistoryLibraryDirSent;
    CHECK(PageKeys(store, f) == "E2,14,11");
    f.m_direction = kHistoryLibraryDirReceived;
    CHECK(PageKeys(store, f) == "16,15,13,E3,12,E4");

    //来源
    f = FilesFilter();
    f.m_source = kHistoryLibrarySourceChat;
    CHECK(PageKeys(store, f) == "16,15,14,13,12,11");
    f.m_source = kHistoryLibrarySourceGroup;
    CHECK(PageKeys(store, f) == "E2,E3,E4");

    //类型：聊天文件按扩展名归类、群文件按服务端类别
    f = FilesFilter();
    f.m_category = kHistoryFileCatDocument;
    CHECK(PageKeys(store, f) == "16,14,E3,11");
    f.m_category = kHistoryFileCatArchive;
    CHECK(PageKeys(store, f) == "E2,12");
    f.m_category = kHistoryFileCatVideo;
    CHECK(PageKeys(store, f) == "15");
    f.m_category = kHistoryFileCatOther;
    CHECK(PageKeys(store, f) == "E4");
    f.m_category = kHistoryFileCatFolder;
    CHECK(PageKeys(store, f) == "13");
    f.m_category = kHistoryFileCatAudio;
    CHECK(PageKeys(store, f).empty());

    //只看某个会话：群聊带上该群的群文件，单聊没有群文件
    f = FilesFilter();
    f.m_ctype = kGroup;
    f.m_cid = 300;
    CHECK(PageKeys(store, f) == "16,E2,15,14");
    f.m_ctype = kSingle;
    f.m_cid = 200;
    CHECK(PageKeys(store, f) == "12,11");
    f.m_ctype = kGroup;
    f.m_cid = 301;
    CHECK(PageKeys(store, f) == "E3,E4");

    //全部会话里只看单聊 / 群聊
    f = FilesFilter();
    f.m_ctypeOnly = kSingle;
    CHECK(PageKeys(store, f) == "13,12,11");
    f.m_ctypeOnly = kGroup;
    CHECK(PageKeys(store, f) == "16,E2,15,14,E3,E4");

    //文件名：普通字符、通配符、% 按普通字符；文字消息里的「季度报告写好了」不算
    f = FilesFilter();
    f.m_keyword = "报告";
    CHECK(PageKeys(store, f) == "14,11");
    f.m_keyword = "季度*报告";
    CHECK(PageKeys(store, f).empty());
    f.m_wildcard = true;
    CHECK(PageKeys(store, f) == "14,11");
    f.m_keyword = "季度??报告";
    CHECK(PageKeys(store, f) == "11");
    f.m_keyword = "%";
    f.m_wildcard = false;
    CHECK(PageKeys(store, f) == "16");
    f.m_keyword = "规范";
    CHECK(PageKeys(store, f) == "E3");

    //自定义时间区间（两端都含）
    f = FilesFilter();
    f.m_range = kHistoryTimeCustom;
    f.m_customMinMs = 2000;
    f.m_customMaxMs = 5000;
    CHECK(PageKeys(store, f) == "15,14,13,E3,12");

    //分页：每页 4 条，第 2 页
    f = FilesFilter();
    f.m_pageSize = 4;
    f.m_page = 2;
    std::vector<HistoryLibraryItem> page;
    REQUIRE(store.QueryLibraryPage(f, kNowMs, page));
    CHECK(Keys(page) == "13,E3,12,11");
    int64_t total = 0;
    REQUIRE(store.CountLibrary(f, kNowMs, total));
    CHECK(total == 9);

    //各行字段：聊天文件从内容补齐，群文件取缓存表
    f = FilesFilter();
    REQUIRE(store.QueryLibraryPage(f, kNowMs, page));
    REQUIRE(page.size() == 9);
    const HistoryLibraryItem& m16 = page[0];
    CHECK_FALSE(m16.m_isGroupFile);
    CHECK(m16.m_ctype == kGroup);
    CHECK(m16.m_cid == 300);
    CHECK(m16.m_fromid == 301);
    CHECK(m16.m_name == "增长 50%.xlsx");
    CHECK(m16.m_size == 6000);
    CHECK(m16.m_fileid == "F16");
    const HistoryLibraryItem& e2 = page[1];
    CHECK(e2.m_isGroupFile);
    CHECK(e2.m_ctype == kGroup);
    CHECK(e2.m_cid == 300);
    CHECK(e2.m_entryid == "E2");
    CHECK(e2.m_fromid == kSelf);
    CHECK(e2.m_uploaderName == "uploader");
    CHECK(e2.m_name == "群公告附件.rar");
    CHECK(e2.m_size == 4096);
    CHECK(e2.m_fileid == "G2");
    CHECK(e2.m_filetype == kHistoryFileCatArchive);
    const HistoryLibraryItem& m13 = page[4];
    CHECK(m13.m_msgtype == kTypeFolder);
    CHECK(m13.m_name == "资料夹");
    CHECK(m13.m_size == 5000);
}

TEST_CASE("HLS2 图片页：只有图片与截图，方向与会话筛选", "[histimages]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshLibraryDb(L"hls2.db").c_str()));
    SeedLibrary(store);

    HistoryLibraryFilter f;
    f.m_kind = kHistoryLibraryImages;
    f.m_selfUserId = kSelf;
    f.m_pageSize = kBigPage;
    //图片页的来源、类型、文件名筛选都不生效
    f.m_source = kHistoryLibrarySourceGroup;
    f.m_category = kHistoryFileCatDocument;
    f.m_keyword = "不存在";
    CHECK(PageKeys(store, f) == "22,21");
    f.m_direction = kHistoryLibraryDirSent;
    CHECK(PageKeys(store, f) == "22");
    f.m_direction = kHistoryLibraryDirReceived;
    CHECK(PageKeys(store, f) == "21");
    f.m_direction = kHistoryLibraryDirAll;
    f.m_ctype = kSingle;
    f.m_cid = 200;
    CHECK(PageKeys(store, f) == "21");

    std::vector<HistoryLibraryItem> page;
    f.m_cid = 0;
    REQUIRE(store.QueryLibraryPage(f, kNowMs, page));
    REQUIRE(page.size() == 2);
    CHECK(page[0].m_msgtype == kTypeScreenshot);
    CHECK(page[0].m_fileid == "I22");
    CHECK(page[0].m_size == 77);
}

TEST_CASE("HLS3 按会话统计：不受会话选择限制，筛选条件照常生效", "[histfiles],[histimages]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshLibraryDb(L"hls3.db").c_str()));
    SeedLibrary(store);

    HistoryLibraryFilter f = FilesFilter();
    //选了某个会话也照样列出全部会话的计数
    f.m_ctype = kSingle;
    f.m_cid = 200;
    f.m_ctypeOnly = kSingle;
    std::vector<HistoryLibraryConvCount> counts;
    REQUIRE(store.CountLibraryByConversation(f, kNowMs, counts));
    REQUIRE(counts.size() == 4);
    CHECK((counts[0].m_ctype == kSingle && counts[0].m_cid == 200 && counts[0].m_count == 2));
    CHECK((counts[1].m_ctype == kSingle && counts[1].m_cid == 201 && counts[1].m_count == 1));
    CHECK((counts[2].m_ctype == kGroup && counts[2].m_cid == 300 && counts[2].m_count == 4));
    CHECK((counts[3].m_ctype == kGroup && counts[3].m_cid == 301 && counts[3].m_count == 2));

    //方向筛选生效
    f.m_direction = kHistoryLibraryDirSent;
    REQUIRE(store.CountLibraryByConversation(f, kNowMs, counts));
    REQUIRE(counts.size() == 2);
    CHECK((counts[0].m_cid == 200 && counts[0].m_count == 1));
    CHECK((counts[1].m_cid == 300 && counts[1].m_count == 2));

    //图片页
    HistoryLibraryFilter img;
    img.m_kind = kHistoryLibraryImages;
    img.m_selfUserId = kSelf;
    REQUIRE(store.CountLibraryByConversation(img, kNowMs, counts));
    REQUIRE(counts.size() == 2);
    CHECK((counts[0].m_cid == 200 && counts[0].m_count == 1));
    CHECK((counts[1].m_cid == 300 && counts[1].m_count == 1));
}

TEST_CASE("HLS4 群文件缓存表：整体替换保留本机路径、删掉服务端已没有的，单条增删与按群删除", "[histfiles]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshLibraryDb(L"hls4.db").c_str()));

    std::vector<GroupFileCacheItem> first;
    first.push_back(GroupFile(301, "E3", "G3", "规范.doc", kHistoryFileCatDocument, 302, 2500));
    first.push_back(GroupFile(301, "E4", "G4", "未知.xyz", kHistoryFileCatOther, 302, 100));
    //群号对不上的项跳过
    first.push_back(GroupFile(999, "X1", "GX", "别的群.doc", kHistoryFileCatDocument, 302, 100));
    REQUIRE(store.ReplaceGroupFileCache(301, first));
    std::vector<GroupFileCacheItem> rows;
    REQUIRE(store.QueryGroupFileCacheOfGroup(301, rows));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_entryid == "E3");
    REQUIRE(store.QueryGroupFileCacheOfGroup(999, rows));
    CHECK(rows.empty());

    //登记本机路径：缓存里有这一条才算命中
    bool hit = false;
    REQUIRE(store.SetGroupFileLocalPath(301, "E3", "C:\\下载\\规范.doc", hit));
    CHECK(hit);
    REQUIRE(store.SetGroupFileLocalPath(301, "E9", "C:\\x", hit));
    CHECK_FALSE(hit);

    //再次整体替换：E3 改了名字但本机路径保留，E4 服务端已没有、删掉，E5 新增
    std::vector<GroupFileCacheItem> second;
    second.push_back(GroupFile(301, "E3", "G3", "规范 v2.doc", kHistoryFileCatDocument, 302, 2600));
    second.push_back(GroupFile(301, "E5", "G5", "新文件.zip", kHistoryFileCatArchive, kSelf, 3000));
    REQUIRE(store.ReplaceGroupFileCache(301, second));
    REQUIRE(store.QueryGroupFileCacheOfGroup(301, rows));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_entryid == "E5");
    CHECK(rows[1].m_entryid == "E3");
    CHECK(rows[1].m_name == "规范 v2.doc");
    CHECK(rows[1].m_uploadTime == 2600);
    CHECK(rows[1].m_localPath == "C:\\下载\\规范.doc");
    GroupFileCacheItem one;
    bool found = true;
    REQUIRE(store.QueryGroupFileCache(301, "E4", one, found));
    CHECK_FALSE(found);

    //单条写入（推送新增）与删除（推送删除）
    REQUIRE(store.UpsertGroupFileCache(GroupFile(301, "E6", "G6", "推送.txt", kHistoryFileCatDocument, 302, 4000)));
    REQUIRE(store.QueryGroupFileCache(301, "E6", one, found));
    CHECK(found);
    CHECK(one.m_name == "推送.txt");
    CHECK(one.m_ext == "txt");
    REQUIRE(store.DeleteGroupFileCacheEntry(301, "E5"));
    REQUIRE(store.QueryGroupFileCacheOfGroup(301, rows));
    CHECK(rows.size() == 2);
    //参数非法的写入
    CHECK_FALSE(store.UpsertGroupFileCache(GroupFile(0, "E7", "G7", "a.txt", 1, 1, 1)));
    CHECK_FALSE(store.UpsertGroupFileCache(GroupFile(301, "", "G7", "a.txt", 1, 1, 1)));

    //按群删除不影响别的群
    REQUIRE(store.UpsertGroupFileCache(GroupFile(302, "E8", "G8", "别的群.txt", kHistoryFileCatDocument, 302, 1)));
    REQUIRE(store.DeleteGroupFileCacheOfGroup(301));
    REQUIRE(store.QueryGroupFileCacheOfGroup(301, rows));
    CHECK(rows.empty());
    REQUIRE(store.QueryGroupFileCacheOfGroup(302, rows));
    CHECK(rows.size() == 1);

    //空列表的整体替换等于清空这个群
    REQUIRE(store.ReplaceGroupFileCache(302, std::vector<GroupFileCacheItem>()));
    REQUIRE(store.QueryGroupFileCacheOfGroup(302, rows));
    CHECK(rows.empty());
}

TEST_CASE("HLM1 浏览模型的默认值：文件页、每页 50 条；切到图片页每页 120 张；非法取值按文件页", "[histfiles],[histimages]")
{
    HistoryLibraryModel model;
    CHECK(model.Filter().m_kind == kHistoryLibraryFiles);
    CHECK(model.Filter().m_pageSize == kHistoryLibraryModelFilePageSize);
    CHECK(model.CurrentPage() == 1);
    CHECK(model.PageCount() == 1);
    CHECK(model.Total() == 0);
    //未注入本地库时三种查询都返回 false
    CHECK_FALSE(model.Refresh(kNowMs));
    CHECK_FALSE(model.RefreshPage(kNowMs));
    CHECK_FALSE(model.RefreshConvCounts(kNowMs));
    CHECK(model.ConvCounts().empty());

    model.SetKind(kHistoryLibraryImages);
    CHECK(model.Filter().m_kind == kHistoryLibraryImages);
    CHECK(model.Filter().m_pageSize == kHistoryLibraryModelImagePageSize);
    const int kBadKind = 9;
    model.SetKind(kBadKind);
    CHECK(model.Filter().m_kind == kHistoryLibraryFiles);
    CHECK(model.Filter().m_pageSize == kHistoryLibraryModelFilePageSize);
}

TEST_CASE("HLM2 分页：总数与页数、换页不重新统计、页号夹取、改筛选回到第 1 页并重新统计", "[histfiles],[histimages]")
{
    //120 个文件，偶数 msgid 是本人发的；时间与 msgid 同序
    const int kFiles = 120;
    const int kTsStep = 10;
    CV2MsgStore store;
    REQUIRE(store.Open(FreshLibraryDb(L"hlm2.db").c_str()));
    for (int i = 1; i <= kFiles; ++i)
    {
        const int64_t from = (i % 2 == 0) ? kSelf : 200;
        REQUIRE(store.Upsert(FileMsg(i, kSingle, 200, from, (int64_t)i * kTsStep,
                                     "f" + std::to_string((long long)i) + ".txt", "F" + std::to_string((long long)i), i)));
    }

    HistoryLibraryModel model;
    model.SetStore(&store);
    model.SetSelfUserId(kSelf);
    REQUIRE(model.Refresh(kNowMs));
    CHECK(model.Total() == kFiles);
    CHECK(model.PageCount() == 3);
    REQUIRE(model.Items().size() == (size_t)kHistoryLibraryModelFilePageSize);
    CHECK(model.Items()[0].m_msgid == kFiles);
    CHECK(model.CountQueryTimes() == 1);

    //换到最后一页：只取当前页，不再统计总数
    model.GoToPage(3);
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.CountQueryTimes() == 1);
    CHECK(model.CurrentPage() == 3);
    REQUIRE(model.Items().size() == 20);
    CHECK(model.Items()[0].m_msgid == 20);

    //页号夹到 [1, 页数]
    model.GoToPage(99);
    CHECK(model.CurrentPage() == 3);
    model.GoToPage(0);
    CHECK(model.CurrentPage() == 1);

    //没有关键字时切换通配符不影响结果，停在原来那一页
    model.GoToPage(2);
    model.SetWildcard(true);
    CHECK(model.CurrentPage() == 2);
    model.SetWildcard(false);

    //改方向：回到第 1 页；只换页的刷新发现条件变过，会重新统计
    model.GoToPage(2);
    model.SetDirection(kHistoryLibraryDirSent);
    CHECK(model.CurrentPage() == 1);
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.CountQueryTimes() == 2);
    CHECK(model.Total() == kFiles / 2);
    CHECK(model.PageCount() == 2);

    //有关键字时切换通配符回到第 1 页
    model.GoToPage(2);
    model.SetKeyword("f1?.txt");
    CHECK(model.CurrentPage() == 1);
    REQUIRE(model.Refresh(kNowMs));
    CHECK(model.Total() == 0);   //不按通配符解释时「?」是普通字符
    model.GoToPage(1);
    model.SetWildcard(true);
    REQUIRE(model.RefreshPage(kNowMs));
    //f10、f12 … f18 这 5 个本人发的
    CHECK(model.Total() == 5);

    //图片页：每页条数改变、回到第 1 页；本组数据没有图片
    model.SetKind(kHistoryLibraryImages);
    CHECK(model.CurrentPage() == 1);
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.Total() == 0);
    CHECK(model.PageCount() == 1);
    CHECK(model.Items().empty());

    //先统计出 3 页、停在第 3 页，再缩小范围后刷新：页号按新总数夹取
    model.SetKind(kHistoryLibraryFiles);
    model.ResetFilters();
    REQUIRE(model.Refresh(kNowMs));
    model.GoToPage(3);
    model.SetDirection(kHistoryLibraryDirReceived);
    model.GoToPage(3);   //页号还按旧总数夹取，仍是第 3 页
    CHECK(model.CurrentPage() == 3);
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.Total() == kFiles / 2);
    CHECK(model.CurrentPage() == 2);
}

TEST_CASE("HLM3 按会话计数与「全部会话」之和；恢复默认筛选只清筛选条件", "[histfiles]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshLibraryDb(L"hlm3.db").c_str()));
    SeedLibrary(store);

    HistoryLibraryModel model;
    model.SetStore(&store);
    model.SetSelfUserId(kSelf);
    REQUIRE(model.RefreshConvCounts(kNowMs));
    CHECK(model.CountOf(kSingle, 200) == 2);
    CHECK(model.CountOf(kSingle, 201) == 1);
    CHECK(model.CountOf(kGroup, 300) == 4);
    CHECK(model.CountOf(kGroup, 301) == 2);
    CHECK(model.CountOf(kSingle, 999) == 0);
    CHECK(model.CountAll() == 9);

    //改了一组筛选条件后恢复默认：汇总哪一类与本人 userid 不变，其余回到初始值
    model.SetKind(kHistoryLibraryImages);
    model.SetDirection(kHistoryLibraryDirSent);
    model.SetSource(kHistoryLibrarySourceGroup);
    model.SetCategory(kHistoryFileCatVideo);
    model.SetTimeRange(kHistoryTimeCustom);
    model.SetKeyword("abc");
    model.SetWildcard(true);
    model.SetConversation(kGroup, 300);
    model.SetCtypeOnly(kGroup);
    model.ResetFilters();
    const HistoryLibraryFilter& f = model.Filter();
    CHECK(f.m_kind == kHistoryLibraryImages);
    CHECK(f.m_selfUserId == kSelf);
    CHECK(f.m_direction == kHistoryLibraryDirAll);
    CHECK(f.m_source == kHistoryLibrarySourceAll);
    CHECK(f.m_category == kHistoryFileCatAll);
    CHECK(f.m_range == kHistoryTimeAll);
    CHECK(f.m_keyword.empty());
    CHECK_FALSE(f.m_wildcard);
    CHECK(f.m_cid == 0);
    CHECK(f.m_ctypeOnly == 0);
    CHECK(f.m_page == 1);

    //解绑本地库后查询返回 false，按会话计数清空
    model.SetStore(NULL);
    CHECK_FALSE(model.RefreshConvCounts(kNowMs));
    CHECK(model.CountAll() == 0);
}

namespace {

//帖子卡片与补充卡片的消息类型（与 net/Protocol.h 一致）
const int kTypeThreadPost  = 25;
const int kTypeThreadReply = 26;
//帖子群
const int64_t kThreadGroup = 400;

//构造一条帖子卡片或补充卡片：content 与 BuildContentJson 的格式相同（卡片内容整体放在 threadcard 子对象里）
HistoryRecord ThreadCardMsg(int64_t msgid, int msgtype, int64_t fromid, int64_t ts, const std::string& cardJson)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = kGroup;
    rec.m_cid = kThreadGroup;
    rec.m_fromid = fromid;
    rec.m_msgtype = msgtype;
    rec.m_timestamp = ts;
    rec.m_content = "{\"threadcard\":" + cardJson + "}";
    return rec;
}

//一张图片的 JSON
std::string CardImageJson(const std::string& id, int size)
{
    return "{\"fileid\":\"F" + id + "\",\"md5\":\"M" + id + "\",\"width\":10,\"height\":10,\"size\":"
         + std::to_string((long long)size) + ",\"thumbfileid\":\"\"}";
}

/**
 *  帖子群 400 的测试数据（时间越大越新）：
 *    41 帖子卡片 本人 1000 三张图 A B C        42 补充卡片 对方 2000 一张图 D
 *    43 帖子卡片 对方 3000 已删除，仍带图 E     44 帖子卡片 对方 4000 没有图片
 *    45 帖子卡片 对方 4500 content 不是 JSON    46 图片消息 对方 5000
 */
void SeedThreadCards(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(ThreadCardMsg(41, kTypeThreadPost, kSelf, 1000,
        "{\"postid\":\"1\",\"title\":\"t\",\"images\":[" + CardImageJson("A", 11) + "," + CardImageJson("B", 12)
        + "," + CardImageJson("C", 13) + "]}")));
    REQUIRE(store.Upsert(ThreadCardMsg(42, kTypeThreadReply, 401, 2000,
        "{\"postid\":\"1\",\"kind\":3,\"images\":[" + CardImageJson("D", 14) + "]}")));
    REQUIRE(store.Upsert(ThreadCardMsg(43, kTypeThreadPost, 401, 3000,
        "{\"postid\":\"2\",\"deleted\":1,\"images\":[" + CardImageJson("E", 15) + "]}")));
    REQUIRE(store.Upsert(ThreadCardMsg(44, kTypeThreadPost, 401, 4000, "{\"postid\":\"3\",\"title\":\"x\"}")));
    HistoryRecord broken = ThreadCardMsg(45, kTypeThreadPost, 401, 4500, "{}");
    broken.m_content = "not json";
    REQUIRE(store.Upsert(broken));
    REQUIRE(store.Upsert(ImageMsg(46, kGroup, kThreadGroup, 401, 5000, kTypeImage)));
}

} // namespace

TEST_CASE("TGE14 帖子卡片里的图片：消息记录窗口的图片分类与聊天记录管理器的图片页", "[threadgroup],[histimages]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshLibraryDb(L"tge14.db").c_str()));
    SeedThreadCards(store);

    SECTION("消息记录窗口的「图片」分类：带图片的卡片与图片消息一条一行，删除的、没图的、坏数据不收录")
    {
        HistoryFilter f;
        f.m_ctype = kGroup;
        f.m_cid = kThreadGroup;
        f.m_type = kHistoryTypeImage;
        f.m_pageSize = kBigPage;
        CHECK(store.Count(f, kNowMs) == 3);
        std::vector<HistoryRecord> page;
        REQUIRE(store.QueryPage(f, kNowMs, page));
        REQUIRE(page.size() == 3);
        CHECK(page[0].m_msgid == 46);
        CHECK(page[1].m_msgid == 42);
        CHECK(page[2].m_msgid == 41);
    }

    SECTION("聊天记录管理器的图片页：每张图片一项，同一张卡片按原顺序，带序号与下载信息")
    {
        HistoryLibraryFilter f;
        f.m_kind = kHistoryLibraryImages;
        f.m_selfUserId = kSelf;
        f.m_pageSize = kBigPage;
        int64_t total = 0;
        REQUIRE(store.CountLibrary(f, kNowMs, total));
        CHECK(total == 5);
        std::vector<HistoryLibraryItem> items;
        REQUIRE(store.QueryLibraryPage(f, kNowMs, items));
        REQUIRE(items.size() == 5);
        //46 图片消息本身
        CHECK(items[0].m_msgid == 46);
        CHECK(items[0].m_cardImageIndex == -1);
        CHECK(items[0].m_fileid == "I46");
        //42 补充卡片的一张
        CHECK(items[1].m_msgid == 42);
        CHECK(items[1].m_cardImageIndex == 0);
        CHECK(items[1].m_fileid == "FD");
        CHECK(items[1].m_md5 == "MD");
        CHECK(items[1].m_size == 14);
        CHECK_FALSE(items[1].m_isGroupFile);
        CHECK(items[1].m_entryid.empty());
        //41 帖子卡片的三张，按卡片里的顺序
        const char* const kExpectIds[] = { "FA", "FB", "FC" };
        for (int k = 0; k < 3; ++k)
        {
            INFO("41 的第 " << k << " 张");
            CHECK(items[2 + k].m_msgid == 41);
            CHECK(items[2 + k].m_cardImageIndex == k);
            CHECK(items[2 + k].m_fileid == kExpectIds[k]);
            CHECK(items[2 + k].m_msgtype == kTypeThreadPost);
            //看图时从整张卡片取全部图片
            CHECK(items[2 + k].m_content.find("threadcard") != std::string::npos);
        }

        //方向：我发送的只有 41 的三张
        f.m_direction = kHistoryLibraryDirSent;
        REQUIRE(store.CountLibrary(f, kNowMs, total));
        CHECK(total == 3);
        f.m_direction = kHistoryLibraryDirAll;

        //按会话统计：帖子群 400 共 5 项
        std::vector<HistoryLibraryConvCount> counts;
        REQUIRE(store.CountLibraryByConversation(f, kNowMs, counts));
        REQUIRE(counts.size() == 1);
        CHECK(counts[0].m_ctype == kGroup);
        CHECK(counts[0].m_cid == kThreadGroup);
        CHECK(counts[0].m_count == 5);

        //分页：每页 2 项时第 2 页是 41 的前两张
        f.m_pageSize = 2;
        f.m_page = 2;
        REQUIRE(store.QueryLibraryPage(f, kNowMs, items));
        REQUIRE(items.size() == 2);
        CHECK(items[0].m_cardImageIndex == 0);
        CHECK(items[1].m_cardImageIndex == 1);
    }

    SECTION("文件页、链接页不收录卡片里的图片")
    {
        HistoryLibraryFilter f;
        f.m_kind = kHistoryLibraryFiles;
        f.m_selfUserId = kSelf;
        f.m_pageSize = kBigPage;
        int64_t total = 0;
        REQUIRE(store.CountLibrary(f, kNowMs, total));
        CHECK(total == 0);
        f.m_kind = kHistoryLibraryLinks;
        REQUIRE(store.CountLibrary(f, kNowMs, total));
        CHECK(total == 0);
    }
}
