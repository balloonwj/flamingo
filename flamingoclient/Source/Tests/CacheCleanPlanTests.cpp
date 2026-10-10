/**
 *  「清理本地缓存」清理计划的单元测试（Suite CCP），CacheCleanPlanTests.cpp
 *
 *  覆盖：
 *    CCP1  正常账号产出三个类别，目录分别是（ChatFile + ChatVoice）/ ChatImage /（Avatar + UserThumb）；
 *    CCP2  计划里<u>永远</u>不出现 ChatLog 与 .ini —— 聊天记录、公告、登记表、配置不可清；
 *    CCP3  未登录（账号为空）或数据根为空时产出空计划；
 *    CCP4  账号里混进路径分隔符 / 上跳时，产出的目录仍落在 Users\ 之下（靠净化）；
 *    CCP5  边界检查放行 Users\ 之下的目录；
 *    CCP6  边界检查拒绝 Users\ 之外的目录、拒绝 Users\ 本身、拒绝含上跳的路径；
 *    CCP7  占用量文本的分档与取整。
 *
 *  框架 Catch2 v3。纯逻辑，不扫描磁盘、不删任何东西。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonSettings/CacheCleanPlan.h"

using balloonsettings::BuildCacheCleanPlan;
using balloonsettings::CacheCleanEntry;
using balloonsettings::IsSafeToDeleteCacheDir;
using balloonsettings::kCacheAvatar;
using balloonsettings::kCacheCategoryCount;
using balloonsettings::kCacheChatFile;
using balloonsettings::kCacheChatImage;

namespace {

//测试用的数据根与账号。数据根按约定以反斜杠结尾。
const wchar_t* const kRoot    = L"E:\\flamingo\\flamingoclient\\Bin\\";
const wchar_t* const kAccount = L"MY0113";

//计划里是否有某个子串出现在任何一个目录上。
bool AnyDirContains(const std::vector<CacheCleanEntry>& plan, const wchar_t* needle)
{
    for (size_t i = 0; i < plan.size(); ++i)
    {
        for (size_t k = 0; k < plan[i].m_dirs.size(); ++k)
        {
            if (plan[i].m_dirs[k].find(needle) != std::wstring::npos)
            {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("CCP1 正常账号产出三个类别及各自目录", "[cacheclean]")
{
    const std::vector<CacheCleanEntry> plan = BuildCacheCleanPlan(kRoot, kAccount);
    REQUIRE(plan.size() == (size_t)kCacheCategoryCount);

    CHECK(plan[0].m_category == kCacheChatFile);
    //聊天文件一类连同语音缓存 ChatVoice（语音消息第 3c 步并入）
    REQUIRE(plan[0].m_dirs.size() == 2);
    CHECK(plan[0].m_dirs[0] == std::wstring(L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0113\\ChatFile\\"));
    CHECK(plan[0].m_dirs[1] == std::wstring(L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0113\\ChatVoice\\"));

    CHECK(plan[1].m_category == kCacheChatImage);
    REQUIRE(plan[1].m_dirs.size() == 1);
    CHECK(plan[1].m_dirs[0] == std::wstring(L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0113\\ChatImage\\"));

    //头像有两个目录，对用户是一件事，合成一项。
    CHECK(plan[2].m_category == kCacheAvatar);
    REQUIRE(plan[2].m_dirs.size() == 2);
    CHECK(plan[2].m_dirs[0] == std::wstring(L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0113\\Avatar\\"));
    CHECK(plan[2].m_dirs[1] == std::wstring(L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0113\\UserThumb\\"));

    //每一项都得有显示名，对话框要拿它做行标题。
    for (size_t i = 0; i < plan.size(); ++i)
    {
        CHECK(plan[i].m_title != NULL);
    }
}

TEST_CASE("CCP2 计划里永远不出现聊天记录与账号配置", "[cacheclean]")
{
    const std::vector<CacheCleanEntry> plan = BuildCacheCleanPlan(kRoot, kAccount);

    //这几样是本地唯一副本，删了就没了：聊天记录、公告、文件落点登记表都在 ChatLog 下，
    //账号配置是 <账号>.ini。清理缓存一律不许碰。
    CHECK_FALSE(AnyDirContains(plan, L"ChatLog"));
    CHECK_FALSE(AnyDirContains(plan, L".ini"));
    CHECK_FALSE(AnyDirContains(plan, L"msg.db"));
    CHECK_FALSE(AnyDirContains(plan, L"announce.db"));
    CHECK_FALSE(AnyDirContains(plan, L"filelocal.db"));
}

TEST_CASE("CCP3 未登录或数据根为空时产出空计划", "[cacheclean]")
{
    CHECK(BuildCacheCleanPlan(kRoot, L"").empty());
    CHECK(BuildCacheCleanPlan(L"", kAccount).empty());

    //账号净化后不剩任何字符，等同于没登录。
    CHECK(BuildCacheCleanPlan(kRoot, L"...").empty());
}

TEST_CASE("CCP4 账号里混进分隔符或上跳时目录仍落在 Users 之下", "[cacheclean]")
{
    //账号是服务端按固定规则生成的，正常不会长这样；这一条防的是历史脏数据与规则放宽。
    const std::vector<CacheCleanEntry> plan = BuildCacheCleanPlan(kRoot, L"..\\..\\Windows");
    for (size_t i = 0; i < plan.size(); ++i)
    {
        for (size_t k = 0; k < plan[i].m_dirs.size(); ++k)
        {
            CHECK(IsSafeToDeleteCacheDir(plan[i].m_dirs[k], kRoot));
        }
    }
}

TEST_CASE("CCP5 边界检查放行 Users 之下的目录", "[cacheclean]")
{
    CHECK(IsSafeToDeleteCacheDir(
        L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0113\\ChatFile\\", kRoot));
}

TEST_CASE("CCP6 边界检查拒绝越界、拒绝 Users 本身、拒绝含上跳的路径", "[cacheclean]")
{
    //在 Users\ 之外。
    CHECK_FALSE(IsSafeToDeleteCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\Image\\", kRoot));
    CHECK_FALSE(IsSafeToDeleteCacheDir(L"D:\\别的地方\\", kRoot));

    //正好是 Users\ 本身：删了等于把所有账号的数据连同聊天记录一起干掉。
    CHECK_FALSE(IsSafeToDeleteCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\Users\\", kRoot));

    //含上跳：字符串前缀比对挡不住，必须单独拒绝。
    CHECK_FALSE(IsSafeToDeleteCacheDir(
        L"E:\\flamingo\\flamingoclient\\Bin\\Users\\..\\..\\Windows\\", kRoot));

    //空参数。
    CHECK_FALSE(IsSafeToDeleteCacheDir(L"", kRoot));
    CHECK_FALSE(IsSafeToDeleteCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0113\\ChatFile\\", L""));

    //目录名里含两个点但不是上跳段：合法目录，必须放行。账号净化后就可能得到这种名字。
    CHECK(IsSafeToDeleteCacheDir(
        L"E:\\flamingo\\flamingoclient\\Bin\\Users\\_.._Windows\\ChatFile\\", kRoot));
}

TEST_CASE("CCP7 占用量文本的分档与取整", "[cacheclean]")
{
    //不足 1 KB 给具体字节数：这一档要能把"空的"和"很小但不为空"区分开。
    CHECK(balloonsettings::FormatByteSize(0) == std::wstring(L"0 字节"));
    CHECK(balloonsettings::FormatByteSize(512) == std::wstring(L"512 字节"));
    CHECK(balloonsettings::FormatByteSize(1023) == std::wstring(L"1023 字节"));

    //1024 起进 KB 档。
    CHECK(balloonsettings::FormatByteSize(1024) == std::wstring(L"1.0 KB"));
    CHECK(balloonsettings::FormatByteSize(1536) == std::wstring(L"1.5 KB"));

    //MB 与 GB 档。进制取 1024，与文件管理器同口径。
    CHECK(balloonsettings::FormatByteSize(1024ULL * 1024ULL) == std::wstring(L"1.0 MB"));
    CHECK(balloonsettings::FormatByteSize(23ULL * 1024ULL * 1024ULL) == std::wstring(L"23.0 MB"));
    CHECK(balloonsettings::FormatByteSize(1024ULL * 1024ULL * 1024ULL) == std::wstring(L"1.0 GB"));

    //超大值不溢出、也不换成 TB —— 聊天缓存到不了那个量级，显示成四位数 GB 也读得懂。
    CHECK(balloonsettings::FormatByteSize(2048ULL * 1024ULL * 1024ULL * 1024ULL)
          == std::wstring(L"2048.0 GB"));
}
