/**
 *  屏幕录制的参数与规则，RecordPolicy.h
 *
 *  录制管线（docs/屏幕共享与录制方案.md 第 6 节）里不依赖窗口与系统对象的部分，全部是纯函数，便于单元测试：
 *    - 输出尺寸：长边不超过 1920（2026-10-07 起「高清」不超过 2560）、宽高取偶数（H.264 要求）；帧率不超过 30；
 *      两档画质对应的码率（按输出像素数换算，「高清」约为「标准」的 2 倍）；
 *    - 文件名：「FlamingoRecord_年月日_时分秒.mp4」，重名时追加序号；默认目录为「视频」文件夹下的 FlamingoRecords；
 *    - 摄像头画中画的位置：画面右下角、宽度为画面的五分之一、16:9；
 *    - 磁盘空间：开始录制要求剩余空间不少于 kRecordMinFreeToStart，录制中低于 kRecordMinFreeToContinue 自动停止；
 *    - 录制完成提示（第 14 期）：从路径取出文件名，时长与大小写成一行。
 *  单元测试见 Source/Tests/RecordPipelineTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <windows.h>

namespace screenrecord {

//画质
enum RecordQuality
{
    record_quality_standard = 0,   //标准：码率较低，文件小
    record_quality_high     = 1,   //高清：码率较高
};

//输出画面长边的上限（像素）：「标准」画质
const int kRecordMaxLongSide = 1920;
//「高清」画质的长边上限（像素，2026-10-07）：2K 屏不再缩小，4K 屏缩到长边 2560；码率按像素数随之提高
const int kRecordHighMaxLongSide = 2560;
//输出画面的最小边长（像素）：再小编码器不接受
const int kRecordMinSide = 64;
//帧率的上限与默认值（帧每秒）
const int kRecordMaxFps = 30;
const int kRecordDefaultFps = 30;
//1080p 下两档画质的码率（bit/s）；其它尺寸按像素数等比换算
const uint32_t kRecordStandardBitrate1080p = 4000000;
const uint32_t kRecordHighBitrate1080p = 8000000;
//码率的下限（bit/s）
const uint32_t kRecordMinBitrate = 500000;
//声音：48 kHz、双声道（单声道混音后复制到两个声道）、AAC 128 kbps
const int kRecordAudioRate = 48000;
const int kRecordAudioChannels = 2;
const uint32_t kRecordAacBytesPerSecond = 16000;
//开始录制要求的最小剩余空间、录制中低于这个值自动停止（字节）
const uint64_t kRecordMinFreeToStart = 500ULL * 1024 * 1024;
const uint64_t kRecordMinFreeToContinue = 200ULL * 1024 * 1024;
//画中画：宽度占画面宽度的比例（分母）、距画面边缘的距离占画面宽度的比例（分母）
const int kRecordPipWidthDivisor = 5;
const int kRecordPipMarginDivisor = 60;

/**
 *  按源画面尺寸算输出尺寸：长边超过 kRecordMaxLongSide 时等比缩小，宽高向下取偶数，且不小于 kRecordMinSide。
 *  @param srcW、srcH 源画面尺寸（像素）
 *  @param outW、outH [出参] 输出尺寸
 */
void ComputeRecordSize(int srcW, int srcH, int& outW, int& outH);

/**
 *  某档画质的输出长边上限（2026-10-07）：「标准」为 kRecordMaxLongSide（1920），「高清」为 kRecordHighMaxLongSide（2560）。
 *  @param quality 取值见 RecordQuality；未知取值按「标准」
 */
int RecordMaxLongSide(int quality);

/**
 *  按画质算输出尺寸（2026-10-07）：长边超过这档画质的上限（RecordMaxLongSide）时等比缩小，其余同上一个重载。
 *  @param srcW、srcH 源画面尺寸（像素）
 *  @param quality    取值见 RecordQuality
 *  @param outW、outH [出参] 输出尺寸
 */
void ComputeRecordSize(int srcW, int srcH, int quality, int& outW, int& outH);

//把帧率夹到 [1, kRecordMaxFps]
int ClampRecordFps(int fps);

/**
 *  视频码率：按画质取 1080p 的基准码率，再按输出像素数与 1920×1080 的比例换算，不低于 kRecordMinBitrate。
 *  @param quality 取值见 RecordQuality
 */
uint32_t RecordVideoBitrate(int quality, int width, int height);

/**
 *  录制文件名（不含目录）：「FlamingoRecord_YYYYMMDD_HHMMSS.mp4」；seq 大于 0 时在扩展名前加「_seq」。
 *  第 13 期由「Flamingo录屏_」改为英文，发到会话或拷到别的电脑时不会因编码出现乱码。
 *  @param t         本地时间
 *  @param seq       重名时的序号，0 表示不加
 *  @param audioOnly 只录声音（第 24 期）：扩展名为 .m4a
 */
std::wstring MakeRecordFileName(const SYSTEMTIME& t, int seq, bool audioOnly = false);

//只录声音的文件扩展名（第 24 期）
const wchar_t* const kRecordAudioExt = L".m4a";

/**
 *  是不是只录声音的录音文件（第 24 期）：按扩展名 .m4a 判断，不区分大小写。录制管理据此画录音的图标。
 *  @param path 文件路径或文件名
 */
