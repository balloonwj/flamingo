/**
 *  flamingoAdmin —— 公告「重新推送」接口封装的单元测试（ARP3）。
 *
 *  公告中心的「重新推送」原先只弹「该功能将在后续阶段开放」。2026-10-04 起服务端提供
 *  announcement/repush.do（只接受已发布的公告，推给受众里在线且未读的成员，应答带推送
 *  人数 pushed），服务端的筛人规则由服务端测试 ARP1、ARP2 覆盖。本文件覆盖控制台一侧：
 *    - ARP3：请求的路径与请求体；成功应答解析出推送人数（缺字段按 0）；公告不是已发布
 *            状态（错误码 140）时给出中文提示；传输失败判为失败。
 *    - TXT1：公告级别的叫法统一为「普通 / 提醒 / 警告」。原先新建公告弹窗与导出用这一套，
 *            公告列表与详情的徽章却写「通知 / 提醒 / 安全公告」，同一条公告前后叫法不一致。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"

#include <string>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 构造一份测试用的服务器配置。主机与端口不影响被测的纯函数，取固定值即可。
ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = 12345;
    s.m_useTls = false;
    return s;
}

// 构造一个「传输成功、HTTP 200、body 为给定 JSON」的应答。
//   json：应答体（UTF-8 JSON 文本）。
HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

// ARP3：请求构造与应答解析。
void TestRepushRequestAndResponse()
{
    BeginCase("ARP3 重新推送：请求构造、推送人数解析与「不是已发布」的提示");

    const HttpRequest req = MakeAnnRepushRequest(MakeServer(), _T("tk"), 4321);
    FA_EXPECT_TRUE(req.m_path == L"/admin/announcement/repush.do");
    FA_EXPECT_TRUE(req.m_bodyUtf8.find("\"announcementid\":4321") != std::string::npos);
    FA_EXPECT_TRUE(req.m_bodyUtf8.find("\"token\":\"tk\"") != std::string::npos);

    const AnnRepushResult ok = ParseAnnRepushResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"pushed\":3}"));
    FA_EXPECT_TRUE(ok.Succeeded());
    FA_EXPECT_EQ(ok.m_pushed, 3);

    // 没有在线且未读的成员时服务端回 pushed=0，仍是成功；缺少该字段时同样按 0。
    const AnnRepushResult none = ParseAnnRepushResponse(MakeHttpOk("{\"code\":0,\"msg\":\"ok\"}"));
    FA_EXPECT_TRUE(none.Succeeded());
    FA_EXPECT_EQ(none.m_pushed, 0);

    const AnnRepushResult notPublished = ParseAnnRepushResponse(MakeHttpOk(
        "{\"code\":140,\"msg\":\"announcement not published\"}"));
    FA_EXPECT_FALSE(notPublished.Succeeded());
    FA_EXPECT_EQ(notPublished.m_code, 140);
    FA_EXPECT_STREQ(notPublished.m_message, _T("该公告不是已发布状态，不能重新推送"));

    // 传输失败：判为失败，提示取传输层给出的错误文字。
    HttpResult broken;
    broken.m_transportOk = false;
    broken.m_error = _T("无法连接服务器");
    const AnnRepushResult failed = ParseAnnRepushResponse(broken);
    FA_EXPECT_FALSE(failed.Succeeded());
    FA_EXPECT_STREQ(failed.m_message, _T("无法连接服务器"));
}

// TXT1：公告级别的统一叫法。
void TestAnnouncementSeverityLabels()
{
    BeginCase("TXT1 公告级别统一叫「普通 / 提醒 / 警告」，按服务端编号 0 / 1 / 2 排列");

    FA_EXPECT_STREQ(AnnSeverityLabel(Severity::Info), _T("普通"));
    FA_EXPECT_STREQ(AnnSeverityLabel(Severity::Normal), _T("普通"));
    FA_EXPECT_STREQ(AnnSeverityLabel(Severity::Warn), _T("提醒"));
    FA_EXPECT_STREQ(AnnSeverityLabel(Severity::Danger), _T("警告"));

    FA_EXPECT_EQ(kAnnSeverityLabelCount, 3);
    FA_EXPECT_STREQ(kAnnSeverityLabels[0], _T("普通"));
    FA_EXPECT_STREQ(kAnnSeverityLabels[1], _T("提醒"));
    FA_EXPECT_STREQ(kAnnSeverityLabels[2], _T("警告"));
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllAnnouncementRepushTests()
{
    TestRepushRequestAndResponse();
    TestAnnouncementSeverityLabels();
}

}} // namespace flamingoadmin::test
