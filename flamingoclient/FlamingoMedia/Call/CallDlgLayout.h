/**
 *  通话对话框的文字与画面去向纯逻辑，CallDlgLayout.h
 *
 *  把音视频通话对话框（CBalloonCallDlg）里三处"只依赖当前形态 + 对端昵称 + 是否视频通话"
 *  的判定抽成不依赖 HWND / 控件的纯函数，便于 FlamingoUnit 测试工程直接断言：
 *    · MakeCallWindowTitle —— 标题栏文字。通话中要写明"在跟谁通话"（如"与 张勇 视频通话中"），
 *      呼叫中 / 来电态还没接通，只标明通话类型。
 *    · MakeCallPromptText  —— 客户区提示文字（呼叫中 / 来电 / 通话中三种措辞；来电态区分
 *      "邀请你视频通话" / "邀请你语音通话"）。
 *    · PickCanvasForFrame  —— 视频通话时一路画面（本端采集 / 远端解码）该画到大画面控件还是
 *      小画面（画中画）控件上。用户可以把自己和对方对调显示，故去向随"本端是否占大画面"翻转。
 *
 *  带窗口副作用的部分（建控件树、SetTitle、窗口置顶、重绘）仍留在 CBalloonCallDlg 里。
 *
 *  典型用法：
 *      SetTitle(call::MakeCallWindowTitle(m_mode, m_peerName, m_videoCall));
 *      m_msgLabel->SetText(call::MakeCallPromptText(m_mode, m_peerName, m_videoCall));
 *      if (call::PickCanvasForFrame(m_localIsBig, true) == call::kCallCanvasBig) ...
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <string>            //std::string（会话 id）
#include <atlstr.h>          //CString（与 FlamingoUnit 测试工程共用，故不 include stdafx.h）
#include "MfCameraCapture.h" //CameraFailReason（摄像头失败原因，只用其枚举；该头仅依赖 stdint）

namespace call {

//通话对话框形态。取值与 CBalloonCallDlg::Mode 一一对应，抽到本模块是为了让纯函数不依赖对话框类。
enum CallDlgMode
{
    kCallModeCalling   = 0,   //主叫：已发出呼叫，正在等待对方接听
    kCallModeIncoming  = 1,   //被叫：收到来电，等待本人点接听 / 拒绝
    kCallModeConnected = 2,   //通话中：双方已接通，媒体通道已建立
};

//视频通话时一路画面的去向：窗口上半部铺满的大画面，还是底部栏左下角的小画面（画中画）。
enum CallCanvasTarget
{
    kCallCanvasBig = 0,   //大画面：视频通话窗上半部铺满的那块
    kCallCanvasPip = 1,   //小画面：底部栏左下角的画中画小窗
};

/**
 *  判定一路视频帧该画到大画面还是小画面控件上。
 *
 *  规则：谁占大画面，谁的帧就进大画面，另一路进小画面。默认对方占大画面（localIsBig=false），
 *  用户点小窗对调后本端占大画面（localIsBig=true）。
 *
 *  @param localIsBig   当前是否本端（自己的摄像头画面）占大画面；false=对方占大画面
 *  @param frameIsLocal 本帧是否本端采集帧；false=远端解码帧
 *  @return 本帧应画到的画面控件（kCallCanvasBig / kCallCanvasPip）
 */
CallCanvasTarget PickCanvasForFrame(bool localIsBig, bool frameIsLocal);

/**
 *  组装通话窗标题栏文字。
 *
 *  通话中（mode=kCallModeConnected）且拿得到对端昵称时写成"与 XXX 视频通话中" /
 *  "与 XXX 语音通话中"，让用户一眼看出在跟谁通话；呼叫中 / 来电态，或昵称为空时，
 *  退回"视频通话" / "语音通话"。
 *
 *  @param mode      当前对话框形态，取 CallDlgMode 之一
 *  @param peerName  对端昵称；空串表示未取到
 *  @param videoCall 本次通话是否含视频；true=视频通话，false=纯语音
 *  @return 标题栏文字（非空）
 */
