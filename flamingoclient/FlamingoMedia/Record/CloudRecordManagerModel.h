/**
 *  录制管理窗口「云端」页的纯逻辑，CloudRecordManagerModel.h
 *
 *  第 27 期 27d（docs/屏幕共享与录制方案.md 12.6.3、12.6.5，效果图 2026-10-06 已确认）。不依赖窗口与系统对象，全部是纯函数，
 *  便于单元测试（Source/Tests/CloudRecordClientTests.cpp 的 CRM 系列，标签 [cloudrec]）：
 *    - 每一行的状态：由服务端的录像状态与本机正在做的事（合成、上传、合成失败）合起来决定；
 *    - 状态列的文字与颜色：「可观看」「合成中 45%」「上传中 72%」「等待合成」「合成失败」「录制中」「没有录到内容」「等待续传」，
 *      可观看的录像离到期不到 3 天时改为「3 天后到期」「今天到期」；
 *    - 副文字：「2026-10-06 14:20 · 共享录制 · 录音 · 58:12 · 612.4 MB」，时长、大小为 0 时省去；
 *    - 一行可做的操作：播放、下载、发送只对可观看的录像，重新合成只对本机合成失败的共享录制；
 *    - 右上角的用量：「已用 12.1 GB / 共 50 GB · 保留 30 天」。
 *
 *  典型用法（录制管理窗口重建云端页时）：
 *      const int state = screenrecord::CloudRowStateOf(info.m_status, local);
 *      const int days = screenrecord::CloudDaysLeft(info.m_expireTime, nowMs);
 *      list->SetTrailing(index, CloudRowStateText(state, local.m_progress, days), CloudRowStateColor(state, days), ...);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <windows.h>

namespace screenrecord {

//云端页一行的状态（CloudRowStateOf 的返回值）
enum CloudRowState
{
    cloud_row_recording      = 1,   //共享中正在录（服务端状态为录制中）
    cloud_row_pending        = 2,   //录制已结束、等开启者的客户端合成，本机还没开始做
    cloud_row_composing      = 3,   //本机正在合成：排队、取令牌、下载原始帧、合成，都算
    cloud_row_uploading      = 4,   //本机正在上传：合成后的 MP4，或「保存到云端」的本机文件
    cloud_row_ready          = 5,   //可观看
    cloud_row_compose_failed = 6,   //本次登录里本机合成失败过，可以「重新合成」
    cloud_row_record_failed  = 7,   //服务端：录制出错、一点内容都没录到，只能删除
    cloud_row_upload_stalled = 8,   //服务端为上传中，本机没有在传（上传中断，下次登录时按本机记录续传）
    cloud_row_paused         = 9,   //用户暂停了本机的合成或上传（2026-10-07，方案第 14.4 节）：点「继续」接着做，下次登录不自动继续
};

//本机对一条录像正在做的事（云端录像中心给出）
struct CloudRowLocal
{
    bool m_composing;       //本机有这条录像的合成任务（排队、取令牌、下载原始帧、合成、取上传令牌都算）
    bool m_uploading;       //本机正在上传它的文件
    int  m_progress;        //合成或上传的进度，0～100；不知道时为 -1
    bool m_composeFailed;   //本次登录里合成失败过
    bool m_paused;          //用户暂停了这条录像本机的合成或上传（2026-10-07）

    CloudRowLocal()
        : m_composing(false), m_uploading(false), m_progress(-1), m_composeFailed(false), m_paused(false)
    {
    }
};

//云端页一行可做的操作（按位组合，CloudRowActions 的返回值）
enum CloudRowAction
{
    cloud_action_play      = 0x01,   //播放：下载到本机缓存后用系统播放器打开
    cloud_action_download  = 0x02,   //下载…：保存到用户选的位置
    cloud_action_send      = 0x04,   //发送到会话…
    cloud_action_rename    = 0x08,   //重命名（改标题，服务端过敏感词）
    cloud_action_delete    = 0x10,   //删除（服务端删记录与文件，会话里的卡片随之不能再看）
    cloud_action_recompose = 0x20,   //重新合成：只在右键菜单里，对本机合成失败的共享录制
    cloud_action_pause     = 0x40,   //暂停（2026-10-07）：停下本机的合成或上传，服务器上的数据保留；合成中、上传中才有
    cloud_action_resume    = 0x80,   //继续（2026-10-07）：接着做暂停了的合成或上传；暂停的才有
};

/**
 *  暂停清单写成文本（2026-10-07，方案第 14.4 节）：暂停的录像下次登录不自动继续，清单存在本机。
 *  @param ids 暂停的录像 id
 *  @return JSON 文本，如 {"paused":[3,12]}
 */
std::string EncodePausedCloudRecords(const std::set<int64_t>& ids);

/**
 *  读回暂停清单。
 *  @param text EncodePausedCloudRecords 写出的文本
 *  @param ids  [出参] 暂停的录像 id；读不出时为空
 *  @return 格式正确返回 true
 */
bool DecodePausedCloudRecords(const std::string& text, std::set<int64_t>& ids);

//离到期还剩几天时开始在状态列提醒（含当天）
const int kCloudExpireWarnDays = 3;

/**
 *  一行的状态。本机正在上传优先于正在合成（上传阶段的合成任务仍在）；本机合成失败只在服务端仍为等待合成时有效。
 *  @param serverStatus 服务端的状态，取值见 Protocol.h 的 cloudrecord_status
 *  @param local        本机正在做的事
 *  @return CloudRowState 之一；不认识的服务端状态按等待合成处理
 */
