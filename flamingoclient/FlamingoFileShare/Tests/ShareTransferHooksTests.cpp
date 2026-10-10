/**
 *  传输子会话扩展点单元测试（Suite SXH，标签 [sharehooks]），ShareTransferHooksTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 14 步的 14c；假的 lane 0 发送器记录发出的帧，落地相关的用例在测试程序目录下
 *  建真实的文件）：
 *    - SXH1 访问者下载的前置交换：连好后发 st_job，收到应答之前每 kShareJobResendMs 重发；应答 0 继续，非 0 以它失败；
 *    - SXH2 访问者下载的落点：顶层条目与目标目录里已有的同名时改名（目录「名字(2)」、文件「名字(2).扩展名」），
 *           只改本端的相对路径；续传记录的键按改名之前的共享内路径；
 *    - SXH3 访问者下载的续传：记录与源文件一致、.part 足够长时写入进度与前缀 MD5；已下完的文件标记完成；
 *           源文件变了的记录作废；有续传记录的顶层条目沿用原名不改名；
 *    - SXH4 访问者上传的前置交换：收到应答 0 返回 0、非 0 返回该码，被停止时返回 818，等不到应答时返回 816；
 *    - SXH5 分享者下载的前置交换：收到 st_job 后生成清单并应答 0；条目非法时应答 808 并失败；方向不对为 3；
 *           等不到 st_job 为 816；重复的 st_job 按原结果再应答；
 *    - SXH6 分享者上传：没有上传权限 806、单文件共享 820、目标目录越界 808、不存在 809；通过时应答 0；顶层同名 807；
 *           半成品设隐藏并登记，列目录时不出现；完成改名不覆盖；改成正式名之后去掉隐藏属性、注销登记，列目录时出现
 *           （第 2 期第 5 步联调发现：此前上传完的文件一直带着隐藏属性，浏览窗口里看不到）；
 *    - SXH7 传输会话结束后先停状态机、再销毁扩展点（第 14 步联调时分享者客户端在这里崩溃过）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../Engine/ShareControlFraming.h"
#include "../Engine/ShareFileOps.h"
#include "../Engine/ShareProtocolCodec.h"
#include "../Engine/ShareTransferHooks.h"
#include "../Engine/ShareTransferSession.h"
#include "../../Source/Common/Util/MD5Core.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"

using namespace fileshare;
using foldertransfer::FolderManifest;
using foldertransfer::FolderReceiveState;
using foldertransfer::FolderRecvFileState;
using foldertransfer::FolderRelayFileEntry;
using foldertransfer::IFolderControlSender;

namespace {

//本套用例在测试程序目录下建的临时目录名
const wchar_t* const kSxhTestDir = L"sxh_test";

//几个共享 id、文件大小与修改时间
const int64_t kShareId = 7;
const int64_t kBigSize = 1000;
const int64_t kResumeOffset = 400;
const int64_t kPartBytes = 500;
const int64_t kSrcMtime = 1759370000000LL;
const size_t kSmallFile = 12;

//等待片长：比重发间隔多一点，单位毫秒
const DWORD kAfterResendMs = kShareJobResendMs + 100;

//SXH4/SXH5 的短等待（毫秒）与另一条线程送来应答前的延时（毫秒）
const int kShortWaitMs = 600;
const DWORD kAckDelayMs = 100;

//假的 lane 0 发送器：解码并记录发出的帧（线程安全）
class FakeSender : public IFolderControlSender
{
public:
    virtual bool SendControl(const std::string& frame) override
    {
        ShareControlMessage msg;
        const bool ok = ShareControlFraming::DecodeFrame(frame.data(), frame.size(), msg);
        std::lock_guard<std::mutex> guard(m_mutex);
        if (ok)
            m_frames.push_back(msg);
        return true;
    }

    std::vector<ShareControlMessage> Frames()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_frames;
    }

private:
    std::mutex                       m_mutex;    //保护 m_frames
    std::vector<ShareControlMessage> m_frames;   //发出的帧
};

//本套用例的临时目录（每次调用都先清空再建）
std::wstring FreshBase()
{
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSxhTestDir;
    sharetestfs::DeleteTree(base);
    sharetestfs::MakeDirs(base);
    return base;
}

//编码一个 st_job_ack 帧
std::string AckFrame(int code)
{
    std::string frame;
    REQUIRE(BuildJobAckFrame(code, frame));
    return frame;
}

//编码一个 st_job 帧
std::string JobFrame(int direction, const std::vector<std::string>& items, const std::string& toDir)
{
    std::string frame;
    REQUIRE(BuildJobFrame(direction, items, toDir, frame));
    return frame;
}

FolderRelayFileEntry File(const char* rel, int64_t size, int64_t mtime)
{
    FolderRelayFileEntry e;
    e.m_relpath = rel;
    e.m_size = size;
    e.m_mtime = mtime;
    return e;
}

ShareLocalRecord FolderShare(const std::wstring& root)
{
    ShareLocalRecord rec;
    rec.m_shareId = kShareId;
    rec.m_rootPath = root;
    rec.m_kind = share_kind_folder;
    return rec;
}

//MD5 摘要的字节数
const int kDigestBytes = 16;

//两个 MD5 上下文按当前内容求出的摘要是否相同（各取一个副本求，不改原上下文）
bool SameDigest(const MD5_CTX& a, const MD5_CTX& b)
{
    MD5_CTX ca = a;
    MD5_CTX cb = b;
    unsigned char da[kDigestBytes] = { 0 };
    unsigned char db[kDigestBytes] = { 0 };
    MD5Final(da, &ca);
    MD5Final(db, &cb);
    return memcmp(da, db, sizeof(da)) == 0;
}

//在另一条线程上延时送来一个应答
struct DelayedAck
{
    foldertransfer::IFolderSendHooks* m_hooks;   //要送达的扩展点
    std::string                       m_frame;   //应答帧

    void operator()() const
    {
        ::Sleep(kAckDelayMs);
        m_hooks->OnControlFrame(reinterpret_cast<const unsigned char*>(m_frame.data()), m_frame.size());
    }
};

} // namespace

TEST_CASE("SXH1 访问者下载的前置交换与重发", "[sharehooks]")
{
    std::vector<std::string> items;
    items.push_back("docs");
    std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
    CVisitorDownloadHooks hooks(kShareId, items, L"C:\\nowhere", NULL, state);
    FakeSender sender;

    CHECK(hooks.OnLanesReady(sender) == 0);
    std::vector<ShareControlMessage> frames = sender.Frames();
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].m_type == st_job);
    CHECK(frames[0].m_direction == share_transfer_download);
    REQUIRE(frames[0].m_paths.size() == 1);
    CHECK(frames[0].m_paths[0] == "docs");

    //间隔未到不重发；到了重发
    CHECK(hooks.OnControlIdle(sender) == 0);
    CHECK(sender.Frames().size() == 1);
    ::Sleep(kAfterResendMs);
    CHECK(hooks.OnControlIdle(sender) == 0);
    CHECK(sender.Frames().size() == 2);

    //不是 st_job_ack 的帧忽略；应答 0 之后不再重发
    ShareControlMessage other;
    other.m_type = sf_bye;
    std::string otherFrame;
    REQUIRE(ShareControlFraming::BuildFrame(other, otherFrame));
    CHECK(hooks.OnControlFrame(sender, otherFrame) == 0);
    CHECK(hooks.OnControlFrame(sender, AckFrame(0)) == 0);
    ::Sleep(kAfterResendMs);
    CHECK(hooks.OnControlIdle(sender) == 0);
    CHECK(sender.Frames().size() == 2);

    //应答非 0：以它失败
    CVisitorDownloadHooks refused(kShareId, items, L"C:\\nowhere", NULL, state);
    FakeSender sender2;
    refused.OnLanesReady(sender2);
    CHECK(refused.OnControlFrame(sender2, AckFrame(error_code_share_path_not_found)) == error_code_share_path_not_found);
}

TEST_CASE("SXH2 访问者下载的落点与顶层改名", "[sharehooks]")
{
    const std::wstring base = FreshBase();
    const std::wstring target = base + L"\\dl";
    REQUIRE(sharetestfs::MakeDirs(target + L"\\docs"));
    REQUIRE(sharetestfs::WriteFileBytes(target + L"\\a.txt", kSmallFile));

    std::vector<std::string> items;
    items.push_back("in/docs");
    items.push_back("in/a.txt");
    items.push_back("in/new.txt");
    std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
    CVisitorDownloadHooks hooks(kShareId, items, target, NULL, state);

    std::vector<std::string> dirs;
    dirs.push_back("docs");
    dirs.push_back("docs/sub");
    std::vector<FolderRelayFileEntry> files;
    files.push_back(File("docs/sub/x.txt", 1, kSrcMtime));
    files.push_back(File("a.txt", 1, kSrcMtime));
    files.push_back(File("new.txt", 1, kSrcMtime));
    state->SyncWithManifest(files);

    std::wstring root;
    REQUIRE(hooks.PrepareRoot("", dirs, files, root) == 0);
    CHECK(root == target);
    CHECK(dirs[0] == "docs(2)");
    CHECK(dirs[1] == "docs(2)/sub");
    CHECK(files[0].m_relpath == "docs(2)/sub/x.txt");
    CHECK(files[1].m_relpath == "a(2).txt");
    CHECK(files[2].m_relpath == "new.txt");

    REQUIRE(hooks.GetFileCount() == 3);
    CHECK(hooks.GetShareRelPath(0) == "in/docs/sub/x.txt");
    CHECK(hooks.GetShareRelPath(1) == "in/a.txt");
    CHECK(hooks.GetLocalPath(1) == target + L"\\a(2).txt");
    CHECK(hooks.GetLocalPath(0) == target + L"\\docs(2)\\sub\\x.txt");
    REQUIRE(hooks.GetLocalTopNames().size() == 3);
    CHECK(hooks.GetLocalTopNames()[0] == L"docs(2)");

    //目标目录不存在：失败
    CVisitorDownloadHooks bad(kShareId, items, base + L"\\nosuch", NULL, state);
    CHECK(bad.PrepareRoot("", dirs, files, root) == error_code_share_io_fail);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SXH3 访问者下载的续传记录", "[sharehooks]")
{
    const std::wstring base = FreshBase();
    const std::wstring target = base + L"\\dl";
    REQUIRE(sharetestfs::MakeDirs(target));
    //上次没下完：big.bin.part 有 500 字节，记录写到 400；done.bin 已下完
    REQUIRE(sharetestfs::WriteFileBytes(target + L"\\big.bin.part", static_cast<size_t>(kPartBytes)));
    REQUIRE(sharetestfs::WriteFileBytes(target + L"\\done.bin", kSmallFile));
    REQUIRE(sharetestfs::WriteFileBytes(target + L"\\stale.bin.part", static_cast<size_t>(kPartBytes)));

    CShareLocalStore store;
    REQUIRE(store.Open(L":memory:"));
    ShareDownloadRecord rec;
    rec.m_shareId = kShareId;
    rec.m_relPath = "big.bin";
    rec.m_srcSize = kBigSize;
    rec.m_srcMtime = kSrcMtime;
    rec.m_localPath = target + L"\\big.bin";
    rec.m_offset = kResumeOffset;
    REQUIRE(store.SaveDownloadProgress(rec));
    rec.m_relPath = "done.bin";
    rec.m_srcSize = static_cast<int64_t>(kSmallFile);
    rec.m_localPath = target + L"\\done.bin";
    rec.m_offset = static_cast<int64_t>(kSmallFile);
    REQUIRE(store.SaveDownloadProgress(rec));
    //源文件后来改过（修改时间不同）的记录
    rec.m_relPath = "stale.bin";
    rec.m_srcSize = kBigSize;
    rec.m_srcMtime = kSrcMtime - 1;
    rec.m_localPath = target + L"\\stale.bin";
    rec.m_offset = kResumeOffset;
    REQUIRE(store.SaveDownloadProgress(rec));

    std::vector<std::string> items;
    items.push_back("big.bin");
    items.push_back("done.bin");
    items.push_back("stale.bin");
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    files.push_back(File("big.bin", kBigSize, kSrcMtime));
    files.push_back(File("done.bin", static_cast<int64_t>(kSmallFile), kSrcMtime));
    files.push_back(File("stale.bin", kBigSize, kSrcMtime));
    std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
    state->SyncWithManifest(files);

    CVisitorDownloadHooks hooks(kShareId, items, target, &store, state);
    std::wstring root;
    REQUIRE(hooks.PrepareRoot("", dirs, files, root) == 0);

    //有续传记录的 done.bin 沿用原名，不因已存在而改名
    CHECK(files[1].m_relpath == "done.bin");

    FolderRecvFileState big;
    REQUIRE(state->GetFileRecord(0, big));
    CHECK(big.m_bytesWritten == kResumeOffset);
    CHECK(big.m_hashing);
    MD5_CTX expected;
    MD5Init(&expected);
    std::vector<unsigned char> xs(static_cast<size_t>(kResumeOffset), 'x');
    MD5Update(&expected, &xs[0], static_cast<unsigned int>(xs.size()));
    CHECK(SameDigest(big.m_md5ctx, expected));

    FolderRecvFileState done;
    REQUIRE(state->GetFileRecord(1, done));
    CHECK(done.m_completed);

    FolderRecvFileState stale;
    REQUIRE(state->GetFileRecord(2, stale));
    CHECK(stale.m_bytesWritten == 0);
    std::wstring path;
    int64_t offset = 0;
    CHECK_FALSE(store.GetResumePoint(kShareId, "stale.bin", kBigSize, kSrcMtime - 1, path, offset));

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SXH4 访问者上传的前置交换", "[sharehooks]")
{
    std::vector<std::string> names;
    names.push_back("photos");
    FolderManifest manifest;
    std::wstring root = L"C:\\src";

    //另一条线程送来应答 0
    {
        CVisitorUploadHooks hooks(names, "in", kShortWaitMs * 4);
        FakeSender sender;
        DelayedAck delayed;
        delayed.m_hooks = &hooks;
        delayed.m_frame = AckFrame(0);
        std::thread t(delayed);
        CHECK(hooks.RunPreamble(sender, manifest, root) == 0);
        t.join();
        std::vector<ShareControlMessage> frames = sender.Frames();
        REQUIRE(!frames.empty());
        CHECK(frames[0].m_type == st_job);
        CHECK(frames[0].m_direction == share_transfer_upload);
        CHECK(frames[0].m_toDir == "in");
    }
    //应答非 0
    {
        CVisitorUploadHooks hooks(names, "in", kShortWaitMs * 4);
        FakeSender sender;
        const std::string ack = AckFrame(error_code_share_permission_denied);
        hooks.OnControlFrame(reinterpret_cast<const unsigned char*>(ack.data()), ack.size());
        CHECK(hooks.RunPreamble(sender, manifest, root) == error_code_share_permission_denied);
    }
    //被停止
    {
        CVisitorUploadHooks hooks(names, "in", kShortWaitMs * 4);
        FakeSender sender;
        hooks.OnStopping();
        CHECK(hooks.RunPreamble(sender, manifest, root) == error_code_share_transfer_canceled);
    }
    //等不到应答：期间按间隔重发
    {
        CVisitorUploadHooks hooks(names, "in", kShortWaitMs);
        FakeSender sender;
        CHECK(hooks.RunPreamble(sender, manifest, root) == error_code_share_owner_timeout);
        CHECK(sender.Frames().size() >= 2);
    }
}

TEST_CASE("SXH5 分享者下载的前置交换与清单", "[sharehooks]")
{
    const std::wstring base = FreshBase();
    const std::wstring shareRoot = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(shareRoot + L"\\docs"));
    REQUIRE(sharetestfs::WriteFileBytes(shareRoot + L"\\docs\\a.txt", kSmallFile));

    std::vector<std::string> items;
    items.push_back("docs");
    //正常：先收到 st_job，前置交换生成清单并应答 0
    {
        COwnerDownloadHooks hooks(FolderShare(shareRoot), kShortWaitMs);
        FakeSender sender;
        const std::string job = JobFrame(share_transfer_download, items, "");
        hooks.OnControlFrame(reinterpret_cast<const unsigned char*>(job.data()), job.size());
        FolderManifest manifest;
        std::wstring root;
        REQUIRE(hooks.RunPreamble(sender, manifest, root) == 0);
        CHECK(root == shareRoot);
        REQUIRE(manifest.m_files.size() == 1);
        CHECK(manifest.m_files[0].m_relativePath == "docs/a.txt");
        std::vector<ShareControlMessage> frames = sender.Frames();
        REQUIRE(frames.size() == 1);
        CHECK(frames[0].m_type == st_job_ack);
        CHECK(frames[0].m_code == 0);
        //重复的 st_job：按原结果再应答
        hooks.OnControlFrame(reinterpret_cast<const unsigned char*>(job.data()), job.size());
        CHECK(sender.Frames().size() == 2);
        CHECK(hooks.GetItems() == items);
    }
    //条目越界：应答 808 并失败
    {
        COwnerDownloadHooks hooks(FolderShare(shareRoot), kShortWaitMs);
        FakeSender sender;
        std::vector<std::string> bad;
        bad.push_back("../x");
        const std::string job = JobFrame(share_transfer_download, bad, "");
        hooks.OnControlFrame(reinterpret_cast<const unsigned char*>(job.data()), job.size());
        FolderManifest manifest;
        std::wstring root;
        CHECK(hooks.RunPreamble(sender, manifest, root) == error_code_share_path_invalid);
        REQUIRE(sender.Frames().size() == 1);
        CHECK(sender.Frames()[0].m_code == error_code_share_path_invalid);
    }
    //方向不对
    {
        COwnerDownloadHooks hooks(FolderShare(shareRoot), kShortWaitMs);
        FakeSender sender;
        const std::string job = JobFrame(share_transfer_upload, items, "");
        hooks.OnControlFrame(reinterpret_cast<const unsigned char*>(job.data()), job.size());
        FolderManifest manifest;
        std::wstring root;
        CHECK(hooks.RunPreamble(sender, manifest, root) == error_code_bad_request);
    }
    //等不到 st_job
    {
        COwnerDownloadHooks hooks(FolderShare(shareRoot), kShortWaitMs);
        FakeSender sender;
        FolderManifest manifest;
        std::wstring root;
        CHECK(hooks.RunPreamble(sender, manifest, root) == error_code_share_owner_timeout);
        CHECK(sender.Frames().empty());
    }

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SXH6 分享者上传的校验与落地规则", "[sharehooks]")
{
    const std::wstring base = FreshBase();
    const std::wstring shareRoot = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(shareRoot + L"\\in\\photos"));
    REQUIRE(sharetestfs::WriteFileBytes(shareRoot + L"\\readme.txt", kSmallFile));
    std::vector<std::string> names;
    names.push_back("photos");

    //没有上传权限
    {
        COwnerUploadHooks hooks(FolderShare(shareRoot), 0);
        FakeSender sender;
        CHECK(hooks.OnControlFrame(sender, JobFrame(share_transfer_upload, names, "in")) == error_code_share_permission_denied);
        REQUIRE(sender.Frames().size() == 1);
        CHECK(sender.Frames()[0].m_code == error_code_share_permission_denied);
    }
    //单文件共享
    {
        ShareLocalRecord file = FolderShare(shareRoot + L"\\readme.txt");
        file.m_kind = share_kind_file;
        COwnerUploadHooks hooks(file, share_perm_upload);
        FakeSender sender;
        CHECK(hooks.OnControlFrame(sender, JobFrame(share_transfer_upload, names, "")) == error_code_share_kind_mismatch);
    }
    //目标目录越界、不存在、是文件
    {
        COwnerUploadHooks hooks(FolderShare(shareRoot), share_perm_upload);
        FakeSender sender;
        CHECK(hooks.OnControlFrame(sender, JobFrame(share_transfer_upload, names, "../x")) == error_code_share_path_invalid);
    }
    {
        COwnerUploadHooks hooks(FolderShare(shareRoot), share_perm_upload);
        FakeSender sender;
        CHECK(hooks.OnControlFrame(sender, JobFrame(share_transfer_upload, names, "nosuch")) == error_code_share_path_not_found);
    }
    {
        COwnerUploadHooks hooks(FolderShare(shareRoot), share_perm_upload);
        FakeSender sender;
        CHECK(hooks.OnControlFrame(sender, JobFrame(share_transfer_upload, names, "readme.txt")) == error_code_share_path_not_found);
    }
    //通过：应答 0；顶层同名 807；不同名时落地根为目标目录
    {
        COwnerUploadHooks hooks(FolderShare(shareRoot), share_perm_upload);
        FakeSender sender;
        CHECK(hooks.OnControlFrame(sender, JobFrame(share_transfer_upload, names, "in")) == 0);
        CHECK(sender.Frames()[0].m_code == 0);
        CHECK(hooks.GetTargetDir() == shareRoot + L"\\in");

        std::vector<std::string> dirs;
        dirs.push_back("photos");
        std::vector<FolderRelayFileEntry> files;
        files.push_back(File("photos/p.jpg", 1, kSrcMtime));
        std::wstring root;
        CHECK(hooks.PrepareRoot("", dirs, files, root) == error_code_share_name_conflict);

        dirs[0] = "albums";
        files[0].m_relpath = "albums/p.jpg";
        REQUIRE(hooks.PrepareRoot("", dirs, files, root) == 0);
        CHECK(root == shareRoot + L"\\in");
        CHECK_FALSE(hooks.ReplaceExistingOnFinish());

        //半成品：设隐藏并登记，列目录时不出现
        const std::wstring part = shareRoot + L"\\in\\upload.bin.part";
        REQUIRE(sharetestfs::WriteFileBytes(part, kSmallFile));
        hooks.OnPartFileOpened(part);
        CHECK((::GetFileAttributesW(part.c_str()) & FILE_ATTRIBUTE_HIDDEN) != 0);
        CHECK(CShareUploadRegistry::Instance().Contains(part));
        std::vector<ShareEntry> entries;
        REQUIRE(ShareFileOps::ListDirectory(shareRoot + L"\\in", true, entries) == error_code_ok);
        for (size_t i = 0; i < entries.size(); ++i)
            CHECK(entries[i].m_name != "upload.bin.part");
        REQUIRE(hooks.GetPartFiles().size() == 1);

        //改成正式名之后（状态机用 MoveFileEx，隐藏属性随之保留）：去掉隐藏属性、注销登记，不含隐藏条目的列目录里出现
        const std::wstring finalPath = shareRoot + L"\\in\\upload.bin";
        REQUIRE(::MoveFileW(part.c_str(), finalPath.c_str()));
        CHECK((::GetFileAttributesW(finalPath.c_str()) & FILE_ATTRIBUTE_HIDDEN) != 0);
        hooks.OnFileFinalized(part, finalPath);
        CHECK((::GetFileAttributesW(finalPath.c_str()) & FILE_ATTRIBUTE_HIDDEN) == 0);
        CHECK_FALSE(CShareUploadRegistry::Instance().Contains(part));
        entries.clear();
        REQUIRE(ShareFileOps::ListDirectory(shareRoot + L"\\in", false, entries) == error_code_ok);
        bool listed = false;
        for (size_t i = 0; i < entries.size(); ++i)
            listed = listed || entries[i].m_name == "upload.bin";
        CHECK(listed);
    }

    sharetestfs::DeleteTree(base);
}

namespace {

//SXH7：连接被拒用的中转地址（本机 1 号端口没有程序监听）与等待失败事件的最长时间（毫秒）
const char* const kClosedIp = "127.0.0.1";
const int kClosedPort = 1;
const DWORD kFailWaitMs = 15000;
const DWORD kPollMs = 50;

//假的事件接收方：记下收到的事件类型
class FakeSink : public IShareSessionEventSink
{
public:
    virtual void PostSessionEvent(const ShareSessionEvent& ev) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_types.push_back(ev.m_type);
    }

    bool Has(int type)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_types.size(); ++i)
        {
            if (m_types[i] == type)
                return true;
        }
        return false;
    }

private:
    std::mutex       m_mutex;   //保护 m_types
    std::vector<int> m_types;   //收到的事件类型
};

} // namespace

TEST_CASE("SXH7 传输会话结束后销毁时先停状态机再销毁扩展点", "[sharehooks]")
{
    //连接本机未监听的端口：状态机很快以失败收场；随后 Stop 并销毁会话。此前扩展点先于状态机析构，
    //状态机的析构函数再回调扩展点就会访问已释放的内存（第 14 步联调时分享者客户端因此崩溃）
    ShareRelayParams relay;
    relay.m_accessId = "share-1-1";
    relay.m_transferId = "share-1-1-t1";
    relay.m_relayIp = kClosedIp;
    relay.m_relayPort = kClosedPort;
    relay.m_token = "tok";
    relay.m_lanes = 1;
    FakeSink sink;
    std::unique_ptr<CShareTransferSession> session(new CShareTransferSession(relay, NULL, &sink));
    ShareLocalRecord share;
    share.m_shareId = kShareId;
    share.m_rootPath = sharetestfs::ExeDir();
    share.m_kind = share_kind_folder;
    REQUIRE(session->StartOwnerDownload(share));

    const DWORD start = ::GetTickCount();
    while (!sink.Has(share_transfer_event_failed) && ::GetTickCount() - start < kFailWaitMs)
        ::Sleep(kPollMs);
    CHECK(sink.Has(share_transfer_event_failed));
    session->Stop();
    session.reset();
    SUCCEED("session destroyed after hooks were still alive");
}
