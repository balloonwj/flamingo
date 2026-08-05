/**
 *  ChatFileStateModel.h
 *
 *  聊天文件气泡的「状态 → 可用操作」映射（纯逻辑核心，不依赖 GDI / ATL / WTL）。
 *
 *  文件气泡按传输阶段呈现不同面貌：待接收时给「接收 / 拒绝」，传输中给「取消」，
 *  完成后给「打开 / 打开所在文件夹 / 另存为」，失败或被取消后给「重试」。这套映射
 *  原先埋在 ChatBubble.cpp 的匿名命名空间里，与绘制代码绑在一起，改一处状态要同时
 *  照顾按钮数、按钮动作、是否画进度条、气泡高度四处，漏改就会出现「按钮画了但点不到」
 *  或「高度不够按钮被裁掉」之类的错位。
 *
 *  抽成独立纯逻辑后可单元测试（见 flamingoclient/Source/Tests/ChatFileStateModelTests.cpp，
 *  Suite CFSM），把各状态的按钮数与动作序列锁死；ChatBubble 只管照着结果画。
 *  几何常量与气泡高度仍留在 ChatBubble.cpp——那些与绘制强耦合，脱离绘制没有意义。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

namespace balloonchatwnd {

//文件气泡按钮动作类型。定义在本头文件（而非 ChatFileAction.h）：后者的负载结构用了
//CString、要求调用方先引入 ATL/WTL，而本枚举是纯逻辑、单元测试也要用。
enum ChatFileActionKind
{
    kChatFileOpen       = 1,   // 打开文件（用关联程序打开本地文件）
    kChatFileSaveAs     = 2,   // 另存为（把本地文件另存到用户选定位置）
    kChatFileCancel     = 3,   // 取消传输（上传 / 下载进行中时）
    kChatFileRetry      = 4,   // 重新发送 / 重新下载（失败 / 已取消 / 已拒绝后）
    kChatFileOpenFolder = 5,   // 打开所在文件夹（在资源管理器里定位选中本地文件）
    kChatFileAccept     = 6,   // 接收文件（待接收态下点它才真正开始下载）
    kChatFileReject     = 7,   // 拒绝接收（仅本端置气泡为已拒绝态，不下载、不通知发送方）
};

//文件传输状态。取值与 ChatMessage::fileState 一一对应，改动须同步那边的注释。
enum ChatFileState
{
    kFileNormal        = 0,   // 完成 / 已就绪：本地已有完整文件，可打开
    kFileUploading     = 1,   // 上传中：本端正在把文件发往服务器
    kFileFailed        = 2,   // 传输失败：上传或下载中途出错
    kFileDownloading   = 3,   // 下载中：正在从服务器取文件
    kFileCancelled     = 4,   // 已取消：用户在传输途中点了「取消」
    kFileDeleted       = 5,   // 已删除 / 失效：服务端已无此文件，不可再下载
    kFilePendingAccept = 6,   // 待接收：收到文件消息但尚未下载，等用户决定收不收
    kFileRejected      = 7,   // 已拒绝接收：用户点了「拒绝」；仍可反悔重新下载
};

//一个文件气泡底部最多容纳的操作数（完成态的「打开 / 打开所在文件夹 / 另存为」是上限）。
const int kMaxChatFileActions = 3;

//对端把本端发出的文件收到什么程度了。取值与 net 的 enum file_ack_state <u>必须一一对应</u>
//——协议里传的就是这些数字，气泡直接拿来用，改一边就要改另一边。
//与 ChatFileState 是两回事：那个说"本端这一侧传得怎么样"，这个说"对端收得怎么样"。
enum ChatFilePeerState
{
    kFilePeerNone      = 0,   // 对方还没有任何动作（也用于非本端发出的消息）
    kFilePeerReceiving = 1,   // 对方点了「接收」、正在下载
    kFilePeerReceived  = 2,   // 对方已完整收到并校验通过
    kFilePeerRejected  = 3,   // 对方点了「拒绝」
};

/**
 *  该状态下气泡是否要画进度条。
 *  完成 / 已删除 / 待接收 / 已拒绝都没有进度可言（前两者已定局、后两者还没开始传）；
 *  失败与已取消保留进度条，让它停在中断处，用户能看出传到哪儿断的。
 *  @param state 文件传输状态，取值见 ChatFileState
 *  @return 需要画进度条返回 true
 */
bool FileStateHasProgress(int state);

/**
 *  该状态下气泡底部的操作数量。
 *  @param state  文件传输状态，取值见 ChatFileState
 *  @param fromMe 该条是否本端发出。只影响待接收态：对方发来的文件要用户决定收不收，
 *                给「接收 / 拒绝」两个；自己发出的文件没有"拒绝"可言（本来就是自己
 *                要发的），只给一个「下载」把它取回本地
 *  @return 操作数量，范围 0 ~ kMaxChatFileActions；未知状态按 0（不给操作）处理
 */
int FileStateButtonCount(int state, bool fromMe);

/**
 *  取该状态下底部各操作的动作 id，按从左到右的显示顺序写入 out。
 *  @param state  文件传输状态，取值见 ChatFileState
 *  @param fromMe 该条是否本端发出，含义同 FileStateButtonCount
 *  @param out    [出参] 动作 id 数组（取值见 ChatFileActionKind），调用方须提供
 *                至少 kMaxChatFileActions 个元素的空间；仅前若干个被写入
 *  @return 实际写入的动作个数，与 FileStateButtonCount(state, fromMe) 一致
 */
int FileButtonActions(int state, bool fromMe, int out[kMaxChatFileActions]);

} // namespace balloonchatwnd
