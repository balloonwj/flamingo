/**
 *  帖子群窗口里只读富文本的建法与填充（docs/帖子群方案.md 4.6，第 0 期确认的第 9 项：详情、补充、留言里的表情画成图片）。
 *
 *  帖子正文、补充、留言与回复都用只读的 DuiRichEdit 显示：
 *    - MakeReadOnlyRichText：建一个没有边框、没有滚动条、不显示光标、自动按内容报告高度（布局用 Hint().Auto()）的只读富文本框，
 *      文字仍然可以拖选复制；
 *    - FillWireText：把线上正文（表情为 /f["编号"]）填进去：文字照常追加，表情插成内联图片；表情库里没有的编号显示为
 *      unknownFace 文字；
 *    - CThreadAtRichEdit（MakeReadOnlyRichText 建出的就是它）：HighlightAtSpans 给「@名字」上色时同时记下各段的位置与
 *      被 @ 的人；登记了回调（SetAtClickHandler）之后，左键或右键点在 @XXX 上时回调，由窗口弹出成员菜单（docs/帖子群方案.md
 *      §15.1 第 4 条），右键点在其他文字上仍是原来的复制菜单。没有登记回调的（如标题）与原来完全相同。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      std::unique_ptr<balloonwjui::DuiRichEdit> body = threadgroup::MakeReadOnlyRichText(font, kTextColor, kBg);
 *      threadgroup::FillWireText(body.get(), post.m_text, faces, TR(L"[表情]"), kFacePx);
 *      vbox->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Auto());
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../Host/ThreadWindowApi.h"   // ThreadFaceEntry
#include "../Core/ThreadMention.h"     // ThreadAtSpan：正文里 @ 片段的位置

namespace threadgroup {

/**
 *  点在正文里的「@某人」上时的回调（左键松开且没有拖选，或右键按下）。
 *  @param user     登记时给的 userdata（借用）
 *  @param userId   被 @ 的人（大于 0；「@所有人」不回调）
 *  @param screenPt 点击位置（屏幕坐标），菜单的锚点
 */
typedef void (*ThreadAtClickFn)(void* user, int64_t userId, POINT screenPt);

/**
 *  CThreadAtRichEdit —— 帖子群窗口的只读富文本：在 DuiRichEdit 之上增加「点 @XXX」的命中判断（子类化，不改 balloonui）。
 *  命中按字符位置算（CharAtPoint）：EM_CHARFROMPOS 给的是离点最近的字边界（插入点），点在一个字的右半边时得到的是下一个字，
 *  所以再用 EM_POSFROMCHAR 退回到点实际所在的那个字，并核对点确实落在这个字上（点在行尾空白处时 EM_CHARFROMPOS 也会给出
 *  最近的字）；最后在登记的 @ 片段里查这个字属于谁（AtUserAtChar）。只在界面线程上使用。
 */
class CThreadAtRichEdit : public balloonwjui::DuiRichEdit
{
public:
    CThreadAtRichEdit();

    /**
     *  登记点 @XXX 的回调。不登记（fn 为 NULL）时本控件的鼠标行为与 DuiRichEdit 完全相同。
     *  @param fn   回调；可为 NULL
     *  @param user 回调的 userdata（借用，须比本控件存活久）
     */
    void SetAtClickHandler(ThreadAtClickFn fn, void* user);

    /**
     *  记下一段正文里的 @ 片段（HighlightAtSpans 调用）。可多次调用，各次的片段合在一起。
     *  @param base   这段正文在富文本里的起始字符位置
     *  @param ranges 片段的显示区间（相对 base）
     */
    void AddAtRanges(int base, const std::vector<ThreadAtDisplay>& ranges);

    //已记下的 @ 片段（绝对字符位置），供测试核对
    const std::vector<ThreadAtDisplay>& AtRanges() const;

    /**
     *  某个点落在哪个人的 @ 上。
     *  @param pt 宿主窗口客户区坐标（与鼠标事件里的坐标相同）
     *  @return 被 @ 的人；没点在 @ 上、或点在「@所有人」上时返回 0
     */
    int64_t HitTestAt(POINT pt);

