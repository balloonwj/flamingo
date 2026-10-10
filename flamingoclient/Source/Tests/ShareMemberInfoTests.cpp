/**
 *  文件共享宿主回送会话成员的转换单元测试（Suite SMI，标签 [sharemembers]），ShareMemberInfoTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 15 步）：
 *    - SMI1 群成员：显示名依次取群名片、昵称、通讯录显示名，角色原样带上；没有通讯录接口时兜底为「用户<id>」；
 *    - SMI2 单聊对方：显示名取通讯录显示名；
 *    - SMI3 显示名超长时按 UTF-8 字符边界截断，不截出半个汉字，结尾总是 '\0'；
 *    - SMI4 头像写进接口结构体（2026-10-03 起）：字形、底色、照片路径原样写入；结构体大小不符时拒绝；字形超长时不留下
 *           半个 emoji；照片路径放不下时留空、改画字形；
 *    - SMI5 没有主面板可问时的头像：显示名首字与按 userid 取的底色，没有显示名时为「?」。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Feature/FileShare/ShareMemberInfo.h"
#include "../net/GroupProtocolClient.h"
#include "../UI/BalloonMain/AvatarPalette.h"   //AvatarColorForUser：SMI5 核对默认头像的底色

using namespace balloonfileshare;

namespace {

//三名群成员的 userid
const int64_t kOwner = 20001;
const int64_t kAdmin = 20002;
const int64_t kPlain = 20003;

//群成员角色：成员、管理员、群主（与 Protocol.h 的 group_member_role 一致）
const int kRoleMember = 0;
const int kRoleAdmin = 1;
const int kRoleOwner = 2;

//截断用例的缓冲区字节数：放得下「甲乙」（6 字节）加 '\0'，放不下第三个汉字
const size_t kSmallBuf = 8;

//通讯录桩：按 userid 给固定的显示名
class FakeDirectory : public IFileShareDirectory
{
public:
    virtual std::wstring GetShareUserDisplayName(int64_t userId) override
    {
        return (userId == kPlain) ? L"王五（备注）" : L"通讯录名";
    }

    virtual void OnShareCardStateChanged(int64_t shareId, int state, int reason, int myPerms) override
    {
        (void)shareId;
        (void)state;
        (void)reason;
        (void)myPerms;
    }

    virtual std::wstring GetShareConversationName(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
        return std::wstring();
    }

    virtual void OpenShareConversation(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
    }

    virtual void OnConversationShareCount(int ctype, int64_t cid, int count) override
    {
        (void)ctype;
        (void)cid;
        (void)count;
    }

    virtual bool OpenShareTargetPicker(HWND owner) override
    {
        (void)owner;
        return false;
    }

    virtual bool CreateShareGroup(const std::string& groupName, const std::vector<int64_t>& memberIds) override
    {
        (void)groupName;
        (void)memberIds;
        return false;
    }

    virtual ShareUserAvatar GetShareUserAvatar(int64_t userId) override
    {
        return DefaultShareUserAvatar(userId, GetShareUserDisplayName(userId));
    }

    //本组用例不涉及弹窗通知：不静音、没开勿扰
    virtual bool IsShareNoticeMuted() override
    {
        return false;
    }

    virtual bool IsDoNotDisturbEnabled() override
    {
        return false;
    }
};

net::GroupMemberItem Member(int64_t userId, int role, const char* card, const char* nickname)
{
    net::GroupMemberItem item;
    item.m_userid = userId;
    item.m_role = role;
    item.m_card = card;
    item.m_nickname = nickname;
    return item;
}

} // namespace

TEST_CASE("SMI1 group members name priority", "[sharemembers]")
{
    net::GroupMembersResult result;
    result.m_code = 0;
    result.m_members.push_back(Member(kOwner, kRoleOwner, "群主名片", "张三"));
    result.m_members.push_back(Member(kAdmin, kRoleAdmin, "", "李四"));
    result.m_members.push_back(Member(kPlain, kRoleMember, "", ""));

    FakeDirectory directory;
    const std::vector<fileshare::FileShareMemberInfo> infos = BuildGroupMemberInfos(result, &directory);
    REQUIRE(infos.size() == 3);
    CHECK(infos[0].m_userId == kOwner);
    CHECK(infos[0].m_role == kRoleOwner);
    CHECK(std::string(infos[0].m_name) == "群主名片");
    CHECK(std::string(infos[1].m_name) == "李四");
    CHECK(infos[1].m_role == kRoleAdmin);
    CHECK(std::string(infos[2].m_name) == "王五（备注）");

    //没有通讯录接口：名片与昵称都为空时兜底
    const std::vector<fileshare::FileShareMemberInfo> bare = BuildGroupMemberInfos(result, NULL);
    CHECK(std::string(bare[2].m_name) == "用户20003");
}

TEST_CASE("SMI2 single chat peer", "[sharemembers]")
{
    FakeDirectory directory;
    const fileshare::FileShareMemberInfo peer = BuildPeerMemberInfo(kPlain, &directory);
    CHECK(peer.m_userId == kPlain);
    CHECK(peer.m_role == 0);
    CHECK(std::string(peer.m_name) == "王五（备注）");
}

TEST_CASE("SMI3 utf8 truncation", "[sharemembers]")
{
    char buf[kSmallBuf];
    CopyUtf8Truncated("甲乙丙", buf, kSmallBuf);
    CHECK(std::string(buf) == "甲乙");
    CopyUtf8Truncated("abcdefghij", buf, kSmallBuf);
    CHECK(std::string(buf) == "abcdefg");
    CopyUtf8Truncated("", buf, kSmallBuf);
    CHECK(std::string(buf).empty());

    //超长名字写进接口结构体：截断后仍以 '\0' 结尾、不截出半个汉字
    net::GroupMembersResult result;
    std::string longName;
    for (size_t i = 0; i < fileshare::kFileShareTextBytes; ++i)
        longName += "汉";
    result.m_members.push_back(Member(kOwner, kRoleOwner, longName.c_str(), ""));
    const std::vector<fileshare::FileShareMemberInfo> infos = BuildGroupMemberInfos(result, NULL);
    const std::string name(infos[0].m_name);
    CHECK(name.size() < fileshare::kFileShareTextBytes);
    CHECK(name.size() % std::string("汉").size() == 0);
}

TEST_CASE("SMI4 avatar written into the interface struct", "[sharemembers]")
{
    //照片头像的本机路径（用例只看拷贝，不读文件）
    const wchar_t* const kPhotoPath = L"D:\\Users\\a\\Avatar\\f1";
    //字形头像的底色
    const COLORREF kGlyphColor = RGB(0x2B, 0xB6, 0x73);

    ShareUserAvatar avatar;
    avatar.m_glyph = L"王";
    avatar.m_color = kGlyphColor;
    avatar.m_imagePath = kPhotoPath;
    fileshare::FileShareAvatarInfo info;
    info.m_cbSize = sizeof(info);
    REQUIRE(FillAvatarInfo(avatar, &info));
    CHECK(info.m_cbSize == sizeof(info));
    CHECK(std::wstring(info.m_glyph) == L"王");
    CHECK(info.m_color == static_cast<uint32_t>(kGlyphColor));
    CHECK(std::wstring(info.m_imagePath) == kPhotoPath);

    //调用方给的结构体大小不符：拒绝，不写入
    fileshare::FileShareAvatarInfo bad;
    bad.m_cbSize = sizeof(bad) - 1;
    CHECK_FALSE(FillAvatarInfo(avatar, &bad));
    CHECK_FALSE(FillAvatarInfo(avatar, NULL));

    //字形超出容量：截断点落在 emoji 的后半个上时连前半个一起去掉，不留下半个 emoji
    std::wstring glyph(fileshare::kFileShareGlyphChars - 2, L'甲');
    glyph += L"\xD83E\xDD8A";   //一个占两个 UTF-16 单元的 emoji，前半个正好是最后一个放得下的单元
    avatar.m_glyph = glyph;
    REQUIRE(FillAvatarInfo(avatar, &info));
    CHECK(std::wstring(info.m_glyph) == std::wstring(fileshare::kFileShareGlyphChars - 2, L'甲'));

    //照片路径放不下：整体留空、改画字形，字形照常写入
    avatar.m_glyph = L"王";
    avatar.m_imagePath = std::wstring(fileshare::kFileSharePathChars, L'a');
    REQUIRE(FillAvatarInfo(avatar, &info));
    CHECK(std::wstring(info.m_imagePath).empty());
    CHECK(std::wstring(info.m_glyph) == L"王");
}

TEST_CASE("SMI5 default avatar without the main panel", "[sharemembers]")
{
    //与主面板对没设置过头像的人的画法相同：显示名首字，底色按 userid 取
    const ShareUserAvatar named = DefaultShareUserAvatar(kPlain, L"王五");
    CHECK(named.m_glyph == L"王");
    CHECK(named.m_color == balloonmain::AvatarColorForUser(kPlain));
    CHECK(named.m_imagePath.empty());

    //没有显示名：字形为「?」
    CHECK(DefaultShareUserAvatar(kPlain, L"").m_glyph == L"?");
}
