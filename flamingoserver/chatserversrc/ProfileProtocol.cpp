/**
 *  v2 账号资料报文编解码实现，ProfileProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "ProfileProtocol.h"
#include "UserManager.h"   //applyProfileUpdate 合并请求进 User
#include "Protocol.h"      //enum profile_gender
#include "../jsoncpp1.9.0/json.h"

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 UserProfile 各字段写入一个 Json::Value 对象（get_user_info 与 notify 共用）
static Json::Value makeProfileJson(const UserProfile& p)
{
    Json::Value jp;
    jp["userid"]       = std::to_string(p.userid);
    jp["usertype"]     = p.usertype;
    jp["username"]     = p.username;
    jp["nickname"]     = p.nickname;
    jp["facetype"]     = p.facetype;
    jp["customface"]   = p.customface;
    jp["gender"]       = p.gender;
    jp["birthday"]     = p.birthday;
    jp["signature"]    = p.signature;
    jp["address"]      = p.address;
    jp["phonenumber"]  = p.phonenumber;
    jp["mail"]         = p.mail;
    jp["landline"]     = p.landline;
    jp["post"]         = p.post;
    jp["enterpriseid"] = std::to_string(p.enterpriseid);
    jp["presence"]     = p.presence;
    jp["clienttype"]   = p.clienttype;
    //两个公开范围标志只发给本人（同事拿到的版本不带，见 applyViewer）
    if (p.hasvisibility)
    {
        jp["phonevisible"]    = p.phonevisible;
        jp["landlinevisible"] = p.landlinevisible;
    }
    return jp;
}

bool ProfileProtocol::parseGetUserInfoRequest(const std::string& json, int64_t& userid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["userid"].isString())
        return false;

    userid = static_cast<int64_t>(atoll(root["userid"].asString().c_str()));
    return userid > 0;
}

//UTF-8 文本的字符数（码点数）：凡不是续字节（10xxxxxx）的字节各算一个字符
static size_t profileUtf8CharCount(const std::string& text)
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

//生日的年份范围：早于 1900 年或晚于 2100 年的视为填错
static const int kBirthdayMinYear = 1900;
static const int kBirthdayMaxYear = 2100;

//生日是否合法：0 表示清空；否则须是存在的 YYYYMMDD 日期（含闰年二月）
static bool isValidBirthday(int birthday)
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

//性别是否为 enum profile_gender 的三个值之一
static bool isValidGender(int gender)
{
    return gender == profile_gender_unset || gender == profile_gender_male || gender == profile_gender_female;
}

//公开范围是否为 enum profile_visibility 的两个值之一
static bool isValidVisibility(int visibility)
{
    return visibility == profile_visibility_self_only || visibility == profile_visibility_colleagues;
}

bool ProfileProtocol::parseUpdateProfileRequest(const std::string& json, UpdateProfileRequest& req)
{
    req.has_nickname  = false;
    req.has_signature = false;
    req.has_gender    = false;
    req.has_birthday  = false;
    req.has_address   = false;
    req.has_mail      = false;
    req.has_landline  = false;
    req.has_post      = false;
    req.has_phonevisible    = false;
    req.has_landlinevisible = false;

    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //字段全可选：出现且类型正确才纳入更新，其余忽略
    if (root["nickname"].isString())
    {
        req.has_nickname = true;
        req.nickname = root["nickname"].asString();
    }
    if (root["signature"].isString())
    {
        req.has_signature = true;
        req.signature = root["signature"].asString();
    }
    if (root["gender"].isInt())
    {
        req.has_gender = true;
        req.gender = root["gender"].asInt();
    }
    if (root["birthday"].isInt())
    {
        req.has_birthday = true;
        req.birthday = root["birthday"].asInt();
    }
    if (root["address"].isString())
    {
        req.has_address = true;
        req.address = root["address"].asString();
    }
    if (root["mail"].isString())
    {
        req.has_mail = true;
        req.mail = root["mail"].asString();
    }
    if (root["landline"].isString())
    {
        req.has_landline = true;
        req.landline = root["landline"].asString();
    }
    if (root["post"].isString())
    {
        req.has_post = true;
        req.post = root["post"].asString();
    }
    if (root["phonevisible"].isInt())
    {
        req.has_phonevisible = true;
        req.phonevisible = root["phonevisible"].asInt();
    }
    if (root["landlinevisible"].isInt())
    {
        req.has_landlinevisible = true;
        req.landlinevisible = root["landlinevisible"].asInt();
    }
    //phonenumber 即使出现也不读：手机号不允许本人修改

    //出现的字段逐项校验：长度按 UTF-8 字符数，与 t_user 的列宽一致；性别、生日按取值范围
    const size_t kShortMax = static_cast<size_t>(kProfileShortTextMaxChars);
    const size_t kLongMax = static_cast<size_t>(kProfileLongTextMaxChars);
    if (req.has_nickname && profileUtf8CharCount(req.nickname) > kShortMax)
        return false;
    if (req.has_post && profileUtf8CharCount(req.post) > kShortMax)
        return false;
    if (req.has_landline && profileUtf8CharCount(req.landline) > kShortMax)
        return false;
    if (req.has_signature && profileUtf8CharCount(req.signature) > kLongMax)
        return false;
    if (req.has_address && profileUtf8CharCount(req.address) > kLongMax)
        return false;
    if (req.has_mail && profileUtf8CharCount(req.mail) > kLongMax)
        return false;
    if (req.has_gender && !isValidGender(req.gender))
        return false;
    if (req.has_birthday && !isValidBirthday(req.birthday))
        return false;
    if (req.has_phonevisible && !isValidVisibility(req.phonevisible))
        return false;
    if (req.has_landlinevisible && !isValidVisibility(req.landlinevisible))
        return false;
    return true;
}

void ProfileProtocol::applyProfileUpdate(const UpdateProfileRequest& req, User& u)
{
    if (req.has_nickname)
        u.nickname = req.nickname;
    if (req.has_signature)
        u.signature = req.signature;
    if (req.has_gender)
        u.gender = req.gender;
    if (req.has_birthday)
        u.birthday = req.birthday;
    if (req.has_address)
        u.address = req.address;
    if (req.has_mail)
        u.mail = req.mail;
    if (req.has_landline)
        u.landline = req.landline;
    if (req.has_post)
        u.post = req.post;
    if (req.has_phonevisible)
        u.phonevisible = req.phonevisible;
    if (req.has_landlinevisible)
        u.landlinevisible = req.landlinevisible;
}

void ProfileProtocol::fillProfileFromUser(UserProfile& p, const User& u)
{
    p.userid          = u.userid;
    p.usertype        = u.usertype;
    p.username        = u.username;
    p.nickname        = u.nickname;
    p.facetype        = u.facetype;
    p.customface      = u.customface;
    p.gender          = u.gender;
    p.birthday        = u.birthday;
    p.signature       = u.signature;
    p.address         = u.address;
    p.phonenumber     = u.phonenumber;
    p.landline        = u.landline;
    p.post            = u.post;
    p.mail            = u.mail;
    p.phonevisible    = u.phonevisible;
    p.landlinevisible = u.landlinevisible;
    p.hasvisibility   = false;
}

bool ProfileProtocol::parseChangePasswordRequest(const std::string& json, ChangePasswordRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["oldpassword"].isString() || !root["newpassword"].isString())
        return false;

    req.oldpassword = root["oldpassword"].asString();
    req.newpassword = root["newpassword"].asString();
    if (req.oldpassword.empty() || req.newpassword.empty())
        return false;

    return true;
}

bool ProfileProtocol::parseChangeAvatarRequest(const std::string& json, ChangeAvatarRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["facetype"].isInt())
        return false;

    req.facetype = root["facetype"].asInt();
    req.customface = root["customface"].isString() ? root["customface"].asString() : "";
    return true;
}

bool ProfileProtocol::parseChangePresenceRequest(const std::string& json, ChangePresenceRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["presence"].isInt())
        return false;

    req.presence = root["presence"].asInt();
    return true;
}

std::string ProfileProtocol::buildGetUserInfoResponse(int code, const std::string& msg,
                                                      const UserProfile& profile)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["profile"] = makeProfileJson(profile);
    return writeCompact(root);
}

std::string ProfileProtocol::buildUserInfoChangedNotify(const UserProfile& profile)
{
    Json::Value root;
    root["userid"]  = std::to_string(profile.userid);
    root["profile"] = makeProfileJson(profile);
    return writeCompact(root);
}

void ProfileProtocol::maskNumbersForViewer(int phonevisible, int landlinevisible, bool viewerIsOwner,
                                           std::string& phonenumber, std::string& landline)
{
    //本人看自己：号码原样保留
    if (viewerIsOwner)
        return;
    //同事看：只有明确设为「对同事可见」的号码才保留，其余取值一律按不公开处理
    if (phonevisible != profile_visibility_colleagues)
        phonenumber.clear();
    if (landlinevisible != profile_visibility_colleagues)
        landline.clear();
}

//按「查看者是不是本人」处理一份资料：applyViewer 与 buildUserInfoChangedNotifies 共用
static void applyViewerKind(UserProfile& profile, bool viewerIsOwner)
{
    ProfileProtocol::maskNumbersForViewer(profile.phonevisible, profile.landlinevisible, viewerIsOwner,
                                          profile.phonenumber, profile.landline);
    profile.hasvisibility = viewerIsOwner;
}

void ProfileProtocol::applyViewer(UserProfile& profile, int64_t viewerUserId)
{
    applyViewerKind(profile, viewerUserId == profile.userid);
}

void ProfileProtocol::buildUserInfoChangedNotifies(const UserProfile& full, std::string& ownerBody,
                                                   std::string& colleagueBody)
{
    UserProfile ownerView = full;
    applyViewerKind(ownerView, true);
    ownerBody = buildUserInfoChangedNotify(ownerView);

    UserProfile colleagueView = full;
    applyViewerKind(colleagueView, false);
    colleagueBody = buildUserInfoChangedNotify(colleagueView);
}

std::string ProfileProtocol::buildPresenceChangedNotify(int64_t userid, int presence, int clienttype)
{
    Json::Value root;
    root["userid"]     = std::to_string(userid);
    root["presence"]   = presence;
    root["clienttype"] = clienttype;
    return writeCompact(root);
}

std::string ProfileProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

//本次提交了该字段、值与当前值不同、且不为空串时，把新值追加到 out
static void appendIfChanged(bool submitted, const std::string& newValue, const std::string& currentValue,
                            std::vector<std::string>& out)
{
    if (submitted && !newValue.empty() && newValue != currentValue)
        out.push_back(newValue);
}

void ProfileProtocol::collectChangedProfileTexts(const UpdateProfileRequest& req, const UserProfile& current,
                                                 std::vector<std::string>& out)
{
    appendIfChanged(req.has_nickname, req.nickname, current.nickname, out);
    appendIfChanged(req.has_signature, req.signature, current.signature, out);
    appendIfChanged(req.has_address, req.address, current.address, out);
    appendIfChanged(req.has_mail, req.mail, current.mail, out);
    appendIfChanged(req.has_post, req.post, current.post, out);
}