    //DuiRichEdit 覆写：左键按下时记下按在了谁的 @ 上，再交给基类（拖选照常）
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //DuiRichEdit 覆写：左键松开时仍在同一个 @ 上且没有拖选出文字，就回调
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    //DuiRichEdit 覆写：右键按在 @XXX 上时回调、不弹复制菜单；按在其他地方交给基类（复制菜单）
    bool OnRButtonDown(POINT pt, UINT mkFlags) override;
    //DuiRichEdit 覆写：鼠标在 @XXX 上时显示手形光标，提示可以点
    bool OnSetCursor(POINT pt) override;

private:
    /**
     *  点落在第几个字上。
     *  @return 字的位置；点不在任何字上（行尾空白、行首左边的空白、正文之外）时返回 -1
     */
    int CharAtPoint(POINT pt);
    //点是否确实落在第 index 个字上（排除行尾空白、行首左边的空白）
    bool PointOnChar(POINT pt, int index);
    //本控件里有没有拖选出的文字
    bool HasSelection();
    //回调：控件坐标换算成屏幕坐标后交给登记的回调
    void FireAtClick(int64_t userId, POINT pt);
    //记一行点击的日志（点的坐标、控件矩形、点所在的字、命中的人），排查点了 @XXX 没反应时用；
    //只在登记了回调且有 @ 片段时记
    void LogAtClick(const char* what, POINT pt);

private:
    ThreadAtClickFn              m_atClickFn;    //点 @XXX 的回调；为 NULL 时不处理
    void*                        m_atClickUser;  //回调的 userdata（借用）
    std::vector<ThreadAtDisplay> m_atRanges;     //全部 @ 片段（绝对字符位置），HighlightAtSpans 登记
    int64_t                      m_pressedAtUser;//左键按下时按在了谁的 @ 上；没有为 0，松开时清零
};

/**
 *  建一个只读展示用的富文本框。
 *  @param font  默认字体（借用；为 NULL 时用 balloonui 的默认字体）
 *  @param color 文字颜色
 *  @param bg    背景色（与所在容器的底色相同，看起来没有框）
 *  @return 富文本框，所有权交给调用方
 */
std::unique_ptr<CThreadAtRichEdit> MakeReadOnlyRichText(HFONT font, COLORREF color, COLORREF bg);

/**
 *  把线上正文填进富文本框（先清空原有内容）。
 *  @param edit        富文本框（借用）
 *  @param wire        线上正文（表情为 /f["编号"]，字面的 /f["n"] 以 //f["n"] 转义）
 *  @param faces       表情库（编号 → 图片路径）
 *  @param unknownFace 表情库里没有的编号显示成什么文字（通常是 TR 过的「[表情]」）
 *  @param facePx      表情图片的排版边长，单位：像素
 */
void FillWireText(balloonwjui::DuiRichEdit* edit, const std::wstring& wire, const std::vector<ThreadFaceEntry>& faces,
                  const std::wstring& unknownFace, int facePx);

/**
 *  在已有内容后面追加线上正文（不清空原有内容），规则同 FillWireText。回复先写「某某 回复 某某：」再用它接正文。
 */
void AppendWireText(balloonwjui::DuiRichEdit* edit, const std::wstring& wire, const std::vector<ThreadFaceEntry>& faces,
                    const std::wstring& unknownFace, int facePx);

/**
 *  在已有内容后面追加一段普通文字（不解析表情），如回复前面的「张三 回复 李四：」。
 *  @param edit 富文本框（借用）
 *  @param text 文字
 */
void AppendPlainText(balloonwjui::DuiRichEdit* edit, const std::wstring& text);

/**
 *  给已经填进富文本框的一段正文里的「@名字」加背景色与文字色（docs/帖子群方案.md 14.4），配色与普通群气泡相同：
 *  @ 别人为浅蓝底蓝字，@ 到本人与「@所有人」为浅橙底橙字。换算不了的片段跳过，不影响正文显示。
 *  edit 是 CThreadAtRichEdit 时同时记下各段的位置与被 @ 的人（点 @XXX 弹成员菜单用）。
 *  @param edit        富文本框（借用）
 *  @param base        这段正文在富文本框里的起始字符位置（FillWireText 为 0；回复为前缀「某某 回复 某某：」之后的位置）
 *  @param wire        这段正文的线上形式（与填进去时的相同）
 *  @param at          片段（服务端下发的位置）
 *  @param myUserId    本人
 *  @param faces       表情库（判断表情显示为图片还是替代文字，与填充时一致）
 *  @param unknownFace 表情库里没有的编号显示的文字
 *  @return 加上颜色的片段数
 */
size_t HighlightAtSpans(balloonwjui::DuiRichEdit* edit, int base, const std::wstring& wire,
                        const std::vector<ThreadAtSpan>& at, int64_t myUserId, const std::vector<ThreadFaceEntry>& faces,
                        const std::wstring& unknownFace);

} // namespace threadgroup
