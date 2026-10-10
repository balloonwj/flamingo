/**
 *  DirectFileTransfer.cpp
 *
 *  单聊单文件 P2P 直传的纯逻辑部分，见 DirectFileTransfer.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "DirectFileTransfer.h"

#include <windows.h>

#include "../../net/Protocol.h"   //folder_transfer_state / error_code_*

namespace foldertransfer {

namespace {

//落地目录名的前缀：<下载缓存目录>\direct-<会话号>\，与普通下载缓存里按 MD5 命名的文件区分开
const wchar_t* const kDirectDirPrefix = L"direct-";

//FILETIME 与 Unix 时间的换算：FILETIME 以 1601-01-01 起的 100 纳秒为单位，两个纪元相差 11644473600 秒
const int64_t kFileTimeUnixEpochDiff = 116444736000000000LL;
const int64_t kFileTimeTicksPerMs = 10000;

//把十六进制串里的大写字母转成小写（MD5 统一按小写记）
std::string ToLowerHex(const std::string& hex)
{
    std::string out = hex;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'F')
            out[i] = (char)(out[i] - 'A' + 'a');
    }
    return out;
}

//路径分隔符判断
bool IsPathSep(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

//去掉末尾的路径分隔符
std::wstring TrimTrailingSeps(const std::wstring& path)
{
    std::wstring out = path;
    while (!out.empty() && IsPathSep(out[out.size() - 1]))
        out.erase(out.size() - 1);
    return out;
}

//是不是一个已存在的目录
bool IsExistingDir(const std::wstring& path)
{
    const DWORD attrs = ::GetFileAttributesW(MakeLongPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/**
 *  逐级创建目录（各级已存在不算失败）。
 *  @param dir 目录完整路径，不带末尾分隔符
 *  @return 最后这一级目录存在返回 true
 */
bool CreateDirTree(const std::wstring& dir)
{
    if (dir.empty())
        return false;
    if (IsExistingDir(dir))
        return true;
    //从第二个字符起找分隔符，逐个前缀建一次：盘符（"D:"）、UNC 前缀等建不出来的前缀返回失败，直接跳过即可，
    //成败以最后对目标目录本身的检查为准
    for (size_t i = 1; i < dir.size(); ++i)
    {
        if (IsPathSep(dir[i]))
            ::CreateDirectoryW(MakeLongPath(dir.substr(0, i)).c_str(), NULL);
    }
    ::CreateDirectoryW(MakeLongPath(dir).c_str(), NULL);
    return IsExistingDir(dir);
}

} // namespace

int DecideDirectSendOnInviteResponse(int code)
{
    if (code == error_code_ok)
        return kDirectSendWait;
    //文件名含敏感词：改走上传服务器同样会被 1801 拒绝，改了也没用
    if (code == error_code_file_sensitive_word)
        return kDirectSendBlocked;
    //其余失败（622 不能直传、607 对方离线、620 名额已满、619 服务器繁忙、服务器内部错误等）：都还能发离线文件
    return kDirectSendToOffline;
}

bool ShouldTryDirectFileSend(bool peerKnownOffline, bool p2pRegistered, bool forceOffline)
{
    //要求按离线文件上传、对方确知离线、本进程没有登记直传的业务方（P2P 模块没加载、诊断开关关掉了）：都不试
    return !forceOffline && !peerKnownOffline && p2pRegistered;
}

int DecideDirectSendOnState(int state, int errCode)
{
    switch (state)
    {
    //响铃中、传输中：继续等
    case folder_state_ringing:
    case folder_state_transferring:
        return kDirectSendWait;

    //对方拒绝：不再上传
    case folder_state_rejected:
        return kDirectSendRejected;

    //本端撤回了邀请（服务端回给对端的推送，本端收到时说明撤回已生效）
    case folder_state_canceled:
        return kDirectSendCanceled;

    //对方离线、5 分钟没接收、本端点了「发送离线文件」：改走上传服务器
    case folder_state_peer_offline:
    case folder_state_timeout:
    case folder_state_to_offline:
        return kDirectSendToOffline;

    //失败：同意之后建不起 P2P 链路（622）改走上传服务器；其余是传输中途的失败，给「重试」
    case folder_state_failed:
        return (errCode == error_code_folder_direct_unavailable) ? kDirectSendToOffline : kDirectSendFailed;

    //完成
    case folder_state_completed:
        return kDirectSendCompleted;

    //未知状态：解析层已按取值范围挡过，走到这里不做处置
    default:
        return kDirectSendWait;
    }
}

int DecideDirectRecvOnState(int state, int errCode)
{
    switch (state)
    {
    //响铃中、传输中：继续等
    case folder_state_ringing:
    case folder_state_transferring:
        return kDirectRecvWait;

    //完成
    case folder_state_completed:
        return kDirectRecvCompleted;

    //本端拒绝
    case folder_state_rejected:
        return kDirectRecvRejected;

    //发送端撤回了邀请
    case folder_state_canceled:
        return kDirectRecvSenderCanceled;

    //发送端离线、邀请超时、发送端改为离线发送：对方随后会发来一条普通离线文件消息
    case folder_state_peer_offline:
    case folder_state_timeout:
    case folder_state_to_offline:
        return kDirectRecvToOffline;

    //失败：建不起 P2P 链路（622）时发送端会改为离线文件；其余是传输中途的失败，发送端可在保留期内续传
    case folder_state_failed:
        return (errCode == error_code_folder_direct_unavailable) ? kDirectRecvToOffline : kDirectRecvFailed;

    //未知状态：不做处置
    default:
        return kDirectRecvWait;
    }
}

