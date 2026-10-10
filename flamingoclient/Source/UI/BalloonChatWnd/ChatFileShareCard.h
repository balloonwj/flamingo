/**
 *  ChatFileShareCard.h
 *
 *  文件共享卡片（msgtype=20）在聊天窗里显示为一张卡片：类型图标（磁盘 / 文件夹 / 文件）与共享名称、真实路径
 *  （只有分享者本人的卡片显示，2026-10-04 起）、
 *  「某某的共享 · 状态」，以及一个按钮（别人的磁盘与文件夹为「打开」，单个文件为「下载」；本人的共享为「在本机打开」，
 *  2026-10-03 起）。本文件把 content 原文解析成卡片
 *  要显示的内容并套用到一条气泡数据上，状态变化时重填状态与按钮。纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试。
 *  卡片的绘制在 ChatBubble 里，设计见 docs/文件共享方案.md 7.3「共享卡片」。
 *
 *  状态的取法（方案 7.3）：FlamingoFileShare.dll 已加载且有最新状态时以它为准；否则用消息里服务端按看的人合并的
 *  sharestate / sharereason；两者都没有时显示「正在获取状态」。DLL 未加载时按钮不可点，状态写「当前客户端未启用共享功能」。
 *
 *  典型用法（聊天窗收到或翻到一条共享卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeRealTextMessage(fromMe, _T("[共享]"), ...);
 *      balloonchatwnd::ShareCardLiveState live = ...;   //模块是否已加载、DLL 手里的最新状态
 *      balloonchatwnd::ApplyFileShareCard(msg, notify.m_shareContent, ownerName, live);
 *      list->AppendMessage(msg);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

#include "ChatMockData.h"   // ChatMessage / kMsgFileShare

namespace balloonchatwnd {

//共享卡片底部的按钮
enum ShareCardButton
{
    kShareCardButtonNone      = 0,   //不显示按钮：卡片正文无法解析
    kShareCardButtonOpen      = 1,   //「打开」：别人共享的磁盘与文件夹，打开浏览窗口
    kShareCardButtonDownload  = 2,   //「下载」：别人共享的单个文件，打开浏览窗口（列表里只有这个文件）后下载
    kShareCardButtonOpenLocal = 3,   //「在本机打开」：本人发起的共享，用资源管理器打开本机上的共享内容（2026-10-03 起）
};

/**
 *  套用卡片时的实时信息（由宿主聊天窗从文件共享宿主取得）。
 */
struct ShareCardLiveState
{
    bool m_moduleReady;   //本客户端是否已加载文件共享模块
    bool m_hasState;      //模块手里是否有这个共享的最新状态
    int  m_state;         //最新状态（share_state），m_hasState 为真时有效
    int  m_reason;        //最新原因

    ShareCardLiveState()
        : m_moduleReady(false)
        , m_hasState(false)
        , m_state(0)
        , m_reason(0)
    {
    }
};

/**
 *  状态文字。
 *  @param moduleReady 本客户端是否已加载文件共享模块；为假时一律为「当前客户端未启用共享功能」
 *  @param hasState    是否知道状态；为假时为「正在获取状态」
 *  @param state       状态（share_state）
 *  @param reason      原因
 */
CString ShareCardStatusText(bool moduleReady, bool hasState, int state, int reason);

/**
 *  按钮文字；不显示按钮时返回 NULL。
 */
LPCWSTR ShareCardButtonLabel(int button);

/**
 *  把一条气泡数据改成共享卡片：kind 置为 kMsgFileShare，填写 shareXxx 字段，text 置为「[共享] 名称」
 *  （引用回复、复制等仍按 text 取摘要的地方因此拿到它）。content 不可用时仍是一张卡片：名称为「共享」，没有按钮。
 *  @param msg         [入参/出参] 已按普通文字气泡构造好的气泡数据（fromMe、头像、msgid 等保持不变）
 *  @param contentJson content 原文，原样存入 msg.shareContent
 *  @param ownerName   分享者的显示名（本人发出的卡片忽略，写「我的共享」）
 *  @param live        实时信息；DLL 有最新状态时优先于 content 里合并的状态
 */
void ApplyFileShareCard(ChatMessage& msg, const std::string& contentJson, const CString& ownerName,
                        const ShareCardLiveState& live);

/**
 *  共享状态变了（DLL 收到 2203 / 2206 后经主面板转来）：重填状态文字与按钮是否可点。
 *  @param msg         [入参/出参] 共享卡片（kind 不是 kMsgFileShare 时不做任何事）
 *  @param moduleReady 本客户端是否已加载文件共享模块
 *  @param state       新状态
 *  @param reason      新原因
 */
void ApplyFileShareState(ChatMessage& msg, bool moduleReady, int state, int reason);

} // namespace balloonchatwnd
