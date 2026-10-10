/**
 *  用户资料字段在界面上的显示规则的实现，ProfileFields.cpp
 *  balloonwj@qq.com   2026-10-04
 */
#include "ProfileFields.h"
#include <stdio.h>                    // swprintf_s
#include "../net/Protocol.h"          // enum profile_gender
#include "../Common/I18n/I18n.h"      // TR：界面文字的多语言查找

namespace profilefields {

//下拉框各项对应的性别编码，下标即下拉框序号：保密、男、女（docs/联系人资料字段扩充方案.md 4.3 节）
static const int kComboGenders[kGenderComboCount] =
{
    profile_gender_unset,
    profile_gender_male,
    profile_gender_female,
};

const wchar_t* GenderText(int gender)
{
    switch (gender)
    {
    //男
    case profile_gender_male:
        return TR(L"男");

    //女
    case profile_gender_female:
        return TR(L"女");

    //未设置，以及不认识的取值（如 2026-10-04 之前客户端按旧约定写入的值之外的异常值）：显示「保密」
    default:
        return TR(L"保密");
    }
}

int GenderToComboIndex(int gender)
{
    for (int i = 0; i < kGenderComboCount; ++i)
    {
        if (kComboGenders[i] == gender)
            return i;
    }
    //不认识的值选中「保密」那一项
    return GenderToComboIndex(profile_gender_unset);
}

int ComboIndexToGender(int index)
{
    if (index < 0 || index >= kGenderComboCount)
        return profile_gender_unset;
    return kComboGenders[index];
}

//公开范围开关文字末尾的下拉箭头，提示点开有菜单（与屏幕共享工具条「暂无观看者 ▾」同一写法）
static const wchar_t* const kVisibilityDropMark = L" \x25BE";

const wchar_t* VisibilityText(int visibility)
{
    //只有明确设为「对同事可见」才显示它，其余取值按不公开显示，与服务端隐藏号码的口径一致
    if (visibility == profile_visibility_colleagues)
        return TR(L"对同事可见");
    return TR(L"仅自己可见");
}

std::wstring VisibilityLinkText(int visibility)
{
    return std::wstring(VisibilityText(visibility)) + kVisibilityDropMark;
}

std::vector<VisibilityMenuEntry> BuildVisibilityMenu(int current)
{
    const bool colleagues = (current == profile_visibility_colleagues);
    std::vector<VisibilityMenuEntry> entries;
    VisibilityMenuEntry selfOnly;
    selfOnly.m_id = kVisibilityMenuIdSelfOnly;
    selfOnly.m_label = VisibilityText(profile_visibility_self_only);
    selfOnly.m_checked = !colleagues;
    entries.push_back(selfOnly);
    VisibilityMenuEntry toColleagues;
    toColleagues.m_id = kVisibilityMenuIdColleagues;
    toColleagues.m_label = VisibilityText(profile_visibility_colleagues);
    toColleagues.m_checked = colleagues;
    entries.push_back(toColleagues);
    return entries;
}

int VisibilityFromMenuId(unsigned int id)
{
    switch (id)
    {
    //菜单项「仅自己可见」
    case kVisibilityMenuIdSelfOnly:
        return profile_visibility_self_only;

    //菜单项「对同事可见」
    case kVisibilityMenuIdColleagues:
        return profile_visibility_colleagues;

    //没有选（菜单被取消时 TrackPopup 返回 0）或不认识的 id
    default:
        return -1;
    }
}

bool DecideVisibilityChange(int target, int current, unsigned int chosenMenuId, net::ProfileUpdateFields& outFields)
{
    const int chosen = VisibilityFromMenuId(chosenMenuId);
    if (chosen < 0)
        return false;
    //不认识的当前值按「仅自己可见」比较，与菜单上打勾的那一项一致
    const int normalized = (current == profile_visibility_colleagues)
        ? profile_visibility_colleagues : profile_visibility_self_only;
    if (chosen == normalized)
        return false;

    outFields = net::ProfileUpdateFields();
    if (target == visibility_target_phone)
    {
        outFields.m_hasPhoneVisible = true;
        outFields.m_values.m_phoneVisible = chosen;
        return true;
    }
    if (target == visibility_target_landline)
    {
        outFields.m_hasLandlineVisible = true;
        outFields.m_values.m_landlineVisible = chosen;
        return true;
    }
    //不认识的号码：不提交
    return false;
}

bool SelfProfileOpensContactWindow(int source)
{
    switch (source)
    {
    //主面板头部昵称区、系统菜单「个人信息」、通讯录双击、右键「查看资料」、搜索选中：都打开联系人资料窗
    //（2026-10-07 起本人资料对话框删除，五个入口统一）
    case self_profile_from_header:
    case self_profile_from_system_menu:
    case self_profile_from_contact_tree:
    case self_profile_from_context_menu:
    case self_profile_from_search:
        return true;

    //不认识的入口：不打开任何窗口，由调用方记日志
    default:
        return false;
    }
}

size_t Utf8CharCount(const std::string& text)
{
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xC0) != 0x80)
            ++count;
    }
    return count;
}

