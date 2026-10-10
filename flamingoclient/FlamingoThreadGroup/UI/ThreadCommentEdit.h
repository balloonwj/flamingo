/**
 *  帖子群里输入文字用的富文本框 CThreadCommentEdit（子类化 DuiRichEdit，不改 balloonui）：
 *    - 右栏留言框：Enter 发送、Ctrl+Enter 换行（默认）；
 *    - 发帖窗口的正文：Enter 换行、Ctrl+Enter 发布（SetEnterSends(false)）。
 *  输入法合成期间的回车一律放行（那时的回车是候选词上屏）。「发送」只投递一条消息给登记的窗口，由窗口取文字发请求。
 *
 *  典型用法：
 *      std::unique_ptr<CThreadCommentEdit> edit(new CThreadCommentEdit());
 *      edit->SetSendTarget(hwnd, kMsgXxx, wParam);
 *      edit->SetEnterSends(false);   //发帖窗口
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

namespace threadgroup {

/**
 *  留言与发帖用的输入框：按键决定发送还是换行，输入法合成期间不拦回车。只覆写按键与原始消息（子类化扩展）。
 */
class CThreadCommentEdit : public balloonwjui::DuiRichEdit
{
public:
    CThreadCommentEdit();

    /**
     *  登记「发送」时投给哪个窗口的哪条消息。
     *  @param hwnd   窗口（借用）
     *  @param msg    消息号
     *  @param wParam 消息的 wParam
     */
    void SetSendTarget(HWND hwnd, UINT msg, WPARAM wParam);

    /**
     *  选择哪个按键是「发送」。
     *  @param enterSends true（默认）：Enter 发送、Ctrl+Enter 换行；false：Enter 换行、Ctrl+Enter 发送
     */
    void SetEnterSends(bool enterSends);

    //按键：按 SetEnterSends 的选择投递发送消息或插入换行；输入法合成期间不拦
    virtual bool OnKeyDown(UINT vk, UINT flags) override;
    //字符：回车产生的字符一并吞掉（按键按下时已经处理过），合成期间不拦
    virtual bool OnChar(TCHAR ch) override;
    //原始消息：记下输入法合成的起止
    virtual bool OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult) override;

private:
    HWND   m_target;         //发送消息的目标窗口（借用）
    UINT   m_msg;            //发送消息号
    WPARAM m_wParam;         //发送消息的 wParam
    bool   m_enterSends;     //true：Enter 发送、Ctrl+Enter 换行；false：反过来
    bool   m_imeComposing;   //输入法正在合成（这时的回车是候选词上屏）
};

} // namespace threadgroup
