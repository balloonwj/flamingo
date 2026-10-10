/**
 *  机器人编辑窗的控件树（docs/机器人方案.md 4.1、4.2、4.3），群机器人与个人机器人共用。从上到下：
 *    - 基本信息：头像预览、名称、简介、头像字形、头像底色；
 *    - 规则：「添加规则」，规则列表（每条一张卡片：序号、摘要、编辑 / 上移 / 下移 / 删除）；
 *    - 试一试：输入一段文字，群机器人可选「@ 了机器人」，个人机器人可选模拟的在线状态，显示判定结果；
 *    - 外部系统推送（群机器人且企业允许外部服务时）：Webhook 地址的写法、令牌与签名密钥（只读，可复制）；
 *    - 底部一行：出错说明、「取消」「保存」。
 *  规则列表与判定结果的高度随内容变化，由 FillRuleList / SetTryResult 重建并更新高度。
 *  控件树与窗口分开建，便于单元测试按设计尺寸排版。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      EditControls controls;
 *      std::unique_ptr<balloonwjui::DuiVBox> body = flamingobot::BuildEditBody(view, font, controls);
 *      flamingobot::FillRuleList(controls, bot.m_rules, bot.m_ownerType, font);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../Core/BotTypes.h"
#include "BotWidgets.h"

namespace flamingobot {

//---- 窗口尺寸，单位：像素（整窗尺寸，含标题栏，UI 约定第 7 条） ----
const int kEditWindowWidth = 620;    //窗口宽
const int kEditWindowHeight = 680;   //窗口高
//内容区可用的宽度：窗口宽减两侧内边距与滚动条留白
const int kEditContentWidth = kEditWindowWidth - 2 * kBotPadX - kBotScrollGutter;

//---- 控件 id ----
const UINT kIdEditName = 8601;       //名称（DUIN_VALUECHANGED：刷新头像预览）
const UINT kIdEditDesc = 8602;       //简介
const UINT kIdEditGlyph = 8603;      //头像字形（DUIN_VALUECHANGED：刷新头像预览）
const UINT kIdEditColor = 8604;      //头像底色（DUIN_VALUECHANGED：刷新头像预览）
const UINT kIdEditAddRule = 8605;    //「添加规则」
const UINT kIdEditTryText = 8606;    //「试一试」的输入
const UINT kIdEditTryAt = 8607;      //「@ 了机器人」
const UINT kIdEditTryPresence = 8608;//模拟的在线状态
const UINT kIdEditTry = 8609;        //「试一试」
const UINT kIdEditToken = 8610;      //Webhook 令牌（只读）
const UINT kIdEditSecret = 8611;     //签名密钥（只读）
const UINT kIdEditCancel = 8612;     //「取消」
const UINT kIdEditSave = 8613;       //「保存」
const UINT kIdEditWebhookUrl = 8614; //Webhook 对外完整地址（只读）；服务端没有下发时不建
const UINT kIdEditRuleFirst = 9000;  //规则列表第 i 条的按钮：kIdEditRuleFirst + i * kEditRuleIdStride + EditRuleButton
const UINT kIdEditRuleLast = 9199;   //规则按钮 id 的上限（50 条规则 × 4 个按钮）
const UINT kEditRuleIdStride = 4;    //每条规则占的 id 个数

//规则卡片上的按钮（id 偏移）
enum EditRuleButton
{
    edit_rule_edit   = 0,   //编辑：打开规则编辑窗
    edit_rule_up     = 1,   //上移
    edit_rule_down   = 2,   //下移
    edit_rule_delete = 3,   //删除（先确认）
};

//建编辑窗时需要的信息
struct EditView
{
    int          m_ownerType;      //归属：2 群机器人、3 个人机器人
    bool         m_showWebhook;    //是否显示「外部系统推送」一组（群机器人且企业允许外部服务）
    std::wstring m_name;           //名称
    std::wstring m_desc;           //简介
    std::wstring m_glyph;          //头像字形
    std::wstring m_color;          //头像底色 "#RRGGBB"
    std::wstring m_webhookToken;   //Webhook 令牌；空表示还没有（新建的机器人保存后才有）
    std::wstring m_secret;         //签名密钥
    std::wstring m_webhookUrl;     //Webhook 对外完整地址；空表示服务端没有配置，只显示说明文字（§11 Q19）

    EditView()
        : m_ownerType(bot_owner_group)
        , m_showWebhook(false)
    {
    }
};

//建好的控件树里窗口要用到的控件（都归控件树所有，借用）
struct EditControls
{
    ScrollFrameControls        m_frame;      //骨架：可滚动的内容区与底部一行（m_buttons 依次为取消、保存）
    balloonwjui::DuiAvatar*    m_avatar;     //头像预览
    balloonwjui::DuiEdit*      m_name;       //名称
    balloonwjui::DuiEdit*      m_desc;       //简介
    balloonwjui::DuiEdit*      m_glyph;      //头像字形
    balloonwjui::DuiComboBox*  m_color;      //头像底色
    balloonwjui::DuiButton*    m_addRule;    //「添加规则」
    balloonwjui::DuiVBox*      m_rules;      //规则列表
    balloonwjui::DuiEdit*      m_tryText;    //「试一试」的输入
    balloonwjui::DuiButton*    m_tryAt;      //「@ 了机器人」（群机器人）；个人机器人为 NULL
    balloonwjui::DuiComboBox*  m_tryPresence;//模拟的在线状态（个人机器人）；群机器人为 NULL
    balloonwjui::DuiButton*    m_tryButton;  //「试一试」
    balloonwjui::DuiLabel*     m_tryResult;  //判定结果
    balloonwjui::DuiButton*    m_save;       //「保存」

    EditControls();
};

/**
 *  建编辑窗标题栏以下的控件树（规则列表为空，由 FillRuleList 填）。
 *  @param view 基本信息与要显示的分组
 *  @param font 字体（借用）
 *  @param out  [出参] 窗口要用到的控件
 */
std::unique_ptr<balloonwjui::DuiVBox> BuildEditBody(const EditView& view, HFONT font, EditControls& out);

/**
 *  重建规则列表并更新它的高度，然后按 UI 约定第 12 条重排内容区。
 *  @param controls  控件
 *  @param rules     规则，按判定顺序
 *  @param ownerType 归属
 *  @param font      字体（借用）
 */
void FillRuleList(EditControls& controls, const std::vector<BotRule>& rules, int ownerType, HFONT font);

/**
 *  显示判定结果并更新它的高度，然后重排内容区。
 *  @param isError 出错（红字）还是正常的结果
 */
void SetTryResult(EditControls& controls, const std::wstring& text, bool isError, HFONT font);

/**
 *  规则卡片上一个按钮的 id。
 */
UINT EditRuleButtonId(size_t index, int button);

/**
 *  从按钮 id 解出规则下标与按钮。
 *  @return id 落在规则按钮的范围内返回 true
 */
bool ParseEditRuleButtonId(UINT id, size_t& index, int& button);

} // namespace flamingobot
