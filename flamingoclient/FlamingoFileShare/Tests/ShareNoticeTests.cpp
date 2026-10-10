/**
 *  第 4 期：文件共享弹窗通知的单元测试（Suite SNM / SNP / SNC / SOA，标签 [sharenotice]），ShareNoticeTests.cpp
 *  覆盖 docs/文件共享方案.md 第 14.5 节：
 *    - SNM1 分享者五种动作单项与多项的标题；新建文件夹不通知；
 *    - SNM2 访问者上传、下载成功与失败的标题、说明、红字与操作链接（一项可「打开」，多项只「打开所在文件夹」，上传「查看」，
 *           失败「重试」，没有重试数据时不给）；传输特有的失败原因；
 *    - SNM3 是否通知：用户取消、拖出、浏览窗口在前台都不弹，其余（成功与失败）都弹；
 *    - SNM4 条目表：新的在前、显示 4 条、「还有 N 条」、按编号删除与查找、超过保留上限丢弃最早的；
 *    - SNM5 分享者条目：说明为共享名称，上传、重命名、移动有本机路径时「在文件夹中显示」，否则与下载、删除一样「查看」；
 *    - SNM6 分享者操作的归纳：写操作只计成功的条目（重命名的新路径、移动后的路径），新建文件夹与全部失败的不算；
 *           传输的条目（下载、上传、单文件共享）；时间显示为「时:分」；
 *    - SNM7 只有访问者下载成功的条目标记「说明是本机路径」，上传成功、失败与分享者的条目都不标记（2026-10-07）；
 *    - SNP1 弹窗显示条目文字与链接、底部「还有 N 条」与「查看全部」（只有分享者的弹窗有），整窗高度按条数；
 *    - SNP2 弹窗排得下：链接完整显示在窗口之内，说明一栏仍有宽度，且放得下实机核对时被截断的那一条保存位置（2026-10-07 加宽后补）；
 *    - SNP3 点链接经监听者回来的是对应条目与动作（控件通知处理完之后才回调），「查看全部」、关闭按钮；
 *    - SNP4 截图（标签 [.snapshot]）：两个弹窗存到 snapshots 目录供人工核对；
 *    - SNP5 尺寸按 DPI 换算：96 / 120 / 144 DPI 下宽 480 / 600 / 720，高度约按同一比例；弹窗实际的宽高等于按它所在 DPI
 *           算出的值（2026-10-07）；
 *    - SNP6 省略方式：下载成功那一条的说明省略路径中间（DT_PATH_ELLIPSIS），其余条目省略末尾（DT_END_ELLIPSIS）（2026-10-07）；
 *    - SNC1 访问者的传输：浏览窗口在前台、用户取消时不弹，不在前台时弹并响一声；
 *    - SNC2 分享者的操作：名字取宿主的通讯录名字，取不到时用服务端给的后备名字；
 *    - SNC3 宿主判定丢弃时不弹；推迟时不弹、解锁（判定变为照常）后补弹并响一声；推迟期间进入勿扰则丢弃；
 *    - SNC4 两个弹窗上下叠放：访问者的在分享者的上方，不重叠；关掉分享者的弹窗后访问者的落到右下角；
 *    - SNC5 链接动作：重试交给模块并删掉这一条、在资源管理器里显示（失败时改为打开「文件共享」窗口）、查看、打开所在文件夹、
 *           查看全部；提示音的最小间隔；
 *    - SNC6 压下后不弹、Reset 关闭弹窗清空条目、用户关掉弹窗后它的条目丢弃；
 *    - SOA1 分享者写操作经业务总控产生一次操作通知：重命名的新路径拼在共享根下，带共享名称；
 *    - SOA2 新建文件夹与全部失败的请求不产生通知，部分成功的删除只计成功的条目。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <cstdlib>    //std::abs：SNP5 比较换算后的高度
#include <time.h>     //localtime_s
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../Engine/ShareBrowseModel.h"   //ErrorText：失败原因与浏览窗口同一张文字表
#include "../Engine/ShareNoticeModel.h"
#include "../Engine/ShareService.h"
#include "../Engine/ShareLocalStore.h"
#include "../UI/ShareNoticePopup.h"
#include "../UI/ShareNoticeCenter.h"
#include "../../Source/net/Protocol.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ShareNoticeTestHost.h"
#include "ShareTestFs.h"
#include "ShareTestSnapshot.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的个人目录（SOA 用例，每个用例开始前整个删掉）
const wchar_t* const kSnTestDir = L"sn_test";

//几个共享与用户
const int64_t kShareA = 9201;
const int64_t kShareB = 9202;
const int64_t kVisitorA = 30002;
const int64_t kVisitorB = 30003;

//分享者不在线
const int kOwnerGone = error_code_share_unavailable;

//等待投给自己的消息被处理的时长，单位：毫秒
const DWORD kPumpMs = 50;

//截图位置（测试程序目录 Debug_Win32/Tests 往上三级是仓库根）
const wchar_t* const kSnapshotOwner = L"\\..\\..\\..\\snapshots\\fileshare_notice_owner.png";
const wchar_t* const kSnapshotVisitor = L"\\..\\..\\..\\snapshots\\fileshare_notice_visitor.png";

//截图前等窗口完成布局与绘制的时长，单位：毫秒
const DWORD kPaintWaitMs = 300;

//一笔访问者传输的结果
ShareVisitorTransferResult VisitorResult(int direction, int code)
{
    ShareVisitorTransferResult r;
    r.m_localId = 7;
    r.m_shareId = kShareA;
    r.m_direction = direction;
    r.m_code = code;
    r.m_label = L"a.txt";
    r.m_shareName = "team";
    r.m_topCount = 1;
    r.m_timeMs = 1700000000000LL;
    if (direction == share_transfer_download)
    {
        r.m_targetDir = L"D:\\dl";
        if (code == error_code_ok)
            r.m_localTopPath = L"D:\\dl\\a.txt";
        ShareTransferEntry e;
        e.m_path = "docs/a.txt";
        r.m_downloadItems.push_back(e);
    }
    else
    {
        r.m_toDir = "docs/sub";
        r.m_uploadItems.push_back(L"C:\\src\\a.txt");
    }
    return r;
}

//分享者的一次操作
ShareOwnerActivity OwnerActivity(int action, size_t count)
{
    ShareOwnerActivity a;
    a.m_shareId = kShareA;
    a.m_visitorId = kVisitorA;
    a.m_visitorName = "fallback";
    a.m_action = action;
    a.m_firstName = "a.txt";
    a.m_count = count;
    a.m_shareName = "team";
    a.m_timeMs = 1700000000000LL;
    if (action == share_audit_upload_file || action == share_audit_rename || action == share_audit_move)
    {
        a.m_revealRelPath = "docs/a.txt";
        a.m_revealPath = L"E:\\root\\docs\\a.txt";
    }
    return a;
}

//一条审计记录
ShareAuditEntry Audit(int action, const char* path, const char* toPath, int result)
{
    ShareAuditEntry e;
    e.m_shareId = kShareA;
    e.m_accessId = "acc";
    e.m_visitorId = kVisitorA;
    e.m_action = action;
    e.m_path = path;
    e.m_toPath = toPath;
    e.m_result = result;
    e.m_clientTime = 1700000000000LL;
    return e;
}

//一条条目的链接动作
std::vector<int> LinkActions(const ShareNoticeEntry& e)
{
    std::vector<int> out;
    for (size_t i = 0; i < e.m_links.size(); ++i)
        out.push_back(e.m_links[i].m_action);
    return out;
}

//在控件树里按控件 id 找控件；没有时返回 NULL
balloonwjui::DuiControl* FindById(balloonwjui::DuiControl* root, UINT id)
{
    if (root == NULL)
        return NULL;
    if (root->GetCtrlId() == id)
        return root;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = root->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        balloonwjui::DuiControl* found = FindById(kids[i].get(), id);
        if (found != NULL)
            return found;
    }
    return NULL;
}

//窗口高度
int WindowHeight(HWND hWnd)
{
    RECT rc = { 0, 0, 0, 0 };
    ::GetWindowRect(hWnd, &rc);
    return rc.bottom - rc.top;
}

//窗口宽度
int WindowWidth(HWND hWnd)
{
    RECT rc = { 0, 0, 0, 0 };
    ::GetWindowRect(hWnd, &rc);
    return rc.right - rc.left;
}

//弹窗监听者桩
class PopupListener : public IShareNoticePopupListener
{
public:
    virtual void OnNoticeLink(int kind, uint32_t entryId, int action) override
    {
        m_links.push_back(std::make_pair(kind, std::make_pair(entryId, action)));
    }

    virtual void OnNoticeViewAll(int kind) override
    {
        m_viewAll.push_back(kind);
    }

    virtual void OnNoticePopupClosed(int kind) override
    {
        m_closed.push_back(kind);
    }

    std::vector<std::pair<int, std::pair<uint32_t, int> > > m_links;   //点过的链接：(弹窗, (条目, 动作))
    std::vector<int>                                         m_viewAll; //点过「查看全部」的弹窗
    std::vector<int>                                         m_closed;  //被关掉的弹窗
};

//模块动作桩
class FakeActions : public IShareNoticeActions
{
public:
    virtual bool IsShareBrowserForeground(int64_t shareId) override
    {
        (void)shareId;
        return m_foreground;
    }

    virtual void NoticeOpenShareCenter(int64_t shareId) override
    {
        m_centerOpens.push_back(shareId);
    }

    virtual void NoticeBrowse(int64_t shareId, const std::string& dir) override
    {
        m_browses.push_back(std::make_pair(shareId, dir));
    }

    virtual bool NoticeRetry(const ShareVisitorTransferResult& result) override
    {
        m_retries.push_back(result);
        return m_retryOk;
    }

    virtual bool NoticeOpenPath(const std::wstring& path) override
    {
        m_opened.push_back(path);
        return true;
    }

    virtual bool NoticeRevealPath(const std::wstring& path) override
    {
        m_revealed.push_back(path);
        return m_revealOk;
    }

    bool m_foreground = false;   //IsShareBrowserForeground 的返回值
    bool m_retryOk = true;       //NoticeRetry 的返回值
    bool m_revealOk = true;      //NoticeRevealPath 的返回值
    std::vector<int64_t>                          m_centerOpens;   //打开「文件共享」窗口时要选中的共享
    std::vector<std::pair<int64_t, std::string> > m_browses;       //打开浏览窗口：(共享, 目录)
    std::vector<ShareVisitorTransferResult>       m_retries;       //重试的传输
    std::vector<std::wstring>                     m_opened;        //打开的路径
    std::vector<std::wstring>                     m_revealed;      //在资源管理器里选中的路径
};

//弹窗总控的夹具：宿主与动作桩在总控之前声明，总控先析构
struct CenterFixture
{
    noticetest::NoticeTestHost m_host;
    FakeActions                m_actions;
    CShareNoticeCenter         m_center;

    CenterFixture()
    {
        m_center.Attach(&m_host, &m_actions);
        m_center.SetSoundGapMs(0);
    }

    ~CenterFixture()
    {
        m_center.Detach();
    }
};

//「时:分」的期望值：按本地时区换算
std::wstring ExpectedClock(int64_t unixMs)
{
    const time_t seconds = static_cast<time_t>(unixMs / 1000);
    struct tm local;
    localtime_s(&local, &seconds);
    wchar_t buf[16] = { 0 };
    _snwprintf_s(buf, 16, _TRUNCATE, L"%02d:%02d", local.tm_hour, local.tm_min);
    return buf;
}

//一段文字用 font 显示时的宽度，单位：像素；与弹窗里量链接宽度的方法相同（GetTextExtentPoint32W）
int NoticeTextWidthPx(HFONT font, const std::wstring& text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

} // namespace

TEST_CASE("SNM1 owner notice titles", "[fileshare][sharenotice]")
{
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_download_file, L"报告.docx", 1, L"") == L"张三 下载了 报告.docx");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_download_file, L"报告.docx", 3, L"")
          == L"张三 下载了 报告.docx 等 3 项");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_upload_file, L"图片", 1, L"") == L"张三 上传了 图片");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_upload_file, L"图片", 2, L"") == L"张三 上传了 图片 等 2 项");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_rename, L"a.txt", 1, L"b.txt") == L"张三 把 a.txt 重命名为 b.txt");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_delete, L"a.docx", 1, L"") == L"张三 删除了 a.docx");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_delete, L"a.docx", 20, L"") == L"张三 删除了 a.docx 等 20 项");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_move, L"a.txt", 1, L"") == L"张三 把 a.txt 移动到共享的根目录");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_move, L"a.txt", 2, L"") == L"张三 把 a.txt 等 2 项移动到共享的根目录");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_move, L"a.txt", 1, L"设计") == L"张三 把 a.txt 移动到「设计」");
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_move, L"a.txt", 2, L"设计")
          == L"张三 把 a.txt 等 2 项移动到「设计」");
    //新建文件夹不通知
    CHECK(ShareNoticeModel::OwnerTitle(L"张三", share_audit_mkdir, L"新建文件夹", 1, L"").empty());
    CHECK_FALSE(ShareNoticeModel::IsOwnerActionNotified(share_audit_mkdir));
    CHECK_FALSE(ShareNoticeModel::IsOwnerActionNotified(share_audit_open));
    const int notified[] = { share_audit_download_file, share_audit_upload_file, share_audit_rename, share_audit_delete,
                             share_audit_move };
    for (size_t i = 0; i < sizeof(notified) / sizeof(notified[0]); ++i)
        CHECK(ShareNoticeModel::IsOwnerActionNotified(notified[i]));
}

TEST_CASE("SNM2 visitor notice entries", "[fileshare][sharenotice]")
{
    //下载成功、一项：「打开」「打开所在文件夹」
    ShareNoticeEntry e = ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, 0));
    CHECK(e.m_title == L"下载完成：a.txt");
    CHECK(e.m_detail == L"已保存到 D:\\dl");
    CHECK_FALSE(e.m_failed);
    CHECK(e.m_time == ExpectedClock(1700000000000LL));
    CHECK(LinkActions(e) == std::vector<int>({ share_notice_open_file, share_notice_open_folder }));
    REQUIRE(e.m_links.size() == 2);
    CHECK(e.m_links[0].m_text == L"打开");
    CHECK(e.m_links[1].m_text == L"打开所在文件夹");

    //下载成功、多项：只有「打开所在文件夹」
    ShareVisitorTransferResult many = VisitorResult(share_transfer_download, 0);
    many.m_topCount = 3;
    many.m_label = L"a.txt 等 3 项";
    e = ShareNoticeModel::MakeVisitorEntry(many);
    CHECK(e.m_title == L"下载完成：a.txt 等 3 项");
    CHECK(LinkActions(e) == std::vector<int>(1, share_notice_open_folder));

    //上传成功：说明写上传到的位置，链接「查看」
    e = ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_upload, 0));
    CHECK(e.m_title == L"上传完成：a.txt");
    CHECK(e.m_detail == L"已上传到「team / docs / sub」");
    CHECK(LinkActions(e) == std::vector<int>(1, share_notice_browse));
    ShareVisitorTransferResult root = VisitorResult(share_transfer_upload, 0);
    root.m_toDir.clear();
    root.m_shareName.clear();
    CHECK(ShareNoticeModel::MakeVisitorEntry(root).m_detail == L"已上传到共享的根目录");

    //失败：红字原因、「重试」；没有重试数据时不给链接
    e = ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, kOwnerGone));
    CHECK(e.m_title == L"下载失败：a.txt");
    CHECK(e.m_failed);
    CHECK(e.m_detail == ShareBrowseModel::ErrorText(kOwnerGone));
    CHECK(LinkActions(e) == std::vector<int>(1, share_notice_retry));
    CHECK(e.m_links[0].m_text == L"重试");
    ShareVisitorTransferResult noItems = VisitorResult(share_transfer_upload, kOwnerGone);
    noItems.m_uploadItems.clear();
    e = ShareNoticeModel::MakeVisitorEntry(noItems);
    CHECK(e.m_title == L"上传失败：a.txt");
    CHECK(e.m_links.empty());

    //传输特有的原因
    CHECK(ShareNoticeModel::FailureReason(error_code_share_name_conflict) == L"目标位置已有同名的文件或文件夹");
    CHECK(ShareNoticeModel::FailureReason(error_code_share_disk_full) == L"磁盘空间不足");
    CHECK(ShareNoticeModel::FailureReason(error_code_share_transfer_busy) == L"对方同时传输的人数已满，请稍后再试");
    CHECK(ShareNoticeModel::FailureReason(error_code_share_transfer_canceled) == L"传输被中止");
    CHECK(ShareNoticeModel::FailureReason(error_code_share_relay_broken) == ShareBrowseModel::ErrorText(error_code_share_relay_broken));
}

TEST_CASE("SNM3 whether a visitor transfer is notified", "[fileshare][sharenotice]")
{
    ShareVisitorTransferResult r = VisitorResult(share_transfer_download, 0);
    CHECK(ShareNoticeModel::ShouldNotifyVisitorTransfer(r, false));
    CHECK_FALSE(ShareNoticeModel::ShouldNotifyVisitorTransfer(r, true));
    ShareVisitorTransferResult failed = VisitorResult(share_transfer_upload, kOwnerGone);
    CHECK(ShareNoticeModel::ShouldNotifyVisitorTransfer(failed, false));
    CHECK_FALSE(ShareNoticeModel::ShouldNotifyVisitorTransfer(failed, true));
    ShareVisitorTransferResult canceled = VisitorResult(share_transfer_download, error_code_share_transfer_canceled);
    canceled.m_userCanceled = true;
    CHECK_FALSE(ShareNoticeModel::ShouldNotifyVisitorTransfer(canceled, false));
    ShareVisitorTransferResult drag = VisitorResult(share_transfer_download, 0);
    drag.m_dragOut = true;
    CHECK_FALSE(ShareNoticeModel::ShouldNotifyVisitorTransfer(drag, false));
}

TEST_CASE("SNM4 notice list keeps the newest first and caps its size", "[fileshare][sharenotice]")
{
    CShareNoticeList list;
    std::vector<uint32_t> ids;
    for (int i = 0; i < 6; ++i)
    {
        ShareNoticeEntry e;
        e.m_title = L"t" + std::to_wstring(i);
        ids.push_back(list.Add(e));
    }
    CHECK(ids[0] == 1);
    CHECK(ids[5] == 6);
    CHECK(list.Size() == 6);
    std::vector<ShareNoticeEntry> visible = list.Visible();
    REQUIRE(visible.size() == kShareNoticeVisibleRows);
    CHECK(visible[0].m_id == 6);
    CHECK(visible[0].m_title == L"t5");
    CHECK(visible[3].m_id == 3);
    CHECK(list.OverflowCount() == 2);
    CHECK(ShareNoticeModel::OverflowText(2) == L"还有 2 条");

    //按编号删除、查找
    CHECK(list.Remove(5));
    CHECK_FALSE(list.Remove(5));
    CHECK(list.Find(5) == NULL);
    REQUIRE(list.Find(4) != NULL);
    CHECK(list.Find(4)->m_title == L"t3");
    visible = list.Visible();
    CHECK(visible[1].m_id == 4);
    CHECK(list.OverflowCount() == 1);

    //超过保留上限：丢弃最早的
    for (size_t i = 0; i < kShareNoticeKeep; ++i)
        list.Add(ShareNoticeEntry());
    CHECK(list.Size() == kShareNoticeKeep);
    CHECK(list.Find(1) == NULL);
    list.Clear();
    CHECK(list.Size() == 0);
    CHECK(list.OverflowCount() == 0);
    //清空之后编号继续递增
    CHECK(list.Add(ShareNoticeEntry()) > 6 + kShareNoticeKeep - 1);
}

TEST_CASE("SNM5 owner notice entries", "[fileshare][sharenotice]")
{
    ShareNoticeEntry e = ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_upload_file, 1), L"张三");
    CHECK(e.m_title == L"张三 上传了 a.txt");
    CHECK(e.m_detail == L"共享「team」");
    CHECK(e.m_time == ExpectedClock(1700000000000LL));
    REQUIRE(e.m_links.size() == 1);
    CHECK(e.m_links[0].m_action == share_notice_reveal);
    CHECK(e.m_links[0].m_text == L"在文件夹中显示");

    //拼不出本机路径：退回「查看」
    ShareOwnerActivity noPath = OwnerActivity(share_audit_rename, 1);
    noPath.m_targetName = "b.txt";
    noPath.m_revealPath.clear();
    e = ShareNoticeModel::MakeOwnerEntry(noPath, L"张三");
    CHECK(e.m_title == L"张三 把 a.txt 重命名为 b.txt");
    CHECK(LinkActions(e) == std::vector<int>(1, share_notice_view_share));

    //下载、删除：「查看」
    e = ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_download_file, 2), L"张三");
    CHECK(e.m_title == L"张三 下载了 a.txt 等 2 项");
    CHECK(LinkActions(e) == std::vector<int>(1, share_notice_view_share));
    CHECK(e.m_links[0].m_text == L"查看");
    e = ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_delete, 1), L"张三");
    CHECK(LinkActions(e) == std::vector<int>(1, share_notice_view_share));

    //移动到多级目录：各级之间用「 / 」
    ShareOwnerActivity move = OwnerActivity(share_audit_move, 1);
    move.m_targetName = "设计/图标";
    CHECK(ShareNoticeModel::MakeOwnerEntry(move, L"张三").m_title == L"张三 把 a.txt 移动到「设计 / 图标」");

    //不知道共享名称：没有说明
    ShareOwnerActivity noName = OwnerActivity(share_audit_delete, 1);
    noName.m_shareName.clear();
    CHECK(ShareNoticeModel::MakeOwnerEntry(noName, L"张三").m_detail.empty());
}

TEST_CASE("SNM6 owner activities from write ops and transfers", "[fileshare][sharenotice]")
{
    ShareOwnerActivity a;
    //重命名：新路径在原来的目录下
    std::vector<ShareAuditEntry> rename(1, Audit(share_audit_rename, "sub/a.txt", "b.txt", 0));
    REQUIRE(ShareNoticeModel::MakeWriteActivity(rename, a));
    CHECK(a.m_action == share_audit_rename);
    CHECK(a.m_firstName == "a.txt");
    CHECK(a.m_targetName == "b.txt");
    CHECK(a.m_revealRelPath == "sub/b.txt");
    CHECK(a.m_count == 1);
    CHECK(a.m_shareId == kShareA);
    CHECK(a.m_visitorId == kVisitorA);
    CHECK(a.m_timeMs == 1700000000000LL);

    //移动：移过去之后在目标目录下
    std::vector<ShareAuditEntry> move;
    move.push_back(Audit(share_audit_move, "x/a.txt", "dst/y", 0));
    move.push_back(Audit(share_audit_move, "x/b.txt", "dst/y", 0));
    REQUIRE(ShareNoticeModel::MakeWriteActivity(move, a));
    CHECK(a.m_count == 2);
    CHECK(a.m_targetName == "dst/y");
    CHECK(a.m_revealRelPath == "dst/y/a.txt");

    //删除三项其中一项失败：只计成功的两项，第一项取第一个成功的
    std::vector<ShareAuditEntry> del;
    del.push_back(Audit(share_audit_delete, "a.txt", "", error_code_share_recycle_unavailable));
    del.push_back(Audit(share_audit_delete, "b.txt", "", 0));
    del.push_back(Audit(share_audit_delete, "c.txt", "", 0));
    REQUIRE(ShareNoticeModel::MakeWriteActivity(del, a));
    CHECK(a.m_count == 2);
    CHECK(a.m_firstName == "b.txt");
    CHECK(a.m_revealRelPath.empty());

    //新建文件夹、全部失败：不算
    std::vector<ShareAuditEntry> mkdir(1, Audit(share_audit_mkdir, "new", "", 0));
    CHECK_FALSE(ShareNoticeModel::MakeWriteActivity(mkdir, a));
    std::vector<ShareAuditEntry> allFailed(1, Audit(share_audit_delete, "a.txt", "", error_code_share_io_fail));
    CHECK_FALSE(ShareNoticeModel::MakeWriteActivity(allFailed, a));

    //传输：下载取请求的条目，上传取顶层条目，单文件共享的条目是空串
    std::vector<std::string> paths;
    paths.push_back("docs/a.txt");
    paths.push_back("docs/b.txt");
    REQUIRE(ShareNoticeModel::MakeTransferActivity(share_transfer_download, paths, std::string(), a));
    CHECK(a.m_action == share_audit_download_file);
    CHECK(a.m_firstName == "a.txt");
    CHECK(a.m_count == 2);
    CHECK(a.m_revealRelPath.empty());
    REQUIRE(ShareNoticeModel::MakeTransferActivity(share_transfer_upload, std::vector<std::string>(1, "pics"), "inbox", a));
    CHECK(a.m_action == share_audit_upload_file);
    CHECK(a.m_firstName == "pics");
    CHECK(a.m_revealRelPath == "inbox/pics");
    REQUIRE(ShareNoticeModel::MakeTransferActivity(share_transfer_download, std::vector<std::string>(1, ""), "", a));
    CHECK(a.m_firstName.empty());
    CHECK_FALSE(ShareNoticeModel::MakeTransferActivity(share_transfer_download, std::vector<std::string>(), "", a));

    CHECK(ShareNoticeModel::FormatClock(0).empty());
    CHECK(ShareNoticeModel::LastSegment("a/b/c.txt") == "c.txt");
}

TEST_CASE("SNM7 only a successful download marks its detail as a local path", "[fileshare][sharenotice]")
{
    //下载成功：「已保存到 D:\dl」是本机路径
    const ShareNoticeEntry downloaded = ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, error_code_ok));
    CHECK(downloaded.m_detail == L"已保存到 D:\\dl");
    CHECK(downloaded.m_detailIsPath);

    //上传成功、下载失败、上传失败：说明不是本机路径
    CHECK_FALSE(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_upload, error_code_ok)).m_detailIsPath);
    CHECK_FALSE(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, kOwnerGone)).m_detailIsPath);
    CHECK_FALSE(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_upload, kOwnerGone)).m_detailIsPath);

    //分享者的条目：说明是共享名称
    CHECK_FALSE(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_delete, 1), L"张三").m_detailIsPath);
    CHECK_FALSE(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_upload_file, 1), L"张三").m_detailIsPath);
}

TEST_CASE("SNP1 popup shows entries, footer and height by rows", "[fileshare][sharenotice]")
{
    PopupListener listener;
    CShareNoticePopup owner;
    owner.SetKind(share_notice_popup_owner, &listener);
    std::vector<ShareNoticeEntry> entries;
    entries.push_back(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_delete, 20), L"张三"));
    entries.push_back(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_upload_file, 1), L"李四"));
    REQUIRE(owner.ShowEntries(entries, 3));
    CHECK(owner.GetRowCount() == 2);
    CHECK(owner.GetRowTitle(0) == L"张三 删除了 a.txt 等 20 项");
    CHECK(owner.GetRowDetail(0) == L"共享「team」");
    CHECK(owner.GetRowLinkTexts(0) == std::vector<std::wstring>(1, L"查看"));
    CHECK(owner.GetRowLinkTexts(1) == std::vector<std::wstring>(1, L"在文件夹中显示"));
    CHECK(owner.GetFooterText() == L"还有 3 条");
    CHECK(owner.HasViewAllLink());
    CHECK(WindowHeight(owner.m_hWnd) == CShareNoticePopup::HeightFor(2, true, owner.GetDpi()));
    //新建的弹窗由调用方摆好之后再显示
    CHECK_FALSE(owner.IsShowing());

    //访问者的弹窗：没有「查看全部」，没有更多条目时没有底部一行
    CShareNoticePopup visitor;
    visitor.SetKind(share_notice_popup_visitor, &listener);
    REQUIRE(visitor.ShowEntries(std::vector<ShareNoticeEntry>(1, ShareNoticeModel::MakeVisitorEntry(
        VisitorResult(share_transfer_download, 0))), 0));
    CHECK_FALSE(visitor.HasViewAllLink());
    CHECK(visitor.GetFooterText().empty());
    CHECK(WindowHeight(visitor.m_hWnd) == CShareNoticePopup::HeightFor(1, false, visitor.GetDpi()));
    CHECK(CShareNoticePopup::HeightFor(4, true, visitor.GetDpi()) > CShareNoticePopup::HeightFor(1, true, visitor.GetDpi()));

    //没有条目：等同关闭
    CHECK_FALSE(visitor.ShowEntries(std::vector<ShareNoticeEntry>(), 0));
    CHECK_FALSE(visitor.IsWindow());
    owner.Hide();
}

TEST_CASE("SNP2 popup links fit inside the window", "[fileshare][sharenotice]")
{
    PopupListener listener;
    CShareNoticePopup visitor;
    visitor.SetKind(share_notice_popup_visitor, &listener);
    //最长的一组链接：「打开」「打开所在文件夹」
    REQUIRE(visitor.ShowEntries(std::vector<ShareNoticeEntry>(1, ShareNoticeModel::MakeVisitorEntry(
        VisitorResult(share_transfer_download, 0))), 0));
    sharesnapshot::PumpMessagesFor(kPumpMs);
    RECT client = { 0, 0, 0, 0 };
    ::GetClientRect(visitor.m_hWnd, &client);
    for (size_t k = 0; k < 2; ++k)
    {
        balloonwjui::DuiControl* link = FindById(visitor.GetClientContent(), visitor.GetLinkCtrlId(0, k));
        REQUIRE(link != NULL);
        const RECT& rc = link->GetRect();
        INFO("link " << k << " rect " << rc.left << "," << rc.right);
        CHECK(rc.right <= client.right);
        CHECK(rc.right - rc.left > 0);
        //说明一栏仍有宽度：它在同一行最左边
        balloonwjui::DuiControl* row = link->GetParent();
        REQUIRE(row != NULL);
        const RECT& detail = row->Children()[0]->GetRect();
        CHECK(detail.right - detail.left > 60);
        CHECK(detail.right <= rc.left);
    }

    //说明一栏放得下 2026-10-07 实机核对时被截断的那一条保存位置（弹窗当天由 380 加宽到 480；
    //380 宽时这一栏为 220 像素，这一句需要 245 像素（测试进程的字体下实测），显示成「已保存到 E:\flamingo\tm…」）
    balloonwjui::DuiControl* firstLink = FindById(visitor.GetClientContent(), visitor.GetLinkCtrlId(0, 0));
    REQUIRE(firstLink != NULL);
    REQUIRE(firstLink->GetParent() != NULL);
    balloonwjui::DuiControl* detailCtrl = firstLink->GetParent()->Children()[0].get();
    const std::wstring liveSaved = L"已保存到 E:\\flamingo\\tmp\\fsplace\\live\\dl_b";
    const int needed = NoticeTextWidthPx(detailCtrl->GetDefaultFont(), liveSaved);
    const RECT& detailRc = detailCtrl->GetRect();
    INFO("detail width " << (detailRc.right - detailRc.left) << ", needed " << needed);
    CHECK(detailRc.right - detailRc.left >= needed);
    visitor.Hide();
}

TEST_CASE("SNP3 clicking a link reports the entry and action after the notification", "[fileshare][sharenotice]")
{
    PopupListener listener;
    CShareNoticePopup owner;
    owner.SetKind(share_notice_popup_owner, &listener);
    CShareNoticeList list;
    list.Add(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_upload_file, 1), L"张三"));
    const uint32_t second = list.Add(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_delete, 1), L"李四"));
    REQUIRE(owner.ShowEntries(list.Visible(), list.OverflowCount()));

    //最上面一条（最新的，删除）的「查看」
    balloonwjui::DuiNotify n;
    n.code = balloonwjui::DUIN_CLICK;
    n.ctrlId = owner.GetLinkCtrlId(0, 0);
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessage(owner.m_hWnd, WM_DUI_NOTIFY, n.ctrlId, reinterpret_cast<LPARAM>(&n));
    //控件通知处理完之后才回调
    CHECK(listener.m_links.empty());
    sharesnapshot::PumpMessagesFor(kPumpMs);
    REQUIRE(listener.m_links.size() == 1);
    CHECK(listener.m_links[0].first == share_notice_popup_owner);
    CHECK(listener.m_links[0].second.first == second);
    CHECK(listener.m_links[0].second.second == share_notice_view_share);

    //「查看全部」
    n.ctrlId = CShareNoticePopup::ViewAllCtrlId();
    ::SendMessage(owner.m_hWnd, WM_DUI_NOTIFY, n.ctrlId, reinterpret_cast<LPARAM>(&n));
    sharesnapshot::PumpMessagesFor(kPumpMs);
    CHECK(listener.m_viewAll == std::vector<int>(1, share_notice_popup_owner));

    //别的控件的同值通知不处理
    n.ctrlId = 9999;
    ::SendMessage(owner.m_hWnd, WM_DUI_NOTIFY, n.ctrlId, reinterpret_cast<LPARAM>(&n));
    sharesnapshot::PumpMessagesFor(kPumpMs);
    CHECK(listener.m_links.size() == 1);

    //关闭按钮：窗口销毁并告诉监听者
    ::SendMessage(owner.m_hWnd, WM_CLOSE, 0, 0);
    CHECK_FALSE(owner.IsWindow());
    CHECK(listener.m_closed == std::vector<int>(1, share_notice_popup_owner));
}

TEST_CASE("SNP5 popup sizes scale with DPI", "[fileshare][sharenotice]")
{
    //三种常见缩放比例 100%、125%、150% 的 DPI，与各自期望的弹窗宽度（480 为 100% 下的设计宽度）
    const int kDpi100 = 96;
    const int kDpi125 = 120;
    const int kDpi150 = 144;
    const int kWidth100 = 480;
    const int kWidth125 = 600;
    const int kWidth150 = 720;
    CHECK(CShareNoticePopup::PopupWidth(kDpi100) == kWidth100);
    CHECK(CShareNoticePopup::PopupWidth(kDpi125) == kWidth125);
    CHECK(CShareNoticePopup::PopupWidth(kDpi150) == kWidth150);

    //高度：各段尺寸分别换算取整后相加，与整体按比例放大的值相差不超过这么多像素
    const int kRoundingSlackPx = 4;
    for (size_t rows = 1; rows <= kShareNoticeVisibleRows; ++rows)
    {
        for (int f = 0; f < 2; ++f)
        {
            const bool footer = (f != 0);
            const int h100 = CShareNoticePopup::HeightFor(rows, footer, kDpi100);
            const int h125 = CShareNoticePopup::HeightFor(rows, footer, kDpi125);
            const int h150 = CShareNoticePopup::HeightFor(rows, footer, kDpi150);
            INFO("rows " << rows << " footer " << footer << " heights " << h100 << " / " << h125 << " / " << h150);
            CHECK(h100 < h125);
            CHECK(h125 < h150);
            CHECK(std::abs(h125 - ::MulDiv(h100, kDpi125, kDpi100)) <= kRoundingSlackPx);
            CHECK(std::abs(h150 - ::MulDiv(h100, kDpi150, kDpi100)) <= kRoundingSlackPx);
        }
    }

    //实际的窗口：宽高等于按它所在显示器的 DPI 算出的值
    PopupListener listener;
    CShareNoticePopup visitor;
    visitor.SetKind(share_notice_popup_visitor, &listener);
    REQUIRE(visitor.ShowEntries(std::vector<ShareNoticeEntry>(1, ShareNoticeModel::MakeVisitorEntry(
        VisitorResult(share_transfer_download, error_code_ok))), 0));
    CHECK(visitor.GetDpi() >= kDpi100);
    CHECK(WindowWidth(visitor.m_hWnd) == CShareNoticePopup::PopupWidth(visitor.GetDpi()));
    CHECK(WindowHeight(visitor.m_hWnd) == CShareNoticePopup::HeightFor(1, false, visitor.GetDpi()));
    visitor.Hide();
}

TEST_CASE("SNP6 a saved path is shortened in the middle, other details at the end", "[fileshare][sharenotice]")
{
    PopupListener listener;
    CShareNoticePopup visitor;
    visitor.SetKind(share_notice_popup_visitor, &listener);
    std::vector<ShareNoticeEntry> entries;
    entries.push_back(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, error_code_ok)));
    entries.push_back(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_upload, error_code_ok)));
    entries.push_back(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, kOwnerGone)));
    REQUIRE(visitor.ShowEntries(entries, 0));
    REQUIRE(visitor.GetRowCount() == entries.size());

    //第 0 条是下载成功：「已保存到 …」省略路径中间
    CHECK((visitor.GetRowDetailAlign(0) & DT_PATH_ELLIPSIS) != 0);
    CHECK((visitor.GetRowDetailAlign(0) & DT_END_ELLIPSIS) == 0);
    //上传成功与下载失败：省略末尾
    for (size_t r = 1; r < entries.size(); ++r)
    {
        INFO("row " << r);
        CHECK((visitor.GetRowDetailAlign(r) & DT_END_ELLIPSIS) != 0);
        CHECK((visitor.GetRowDetailAlign(r) & DT_PATH_ELLIPSIS) == 0);
    }
    visitor.Hide();

    //分享者的条目（说明是共享名称）：省略末尾
    CShareNoticePopup owner;
    owner.SetKind(share_notice_popup_owner, &listener);
    REQUIRE(owner.ShowEntries(std::vector<ShareNoticeEntry>(1, ShareNoticeModel::MakeOwnerEntry(
        OwnerActivity(share_audit_delete, 1), L"张三")), 0));
    CHECK((owner.GetRowDetailAlign(0) & DT_END_ELLIPSIS) != 0);
    CHECK((owner.GetRowDetailAlign(0) & DT_PATH_ELLIPSIS) == 0);
    owner.Hide();
}

TEST_CASE("SNP4 notice popup snapshots", "[.snapshot][sharenotice]")
{
    PopupListener listener;
    CShareNoticePopup owner;
    owner.SetKind(share_notice_popup_owner, &listener);
    std::vector<ShareNoticeEntry> entries;
    ShareOwnerActivity rename = OwnerActivity(share_audit_rename, 1);
    rename.m_targetName = "季度汇报（定稿）.pptx";
    entries.push_back(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_delete, 20), L"张三"));
    entries.push_back(ShareNoticeModel::MakeOwnerEntry(rename, L"李四"));
    entries.push_back(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_upload_file, 3), L"王五"));
    entries.push_back(ShareNoticeModel::MakeOwnerEntry(OwnerActivity(share_audit_download_file, 1), L"赵六"));
    REQUIRE(owner.ShowEntries(entries, 2));
    owner.ShowNoActivate();
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(owner.m_hWnd, sharetestfs::ExeDir() + kSnapshotOwner));

    CShareNoticePopup visitor;
    visitor.SetKind(share_notice_popup_visitor, &listener);
    std::vector<ShareNoticeEntry> transfers;
    ShareVisitorTransferResult many = VisitorResult(share_transfer_download, 0);
    many.m_topCount = 3;
    many.m_label = L"设计稿 等 3 项";
    transfers.push_back(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, 0)));
    transfers.push_back(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_upload, 0)));
    transfers.push_back(ShareNoticeModel::MakeVisitorEntry(VisitorResult(share_transfer_download, kOwnerGone)));
    transfers.push_back(ShareNoticeModel::MakeVisitorEntry(many));
    REQUIRE(visitor.ShowEntries(transfers, 0));
    visitor.ShowNoActivate();
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(visitor.m_hWnd, sharetestfs::ExeDir() + kSnapshotVisitor));
    owner.Hide();
    visitor.Hide();
}

TEST_CASE("SNC1 visitor transfers pop up only when the browser is not in front", "[fileshare][sharenotice]")
{
    CenterFixture f;
    //浏览窗口在前台：不弹
    f.m_actions.m_foreground = true;
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    CHECK(f.m_center.GetEntryCount(share_notice_popup_visitor) == 0);
    CHECK_FALSE(f.m_center.GetPopup(share_notice_popup_visitor).IsShowing());

    //用户取消：不弹
    f.m_actions.m_foreground = false;
    ShareVisitorTransferResult canceled = VisitorResult(share_transfer_download, error_code_share_transfer_canceled);
    canceled.m_userCanceled = true;
    f.m_center.OnVisitorTransferFinished(canceled);
    CHECK(f.m_center.GetEntryCount(share_notice_popup_visitor) == 0);

    //不在前台：弹出并响一声；失败的也弹
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_upload, kOwnerGone));
    CHECK(f.m_center.GetEntryCount(share_notice_popup_visitor) == 2);
    CShareNoticePopup& popup = f.m_center.GetPopup(share_notice_popup_visitor);
    CHECK(popup.IsShowing());
    CHECK(popup.GetRowTitle(0) == L"上传失败：a.txt");
    CHECK(popup.GetRowTitle(1) == L"下载完成：a.txt");
    CHECK(f.m_host.m_sounds == 2);
    CHECK_FALSE(f.m_center.GetPopup(share_notice_popup_owner).IsWindow());
}

TEST_CASE("SNC2 owner activities use the contact name", "[fileshare][sharenotice]")
{
    CenterFixture f;
    f.m_host.m_names[kVisitorA] = "张三";
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 20));
    ShareOwnerActivity other = OwnerActivity(share_audit_download_file, 1);
    other.m_visitorId = kVisitorB;
    f.m_center.OnOwnerActivity(other);
    //新建文件夹不弹
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_mkdir, 1));
    CShareNoticePopup& popup = f.m_center.GetPopup(share_notice_popup_owner);
    REQUIRE(popup.GetRowCount() == 2);
    //通讯录里没有的：用服务端给的后备名字
    CHECK(popup.GetRowTitle(0) == L"fallback 下载了 a.txt");
    CHECK(popup.GetRowTitle(1) == L"张三 删除了 a.txt 等 20 项");
    CHECK(popup.IsShowing());
    CHECK(popup.HasViewAllLink());
}

TEST_CASE("SNC3 host policy drops or defers notices", "[fileshare][sharenotice]")
{
    CenterFixture f;
    //勿扰：丢弃
    f.m_host.m_policy = fileshare_notice_drop;
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    CHECK(f.m_center.GetEntryCount(share_notice_popup_owner) == 0);
    CHECK(f.m_host.m_sounds == 0);

    //锁屏：先记下，不弹不响
    f.m_host.m_policy = fileshare_notice_defer;
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    CHECK(f.m_center.GetEntryCount(share_notice_popup_owner) == 1);
    CHECK(f.m_center.HasDeferred());
    CHECK_FALSE(f.m_center.GetPopup(share_notice_popup_owner).IsShowing());
    CHECK(f.m_host.m_sounds == 0);
    f.m_center.OnTimer();
    CHECK_FALSE(f.m_center.GetPopup(share_notice_popup_owner).IsShowing());

    //解锁：补弹，响一声
    f.m_host.m_policy = fileshare_notice_show;
    f.m_center.OnTimer();
    CHECK_FALSE(f.m_center.HasDeferred());
    CHECK(f.m_center.GetPopup(share_notice_popup_owner).IsShowing());
    CHECK(f.m_center.GetPopup(share_notice_popup_visitor).IsShowing());
    CHECK(f.m_host.m_sounds == 1);

    //推迟期间进入勿扰：丢弃
    f.m_center.Reset();
    f.m_host.m_policy = fileshare_notice_defer;
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    f.m_host.m_policy = fileshare_notice_drop;
    f.m_center.OnTimer();
    CHECK_FALSE(f.m_center.HasDeferred());
    CHECK(f.m_center.GetEntryCount(share_notice_popup_owner) == 0);
    CHECK_FALSE(f.m_center.GetPopup(share_notice_popup_owner).IsWindow());
}

TEST_CASE("SNC4 the two popups are stacked without overlapping", "[fileshare][sharenotice]")
{
    CenterFixture f;
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    CShareNoticePopup& owner = f.m_center.GetPopup(share_notice_popup_owner);
    CShareNoticePopup& visitor = f.m_center.GetPopup(share_notice_popup_visitor);
    REQUIRE(owner.IsShowing());
    REQUIRE(visitor.IsShowing());
    RECT ro;
    RECT rv;
    ::GetWindowRect(owner.m_hWnd, &ro);
    ::GetWindowRect(visitor.m_hWnd, &rv);
    CHECK(rv.bottom <= ro.top);
    CHECK(rv.right == ro.right);

    //关掉分享者的弹窗：访问者的落到它原来的位置
    ::SendMessage(owner.m_hWnd, WM_CLOSE, 0, 0);
    CHECK(f.m_center.GetEntryCount(share_notice_popup_owner) == 0);
    RECT moved;
    ::GetWindowRect(visitor.m_hWnd, &moved);
    CHECK(moved.bottom == ro.bottom);
}

TEST_CASE("SNC5 link actions go to the module", "[fileshare][sharenotice]")
{
    CenterFixture f;
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_upload_file, 1));
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, kOwnerGone));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_upload, 0));
    //编号：分享者的弹窗 1（上传）、2（删除）；访问者的弹窗 1（下载失败）、2（下载成功）、3（上传成功）

    //在资源管理器里显示；那一项已不在原处时改为打开「文件共享」窗口选中那个共享
    f.m_center.OnNoticeLink(share_notice_popup_owner, 1, share_notice_reveal);
    CHECK(f.m_actions.m_revealed == std::vector<std::wstring>(1, L"E:\\root\\docs\\a.txt"));
    CHECK(f.m_actions.m_centerOpens.empty());
    f.m_actions.m_revealOk = false;
    f.m_center.OnNoticeLink(share_notice_popup_owner, 1, share_notice_reveal);
    CHECK(f.m_actions.m_centerOpens == std::vector<int64_t>(1, kShareA));
    f.m_center.OnNoticeLink(share_notice_popup_owner, 2, share_notice_view_share);
    CHECK(f.m_actions.m_centerOpens.size() == 2);
    f.m_center.OnNoticeViewAll(share_notice_popup_owner);
    REQUIRE(f.m_actions.m_centerOpens.size() == 3);
    CHECK(f.m_actions.m_centerOpens[2] == 0);

    //打开、打开所在文件夹（找不到落地的那一项时打开目标目录）、查看上传到的目录
    f.m_actions.m_revealOk = true;
    f.m_center.OnNoticeLink(share_notice_popup_visitor, 2, share_notice_open_file);
    CHECK(f.m_actions.m_opened == std::vector<std::wstring>(1, L"D:\\dl\\a.txt"));
    f.m_center.OnNoticeLink(share_notice_popup_visitor, 2, share_notice_open_folder);
    CHECK(f.m_actions.m_revealed.back() == L"D:\\dl\\a.txt");
    f.m_actions.m_revealOk = false;
    f.m_center.OnNoticeLink(share_notice_popup_visitor, 2, share_notice_open_folder);
    CHECK(f.m_actions.m_opened.back() == L"D:\\dl");
    f.m_center.OnNoticeLink(share_notice_popup_visitor, 3, share_notice_browse);
    REQUIRE(f.m_actions.m_browses.size() == 1);
    CHECK(f.m_actions.m_browses[0].first == kShareA);
    CHECK(f.m_actions.m_browses[0].second == "docs/sub");

    //重试没有发起：这一条留着；发起了：交给模块并删掉这一条
    f.m_actions.m_retryOk = false;
    f.m_center.OnNoticeLink(share_notice_popup_visitor, 1, share_notice_retry);
    CHECK(f.m_center.GetEntryCount(share_notice_popup_visitor) == 3);
    f.m_actions.m_retryOk = true;
    f.m_center.OnNoticeLink(share_notice_popup_visitor, 1, share_notice_retry);
    REQUIRE(f.m_actions.m_retries.size() == 2);
    CHECK(f.m_actions.m_retries[1].m_code == kOwnerGone);
    CHECK(f.m_actions.m_retries[1].m_downloadItems.size() == 1);
    CHECK(f.m_center.GetEntryCount(share_notice_popup_visitor) == 2);
    CHECK(f.m_center.GetPopup(share_notice_popup_visitor).GetRowCount() == 2);
    //已删掉的条目、不认识的编号：什么也不做
    f.m_center.OnNoticeLink(share_notice_popup_visitor, 1, share_notice_retry);
    CHECK(f.m_actions.m_retries.size() == 2);

    //提示音的最小间隔：两条接连到达只响一次
    CenterFixture g;
    g.m_center.SetSoundGapMs(kShareNoticeSoundGapMs);
    g.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    g.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    CHECK(g.m_host.m_sounds == 1);
}

TEST_CASE("SNC6 suppressed, reset and closed popups", "[fileshare][sharenotice]")
{
    CenterFixture f;
    //压下（退出登录期间）：不弹
    f.m_center.SetSuppressed(true);
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    CHECK(f.m_center.GetEntryCount(share_notice_popup_owner) == 0);
    CHECK(f.m_center.GetEntryCount(share_notice_popup_visitor) == 0);

    //恢复后弹；Reset 关闭弹窗、清空条目
    f.m_center.SetSuppressed(false);
    f.m_center.OnOwnerActivity(OwnerActivity(share_audit_delete, 1));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    CHECK(f.m_center.GetPopup(share_notice_popup_owner).IsShowing());
    f.m_center.Reset();
    CHECK_FALSE(f.m_center.GetPopup(share_notice_popup_owner).IsWindow());
    CHECK_FALSE(f.m_center.GetPopup(share_notice_popup_visitor).IsWindow());
    CHECK(f.m_center.GetEntryCount(share_notice_popup_owner) == 0);

    //用户关掉访问者的弹窗：它的条目丢弃，下一条到达时只显示新的
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_download, 0));
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_upload, 0));
    ::SendMessage(f.m_center.GetPopup(share_notice_popup_visitor).m_hWnd, WM_CLOSE, 0, 0);
    CHECK(f.m_center.GetEntryCount(share_notice_popup_visitor) == 0);
    f.m_center.OnVisitorTransferFinished(VisitorResult(share_transfer_upload, kOwnerGone));
    CHECK(f.m_center.GetPopup(share_notice_popup_visitor).GetRowCount() == 1);
}

namespace {

//分享者写操作用例的夹具：业务总控有一个通知窗口（事件经它排队再由 DrainEvents 取出），已登录，并发起了一个文件夹共享
struct OwnerFixture
{
    noticetest::ActivityRecorder m_activity;   //分享者操作的监听者
    noticetest::FinishRecorder   m_finish;     //结束监听（本组不用）
    noticetest::NoticeTestHost   m_host;       //宿主桩
    HWND                         m_notifyWnd;  //通知窗口（纯消息窗口，收到的通知不处理，用例自己调 DrainEvents）
    CShareService                m_service;    //被测的业务总控
    std::wstring                 m_root;       //共享根

    OwnerFixture()
        : m_notifyWnd(NULL)
    {
        const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSnTestDir;
        sharetestfs::DeleteTree(base);
        sharetestfs::MakeDirs(base);
        m_host.m_personalDir = base + L"\\";
        m_root = base + L"\\root";
        sharetestfs::MakeDirs(m_root);
        m_notifyWnd = ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
        m_service.Attach(&m_host, m_notifyWnd, WM_APP + 1);
        m_service.OnLoggedIn();
        m_service.SetNoticeListeners(&m_finish, &m_activity);
    }

    ~OwnerFixture()
    {
        m_service.SetNoticeListeners(NULL, NULL);
        m_service.Detach();
        if (m_notifyWnd != NULL)
            ::DestroyWindow(m_notifyWnd);
    }

    //发起一个文件夹共享（名称 docs-share）并让它成功，返回 shareid
    int64_t CreateShare()
    {
        ShareCreateParams p;
        p.m_ctype = conversation_group;
        p.m_cid = 900200;
        p.m_kind = share_kind_folder;
        p.m_name = "docs-share";
        p.m_displayPath = "x";
        ShareMemberPerm m;
        m.m_userId = kVisitorA;
        m.m_perms = share_perm_rename;
        p.m_members.push_back(m);
        REQUIRE(m_service.CreateShare(p, m_root, NULL) != 0);
        std::vector<ShareLocalRecord> recs;
        CShareLocalStore store;
        REQUIRE(store.Open((m_host.m_personalDir + L"ChatLog\\fileshare.db").c_str()));
        REQUIRE(store.ListShares(recs));
        REQUIRE(recs.size() == 1);
        const std::string body = "{\"code\":0,\"msg\":\"x\",\"clientshareid\":\"" + recs[0].m_clientShareId
            + "\",\"shareid\":\"" + std::to_string(kShareB) + "\"}";
        m_service.HandleServerFrame(cmd_share_create, m_host.LastSeq(cmd_share_create), body);
        return kShareB;
    }

    //模拟提供会话执行完一次写操作
    void WriteDone(const std::vector<ShareAuditEntry>& audits)
    {
        ShareSessionEvent ev;
        ev.m_type = share_provider_event_write_done;
        ev.m_accessId = "acc";
        ev.m_audits = audits;
        m_service.PostSessionEvent(ev);
        m_service.DrainEvents();
    }
};

//一条属于共享 B 的审计记录
ShareAuditEntry AuditB(int action, const char* path, const char* toPath, int result)
{
    ShareAuditEntry e = Audit(action, path, toPath, result);
    e.m_shareId = kShareB;
    return e;
}

} // namespace

TEST_CASE("SOA1 a write op on my share produces one owner activity", "[fileshare][sharenotice]")
{
    OwnerFixture f;
    f.CreateShare();
    f.WriteDone(std::vector<ShareAuditEntry>(1, AuditB(share_audit_rename, "sub/a.txt", "b.txt", 0)));
    REQUIRE(f.m_activity.m_activities.size() == 1);
    const ShareOwnerActivity& a = f.m_activity.m_activities[0];
    CHECK(a.m_shareId == kShareB);
    CHECK(a.m_visitorId == kVisitorA);
    CHECK(a.m_action == share_audit_rename);
    CHECK(a.m_firstName == "a.txt");
    CHECK(a.m_targetName == "b.txt");
    CHECK(a.m_shareName == "docs-share");
    CHECK(a.m_revealPath == f.m_root + L"\\sub\\b.txt");
    //审计照常上报
    CHECK(f.m_host.Count(cmd_share_audit_report) == 1);
}

TEST_CASE("SOA2 mkdir and failed write ops do not notify, partial deletes count the done ones", "[fileshare][sharenotice]")
{
    OwnerFixture f;
    f.CreateShare();
    f.WriteDone(std::vector<ShareAuditEntry>(1, AuditB(share_audit_mkdir, "new", "", 0)));
    f.WriteDone(std::vector<ShareAuditEntry>(1, AuditB(share_audit_delete, "a.txt", "", error_code_share_recycle_unavailable)));
    CHECK(f.m_activity.m_activities.empty());
    std::vector<ShareAuditEntry> del;
    del.push_back(AuditB(share_audit_delete, "a.txt", "", 0));
    del.push_back(AuditB(share_audit_delete, "b.txt", "", error_code_share_io_fail));
    del.push_back(AuditB(share_audit_delete, "c.txt", "", 0));
    f.WriteDone(del);
    REQUIRE(f.m_activity.m_activities.size() == 1);
    CHECK(f.m_activity.m_activities[0].m_action == share_audit_delete);
    CHECK(f.m_activity.m_activities[0].m_count == 2);
    CHECK(f.m_activity.m_activities[0].m_revealPath.empty());
}
