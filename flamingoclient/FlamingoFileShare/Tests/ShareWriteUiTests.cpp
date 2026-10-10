/**
 *  写操作用到的两个小窗口的单元测试（Suite SWU，标签 [sharewriteui]），ShareWriteUiTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 2 步）：
 *    - SWU1 名字输入窗口：打开时所有者被置为不可操作、输入框预先填好；不合法的名字显示原因、不回调、窗口保持打开；
 *           合法的名字回调监听者（用途编号原样带回）、所有者恢复、窗口随后关闭；直接关闭不回调；
 *    - SWU2 选目标文件夹窗口：打开即请求起始目录；结果里只列子文件夹；进入子文件夹与上一级各发一次请求；
 *           与最近一次请求不符的结果忽略；「移动到这里」交回当前目录并回调关闭；正在加载时不能确认；
 *    - SWU3 截图（标签 [.snapshot]，默认不运行）：两个窗口存到仓库根 snapshots 目录供人工核对。
 *  窗口真实创建，不模拟鼠标与键盘；消息循环由用例自己处理。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../UI/ShareFolderPickDialog.h"
#include "../UI/ShareNameDialog.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"
#include "ShareTestSnapshot.h"

using namespace fileshare;

namespace {

//让窗口完成布局与绘制的时长，单位：毫秒
const DWORD kPaintWaitMs = 300;

//两个截图的保存位置（相对测试程序目录）
const wchar_t* const kSnapshotName = L"\\..\\..\\..\\snapshots\\fileshare_name_dialog.png";
const wchar_t* const kSnapshotPick = L"\\..\\..\\..\\snapshots\\fileshare_folder_pick.png";

//名字输入窗口的用途编号
const int kPurpose = 7;

//充当所有者的窗口的边长，单位：像素
const int kOwnerSize = 200;

//充当所有者的空白顶层窗口
class OwnerWindow
{
public:
    OwnerWindow()
        : m_hWnd(::CreateWindowExW(0, L"STATIC", L"owner", WS_OVERLAPPEDWINDOW, 0, 0, kOwnerSize, kOwnerSize, NULL, NULL,
                                   ::GetModuleHandleW(NULL), NULL))
    {
    }

    ~OwnerWindow()
    {
        if (m_hWnd != NULL)
            ::DestroyWindow(m_hWnd);
    }

    HWND m_hWnd;   //窗口句柄
};

//名字输入窗口的假监听者
class NameListener : public IShareNameDialogListener
{
public:
    NameListener()
        : m_count(0)
        , m_purpose(0)
    {
    }

    virtual void OnShareNameEntered(int purpose, const std::wstring& name) override
    {
        ++m_count;
        m_purpose = purpose;
        m_name = name;
    }

    int          m_count;    //回调次数
    int          m_purpose;  //最近一次的用途编号
    std::wstring m_name;     //最近一次的名字
};

//选目标文件夹窗口的假来源与假监听者
class PickHarness : public IShareFolderSource, public IShareFolderPickListener
{
public:
    PickHarness()
        : m_pickedCount(0)
        , m_closedCount(0)
    {
    }

    virtual void RequestFolderList(const std::string& path) override
    {
        m_requests.push_back(path);
    }

    virtual void OnShareFolderPicked(const std::string& dir) override
    {
        ++m_pickedCount;
        m_picked = dir;
    }

    virtual void OnShareFolderPickClosed() override
    {
        ++m_closedCount;
    }

    std::vector<std::string> m_requests;     //请求过的目录，按顺序
    int                      m_pickedCount;  //选中回调次数
    std::string              m_picked;       //最近一次选中的目录
    int                      m_closedCount;  //关闭回调次数
};

ShareEntry Entry(const char* name, bool isDir)
{
    ShareEntry e;
    e.m_name = name;
    e.m_isDir = isDir;
    return e;
}

//一个目录的内容：两个子文件夹与一个文件
std::vector<ShareEntry> SampleEntries()
{
    std::vector<ShareEntry> v;
    v.push_back(Entry("b-dir", true));
    v.push_back(Entry("file.txt", false));
    v.push_back(Entry("A-dir", true));
    return v;
}

} // namespace

TEST_CASE("SWU1 name dialog", "[fileshare][sharewriteui]")
{
    OwnerWindow owner;
    REQUIRE(owner.m_hWnd != NULL);
    NameListener listener;
    CShareNameDialog dialog;
    REQUIRE(dialog.Open(owner.m_hWnd, kPurpose, L"重命名", L"新名称", L"old.txt", &listener));
    CHECK(dialog.IsOpen());
    //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 的大、小图标都已设置
    CHECK(dialog.GetIcon() != NULL);
    CHECK(::SendMessage(dialog.m_hWnd, WM_GETICON, ICON_BIG, 0) != 0);
    CHECK(::SendMessage(dialog.m_hWnd, WM_GETICON, ICON_SMALL, 0) != 0);
    CHECK_FALSE(::IsWindowEnabled(owner.m_hWnd));

    //不合法：显示原因、不回调、窗口保持打开
    dialog.SetInputText(L"a/b");
    CHECK_FALSE(dialog.Confirm());
    CHECK(dialog.GetErrorText().find(L"不能包含") != std::wstring::npos);
    CHECK(listener.m_count == 0);
    CHECK(dialog.IsOpen());
    dialog.SetInputText(L"");
    CHECK_FALSE(dialog.Confirm());
    CHECK(dialog.GetErrorText() == L"名称不能为空");

    //合法：回调、所有者恢复，窗口随后关闭
    dialog.SetInputText(L"新名字.txt");
    CHECK(dialog.Confirm());
    CHECK(listener.m_count == 1);
    CHECK(listener.m_purpose == kPurpose);
    CHECK(listener.m_name == L"新名字.txt");
    CHECK(::IsWindowEnabled(owner.m_hWnd));
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK_FALSE(dialog.IsOpen());

    //直接关闭：不回调，所有者恢复
    REQUIRE(dialog.Open(owner.m_hWnd, kPurpose, L"新建文件夹", L"文件夹名称", L"新建文件夹", &listener));
    CHECK_FALSE(::IsWindowEnabled(owner.m_hWnd));
    dialog.Close();
    CHECK(listener.m_count == 1);
    CHECK(::IsWindowEnabled(owner.m_hWnd));
}

TEST_CASE("SWU2 folder pick dialog", "[fileshare][sharewriteui]")
{
    OwnerWindow owner;
    REQUIRE(owner.m_hWnd != NULL);
    PickHarness h;
    CShareFolderPickDialog dialog;
    REQUIRE(dialog.Open(owner.m_hWnd, &h, &h, "docs", L"Projects"));
    CHECK_FALSE(::IsWindowEnabled(owner.m_hWnd));
    REQUIRE(h.m_requests.size() == 1);
    //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 的大、小图标都已设置
    CHECK(dialog.GetIcon() != NULL);
    CHECK(::SendMessage(dialog.m_hWnd, WM_GETICON, ICON_BIG, 0) != 0);
    CHECK(::SendMessage(dialog.m_hWnd, WM_GETICON, ICON_SMALL, 0) != 0);
    CHECK(h.m_requests[0] == "docs");
    //正在加载：不能确认
    CHECK_FALSE(dialog.ConfirmHere());

    //结果只列子文件夹
    dialog.OnListed("docs", 0, SampleEntries());
    CHECK(dialog.GetCurrentPath() == "docs");
    CHECK(dialog.GetFolderCount() == 2);

    //进入子文件夹（按名称排序后第一个是 A-dir）；与最近一次请求不符的结果忽略
    dialog.EnterFolder(0);
    REQUIRE(h.m_requests.size() == 2);
    CHECK(h.m_requests[1] == "docs/A-dir");
    dialog.OnListed("docs", 0, SampleEntries());
    CHECK(dialog.GetCurrentPath() == "docs");
    dialog.OnListed("docs/A-dir", 0, std::vector<ShareEntry>());
    CHECK(dialog.GetCurrentPath() == "docs/A-dir");
    CHECK(dialog.GetFolderCount() == 0);

    //上一级
    dialog.GoUp();
    REQUIRE(h.m_requests.size() == 3);
    CHECK(h.m_requests[2] == "docs");
    dialog.OnListed("docs", 0, SampleEntries());

    //移动到这里：交回当前目录，所有者恢复，窗口随后关闭并回调关闭
    CHECK(dialog.ConfirmHere());
    CHECK(h.m_pickedCount == 1);
    CHECK(h.m_picked == "docs");
    CHECK(::IsWindowEnabled(owner.m_hWnd));
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK_FALSE(dialog.IsOpen());
    CHECK(h.m_closedCount == 1);

    //直接关闭：不回调选中，只回调关闭
    REQUIRE(dialog.Open(owner.m_hWnd, &h, &h, "", L"Projects"));
    dialog.Close();
    CHECK(h.m_pickedCount == 1);
    CHECK(h.m_closedCount == 2);
}

TEST_CASE("SWU3 write dialogs snapshot", "[.snapshot][fileshare][sharewriteui]")
{
    OwnerWindow owner;
    NameListener listener;
    CShareNameDialog name;
    REQUIRE(name.Open(owner.m_hWnd, kPurpose, L"重命名", L"新名称", L"季度报告.docx", &listener));
    name.SetInputText(L"季度报告?.docx");
    name.Confirm();
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(name.m_hWnd, sharetestfs::ExeDir() + kSnapshotName));
    name.Close();

    PickHarness h;
    CShareFolderPickDialog pick;
    REQUIRE(pick.Open(owner.m_hWnd, &h, &h, "docs", L"Projects"));
    pick.OnListed("docs", 0, SampleEntries());
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(pick.m_hWnd, sharetestfs::ExeDir() + kSnapshotPick));
    pick.Close();
}
