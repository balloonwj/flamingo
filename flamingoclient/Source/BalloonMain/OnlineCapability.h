/**
 *  OnlineCapability.h
 *
 *  "当前状态下哪些功能可用"的<u>纯数据判定</u>。客户端有三处需要这套判定，散在各处
 *  各判一次必然会漏（现有的自动重连守卫就是只判了一处、漏了另一处，导致用户选了
 *  离线仍会被重连回去），故集中到本文件：
 *
 *    · UI 门禁 —— 聊天窗输入框 / 发送按钮是否置灰，文件传输、窗口抖动、远程桌面、
 *      加好友等入口是否放行，以及顶部提示条显示什么文字。
 *    · 网络层兜底 —— CIUSocket::EnqueueFrame 在连接未就绪时丢弃业务帧，避免离线
 *      期间把帧攒在发送缓冲里、下次连上后批量补发。
 *    · 托盘表现 —— 按当前状态选托盘图标（离线用灰色图标）、拼托盘悬停提示的状态行。
 *    · 主面板表现 —— 状态胶囊显示的文字与圆点色，以及我自己的头像要不要置灰。
 *      胶囊与头像<u>必须</u>和托盘图标同走本文件，否则就会出现"图标已经灰了、胶囊
 *      还写着在线"这种自相矛盾（掉线时就发生过）。
 *
 *  判定同时看<u>两个</u>输入：用户选择的在线状态（v2 presence_type）与<u>真实连接
 *  是否就绪</u>。两者缺一不可 —— "用户主动选了离线"和"选的是在线但连接还没建好
 *  （正从离线恢复 / 正在断线重连）"是两种不同情形，前者要提示"点此恢复在线"，
 *  后者要提示"正在连接"，但两者都必须拦住需要服务器的操作。
 *
 *  本文件不依赖窗口、不依赖网络对象，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj
 *  两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <windows.h>      // 无直接依赖，但沿用本目录头文件约定（调用方须先引入 windows.h）

#include "MainPanelStatusMenu.h"   // PresenceVisual（状态胶囊的文字 + 圆点色）

namespace balloonmain {

/**
 *  OnlineActionKind —— 需要服务器配合、因而在离线时必须拦住的操作类别。
 *
 *  之所以分类而不是只用一个布尔，是为了让每一类被拦时能给出针对性的提示文字
 *  （"消息未发送"和"无法发起通话"对用户是两件事）。新增类别时必须同时在
 *  GetBlockedActionTip 里配好文案，否则 ACT 测试会失败。
 */
enum OnlineActionKind
{
    kActionSendMessage  = 0,   // 发送聊天消息（单聊 / 群聊的文本、图片、文件消息体）
    kActionTransferFile = 1,   // 文件与图片的上传下载（走 file / img 两条连接）
    kActionShakeWindow  = 2,   // 发送窗口抖动（与聊天消息同一条 cmd_send_message 通道）
    kActionRealtimeCall = 3,   // 发起远程桌面协助、语音 / 视频通话（需信令通道）
    kActionAccountOp    = 4,   // 加好友、建群、改资料等一切需要向服务器发请求的账号类操作

    kActionKindCount    = 5,   // 类别总数，仅供遍历 / 越界判断用，不是有效操作类别
};

/**
 *  ConnectionUiState —— 界面上需要区分的三种连接态。由在线状态与真实连接状态
 *  共同推导（见 ComputeConnectionUiState），聊天窗提示条、托盘图标都据它决定表现。
 */
enum ConnectionUiState
{
    kConnUiOnline     = 0,   // 在线（含离开 / 忙碌）且连接已就绪：不显示提示条，功能全开
    kConnUiConnecting = 1,   // 状态是在线 / 离开 / 忙碌但连接尚未就绪 —— 正从离线恢复，
                             // 或正在断线重连。功能仍需拦住，但提示"正在连接"而非"已离线"
    kConnUiOffline    = 2,   // 用户主动选择了离线：连接已断开、不会自动重连
};

/**
 *  推导界面连接态。
 *
 *    presence： 用户当前选择的在线状态（v2 presence_type）。一切未知取值按
 *               "离线"处理，与 NormalizePresenceForMenu 的兜底口径一致。
 *    connected：与聊天服务器的连接是否已就绪（已连上且登录成功）。
 *
 *  返回：三种连接态之一，见 ConnectionUiState。
 */
ConnectionUiState ComputeConnectionUiState(int presence, bool connected);

