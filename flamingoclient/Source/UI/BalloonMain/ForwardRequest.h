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
struct ForwardRequest
{
    int                  m_srcCtype;   //源会话类型：1 单聊、2 群聊
    int64_t              m_srcCid;     //源会话 id：单聊为对方 userid，群聊为群 id
    //发起转发的聊天窗或消息记录窗，转发结束后在它上面提示结果。用户可能早已关掉它，使用前须 IsWindow
    HWND                 m_srcWnd;
    bool                 m_merge;      //true 合并转发为一条聊天记录，false 逐条转发
    std::vector<int64_t> m_msgids;     //要转发的服务器 msgid，按 msgid 升序、去重，至少一个
    size_t               m_skipped;    //选中了但不能转发的行数，用于提示「已跳过 N 条」
    //合并转发的标题；逐条转发时为空。聊天窗投递时不填，由主面板用真名（不含备注）与群名生成
    std::wstring         m_title;

    ForwardRequest()
        : m_srcCtype(0)
        , m_srcCid(0)
        , m_srcWnd(NULL)
        , m_merge(false)
        , m_skipped(0)
    {
    }
};

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
