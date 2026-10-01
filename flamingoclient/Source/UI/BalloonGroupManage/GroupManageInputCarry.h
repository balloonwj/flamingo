/**
 *  GroupManageInputCarry.h
 *
 *  群管理窗整页重建（或切页）时，输入框里用户已输入但尚未提交的内容如何带到新建的页面上，
 *  以及重建后滚动位置怎么取。纯逻辑，不依赖 ATL 与界面，FlamingoUnit 直接测试。
 *
 *  为什么需要它：群管理窗每次刷新都整页重建客户区（CBalloonGroupManageWindow::
 *  RefreshNavAndPage），切页时也新建整页，而输入框是随页面新造的，用户输入的文字只存在于
 *  旧控件里。拉取群资料 / 成员 / 公告等应答、服务端推送、点公告优先级按钮都会触发重建，
 *  不做处理时正在写的公告会被清空，没保存的群名称会被改回旧值（2026-10-01 修复）。
 *
 *  规则只有一条：输入框随页面建好后立即记下它的文字（"建页时的文字"）；页面被换掉之前，
 *  当前文字与之不同就说明用户改过，存为草稿，下次建出这个输入框时填回；相同则作废草稿，
 *  保留新建出来的内容。这条规则对两类输入框都成立：
 *    · 建页时为空的（公告标题、公告正文、新增敏感词）：用户输入了什么就带过去什么；
 *    · 建页时填入服务端取值的（群名称、群简介）：用户没改时服务端推来的新值照常显示，
 *      改了但没保存时以用户的修改为准。
 *
 *  草稿只保存在内存里，随群管理窗对象存在；关窗即丢弃（主面板每次打开群管理窗都新建对象）。
 *
 *  典型用法（窗口侧）：
 *      //页面被换掉之前，对当前页上存在的每个输入框：
 *      m_inputDrafts.OnInputLeaving(kIdEditAnnounceTitle, (LPCTSTR)pEdit->GetText());
 *      //新页面建好之后，对新页上存在的每个输入框：
 *      std::wstring draft;
 *      if (m_inputDrafts.OnInputBuilt(kIdEditAnnounceTitle, (LPCTSTR)pEdit->GetText(), &draft))
 *          pEdit->SetText(draft.c_str());
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <map>
#include <string>

namespace balloongroupmanage {

/**
 *  按控件编号记录群管理窗各输入框的"建页时的文字"与草稿。
 *
 *  控件编号取值见 GroupManageLayout.h（kIdEditAnnounceTitle 等）；本类不关心编号的含义，
 *  要跟踪哪些输入框由调用方决定。某个输入框暂时不存在（例如群资料尚未加载时基本信息页
 *  不建表单）时调用方不对它调用任何方法，它的草稿保持不变，等输入框建出来时再填回。
 */
class InputDraftTable
{
public:
    /**
     *  某个输入框刚随页面建好。记下它此刻的文字，作为之后判断"用户是否改过"的基准。
     *    ctrlId：   输入框控件编号。
     *    builtText：输入框建好后的文字（建页时填入的服务端取值，或空串）。
     *    draft：    [出参] 返回 true 时为应填回的草稿文字；返回 false 时不改动。不可为 NULL。
     *  @return 此前为它存过草稿、且草稿与 builtText 不同时返回 true，调用方应把输入框改成
     *          *draft。草稿与 builtText 相同时（例如用户保存成功，服务端推回的就是这段文字）
     *          直接作废草稿并返回 false。
     */
    bool OnInputBuilt(int ctrlId, const std::wstring& builtText, std::wstring* draft);

    /**
     *  当前页面即将被换掉（整页重建或切页），采集某个输入框的当前文字。
     *    ctrlId：     输入框控件编号。
     *    currentText：输入框当前的文字。
     *  与建页时的文字不同则存为草稿（覆盖之前的草稿）；相同则作废之前的草稿 —— 用户清空了
     *  公告表单、或改回了原值，都属于这种情况。从未经 OnInputBuilt 登记过的输入框按建页时
     *  为空处理。
     */
    void OnInputLeaving(int ctrlId, const std::wstring& currentText);

    /**
     *  用户要求放弃对某个输入框的修改（基本信息页的「重置」）：作废它的草稿，并取回建页时的
     *  文字，调用方应立即把输入框改成这段文字。
     *
     *  不能只靠随后的重新拉取与整页重建来恢复：重建前采集到的仍是用户改过的文字，会被
     *  OnInputLeaving 重新存为草稿再填回去，「重置」就没有效果了。
     *    ctrlId：  输入框控件编号。
     *    baseline：[出参] 返回 true 时为建页时的文字；返回 false 时不改动。不可为 NULL。
     *  @return 该输入框登记过建页时的文字时返回 true；从未经 OnInputBuilt 登记过时返回 false
     *          （此时仍会作废可能存在的草稿）。
     */
    bool Revert(int ctrlId, std::wstring* baseline);

    /**
     *  某个输入框当前是否存有草稿。
     *    ctrlId：输入框控件编号。
     *  @return 有草稿返回 true。本函数无副作用。
     */
    bool HasDraft(int ctrlId) const;

private:
    std::map<int, std::wstring> m_baselines;   //各输入框建页时的文字，键为控件编号；每次 OnInputBuilt 覆盖
    std::map<int, std::wstring> m_drafts;      //各输入框的草稿，键为控件编号；作废时整项删除
};

/**
 *  整页重建之后滚动视口应当处在的位置。
 *    pageBefore：     重建前停留的页（页面编号见 GroupManageLayout.h 的 kPageXxx）。
 *    pageAfter：      重建后停留的页。本人角色被降级、原来那页已不可见时会与 pageBefore 不同。
 *    scrollPosBefore：重建前的滚动位置，单位为像素；负数按 0 处理。
 *  @return 仍停在同一页时保持原位置（超出新内容的部分由滚动视口自己收回），换了页时回到顶部（0）。
 *          本函数无副作用。
 */
int ScrollPosAfterRebuild(int pageBefore, int pageAfter, int scrollPosBefore);

} // namespace balloongroupmanage