//生日的年份范围，与服务端 ProfileProtocol.cpp 相同：早于 1900 年或晚于 2100 年的视为填错
static const int kBirthdayMinYear = 1900;
static const int kBirthdayMaxYear = 2100;

bool IsValidBirthday(int birthday)
{
    if (birthday == 0)
        return true;
    const int kYearFactor = 10000;
    const int kMonthFactor = 100;
    const int kMonthsPerYear = 12;
    const int year = birthday / kYearFactor;
    const int month = (birthday / kMonthFactor) % kMonthFactor;
    const int day = birthday % kMonthFactor;
    if (year < kBirthdayMinYear || year > kBirthdayMaxYear)
        return false;
    if (month < 1 || month > kMonthsPerYear)
        return false;
    //各月天数，下标为月份减一；二月按平年，闰年另加一天
    static const int kDaysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const int kFebruary = 2;
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    int maxDay = kDaysInMonth[month - 1];
    if (month == kFebruary && leap)
        ++maxDay;
    return day >= 1 && day <= maxDay;
}

//生日显示时接受的最大年份；比校验宽松，只挡掉明显不是日期的值
static const int kBirthdayDisplayMaxYear = 9999;
//一个月最多的天数
static const int kMaxDaysInMonth = 31;
//格式化生日文字的缓冲区长度：「YYYY-MM-DD」10 个字符加结尾的 0，留出余量
static const int kBirthdayTextBufLen = 16;

std::wstring FormatBirthdayText(int birthday)
{
    if (birthday <= 0)
        return std::wstring();
    const int kYearFactor = 10000;
    const int kMonthFactor = 100;
    const int kMonthsPerYear = 12;
    const int year = birthday / kYearFactor;
    const int month = (birthday / kMonthFactor) % kMonthFactor;
    const int day = birthday % kMonthFactor;
    if (year < kBirthdayMinYear || year > kBirthdayDisplayMaxYear || month < 1 || month > kMonthsPerYear
        || day < 1 || day > kMaxDaysInMonth)
        return std::wstring();
    wchar_t buf[kBirthdayTextBufLen] = { 0 };
    swprintf_s(buf, kBirthdayTextBufLen, L"%04d-%02d-%02d", year, month, day);
    return std::wstring(buf);
}

//「YYYY-MM-DD」的总长度与两个分隔符的位置
static const size_t kBirthdayTextLen = 10;
static const size_t kBirthdayFirstDash = 4;
static const size_t kBirthdaySecondDash = 7;
static const size_t kBirthdayYearDigits = 4;
static const size_t kBirthdayMonthDigits = 2;

//从 text[start] 起读 count 个十进制数字；遇到非数字返回 -1
static int ParseFixedDigits(const std::wstring& text, size_t start, size_t count)
{
    const int kDecimal = 10;
    int value = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const wchar_t c = text[start + i];
        if (c < L'0' || c > L'9')
            return -1;
        value = value * kDecimal + (int)(c - L'0');
    }
    return value;
}

int ParseBirthdayText(const std::wstring& text)
{
    //去掉首尾空白（空格、制表符、换行）
    const wchar_t* const kSpaces = L" \t\r\n";
    const size_t first = text.find_first_not_of(kSpaces);
    if (first == std::wstring::npos)
        return 0;
    const size_t last = text.find_last_not_of(kSpaces);
    const std::wstring s = text.substr(first, last - first + 1);
    if (s.size() != kBirthdayTextLen || s[kBirthdayFirstDash] != L'-' || s[kBirthdaySecondDash] != L'-')
        return -1;
    const int year = ParseFixedDigits(s, 0, kBirthdayYearDigits);
    const int month = ParseFixedDigits(s, kBirthdayFirstDash + 1, kBirthdayMonthDigits);
    const int day = ParseFixedDigits(s, kBirthdaySecondDash + 1, kBirthdayMonthDigits);
    const int kMonthsPerYear = 12;
    if (year < kBirthdayMinYear || year > kBirthdayDisplayMaxYear || month < 1 || month > kMonthsPerYear
        || day < 1 || day > kMaxDaysInMonth)
        return -1;
    const int kYearFactor = 10000;
    const int kMonthFactor = 100;
    return year * kYearFactor + month * kMonthFactor + day;
}

