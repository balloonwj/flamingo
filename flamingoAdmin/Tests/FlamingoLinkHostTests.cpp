/**
 *  flamingoAdmin —— 控制台接入 FlamingoLink.dll 的单元测试（FLK10~FLK12）。
 *    - FLK10：login.do 应答的 linkport / linkpubkey：有值时取到；旧版服务端没有这两项时端口 0、公钥为空；
 *    - FLK11：「在线用户」页面窗口的状态：已登录、企业详情屏确实可见、选中「在线用户」子 tab 三者同时满足
 *             才活动；叠层弹窗显示时只记为被盖住；页面不活动时不算被盖住；
 *    - FLK12：DLL 装卸：路径或函数名为空、文件不存在、文件不是 DLL、没有导出指定函数、正常加载五种结果；
 *    - FLK13：企业详情屏的「在线用户」子 tab：不可用时选不中；可用后占位控件铺在主体区、四周留 24 像素
 *             并在每次排版时报出矩形；切到该 tab 后占位控件可见；变为不可用时退回组织架构。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"
#include "Util/LinkPageState.h"
#include "Util/PluginLibrary.h"
#include "FlamingoLink/FlamingoLinkApi.h"
#include "Screens/EnterpriseDetailScreen.h"
#include "Widgets/LinkPageSlot.h"
#include "UiIds.h"

#include <string>
#include <cstdio>

namespace flamingoadmin { namespace test {

namespace {

// 构造一个「传输成功、HTTP 200、body 为给定 JSON」的应答。
HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

// 管理端口的示例值
const int kLinkPort = 20006;

// FLK10：login.do 新字段
void TestLoginLinkFields()
{
    BeginCase("FLK10 login.do 的 linkport / linkpubkey：有值时取到，旧版服务端缺省时端口 0、公钥为空");

    const LoginResult withLink = adminapi::ParseLoginResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"token\":\"t1\",\"admin\":{\"adminid\":1,\"name\":\"A\",\"email\":\"a@b\","
        "\"enterpriseid\":0},\"linkport\":20006,\"linkpubkey\":\"QLcjYOULvHX+dgwbRRvlwg0dwNRnO2H6D9a/u+7KZEg=\"}"));
    FA_EXPECT_TRUE(withLink.Succeeded());
    FA_EXPECT_EQ(withLink.m_linkPort, kLinkPort);
    FA_EXPECT_TRUE(withLink.m_linkPubkey == "QLcjYOULvHX+dgwbRRvlwg0dwNRnO2H6D9a/u+7KZEg=");

    // 服务器未开启加密：公钥为空串
    const LoginResult plain = adminapi::ParseLoginResponse(MakeHttpOk(
        "{\"code\":0,\"token\":\"t1\",\"admin\":{\"adminid\":1},\"linkport\":20006,\"linkpubkey\":\"\"}"));
    FA_EXPECT_EQ(plain.m_linkPort, kLinkPort);
    FA_EXPECT_TRUE(plain.m_linkPubkey.empty());

    // 旧版服务端：两项都没有
    const LoginResult old = adminapi::ParseLoginResponse(MakeHttpOk(
        "{\"code\":0,\"token\":\"t1\",\"admin\":{\"adminid\":1}}"));
    FA_EXPECT_TRUE(old.Succeeded());
    FA_EXPECT_EQ(old.m_linkPort, 0);
    FA_EXPECT_TRUE(old.m_linkPubkey.empty());
}

// FLK11：页面窗口状态
void TestLinkPageState()
{
    BeginCase("FLK11 在线用户页：登录、详情屏可见、选中页签三者满足才活动；弹窗只算盖住");

    LinkPageState s = DecideLinkPageState(true, true, true, false);
    FA_EXPECT_TRUE(s.m_active);
    FA_EXPECT_FALSE(s.m_covered);

    // 叠层弹窗：活动但被盖住
    s = DecideLinkPageState(true, true, true, true);
    FA_EXPECT_TRUE(s.m_active);
    FA_EXPECT_TRUE(s.m_covered);

    // 任一条件不满足即不活动，也不算被盖住
    s = DecideLinkPageState(false, true, true, true);
    FA_EXPECT_FALSE(s.m_active);
    FA_EXPECT_FALSE(s.m_covered);
    s = DecideLinkPageState(true, false, true, false);
    FA_EXPECT_FALSE(s.m_active);
    s = DecideLinkPageState(true, true, false, true);
    FA_EXPECT_FALSE(s.m_active);
    FA_EXPECT_FALSE(s.m_covered);
}

// 取测试程序所在目录
CString ExeDir()
{
    wchar_t path[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, path, MAX_PATH);
    CString dir(path);
    const int pos = dir.ReverseFind(L'\\');
    return (pos >= 0) ? dir.Left(pos) : dir;
}

// FLK12：DLL 装卸
void TestPluginLibrary()
{
    BeginCase("FLK12 DLL 装卸：空路径、文件不存在、不是 DLL、没有导出函数、正常加载");

    PluginLibrary lib;
    FARPROC entry = reinterpret_cast<FARPROC>(1);

    FA_EXPECT_EQ(static_cast<int>(lib.Load(nullptr, FLAMINGO_LINK_FACTORY_NAME, entry)),
                 static_cast<int>(PluginLoadResult::BadPath));
    FA_EXPECT_NULL(entry);
    FA_EXPECT_EQ(static_cast<int>(lib.Load(L"", FLAMINGO_LINK_FACTORY_NAME, entry)),
                 static_cast<int>(PluginLoadResult::BadPath));
    FA_EXPECT_EQ(static_cast<int>(lib.Load(L"C:\\x.dll", "", entry)),
                 static_cast<int>(PluginLoadResult::BadPath));

    // 文件不存在
    const CString missing = ExeDir() + _T("\\FlamingoLink_not_exist_12345.dll");
    FA_EXPECT_EQ(static_cast<int>(lib.Load(missing, FLAMINGO_LINK_FACTORY_NAME, entry)),
                 static_cast<int>(PluginLoadResult::NotFound));
    FA_EXPECT_FALSE(lib.IsLoaded());

    // 文件存在但不是 DLL
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    CString junk;
    junk.Format(_T("%sflamingolink_junk_%lu.dll"), tempDir, ::GetCurrentProcessId());
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, junk, L"wb") == 0 && fp != nullptr)
    {
        const char text[] = "this is not a dll";
        fwrite(text, 1, sizeof(text), fp);
        fclose(fp);
    }
    FA_EXPECT_EQ(static_cast<int>(lib.Load(junk, FLAMINGO_LINK_FACTORY_NAME, entry)),
                 static_cast<int>(PluginLoadResult::LoadFailed));
    ::DeleteFileW(junk);

    // 一个确实存在、但没有这个导出函数的系统 DLL
    wchar_t sysDir[MAX_PATH] = { 0 };
    ::GetSystemDirectoryW(sysDir, MAX_PATH);
    const CString versionDll = CString(sysDir) + _T("\\version.dll");
    FA_EXPECT_EQ(static_cast<int>(lib.Load(versionDll, FLAMINGO_LINK_FACTORY_NAME, entry)),
                 static_cast<int>(PluginLoadResult::EntryMissing));
    FA_EXPECT_FALSE(lib.IsLoaded());

    // 测试程序同目录的 FlamingoLink.dll：正常加载
    const CString linkDll = ExeDir() + _T("\\") + FLAMINGO_LINK_DLL_NAME;
    FA_EXPECT_EQ(static_cast<int>(lib.Load(linkDll, FLAMINGO_LINK_FACTORY_NAME, entry)),
                 static_cast<int>(PluginLoadResult::Ok));
    FA_EXPECT_NOTNULL(entry);
    FA_EXPECT_TRUE(lib.IsLoaded());
    lib.Free();
    FA_EXPECT_FALSE(lib.IsLoaded());

    // 每种结果都有说明文字
    FA_EXPECT_TRUE(wcslen(PluginLibrary::DescribeResult(PluginLoadResult::NotFound)) > 0);
    FA_EXPECT_TRUE(wcslen(PluginLibrary::DescribeResult(PluginLoadResult::EntryMissing)) > 0);
}

// FLK13 用的屏幕矩形与期望值：企业详情屏头部高 152（含底部 40 高的子 tab 条，见
// EnterpriseDetailScreen.cpp 的 kHeaderH），「在线用户」占位控件左右下各留 24（kOnlineSidePad）
const RECT kScreenRect = { 0, 0, 1200, 800 };
const int kDetailHeaderH = 152;
const int kOnlineSidePad = 24;

// 子 tab 序号（与 EnterpriseDetailScreen 内部的取值一致）
const int kTabOrg = 0;
const int kTabOnline = 2;

// 记录占位控件最近一次报出的矩形与报出次数
struct SlotRecorder
{
    RECT m_rc;
    int  m_calls;

    SlotRecorder() : m_calls(0)
    {
        m_rc = RECT{ 0, 0, 0, 0 };
    }

    static void OnBounds(void* user, const RECT& rc)
    {
        SlotRecorder* self = static_cast<SlotRecorder*>(user);
        self->m_rc = rc;
        ++self->m_calls;
    }
};

// FLK13：企业详情屏的「在线用户」子 tab
void TestDetailOnlineTab()
{
    BeginCase("FLK13 企业详情屏的在线用户子 tab：可用性、占位控件的位置与显隐、不可用时退回组织架构");

    EnterpriseDetailScreen screen;
    SlotRecorder rec;
    FA_EXPECT_NOTNULL(screen.GetOnlineSlot());
    if (screen.GetOnlineSlot() == nullptr)
        return;
    screen.GetOnlineSlot()->SetBoundsCallback(&SlotRecorder::OnBounds, &rec);

    screen.ForceLayout(kScreenRect);
    FA_EXPECT_FALSE(screen.IsOnlineTabAvailable());

    // 不可用：选不中，仍在组织架构
    screen.SelectSubTab(kTabOnline);
    FA_EXPECT_TRUE(screen.IsOrgTab());
    FA_EXPECT_FALSE(screen.IsOnlineTab());
    FA_EXPECT_FALSE(screen.GetOnlineSlot()->IsVisible());

    // 可用：重新排版，占位控件铺在主体区，四周留出边距
    const int callsBefore = rec.m_calls;
    screen.SetOnlineTabAvailable(true);
    FA_EXPECT_TRUE(screen.IsOnlineTabAvailable());
    FA_EXPECT_TRUE(rec.m_calls > callsBefore);
    FA_EXPECT_EQ(rec.m_rc.left, kScreenRect.left + kOnlineSidePad);
    FA_EXPECT_EQ(rec.m_rc.top, kScreenRect.top + kDetailHeaderH);
    FA_EXPECT_EQ(rec.m_rc.right, kScreenRect.right - kOnlineSidePad);
    FA_EXPECT_EQ(rec.m_rc.bottom, kScreenRect.bottom - kOnlineSidePad);
    FA_EXPECT_EQ(screen.GetOnlineSlot()->LastBounds().right, rec.m_rc.right);

    // 切到「在线用户」：占位控件可见，组织树搜索框隐藏（否则会从页面左上角的边距里露出来）
    balloonwjui::DuiControl* search = screen.FindCtrlById(kIdOrgSearch);
    FA_EXPECT_NOTNULL(search);
    screen.SelectSubTab(kTabOnline);
    FA_EXPECT_TRUE(screen.IsOnlineTab());
    FA_EXPECT_FALSE(screen.IsOrgTab());
    FA_EXPECT_TRUE(screen.GetOnlineSlot()->IsVisible());
    if (search != nullptr)
    {
        FA_EXPECT_FALSE(search->IsVisible());

        // 关闭叠层弹窗时 AdminFrame::SetEditsVisible 会把外壳里的输入框全部设回可见，随后由本屏纠正
        search->SetVisible(true);
        screen.ApplySubTabEditVisibility();
        FA_EXPECT_FALSE(search->IsVisible());
    }

    // 窗口变大：再次排版时报出新矩形
    const RECT bigger = { 0, 0, 1400, 900 };
    screen.ForceLayout(bigger);
    FA_EXPECT_EQ(rec.m_rc.right, bigger.right - kOnlineSidePad);
    FA_EXPECT_EQ(rec.m_rc.bottom, bigger.bottom - kOnlineSidePad);

    // 切回组织架构：占位控件隐藏，搜索框恢复
    screen.SelectSubTab(kTabOrg);
    FA_EXPECT_TRUE(screen.IsOrgTab());
    FA_EXPECT_FALSE(screen.GetOnlineSlot()->IsVisible());
    if (search != nullptr)
    {
        FA_EXPECT_TRUE(search->IsVisible());
    }

    // 正停在「在线用户」时变为不可用：退回组织架构
    screen.SelectSubTab(kTabOnline);
    FA_EXPECT_TRUE(screen.IsOnlineTab());
    screen.SetOnlineTabAvailable(false);
    FA_EXPECT_TRUE(screen.IsOrgTab());
    FA_EXPECT_FALSE(screen.GetOnlineSlot()->IsVisible());
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllFlamingoLinkHostTests()
{
    TestLoginLinkFields();
    TestLinkPageState();
    TestPluginLibrary();
    TestDetailOnlineTab();
}

}} // namespace flamingoadmin::test