bool IsAudioRecordingPath(const std::wstring& path);

//录制条电平指示的下限（dB）：峰值低于它时电平为 0
const int kRecordLevelFloorDb = -60;

/**
 *  录制条的电平（第 24 期）：16 位采样的峰值换算成 0~100。按分贝线性，kRecordLevelFloorDb 及以下为 0，满幅为 100。
 *  @param peak 峰值（采样绝对值的最大值；负数按绝对值）
 */
int RecordLevelPercent(int peak);

//默认录像目录在「视频」文件夹下的子目录名（第 13 期由「Flamingo录屏」改为英文）
const wchar_t* const kRecordDefaultSubDir = L"FlamingoRecords";

/**
 *  默认录像目录：「视频」文件夹下的 FlamingoRecords。
 *  @param videosDir 「视频」文件夹的路径（SHGetKnownFolderPath(FOLDERID_Videos) 的结果），末尾带不带反斜杠都可以
 *  @return 默认录像目录；videosDir 为空时返回空串
 */
std::wstring MakeDefaultRecordFolder(const std::wstring& videosDir);

/**
 *  画中画的矩形：贴画面右下角（留出边距），宽为画面宽的 1/kRecordPipWidthDivisor，16:9，宽高取偶数。
 *  @param frameW、frameH 输出画面尺寸
 */
RECT ComputePipRect(int frameW, int frameH);

//画中画在录像里的摆放（2026-10-05：录制中屏幕上有一个可拖动的摄像头小窗，录像里的画中画跟着它走）。
//坐标与尺寸都是相对画面来源所占矩形的比例（0~1）；m_custom 为 false 时按 ComputePipRect 摆在右下角
struct RecordPipPlacement
{
    bool   m_custom;   //是否按下面四个比例摆放
    double m_x;        //左边距占画面来源宽度的比例
    double m_y;        //上边距占画面来源高度的比例
    double m_w;        //宽占画面来源宽度的比例
    double m_h;        //高占画面来源高度的比例

    RecordPipPlacement();
};

/**
 *  把屏幕上摄像头小窗的位置换算成画中画在录像里的摆放：按小窗相对被录内容的位置与大小。小窗有一部分或全部在被录内容
 *  以外时贴到最近的边上；比被录内容还宽或还高时缩到与之同宽或同高。
 *  @param camScreen 摄像头小窗的屏幕矩形
 *  @param recorded  被录内容的屏幕矩形（显示器、窗口或框选区域）
 *  @return 摆放；被录内容或小窗为空矩形时 m_custom 为 false
 */
RecordPipPlacement MapPipWindowToContent(const RECT& camScreen, const RECT& recorded);

/**
 *  摄像头小窗的默认屏幕矩形：与 ComputePipRect 在录像里的位置一致，即被录内容右下角、宽为被录内容宽的
 *  1/kRecordPipWidthDivisor、16:9。
 *  @param recorded 被录内容的屏幕矩形
 *  @return 小窗的屏幕矩形；recorded 为空时返回空矩形
 */
RECT DefaultPipWindowRect(const RECT& recorded);

/**
 *  画中画在输出画面里的矩形（合成器用）：按摆放比例换算到画面来源所占的矩形 content 里；不是自定义摆放时同 ComputePipRect。
 *  @param content   画面来源在输出画面里所占的矩形（RecordFrameComposer::ContentRect）
 *  @param placement 摆放
 *  @param frameW、frameH 输出画面尺寸
 */
RECT PlacePipRect(const RECT& content, const RecordPipPlacement& placement, int frameW, int frameH);

//剩余空间是否够开始录制
bool RecordCanStart(uint64_t freeBytes);

//剩余空间是否够继续录制（不够时自动停止并保存）
bool RecordCanContinue(uint64_t freeBytes);

/**
 *  框选区域：按下点与当前点围成的矩形（与拖动方向无关），限定在 bounds 以内。
 *  @param a、b   按下点与当前点（屏幕坐标）
 *  @param bounds 允许的范围（显示器矩形）
 *  @return 规整后的矩形；宽或高为 0 时为空矩形
 */
RECT NormalizeSelection(POINT a, POINT b, const RECT& bounds);

//框选的区域是否够大（宽高都不小于 kRecordMinSide）
bool IsSelectionUsable(const RECT& rc);

/**
 *  录制时长的文字：不满 1 小时为「mm:ss」，否则为「h:mm:ss」。
 *  @param ms 时长（毫秒）
 */
std::wstring FormatRecordDuration(int64_t ms);

/**
 *  录制完成提示里显示的文件名（第 14 期）：取路径里最后一个「\」或「/」之后的部分。
 *  @param path 录像文件的完整路径
 *  @return 文件名；path 里没有目录时原样返回；以分隔符结尾时为空
 */
std::wstring RecordFileNameOf(const std::wstring& path);

/**
 *  录制完成提示的时长与大小一行（第 14 期），如「时长 01:05 · 3.2 MB」。大小按 1 MB = 1024 × 1024 字节、保留一位小数。
 *  @param durationMs 录制时长（毫秒，不含暂停）
 *  @param bytes      文件大小（字节）
 */
std::wstring FormatRecordDoneDetail(int64_t durationMs, uint64_t bytes);

} // namespace screenrecord