//文字是否为空或只有空白（空格、制表符、回车、换行）。按字节检查即可：这几个空白字符都是 ASCII，
//UTF-8 里任何多字节字符的各个字节都不会与它们相同
static bool IsBlankUtf8(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            return false;
    }
    return true;
}

//座机里允许出现的字符：数字、空格与 + - ( )
static bool IsLandlineChar(char c)
{
    if (c >= '0' && c <= '9')
        return true;
    return c == ' ' || c == '+' || c == '-' || c == '(' || c == ')';
}

ProfileEditError ValidateProfileUpdate(const net::ProfileUpdateFields& fields)
{
    const net::ProfileEditableValues& v = fields.m_values;
    const size_t kShortMax = static_cast<size_t>(kProfileShortTextMaxChars);
    const size_t kLongMax = static_cast<size_t>(kProfileLongTextMaxChars);

    if (fields.m_hasNickname && IsBlankUtf8(v.m_nickname))
        return profile_edit_nickname_empty;
    if (fields.m_hasNickname && Utf8CharCount(v.m_nickname) > kShortMax)
        return profile_edit_nickname_too_long;
    if (fields.m_hasSignature && Utf8CharCount(v.m_signature) > kLongMax)
        return profile_edit_signature_too_long;
    if (fields.m_hasBirthday && !IsValidBirthday(v.m_birthday))
        return profile_edit_birthday_invalid;
    if (fields.m_hasAddress && Utf8CharCount(v.m_address) > kLongMax)
        return profile_edit_address_too_long;
    if (fields.m_hasMail)
    {
        if (Utf8CharCount(v.m_mail) > kLongMax)
            return profile_edit_mail_too_long;
        //只要求含「@」；清空邮箱（空串）是允许的
        if (!v.m_mail.empty() && v.m_mail.find('@') == std::string::npos)
            return profile_edit_mail_invalid;
    }
    if (fields.m_hasPost && Utf8CharCount(v.m_post) > kShortMax)
        return profile_edit_post_too_long;
    if (fields.m_hasLandline)
    {
        if (Utf8CharCount(v.m_landline) > kShortMax)
            return profile_edit_landline_too_long;
        //按字节逐个检查：非 ASCII 字符（如全角数字）的每个字节都不在允许范围内
        for (size_t i = 0; i < v.m_landline.size(); ++i)
        {
            if (!IsLandlineChar(v.m_landline[i]))
                return profile_edit_landline_invalid;
        }
    }
    return profile_edit_ok;
}

std::wstring ProfileEditErrorText(ProfileEditError error)
{
    switch (error)
    {
    //昵称超长
    case profile_edit_nickname_too_long:
        return TRF(L"昵称不能超过 {0} 个字。", kProfileShortTextMaxChars);

    //个性签名超长
    case profile_edit_signature_too_long:
        return TRF(L"个性签名不能超过 {0} 个字。", kProfileLongTextMaxChars);

    //生日不是存在的日期
    case profile_edit_birthday_invalid:
        return std::wstring(TR(L"生日不是有效的日期，请检查后重试。"));

    //生日输入框里的文字不是 YYYY-MM-DD
    case profile_edit_birthday_format:
        return std::wstring(TR(L"生日格式应为 YYYY-MM-DD，请检查后重试。"));

    //所在地超长
    case profile_edit_address_too_long:
        return TRF(L"所在地不能超过 {0} 个字。", kProfileLongTextMaxChars);

    //邮箱超长
    case profile_edit_mail_too_long:
        return TRF(L"邮箱不能超过 {0} 个字。", kProfileLongTextMaxChars);

    //邮箱缺「@」
    case profile_edit_mail_invalid:
        return std::wstring(TR(L"邮箱格式不正确，应包含「@」。"));

    //岗位超长
    case profile_edit_post_too_long:
        return TRF(L"岗位不能超过 {0} 个字。", kProfileShortTextMaxChars);

    //座机超长
    case profile_edit_landline_too_long:
        return TRF(L"座机不能超过 {0} 个字。", kProfileShortTextMaxChars);

    //座机含不允许的字符
    case profile_edit_landline_invalid:
        return std::wstring(TR(L"座机只能包含数字、空格和 + - ( )。"));

    //昵称被清空
    case profile_edit_nickname_empty:
        return std::wstring(TR(L"昵称不能为空。"));

    //全部合格：没有提示
    default:
        return std::wstring();
    }
}

} // namespace profilefields