CString MakeCallWindowTitle(int mode, const CString& peerName, bool videoCall);

/**
 *  组装客户区提示文字（纯语音布局与呼叫 / 来电对话框用；视频通话中态没有这行文字）。
 *
 *  @param mode      当前对话框形态，取 CallDlgMode 之一
 *  @param peerName  对端昵称；空串时用"对方"兜底，避免出现"正在呼叫 ，等待…"这种空档
 *  @param videoCall 本次通话是否含视频：来电态据此区分"邀请你视频通话" / "邀请你语音通话"
 *  @return 提示文字（非空）
 */
CString MakeCallPromptText(int mode, const CString& peerName, bool videoCall);

/**
 *  组装"本端摄像头不可用"的提示文字，显示在视频通话窗里本端那一路画面上（代替一块纯黑）。
 *  分两句：第一句说清是什么毛病（没检测到设备 / 打不开 / 中途断了），第二句说清后果
 *  （对方看不到你），中间用 '\n' 分行。
 *
 *  @param reason 失败原因，取 call::CameraFailReason；未知取值按"摄像头不可用"兜底
 *  @return 提示文字（非空，含一个换行）
 */
CString MakeCameraUnavailableTip(int reason);

/**
 *  通话中中转意外断开后，等待服务端状态推送的时长（毫秒）。正常挂断时推送通常先于或紧随中转断开到达，
 *  期间收到推送就按推送收尾（显示「通话结束 mm:ss」）；超时仍未收到，说明对方掉线或退出了程序，由本端自行结束。
 */
const unsigned int kRelayLossGraceMs = 5000;

/**
 *  判定中转意外断开时是否要在等待 kRelayLossGraceMs 后由本端自行结束通话。
 *  @param mode      当前对话框形态，取 CallDlgMode 之一
 *  @param sessionId 本次通话的会话 id；自行结束时要用它向服务端发挂断信令
 *  @return 只有通话中态、且会话 id 非空时返回 true
 */
bool ShouldEndCallAfterRelayLoss(int mode, const std::string& sessionId);

/**
 *  组装本端因中转中断而自行结束通话时写进聊天窗的提示。
 *  @param durationText 已通话时长（如「05:23」）；为空时不带时长
 *  @return 「通话已中断」或「通话已中断 05:23」
 */
CString MakeCallInterruptedTip(const CString& durationText);

//用户主动关闭通话窗（点「取消」「拒绝」「挂断」按钮或标题栏关闭按钮）时要发给服务端的信令
enum CallUserCloseSignal
{
    kCallCloseSignalNone   = 0,   //会话 id 还没拿到（呼叫刚发出、服务端尚未应答），没有可发的信令
    kCallCloseSignalCancel = 1,   //呼叫中：主叫在对方接听之前取消
    kCallCloseSignalReject = 2,   //来电中：被叫拒绝
    kCallCloseSignalHangup = 3,   //通话中：挂断
};

/**
 *  判定用户主动关闭通话窗时要发的信令。三个按钮与标题栏关闭按钮共用这一个判定，并统一经
 *  CBalloonCallDlg::CloseByUser 收尾（请求通话控制器停媒体会话）。2026-10-04 界面联调查出：「挂断」按钮
 *  原先只发信令、不请求停媒体会话，而服务端只把结束推送发给对方，挂断的一方采集线程（视频通话时连同摄像头）
 *  一直运行到下一次通话开始。
 *  @param mode      当前对话框形态，取 CallDlgMode 之一；未知取值按通话中处理
 *  @param sessionId 本次通话的会话 id；为空时不发信令
 *  @return 取 CallUserCloseSignal 之一
 */
int PickUserCloseSignal(int mode, const std::string& sessionId);

/**
 *  算出窗口放在工作区正中时的左上角坐标（第 10 期 10a：来电在右下角接听后，通话窗移到该显示器工作区正中）。
 *  @param work 显示器工作区（屏幕坐标，已排除任务栏）
 *  @param size 窗口尺寸
 *  @return 左上角屏幕坐标；窗口比工作区大时贴工作区的左 / 上边，保证标题栏可见
 */
