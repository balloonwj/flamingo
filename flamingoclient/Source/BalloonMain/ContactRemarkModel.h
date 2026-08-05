/**
 *  ContactRemarkModel.h
 *
 *  联系人备注名相关的两段纯逻辑：
 *
 *    一、ResolveContactDisplayName —— "这个联系人在界面上该叫什么名字"。
 *        备注优先、其次昵称、再次账号、都没有才退回"用户<id>"。通讯录树、会话
 *        列表、单聊窗标题三处都要按同一口径取名，各写一份的话很容易出现同一个人
 *        在三个地方叫三个名字，故收口到这里。
 *
 *    二、DecideRemarkCommit —— "这次备注编辑该不该发出去"。给定编辑框文本、
 *        当前备注与当前连接态，判定 发送 / 不发(无变化) / 拦截(离线) / 拦截(超长)，
 *        并给出 trim 后的待提交文本。写法与同级的 SignatureEditModel 一致。
 *
 *  两者都不依赖 HWND、不碰网络，便于被 FlamingoUnit 测试工程直接断言；真正的
 *  发包、弹层显隐与各处刷新留在 CBalloonMainPanel / CBalloonContactWindow。
 *
 *  备注名经 cmd_set_friend_remark(1209) 提交，随通讯录 cmd_get_contacts 应答里
 *  每个联系人的 remark 字段读回（见 net/ContactsProtocolClient.h 的
 *  ContactItem::m_remark）。备注名是单向的 —— 只影响"本人看该联系人"的显示。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <atlstr.h>   // CString

namespace balloonmain {

//备注名上限（字符数）。取 32 是被库结构卡死的：t_user_relationship 的
//f_user1_markname / f_user2_markname 都是 VARCHAR(32)，超了会被库悄悄截断，
//界面上就会出现"改完显示的是全的、重登之后少一截"的怪事，故在提交前就拦住。
const int kContactRemarkMaxLen = 32;

//备注提交判定结果动作
enum RemarkCommitAction
{
    kRemarkCommitSend     = 0,   //trim 后与原值不同、在线且未超长 → 发 cmd_set_friend_remark（含发空串清除备注）
    kRemarkCommitNoChange = 1,   //trim 后与原值相同 → 不发包，仅收起编辑态
    kRemarkCommitOffline  = 2,   //当前不可发起联网操作 → 拦截，提示并把备注还原成原值
    kRemarkCommitTooLong  = 3,   //trim 后超过上限 → 拦截，提示并留在编辑态
};

//备注提交判定结果
struct RemarkCommitDecision
{
    RemarkCommitAction m_action;    //本次该做什么（见 RemarkCommitAction）
    CString            m_trimmed;   //trim 首尾空白后的待提交文本（m_action==Send 时即要发送的内容）

    RemarkCommitDecision()
        : m_action(kRemarkCommitNoChange)
    {
    }
};

/**
 *  解析一个联系人在界面上显示的名字。纯函数、无副作用。
 *
 *  取名顺序：备注名 → 昵称 → 账号 → "用户<userid>"。每一级都要求 trim 后非空
 *  —— 服务端下发的昵称有可能是几个空格，那种情况下继续往下退比显示一片空白好。
 *
 *    remark：   本人给该联系人起的备注名，可空。
 *    nickname： 联系人昵称，可空。
 *    username： 联系人账号，可空。
 *    userid：   联系人 userid，前三者都为空时用来拼兜底名。
 *
 *  返回：显示名，保证非空。
 */
CString ResolveContactDisplayName(const CString& remark,
                                  const CString& nickname,
                                  const CString& username,
                                  UINT userid);

/**
 *  判定一次备注提交该如何处理。纯函数、无副作用、不碰窗口/网络。
 *
 *  判定顺序刻意是"先看有没有变化，再看能不能发"：用户只是点进编辑框又点出来、
 *  一个字没改时，即便当前离线也不该弹"网络不可用"来烦他。
 *
 *    edited： 编辑框当前文本（未 trim）。
 *    current：当前已生效的备注名（用于判断是否真的发生了变化）。
 *    online： 当前是否可以发起联网操作（由调用方从连接态换算，见
 *             OnlineCapability.h；本函数不自己查网络）。
 *    maxLen： 上限字符数（一般传 kContactRemarkMaxLen）。
 *
 *  返回：判定结果（动作 + trim 后文本）。
 */
RemarkCommitDecision DecideRemarkCommit(const CString& edited,
                                        const CString& current,
                                        bool online,
                                        int maxLen);

} // namespace balloonmain
