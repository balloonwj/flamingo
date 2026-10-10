/**
 *  聊天窗投给主面板的转发请求，以及合并转发标题的生成规则。纯逻辑，不依赖 ATL 与界面，
 *  可被 FlamingoUnit 直接测试。
 *
 *  典型用法：聊天窗收到消息列表的 WM_CHAT_FORWARD_REQUEST 后，填好一份 ForwardRequest，
 *  以 FMG_MSG_OPEN_FORWARD_PICKER 投给主面板；主面板用 BuildMergedForwardTitle 生成合并
 *  转发的标题（单聊用对方真名，不用含备注的显示名），打开目标选择窗，用户选定目标后由
 *  转发调度器逐个目标发出 cmd_forward_message。流程见 docs/聊天消息转发方案.md §3、§5。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace balloonmain {

//合并转发标题的长度上限，单位为 UTF-16 字符。服务端按 64 个字符截断（chatserversrc/
//MessageProtocol.h 的 FORWARD_TITLE_MAX_CHARS），客户端先按同一数目截断，目标选择窗里
//预览的标题就是对方最终看到的标题。
const size_t kForwardTitleMaxChars = 64;

/**
 *  聊天窗或消息记录窗投给主面板的转发请求（FMG_MSG_OPEN_FORWARD_PICKER 的 lParam）。
 *  由发起转发的窗口在堆上创建，主面板处理后 delete。消息记录窗只转发右键的那一条，
 *  按逐条转发发出（m_merge 为 false）。
 */
//ForwardRequest::m_callMedia 的取值：主菜单发起通话时的媒体类型（0 表示不是发起通话）
const int kForwardCallVoice = 1;   //语音通话
const int kForwardCallVideo = 2;   //视频通话

struct ForwardRequest
{
    int                  m_srcCtype;   //源会话类型：1 单聊、2 群聊
    int64_t              m_srcCid;     //源会话 id：单聊为对方 userid，群聊为群 id
    //发起转发的聊天窗或消息记录窗，转发结束后在它上面提示结果。用户可能早已关掉它，使用前须 IsWindow
    HWND                 m_srcWnd;
    bool                 m_merge;      //true 合并转发为一条聊天记录，false 逐条转发
    //要转发的服务器 msgid，按 msgid 升序、去重。逐条转发选中的只有在线直传文件时为空（此时 m_reuploadFiles 不为空）
    std::vector<int64_t> m_msgids;
    size_t               m_skipped;    //选中了但不能转发的行数，用于提示「已跳过 N 条」
    //合并转发的标题；逐条转发时为空。聊天窗投递时不填，由主面板用真名（不含备注）与群名生成
    std::wstring         m_title;
    //不为空时这一次不是转发消息，而是把这个本地文件发到选中的会话（录屏完成后的「发送到会话…」）；此时 m_msgids 为空
    std::wstring         m_filePath;
    //逐条转发时选中的在线直传文件里本机还有文件的那些：不按消息转发（服务器上没有文件），改为把本机文件
    //以离线文件发给每个目标（2026-10-05 起，见 DecideDirectFileForward）。合并转发时恒为空
    std::vector<std::wstring> m_reuploadFiles;
    //逐条转发时选中的在线直传文件里本机已没有文件、因而跳过的条数，不计入 m_skipped，单独提示「在线直传的文件无法转发」
    size_t               m_skippedDirect;
    //合并转发时进入记录的在线直传文件条数：文件本身不随记录发出，对方在记录里看到说明文字。逐条转发时恒为 0
    size_t               m_directPlaceholders;
    //不为 0 时这一次不是转发，而是主菜单「发起语音 / 视频通话」选通话对象（docs/音视频通话优化方案.md 第 4 项）：
    //1 语音、2 视频。此时只能选一个目标，m_msgids 与 m_filePath 都为空
    int                  m_callMedia;
    //为 true 时这一次不是转发，而是主菜单「发起屏幕共享」选共享给谁（docs/屏幕共享与录制方案.md 第 2 节第 42 条）：
    //只能选一个目标，选好后打开该会话的聊天窗并弹出共享内容选择窗；m_msgids 与 m_filePath 都为空
    bool                 m_screenShare;
    //为 true 时这一次不是转发，而是 Markdown 编辑器选发送对象（docs/Markdown消息方案.md 第 11.3 节）：可以多选，按「确定」后
    //把选定的对象交回编辑器，由编辑器的「发送」发出；m_title 为文档标题，m_markdownDigest 为摘要，m_msgids 与 m_filePath 都为空
    bool                 m_markdownPick;
    //Markdown 编辑器选发送对象时显示在预览区的文档摘要（纯文字）；其余用途为空
    std::wstring         m_markdownDigest;
    //不为空时这一次不是转发，而是把这段文字以普通文字消息发到选中的会话（屏幕共享第 25 期：主持人「发送邀请到…」
    //发分享链接，docs/屏幕共享与录制方案.md 第 12.4.5 节）；可以多选，此时 m_msgids 与 m_filePath 都为空
    std::wstring         m_text;
    //为 true 时这一次不是转发，而是 FlamingoMedia 请用户选会话（云端录制第 27 期：把云端录像发到会话）：可以多选，按「确定」后
    //经 IMediaModule::OnConversationsPicked 把选定的会话交回 DLL；m_msgids、m_filePath、m_text 都为空
    bool                 m_mediaPick;
    //FlamingoMedia 发起选择时传入的标识，原样交回
    uint32_t             m_mediaPickToken;
    //FlamingoMedia 给的选择窗标题，也作为预览区的文字；为空时用默认标题
    std::wstring         m_mediaPickTitle;