POINT ComputeCenteredOrigin(const RECT& work, const SIZE& size);

//语音态画面区的图标圆底直径（像素）：区域够大时取此值（第 10 期 10b）
const int kVoiceArtCircle = 96;
//图标圆底与下方文字之间的间距（像素）
const int kVoiceArtGap = 12;
//「语音通话中」文字的行高（像素）
const int kVoiceArtTextH = 24;
//文字下面通话时长那一行的行高（像素，第 11 期 11b）
const int kVoiceArtSubTextH = 20;

//语音态画面区里图标与文字的位置（第 10 期 10b）
struct VoiceArtLayout
{
    RECT m_circle;   //主题蓝圆底的外接矩形（正方形），听筒画在它里面
    RECT m_text;     //「语音通话中」文字的矩形（与区域同宽，文字水平居中）
    RECT m_subText;  //通话时长的矩形（紧接在文字下面，与区域同宽，第 11 期 11b）
};

/**
 *  计算语音态画面区的图标、文字与时长位置：三者作为一组在区域里居中；区域高度放不下 96 的圆底时按剩余高度缩小。
 *  @param area 画面区矩形
 *  @return 圆底与文字的矩形；区域太小时圆底可能为 0 宽高
 */
VoiceArtLayout ComputeVoiceArtLayout(const RECT& area);

//视频态没有画面时，头像与下方提示文字之间的间距（像素）：比语音态的 kVoiceArtGap 小，给小画面里的头像多留高度
const int kAvatarTipGap = 6;

/**
 *  视频态画面区没有画面时「头像在上、一行提示在下」的位置（docs/音视频通话优化方案.md 第 5 项）。与 ComputeVoiceArtLayout
 *  的区别是不留通话时长那一行、间距更小，左下角 160×90 的小画面里头像也有足够的大小。头像与提示作为一组在区域里居中。
 *  @param area 画面区矩形
 *  @return m_circle 为头像的外接正方形（区域太小时为 0 宽高），m_text 为提示文字的矩形，m_subText 为空矩形（不使用）
 */
VoiceArtLayout ComputeAvatarTipLayout(const RECT& area);

//对方画面多久没有新帧就当作对方关了摄像头（毫秒）：一对一通话没有「对方关了摄像头」的通知，发送端 15 帧每秒，
//连续 2 秒一帧都没有，基本可以断定摄像头已关（网络卡住时同样先显示头像，来了新帧立刻恢复画面）
const int64_t kRemoteVideoStaleMs = 2000;

/**
 *  对方画面是否已经停了（docs/音视频通话优化方案.md 第 5 项：对方没开摄像头时显示对方头像）。
 *  @param nowMs       当前时刻（毫秒，与 lastFrameMs 同一时钟，如 GetTickCount64）
 *  @param lastFrameMs 最近一次收到对方画面的时刻；0 表示还没收到过
 *  @param staleMs     判定的时长（毫秒），正常传 kRemoteVideoStaleMs
 *  @return 收到过画面、且已超过 staleMs 没有新帧时返回 true；还没收到过画面时返回 false（那时本来就显示头像）
 */
bool IsRemoteVideoStale(int64_t nowMs, int64_t lastFrameMs, int64_t staleMs);

/**
 *  来电弹窗第二行的文字（第一行是对方的名字，docs/音视频通话优化方案.md 第 1 项）。
 *  @param videoCall 是否视频通话
 *  @return 「邀请你视频通话」或「邀请你语音通话」（已按当前界面语言翻译）
 */
CString MakeIncomingSubText(bool videoCall);

/**
 *  呼叫中态画面区头像下面那一行的文字（呼叫中态与接通后同一个窗口形态，见 CallingLayout.h）。
 *  @return 「正在呼叫，等待对方接听…」（已按当前界面语言翻译）
 */
CString MakeOutgoingStatusText();

} // namespace call
