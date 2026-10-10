/**
 *  flamingoAdmin —— 外部服务域名白名单窗 BotDomainsModal（docs/机器人方案.md 1m，接口见 docs/Admin控制台接口文档.md 14.9、14.10）。
 *
 *  机器人页点「外部服务域名白名单」时弹出：一个多行输入框，一行一个域名。机器人规则里「呼叫外部服务」的地址，主机
 *  必须等于白名单里某一项或是它的子域名；白名单为空时不能配置外部服务。点「保存」时先在本地校验
 *  （botrule::ValidateDomains：最多 50 条、每条都是合法域名），通过后由 AdminFrame 交给机器人页整份提交；
 *  服务端拒绝时在按钮栏左侧用红字提示，窗口不关闭。
 *
 *  实现与企业问答的编辑窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，开 / 关只切显隐。
 *  单元测试见 Tests/EnterpriseBotTests.cpp。
 *
 *  典型用法（AdminFrame 内）：
 *      m_botDomainsModal->ShowWithDomains(request.m_domains);
 *      m_root->ShowModal(m_botDomainsModal);
 *      //点「保存」时
 *      if (m_botDomainsModal->Validate(&error)) screen->SubmitDomains(m_botDomainsModal->GetDomains());
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <vector>

#include "DuiControl.h"

namespace balloonwjui { class DuiEdit; class DuiButton; }

namespace flamingoadmin {

// 外部服务域名白名单窗。
class BotDomainsModal : public balloonwjui::DuiControl
{
public:
    BotDomainsModal();

    // 打开：输入框里填好当前的白名单（每项一行），清掉错误提示与提交中状态。
    void ShowWithDomains(const std::vector<CString>& domains);

    // 输入框里的白名单：一行一个，去掉首尾空白、转小写、去掉空行与重复项（botrule::SplitDomains）。
    std::vector<CString> GetDomains() const;

    // 本地校验（botrule::ValidateDomains）。
    //   error：[出参，可为 NULL] 不通过时的中文原因。
    //   返回：通过返回 true。
    bool Validate(CString* error) const;

    // 设置按钮栏左侧的红字错误提示并重绘；空串表示不显示。
    void SetError(const CString& text);

    // 当前的错误提示。
    const CString& ErrorText() const { return m_error; }

    // 设置「提交中」状态：提交中「保存」按钮不可用并显示「保存中…」。
    void SetBusy(bool busy);

    // 是否处于提交中。
    bool IsBusy() const { return m_busy; }

    // 直接设置输入框里的文字（单元测试用）。
    void SetText(const CString& text);

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    CString                  m_error;      // 红字错误提示，空串表示不显示
    bool                     m_busy;       // 是否处于提交中
    RECT                     m_panelRect;  // 卡片矩形
    RECT                     m_noteRect;   // 说明段落的矩形
    RECT                     m_errorRect;  // 按钮栏左侧错误提示的矩形
    balloonwjui::DuiEdit*    m_text;       // 白名单输入框（多行）；所有权在子控件列表
    balloonwjui::DuiButton*  m_btnClose;   // 右上角关闭按钮
    balloonwjui::DuiButton*  m_btnCancel;  // 「取消」按钮
    balloonwjui::DuiButton*  m_btnSave;    // 「保存」按钮
};

} // namespace flamingoadmin
