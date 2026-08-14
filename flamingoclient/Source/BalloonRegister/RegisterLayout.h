/**
 *  RegisterLayout.h
 *
 *  注册对话框客户区控件树的构造接口。布局以内嵌 XML 为唯一真值源，
 *  交给 balloonui 的 DuiXmlBuilder 解析。注册窗分两步，每步的客户区
 *  控件树不同 —— BuildRegisterBody(step) 按步骤号返回对应的控件树。
 *
 *  窗口代码按下面的 RegCtrlId 数字 id 取回需要操作的控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonregister {

/**
 *  注册窗客户区控件的数字 id —— 窗口代码按这些 id 取回控件。
 *  统一落在 800-899 区间，避免与其它 balloonui 子窗冲突。
 */
enum RegCtrlId
{
    // ---- 公共（每步都有）----
    kRegIdBackBtn      = 800,   // 返回按钮（RegBackButton），点击回到登录窗

    // ---- 步骤 1：填写资料 ----
    kRegIdEditNick     = 810,   // "昵称"输入框（DuiEditHost）
    // 811 曾是"自定义账号"输入框，账号改为服务端自动生成后已删除，编号留空不再复用
    kRegIdEditPwd      = 812,   // "设置密码"输入框（DuiEditHost，密码态）
    kRegIdEditPwd2     = 813,   // "确认密码"输入框（DuiEditHost，密码态）
    kRegIdEditPhone    = 815,   // "手机号"输入框（DuiEditHost）
    kRegIdEditCompany  = 816,   // "公司名称"输入框（DuiEditHost）
    kRegIdBtnSubmit    = 817,   // "提交注册"主按钮（RegActionButton）
    kRegIdLinkAgreement = 818,  // "FlamingoIM 服务协议"超链接（DuiLabel，ModeLink）
    kRegIdLinkPrivacy   = 819,  // "隐私政策"超链接（DuiLabel，ModeLink）

    // ---- 步骤 2：完成 ----
    kRegIdTextAccount  = 831,   // "你的账号是 xxx"结果文字（RegText），注册成功后按服务端应答回填
    kRegIdTextCompany  = 832,   // "已加入 xxx"结果文字（RegText），显示服务端返回的企业全称
    kRegIdBtnLogin     = 830,   // "立即登录"主按钮（RegActionButton），点击关窗并把账号回填到登录框
};

/**
 *  构造注册窗某一步的客户区控件树。
 *    step：步骤号，取值见 RegisterStep（1=填写资料，2=完成）；
 *          越界时回退到步骤 1。
 *  返回：该步的客户区根控件（透明底纵向布局容器，banner 区域透出窗口
 *        皮肤的橙色）；解析失败返回 nullptr。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildRegisterBody(int step);

/**
 *  改写注册窗里某个文字控件的内容。
 *
 *  注册完成页上的"你的账号是 xxx""已加入 xxx"两行取决于服务端应答，
 *  只能在收到应答后回填。承载它们的文字控件是布局层的内部类型，窗口
 *  代码 dynamic_cast 不到，故由布局层提供本接口按 ctrl-id 定位并改写。
 *
 *    root：  客户区根控件（GetClientContent 的返回值）；NULL 时直接返回 false。
 *    ctrlId：目标控件的数字 id，取值见 RegCtrlId。
 *    text：  新文本；NULL 按空串处理。
 *  返回：找到该 id 且它确实是文字控件时返回 true 并已改写；否则返回 false
 *        （调用方据此打日志，说明布局与 id 对不上了）。
 *
 *  注意：本函数只改数据不触发重绘，调用方改完后需自行让控件重画。
 */
bool SetRegisterText(balloonwjui::DuiControl* root, int ctrlId, LPCTSTR text);

} // namespace balloonregister
