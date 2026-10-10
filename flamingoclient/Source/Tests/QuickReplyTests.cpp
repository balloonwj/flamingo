/**
 *  快捷回复的单测（docs/快捷回复方案.md §5），QuickReplyTests.cpp
 *
 *  [quickreply]
 *    QR1 配置：默认文字、写入读回、特殊字符往返、「设成空」不被默认文字顶替、恢复默认
 *    QR2 菜单模型：各项的顺序、空位置不出项但编号不变、三条都空时没有标题与第一条分隔线、发送键打勾
 *    QR3 菜单显示文字：截断加省略号、换行变空格、'&' 转义；命令 id 解析回原位置
 *    QR6 发送键（BUG-119）：绑定账号配置后读写都走账号配置；账号配置没有该项时取 flamingo.ini 的旧值并迁移
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonChatWnd/ChatSendMenu.h"
#include "../UI/BalloonChatWnd/ChatSendMode.h"
#include "../Session/UserConfig.h"

using namespace balloonchatwnd;

namespace {

//三条都有内容的快捷回复
std::vector<CString> ThreeReplies()
{
    std::vector<CString> v;
    v.push_back(CString(L"收到"));
    v.push_back(CString(L"好的，稍后回复"));
    v.push_back(CString(L"正在开会，稍后联系"));
    return v;
}

//按种类数菜单项
int CountKind(const std::vector<ChatSendMenuItem>& items, int kind)
{
    int n = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_kind == kind)
        {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("QR2 send menu model lists quick replies above the send-key options", "[quickreply]")
{
    SECTION("three replies")
    {
        const std::vector<ChatSendMenuItem> items = BuildChatSendMenuModel(ThreeReplies(), true);
        REQUIRE(items.size() == 9);
        CHECK(items[0].m_kind == kSendMenuHeader);
        CHECK(items[1].m_kind == kSendMenuQuickReply);
        CHECK(items[1].m_replyIndex == 0);
        CHECK(items[2].m_replyIndex == 1);
        CHECK(items[3].m_replyIndex == 2);
        CHECK(items[4].m_kind == kSendMenuSeparator);
        CHECK(items[5].m_kind == kSendMenuEnterKey);
        CHECK(items[5].m_checked);
        CHECK(items[6].m_kind == kSendMenuCtrlEnterKey);
        CHECK_FALSE(items[6].m_checked);
        CHECK(items[7].m_kind == kSendMenuSeparator);
        CHECK(items[8].m_kind == kSendMenuSettings);
    }

    SECTION("an empty slot keeps the numbering of the others")
    {
        std::vector<CString> replies = ThreeReplies();
        replies[1].Empty();
        const std::vector<ChatSendMenuItem> items = BuildChatSendMenuModel(replies, false);
        CHECK(CountKind(items, kSendMenuQuickReply) == 2);
        CHECK(items[1].m_replyIndex == 0);
        CHECK(items[2].m_replyIndex == 2);
        CHECK(items[2].m_cmdId == kSendMenuCmdQuickReplyBase + 2);
        //Ctrl+Enter 打勾
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].m_kind == kSendMenuEnterKey)
                CHECK_FALSE(items[i].m_checked);
            if (items[i].m_kind == kSendMenuCtrlEnterKey)
                CHECK(items[i].m_checked);
        }
    }

    SECTION("all empty: no header and no first separator")
    {
        std::vector<CString> replies(kQuickReplyCount);
        const std::vector<ChatSendMenuItem> items = BuildChatSendMenuModel(replies, true);
        REQUIRE(items.size() == 4);
        CHECK(items[0].m_kind == kSendMenuEnterKey);
        CHECK(items[1].m_kind == kSendMenuCtrlEnterKey);
        CHECK(items[2].m_kind == kSendMenuSeparator);
        CHECK(items[3].m_kind == kSendMenuSettings);
        CHECK(CountKind(items, kSendMenuHeader) == 0);
    }

    SECTION("command ids are unique and non-zero for clickable items")
    {
        const std::vector<ChatSendMenuItem> items = BuildChatSendMenuModel(ThreeReplies(), true);
        std::vector<UINT> ids;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].m_kind == kSendMenuHeader || items[i].m_kind == kSendMenuSeparator)
            {
                CHECK(items[i].m_cmdId == 0);
                continue;
            }
            CHECK(items[i].m_cmdId != 0);
            for (size_t j = 0; j < ids.size(); ++j)
            {
                CHECK(ids[j] != items[i].m_cmdId);
            }
            ids.push_back(items[i].m_cmdId);
        }
    }
}

TEST_CASE("QR3 quick reply menu text is one short line and commands resolve back", "[quickreply]")
{
    //恰好 20 个字不截断
    CHECK(QuickReplyMenuText(CString(L"一二三四五六七八九十一二三四五六七八九十")) == CString(L"一二三四五六七八九十一二三四五六七八九十"));
    //21 个字截成 20 个字加省略号
    CHECK(QuickReplyMenuText(CString(L"一二三四五六七八九十一二三四五六七八九十多")) ==
          CString(L"一二三四五六七八九十一二三四五六七八九十\x2026"));
    //换行变空格
    CHECK(QuickReplyMenuText(CString(L"第一行\r\n第二行\n第三行")) == CString(L"第一行 第二行 第三行"));
    //'&' 转义
    CHECK(QuickReplyMenuText(CString(L"A&B")) == CString(L"A&&B"));
    //截断位置正好落在代理对中间时往前退一个字符：19 个汉字后接一个代理对表情
    CString withEmoji(L"一二三四五六七八九十一二三四五六七八九");
    withEmoji += L"\xD83D\xDE00";
    withEmoji += L"尾";
    const CString shown = QuickReplyMenuText(withEmoji);
    CHECK(shown == CString(L"一二三四五六七八九十一二三四五六七八九\x2026"));

    //命令解析
    ChatSendMenuCommand cmd = ResolveChatSendMenuCommand(kSendMenuCmdQuickReplyBase + 2, kQuickReplyCount);
    CHECK(cmd.m_kind == kSendMenuQuickReply);
    CHECK(cmd.m_replyIndex == 2);
    CHECK(ResolveChatSendMenuCommand(kSendMenuCmdQuickReplyBase + 3, kQuickReplyCount).m_kind == kSendMenuNone);
    CHECK(ResolveChatSendMenuCommand(0, kQuickReplyCount).m_kind == kSendMenuNone);
    CHECK(ResolveChatSendMenuCommand(kSendMenuCmdEnter, kQuickReplyCount).m_kind == kSendMenuEnterKey);
    CHECK(ResolveChatSendMenuCommand(kSendMenuCmdCtrlEnter, kQuickReplyCount).m_kind == kSendMenuCtrlEnterKey);
    CHECK(ResolveChatSendMenuCommand(kSendMenuCmdSettings, kQuickReplyCount).m_kind == kSendMenuSettings);
}

// ---- QR1 / QR6：配置与发送键 ----------------------------------------------------------------------

//ChatSendMode.cpp 拼 flamingo.ini 路径要用 g_szHomePath，它本来定义在 Session/UserSessionData.cpp，而那个文件依赖太多、
//没有编进单测工程，故在这里给一份测试用的定义。哪天 UserSessionData.cpp 进了单测工程，删掉这一行即可
TCHAR g_szHomePath[MAX_PATH] = { 0 };

namespace {

//读 ini 原文时的缓冲区长度（字符）
const DWORD kQrRawValueChars = 1024;

//生成本进程专用的临时文件路径，并删掉上一次残留的同名文件
std::wstring QrFreshPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_quickreply_%s_%lu.ini", dir, tag, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

//读出 ini 里某个键的原文；不存在时返回 "<missing>"
std::wstring QrRawValue(const std::wstring& path, const wchar_t* section, const wchar_t* key)
{
    std::vector<wchar_t> buf(kQrRawValueChars, L'\0');
    ::GetPrivateProfileStringW(section, key, L"<missing>", &buf[0], kQrRawValueChars, path.c_str());
    return std::wstring(&buf[0]);
}

//测试用的发送键存储：记录写入的值，不碰磁盘
class FakeSendModeStore : public IChatSendModeStore
{
public:
    FakeSendModeStore()
        : m_has(true)
        , m_mode(kSendOnEnter)
        , m_saveCount(0)
    {
    }
    virtual bool HasSendMode() const { return m_has; }
    virtual SendKeyMode LoadSendMode() const { return m_mode; }
    virtual void SaveSendMode(SendKeyMode mode)
    {
        m_mode = mode;
        m_has = true;
        ++m_saveCount;
    }

    bool        m_has;         //账号配置里有没有发送键
    SendKeyMode m_mode;        //当前存的发送键
    int         m_saveCount;   //SaveSendMode 被调用的次数
};

} // namespace

TEST_CASE("QR1 quick replies round-trip through the account config", "[quickreply]")
{
    SECTION("defaults when the section is missing")
    {
        const std::wstring path = QrFreshPath(L"qr1a");
        CUserConfig cfg;
        REQUIRE(cfg.LoadConfig(path.c_str()));
        CHECK(cfg.GetQuickReply(0) == CString(L"收到"));
        CHECK(cfg.GetQuickReply(1) == CString(L"好的，稍后回复"));
        CHECK(cfg.GetQuickReply(2) == CString(L"正在开会，稍后联系"));
        CHECK_FALSE(cfg.IsQuickReplyCustomized(0));
        CHECK(cfg.GetQuickReply(-1).IsEmpty());
        CHECK(cfg.GetQuickReply(CUserConfig::kQuickReplySlotCount).IsEmpty());
        ::DeleteFileW(path.c_str());
    }

    SECTION("written values come back unchanged; an emptied slot stays empty")
    {
        const std::wstring path = QrFreshPath(L"qr1b");
        CString special(L"A&B\n第二行");
        special += L"\xD83D\xDE00";
        {
            CUserConfig writer;
            REQUIRE(writer.LoadConfig(path.c_str()));
            writer.SetQuickReply(0, special);
            writer.SetQuickReply(1, L"");
            REQUIRE(writer.SaveConfig(path.c_str()));
        }
        //存盘的是 ASCII 编码串，第三条没设过所以没有这个键
        CHECK(QrRawValue(path, L"QuickReply", L"Text2").empty());
        CHECK(QrRawValue(path, L"QuickReply", L"Text3") == L"<missing>");
        CUserConfig reader;
        REQUIRE(reader.LoadConfig(path.c_str()));
        CHECK(reader.GetQuickReply(0) == special);
        CHECK(reader.IsQuickReplyCustomized(1));
        CHECK(reader.GetQuickReply(1).IsEmpty());
        CHECK_FALSE(reader.IsQuickReplyCustomized(2));
        CHECK(reader.GetQuickReply(2) == CString(L"正在开会，稍后联系"));

        //恢复默认：三个键都删掉，读回默认文字
        reader.ResetQuickReplies();
        REQUIRE(reader.SaveConfig(path.c_str()));
        CHECK(QrRawValue(path, L"QuickReply", L"Text1") == L"<missing>");
        CUserConfig again;
        REQUIRE(again.LoadConfig(path.c_str()));
        CHECK(again.GetQuickReply(0) == CString(L"收到"));
        CHECK(again.GetQuickReply(1) == CString(L"好的，稍后回复"));
        ::DeleteFileW(path.c_str());
    }

    SECTION("enter-to-send key presence is reported")
    {
        const std::wstring path = QrFreshPath(L"qr1c");
        CUserConfig fresh;
        REQUIRE(fresh.LoadConfig(path.c_str()));
        CHECK_FALSE(fresh.HasEnterToSendSetting());
        fresh.EnablePressEnterToSend(FALSE);
        CHECK(fresh.HasEnterToSendSetting());
        REQUIRE(fresh.SaveConfig(path.c_str()));
        CUserConfig loaded;
        REQUIRE(loaded.LoadConfig(path.c_str()));
        CHECK(loaded.HasEnterToSendSetting());
        CHECK_FALSE(loaded.IsEnablePressEnterToSend());
        ::DeleteFileW(path.c_str());
    }
}

TEST_CASE("QR6 send mode reads and writes the bound account config, migrating the old value once", "[quickreply]")
{
    //让未绑定时读写的 flamingo.ini 落在临时目录：<临时目录>\flamingo_qr6\config\flamingo.ini
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    const std::wstring home = std::wstring(dir) + L"flamingo_qr6\\";
    ::CreateDirectoryW(home.c_str(), NULL);
    ::CreateDirectoryW((home + L"config").c_str(), NULL);
    const std::wstring ini = home + L"config\\flamingo.ini";
    ::WritePrivateProfileStringW(L"chat", L"sendmsgkey", L"ctrlenter", ini.c_str());
    wcscpy_s(g_szHomePath, home.c_str());

    SECTION("bound store with a value: reads and writes go to the store")
    {
        FakeSendModeStore store;
        store.m_mode = kSendOnCtrlEnter;
        BindChatSendModeStore(&store);
        CHECK(store.m_saveCount == 0);
        CHECK(GetChatSendMode() == kSendOnCtrlEnter);
        SetChatSendMode(kSendOnEnter);
        CHECK(store.m_mode == kSendOnEnter);
        CHECK(GetChatSendMode() == kSendOnEnter);
        //flamingo.ini 不受影响
        CHECK(QrRawValue(ini, L"chat", L"sendmsgkey") == L"ctrlenter");
        BindChatSendModeStore(NULL);
    }

    SECTION("bound store without a value: the old flamingo.ini value is migrated once")
    {
        FakeSendModeStore store;
        store.m_has = false;
        BindChatSendModeStore(&store);
        CHECK(store.m_saveCount == 1);
        CHECK(store.m_mode == kSendOnCtrlEnter);
        CHECK(GetChatSendMode() == kSendOnCtrlEnter);
        BindChatSendModeStore(NULL);
    }

    SECTION("unbound: flamingo.ini is used")
    {
        BindChatSendModeStore(NULL);
        CHECK(GetChatSendMode() == kSendOnCtrlEnter);
    }

    BindChatSendModeStore(NULL);
    g_szHomePath[0] = 0;
    ::DeleteFileW(ini.c_str());
    ::RemoveDirectoryW((home + L"config").c_str());
    ::RemoveDirectoryW(home.c_str());
}
