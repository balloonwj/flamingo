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
#include "../BalloonAuth/BrandTextFit.h"   // BrandTextFitResult

namespace balloonregister {

// 注册窗的窗口尺寸（逻辑像素，含标题栏）。宽度沿用设计稿 AuthShell 的 380；
// 高度自 520 增至 610 —— 填写页由原来的 4 个输入框变成 5 个（手机号并入本页、
// 新增公司名称，同时去掉了自定义账号），每个输入框占 40 + 10(gap) = 50 像素。
// 2026-10-02 又加上 32 像素标题栏改为 642：原先的核算漏扣了标题栏，靠 ResizeClient
// 多叠加的一圈系统边框才排得下，按精确尺寸设置时提交按钮离窗口
// 下缘只剩 4 像素。具体的高度核算见 RegisterLayout.cpp 里 kRegBodyStep1Xml 上方的
// 注释，改动其一必须连带核对另一处；布局单测 R25 校验两者一致。窗口与布局单测共用这一份。
const int kRegWindowWidth  = 380;
const int kRegWindowHeight = 642;

// 标题栏高度（逻辑像素）。标题栏透明、只承载 min/close 按钮；其同高的
// 橙色区由窗体 9-grid 背景图透出，与下方 banner 渐变衔接，使 banner
// 视觉上直抵窗口顶端。须与 RegisterLayout.cpp 的 kRegTitleBarH 保持一致。
const int kRegWindowTitleBarH = 32;

/**
 *  注册窗客户区控件的数字 id —— 窗口代码按这些 id 取回控件。
 *  统一落在 800-899 区间，避免与其它 balloonui 子窗冲突。
 */
enum RegCtrlId
{
    // ---- 公共（每步都有）----
    kRegIdBackBtn      = 800,   // 返回按钮（RegBackButton），点击回到登录窗
    kRegIdBackTitle    = 801,   // 返回按钮右侧的标题「注册新账号」（RegText），深色字，大部分落在 banner 下方的白底上

    // ---- 步骤 1：填写资料 ----
    kRegIdEditNick     = 810,   // "昵称"输入框（DuiEdit）
    // 811 曾是"自定义账号"输入框，账号改为服务端自动生成后已删除，编号留空不再复用
    kRegIdEditPwd      = 812,   // "设置密码"输入框（DuiEdit，密码态）
    kRegIdEditPwd2     = 813,   // "确认密码"输入框（DuiEdit，密码态）
    kRegIdEditPhone    = 815,   // "手机号"输入框（DuiEdit）
    kRegIdComboCompany = 816,   // "公司名称"组合框（DuiComboBox，可编辑）：下拉里是默认公司，也可自行输入
    kRegIdBtnSubmit    = 817,   // "提交注册"主按钮（RegActionButton）
    kRegIdLinkAgreement = 818,  // "<产品名称> 服务协议"超链接（DuiLabel，ModeLink），默认 "FlamingoIM 服务协议"
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

/**
 *  读取注册窗某个文字控件（RegText）的文字颜色。供单元测试核对文字与底色有足够的反差。
 *    root：  客户区根控件（BuildRegisterBody 的返回值）；NULL 时直接返回 false。
 *    ctrlId：文字控件的 ctrl-id（RegCtrlId 中标为 RegText 的那几项）。
 *    color： [出参] 文字颜色。
 *  返回：找到该 id 且它确实是文字控件时返回 true 并写入 color；否则返回 false，color 不修改。
 */
bool GetRegisterTextColor(balloonwjui::DuiControl* root, int ctrlId, COLORREF& color);

/**
 *  把产品名称与标语应用到注册窗某一步的控件树上：改写 banner 左下角的品牌文字；填写页
 *  还会把服务协议链接改成「<产品名称> 服务协议」，并按文字实际宽度重设链接宽度（以该行
 *  放得下为上限，超出时链接文字截断成省略号）。
 *
 *  BuildRegisterBody 建出的控件树默认显示 FlamingoIM 与内置标语；注册窗应在建树之后、交给
 *  窗口排版之前调用本函数。
 *    root：  BuildRegisterBody 返回的根控件；NULL 时直接返回 false。
 *    title： 产品名称；NULL 或空串时按 FlamingoIM 处理。
 *    slogan：标语；NULL 或空串表示不显示标语。
 *  返回：找到了 banner 返回 true；否则返回 false（说明布局与本函数对不上了）。
 */
bool ApplyRegisterBrand(balloonwjui::DuiControl* root, LPCTSTR title, LPCTSTR slogan);

/**
 *  读取注册窗 banner 当前显示的产品名称与标语。供单元测试核对 ApplyRegisterBrand 的结果。
 *    root：  BuildRegisterBody 返回的根控件。
 *    title： [出参] 产品名称。
 *    slogan：[出参] 标语，空串表示不显示。
 *  返回：找到了 banner 返回 true；否则返回 false，两个出参不修改。
 */
bool GetRegisterBrandText(balloonwjui::DuiControl* root, CString& title, CString& slogan);

/**
 *  取注册窗 banner 左下角产品名称与标语两行文字的矩形，以及完整 banner 下沿的 y 坐标（都与
 *  控件树同一坐标系）。供单元测试核对品牌文字的位置、以及它与返回按钮不重叠；须在控件树
 *  排版之后调用。
 *    root：        BuildRegisterBody 返回的根控件。
 *    rcTitle：     [出参] 产品名称行的矩形。
 *    rcSlogan：    [出参] 标语行的矩形；不显示标语时为空矩形（上下缘相同）。
 *    bannerBottom：[出参] 完整 banner（从窗口顶算高 160）下沿的 y 坐标。
 *  返回：找到了 banner 返回 true；否则返回 false，出参不修改。
 */
bool GetRegisterBrandRows(balloonwjui::DuiControl* root, RECT& rcTitle, RECT& rcSlogan, int& bannerBottom);

/**
 *  计算注册窗 banner 左下角产品名称应采用的字号。可用宽度为文字左边距到火烈鸟图案左侧
 *  （留出间距）之间，放不下时逐级缩小，缩到下限仍放不下则标记为需要截断。
 *    hdc：        用于测量的 DC。
 *    bannerWidth：banner 宽度（像素），即注册窗宽度。
 *    title：      产品名称。
 *  返回：字号与是否截断。
 */
balloonauth::BrandTextFitResult FitRegisterBrandTitle(HDC hdc, int bannerWidth, LPCWSTR title);

} // namespace balloonregister