/**
 *  判定某类操作在当前状态下是否放行。
 *
 *    presence： 用户当前选择的在线状态（v2 presence_type）。
 *    connected：与聊天服务器的连接是否已就绪。
 *    action：   操作类别，取值见 OnlineActionKind；越界值一律不放行（保守兜底 ——
 *               新增了类别却忘了在这里登记时，宁可误拦也不要漏放出去）。
 *
 *  返回：true 表示可以执行；false 表示必须拦住并给用户提示。
 *
 *  说明：目前五类操作的放行条件相同（都要求在线且连接就绪），保留 action 参数是
 *  为了调用点自解释、以及日后出现"离线也允许"的操作（如纯本地的历史记录搜索）时
 *  不必改动所有调用点的签名。
 */
bool IsActionAllowed(int presence, bool connected, int action);

/**
 *  取某类操作被拦住时给用户看的提示文字。
 *
 *    action：操作类别，取值见 OnlineActionKind；越界值返回一句通用文案。
 *
 *  返回：静态常量串，进程级生命周期，调用方可直接持有。恒不为空。
 */
const wchar_t* GetBlockedActionTip(int action);

/**
 *  取聊天窗顶部提示条应显示的文字。
 *
 *    state：界面连接态，取值见 ConnectionUiState。
 *
 *  返回：提示条文字（静态常量串）；kConnUiOnline 返回 NULL —— 表示此时<u>不显示</u>
 *        提示条。调用方须判空。
 */
const wchar_t* GetConnectionBannerText(int state);

/**
 *  取当前连接态该用的托盘图标文件名（不含目录，调用方自行拼到 Image 目录下）。
 *
 *    state：界面连接态，取值见 ConnectionUiState。
 *
 *  返回：只有真正在线（kConnUiOnline）才返回彩色图标 L"imonline.ico"；用户主动离线
 *        与"连接尚未就绪"都返回灰色图标 L"offline.ico"。静态常量串，进程级生命周期。
 *
 *  之所以按<u>连接态</u>而不是按在线状态取：断线重连期间用户选的仍是"在线"，但此刻
 *  根本连不上服务器，托盘还显示彩色图标会误导人（原有代码在重连时也是显示灰色的）。
 *
 *  说明：只有这两个图标资源。离开 / 忙碌与在线共用同一张彩色底图，它们的区分靠在
 *  底图右下角叠加状态徽标实现，见 GetTrayOverlayPresence。
 */
const wchar_t* GetTrayIconFileName(int state);

//GetTrayOverlayPresence 的返回值：托盘图标不叠加任何徽标。
//取 -1 而不是某个 presence 值，是为了与"要叠加的那个状态"彻底区分开，
//调用方拿到它就该原样使用底图。
const int kNoTrayOverlay = -1;

/**
 *  取托盘图标右下角该叠加哪个在线状态的徽标。
 *
 *    presence：   用户当前选择的在线状态（v2 presence_type）。
 *    connUiState：界面连接态，取值见 ConnectionUiState。
 *
 *  @return presence_away 或 presence_busy 表示叠加对应徽标；kNoTrayOverlay 表示不叠加。
 *
 *  只有"连接就绪 + 离开 / 忙碌"这一种组合才叠加，理由：
 *    · 在线不叠 —— 彩色底图本身就表示在线，再贴个徽标是冗余；
 *    · 离线不叠 —— 用户主动离线时底图已经是灰的（见 GetTrayIconFileName）；
 *    · 连接未就绪（连接中 / 掉线）一律不叠 —— 此刻底图是灰的、消息也收发不了，
 *      灰底图上贴着"离开"徽标会自相矛盾；
 *    · 一切越界取值（含已作废的编号 4）按不叠加处理，与本模块其它函数的保守兜底一致。
 *
 *  本函数无副作用。合成图标的实现见 MainPanelAvatar.h 的 MakePresenceOverlayIcon。
 */
int GetTrayOverlayPresence(int presence, int connUiState);

