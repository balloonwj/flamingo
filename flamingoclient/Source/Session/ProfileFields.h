/**
 *  用户资料字段在界面上的显示与校验规则，供联系人资料窗（看同事与看本人两种模式）使用。2026-10-07 之前
 *  本人资料对话框（UI/BalloonMain/BalloonProfileDlg）也用它，该对话框已删除，本人资料改由资料窗承担：
 *    - 性别：协议编码（net/Protocol.h 的 enum profile_gender，0 未设置 / 1 男 / 2 女）与界面文字、
 *      下拉框序号之间的对应（单元测试 PFX4）；
 *    - 本人修改资料时的本地校验：长度、邮箱、座机、生日，规则与服务端
 *      ProfileProtocol::parseUpdateProfileRequest 一致，另加邮箱与座机的格式（单元测试 PFX14）；
 *    - 手机号、座机对同事的公开范围：开关文字、两项菜单、选中后是否提交（单元测试 PFX23、PFX24）。
 *  纯逻辑，不碰窗口，单元测试见 Tests/ProfileFieldsTests.cpp。
 *
 *  典型用法：
 *      label->SetText(profilefields::GenderText(info.gender));
 *      for (int i = 0; i < profilefields::kGenderComboCount; ++i)
 *          combo->AddString(profilefields::GenderText(profilefields::ComboIndexToGender(i)));
 *      combo->SetCurSel(profilefields::GenderToComboIndex(info.gender), false);
 *
 *      const net::ProfileUpdateFields fields = net::ProfileProtocolClient::diffProfileValues(before, after);
 *      const profilefields::ProfileEditError err = profilefields::ValidateProfileUpdate(fields);
 *      if (err != profilefields::profile_edit_ok)
 *          ::MessageBox(hWnd, profilefields::ProfileEditErrorText(err).c_str(), ...);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

#include "../net/ProfileProtocolClient.h"   // net::ProfileUpdateFields

namespace profilefields {

//资料文字的长度上限，按 UTF-8 字符数计（中文、英文都算一个），与服务端 ProfileProtocol.h 的同名常量一致
const int kProfileShortTextMaxChars = 64;    //昵称、岗位、座机
const int kProfileLongTextMaxChars  = 256;   //个性签名、所在地、邮箱

//打开本人资料的入口（docs/联系人资料字段扩充方案.md 4.2 节、第 10 节）
enum SelfProfileSource
{
    self_profile_from_header = 0,        //点主面板左上角头部的本人昵称区
    self_profile_from_system_menu,       //主面板系统菜单里的「个人信息」
    self_profile_from_contact_tree,      //通讯录里双击自己那一行
    self_profile_from_context_menu,      //通讯录右键菜单的「查看资料」选中的是自己
    self_profile_from_search,            //主面板搜索结果里选中的是自己
};

//本人修改资料时本地校验的结果；不合格时界面给出 ProfileEditErrorText 的提示、聚焦对应的输入框、不发请求
enum ProfileEditError
{
    profile_edit_ok = 0,               //本次提交的各项都合格，可以发请求
    profile_edit_nickname_too_long,    //昵称超过 kProfileShortTextMaxChars 个字
    profile_edit_signature_too_long,   //个性签名超过 kProfileLongTextMaxChars 个字
    profile_edit_birthday_invalid,     //生日不是 0（清空），也不是 1900～2100 年间存在的日期（如 2 月 30 日）
    profile_edit_birthday_format,      //生日输入框里的文字不是 YYYY-MM-DD 形式（由 ParseBirthdayText 判定）
    profile_edit_address_too_long,     //所在地超过 kProfileLongTextMaxChars 个字
    profile_edit_mail_too_long,        //邮箱超过 kProfileLongTextMaxChars 个字
    profile_edit_mail_invalid,         //邮箱不为空，但不含「@」
    profile_edit_post_too_long,        //岗位超过 kProfileShortTextMaxChars 个字
    profile_edit_landline_too_long,    //座机超过 kProfileShortTextMaxChars 个字
    profile_edit_landline_invalid,     //座机含数字、空格与 + - ( ) 以外的字符
    profile_edit_nickname_empty,       //昵称被清空或只剩空白（2026-10-07 起资料窗里可改昵称，昵称是同事看到的姓名，不允许为空）
};

//性别下拉框的项数：保密、男、女
const int kGenderComboCount = 3;

//公开范围开关要设置的号码（2026-10-06，docs/联系人资料字段扩充方案.md 第 9 节）
enum VisibilityTarget
{
    visibility_target_phone    = 0,   //手机号：改资料请求里的 phonevisible
    visibility_target_landline = 1,   //座机：改资料请求里的 landlinevisible
};

//公开范围菜单的命令 id。DuiMenu::TrackPopup 没有选中任何项时返回 0，故从 1 起
enum VisibilityMenuId
{
    kVisibilityMenuIdSelfOnly   = 1,   //菜单项「仅自己可见」
    kVisibilityMenuIdColleagues = 2,   //菜单项「对同事可见」
};

//公开范围菜单里的一项
struct VisibilityMenuEntry
{
    unsigned int m_id;       //命令 id，取值见 enum VisibilityMenuId
    std::wstring m_label;    //菜单文字，已按当前界面语言翻译
    bool         m_checked;  //是否打勾：与当前取值相同的那一项打勾
};

/**
 *  性别编码对应的界面文字，已按当前界面语言翻译。
 *  @param gender 性别编码；1 男、2 女，0（未设置）与其它不认识的值一律显示「保密」
 *  @return 译文字符串，归语言包或本模块的字面量所有，进程结束前有效
 */
const wchar_t* GenderText(int gender);