int DirectFileCloseErrCode(int errCode, bool decidedP2P)
{
    //用户取消要照实报：报成 622 的话发送端会自动改为上传服务器，把用户不想发的文件又发了出去
    if (errCode == error_code_folder_canceled)
        return errCode;
    //从没走上 P2P 链路（直连、打洞、imgserver 一对一中转都不通）：直传做不成，让发送端改为离线文件
    if (!decidedP2P)
        return error_code_folder_direct_unavailable;
    //走上过 P2P 链路之后的失败：照报原因，保留期内可续传
    return errCode;
}

bool BuildSingleFileManifest(const std::wstring& filePath, std::wstring& outRootPath, FolderManifest& outManifest)
{
    if (filePath.empty())
        return false;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(MakeLongPath(filePath).c_str(), GetFileExInfoStandard, &fad))
        return false;
    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;

    //根目录取文件所在目录、根名与相对路径都取文件名：发送会话按「根目录 + 相对路径」读到的正是这个文件
    const std::wstring::size_type sep = filePath.find_last_of(L"\\/");
    if (sep == std::wstring::npos || sep + 1 >= filePath.size())
        return false;
    const std::wstring dir = TrimTrailingSeps(filePath.substr(0, sep));
    const std::wstring name = filePath.substr(sep + 1);
    std::string nameUtf8;
    if (dir.empty() || !Utf16ToUtf8(name, nameUtf8) || nameUtf8.empty())
        return false;

    ULARGE_INTEGER size;
    size.LowPart = fad.nFileSizeLow;
    size.HighPart = fad.nFileSizeHigh;
    ULARGE_INTEGER mtime;
    mtime.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    mtime.HighPart = fad.ftLastWriteTime.dwHighDateTime;

    FolderManifestFile file;
    file.m_relativePath = nameUtf8;
    file.m_sizeBytes = (int64_t)size.QuadPart;
    file.m_mtimeMs = ((int64_t)mtime.QuadPart - kFileTimeUnixEpochDiff) / kFileTimeTicksPerMs;

    outManifest = FolderManifest();
    outManifest.m_folderName = nameUtf8;
    outManifest.m_files.push_back(file);
    outRootPath = dir;
    return true;
}

std::wstring DirectFileSaveDir(const std::wstring& chatFileDir, const std::string& sessionId)
{
    if (chatFileDir.empty() || sessionId.empty())
        return std::wstring();

    std::wstring dir = TrimTrailingSeps(chatFileDir);
    dir += L"\\";
    dir += kDirectDirPrefix;
    //会话号只含数字、十六进制字母与连字符（服务端 FolderTransferManager 生成），逐字节放宽即可
    for (size_t i = 0; i < sessionId.size(); ++i)
        dir += (wchar_t)(unsigned char)sessionId[i];
    dir += L"\\";
    return dir;
}

DirectFileSendHooks::DirectFileSendHooks()
{
}

int DirectFileSendHooks::RunPreamble(IFolderControlSender& sender, FolderManifest& manifest, std::wstring& rootPath)
{
    (void)sender;
    (void)manifest;
    (void)rootPath;
    return 0;
}

void DirectFileSendHooks::OnControlFrame(const unsigned char* data, size_t len)
{
    (void)data;
    (void)len;
}

void DirectFileSendHooks::OnStopping()
{
}

void DirectFileSendHooks::OnFileMd5Computed(int64_t index, const std::string& md5Hex)
{
    (void)index;
    std::lock_guard<std::mutex> guard(m_mutex);
    m_md5 = ToLowerHex(md5Hex);
}

std::string DirectFileSendHooks::Md5() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_md5;
}

void DirectFileRecvHooks::OnFileVerified(const std::wstring& finalPath, const std::string& md5Hex)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_finalPath = finalPath;
    m_md5 = ToLowerHex(md5Hex);
}

std::string DirectFileRecvHooks::Md5() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_md5;
}

std::wstring DirectFileRecvHooks::FinalPath() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_finalPath;
}

DirectFileRecvHooks::DirectFileRecvHooks(const std::wstring& saveDir)
    : m_saveDir(saveDir)
{
}

int DirectFileRecvHooks::OnLanesReady(IFolderControlSender& sender)
{
    (void)sender;
    return 0;
}

int DirectFileRecvHooks::OnControlIdle(IFolderControlSender& sender)
{
    (void)sender;
    return 0;
}

int DirectFileRecvHooks::OnControlFrame(IFolderControlSender& sender, const std::string& frame)
{
    (void)sender;
    (void)frame;
    return 0;
}

int DirectFileRecvHooks::PrepareRoot(const std::string& folderName, std::vector<std::string>& dirs,
                                     std::vector<FolderRelayFileEntry>& files, std::wstring& rootPath)
{
    (void)folderName;
    (void)dirs;
    (void)files;
    const std::wstring dir = TrimTrailingSeps(m_saveDir);
    if (!CreateDirTree(dir))
        return error_code_folder_mkdir_fail;
    rootPath = dir;
    return 0;
}

void DirectFileRecvHooks::OnPartFileOpened(const std::wstring& partPath)
{
    (void)partPath;
}

bool DirectFileRecvHooks::ReplaceExistingOnFinish() const
{
    //落地目录是这一次直传独占的，同名文件只可能是上一次尝试留下的
    return true;
}

} // namespace foldertransfer
