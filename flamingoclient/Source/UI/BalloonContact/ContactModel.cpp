/**
 *  ContactModel.cpp
 *
 *  联系人资料对话框数据模型的实现。当前以 mock 数据初始化，对照设计稿
 *  dialogs.jsx 的 ContactInfoDialog —— remark=「设计组组长」、
 *  name=「江屿白」那一组。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "ContactModel.h"
#include "../../net/Protocol.h"   // notify_mode_allow：提醒模式的默认值；enum profile_gender
#include "../../Session/ProfileFields.h"   // 个人资料区块：性别文字、生日格式
#include "../../Common/I18n/I18n.h"        // 个人资料区块：TR，界面文字的多语言查找

namespace ballooncontact {

namespace {

// i18n-ignore-begin: 模型的模拟初始数据，接入真实数据后由调用方覆盖，不显示给用户
// mock 联系人「江屿白」的两个初始标签（设计稿 useState(['朋友','设计'])）。
const TCHAR* const kMockTags[] =
{
    _T("朋友"),
    _T("设计"),
};
const int kMockTagCount = sizeof(kMockTags) / sizeof(kMockTags[0]);

// mock 分组列表（设计稿用 CONTACT_GROUPS，此处给一组等价的中文分组名）。
const TCHAR* const kMockGroups[] =
{
    _T("我的好友"),
    _T("产品部 · 设计组"),
    _T("同事"),
    _T("家人"),
};
const int kMockGroupCount = sizeof(kMockGroups) / sizeof(kMockGroups[0]);

// mock 默认选中的分组下标 —— 对应「产品部 · 设计组」（见 kMockGroups[1]）。
const int kMockDefaultGroup = 1;

// mock 联系人的生日（YYYYMMDD），与方案文档 4.2 节的示意图一致。
const int kMockBirthday = 19960812;

} // anonymous namespace

ContactModel::ContactModel()
    : m_name(_T("江屿白"))
    , m_remark(_T("设计组组长"))
    , m_account(_T("10923847"))
    , m_region(_T("北京"))
    , m_title(_T("设计组组长"))
    , m_dept(_T("产品部 / 设计组"))
    , m_signature(_T("在出差路上…"))
// i18n-ignore-end
    , m_status(kContactStatusAway)        // 设计稿头像状态点为 away（橙）
    , m_avatarGray(false)
    , m_gender(profile_gender_female)
    , m_birthday(kMockBirthday)
    , m_mail(_T("jiang@flamingo.im"))
    , m_mobile(_T("13800000000"))
    , m_landline(_T("010-8888 6666"))
    , m_post(_T("交互设计师"))   //i18n-ignore: 模型的模拟初始数据，接入真实数据后由调用方覆盖
    , m_phoneVisible(profile_visibility_self_only)
    , m_landlineVisible(profile_visibility_colleagues)
    , m_self(false)
    , m_selectedGroup(kMockDefaultGroup)
    , m_notifyMode(notify_mode_allow)
{
    // 初始标签。
    for (int i = 0; i < kMockTagCount; ++i)
    {
        m_tags.push_back(CString(kMockTags[i]));
    }

    // 可选分组。
    for (int i = 0; i < kMockGroupCount; ++i)
    {
        m_groups.push_back(CString(kMockGroups[i]));
    }

    // 开关默认全关。
    for (int i = 0; i < kContactSwitchCount; ++i)
    {
        m_switches[i] = false;
    }
}

bool ContactModel::IsValidSwitch(int kind)
{
    return kind >= 0 && kind < kContactSwitchCount;
}

// ---- 真实数据注入 ----

void ContactModel::SetBasicInfo(const CString& name, const CString& account,
                                const CString& region, const CString& title,
                                const CString& dept, const CString& signature,
                                int status)
{
    m_name      = name;
    m_account   = account;
    m_region    = region;
    m_title     = title;
    m_dept      = dept;
    m_signature = signature;
    m_status    = status;
    // 真实数据暂无独立备注名，备注名清空（窗口会回退用真名显示）。
    m_remark.Empty();
    // 个人资料区块的字段与「是否本人」一并复位为空，由调用方另经 SetProfileFields / SetSelf 设入；
    // 否则没有调用这两个函数的调用方会显示构造函数里的模拟数据。
    m_gender = profile_gender_unset;
    m_birthday = 0;
    m_mail.Empty();
    m_mobile.Empty();
    m_landline.Empty();
    m_post.Empty();
    m_phoneVisible = profile_visibility_self_only;
    m_landlineVisible = profile_visibility_colleagues;
    m_self = false;
    // 昵称原值与头像来源同样复位，由调用方另经 SetNickname / SetAvatarSource 设入（2026-10-07）
    m_nickname.Empty();
    m_avatarSource = ContactAvatarSource();
}

void ContactModel::SetProfileFields(int gender, int birthday, const CString& mail, const CString& mobile,
                                    const CString& landline, const CString& post)
{
    m_gender   = gender;
    m_birthday = birthday;
    m_mail     = mail;
    m_mobile   = mobile;
    m_landline = landline;
    m_post     = post;
}

int ContactModel::Gender() const
{
    return m_gender;
}

int ContactModel::Birthday() const
{
    return m_birthday;
}

const CString& ContactModel::Mail() const
{
    return m_mail;
}

const CString& ContactModel::Mobile() const
{
    return m_mobile;
}

const CString& ContactModel::Landline() const
{
    return m_landline;
}

const CString& ContactModel::Post() const
{
    return m_post;
}

void ContactModel::SetProfileVisibility(int phoneVisible, int landlineVisible)
{
    m_phoneVisible = phoneVisible;
    m_landlineVisible = landlineVisible;
}

int ContactModel::PhoneVisible() const
{
    return m_phoneVisible;
}

int ContactModel::LandlineVisible() const
{
    return m_landlineVisible;
}

void ContactModel::SetSelf(bool self)
{
    m_self = self;
}

bool ContactModel::IsSelf() const
{
    return m_self;
}

void ContactModel::SetNickname(const CString& nickname)
{
    m_nickname = nickname;
}

const CString& ContactModel::Nickname() const
{
    return m_nickname;
}

void ContactModel::SetAvatarSource(const ContactAvatarSource& source)
{
    m_avatarSource = source;
}

const ContactAvatarSource& ContactModel::AvatarSource() const
{
    return m_avatarSource;
}

bool SameAvatarSource(const ContactAvatarSource& a, const ContactAvatarSource& b)
{
    return a.m_valid == b.m_valid && a.m_glyph == b.m_glyph && a.m_color == b.m_color
        && a.m_imagePath == b.m_imagePath;
}

void ContactModel::ApplyProfileUpdate(const net::ProfileUpdateFields& fields)
{
    const net::ProfileEditableValues& v = fields.m_values;
    if (fields.m_hasNickname)
    {
        m_nickname = CA2W(v.m_nickname.c_str(), CP_UTF8);
        // 头部与标题用的姓名随之更新；昵称为空时与主面板的取名口径一致，退回账号
        CString trimmed = m_nickname;
        trimmed.Trim();
        m_name = trimmed.IsEmpty() ? m_account : m_nickname;
    }
    if (fields.m_hasSignature)
        m_signature = CA2W(v.m_signature.c_str(), CP_UTF8);
    if (fields.m_hasGender)
        m_gender = v.m_gender;
    if (fields.m_hasBirthday)
        m_birthday = v.m_birthday;
    if (fields.m_hasAddress)
        m_region = CA2W(v.m_address.c_str(), CP_UTF8);
    if (fields.m_hasMail)
        m_mail = CA2W(v.m_mail.c_str(), CP_UTF8);
    if (fields.m_hasLandline)
        m_landline = CA2W(v.m_landline.c_str(), CP_UTF8);
    if (fields.m_hasPost)
        m_post = CA2W(v.m_post.c_str(), CP_UTF8);
    if (fields.m_hasPhoneVisible)
        m_phoneVisible = v.m_phoneVisible;
    if (fields.m_hasLandlineVisible)
        m_landlineVisible = v.m_landlineVisible;
}

void ContactModel::ClearTags()
{
    m_tags.clear();
}

void ContactModel::SetGroups(const std::vector<CString>& groups, int selected)
{
    m_groups = groups;
    if (selected < 0 || selected >= (int)m_groups.size())
    {
        selected = 0;
    }
    m_selectedGroup = selected;
}

// ---- 只读资料 ----

const CString& ContactModel::Name() const
{
    return m_name;
}

const CString& ContactModel::Remark() const
{
    return m_remark;
}

void ContactModel::SetRemark(LPCTSTR remark)
{
    m_remark = (remark != NULL) ? remark : _T("");
}

const CString& ContactModel::Account() const
{
    return m_account;
}

const CString& ContactModel::Region() const
{
    return m_region;
}

const CString& ContactModel::Title() const
{
    return m_title;
}

const CString& ContactModel::Dept() const
{
    return m_dept;
}

const CString& ContactModel::Signature() const
{
    return m_signature;
}

int ContactModel::Status() const
{
    return m_status;
}

void ContactModel::SetStatus(int status)
{
    m_status = status;
}

void ContactModel::SetAvatarGray(bool gray)
{
    m_avatarGray = gray;
}

bool ContactModel::IsAvatarGray() const
{
    return m_avatarGray;
}

CString ContactModel::AvatarLetter() const
{
    // 头像占位取真名首字。真名为空时返回空串。
    if (m_name.IsEmpty())
    {
        return CString();
    }
    return m_name.Left(1);
}

// ---- 标签 ----

int ContactModel::TagCount() const
{
    return (int)m_tags.size();
}

CString ContactModel::Tag(int index) const
{
    if (index < 0 || index >= (int)m_tags.size())
    {
        return CString();
    }
    return m_tags[index];
}

const std::vector<CString>& ContactModel::Tags() const
{
    return m_tags;
}

void ContactModel::SetTags(const std::vector<CString>& tags)
{
    m_tags = tags;
}

bool ContactModel::AddTag(LPCTSTR tag)
{
    if (tag == NULL)
    {
        return false;
    }

    // 去首尾空白后为空则忽略 —— 对应设计稿 newTag.trim() 判空。
    CString trimmed(tag);
    trimmed.Trim();
    if (trimmed.IsEmpty())
    {
        return false;
    }

    m_tags.push_back(trimmed);
    return true;
}

void ContactModel::RemoveTag(int index)
{
    if (index < 0 || index >= (int)m_tags.size())
    {
        return;
    }
    m_tags.erase(m_tags.begin() + index);
}

// ---- 分组 ----

int ContactModel::GroupCount() const
{
    return (int)m_groups.size();
}

CString ContactModel::Group(int index) const
{
    if (index < 0 || index >= (int)m_groups.size())
    {
        return CString();
    }
    return m_groups[index];
}

int ContactModel::SelectedGroup() const
{
    return m_selectedGroup;
}

void ContactModel::SetSelectedGroup(int index)
{
    if (index >= 0 && index < (int)m_groups.size())
    {
        m_selectedGroup = index;
    }
}

// ---- 开关 ----

bool ContactModel::Switch(int kind) const
{
    if (!IsValidSwitch(kind))
    {
        return false;
    }
    return m_switches[kind];
}

void ContactModel::SetSwitch(int kind, bool on)
{
    if (IsValidSwitch(kind))
    {
        m_switches[kind] = on;
    }
}

// ---- 消息提醒 ----

int ContactModel::NotifyMode() const
{
    return m_notifyMode;
}

void ContactModel::SetNotifyMode(int mode)
{
    m_notifyMode = mode;
}

int ContactStatusFromPresence(int presence)
{
    // 资料窗的状态点只区分在线与离线（2026-10-03 之前写作 presence > 0，越界的 4 会被当成在线，
    // 与通讯录把越界值归为离线的口径不一致，故改为逐个列出三种在线状态）
    if (presence == presence_online || presence == presence_away || presence == presence_busy)
    {
        return kContactStatusOnline;
    }
    return kContactStatusOffline;
}

// =====================================================================
// 「个人资料」区块的格子数据（2026-10-04）
// =====================================================================

namespace {

//字段名，下标即 ContactProfileField；只做标记，使用处再经 TR 取当前语言的译文
const wchar_t* const kFieldLabels[kContactProfileFieldCount] =
{
    TR_NOOP(L"昵称"),
    TR_NOOP(L"个性签名"),
    TR_NOOP(L"性别"),
    TR_NOOP(L"生日"),
    TR_NOOP(L"所在地"),
    TR_NOOP(L"岗位"),
    TR_NOOP(L"邮箱"),
    TR_NOOP(L"座机"),
    TR_NOOP(L"手机号"),
};

//取某个字段要显示的原始文字；字段没有填写时返回空串
CString FieldValueText(const ContactModel& model, int field)
{
    switch (field)
    {
    //昵称：只在看本人时列出（看别人时姓名已在头部，返回空串即不显示）
    case kProfileFieldNickname:
        if (!model.IsSelf())
            return CString();
        return model.Nickname();

    //个性签名
    case kProfileFieldSignature:
        return model.Signature();

    //性别：未设置时看本人显示「保密」，看别人算没填（返回空串，格子不显示）
    case kProfileFieldGender:
        if (model.Gender() == profile_gender_unset && !model.IsSelf())
            return CString();
        return CString(profilefields::GenderText(model.Gender()));

    //生日：0 或不像日期的值算没填
    case kProfileFieldBirthday:
        return CString(profilefields::FormatBirthdayText(model.Birthday()).c_str());

    //所在地（模型里沿用 Region）
    case kProfileFieldAddress:
        return model.Region();

    //岗位
    case kProfileFieldPost:
        return model.Post();

    //邮箱
    case kProfileFieldMail:
        return model.Mail();

    //座机
    case kProfileFieldLandline:
        return model.Landline();

    //手机号
    case kProfileFieldMobile:
        return model.Mobile();

    //不认识的字段：当作没填
    default:
        return CString();
    }
}

} // namespace

std::vector<ContactProfileCell> BuildContactProfileCells(const ContactModel& model)
{
    std::vector<ContactProfileCell> cells;
    const bool self = model.IsSelf();
    for (int field = 0; field < kContactProfileFieldCount; ++field)
    {
        CString value = FieldValueText(model, field);
        value.Trim();
        const bool empty = value.IsEmpty();
        //看别人：没填的字段不显示，后面的格子按顺序前移
        if (empty && !self)
            continue;

        ContactProfileCell cell;
        cell.m_field = field;
        cell.m_label = TR(kFieldLabels[field]);
        cell.m_isEmpty = empty;
        cell.m_value = empty ? CString(TR(L"未填写")) : value;
        //手机号是注册时唯一、找回密码的凭据，本人也不能修改
        cell.m_editable = self && field != kProfileFieldMobile;
        //看本人时手机号、座机两格带公开范围开关；号码没填时也带，设置对日后填上的号码同样有效
        if (self && field == kProfileFieldMobile)
        {
            cell.m_hasVisibility = true;
            cell.m_visibility = model.PhoneVisible();
        }
        else if (self && field == kProfileFieldLandline)
        {
            cell.m_hasVisibility = true;
            cell.m_visibility = model.LandlineVisible();
        }
        cells.push_back(cell);
    }
    return cells;
}

bool IsFullRowProfileField(int field)
{
    return field == kProfileFieldNickname || field == kProfileFieldSignature;
}

int ContactProfileRowCount(const std::vector<ContactProfileCell>& cells)
{
    //昵称、个性签名各独占一行
    int fullRows = 0;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (IsFullRowProfileField(cells[i].m_field))
            ++fullRows;
    }
    const int gridCells = (int)cells.size() - fullRows;
    //其余每两个一行，单数时最后一行只有一个
    const int kColumns = 2;
    const int gridRows = (gridCells + kColumns - 1) / kColumns;
    return fullRows + gridRows;
}

CString ProfileCellEditText(const ContactModel& model, int field)
{
    switch (field)
    {
    //生日：输入框里是 YYYY-MM-DD，没填为空串
    case kProfileFieldBirthday:
        return CString(profilefields::FormatBirthdayText(model.Birthday()).c_str());

    //性别用下拉框，没有文字；其余字段取原值（没填的本来就是空串）
    case kProfileFieldGender:
        return CString();

    default:
        return FieldValueText(model, field);
    }
}

namespace {

//宽字符串去掉首尾空白后转成 UTF-8
std::string TrimmedUtf8(const CString& text)
{
    CString s = text;
    s.Trim();
    return std::string(CW2A(s.GetString(), CP_UTF8));
}

//模型里某个文字字段的原值（UTF-8，去掉首尾空白，与新值同口径比较）
std::string OriginalUtf8(const ContactModel& model, int field)
{
    return TrimmedUtf8(FieldValueText(model, field));
}

} // namespace

ProfileCellCommitResult DecideProfileCellCommit(const ContactModel& model, int field, const CString& text,
                                                int genderIndex, net::ProfileUpdateFields& outFields,
                                                profilefields::ProfileEditError& outError)
{
    outFields = net::ProfileUpdateFields();
    outError = profilefields::profile_edit_ok;
    net::ProfileEditableValues& v = outFields.m_values;
    switch (field)
    {
    //昵称：只有看本人时可改；清空与超长由 ValidateProfileUpdate 判为不合格
    case kProfileFieldNickname:
        if (!model.IsSelf())
            return profile_cell_unchanged;
        v.m_nickname = TrimmedUtf8(text);
        outFields.m_hasNickname = (v.m_nickname != OriginalUtf8(model, field));
        break;

    //个性签名
    case kProfileFieldSignature:
        v.m_signature = TrimmedUtf8(text);
        outFields.m_hasSignature = (v.m_signature != OriginalUtf8(model, field));
        break;

    //性别：取下拉框序号对应的编码
    case kProfileFieldGender:
        v.m_gender = profilefields::ComboIndexToGender(genderIndex);
        outFields.m_hasGender = (v.m_gender != model.Gender());
        break;

    //生日：格式不对直接判为不合格，不再比较
    case kProfileFieldBirthday:
        {
            const int birthday = profilefields::ParseBirthdayText(std::wstring(text.GetString()));
            if (birthday < 0)
            {
                outError = profilefields::profile_edit_birthday_format;
                return profile_cell_invalid;
            }
            v.m_birthday = birthday;
            outFields.m_hasBirthday = (birthday != model.Birthday());
        }
        break;

    //所在地
    case kProfileFieldAddress:
        v.m_address = TrimmedUtf8(text);
        outFields.m_hasAddress = (v.m_address != OriginalUtf8(model, field));
        break;

    //岗位
    case kProfileFieldPost:
        v.m_post = TrimmedUtf8(text);
        outFields.m_hasPost = (v.m_post != OriginalUtf8(model, field));
        break;

    //邮箱
    case kProfileFieldMail:
        v.m_mail = TrimmedUtf8(text);
        outFields.m_hasMail = (v.m_mail != OriginalUtf8(model, field));
        break;

    //座机
    case kProfileFieldLandline:
        v.m_landline = TrimmedUtf8(text);
        outFields.m_hasLandline = (v.m_landline != OriginalUtf8(model, field));
        break;

    //手机号与不认识的字段：不可修改
    default:
        return profile_cell_unchanged;
    }

    if (outFields.IsEmpty())
        return profile_cell_unchanged;
    outError = profilefields::ValidateProfileUpdate(outFields);
    return (outError == profilefields::profile_edit_ok) ? profile_cell_submit : profile_cell_invalid;
}

} // namespace ballooncontact