int CloudRowStateOf(int serverStatus, const CloudRowLocal& local);

/**
 *  离到期还剩几天，用于状态列的到期提醒。
 *  @param expireTimeMs 到期时间（Unix 毫秒）；0 表示永久保留
 *  @param nowMs        当前时间（Unix 毫秒）
 *  @return 不满 kCloudExpireWarnDays 天时为剩余天数（向上取整，已过期或当天为 0）；永久保留或还早时为 -1
 */
int CloudDaysLeft(int64_t expireTimeMs, int64_t nowMs);

/**
 *  状态列的文字（已按界面语言翻译）。
 *  @param state    CloudRowState 之一
 *  @param progress 合成或上传的进度 0～100；-1 表示不知道，这时只写「合成中」「上传中」
 *  @param daysLeft CloudDaysLeft 的返回值；可观看的录像不为 -1 时写「N 天后到期」或「今天到期」
 */
std::wstring CloudRowStateText(int state, int progress, int daysLeft);

/**
 *  状态列的颜色：可观看为绿，合成中与上传中为蓝，等待合成、录制中、等待续传为灰，失败为红，快到期为橙。
 *  @param state    CloudRowState 之一
 *  @param daysLeft CloudDaysLeft 的返回值
 */
COLORREF CloudRowStateColor(int state, int daysLeft);

/**
 *  状态列是否画进度条：合成中、上传中且进度已知时画。
 */
bool CloudRowShowsProgress(int state, int progress);

/**
 *  一行的副文字：「开始时间 · 来源 · 录音 · 时长 · 大小」，如「2026-10-06 14:20 · 共享录制 · 58:12 · 612.4 MB」。
 *  只有声音时才有「录音」一段；时长、大小为 0 时省去那一段（还没录完、还没合成）。
 *  @param localStart 开始时间（本地时间，只用年、月、日、时、分）
 *  @param source     来源，取值见 Protocol.h 的 cloudrecord_source
 *  @param audioOnly  是否只有声音
 *  @param durationMs 时长（毫秒）
 *  @param bytes      最终文件字节数
 */
std::wstring CloudRowSubText(const SYSTEMTIME& localStart, int source, bool audioOnly, int64_t durationMs, int64_t bytes);

/**
 *  一行可做的操作（列表只列本人的录像，开启者对自己的录像都有管理权限）。
 *  @param state CloudRowState 之一
 *  @return CloudRowAction 的按位组合：可观看时为播放、下载、发送、重命名、删除；录制中只能重命名；本机合成失败时另有重新合成；
 *          其余为重命名、删除
 */
int CloudRowActions(int state);

/**
 *  右上角的用量文字：「已用 12.1 GB / 共 50 GB · 保留 30 天」，保留天数为 0 时为「· 永久保留」；
 *  企业没有开通时为「管理员没有开通云端录制」。
 *  @param enabled    企业是否开通了云端录制
 *  @param usedBytes  已用字节数
 *  @param quotaBytes 配额字节数
 *  @param retainDays 保留天数，0 为永久
 */
std::wstring CloudUsageText(bool enabled, int64_t usedBytes, int64_t quotaBytes, int64_t retainDays);

/**
 *  用量是否要以提醒色显示：没有开通、配额为 0、已用不少于配额。
 */
bool CloudUsageWarn(bool enabled, int64_t usedBytes, int64_t quotaBytes);

/**
 *  已用占配额的比例（0～1000，千分比），用于用量条；配额为 0 时为 1000。
 */
int CloudUsagePermille(int64_t usedBytes, int64_t quotaBytes);

//云端录像标题的字数上限，与服务端 CloudRecordRules::kTitleMaxChars 一致
const size_t kCloudTitleMaxChars = 50;

//NormalizeCloudTitle 的结果
enum CloudTitleResult
{
    cloud_title_ok        = 0,   //可以改名，out 为去掉首尾空白后的标题
    cloud_title_unchanged = 1,   //与原标题相同，不需要提交
    cloud_title_empty     = 2,   //去掉首尾空白后为空
    cloud_title_too_long  = 3,   //超过 kCloudTitleMaxChars 个字（代理对算一个字，与服务端按 UTF-8 字符计数一致）
};

/**
 *  校验重命名时输入的标题，规则与服务端 CloudRecordRules::normalizeTitle 相同（去掉首尾空白后 1～50 个字）。
 *  @param oldTitle 原标题
 *  @param input    用户输入的标题
 *  @param out      [出参] 结果为 cloud_title_ok 时为去掉首尾空白后的标题
 *  @return 校验结果
 */
CloudTitleResult NormalizeCloudTitle(const std::wstring& oldTitle, const std::wstring& input, std::wstring& out);

/**
 *  云端页一行右侧状态列的矩形：贴着行的右边（让出悬浮滚动条的命中带），宽 width，与行同高。
 *  @param listRect   列表控件的矩形（宿主窗口客户区坐标）
 *  @param rowIndex   行号，从 0 起
 *  @param rowHeight  行高（像素），须大于 0
 *  @param scrollPos  列表当前的滚动位置（像素）
 *  @param width      状态列宽度（像素），须大于 0
 *  @param rightInset 状态列右边与列表右边的距离（像素）
 *  @param out        [出参] 状态列的矩形
 *  @return 这一行与列表可见范围有交集时返回 true；完全滚出或参数非法时返回 false
 */
bool CloudTrailingRect(const RECT& listRect, int rowIndex, int rowHeight, int scrollPos, int width, int rightInset,
                       RECT& out);

} // namespace screenrecord
