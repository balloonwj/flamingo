/**
 *  ChatMemoPrefill.h
 *
 *  聊天消息「添加到备忘」的预填内容（docs/备忘与提醒方案.md §8）：单聊窗、群聊窗的文字消息右键选
 *  「添加到备忘」/「添加到我的备忘」/「添加到群备忘」后，打开新建备忘窗口前，按本文件的规则生成标题与备注：
 *
 *    · 标题：消息纯文字的第一个非空行，超过 50 字时截成 49 字加「…」；纯文字为空时为「聊天消息」。
 *    · 备注：第一行写来源（「来自 张三，10月6日 14:30」，群聊为「来自 项目群 / 张三，10月6日 14:30」，发送人是
 *      自己时写「我」），第二行起是消息的纯文字全文；合计超过 200 字时把正文截短并以「…」结尾，来源行保留。
 *
 *  字数按字符计，代理对算 1 个，与备忘编辑窗的校验口径相同（MemoViewModel.h 的 kMemoTitleMaxChars /
 *  kMemoNoteMaxChars）。编辑框的 SetMaxLength 只限制键盘输入、不限制程序预填的文字，所以必须在这里截断，
 *  否则要到点「保存」时才报错。
 *
 *  本文件只放与窗口无关的部分，便于单测。典型用法（宿主聊天窗收到 WM_CHAT_ADD_TO_MEMO 前，由气泡调用）：
 *      MemoPrefillSource src;
 *      src.m_groupName = groupName;          //单聊留空
 *      src.m_senderName = msg.senderName;
 *      src.m_fromMe = msg.fromMe;
 *      src.m_timestampMs = msg.timestampMs;
 *      MemoPrefill prefill = BuildMemoPrefill(msg, src);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "ChatMockData.h"   // ChatMessage

namespace balloonchatwnd {

// 备忘标题的字数上限（字符，代理对算 1 个），与 balloonmemo::kMemoTitleMaxChars 相同
const int kMemoPrefillTitleMaxChars = 50;

// 备忘备注的字数上限（字符，代理对算 1 个），与 balloonmemo::kMemoNoteMaxChars 相同
const int kMemoPrefillNoteMaxChars = 200;

// 「添加到备忘」的目标
enum MemoPrefillTarget
{
    // 存成本人备忘：单聊的「添加到备忘」与群聊的「添加到我的备忘」
    kMemoPrefillPersonal = 1,
    // 存成群备忘：群聊的「添加到群备忘」，只有能新建群备忘的人（群主、管理员）看得到
    kMemoPrefillGroup = 2,
};

/**
 *  消息的来源信息，写进备注第一行。
 */
struct MemoPrefillSource
{
    CString   m_groupName;     // 群名；单聊留空
    CString   m_senderName;    // 发送人显示名；m_fromMe 为 true 时不用
    bool      m_fromMe;        // 发送人是不是本人：是的话来源行写「我」
    long long m_timestampMs;   // 消息时间（Unix 毫秒）；<= 0 时来源行不写时间

    MemoPrefillSource()
        : m_fromMe(false)
        , m_timestampMs(0)
    {
    }
};

/**
 *  预填结果（UTF-8，可直接写进 net::MemoItem 的 m_title / m_note）。
 */
struct MemoPrefill
{
    std::string m_title;   // 标题，不含换行，不超过 kMemoPrefillTitleMaxChars 个字
    std::string m_note;    // 备注，不超过 kMemoPrefillNoteMaxChars 个字
};

/**
 *  WM_CHAT_ADD_TO_MEMO 的 lParam（堆对象，宿主处理后 delete）：气泡把被点的消息原样交给宿主聊天窗，由宿主补上
 *  来源信息（单聊的对方名字、群名）后生成预填内容。
 */
struct ChatAddToMemoRequest
{
    int         m_target;   // 存成哪种备忘，取值见 MemoPrefillTarget
    ChatMessage m_msg;      // 被点的那条消息（副本）

    ChatAddToMemoRequest()
        : m_target(kMemoPrefillPersonal)
    {
    }
};

/**
 *  判断一条消息能不能「添加到备忘」：kMsgText 一类（普通文字、带 @、Markdown、代码片段、带引用的回复），
 *  且不是发送失败的行。大表情、图片、文件等都不能。
 *    msg：消息。
 *  @return 能则返回 true。本函数无副作用。
 */
bool CanAddMessageToMemo(const ChatMessage& msg);

/**
 *  取消息的纯文字：普通文字的内联表情写成「[表情]」、@ 保留显示名；Markdown 取渲染后的纯文字（Markdown 模块
 *  不可用时为源文本）；代码片段取原文；带引用的回复只取回复正文。
 *    msg：消息。
 *  @return 纯文字，换行为 \n。本函数无副作用。
 */
std::wstring MessagePlainTextForMemo(const ChatMessage& msg);

/**
 *  按 §8.1 的规则生成预填的标题与备注。
 *    msg：消息；不满足 CanAddMessageToMemo 时照样生成（标题为「聊天消息」），由调用方事先判断。
 *    src：来源信息。
 *  @return 预填结果。本函数无副作用。
 */
MemoPrefill BuildMemoPrefill(const ChatMessage& msg, const MemoPrefillSource& src);

/**
 *  数一段文字有多少个字符（代理对算 1 个），与备忘编辑窗的校验口径相同。
 *    text：文字。
 *  @return 字符数。
 */
int CountMemoChars(const std::wstring& text);

} // namespace balloonchatwnd
