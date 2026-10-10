/**
 *  帖子群窗口右栏底部的留言框 CThreadComposer（docs/帖子群方案.md 4.3、4.6，样稿第 0 期）。
 *
 *  由 balloonui 现成控件组合而成（UI 约定第 2 条），从上到下：
 *    - 「回复 某某：  ✕ 取消」提示行：点了某条留言或回复的「回复」后出现，取消后隐藏；
 *    - 输入框（CThreadCommentEdit，子类化 DuiRichEdit）：Enter 发送、Ctrl+Enter 换行，输入法合成期间的回车照常上屏；
 *      表情在输入框里显示为「[表情名]」文字，发出时换成 /f["编号"]；
 *    - 底部一行：出错说明（红字，发送失败或内容不合法时）、字数「12 / 1000」、「发送」。
 *  本人不能发言时输入框只读、提示说明原因，「发送」置灰。
 *
 *  发送：按 Enter 或点「发送」时，向窗口投一条 kMsgThreadWindowDeferred（wParam 由窗口登记），由窗口取出线上正文发请求；
 *  发送成功后窗口调 Clear。提示行显隐之后对本控件重排（UI 约定第 5 条）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "ThreadCommentEdit.h"   // CThreadCommentEdit：留言输入框
#include "../Core/ThreadText.h"   // FaceTable
#include "../Core/ThreadMention.h"   // ThreadAtCandidate：插入过的 @ 候选

namespace balloonwjui {
class DuiButton;
class DuiHBox;
class DuiLabel;
}

namespace threadgroup {

//留言框里按钮的控件 id（DUIN_CLICK 的 ctrlId）
const UINT kIdComposerSend = 7701;         //「发送」
const UINT kIdComposerCancelReply = 7702;  //提示行的「✕ 取消」
const UINT kIdComposerEdit = 7703;         //输入框（DUIN_VALUECHANGED：文字变了，窗口据此刷新字数）

/**
 *  CThreadComposer —— 右栏底部的留言框。只在界面线程上使用。
 */
class CThreadComposer : public balloonwjui::DuiVBox
{
public:
    CThreadComposer();

    /**
     *  建出里面的控件。只调用一次。
     *  @param font       文字字体（借用）
     *  @param sendTarget Enter 与「发送」投递消息的窗口（借用）
     *  @param sendMsg    投递的消息号
     *  @param sendWParam 投递消息的 wParam
     */
    void Build(HFONT font, HWND sendTarget, UINT sendMsg, WPARAM sendWParam);

    /**
     *  能不能留言。不能时输入框只读、提示文字换成 reason，「发送」置灰。
     *  @param canSpeak 能否发言
     *  @param reason   不能发言时显示的说明
     */
    void SetCanSpeak(bool canSpeak, const std::wstring& reason);

    /**
     *  进入「回复」状态：显示「回复 某某：」提示行，并把焦点放进输入框。
     *  @param commentId 被回复的留言或回复
     *  @param name      被回复的人的显示名
     */
    void SetReplyTarget(int64_t commentId, const std::wstring& name);

    //退出「回复」状态（隐藏提示行，回到给帖子留言）
    void ClearReplyTarget();

    //当前被回复的留言或回复；不在「回复」状态时为 0
    int64_t ReplyTarget() const;

    /**
     *  输入框里的线上正文（「[表情名]」换成 /f["编号"]）。
     *  @param faces 表情名表
     */
    std::wstring WireText(const FaceTable& faces) const;

    //清空输入框、出错说明并退出「回复」状态（发送成功后）
    void Clear();

    /**
     *  刷新字数（输入框文字变了之后）：超过上限时字数变红。
     *  @param faces 表情名表（字数按线上正文计）
     */
    void UpdateCount(const FaceTable& faces);

    //显示一行出错说明（红字）；传空串时隐藏
    void SetError(const std::wstring& text);

    //控件需要的高度：提示行显示时更高，单位：像素
    int PreferredHeight() const;

    //输入框里的显示文字（测试用）
    std::wstring DisplayText() const;

    /**
     *  输入框的文字刚变化时调用（docs/帖子群方案.md 14.1 的 P6）：判断是不是刚输入了一个用来 @ 人的「@」——文字只多了
     *  一个字符、光标前一个字符是「@」、它位于开头或空白之后。是的话给出它的位置与光标处的屏幕坐标，窗口据此在光标处
     *  弹出成员浮层。每次调用都会记下当前的文字长度，供下一次比较。
     *  @param atPos       [出参] 那个「@」的字符位置
     *  @param caretScreen [出参] 光标那一行底边的屏幕坐标
     *  @return 刚输入了这样的「@」时返回 true
     */
    bool DetectMentionTrigger(int& atPos, POINT& caretScreen);

    /**
     *  @ 一个人：把 atPos 处的「@」换成「@名字 」并记成候选；atPos 处已经不是「@」（浮层打开期间用户又改了文字）时
     *  插在光标处。插入后光标在尾随空格之后。
     *  @param atPos  DetectMentionTrigger 给出的位置
     *  @param userId 被 @ 的人
     *  @param name   显示名（群名片或昵称，不含 '@'）
     */
    void InsertMention(int atPos, int64_t userId, const std::wstring& name);

    //插入过的 @ 候选，按插入顺序（发送前按最终文本重扫，删改过的自然不算）；Clear 时清空
    const std::vector<ThreadAtCandidate>& MentionCandidates() const;

private:
    //输入框里第 pos 个字符（富文本的字符位置）；越界时返回 0
    wchar_t CharAt(int pos) const;

private:
    CThreadCommentEdit*      m_edit;       //输入框（归本控件所有）
    balloonwjui::DuiHBox*    m_replyBar;   //「回复 某某：」提示行
    balloonwjui::DuiLabel*   m_replyText;  //提示行里的文字
    balloonwjui::DuiLabel*   m_error;      //出错说明
    balloonwjui::DuiLabel*   m_count;      //字数
    balloonwjui::DuiButton*  m_send;       //「发送」
    int64_t                  m_replyTo;    //被回复的留言或回复；0 表示给帖子留言
    std::vector<ThreadAtCandidate> m_atCandidates;   //插入过的 @ 候选；发送成功（Clear）后清空
    int                      m_lastLength; //上一次文字变化时输入框的字符数（判断「只多了一个字符」用）
    bool                     m_canSpeak;   //能否发言
};

} // namespace threadgroup
