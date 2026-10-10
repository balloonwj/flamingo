/**
 *  exe 侧的语音消息中心，说明见 VoiceHub.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "VoiceHub.h"

#include <windows.h>
#include <fstream>
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/MD5Core.h"
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace balloonvoice {

namespace {

//临时文件名的前缀与语音文件的扩展名
const wchar_t kTempPrefix[] = L"rec_";
const wchar_t kVoiceExt[] = L".ogg";
//暂停中试听另存文件在录音临时文件名后加的后缀（扩展名之前）
const wchar_t kPausedSnapshotSuffix[] = L"_paused";
//读语音文件的上限（字节），与服务端的语音文件上限相同
const size_t kVoiceMaxFileBytes = 1024 * 1024;
//MD5 摘要的字节数与十六进制字符表
const int kMd5DigestBytes = 16;
const char kHexDigits[] = "0123456789abcdef";
//一个字节拆成两个十六进制字符时的位移与掩码
const int kNibbleShift = 4;
const unsigned char kNibbleMask = 0x0F;
//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;
//64 位文件大小里高 32 位的位移
const int kHighDwordShift = 32;

//逐级创建目录（已存在时不算失败）
bool ensureDirectory(const std::wstring& dir)
{
    if (dir.empty())
        return false;
    for (size_t i = 0; i < dir.size(); ++i)
    {
        if (dir[i] != L'\\' && dir[i] != L'/')
            continue;
        //跳过盘符（如 E:\）与开头的分隔符
        if (i == 0 || dir[i - 1] == L':')
            continue;
        ::CreateDirectoryW(dir.substr(0, i).c_str(), NULL);
    }
    ::CreateDirectoryW(dir.c_str(), NULL);
    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

//文件是否存在
bool fileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

//文件字节数；取不到时返回 -1
int64_t fileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return -1;
    return (static_cast<int64_t>(data.nFileSizeHigh) << kHighDwordShift) | data.nFileSizeLow;
}

//宽字符转 UTF-8（只用于日志）
std::string toUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    std::string out(static_cast<size_t>(len > 0 ? len : 0), '\0');
    if (len > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

} // namespace

VoiceRecordOutput::VoiceRecordOutput()
    : m_durationMs(0)
    , m_fileBytes(0)
    , m_reusedExisting(false)
{
}

CVoiceHub& CVoiceHub::GetInstance()
{
    static CVoiceHub hub;
    return hub;
}

CVoiceHub::CVoiceHub()
    : m_module(NULL)
    , m_listener(NULL)
    , m_recPaused(false)
    , m_playListener(NULL)
    , m_playToken(0)
    , m_lastToken(0)
{
}

void CVoiceHub::AttachModule(flamingomedia::IMediaModule* module)
{
    //模块要卸载：正在录音时先取消、正在播放时先停止，免得之后再调已卸载的模块
    if (module == NULL && m_module != NULL && m_listener != NULL)
        CancelRecord();
    if (module == NULL && m_module != NULL)
        endPlay(voice_play_end_stopped, true, true);
    m_module = module;
}

bool CVoiceHub::IsModuleReady() const
{
    return m_module != NULL;
}

int CVoiceHub::StartRecord(IVoiceRecordListener* listener, const std::wstring& voiceDir,
                           const std::string& clientMsgId)
{
    if (m_module == NULL || listener == NULL || clientMsgId.empty())
        return flamingomedia::media_voice_record_not_ready;
    if (m_listener != NULL)
    {
        LOG_WARNING("voice hub: start ignored, another conversation is recording.");
        return flamingomedia::media_voice_record_already;
    }
    if (!ensureDirectory(voiceDir))
    {
        LOG_ERROR("voice hub: cannot create voice directory %s.", toUtf8(voiceDir).c_str());
        return flamingomedia::media_voice_record_device_error;
    }
    const std::wstring path = VoiceRecordTempPath(voiceDir, clientMsgId);
    //开始录音前停止正在播的语音：一边放一边录会把扬声器的声音录进去
    endPlay(voice_play_end_stopped, true, true);
    const int result = m_module->StartVoiceRecord(path.c_str());
    if (result != flamingomedia::media_voice_record_ok)
    {
        LOG_INFO("voice hub: start record refused, result %d.", result);
        return result;
    }
    m_listener = listener;
    m_recPath = path;
    m_voiceDir = voiceDir;
    LOG_INFO("voice hub: recording started, clientmsgid %s.", clientMsgId.c_str());
    return result;
}

int CVoiceHub::StopRecord(VoiceRecordOutput& out)
{
    if (m_module == NULL || m_listener == NULL)
        return voice_stop_not_recording;
    const std::wstring recPath = m_recPath;
    const std::wstring voiceDir = m_voiceDir;
    resetRecordState();

    flamingomedia::MediaVoiceRecordInfo info;
    memset(&info, 0, sizeof(info));
    if (!m_module->StopVoiceRecord(&info))
    {
        LOG_WARNING("voice hub: module stop failed, no file.");
        ::DeleteFileW(recPath.c_str());
        return voice_stop_failed;
    }
    //不到 1 秒：不发送，删掉文件
    if (info.m_durationMs < kVoiceMinDurationMs)
    {
        LOG_INFO("voice hub: record too short, %lld ms.", static_cast<long long>(info.m_durationMs));
        ::DeleteFileW(recPath.c_str());
        return voice_stop_too_short;
    }
    std::string md5;
    if (!ComputeVoiceFileMd5(recPath, md5))
    {
        LOG_ERROR("voice hub: cannot read recorded file for md5.");
        ::DeleteFileW(recPath.c_str());
        return voice_stop_failed;
    }
    //改名为缓存文件名；同内容的文件已在缓存里时直接用它
    const std::wstring cachePath = VoiceCachePath(voiceDir, md5);
    const bool reused = fileExists(cachePath);
    if (reused)
    {
        ::DeleteFileW(recPath.c_str());
    }
    else if (!::MoveFileExW(recPath.c_str(), cachePath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        LOG_ERROR("voice hub: rename recorded file failed, error %lu.", ::GetLastError());
        ::DeleteFileW(recPath.c_str());
        return voice_stop_failed;
    }
    out.m_path = cachePath;
    out.m_reusedExisting = reused;
    out.m_md5 = md5;
    out.m_durationMs = info.m_durationMs;
    out.m_fileBytes = fileSize(cachePath);
    out.m_wave.clear();
    for (int i = 0; i < info.m_waveCount && i < flamingomedia::kMediaVoiceWaveBars; ++i)
        out.m_wave.push_back(info.m_wave[i]);
    LOG_INFO("voice hub: recording stopped, %lld ms, %lld bytes, md5 %s.", static_cast<long long>(out.m_durationMs),
             static_cast<long long>(out.m_fileBytes), md5.c_str());
    return voice_stop_ok;
}

void CVoiceHub::CancelRecord()
{
    if (m_listener == NULL)
        return;
    const std::wstring recPath = m_recPath;
    resetRecordState();
    if (m_module != NULL)
        m_module->CancelVoiceRecord();
    //DLL 取消时不写文件；以防万一删掉同名的临时文件
    ::DeleteFileW(recPath.c_str());
    LOG_INFO("voice hub: recording cancelled.");
}

bool CVoiceHub::PauseRecord()
{
    if (m_module == NULL || m_listener == NULL || m_recPaused)
        return false;
    if (!m_module->PauseVoiceRecord())
    {
        LOG_WARNING("voice hub: pause refused by the media module.");
        return false;
    }
    m_recPaused = true;
    LOG_INFO("voice hub: recording paused.");
    return true;
}

int CVoiceHub::ResumeRecord()
{
    if (m_module == NULL)
        return flamingomedia::media_voice_record_not_ready;
    if (m_listener == NULL || !m_recPaused)
        return flamingomedia::media_voice_record_not_paused;
    //与开始录音一样：一边放一边录会把扬声器的声音录进去，先停止正在播的语音并通知发起播放的窗口
    endPlay(voice_play_end_stopped, true, true);
    const int result = m_module->ResumeVoiceRecord();
    if (result == flamingomedia::media_voice_record_ok)
    {
        m_recPaused = false;
        //暂停中试听另存的文件已经过时（播放已在上面停止），删掉
        deletePausedSnapshot();
        LOG_INFO("voice hub: recording resumed.");
    }
    else
    {
        LOG_INFO("voice hub: resume refused, result %d, still paused.", result);
    }
    return result;
}

bool CVoiceHub::IsRecordPaused() const
{
    return m_listener != NULL && m_recPaused;
}

bool CVoiceHub::SnapshotPausedRecord(std::wstring& outPath, int64_t& outDurationMs)
{
    outPath.clear();
    outDurationMs = 0;
    if (m_module == NULL || m_listener == NULL || !m_recPaused || m_recPath.empty())
        return false;
    const std::wstring path = VoicePausedSnapshotPath(m_recPath);
    int64_t durationMs = 0;
    if (!m_module->SnapshotVoiceRecord(path.c_str(), &durationMs))
    {
        LOG_WARNING("voice hub: paused snapshot failed.");
        ::DeleteFileW(path.c_str());
        return false;
    }
    m_snapshotPath = path;
    outPath = path;
    outDurationMs = durationMs;
    LOG_INFO("voice hub: paused snapshot ready for preview, %lld ms.", static_cast<long long>(durationMs));
    return true;
}

void CVoiceHub::deletePausedSnapshot()
{
    if (m_snapshotPath.empty())
        return;
    ::DeleteFileW(m_snapshotPath.c_str());
    m_snapshotPath.clear();
}

bool CVoiceHub::DiscardRecorded(const VoiceRecordOutput& out)
{
    //缓存里原本就有的同内容文件是别的消息在用的，不删
    if (out.m_path.empty() || out.m_reusedExisting)
        return false;
    if (!::DeleteFileW(out.m_path.c_str()))
    {
        LOG_WARNING("voice hub: delete discarded voice failed, error %lu.", ::GetLastError());
        return false;
    }
    LOG_INFO("voice hub: discarded recorded voice, md5 %s.", out.m_md5.c_str());
    return true;
}

bool CVoiceHub::IsRecordingFor(const IVoiceRecordListener* listener) const
{
    return listener != NULL && m_listener == listener;
}

void CVoiceHub::DetachListener(IVoiceRecordListener* listener)
{
    if (listener != NULL && m_listener == listener)
        CancelRecord();
}

void CVoiceHub::OnRecordLevel(int level)
{
    if (m_listener != NULL)
        m_listener->OnVoiceRecordLevel(level);
}

void CVoiceHub::OnRecordLimitReached()
{
    if (m_listener != NULL)
        m_listener->OnVoiceRecordLimitReached();
}

bool CVoiceHub::StartPlay(IVoicePlayListener* listener, const VoicePlayKey& key, const std::wstring& path)
{
    if (m_module == NULL || listener == NULL || path.empty())
    {
        return false;
    }
    //正在播别的：先结束并通知原来的窗口；模块的 PlayVoice 自己会停掉旧的，这里不必再调 StopVoicePlay
    endPlay(voice_play_end_stopped, true, false);
    //编号只增不减、跳过 0（0 表示没有在播放）
    ++m_lastToken;
    if (m_lastToken == 0)
    {
        ++m_lastToken;
    }
    if (!m_module->PlayVoice(path.c_str(), m_lastToken))
    {
        LOG_WARNING("voice hub: play failed, msgid %lld.", key.m_msgid);
        return false;
    }
    m_playListener = listener;
    m_playKey = key;
    m_playToken = m_lastToken;
    LOG_INFO("voice hub: playing msgid %lld, token %u.", key.m_msgid, m_playToken);
    return true;
}

void CVoiceHub::StopPlay()
{
    endPlay(voice_play_end_stopped, true, true);
}

bool CVoiceHub::IsPlayingFor(const IVoicePlayListener* listener) const
{
    return listener != NULL && m_playListener == listener;
}

void CVoiceHub::DetachPlayListener(IVoicePlayListener* listener)
{
    //窗口要销毁：停掉它在播的，不再给它发通知
    if (listener != NULL && m_playListener == listener)
    {
        endPlay(voice_play_end_stopped, false, true);
    }
}

void CVoiceHub::OnPlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs)
{
    //旧编号（已停止或已改播别的）的进度丢弃
    if (token == 0 || token != m_playToken || m_playListener == NULL)
    {
        return;
    }
    m_playListener->OnVoicePlayProgress(m_playKey, playedMs, totalMs);
}

void CVoiceHub::OnPlayFinished(uint32_t token)
{
    if (token == 0 || token != m_playToken)
    {
        return;
    }
    //模块播完时扬声器已关闭，不必再调 StopVoicePlay
    endPlay(voice_play_end_finished, true, false);
}

void CVoiceHub::endPlay(int reason, bool notify, bool stopModule)
{
    if (m_playListener == NULL)
    {
        return;
    }
    //先清掉播放状态再通知：收到「播完」的窗口可能马上接着播下一条，会重新进入 StartPlay
    IVoicePlayListener* listener = m_playListener;
    const VoicePlayKey key = m_playKey;
    m_playListener = NULL;
    m_playKey = VoicePlayKey();
    m_playToken = 0;
    if (stopModule && m_module != NULL)
    {
        m_module->StopVoicePlay();
    }
    LOG_INFO("voice hub: playback of msgid %lld ended, reason %d.", key.m_msgid, reason);
    if (notify)
    {
        listener->OnVoicePlayEnded(key, reason);
    }
}

void CVoiceHub::resetRecordState()
{
    m_listener = NULL;
    m_recPath.clear();
    m_voiceDir.clear();
    m_recPaused = false;
    //录音结束（停止、取消、关窗）：暂停中试听另存的文件不再需要
    deletePausedSnapshot();
}

std::wstring VoiceRecordTempPath(const std::wstring& voiceDir, const std::string& clientMsgId)
{
    //clientmsgid 是 32 位大写十六进制（或 ASCII 的后备串），逐字节转成宽字符即可
    return voiceDir + kTempPrefix + std::wstring(clientMsgId.begin(), clientMsgId.end()) + kVoiceExt;
}

std::wstring VoicePausedSnapshotPath(const std::wstring& recPath)
{
    const std::wstring ext(kVoiceExt);
    std::wstring base = recPath;
    if (base.size() >= ext.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0)
        base.erase(base.size() - ext.size());
    return base + kPausedSnapshotSuffix + ext;
}

std::wstring VoiceCachePath(const std::wstring& voiceDir, const std::string& md5)
{
    return voiceDir + std::wstring(md5.begin(), md5.end()) + kVoiceExt;
}

bool ComputeVoiceFileMd5(const std::wstring& path, std::string& md5)
{
    md5.clear();
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return false;
    std::vector<char> data;
    char buf[kReadChunkBytes];
    while (in)
    {
        in.read(buf, sizeof(buf));
        const std::streamsize got = in.gcount();
        if (got > 0)
            data.insert(data.end(), buf, buf + got);
        if (data.size() > kVoiceMaxFileBytes)
            return false;
    }
    if (in.bad())
        return false;
    MD5_CTX ctx;
    MD5Init(&ctx);
    if (!data.empty())
        MD5Update(&ctx, reinterpret_cast<unsigned char*>(&data[0]), static_cast<unsigned int>(data.size()));
    unsigned char digest[kMd5DigestBytes] = { 0 };
    MD5Final(digest, &ctx);
    for (int i = 0; i < kMd5DigestBytes; ++i)
    {
        md5 += kHexDigits[(digest[i] >> kNibbleShift) & kNibbleMask];
        md5 += kHexDigits[digest[i] & kNibbleMask];
    }
    return true;
}

} // namespace balloonvoice
