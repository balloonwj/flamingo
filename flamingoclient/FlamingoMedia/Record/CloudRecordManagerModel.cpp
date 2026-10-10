/**
 *  录制管理窗口「云端」页的纯逻辑，见 CloudRecordManagerModel.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordManagerModel.h"

#include <stdio.h>
#include <cwctype>
#include "RecordManagerModel.h"   // FormatRecordSize：大小的写法
#include "RecordPolicy.h"         // FormatRecordDuration：时长的写法
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Source/net/Protocol.h"       // cloudrecord_status、cloudrecord_source

namespace screenrecord {

namespace {

//一天的毫秒数
const int64_t kMsPerDay = 24LL * 60 * 60 * 1000;
//64 位整数写成十进制文本所需的缓冲区长度（最多 20 位数字加符号与结尾零，留有余量）
const size_t kInt64TextChars = 24;
//读暂停清单时整数的进制
const int kDecimalBase = 10;
//1 GB 的字节数（配额按 GB 设置）
const int64_t kBytesPerGb = 1024LL * 1024 * 1024;
//千分比的满值
const int kPermilleFull = 1000;
//进度的上限（百分比）
const int kProgressMax = 100;
//日期文字的缓冲区长度（字符）
const int kDateChars = 32;
//副文字各段之间的分隔，与本机页相同
const wchar_t* const kSep = L" · ";
//UTF-16 低位代理的取值范围：数标题字数时不单独计数
const wchar_t kLowSurrogateFirst = 0xDC00;
const wchar_t kLowSurrogateLast = 0xDFFF;

//状态列的颜色：可观看（绿）、进行中（蓝）、等待（灰）、失败（红）、快到期（橙）
const COLORREF kStateOkColor = RGB(31, 157, 85);
const COLORREF kStateRunColor = RGB(45, 108, 223);
const COLORREF kStateWaitColor = RGB(138, 145, 156);
const COLORREF kStateErrColor = RGB(209, 67, 67);
const COLORREF kStateWarnColor = RGB(217, 130, 43);

} // namespace

int CloudRowStateOf(int serverStatus, const CloudRowLocal& local)
{
    //用户暂停了本机的合成或上传（2026-10-07）：只对等待合成、上传中的录像有意义
    if (local.m_paused && (serverStatus == cloudrecord_status_pending || serverStatus == cloudrecord_status_uploading))
        return cloud_row_paused;
    switch (serverStatus)
    {
    //共享中正在录
    case cloudrecord_status_recording:
        return cloud_row_recording;
    //可观看：本机即使还留着任务，也以服务端为准
    case cloudrecord_status_ready:
        return cloud_row_ready;
    //录制出错、没有录到内容
    case cloudrecord_status_failed:
        return cloud_row_record_failed;
    //保存到云端的上传：本机在传就显示进度，否则是中断后等待续传
    case cloudrecord_status_uploading:
        return local.m_uploading ? cloud_row_uploading : cloud_row_upload_stalled;
    //等待合成（以及不认识的状态）：看本机在做什么
    case cloudrecord_status_pending:
    default:
        if (local.m_uploading)
            return cloud_row_uploading;
        if (local.m_composing)
            return cloud_row_composing;
        if (local.m_composeFailed)
            return cloud_row_compose_failed;
        return cloud_row_pending;
    }
}

int CloudDaysLeft(int64_t expireTimeMs, int64_t nowMs)
{
    if (expireTimeMs <= 0)
        return -1;
    const int64_t left = expireTimeMs - nowMs;
    if (left <= 0)
        return 0;
    //向上取整：还剩 1 毫秒到 1 天之间都算 1 天。已经过了到期时刻、服务端还没清理的返回 0（「今天到期」）
    const int64_t days = (left + kMsPerDay - 1) / kMsPerDay;
    if (days > kCloudExpireWarnDays)
        return -1;
    return static_cast<int>(days);
}

std::wstring CloudRowStateText(int state, int progress, int daysLeft)
{
    switch (state)
    {
    //共享中正在录
    case cloud_row_recording:
        return TR(L"录制中");
    //本机正在合成
    case cloud_row_composing:
        if (progress >= 0 && progress <= kProgressMax)
            return TRF(L"合成中 {0}%", progress);
        return TR(L"合成中");
    //本机正在上传
    case cloud_row_uploading:
        if (progress >= 0 && progress <= kProgressMax)
            return TRF(L"上传中 {0}%", progress);
        return TR(L"上传中");
    //可观看：快到期时改为到期提醒
    case cloud_row_ready:
        if (daysLeft == 0)
            return TR(L"今天到期");
        if (daysLeft > 0)
            return TRF(L"{0} 天后到期", daysLeft);
        return TR(L"可观看");
    //本机合成失败
    case cloud_row_compose_failed:
        return TR(L"合成失败");
    //录制出错
    case cloud_row_record_failed:
        return TR(L"没有录到内容");
    //上传中断
    case cloud_row_upload_stalled:
        return TR(L"等待续传");
    //用户暂停了本机的合成或上传
    case cloud_row_paused:
        return TR(L"已暂停");
    //等待合成
    case cloud_row_pending:
    default:
        return TR(L"等待合成");
    }
}

COLORREF CloudRowStateColor(int state, int daysLeft)
{
    switch (state)
    {
    //可观看：快到期时为橙
    case cloud_row_ready:
        return (daysLeft >= 0) ? kStateWarnColor : kStateOkColor;
    //本机正在做
    case cloud_row_composing:
    case cloud_row_uploading:
        return kStateRunColor;
    //失败
    case cloud_row_compose_failed:
    case cloud_row_record_failed:
        return kStateErrColor;
    //等待（录制中、等待合成、等待续传）
    default:
        return kStateWaitColor;
    }
}

bool CloudRowShowsProgress(int state, int progress)
{
    return (state == cloud_row_composing || state == cloud_row_uploading) && progress >= 0 && progress <= kProgressMax;
}

std::wstring CloudRowSubText(const SYSTEMTIME& localStart, int source, bool audioOnly, int64_t durationMs, int64_t bytes)
{
    //日期写成与语言无关的「年-月-日 时:分」，与本机页相同
    wchar_t date[kDateChars] = { 0 };
    _snwprintf_s(date, kDateChars, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u", localStart.wYear, localStart.wMonth,
                 localStart.wDay, localStart.wHour, localStart.wMinute);
    std::wstring text(date);
    text += kSep;
    text += (source == cloudrecord_source_upload) ? TR(L"本机上传") : TR(L"共享录制");
    if (audioOnly)
    {
        text += kSep;
        text += TR(L"录音");
    }
    if (durationMs > 0)
    {
        text += kSep;
        text += FormatRecordDuration(durationMs);
    }
    if (bytes > 0)
    {
        text += kSep;
        text += FormatRecordSize(static_cast<uint64_t>(bytes));
    }
    return text;
}

int CloudRowActions(int state)
{
    switch (state)
    {
    //可观看：全部日常操作
    case cloud_row_ready:
        return cloud_action_play | cloud_action_download | cloud_action_send | cloud_action_rename | cloud_action_delete;
    //共享中正在录：要先停止录制才能删除
    case cloud_row_recording:
        return cloud_action_rename;
    //本机合成失败：可以重新合成
    case cloud_row_compose_failed:
        return cloud_action_recompose | cloud_action_rename | cloud_action_delete;
    //本机正在合成或上传：可以暂停（2026-10-07）
    case cloud_row_composing:
    case cloud_row_uploading:
        return cloud_action_pause | cloud_action_rename | cloud_action_delete;
    //暂停了：可以继续
    case cloud_row_paused:
        return cloud_action_resume | cloud_action_rename | cloud_action_delete;
    //等待合成、没有录到内容、等待续传
    default:
        return cloud_action_rename | cloud_action_delete;
    }
}

std::string EncodePausedCloudRecords(const std::set<int64_t>& ids)
{
    std::string text = "{\"paused\":[";
    bool first = true;
    for (std::set<int64_t>::const_iterator it = ids.begin(); it != ids.end(); ++it)
    {
        char buf[kInt64TextChars];
        sprintf_s(buf, sizeof(buf), "%lld", static_cast<long long>(*it));
        if (!first)
            text += ",";
        text += buf;
        first = false;
    }
    text += "]}";
    return text;
}

bool DecodePausedCloudRecords(const std::string& text, std::set<int64_t>& ids)
{
    ids.clear();
    //格式固定为 {"paused":[n,n,...]}：找到方括号，逐个读出整数
    const std::string head = "{\"paused\":[";
    if (text.compare(0, head.size(), head) != 0)
        return false;
    const size_t close = text.find(']', head.size());
    if (close == std::string::npos || text.compare(close, std::string::npos, "]}") != 0)
        return false;
    std::set<int64_t> parsed;
    size_t pos = head.size();
    while (pos < close)
    {
        const size_t comma = text.find(',', pos);
        const size_t end = (comma == std::string::npos || comma > close) ? close : comma;
        const std::string item = text.substr(pos, end - pos);
        if (item.empty())
            return false;
        for (size_t i = 0; i < item.size(); ++i)
        {
            if (item[i] < '0' || item[i] > '9')
                return false;
        }
        parsed.insert(static_cast<int64_t>(_strtoi64(item.c_str(), NULL, kDecimalBase)));
        pos = end + 1;
    }
    ids.swap(parsed);
    return true;
}

std::wstring CloudUsageText(bool enabled, int64_t usedBytes, int64_t quotaBytes, int64_t retainDays)
{
    if (!enabled)
        return TR(L"管理员没有开通云端录制");
    const std::wstring used = (usedBytes > 0) ? FormatRecordSize(static_cast<uint64_t>(usedBytes)) : std::wstring(L"0 B");
    const long long quotaGb = static_cast<long long>((quotaBytes > 0 ? quotaBytes : 0) / kBytesPerGb);
    //{0} 为已用大小，{1} 为配额的 GB 数
    std::wstring text = TRF(L"已用 {0} / 共 {1} GB", used, quotaGb);
    text += kSep;
    if (retainDays <= 0)
        text += TR(L"永久保留");
    else
        text += TRF(L"保留 {0} 天", static_cast<long long>(retainDays));
    return text;
}

bool CloudUsageWarn(bool enabled, int64_t usedBytes, int64_t quotaBytes)
{
    return !enabled || quotaBytes <= 0 || usedBytes >= quotaBytes;
}

CloudTitleResult NormalizeCloudTitle(const std::wstring& oldTitle, const std::wstring& input, std::wstring& out)
{
    size_t begin = 0;
    size_t end = input.size();
    while (begin < end && iswspace(input[begin]))
        ++begin;
    while (end > begin && iswspace(input[end - 1]))
        --end;
    const std::wstring trimmed = input.substr(begin, end - begin);
    if (trimmed.empty())
        return cloud_title_empty;
    //按 Unicode 字符计数：代理对的低位不单独计数
    size_t chars = 0;
    for (size_t i = 0; i < trimmed.size(); ++i)
    {
        if (trimmed[i] < kLowSurrogateFirst || trimmed[i] > kLowSurrogateLast)
            ++chars;
    }
    if (chars > kCloudTitleMaxChars)
        return cloud_title_too_long;
    if (trimmed == oldTitle)
        return cloud_title_unchanged;
    out = trimmed;
    return cloud_title_ok;
}

bool CloudTrailingRect(const RECT& listRect, int rowIndex, int rowHeight, int scrollPos, int width, int rightInset,
                       RECT& out)
{
    if (rowHeight <= 0 || rowIndex < 0 || width <= 0)
        return false;
    const int top = listRect.top + rowIndex * rowHeight - scrollPos;
    const int bottom = top + rowHeight;
    if (bottom <= listRect.top || top >= listRect.bottom)
        return false;
    const int right = listRect.right - rightInset;
    const int left = right - width;
    if (left < listRect.left)
        return false;
    out.left = left;
    out.top = top;
    out.right = right;
    out.bottom = bottom;
    return true;
}

int CloudUsagePermille(int64_t usedBytes, int64_t quotaBytes)
{
    if (quotaBytes <= 0)
        return kPermilleFull;
    if (usedBytes <= 0)
        return 0;
    if (usedBytes >= quotaBytes)
        return kPermilleFull;
    return static_cast<int>(usedBytes * kPermilleFull / quotaBytes);
}

} // namespace screenrecord
