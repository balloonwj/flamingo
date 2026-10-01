/**
 *  删除历史登录账号时的确认与本地数据清理，AccountRemoval.h
 *
 *  登录框账号下拉里每项右侧有个删除叉（balloonui 的 DuiComboBox::SetShowItemDelete）。
 *  点它之后要做两件事：先跟用户确认，确认了再清东西。本文件放的就是这两件事。
 *
 *  为什么要二次确认：删除叉紧挨着账号文字，误点很容易；而删掉之后既找不回来、
 *  又可能连带清掉本地聊天记录。确认框里因此另给一个「同时删除本地聊天记录」的
 *  勾选项，<u>默认不勾</u> —— 多数人只是想让下拉列表干净点，不是要清历史。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#pragma once

#include <string>

namespace balloonauth {

/**
 *  弹出"确认删除这个账号吗"的二次确认框。
 *
 *  用 Windows 的 TaskDialog（系统原生样式）：确认框需要一个"同时删除本地聊天
 *  记录"的勾选项，TaskDialog 的 verification checkbox 正好是这个形态，键盘
 *  导航、高 DPI、无障碍也都是系统给的。
 *
 *  @param hParent          父窗口句柄；用于让确认框以它为中心显示并置于其上。
 *  @param account          待删除的账号名（会显示在确认文字里）。
 *  @param outDeleteLocal   [出参] 用户是否勾了「同时删除本地聊天记录」；
 *                          返回 false 时该出参无意义，调用方不应使用。
 *  @return 用户点了「删除」返回 true；点「取消」、按 ESC 或关掉窗口返回 false。
 *          TaskDialog 调用本身失败时也返回 false —— 确认不了就不删，宁可让
 *          用户再点一次，也不能在没确认的情况下删东西。
 */
bool ConfirmRemoveAccount(HWND hParent, const CString& account, bool& outDeleteLocal);

/**
 *  删掉某账号在本机的全部数据目录（<主目录>Users\<账号>\ 整棵）。
 *
 *  这个目录下是该账号的配置（<账号>.ini）、聊天记录（ChatLog）、头像
 *  （UserThumb）以及文件 / 图片 / 头像缓存（ChatFile / ChatImage / Avatar）——
 *  自 2026-08-11 起这几样统一挂在同一个按登录账号命名的目录下（见
 *  net/UserCachePath.h），所以清理只需删这一处。
 *
 *  <b>不可恢复</b>：走的是直接删除、不进回收站。调用方必须已经拿到用户确认。
 *
 *  安全边界：账号名经 net::SanitizeAccountForPath 净化，且拼出的路径必须落在
 *  <主目录>Users\ 之下，否则一律拒绝执行 —— 避免账号名里带 ".." 或分隔符时
 *  删到别处去。
 *
 *  @param account 账号名。为空、或净化后为空时直接返回 false，不做任何事。
 *  @return 目录删除成功、或该目录本来就不存在时返回 true；路径不合法或删除
 *          失败返回 false。
 */
bool DeleteAccountLocalData(const CString& account);

} // namespace balloonauth
