/**
 *  云端录像的离线合成，CloudRecordComposer.h
 *
 *  第 27a 期（docs/屏幕共享与录制方案.md 12.6.3，第 49、49a 条）：把 imgserver 录下的原始帧文件（.frec）合成为一个标准 MP4
 *  （H.264 + AAC），画面与观看窗的默认布局相同：并列显示所有共享（1 块铺满、2 块左右、3～4 块田字格），每块左上角标共享人名字，
 *  标注画在对应画面上，说话中的人的摄像头作为画中画放在右下角，各路声音混在一起。
 *  合成在本类自己的工作线程上进行，不按墙钟等待：
 *    1. 第一遍只读座位记录，收集参与者的 userid，经消息请 UI 线程查名字（宿主接口只在 UI 线程上调用），等名字回来；
 *    2. 第二遍逐条处理：屏幕画面按座位解码（ShareScreenDecoders），摄像头每座位一个 H.264 解码器，声音每路一个 Opus 解码器后
 *       按发送端时间戳叠加，标注交给标注模型；每到一个输出帧的时刻画一帧、每 20 毫秒写一段声音（RecordMp4Writer）。
 *  解码与编码都在本线程上（COM 多线程套间 + Media Foundation），画面用 GDI 内存设备上下文绘制，不需要窗口。
 *  纯逻辑部分在 CloudRecordCompose.h，单元测试见 Source/Tests/CloudRecordClientTests.cpp。
 *
 *  典型用法（CCloudRecordCenter 里）：
 *      task.reset(new CCloudRecordComposeTask(recordId, frecPath, mp4Path, hWnd, kMsgNames, kMsgDone));
 *      task->Start();
 *      ...收到 kMsgNames：task->SetNames(names)；收到 kMsgDone（wParam 为任务指针，lParam 为结果）：task->Join()
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <map>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace screenrecord {

//合成的结果（kMsgComposeDone 的 lParam）
enum CloudComposeResult
{
    kCloudComposeOk        = 0,    //合成完成
    kCloudComposeCancelled = -1,   //被取消（退出登录、退出程序）
    kCloudComposeBadFile   = 1,    //原始帧文件打不开或格式不对
    kCloudComposeWriteFail = 2,    //MP4 写入失败（编码器、磁盘）
    kCloudComposeMfFail    = 3,    //Media Foundation 初始化失败
};

//一次合成各阶段的累计耗时（2026-10-07，docs/屏幕共享与录制方案.md 第 14.9 节：实测长录像的合成慢于实时，用来找出时间花在
//哪一步）。合成结束时写进日志；计时用例在 Join 之后经 StageTimes 取得
struct CloudComposeStageTimes
{
    double  m_decodeMs;       //屏幕画面解码（ShareScreenDecoders::OnScreenFrame），毫秒
    double  m_drawMs;         //每个输出帧的绘制：取各块最新画面、缩放贴进画布、标注、名字标签、补不透明度，毫秒
    double  m_composeMs;      //画中画合成（RecordFrameComposer::Compose），毫秒
    double  m_videoWriteMs;   //视频写入：颜色转换与 H.264 编码（RecordMp4Writer::WriteVideo），毫秒
    double  m_audioMs;        //声音：Opus 解码、混音与写入，毫秒
    int64_t m_videoFrames;    //写出的视频帧数
    int64_t m_reusedFrames;   //画布沿用上一帧、没有重画的帧数（2026-10-08）
    int64_t m_directFrames;   //没有画中画、画布直接交给编码器的帧数（2026-10-08）

    CloudComposeStageTimes();
};

//一次离线合成
class CCloudRecordComposeTask
{
public:
    /**
     *  @param recordId  录像 id（只用于日志与回报）
     *  @param frecPath  原始帧文件
     *  @param mp4Path   输出 MP4（已存在时覆盖）
     *  @param notifyWnd 回报窗口（不拥有）
     *  @param msgNames  请 UI 线程查名字的消息号：lParam 为本对象指针
     *  @param msgDone   合成结束的消息号：wParam 为本对象指针，lParam 为结果（CloudComposeResult）
     *  @param labelFont 名字标签的字体（UI 线程上取 i18n::UiFontName()，随界面语言变化）
     */
    CCloudRecordComposeTask(int64_t recordId, const std::wstring& frecPath, const std::wstring& mp4Path, HWND notifyWnd,
                            UINT msgNames, UINT msgDone, const std::wstring& labelFont);
    ~CCloudRecordComposeTask();

    CCloudRecordComposeTask(const CCloudRecordComposeTask&) = delete;
    CCloudRecordComposeTask& operator=(const CCloudRecordComposeTask&) = delete;

    //开始合成（开工作线程）
    void Start();

    //要求取消（在处理记录的间隙生效）；正在等名字时立即醒来
    void Cancel();

    //等工作线程结束；可重复调用
    void Join();

    int64_t RecordId() const;

    //工作线程收集到的参与者 userid（发 msgNames 之前填好，之后只读）
    const std::vector<int64_t>& UserIds() const;

    /**
     *  UI 线程填好名字后调用，唤醒工作线程。
     *  @param names userid → 显示名；查不到的不在其中（画面上不标名字）
     */
    void SetNames(const std::map<int64_t, std::wstring>& names);

    //进度：0～100，按读过的字节估算
    int Progress() const;

    //各阶段的累计耗时（2026-10-07）；只在 Join 之后读取，合成线程运行期间读到的值不完整
    CloudComposeStageTimes StageTimes() const;

private:
    void threadProc();
    int run();
    //等 UI 线程的名字；被取消时返回 false
    bool waitNames();

    int64_t                          m_recordId;    //录像 id
    std::wstring                     m_frecPath;    //原始帧文件
    std::wstring                     m_mp4Path;     //输出 MP4
    HWND                             m_notifyWnd;   //回报窗口（不拥有）
    UINT                             m_msgNames;    //查名字的消息号
    UINT                             m_msgDone;     //结束的消息号
    std::wstring                     m_labelFont;   //名字标签的字体
    std::thread                      m_thread;      //工作线程
    std::atomic<bool>                m_cancel;      //取消标志
    std::atomic<int>                 m_progress;    //进度 0～100
    std::vector<int64_t>             m_userIds;     //参与者（工作线程写、发消息之后只读）
    std::mutex                       m_mutex;       //保护下面三项
    std::condition_variable          m_cv;          //名字到了或被取消时通知
    bool                             m_namesReady;  //名字是否已到
    std::map<int64_t, std::wstring>  m_names;       //userid → 显示名
    CloudComposeStageTimes           m_stageTimes;  //各阶段的累计耗时，合成线程写、Join 之后读
};

} // namespace screenrecord
