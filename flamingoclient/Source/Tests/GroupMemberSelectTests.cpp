/**
 *  GroupMemberSelectTests.cpp
 *
 *  群聊窗成员列表（GroupMemberList）平铺视图的选中（标签 [memberselect]，2026-10-08 用户确认的方案）：
 *    样式：浅橙底 + 左侧橙色竖条；只有平铺视图有选中，部门树视图保持原样。
 *    交互：单击一行选中（单选），右键一行时先选中再弹菜单，点列表空白处不取消；列表重建后按 userid 保留，
 *          选中的人不在新名单里时取消；搜索过滤只隐藏、不取消；切到部门树视图不显示选中，切回平铺后恢复。
 *
 *    MSEL1 单击选中、选中随点击移动、始终只有一行选中、点空白处不取消
 *    MSEL2 右键一行时先选中它
 *    MSEL3 重建后按 userid 保留；选中的人不在新名单里时取消；过滤隐藏后清空关键字仍是选中
 *    MSEL4 部门树视图：点成员行不产生选中、不画选中底；切回平铺后恢复之前选中的人
 *    MSEL5 离屏绘制：选中行画出浅橙底与左侧橙色竖条；鼠标停在选中行上时仍是选中样式
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <cstdlib>    // abs：MS5 比较颜色分量
#include <memory>
#include <vector>
#include <gdiplus.h>  // MS5：成员行用 GDI+ 绘制，测试期间要启动 GDI+

#include "../UI/BalloonChatWnd/GroupMemberList.h"
#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/GroupDeptHeaderItem.h"

using namespace balloonchatwnd;

namespace {

// 测试里几位成员的 userid（任取，互不相同）
const int64_t kOwnerId = 5308;
const int64_t kMoMeiId = 5010;
const int64_t kJiangId = 5011;
const int64_t kQinId   = 5012;

// 列表与成员行的离屏尺寸（像素）：宽取群聊窗侧栏的常见宽度，高够放下全部测试行
const int kListW = 230;
const int kListH = 400;
const int kItemW = 230;

// 选中样式的颜色（与 GroupMemberItem.cpp 的常量一致）：浅橙底、左侧橙色竖条
const COLORREF kSelectFill = RGB(0xFF, 0xF1, 0xE8);
const COLORREF kSelectBar  = RGB(0xFF, 0x7A, 0x45);
// 判定颜色相同时每个分量允许的误差（抗锯齿与 GDI+ 取整）
const int kColorTolerance = 6;

// 32 位 DIB 一个像素的字节数与各分量偏移（BGRA）
const int kBytesPerPixel = 4;
const int kOffsetBlue    = 0;
const int kOffsetGreen   = 1;
const int kOffsetRed     = 2;

// 在测试期间启动 GDI+，析构时关闭（成员行用 GDI+ 画底色与头像）
class MsScopedGdiplus
{
public:
    MsScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~MsScopedGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    // GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时交回
    bool      m_ok;      // 是否启动成功
};

// 两个颜色的各分量是否都在误差之内
bool MsColorNear(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= kColorTolerance
        && abs((int)GetGValue(a) - (int)GetGValue(b)) <= kColorTolerance
        && abs((int)GetBValue(a) - (int)GetBValue(b)) <= kColorTolerance;
}

// 造一行成员（灰色字形头像、普通成员）
std::unique_ptr<GroupMemberItem> MakeMemberRow(int64_t userid, LPCTSTR name)
{
    std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
    item->SetMember(name, CString(name).Left(1), RGB(0x80, 0x80, 0x80), 0);
    item->SetUserId(userid);
    return item;
}

// 按平铺视图重建列表：与 FillGroupMemberList 的调用顺序相同（清空、设视图、逐行挂上、提交、过滤、排版）
void FillFlat(GroupMemberList& list, const std::vector<int64_t>& ids, const std::vector<CString>& names)
{
    list.ClearRows();
    list.SetTreeMode(false);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        list.AddRow(std::unique_ptr<balloonwjui::DuiControl>(MakeMemberRow(ids[i], names[i]).release()), -1);
    }
    list.CommitRows();
    list.ReapplyFilter();
    list.ForceLayout(RECT{ 0, 0, kListW, kListH });
}

// 平铺视图的四个人：群主、莫梅、江怡、秦明芳
void FillFourMembers(GroupMemberList& list)
{
    std::vector<int64_t> ids;
    ids.push_back(kOwnerId);
    ids.push_back(kMoMeiId);
    ids.push_back(kJiangId);
    ids.push_back(kQinId);
    std::vector<CString> names;
    names.push_back(L"韩晓燕888");
    names.push_back(L"莫梅");
    names.push_back(L"江怡");
    names.push_back(L"秦明芳");
    FillFlat(list, ids, names);
}

// 按 userid 找成员行（同一个人只取第一行）；找不到返回 NULL
GroupMemberItem* RowOf(GroupMemberList& list, int64_t userid)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(kids[i].get());
        if (item != NULL && item->UserId() == userid)
            return item;
    }
    return NULL;
}

// 显示选中样式的成员行数
int SelectedRowCount(GroupMemberList& list)
{
    int n = 0;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(kids[i].get());
        if (item != NULL && item->IsSelected())
            ++n;
    }
    return n;
}

// 行矩形的中心点
POINT CenterOf(const balloonwjui::DuiControl* ctrl)
{
    const RECT rc = ctrl->GetRect();
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

// 在某一行上按下左键
void ClickRow(GroupMemberItem* row)
{
    REQUIRE(row != NULL);
    row->OnLButtonDown(CenterOf(row), MK_LBUTTON);
    row->OnLButtonUp(CenterOf(row), 0);
}

// 把控件画到白底的 32 位离屏位图上，逐像素取颜色；画布建不出来时返回 false
bool PaintOnWhite(balloonwjui::DuiControl& ctrl, int w, int h, std::vector<COLORREF>& outPixels)
{
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
        return false;
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高表示自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    RECT all = { 0, 0, w, h };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);
    ctrl.SetRect(all);
    ctrl.OnPaint(memDC, all);
    ::GdiFlush();
    outPixels.resize((size_t)w * h);
    const BYTE* p = static_cast<const BYTE*>(bits);
    for (int i = 0; i < w * h; ++i)
    {
        outPixels[i] = RGB(p[i * kBytesPerPixel + kOffsetRed], p[i * kBytesPerPixel + kOffsetGreen],
                           p[i * kBytesPerPixel + kOffsetBlue]);
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return true;
}

} // anonymous namespace

TEST_CASE("MSEL1 单击一行选中，选中随点击移动，始终只有一行选中，点空白处不取消", "[memberselect]")
{
    GroupMemberList list;
    FillFourMembers(list);
    CHECK(list.SelectedUserId() == 0);
    CHECK(SelectedRowCount(list) == 0);

    ClickRow(RowOf(list, kMoMeiId));
    CHECK(list.SelectedUserId() == kMoMeiId);
    CHECK(RowOf(list, kMoMeiId)->IsSelected());
    CHECK(SelectedRowCount(list) == 1);

    //点另一行：选中移过去
    ClickRow(RowOf(list, kJiangId));
    CHECK(list.SelectedUserId() == kJiangId);
    CHECK_FALSE(RowOf(list, kMoMeiId)->IsSelected());
    CHECK(RowOf(list, kJiangId)->IsSelected());
    CHECK(SelectedRowCount(list) == 1);

    //再点同一行：保持选中
    ClickRow(RowOf(list, kJiangId));
    CHECK(list.SelectedUserId() == kJiangId);

    //点列表下方的空白处（成员行以下、不在滚动条命中带上）：不取消
    const POINT blank = { kListW / 4, kListH - 10 };
    list.OnLButtonDown(blank, MK_LBUTTON);
    list.OnLButtonUp(blank, 0);
    CHECK(list.SelectedUserId() == kJiangId);
    CHECK(SelectedRowCount(list) == 1);
}

TEST_CASE("MSEL2 右键一行时先选中它", "[memberselect]")
{
    GroupMemberList list;
    FillFourMembers(list);
    ClickRow(RowOf(list, kMoMeiId));

    //没有宿主窗口，菜单弹不出来；选中要在弹菜单之前发生
    GroupMemberItem* qin = RowOf(list, kQinId);
    REQUIRE(qin != NULL);
    qin->OnRButtonDown(CenterOf(qin), MK_RBUTTON);
    CHECK(list.SelectedUserId() == kQinId);
    CHECK(qin->IsSelected());
    CHECK(SelectedRowCount(list) == 1);
}

TEST_CASE("MSEL3 重建后按 userid 保留选中；选中的人不在新名单里时取消；过滤隐藏后清空关键字仍是选中", "[memberselect]")
{
    GroupMemberList list;
    FillFourMembers(list);
    ClickRow(RowOf(list, kMoMeiId));

    //同一批人重建（成员到达、有人上下线时都会重建）：按 userid 保留
    FillFourMembers(list);
    CHECK(list.SelectedUserId() == kMoMeiId);
    REQUIRE(RowOf(list, kMoMeiId) != NULL);
    CHECK(RowOf(list, kMoMeiId)->IsSelected());
    CHECK(SelectedRowCount(list) == 1);

    //搜索过滤把选中的人隐藏：不取消；清空关键字后仍是选中
    list.SetFilterKeyword(L"江");
    CHECK_FALSE(RowOf(list, kMoMeiId)->IsVisible());
    CHECK(list.SelectedUserId() == kMoMeiId);
    list.SetFilterKeyword(CString());
    CHECK(RowOf(list, kMoMeiId)->IsVisible());
    CHECK(RowOf(list, kMoMeiId)->IsSelected());

    //选中的人离开了群（新名单里没有他）：取消
    std::vector<int64_t> ids;
    ids.push_back(kOwnerId);
    ids.push_back(kJiangId);
    std::vector<CString> names;
    names.push_back(L"韩晓燕888");
    names.push_back(L"江怡");
    FillFlat(list, ids, names);
    CHECK(list.SelectedUserId() == 0);
    CHECK(SelectedRowCount(list) == 0);

    //他之后又入群：不会自动恢复选中
    FillFourMembers(list);
    CHECK(list.SelectedUserId() == 0);
    CHECK(SelectedRowCount(list) == 0);
}

TEST_CASE("MSEL4 部门树视图：点成员行不产生选中、不画选中底；切回平铺后恢复之前选中的人", "[memberselect]")
{
    GroupMemberList list;
    FillFourMembers(list);
    ClickRow(RowOf(list, kMoMeiId));

    //切到部门树视图重建：一个部门标题行，莫梅与江怡挂在它下面
    const int64_t kDeptId = 9001;
    list.ClearRows();
    list.SetTreeMode(true);
    std::unique_ptr<GroupDeptHeaderItem> head(new GroupDeptHeaderItem());
    head->SetDept(kDeptId, L"研发部", 2, 0);
    list.AddRow(std::unique_ptr<balloonwjui::DuiControl>(head.release()), -1);
    list.AddRow(std::unique_ptr<balloonwjui::DuiControl>(MakeMemberRow(kMoMeiId, L"莫梅").release()), 0);
    list.AddRow(std::unique_ptr<balloonwjui::DuiControl>(MakeMemberRow(kJiangId, L"江怡").release()), 0);
    list.CommitRows();
    list.ReapplyFilter();
    list.ForceLayout(RECT{ 0, 0, kListW, kListH });

    //部门树视图不显示选中
    CHECK(SelectedRowCount(list) == 0);
    //点成员行：不产生选中，也不改掉平铺视图里记着的人
    ClickRow(RowOf(list, kJiangId));
    CHECK(SelectedRowCount(list) == 0);
    CHECK(list.SelectedUserId() == kMoMeiId);

    //切回平铺：恢复之前选中的人
    FillFourMembers(list);
    REQUIRE(RowOf(list, kMoMeiId) != NULL);
    CHECK(RowOf(list, kMoMeiId)->IsSelected());
    CHECK(SelectedRowCount(list) == 1);
}

TEST_CASE("MSEL5 离屏绘制：选中行画出浅橙底与左侧橙色竖条，鼠标停在选中行上时仍是选中样式", "[memberselect]")
{
    MsScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const int h = kGroupMemberRowHeight;
    //取样点：行右侧中部（避开头像、名字与角色徽标）、左侧竖条中部
    const int fillX = kItemW - 40;
    const int midY = h / 2;
    const int barX = 4;

    GroupMemberItem item;
    item.SetMember(L"莫梅", L"莫", RGB(0x80, 0x80, 0x80), 0);
    item.SetUserId(kMoMeiId);

    //未选中：白底
    std::vector<COLORREF> px;
    REQUIRE(PaintOnWhite(item, kItemW, h, px));
    CHECK(MsColorNear(px[(size_t)midY * kItemW + fillX], RGB(0xFF, 0xFF, 0xFF)));

    //选中：浅橙底 + 左侧橙色竖条
    item.SetSelected(true);
    REQUIRE(PaintOnWhite(item, kItemW, h, px));
    CHECK(MsColorNear(px[(size_t)midY * kItemW + fillX], kSelectFill));
    CHECK(MsColorNear(px[(size_t)midY * kItemW + barX], kSelectBar));

    //鼠标停在选中行上：仍是选中样式，不换成悬停的浅灰底
    item.OnMouseEnter();
    REQUIRE(item.IsHover());
    REQUIRE(PaintOnWhite(item, kItemW, h, px));
    CHECK(MsColorNear(px[(size_t)midY * kItemW + fillX], kSelectFill));
    CHECK(MsColorNear(px[(size_t)midY * kItemW + barX], kSelectBar));
}