/**
 *  取当前<u>连接态</u>下状态胶囊与托盘悬停提示该显示的视觉（文字 + 圆点色）。
 *
 *  与只看在线状态的 GetPresenceVisual 的区别，也是本函数存在的理由：后者回答的是
 *  "用户选了什么状态"，掉线时用户并没有改状态，于是它照旧返回"在线"，而托盘图标
 *  这边按连接态已经转灰了 —— 两个口径一分叉，就出现"图标灰了、文字还写着在线"。
 *  凡是要显示给用户看的状态文字，都走本函数。
 *
 *    presence：    用户当前选择的在线状态（v2 presence_type）。仅在连接就绪时用得上，
 *                  用来区分在线 / 离开 / 忙碌三种文字。
 *    connUiState： 界面连接态，取值见 ConnectionUiState（一般由 ComputeConnectionUiState 算得）。
 *
 *  @return 对应的 PresenceVisual；text 指向静态常量串，进程级生命周期，调用方可直接持有。
 *          三种连接态分别给出：
 *            · kConnUiOnline     —— 按 presence 显示"在线 / 离开 / 忙碌"及各自的绿 / 橙 / 红点；
 *            · kConnUiConnecting —— 一律显示"正在连接"配灰点（掉线重连、从离线恢复都算）；
 *            · kConnUiOffline    —— 显示"离线"配灰点。
 *          <u>越界的 connUiState 按"离线"兜底</u>：宁可显示离线，也不要在一个没登记过的
 *          状态下告诉用户"在线"，这样也才和 GetTrayIconFileName / ShouldGrayAvatar 的
 *          兜底方向一致（那两个也是只认 kConnUiOnline）。
 *
 *  本函数无副作用。
 */
PresenceVisual GetConnectionPresenceVisual(int presence, int connUiState);

/**
 *  取当前<u>连接态</u>下状态胶囊该画哪个状态的徽标（形状）。是 GetConnectionPresenceVisual
 *  的形状版：后者回答"显示什么文字、什么颜色"，本函数回答"画成什么形状"。
 *
 *  2026-08-12 状态图标改版后，胶囊上的点不再只有颜色、还带字形（在线实心圆 / 离开时钟 /
 *  忙碌横杠 / 离线空心环），于是"画哪个状态"必须与"用哪个颜色"由同一套连接态规则决定，
 *  否则会出现"灰色的点、却画成在线实心圆"这种自相矛盾。凡是要画胶囊徽标的地方都走本函数。
 *
 *    presence：   用户当前选择的在线状态（v2 presence_type）；仅连接就绪时用得上。
 *    connUiState：界面连接态，取值见 ConnectionUiState。
 *
 *  @return 该画的在线状态（presence_type）：
 *            · kConnUiOnline     —— 用户选的那个状态（先经 NormalizePresenceForMenu 归一）；
 *            · kConnUiConnecting —— presence_offline（空心环，与"正在连接"的灰点同源）；
 *            · kConnUiOffline 及一切越界取值 —— presence_offline。
 *
 *  本函数无副作用。
 */
int GetConnectionBadgePresence(int presence, int connUiState);

/**
 *  判定主面板顶部"我自己"的头像是否该置灰。
 *
 *    connUiState： 界面连接态，取值见 ConnectionUiState。
 *
 *  @return true 表示该画成灰度（收发不了消息）；false 表示画彩色原图。
 *
 *  口径与 GetTrayIconFileName 完全一致 —— 只有真正在线（kConnUiOnline）才是彩色，
 *  连接中、用户主动离线、以及一切越界取值都置灰。两个函数同进同退，"托盘图标是彩色"
 *  与"头像是彩色"才永远等价（OnlineCapabilityTests 里有一条专门断言这件事的用例）。
 *
 *  说明：只管我自己的头像。会话列表、联系人列表里<u>别人</u>的头像反映的是别人的在线
 *  状态，与我这边掉没掉线是两回事，不走本函数。
 *
 *  本函数无副作用。
 */
bool ShouldGrayAvatar(int connUiState);

/**
 *  判定掉线后是否应当启动 / 继续自动重连。
 *
 *  之所以做成纯函数，是因为这个判断在两个地方各需要一次：掉线事件到达时决定要不要
 *  起重连定时器，以及重连定时器每次触发时决定要不要真的去登录。<u>原先只有前者判了
 *  用户是否主动离线，后者没判</u>，导致"正在断线重连时用户选离线"这条路上客户端会
 *  自己又连回去。两处共用本函数即可杜绝这类遗漏。
 *
 *    userWentOffline： 用户是否主动选择了离线（对应 MAINPANEL_STATUS_USERGOOFFLINE）。
 *    enableReconnect： 配置里是否允许自动重连。
 *
 *  返回：true 表示应当重连。用户主动离线时恒为 false —— 主动离线的语义就是"别再连了"。
 */
bool ShouldAutoReconnect(bool userWentOffline, bool enableReconnect);

} // namespace balloonmain
