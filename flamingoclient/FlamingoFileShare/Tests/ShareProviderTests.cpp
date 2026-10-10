/**
 *  分享者一端访问会话请求处理器单元测试（Suite SPV，标签 [shareprovider]），ShareProviderTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 12 步的 12c；用假通道收集发出的帧，在测试程序目录下建真实的文件）：
 *    - SPV1 Start 发 sf_hello：版本、共享类型、是否含隐藏文件、权限；根名只在单文件共享时给（文件名），文件夹与磁盘
 *           共享不给（取自真实路径，2026-10-04 起）；RootDisplayName 的取法（文件夹取最后一段、磁盘取 "X:"）；
 *           ResendHello 重发同样的内容，收到访问者的任何一帧后 HasHeardFromVisitor 为真；
 *    - SPV2 sf_list：列根目录与子目录；大目录按游标分批取，各批来自同一份快照，取完后重新列才看到新条目；
 *           不存在回 809、路径非法回 808、游标非法回 3、单文件共享回 820；
 *    - SPV3 隐藏与重解析点：未含隐藏文件时经过隐藏目录的列目录与查条目回 809、含隐藏文件时照常；
 *           经目录联接走出共享根的回 808；
 *    - SPV4 sf_stat：文件夹共享里的文件与根本身，单文件共享的根（空路径）与非空路径，不存在，共享根已被删除；
 *    - SPV5 SetPerms 发 sf_perms 并立即生效（之后没有新建权限的 sf_mkdir 回 806），sf_bye 转告监听者，不存在的图片的
 *           缩略图回 809、没有命中的搜索回已结束的空结果，方向不对的帧与解不开的帧忽略、不回任何帧，SendBye 发 sf_bye；
 *    - SPV6 写操作（第 2 期第 2 步）：四种操作各自按权限判定，执行后回 sf_op_result（多条目逐条），每个条目一条审计
 *           交给监听者（动作、路径、目标、结果）；删除经注入的假回收站；单文件共享回 820；空的多条目请求回 3；
 *    - SPV7 搜索（第 2 期第 3 步，没有线程池时同步执行）：命中按批回 sf_search_result、路径相对共享根、最后一批 done；
 *           条数上限来自上下文；关键字不合法回 3、起点不存在或是文件回 809，都是带结果码的、已结束的空结果；
 *    - SPV8 经线程池异步执行：写操作的结果与审计在工作线程上回来；搜索任务排在队列里时会话被销毁，任务随后执行时
 *           不再发帧（核心已断开、取消标志已置位），通道可以先于任务销毁；
 *    - SPV10 列整棵目录树（第 2 期第 5 步）：不要关键字，列出目录之下的全部条目；按访问者要的上限截断；
 *    - SPV9 缩略图（第 2 期第 4 步）：图片回 sf_thumb_result（尺寸与 JPEG 附件）；不是图片回 3，目录与不存在回 809，
 *           路径非法回 808；排在队列里时收到 sf_cancel 则不回结果。
 *  本文件由 FlamingoUnit 编译，被测源文件 Engine/ShareProvider.cpp 同时编进 FlamingoFileShare.dll。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include <mutex>
#include <thread>
#include <objidl.h>
#include <gdiplus.h>

#include "../Engine/ShareProvider.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的临时目录名（每个用例开始前整个删掉）
const wchar_t* const kSpvTestDir = L"spv_test";

//SPV2：大目录里的文件数，按每批 500 条应分 3 批取完
const size_t kBigDirFiles = 1200;
const size_t kBigDirBatches = 3;

//几个测试用的标识与权限
const char* const kAccessId = "share-1-1759370000000";
const int64_t kShareId = 4096;
const int64_t kVisitorId = 10002;
const int64_t kReqId = 77;
const int kPermsInitial = share_perm_upload | share_perm_mkdir;
const int kPermsChanged = share_perm_delete;
const int kByeErrCode = 0;

//小文件的字节数
const size_t kSmallFileBytes = 16;

//假通道：把发出的帧逐个解码后收集起来
class FakeChannel : public IShareChannel
{
public:
    virtual bool SendFrame(const std::string& frame) override
    {
        ShareControlMessage msg;
        REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), msg));
        m_sent.push_back(msg);
        return true;
    }

    //取出并清空已发出的帧
    std::vector<ShareControlMessage> Take()
    {
        std::vector<ShareControlMessage> out;
        out.swap(m_sent);
        return out;
    }

private:
    std::vector<ShareControlMessage> m_sent;   //已发出、尚未取走的帧
};

//假监听者：记下 sf_bye 与写操作完成的回调
class FakeListener : public IShareProviderListener
{
public:
    FakeListener()
        : m_byeCount(0)
        , m_byeErrCode(-1)
    {
    }

    virtual void OnVisitorBye(const std::string& accessId, int errCode) override
    {
        ++m_byeCount;
        m_byeAccessId = accessId;
        m_byeErrCode = errCode;
    }

    virtual void OnWriteOpDone(const std::string& accessId, const std::vector<ShareAuditEntry>& audits) override
    {
        m_writeAccessId = accessId;
        m_audits = audits;
    }

    int                          m_byeCount;      //回调次数
    std::string                  m_byeAccessId;   //最近一次回调的访问会话 id
    int                          m_byeErrCode;    //最近一次回调的结束原因
    std::string                  m_writeAccessId; //最近一次写操作回调的访问会话 id
    std::vector<ShareAuditEntry> m_audits;        //最近一次写操作的审计记录
};

//假回收站：记下交给它的路径并真的删掉（模拟进了回收站）
class FakeRecycler : public IShareRecycler
{
public:
    virtual int CheckRecyclable(const std::wstring& anyPath, int64_t totalBytes) override
    {
        (void)anyPath;
        (void)totalBytes;
        return error_code_ok;
    }

    virtual int Recycle(const std::wstring& absPath) override
    {
        m_recycled.push_back(absPath);
        sharetestfs::DeleteTree(absPath);
        return error_code_ok;
    }

    std::vector<std::wstring> m_recycled;   //交给 Recycle 的路径
};

//本套用例的临时目录（每次调用都先清空再建）
std::wstring FreshBase()
{
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSpvTestDir;
    sharetestfs::DeleteTree(base);
    sharetestfs::MakeDirs(base);
    return base;
}

//按共享根与类型构造上下文
ShareProviderContext MakeContext(const std::wstring& root, int kind, bool includeHidden)
{
    ShareProviderContext ctx;
    ctx.m_accessId = kAccessId;
    ctx.m_shareId = kShareId;
    ctx.m_visitorId = kVisitorId;
    ctx.m_visitorName = "visitor";
    ctx.m_rootPath = root;
    ctx.m_kind = kind;
    ctx.m_includeHidden = includeHidden;
    ctx.m_perms = kPermsInitial;
    return ctx;
}

//构造一个请求帧并交给处理器
void Feed(CShareProvider& provider, int type, const std::string& path, const std::string& cursor)
{
    ShareControlMessage req;
    req.m_type = type;
    req.m_reqId = kReqId;
    req.m_path = path;
    req.m_cursor = cursor;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(req, frame));
    provider.OnFrame(frame.data(), frame.size());
}

//发一个请求，要求恰好回一帧并返回它
ShareControlMessage Ask(CShareProvider& provider, FakeChannel& channel, int type, const std::string& path,
                        const std::string& cursor)
{
    Feed(provider, type, path, cursor);
    std::vector<ShareControlMessage> sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_reqId == kReqId);
    return sent[0];
}

//条目名列表
std::vector<std::string> Names(const std::vector<ShareEntry>& entries)
{
    std::vector<std::string> names;
    for (size_t i = 0; i < entries.size(); ++i)
        names.push_back(entries[i].m_name);
    return names;
}

} // namespace

TEST_CASE("SPV1 Start 发 sf_hello", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    REQUIRE(sharetestfs::MakeDirs(base + L"\\Projects"));

    FakeChannel channel;
    CShareProvider provider(MakeContext(base + L"\\Projects\\", share_kind_folder, true), &channel, NULL);
    REQUIRE(provider.Start());
    std::vector<ShareControlMessage> sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_hello);
    CHECK(sent[0].m_version == kShareControlVersion);
    CHECK(sent[0].m_kind == share_kind_folder);
    //文件夹共享不给根名：根名取自真实路径（文件夹名），2026-10-04 起不发给访问者
    CHECK(sent[0].m_rootName.empty());
    CHECK(sent[0].m_includeHidden);
    CHECK(sent[0].m_perms == kPermsInitial);
    CHECK(provider.GetAccessId() == kAccessId);
    CHECK(provider.GetShareId() == kShareId);
    CHECK(provider.GetVisitorId() == kVisitorId);

    //重发：内容与首帧相同；收到访问者的任何一帧之后 HasHeardFromVisitor 为真（连接线程据此停止重发）
    CHECK_FALSE(provider.HasHeardFromVisitor());
    REQUIRE(provider.ResendHello());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_hello);
    CHECK(sent[0].m_rootName.empty());
    const char garbage[] = { 0x7F };
    provider.OnFrame(garbage, sizeof(garbage));
    CHECK(provider.HasHeardFromVisitor());
    CHECK(channel.Take().empty());

    //磁盘共享的根名为 "X:"
    CHECK(CShareProvider::RootDisplayName(L"D:\\") == "D:");
    CHECK(CShareProvider::RootDisplayName(L"D:") == "D:");
    CHECK(CShareProvider::RootDisplayName(L"D:\\a\\report.docx") == "report.docx");

    //单文件共享照旧给根名：就是那个文件的名字，访问者下载时要用
    REQUIRE(sharetestfs::WriteFileBytes(base + L"\\report.docx", 1));
    FakeChannel fileChannel;
    CShareProvider fileProvider(MakeContext(base + L"\\report.docx", share_kind_file, false), &fileChannel, NULL);
    REQUIRE(fileProvider.Start());
    sent = fileChannel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_kind == share_kind_file);
    CHECK(sent[0].m_rootName == "report.docx");

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SPV2 sf_list 列目录与按游标分批", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs\\deep"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\big"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\a.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\b.txt", kSmallFileBytes));
    for (size_t i = 0; i < kBigDirFiles; ++i)
    {
        wchar_t name[32] = { 0 };
        ::wsprintfW(name, L"\\big\\f%04u.dat", static_cast<unsigned>(i));
        REQUIRE(sharetestfs::WriteFileBytes(root + name, 0));
    }

    FakeChannel channel;
    CShareProvider provider(MakeContext(root, share_kind_folder, false), &channel, NULL);

    //根目录与子目录
    ShareControlMessage r = Ask(provider, channel, sf_list, "", "");
    CHECK(r.m_type == sf_list_result);
    CHECK(r.m_code == error_code_ok);
    std::vector<std::string> expected;
    expected.push_back("big");
    expected.push_back("docs");
    expected.push_back("a.txt");
    CHECK(Names(r.m_entries) == expected);
    CHECK_FALSE(r.m_more);

    r = Ask(provider, channel, sf_list, "docs", "");
    REQUIRE(r.m_entries.size() == 2);
    CHECK(r.m_entries[0].m_name == "deep");
    CHECK(r.m_entries[1].m_name == "b.txt");

    //大目录：逐批取，中途新增一个文件，本轮各批仍来自首批时的快照
    size_t total = 0;
    size_t batches = 0;
    std::string cursor;
    while (true)
    {
        r = Ask(provider, channel, sf_list, "big", cursor);
        REQUIRE(r.m_code == error_code_ok);
        total += r.m_entries.size();
        ++batches;
        if (batches == 1)
            REQUIRE(sharetestfs::WriteFileBytes(root + L"\\big\\zzz_new.dat", 0));
        if (!r.m_more)
            break;
        cursor = r.m_cursor;
    }
    CHECK(batches == kBigDirBatches);
    CHECK(total == kBigDirFiles);

    //重新列（不带游标）才看到新文件
    total = 0;
    cursor.clear();
    while (true)
    {
        r = Ask(provider, channel, sf_list, "big", cursor);
        total += r.m_entries.size();
        if (!r.m_more)
            break;
        cursor = r.m_cursor;
    }
    CHECK(total == kBigDirFiles + 1);

    //失败的几种情况：只回结果码、没有条目
    r = Ask(provider, channel, sf_list, "nosuch", "");
    CHECK(r.m_code == error_code_share_path_not_found);
    CHECK(r.m_entries.empty());
    CHECK(Ask(provider, channel, sf_list, "a.txt", "").m_code == error_code_share_path_not_found);
    CHECK(Ask(provider, channel, sf_list, "../x", "").m_code == error_code_share_path_invalid);
    CHECK(Ask(provider, channel, sf_list, "docs\\deep", "").m_code == error_code_share_path_invalid);
    CHECK(Ask(provider, channel, sf_list, "docs", "abc").m_code == error_code_bad_request);

    //单文件共享没有目录可列
    FakeChannel fileChannel;
    CShareProvider fileProvider(MakeContext(root + L"\\a.txt", share_kind_file, false), &fileChannel, NULL);
    CHECK(Ask(fileProvider, fileChannel, sf_list, "", "").m_code == error_code_share_kind_mismatch);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SPV3 隐藏条目与重解析点", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\secret\\inner"));
    REQUIRE(sharetestfs::MakeDirs(base + L"\\outside"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\secret\\x.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\hidden.txt", kSmallFileBytes));
    REQUIRE(::SetFileAttributesW((root + L"\\secret").c_str(), FILE_ATTRIBUTE_HIDDEN));
    REQUIRE(::SetFileAttributesW((root + L"\\hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN));
    REQUIRE(sharetestfs::RunCmd(L"mklink /J \"" + root + L"\\toOutside\" \"" + base + L"\\outside\"") == 0);

    //未含隐藏文件：根目录只列出目录联接；经过隐藏目录的请求一律回 809
    FakeChannel channel;
    CShareProvider provider(MakeContext(root, share_kind_folder, false), &channel, NULL);
    ShareControlMessage r = Ask(provider, channel, sf_list, "", "");
    std::vector<std::string> expected;
    expected.push_back("toOutside");
    CHECK(Names(r.m_entries) == expected);
    CHECK(Ask(provider, channel, sf_list, "secret", "").m_code == error_code_share_path_not_found);
    CHECK(Ask(provider, channel, sf_list, "secret/inner", "").m_code == error_code_share_path_not_found);
    CHECK(Ask(provider, channel, sf_stat, "secret/x.txt", "").m_code == error_code_share_path_not_found);
    CHECK(Ask(provider, channel, sf_stat, "hidden.txt", "").m_code == error_code_share_path_not_found);

    //经目录联接走出共享根：808
    CHECK(Ask(provider, channel, sf_list, "toOutside", "").m_code == error_code_share_path_invalid);
    CHECK(Ask(provider, channel, sf_stat, "toOutside", "").m_code == error_code_share_path_invalid);

    //含隐藏文件：照常
    FakeChannel channel2;
    CShareProvider withHidden(MakeContext(root, share_kind_folder, true), &channel2, NULL);
    r = Ask(withHidden, channel2, sf_list, "secret", "");
    CHECK(r.m_code == error_code_ok);
    CHECK(r.m_entries.size() == 2);
    CHECK(Ask(withHidden, channel2, sf_stat, "hidden.txt", "").m_code == error_code_ok);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SPV4 sf_stat 查条目", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\Reports";
    REQUIRE(sharetestfs::MakeDirs(root));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\q3.xlsx", kSmallFileBytes));

    FakeChannel channel;
    CShareProvider provider(MakeContext(root, share_kind_folder, false), &channel, NULL);
    ShareControlMessage r = Ask(provider, channel, sf_stat, "q3.xlsx", "");
    CHECK(r.m_type == sf_stat_result);
    CHECK(r.m_code == error_code_ok);
    REQUIRE(r.m_entries.size() == 1);
    CHECK(r.m_entries[0].m_name == "q3.xlsx");
    CHECK(r.m_entries[0].m_size == static_cast<int64_t>(kSmallFileBytes));

    //根本身：名字取共享根的显示名
    r = Ask(provider, channel, sf_stat, "", "");
    REQUIRE(r.m_entries.size() == 1);
    CHECK(r.m_entries[0].m_name == "Reports");
    CHECK(r.m_entries[0].m_isDir);

    r = Ask(provider, channel, sf_stat, "nosuch.txt", "");
    CHECK(r.m_code == error_code_share_path_not_found);
    CHECK(r.m_entries.empty());

    //单文件共享：只接受空路径
    FakeChannel fileChannel;
    CShareProvider fileProvider(MakeContext(root + L"\\q3.xlsx", share_kind_file, false), &fileChannel, NULL);
    r = Ask(fileProvider, fileChannel, sf_stat, "", "");
    CHECK(r.m_code == error_code_ok);
    REQUIRE(r.m_entries.size() == 1);
    CHECK(r.m_entries[0].m_name == "q3.xlsx");
    CHECK_FALSE(r.m_entries[0].m_isDir);
    CHECK(Ask(fileProvider, fileChannel, sf_stat, "q3.xlsx", "").m_code == error_code_share_path_invalid);

    //共享根被删除：列目录与查条目都回 809
    sharetestfs::DeleteTree(root);
    CHECK(Ask(provider, channel, sf_list, "", "").m_code == error_code_share_path_not_found);
    CHECK(Ask(provider, channel, sf_stat, "q3.xlsx", "").m_code == error_code_share_path_not_found);
    CHECK(Ask(fileProvider, fileChannel, sf_stat, "", "").m_code == error_code_share_path_not_found);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SPV5 权限变化、sf_bye、未实现的请求与异常帧", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root));

    FakeChannel channel;
    FakeListener listener;
    CShareProvider provider(MakeContext(root, share_kind_folder, false), &channel, &listener);

    //SetPerms：发 sf_perms，GetPerms 立即返回新值
    REQUIRE(provider.SetPerms(kPermsChanged));
    std::vector<ShareControlMessage> sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_perms);
    CHECK(sent[0].m_perms == kPermsChanged);
    CHECK(provider.GetPerms() == kPermsChanged);

    //sf_bye：转告监听者，不回帧
    ShareControlMessage bye;
    bye.m_type = sf_bye;
    bye.m_errCode = kByeErrCode;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(bye, frame));
    provider.OnFrame(frame.data(), frame.size());
    CHECK(channel.Take().empty());
    CHECK(listener.m_byeCount == 1);
    CHECK(listener.m_byeAccessId == kAccessId);
    CHECK(listener.m_byeErrCode == kByeErrCode);

    //权限已改为只能删除：新建文件夹回 806，逐条结果同样是 806
    ShareControlMessage r = Ask(provider, channel, sf_mkdir, "newdir", "");
    CHECK(r.m_type == sf_op_result);
    CHECK(r.m_code == error_code_share_permission_denied);
    REQUIRE(r.m_items.size() == 1);
    CHECK(r.m_items[0].m_code == error_code_share_permission_denied);
    //缩略图与搜索（第 2 期起已实现，详见 SPV7、SPV9）：不存在的图片回 809，没有命中的搜索回已结束的空结果
    ShareControlMessage thumbReq;
    thumbReq.m_type = sf_thumb;
    thumbReq.m_reqId = kReqId;
    thumbReq.m_path = "a.jpg";
    REQUIRE(ShareControlFraming::BuildFrame(thumbReq, frame));
    provider.OnFrame(frame.data(), frame.size());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_thumb_result);
    CHECK(sent[0].m_code == error_code_share_path_not_found);
    ShareControlMessage searchReq;
    searchReq.m_type = sf_search;
    searchReq.m_reqId = kReqId;
    searchReq.m_keyword = "zzz-no-such-name";
    REQUIRE(ShareControlFraming::BuildFrame(searchReq, frame));
    provider.OnFrame(frame.data(), frame.size());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_search_result);
    CHECK(sent[0].m_done);
    CHECK(sent[0].m_entries.empty());

    //方向不对的帧（访问者发来 sf_hello）与解不开的帧：忽略
    ShareControlMessage hello;
    hello.m_type = sf_hello;
    REQUIRE(ShareControlFraming::BuildFrame(hello, frame));
    provider.OnFrame(frame.data(), frame.size());
    const char garbage[] = { 0x21, 0x00, 0x00 };
    provider.OnFrame(garbage, sizeof(garbage));
    provider.OnFrame(NULL, 0);
    CHECK(channel.Take().empty());

    //SendBye：发 sf_bye
    REQUIRE(provider.SendBye(error_code_share_canceled));
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_bye);
    CHECK(sent[0].m_errCode == error_code_share_canceled);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SPV6 写操作经处理器执行并回报审计", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dst"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\a.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\b.txt", kSmallFileBytes));

    FakeChannel channel;
    FakeListener listener;
    FakeRecycler recycler;
    ShareProviderContext ctx = MakeContext(root, share_kind_folder, false);
    ctx.m_perms = share_perm_mkdir | share_perm_rename | share_perm_delete | share_perm_move;
    CShareProvider provider(ctx, &channel, &listener);
    provider.SetRecycler(&recycler);
    std::string frame;

    //新建文件夹
    ShareControlMessage r = Ask(provider, channel, sf_mkdir, "docs/new", "");
    CHECK(r.m_type == sf_op_result);
    CHECK(r.m_code == error_code_ok);
    REQUIRE(r.m_items.size() == 1);
    CHECK(r.m_items[0].m_path == "docs/new");
    CHECK(::GetFileAttributesW(ShareToLongPath(root + L"\\docs\\new").c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(listener.m_writeAccessId == kAccessId);
    REQUIRE(listener.m_audits.size() == 1);
    CHECK(listener.m_audits[0].m_action == share_audit_mkdir);
    CHECK(listener.m_audits[0].m_shareId == kShareId);
    CHECK(listener.m_audits[0].m_visitorId == kVisitorId);
    CHECK(listener.m_audits[0].m_path == "docs/new");
    CHECK(listener.m_audits[0].m_result == error_code_ok);
    CHECK(listener.m_audits[0].m_clientTime > 0);

    //重命名：同名冲突回 807，审计如实记下失败与新名字
    ShareControlMessage rename;
    rename.m_type = sf_rename;
    rename.m_reqId = kReqId;
    rename.m_path = "docs/a.txt";
    rename.m_newName = "b.txt";
    REQUIRE(ShareControlFraming::BuildFrame(rename, frame));
    provider.OnFrame(frame.data(), frame.size());
    std::vector<ShareControlMessage> sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_share_name_conflict);
    REQUIRE(listener.m_audits.size() == 1);
    CHECK(listener.m_audits[0].m_action == share_audit_rename);
    CHECK(listener.m_audits[0].m_toPath == "b.txt");
    CHECK(listener.m_audits[0].m_result == error_code_share_name_conflict);

    //移动两条：逐条结果，审计各一条、目标为目标目录
    ShareControlMessage move;
    move.m_type = sf_move;
    move.m_reqId = kReqId;
    move.m_paths.push_back("docs/a.txt");
    move.m_paths.push_back("docs/none.txt");
    move.m_toDir = "dst";
    REQUIRE(ShareControlFraming::BuildFrame(move, frame));
    provider.OnFrame(frame.data(), frame.size());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_share_path_not_found);
    REQUIRE(sent[0].m_items.size() == 2);
    CHECK(sent[0].m_items[0].m_code == error_code_ok);
    CHECK(sent[0].m_items[1].m_code == error_code_share_path_not_found);
    REQUIRE(listener.m_audits.size() == 2);
    CHECK(listener.m_audits[0].m_action == share_audit_move);
    CHECK(listener.m_audits[0].m_toPath == "dst");

    //删除：经注入的回收站
    ShareControlMessage del;
    del.m_type = sf_delete;
    del.m_reqId = kReqId;
    del.m_paths.push_back("dst/a.txt");
    REQUIRE(ShareControlFraming::BuildFrame(del, frame));
    provider.OnFrame(frame.data(), frame.size());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_ok);
    REQUIRE(recycler.m_recycled.size() == 1);
    CHECK(recycler.m_recycled[0] == root + L"\\dst\\a.txt");
    CHECK(listener.m_audits[0].m_action == share_audit_delete);

    //空的多条目请求
    ShareControlMessage empty;
    empty.m_type = sf_delete;
    empty.m_reqId = kReqId;
    REQUIRE(ShareControlFraming::BuildFrame(empty, frame));
    provider.OnFrame(frame.data(), frame.size());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_bad_request);

    //收回删除权限之后再删：806，不交给回收站
    provider.SetPerms(share_perm_mkdir);
    channel.Take();
    REQUIRE(ShareControlFraming::BuildFrame(del, frame));
    provider.OnFrame(frame.data(), frame.size());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_share_permission_denied);
    CHECK(recycler.m_recycled.size() == 1);

    //单文件共享：820
    FakeChannel fileChannel;
    ShareProviderContext fileCtx = MakeContext(root + L"\\docs\\b.txt", share_kind_file, false);
    fileCtx.m_perms = share_perm_mkdir;
    CShareProvider fileProvider(fileCtx, &fileChannel, NULL);
    CHECK(Ask(fileProvider, fileChannel, sf_mkdir, "x", "").m_code == error_code_share_kind_mismatch);

    sharetestfs::DeleteTree(base);
}

namespace {

//SPV7：搜索的条数上限（小于命中数，验证上限来自上下文）
const int kSpvSearchMax = 3;

//SPV8：等工作线程回结果的最长时间，单位：毫秒
const DWORD kSpvAsyncWaitMs = 5000;

//可在多个线程上使用的假通道：工作线程也会发帧，不能在发帧里断言（Catch2 的断言不是线程安全的）
class LockedChannel : public IShareChannel
{
public:
    virtual bool SendFrame(const std::string& frame) override
    {
        ShareControlMessage msg;
        if (!ShareControlFraming::DecodeFrame(frame.data(), frame.size(), msg))
            return false;
        std::lock_guard<std::mutex> guard(m_mutex);
        m_sent.push_back(msg);
        return true;
    }

    //取出并清空已发出的帧
    std::vector<ShareControlMessage> Take()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::vector<ShareControlMessage> out;
        out.swap(m_sent);
        return out;
    }

    //已发出、尚未取走的帧数
    size_t Count()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_sent.size();
    }

private:
    std::mutex                       m_mutex;  //保护 m_sent
    std::vector<ShareControlMessage> m_sent;   //已发出、尚未取走的帧
};

//可在多个线程上使用的假监听者：只记写操作的审计条数
class LockedListener : public IShareProviderListener
{
public:
    LockedListener()
        : m_writeReports(0)
    {
    }

    virtual void OnVisitorBye(const std::string& accessId, int errCode) override
    {
        (void)accessId;
        (void)errCode;
    }

    virtual void OnWriteOpDone(const std::string& accessId, const std::vector<ShareAuditEntry>& audits) override
    {
        (void)accessId;
        (void)audits;
        ++m_writeReports;
    }

    std::atomic<int> m_writeReports;   //写操作回报的次数
};

//阻塞任务：占住唯一的工作线程，直到放行
class BlockTask : public IShareTask
{
public:
    explicit BlockTask(HANDLE release)
        : m_release(release)
    {
    }

    virtual void Run() override
    {
        ::WaitForSingleObject(m_release, kSpvAsyncWaitMs);
    }

private:
    HANDLE m_release;   //放行事件
};

//发一个搜索请求
void FeedSearch(CShareProvider& provider, int64_t reqId, const std::string& path, const std::string& keyword)
{
    ShareControlMessage req;
    req.m_type = sf_search;
    req.m_reqId = reqId;
    req.m_path = path;
    req.m_keyword = keyword;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(req, frame));
    provider.OnFrame(frame.data(), frame.size());
}

} // namespace

TEST_CASE("SPV7 搜索经处理器执行", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs\\sub"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\plan-a.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\sub\\PLAN-b.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\plan-c.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\other.txt", kSmallFileBytes));

    FakeChannel channel;
    CShareProvider provider(MakeContext(root, share_kind_folder, false), &channel, NULL);
    FeedSearch(provider, kReqId, "docs", "plan");
    std::vector<ShareControlMessage> sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_search_result);
    CHECK(sent[0].m_reqId == kReqId);
    CHECK(sent[0].m_done);
    CHECK_FALSE(sent[0].m_truncated);
    CHECK(sent[0].m_code == 0);
    std::vector<std::string> names = Names(sent[0].m_entries);
    std::sort(names.begin(), names.end());
    std::vector<std::string> expected;
    expected.push_back("docs/plan-a.txt");
    expected.push_back("docs/sub/PLAN-b.txt");
    CHECK(names == expected);
    CHECK(provider.GetRunningSearchCount() == 0);

    //条数上限来自上下文
    REQUIRE(sharetestfs::MakeDirs(root + L"\\many"));
    for (int i = 0; i < kSpvSearchMax * 3; ++i)
    {
        wchar_t name[32] = { 0 };
        ::wsprintfW(name, L"\\many\\plan%02d.txt", i);
        REQUIRE(sharetestfs::WriteFileBytes(root + name, 0));
    }
    FakeChannel capChannel;
    ShareProviderContext capCtx = MakeContext(root, share_kind_folder, false);
    capCtx.m_searchMaxResults = kSpvSearchMax;
    CShareProvider capped(capCtx, &capChannel, NULL);
    FeedSearch(capped, kReqId, "", "plan");
    sent = capChannel.Take();
    REQUIRE_FALSE(sent.empty());
    size_t hits = 0;
    for (size_t i = 0; i < sent.size(); ++i)
        hits += sent[i].m_entries.size();
    CHECK(hits == static_cast<size_t>(kSpvSearchMax));
    CHECK(sent.back().m_done);
    CHECK(sent.back().m_truncated);

    //失败：带结果码的、已结束的空结果
    FeedSearch(provider, kReqId, "docs", "");
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_bad_request);
    CHECK(sent[0].m_done);
    FeedSearch(provider, kReqId, "nope", "plan");
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_share_path_not_found);
    FeedSearch(provider, kReqId, "other.txt", "plan");
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_share_path_not_found);
    FeedSearch(provider, kReqId, "../x", "plan");
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_share_path_invalid);

    //取消一个不存在的搜索：不回任何帧
    ShareControlMessage cancel;
    cancel.m_type = sf_cancel;
    cancel.m_reqId = kReqId;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(cancel, frame));
    provider.OnFrame(frame.data(), frame.size());
    CHECK(channel.Take().empty());

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SPV8 经线程池异步执行", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\plan.txt", kSmallFileBytes));

    CShareWorkerPool pool;
    REQUIRE(pool.Start(1));

    //写操作：结果与审计在工作线程上回来
    {
        LockedChannel channel;
        LockedListener listener;
        ShareProviderContext ctx = MakeContext(root, share_kind_folder, false);
        ctx.m_perms = share_perm_mkdir;
        ctx.m_pool = &pool;
        CShareProvider provider(ctx, &channel, &listener);
        Feed(provider, sf_mkdir, "docs/new", "");
        const DWORD start = ::GetTickCount();
        while (channel.Count() == 0 && ::GetTickCount() - start < kSpvAsyncWaitMs)
            ::Sleep(1);
        std::vector<ShareControlMessage> sent = channel.Take();
        REQUIRE(sent.size() == 1);
        CHECK(sent[0].m_type == sf_op_result);
        CHECK(sent[0].m_code == error_code_ok);
        CHECK(listener.m_writeReports.load() == 1);
        CHECK(::GetFileAttributesW(ShareToLongPath(root + L"\\docs\\new").c_str()) != INVALID_FILE_ATTRIBUTES);
    }

    //搜索排在队列里时会话被销毁：任务随后执行，不再发帧；通道先于任务销毁也安全
    HANDLE release = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    std::unique_ptr<IShareTask> block(new BlockTask(release));
    REQUIRE(pool.Post(block));
    std::unique_ptr<LockedChannel> channel(new LockedChannel());
    {
        ShareProviderContext ctx = MakeContext(root, share_kind_folder, false);
        ctx.m_pool = &pool;
        CShareProvider provider(ctx, channel.get(), NULL);
        FeedSearch(provider, kReqId, "", "plan");
        CHECK(provider.GetRunningSearchCount() == 1);
        CHECK(pool.GetQueuedCount() == 1);
    }
    channel.reset();
    ::SetEvent(release);
    const DWORD start = ::GetTickCount();
    while (pool.GetQueuedCount() > 0 && ::GetTickCount() - start < kSpvAsyncWaitMs)
        ::Sleep(1);
    pool.Stop();
    CHECK(pool.GetQueuedCount() == 0);
    ::CloseHandle(release);

    sharetestfs::DeleteTree(base);
}

namespace {

//SPV9：测试图片的尺寸与要的长边（像素）
const int kSpvImageW = 300;
const int kSpvImageH = 150;
const int kSpvThumbEdge = 60;

//发一个缩略图请求
void FeedThumb(CShareProvider& provider, int64_t reqId, const std::string& path, int maxEdge)
{
    ShareControlMessage req;
    req.m_type = sf_thumb;
    req.m_reqId = reqId;
    req.m_path = path;
    req.m_maxEdge = maxEdge;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(req, frame));
    provider.OnFrame(frame.data(), frame.size());
}

//用 GDI+ 做一张纯色 PNG
bool SavePng(const std::wstring& path, int w, int h)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            Gdiplus::Bitmap bmp(w, h, PixelFormat24bppRGB);
            Gdiplus::Graphics g(&bmp);
            g.Clear(Gdiplus::Color(0xFF20A040));
            return bmp.Save(path.c_str(), &codecs[i].Clsid, NULL) == Gdiplus::Ok;
        }
    }
    return false;
}

} // namespace

TEST_CASE("SPV9 缩略图经处理器生成", "[shareprovider]")
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    REQUIRE(Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok);
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\pics"));
    REQUIRE(SavePng(root + L"\\pics\\a.png", kSpvImageW, kSpvImageH));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\pics\\note.txt", kSmallFileBytes));

    FakeChannel channel;
    CShareProvider provider(MakeContext(root, share_kind_folder, false), &channel, NULL);
    FeedThumb(provider, kReqId, "pics/a.png", kSpvThumbEdge);
    std::vector<ShareControlMessage> sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_type == sf_thumb_result);
    CHECK(sent[0].m_reqId == kReqId);
    CHECK(sent[0].m_code == error_code_ok);
    CHECK(sent[0].m_width == kSpvThumbEdge);
    CHECK(sent[0].m_height == kSpvThumbEdge / 2);
    CHECK(sent[0].m_binary.size() > 0);
    CHECK(provider.GetRunningSearchCount() == 0);

    //失败：都带结果码、没有附件
    FeedThumb(provider, kReqId, "pics/note.txt", kSpvThumbEdge);
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == error_code_bad_request);
    CHECK(sent[0].m_binary.empty());
    FeedThumb(provider, kReqId, "pics", kSpvThumbEdge);
    CHECK(channel.Take()[0].m_code == error_code_share_path_not_found);
    FeedThumb(provider, kReqId, "pics/none.png", kSpvThumbEdge);
    CHECK(channel.Take()[0].m_code == error_code_share_path_not_found);
    FeedThumb(provider, kReqId, "../a.png", kSpvThumbEdge);
    CHECK(channel.Take()[0].m_code == error_code_share_path_invalid);

    //排在队列里时收到 sf_cancel：不回结果
    CShareWorkerPool pool;
    REQUIRE(pool.Start(1));
    HANDLE release = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    std::unique_ptr<IShareTask> block(new BlockTask(release));
    REQUIRE(pool.Post(block));
    LockedChannel locked;
    ShareProviderContext ctx = MakeContext(root, share_kind_folder, false);
    ctx.m_pool = &pool;
    {
        CShareProvider pooled(ctx, &locked, NULL);
        FeedThumb(pooled, kReqId, "pics/a.png", kSpvThumbEdge);
        CHECK(pooled.GetRunningSearchCount() == 1);
        ShareControlMessage cancel;
        cancel.m_type = sf_cancel;
        cancel.m_reqId = kReqId;
        std::string frame;
        REQUIRE(ShareControlFraming::BuildFrame(cancel, frame));
        pooled.OnFrame(frame.data(), frame.size());
        ::SetEvent(release);
        const DWORD start = ::GetTickCount();
        while (pooled.GetRunningSearchCount() > 0 && ::GetTickCount() - start < kSpvAsyncWaitMs)
            ::Sleep(1);
        CHECK(pooled.GetRunningSearchCount() == 0);
        CHECK(locked.Count() == 0);
    }
    pool.Stop();
    ::CloseHandle(release);
    sharetestfs::DeleteTree(base);
    Gdiplus::GdiplusShutdown(token);
}

TEST_CASE("SPV10 列整棵目录树", "[shareprovider]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs\\sub"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\a.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\sub\\b.txt", kSmallFileBytes));

    FakeChannel channel;
    CShareProvider provider(MakeContext(root, share_kind_folder, false), &channel, NULL);
    ShareControlMessage req;
    req.m_type = sf_search;
    req.m_reqId = kReqId;
    req.m_path = "docs";
    req.m_matchAll = true;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(req, frame));
    provider.OnFrame(frame.data(), frame.size());
    std::vector<ShareControlMessage> sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_code == 0);
    CHECK(sent[0].m_done);
    CHECK_FALSE(sent[0].m_truncated);
    CHECK(sent[0].m_entries.size() == 3);

    //访问者要的上限：2 条，截断
    req.m_maxItems = 2;
    REQUIRE(ShareControlFraming::BuildFrame(req, frame));
    provider.OnFrame(frame.data(), frame.size());
    sent = channel.Take();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].m_entries.size() == 2);
    CHECK(sent[0].m_truncated);
    sharetestfs::DeleteTree(base);
}