/**
 *  性别编码换成下拉框序号。下拉框的顺序是保密、男、女，与编码 0 / 1 / 2 相同。
 *  @param gender 性别编码；不认识的值按「保密」处理
 *  @return 下拉框序号，0 ~ kGenderComboCount - 1
 */
int GenderToComboIndex(int gender);

/**
 *  下拉框序号换成性别编码，与 GenderToComboIndex 互逆。
 *  @param index 下拉框序号；越界（含没有选中时的 -1）按「保密」处理
 *  @return 性别编码，取值见 enum profile_gender
 */
int ComboIndexToGender(int index);

/**
 *  公开范围的界面文字，已按当前界面语言翻译。
 *  @param visibility 取值见 net/Protocol.h 的 enum profile_visibility；1 为「对同事可见」，0 与其它不认识的值
 *                    一律为「仅自己可见」（与服务端隐藏号码的口径一致：只有明确公开的号码才发给同事）
 *  @return 译文字符串，归语言包或本模块的字面量所有，进程结束前有效
 */
const wchar_t* VisibilityText(int visibility);

/**
 *  公开范围开关上显示的文字：VisibilityText 后加一个下拉箭头，如「仅自己可见 ▾」，提示点开有菜单。
 *  @param visibility 取值见 enum profile_visibility
 *  @return 显示文字
 */
std::wstring VisibilityLinkText(int visibility);

/**
 *  公开范围菜单的两项：「仅自己可见」「对同事可见」，与当前取值相同的那一项打勾。
 *  @param current 当前取值；不认识的值按「仅自己可见」打勾
 *  @return 两项，顺序即菜单上的顺序
 */
std::vector<VisibilityMenuEntry> BuildVisibilityMenu(int current);

/**
 *  菜单命令 id 换成公开范围取值。
 *  @param id DuiMenu::TrackPopup 的返回值
 *  @return 取值见 enum profile_visibility；0（没有选）与不认识的 id 返回 -1
 */
int VisibilityFromMenuId(unsigned int id);

/**
 *  在公开范围菜单里选了一项之后的判定（纯函数）：没有选、或选的就是当前取值时不提交；否则组装
 *  只含这一项的改资料请求。
 *  @param target       要设置的号码，取值见 enum VisibilityTarget
 *  @param current      当前取值；不认识的值按「仅自己可见」比较
 *  @param chosenMenuId DuiMenu::TrackPopup 的返回值
 *  @param outFields    [出参] 返回 true 时是要提交的字段：只标记 phonevisible 或 landlinevisible 一项
 *  @return 需要提交返回 true
 */
bool DecideVisibilityChange(int target, int current, unsigned int chosenMenuId, net::ProfileUpdateFields& outFields);

/**
 *  从某个入口看本人资料时是否打开联系人资料窗（看本人模式）。2026-10-07 起五个入口（主面板头部昵称区、
 *  系统菜单「个人信息」、通讯录双击、右键「查看资料」、搜索选中）都打开联系人资料窗，本人资料对话框已删除。
 *  @param source 入口，取值见 enum SelfProfileSource
 *  @return 认识的入口返回 true；不认识的入口返回 false，调用方记日志后不打开任何窗口
 */
bool SelfProfileOpensContactWindow(int source);

/**
 *  UTF-8 文字的字符数（码点数），与服务端计算长度上限的口径相同。
 *  @param text UTF-8 文字
 *  @return 字符数；凡不是续字节（10xxxxxx）的字节各算一个字符
 */
size_t Utf8CharCount(const std::string& text);

/**
 *  生日是否合法，规则与服务端相同：0 表示清空；否则须是 1900～2100 年间存在的 YYYYMMDD 日期（含闰年二月）。
 *  @param birthday YYYYMMDD 形式的整数
 *  @return 合法返回 true
 */
bool IsValidBirthday(int birthday);

/**
 *  校验一次改资料请求里要提交的各项。只查 m_hasXxx 为 true 的项：没有改动的字段即使是库里遗留的
 *  超长旧值也不拦。按昵称、签名、性别以外的字段在界面上的顺序逐项检查，返回第一处不合格。
 *  昵称另有一条：提交的昵称为空或只有空白时不合格（profile_edit_nickname_empty）。
 *  @param fields 要提交的字段，通常由 ProfileProtocolClient::diffProfileValues 得出
 *  @return 全部合格返回 profile_edit_ok，否则为第一处不合格的原因
 */
ProfileEditError ValidateProfileUpdate(const net::ProfileUpdateFields& fields);

/**
 *  生日的显示文字：YYYYMMDD 换成「YYYY-MM-DD」。只用于显示，比 IsValidBirthday 宽松（年 1900～9999、
 *  月 1～12、日 1～31），库里遗留的不严格的值也照样显示出来。
 *  @param birthday YYYYMMDD 形式的整数
 *  @return 显示文字；0 与明显不是日期的值返回空串，按没填处理
 */
std::wstring FormatBirthdayText(int birthday);

/**
 *  把生日输入框里的文字解析成 YYYYMMDD 整数（FormatBirthdayText 的逆操作）。先去掉首尾空白：
 *  空串返回 0（清空生日）；须严格是「YYYY-MM-DD」，年 1900～9999、月 1～12、日 1～31，否则返回 -1。
 *  是否为存在的日期（如 2 月 30 日）由 ValidateProfileUpdate 另行检查。
 *  @param text 输入框里的文字
 *  @return YYYYMMDD 整数；0 表示清空；-1 表示格式不对
 */
int ParseBirthdayText(const std::wstring& text);

/**
 *  校验结果对应的提示文字，已按当前界面语言翻译，带长度上限的提示里写明具体字数。
 *  @param error 校验结果；profile_edit_ok 返回空串
 *  @return 提示文字
 */
std::wstring ProfileEditErrorText(ProfileEditError error);

} // namespace profilefields