    ForwardRequest()
        : m_srcCtype(0)
        , m_srcCid(0)
        , m_srcWnd(NULL)
        , m_merge(false)
        , m_skipped(0)
        , m_skippedDirect(0)
        , m_directPlaceholders(0)
        , m_callMedia(0)
        , m_screenShare(false)
        , m_markdownPick(false)
        , m_mediaPick(false)
        , m_mediaPickToken(0)
    {
    }
};

//转发时一条在线直传文件消息怎么处理（2026-10-05 起，docs/单聊文件P2P直传与P2P徽章方案.md 第 7 节）。
//在线直传的文件服务器上没有副本，按消息原样转发出去，对方拿到的只是一条打不开的记录
enum DirectFileForwardAction
{
    kDirectForwardPlaceholder = 0,   //合并转发：这条照常进入合并记录，对方在记录里看到说明文字，文件本身不发
    kDirectForwardReupload    = 1,   //逐条转发且本机还有文件：不转发这条消息，改为把本机文件以离线文件发给每个目标
    kDirectForwardSkip        = 2,   //逐条转发且本机已没有文件：跳过，提示「在线直传的文件无法转发」
};

/**
 *  决定转发时一条在线直传文件消息怎么处理（用户 2026-10-05 确认的规则）。
 *  @param merge        true 合并转发，false 逐条转发
 *  @param hasLocalFile 本机是否还找得到这份文件（存在且大小相符）
 *  @return 取值见 DirectFileForwardAction：合并转发一律 kDirectForwardPlaceholder；逐条转发本机有文件时
 *          kDirectForwardReupload，没有时 kDirectForwardSkip。本函数无副作用
 */
int DecideDirectFileForward(bool merge, bool hasLocalFile);

/**
 *  生成合并转发的标题。
 *
 *  规则：
 *    · 单聊：「<本人名字>和<对方名字>的聊天记录」；只有一方有名字时为「<该名字>的聊天记录」；
 *    · 群聊：「<群名>的聊天记录」；群名为空时为「群聊的聊天记录」；
 *    · 两方都没有名字时为「聊天记录」；
 *    · 超过 kForwardTitleMaxChars 个字符时截断，截断点落在代理对中间时连同高位一起舍去。
 *
 *    srcCtype：        源会话类型，1 单聊、2 群聊；其它取值按单聊处理。
 *    selfName：        本人名字（单聊用；群聊忽略）。
 *    peerOrGroupName： 单聊为对方名字，群聊为群名。
 *  @return 标题，非空。本函数无副作用。
 */
std::wstring BuildMergedForwardTitle(int srcCtype, const std::wstring& selfName,
                                     const std::wstring& peerOrGroupName);

} // namespace balloonmain
