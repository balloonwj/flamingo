/**
 *  NetSettingsTests.cpp
 *
 *  网络设置窗口的契约测试：
 *    N1-N5  BuildNetSettingsBody 产出的控件树符合"控件点名契约" ——
 *           三张服务器卡片 / 地址端口输入框 / 测试连接与保存按钮都能按
 *           既定数字 id 找到、类型正确、id 互不重复（N4 已随 TLS 开关移除）。
 *    N7-N9  NetSettingsModel 数据模型：默认值、三组配置互相独立、
 *           Select 越界被忽略。
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 / 操作模型 + 断言。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonNetSettings/NetSettingsLayout.h"
#include "../BalloonNetSettings/NetSettingsModel.h"
#include "../BalloonNetSettings/NetServerCard.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"

using namespace balloonwjui;
using namespace balloonnetsettings;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectNetIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectNetIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateNetId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// N. 网络设置窗布局契约
// =====================================================================

TEST_CASE("N1 BuildNetSettingsBody 返回非空控件树", "[netsettings]")
{
    auto root = BuildNetSettingsBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("N2 网络设置窗含三张服务器卡片且 kind 正确", "[netsettings]")
{
    auto root = BuildNetSettingsBody();
    REQUIRE(root != nullptr);

    auto* chat  = dynamic_cast<NetServerCard*>(root->FindCtrlById(kIdCardChat));
    auto* image = dynamic_cast<NetServerCard*>(root->FindCtrlById(kIdCardImage));
    auto* file  = dynamic_cast<NetServerCard*>(root->FindCtrlById(kIdCardFile));
    REQUIRE(chat  != nullptr);
    REQUIRE(image != nullptr);
    REQUIRE(file  != nullptr);

    CHECK(chat->GetServerKind()  == kNetServerChat);
    CHECK(image->GetServerKind() == kNetServerImage);
    CHECK(file->GetServerKind()  == kNetServerFile);
}

TEST_CASE("N3 网络设置窗含地址 / 端口输入框", "[netsettings]")
{
    auto root = BuildNetSettingsBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiEditHost*>(root->FindCtrlById(kIdEditHost)) != nullptr);
    CHECK(dynamic_cast<DuiEditHost*>(root->FindCtrlById(kIdEditPort)) != nullptr);
}

TEST_CASE("N5 网络设置窗含测试连接 / 保存按钮", "[netsettings]")
{
    auto root = BuildNetSettingsBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdBtnTest) != nullptr);
    CHECK(root->FindCtrlById(kIdBtnSave) != nullptr);
}

TEST_CASE("N6 网络设置窗控件 id 互不重复", "[netsettings]")
{
    auto root = BuildNetSettingsBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectNetIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateNetId(ids));
}

// =====================================================================
// N7-N9 NetSettingsModel 数据模型契约
// =====================================================================

TEST_CASE("N7 NetSettingsModel 默认选中聊天服务器、三组有默认端口", "[netsettings][model]")
{
    NetSettingsModel model;
    CHECK(model.Selected() == kNetServerChat);
    CHECK(model.Server(kNetServerChat).port  == 20000);
    CHECK(model.Server(kNetServerImage).port == 20002);
    CHECK(model.Server(kNetServerFile).port  == 20001);
    CHECK_FALSE(model.Server(kNetServerChat).host.IsEmpty());
}

TEST_CASE("N8 NetSettingsModel 三组配置互相独立、切换不串", "[netsettings][model]")
{
    NetSettingsModel model;

    NetServerEntry chat;
    chat.host = _T("chat.example.com");
    chat.port = 1111;
    model.SetServer(kNetServerChat, chat);

    NetServerEntry image;
    image.host = _T("image.example.com");
    image.port = 2222;
    model.SetServer(kNetServerImage, image);

    // 切到文件组 —— 不应影响已写入的聊天 / 图片两组。
    model.Select(kNetServerFile);
    CHECK(model.Selected() == kNetServerFile);

    CHECK(model.Server(kNetServerChat).host == CString(_T("chat.example.com")));
    CHECK(model.Server(kNetServerChat).port == 1111);

    CHECK(model.Server(kNetServerImage).host == CString(_T("image.example.com")));
    CHECK(model.Server(kNetServerImage).port == 2222);
}

TEST_CASE("N9 NetSettingsModel.Select 越界被忽略", "[netsettings][model]")
{
    NetSettingsModel model;

    model.Select(kNetServerFile);
    CHECK(model.Selected() == kNetServerFile);

    model.Select(99);    // 越界 —— 应被忽略
    CHECK(model.Selected() == kNetServerFile);

    model.Select(-1);    // 越界 —— 应被忽略
    CHECK(model.Selected() == kNetServerFile);
}
