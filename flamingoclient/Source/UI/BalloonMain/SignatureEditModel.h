/**
 *  SignatureEditModel.h
 *
 *  主面板"点签名→原地编辑→提交"里**提交判定**的纯逻辑：给定编辑框里的文本和
 *  当前签名，判定这次应当 发送 / 不发(无变化) / 拦截(超长)，并给出 trim 后的
 *  待提交文本。把这段判定从对话框/窗口里抽成不依赖 HWND 的纯函数，便于被
 *  FlamingoUnit 测试工程直接断言（弹层显隐 / 发包 / 刷新等带窗口副作用的部分
 *  留在 CBalloonMainPanel / CSignatureEditPopup）。
 *
 *  说明：
 *    · "离线禁止修改"不在此判定 —— 那依赖运行期网络态，由调用方在判定之外检查。
 *    · 上限 127 字符沿用旧版客户端口径；编辑框另有 SetMaxLength(127) 兜底，故
 *      实际很难走到"超长"分支，仍保留以便测试与防御。
 *    · 允许清空签名：trim 后为空且与原值不同，照样判为"发送"（发空串）。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <atlstr.h>   // CString

namespace balloonmain {

//个性签名上限（字符数），沿用旧版客户端口径。
const int kSignatureMaxLen = 127;

//提交判定结果动作
enum SignCommitAction
{
    kSignCommitSend     = 0,   //trim 后与原值不同且未超长 → 发送更新（含发空串清空）
    kSignCommitNoChange = 1,   //trim 后与原值相同 → 不发包，仅收起编辑
    kSignCommitTooLong  = 2,   //trim 后超过上限 → 拦截，提示并留在编辑态
};

//提交判定结果
struct SignCommitDecision
{
    SignCommitAction action;    //本次该做什么（见 SignCommitAction）
    CString          trimmed;   //trim 首尾空白后的待提交文本（action==Send 时即要发送的内容）
};

/**
 *  判定一次签名提交该如何处理。纯函数、无副作用、不碰窗口/网络。
 *    edited： 编辑框当前文本（未 trim）。
 *    current：当前已生效的签名（用于判断是否真的发生了变化）。
 *    maxLen： 上限字符数（一般传 kSignatureMaxLen）。
 *  返回：判定结果（动作 + trim 后文本）。
 */
SignCommitDecision DecideSignatureCommit(const CString& edited,
                                         const CString& current,
                                         int maxLen);

} // namespace balloonmain
