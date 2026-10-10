/**
 *  DirectFileTransferTests.cpp
 *
 *  单聊单文件 P2P 直传的纯逻辑（Feature/FolderTransfer/DirectFileTransfer）测试，标签 [directfile]，
 *  方案见 docs/单聊文件P2P直传与P2P徽章方案.md 第 3 节：
 *    - DFT1：发送端收到 1810 应答——成功继续等；文件名含敏感词不改走离线；其余失败一律改为离线文件；
 *    - DFT2：发送端收到 1815——拒绝、撤回、完成各自对应；对方离线、超时、已改为离线、建不起 P2P（622）改为离线；
 *            其余失败给「重试」；
 *    - DFT3：接收端收到 1815——完成、失败、已拒绝、对方撤回各自对应；对方离线、超时、已改为离线、622 改为离线文件；
 *    - DFT4：用一个文件构造清单：根目录是文件所在目录、清单里只有这个文件；目录、不存在的路径、空路径都失败；
 *    - DFT5：落地目录是下载缓存目录下的 direct-<会话号>\，带不带末尾反斜杠都一样；参数为空返回空串；
 *    - DFT6：接收端扩展点：建好落地目录、文件直接落在它下面、不改写清单、覆盖同名文件，其余回调什么也不做。
 *    - DFT7：两端扩展点记下 MD5：发送端不做前置交换、不改清单，记下算出的 MD5；接收端记下落地路径与校验通过的 MD5；
 *            MD5 统一转成小写。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Feature/FolderTransfer/DirectFileTransfer.h"
#include "../net/Protocol.h"

using namespace foldertransfer;

namespace {

//在系统临时目录下建一个本组用例专用的空目录，返回带末尾反斜杠的完整路径
std::wstring MakeScratchDir(const wchar_t* name)
{
    wchar_t tmp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"flamingo_dft_" + name + L"_" + std::to_wstring(::GetCurrentProcessId()) + L"\\";
    ::CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

//写一个指定字节数的文件
void WriteBytes(const std::wstring& path, size_t bytes)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    std::string data(bytes, 'x');
    DWORD written = 0;
    if (!data.empty())
        ::WriteFile(h, data.data(), (DWORD)data.size(), &written, NULL);
    ::CloseHandle(h);
}

//测试文件的字节数
const size_t kSampleBytes = 1234;

//不用的控制帧发送器：直传的扩展点不该调用它
class NullControlSender : public IFolderControlSender
{
public:
    NullControlSender()
        : m_calls(0)
    {
    }

    virtual bool SendControl(const std::string& frame) override
    {
        (void)frame;
        ++m_calls;
        return true;
    }

    int m_calls;   //被调用的次数
};

} // namespace

TEST_CASE("DFT1 发送端收到邀请应答：成功继续等，敏感词不改走离线，其余失败改为离线文件", "[directfile]")
{
    CHECK(DecideDirectSendOnInviteResponse(error_code_ok) == kDirectSendWait);
    CHECK(DecideDirectSendOnInviteResponse(error_code_folder_direct_unavailable) == kDirectSendToOffline);
    CHECK(DecideDirectSendOnInviteResponse(error_code_folder_peer_offline) == kDirectSendToOffline);
    CHECK(DecideDirectSendOnInviteResponse(error_code_folder_busy) == kDirectSendToOffline);
    CHECK(DecideDirectSendOnInviteResponse(error_code_folder_server_busy) == kDirectSendToOffline);
    CHECK(DecideDirectSendOnInviteResponse(error_code_bad_request) == kDirectSendToOffline);
    //服务器内部错误等其它失败：一律改走离线
    CHECK(DecideDirectSendOnInviteResponse(error_code_server_internal) == kDirectSendToOffline);
    //文件名含敏感词：离线发送同样会被拒，不改走离线
    CHECK(DecideDirectSendOnInviteResponse(error_code_file_sensitive_word) == kDirectSendBlocked);
}

TEST_CASE("DFT2 发送端收到状态推送：各状态的处置", "[directfile]")
{
    CHECK(DecideDirectSendOnState(folder_state_ringing, 0) == kDirectSendWait);
    CHECK(DecideDirectSendOnState(folder_state_transferring, 0) == kDirectSendWait);
    CHECK(DecideDirectSendOnState(folder_state_rejected, 0) == kDirectSendRejected);
    CHECK(DecideDirectSendOnState(folder_state_canceled, 0) == kDirectSendCanceled);
    CHECK(DecideDirectSendOnState(folder_state_peer_offline, 0) == kDirectSendToOffline);
    CHECK(DecideDirectSendOnState(folder_state_timeout, 0) == kDirectSendToOffline);
    CHECK(DecideDirectSendOnState(folder_state_to_offline, 0) == kDirectSendToOffline);
    CHECK(DecideDirectSendOnState(folder_state_completed, 0) == kDirectSendCompleted);
    //同意之后建不起 P2P 链路：改为离线
    CHECK(DecideDirectSendOnState(folder_state_failed, error_code_folder_direct_unavailable) == kDirectSendToOffline);
    //传输中途的其它失败：给「重试」
    CHECK(DecideDirectSendOnState(folder_state_failed, error_code_folder_relay_broken) == kDirectSendFailed);
    CHECK(DecideDirectSendOnState(folder_state_failed, error_code_folder_peer_offline) == kDirectSendFailed);
    //未知状态：不做处置
    CHECK(DecideDirectSendOnState(0, 0) == kDirectSendWait);
    CHECK(DecideDirectSendOnState(100, 0) == kDirectSendWait);
}

TEST_CASE("DFT3 接收端收到状态推送：各状态的处置", "[directfile]")
{
    CHECK(DecideDirectRecvOnState(folder_state_ringing, 0) == kDirectRecvWait);
    CHECK(DecideDirectRecvOnState(folder_state_transferring, 0) == kDirectRecvWait);
    CHECK(DecideDirectRecvOnState(folder_state_completed, 0) == kDirectRecvCompleted);
    CHECK(DecideDirectRecvOnState(folder_state_rejected, 0) == kDirectRecvRejected);
    CHECK(DecideDirectRecvOnState(folder_state_canceled, 0) == kDirectRecvSenderCanceled);
    CHECK(DecideDirectRecvOnState(folder_state_peer_offline, 0) == kDirectRecvToOffline);
    CHECK(DecideDirectRecvOnState(folder_state_timeout, 0) == kDirectRecvToOffline);
    CHECK(DecideDirectRecvOnState(folder_state_to_offline, 0) == kDirectRecvToOffline);
    CHECK(DecideDirectRecvOnState(folder_state_failed, error_code_folder_direct_unavailable) == kDirectRecvToOffline);
    CHECK(DecideDirectRecvOnState(folder_state_failed, error_code_folder_relay_broken) == kDirectRecvFailed);
    CHECK(DecideDirectRecvOnState(0, 0) == kDirectRecvWait);
}

TEST_CASE("DFT4 用一个文件构造清单：根目录是所在目录、只有这一个文件；目录与不存在的路径失败", "[directfile]")
{
    const std::wstring dir = MakeScratchDir(L"manifest");
    const std::wstring file = dir + L"\u62a5\u4ef7\u5355.xlsx";   //i18n-ignore: 测试用的中文文件名
    WriteBytes(file, kSampleBytes);

    std::wstring root;
    FolderManifest manifest;
    REQUIRE(BuildSingleFileManifest(file, root, manifest));
    CHECK(root + L"\\" == dir);
    CHECK(manifest.m_folderName == u8"\u62a5\u4ef7\u5355.xlsx");   //i18n-ignore: 测试用的中文文件名
    CHECK(manifest.m_dirs.empty());
    CHECK(manifest.m_skipped.empty());
    REQUIRE(manifest.m_files.size() == 1);
    CHECK(manifest.m_files[0].m_relativePath == u8"\u62a5\u4ef7\u5355.xlsx");   //i18n-ignore: 测试用的中文文件名
    CHECK(manifest.m_files[0].m_sizeBytes == (int64_t)kSampleBytes);

    //0 字节文件合法
    const std::wstring empty = dir + L"empty.txt";
    WriteBytes(empty, 0);
    FolderManifest m2;
    REQUIRE(BuildSingleFileManifest(empty, root, m2));
    CHECK(m2.m_files[0].m_sizeBytes == 0);

    FolderManifest bad;
    CHECK_FALSE(BuildSingleFileManifest(dir, root, bad));
    CHECK_FALSE(BuildSingleFileManifest(dir + L"missing.bin", root, bad));
    CHECK_FALSE(BuildSingleFileManifest(L"", root, bad));

    ::DeleteFileW(file.c_str());
    ::DeleteFileW(empty.c_str());
    ::RemoveDirectoryW(dir.c_str());
}

TEST_CASE("DFT5 落地目录：下载缓存目录下的 direct-<会话号>", "[directfile]")
{
    CHECK(DirectFileSaveDir(L"D:\\data\\Users\\MY0127\\ChatFile\\", "5127-5308-1-1") ==
          L"D:\\data\\Users\\MY0127\\ChatFile\\direct-5127-5308-1-1\\");
    CHECK(DirectFileSaveDir(L"D:\\data\\Users\\MY0127\\ChatFile", "5127-5308-1-1") ==
          L"D:\\data\\Users\\MY0127\\ChatFile\\direct-5127-5308-1-1\\");
    CHECK(DirectFileSaveDir(L"", "5127-5308-1-1").empty());
    CHECK(DirectFileSaveDir(L"D:\\x\\", "").empty());
}

TEST_CASE("DFT6 接收端扩展点：建好落地目录、文件直接落在它下面、覆盖同名文件、其余回调不做事", "[directfile]")
{
    const std::wstring base = MakeScratchDir(L"hooks");
    const std::wstring saveDir = base + L"direct-1-2-3-4\\";
    DirectFileRecvHooks hooks(saveDir);
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files(1);
    files[0].m_relpath = "a.txt";
    files[0].m_size = 10;
    std::wstring root;
    REQUIRE(hooks.PrepareRoot("a.txt", dirs, files, root) == 0);
    CHECK(root + L"\\" == saveDir);
    CHECK((::GetFileAttributesW(saveDir.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0);
    CHECK(files[0].m_relpath == "a.txt");
    CHECK(dirs.empty());
    CHECK(hooks.ReplaceExistingOnFinish());

    NullControlSender sender;
    CHECK(hooks.OnLanesReady(sender) == 0);
    CHECK(hooks.OnControlIdle(sender) == 0);
    CHECK(hooks.OnControlFrame(sender, "x") == 0);
    CHECK(sender.m_calls == 0);

    ::RemoveDirectoryW(saveDir.c_str());
    ::RemoveDirectoryW(base.c_str());
}

TEST_CASE("DFT7 两端扩展点记下 MD5：发送端不做前置交换、不改清单；接收端记下落地路径；MD5 统一转成小写", "[directfile]")
{
    DirectFileSendHooks sendHooks;
    FolderManifest manifest;
    manifest.m_folderName = "a.txt";
    std::wstring root = L"D:\\x";
    NullControlSender sender;
    CHECK(sendHooks.RunPreamble(sender, manifest, root) == 0);
    CHECK(manifest.m_folderName == "a.txt");
    CHECK(root == L"D:\\x");
    CHECK(sender.m_calls == 0);
    //还没算出来：空
    CHECK(sendHooks.Md5().empty());
    sendHooks.OnFileMd5Computed(0, "0123456789ABCDEF0123456789ABCDEF");
    CHECK(sendHooks.Md5() == "0123456789abcdef0123456789abcdef");
    //自定义帧（首字节取文件共享自定义帧的起始值 0x20，不属于 ft_*）与停止：什么也不做
    const unsigned char frame[1] = { 0x20 };
    sendHooks.OnControlFrame(frame, sizeof(frame));
    sendHooks.OnStopping();
    CHECK(sendHooks.Md5() == "0123456789abcdef0123456789abcdef");

    DirectFileRecvHooks recvHooks(L"D:\\y\\direct-1\\");
    CHECK(recvHooks.Md5().empty());
    CHECK(recvHooks.FinalPath().empty());
    recvHooks.OnFileVerified(L"D:\\y\\direct-1\\a.txt", "FEDCBA9876543210FEDCBA9876543210");
    CHECK(recvHooks.Md5() == "fedcba9876543210fedcba9876543210");
    CHECK(recvHooks.FinalPath() == L"D:\\y\\direct-1\\a.txt");
}
