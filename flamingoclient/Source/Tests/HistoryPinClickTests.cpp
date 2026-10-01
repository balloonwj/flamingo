/**
 *  HistoryPinClickTests.cpp
 *
 *  「消息记录」窗里时间右侧那枚图钉「点击即取消 Pin」的测试（标签 [historypinclick]）。
 *
 *  已 Pin 的行在时间右侧画一枚图钉（HistoryPinMark）。点击它与行内「取消」按钮效果
 *  相同，区别在于图钉只可能出现在已 Pin 的行上，所以它的目标状态恒为「取消」。
 *  图钉以自己的 ctrlId（kIdHistoryPinMarkBase + 行下标）发 DUIN_CLICK，窗口用
 *  ResolveHistoryPinClick 把 id 解析成「第几行、改成什么状态」，再发 cmd_pin_message。
 *
 *  覆盖：
 *    L 组 —— 布局契约：已 Pin 的行带图钉且 id 正确，未 Pin 的行没有图钉，id 段不重叠
 *    I 组 —— 图钉控件的交互：单击发通知、双击不外发、手型光标、悬停提示登记
 *    R 组 —— id 解析函数：图钉恒为取消，行内按钮按当前状态取反，其它 id 不解析
 *
 *  I 组要接收 DUIN_CLICK 通知：DuiHost 把 WM_DUI_NOTIFY 同步发给它的父窗口，故建一个
 *  不显示的记录窗口作父窗口，把 DuiHost 建成它的子窗口。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <utility>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"                      // I 组：发通知的宿主
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"                    // WM_DUI_NOTIFY / DuiNotify
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"  // I 组：查悬停提示登记
#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../UI/BalloonHistory/HistoryRow.h"

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

// 测试用列表宽度与排版高度（像素）。宽度取中等窗口的典型值，高度足够排下几行。
const int kTestListWidth    = 640;
const int kTestLayoutHeight = 900;

// 测试宿主窗口的尺寸（像素）。窗口不显示，只用来收发通知。
const int kHostWidth  = 640;
const int kHostHeight = 900;

// 悬停提示文字（与 HistoryRow.cpp 里登记的文字一致）。
const wchar_t* const kUnpinTip = L"取消 Pin";

// 测试行的 msgid 起点（任意正数，逐行加一）。
const long long kFirstMsgid = 770001;

// 造一行消息视图。
//   index： 行下标，决定 msgid 与时间文字。
//   pinned：是否已 Pin。
HistoryRowView MakeRow(int index, bool pinned)
{
    HistoryRowView r;
    r.m_senderName  = _T("林知夏");
    r.m_timeText    = _T("09-30 14:20");
    r.m_previewText = _T("这周五之前把设计稿定下来");
    r.m_fullText    = r.m_previewText;
    r.m_msgid       = kFirstMsgid + index;
    r.m_pinned      = pinned;
    return r;
}

// 按 id 在子树里找控件（深度优先）；找不到返回 nullptr。
DuiControl* FindById(DuiControl* node, UINT id)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->GetCtrlId() == id)
    {
        return node;
    }
    const std::vector<std::unique_ptr<DuiControl> >& kids = node->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        DuiControl* hit = FindById(kids[i].get(), id);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

// 统计子树里某类控件的个数。
template <typename T>
int CountInSubtree(const DuiControl* node)
{
    if (node == nullptr)
    {
        return 0;
    }
    int n = (dynamic_cast<const T*>(node) != nullptr) ? 1 : 0;
    const std::vector<std::unique_ptr<DuiControl> >& kids = node->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        n += CountInSubtree<T>(kids[i].get());
    }
    return n;
}

// 构建一页列表并排版。
//   rows：当前页各行。
// @return 列表内容控件，所有权归调用方。
std::unique_ptr<DuiControl> BuildAndLayout(const std::vector<HistoryRowView>& rows)
{
    std::unique_ptr<DuiControl> content = BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kTestListWidth, kTestLayoutHeight });
    return content;
}

// 取矩形中心点。
POINT CenterOf(const RECT& rc)
{
    POINT pt;
    pt.x = (rc.left + rc.right) / 2;
    pt.y = (rc.top + rc.bottom) / 2;
    return pt;
}

// 记录 WM_DUI_NOTIFY 的父窗口。DuiHost 的通知同步发到这里，按到达顺序记下
// （通知码, ctrlId）。
class NotifyRecorderWnd : public CWindowImpl<NotifyRecorderWnd>
{
public:
    DECLARE_WND_CLASS(_T("HistoryPinClickNotifyRecorder"))

    BEGIN_MSG_MAP(NotifyRecorderWnd)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
    END_MSG_MAP()

    // 记下一条通知。
    LRESULT OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
        const DuiNotify* n = reinterpret_cast<const DuiNotify*>(lParam);
        if (n != nullptr)
        {
            m_notes.push_back(std::make_pair(n->code, n->ctrlId));
        }
        return 0;
    }

    std::vector<std::pair<UINT, UINT> > m_notes;   // 收到的（通知码, ctrlId），按到达顺序
};

// 建一个不显示的父窗口 + 作为其子窗口的 DuiHost，析构时依次销毁。
class ScopedRecordingHost
{
public:
    // 创建两个窗口（都不带 WS_VISIBLE，跑测试时屏幕上不会闪出窗口）。
    ScopedRecordingHost()
    {
        RECT rc = { 0, 0, kHostWidth, kHostHeight };
        m_recorder.Create(NULL, rc, _T("HistoryPinClickTests"), WS_POPUP);
        m_host.Create(m_recorder.m_hWnd, rc, NULL, WS_CHILD);
    }

    // 先销毁子窗口再销毁父窗口。
    ~ScopedRecordingHost()
    {
        if (m_host.IsWindow())
        {
            m_host.DestroyWindow();
        }
        if (m_recorder.IsWindow())
        {
            m_recorder.DestroyWindow();
        }
    }

    // 两个窗口是否都建成功。
    bool Ok()
    {
        return m_recorder.IsWindow() && m_host.IsWindow();
    }

    // 返回宿主（由本类持有，调用方借用）。
    DuiHost& Host()
    {
        return m_host;
    }

    // 返回收到的通知记录（由本类持有，调用方借用）。
    std::vector<std::pair<UINT, UINT> >& Notes()
    {
        return m_recorder.m_notes;
    }

private:
    NotifyRecorderWnd m_recorder;   // 父窗口，负责记录通知
    DuiHost           m_host;       // 宿主，是 m_recorder 的子窗口
};

// 数出某个（通知码, ctrlId）组合出现的次数。
int CountNotes(const std::vector<std::pair<UINT, UINT> >& notes, UINT code, UINT id)
{
    int n = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (notes[i].first == code && notes[i].second == id)
        {
            ++n;
        }
    }
    return n;
}

// 数出某个通知码出现的次数（不论 ctrlId）。
int CountCode(const std::vector<std::pair<UINT, UINT> >& notes, UINT code)
{
    int n = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (notes[i].first == code)
        {
            ++n;
        }
    }
    return n;
}

} // anonymous namespace

// =====================================================================
// L 组：布局契约
// =====================================================================

TEST_CASE("HPC1 已 Pin 的行，时间右侧的图钉 id 为基址加行下标", "[historypinclick]")
{
    //第 0 行与第 3 行已 Pin，其余未 Pin：验证 id 按行下标算，而不是按"第几枚图钉"算
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeRow(0, true));
    rows.push_back(MakeRow(1, false));
    rows.push_back(MakeRow(2, false));
    rows.push_back(MakeRow(3, true));
    std::unique_ptr<DuiControl> content = BuildAndLayout(rows);

    const int pinnedRows[] = { 0, 3 };
    for (size_t i = 0; i < sizeof(pinnedRows) / sizeof(pinnedRows[0]); ++i)
    {
        const int row = pinnedRows[i];
        INFO("row=" << row);
        DuiControl* mark = FindById(content.get(), (UINT)(kIdHistoryPinMarkBase + row));
        REQUIRE(mark != nullptr);
        CHECK(dynamic_cast<HistoryPinMark*>(mark) != nullptr);
        //图钉必须落在自己那一行里
        CHECK(HistoryRowIndexOfControl(mark) == row);
    }
}

TEST_CASE("HPC2 未 Pin 的行里没有图钉", "[historypinclick]")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeRow(0, false));
    rows.push_back(MakeRow(1, false));
    std::unique_ptr<DuiControl> content = BuildAndLayout(rows);

    CHECK(CountInSubtree<HistoryPinMark>(content.get()) == 0);
    CHECK(FindById(content.get(), (UINT)(kIdHistoryPinMarkBase + 0)) == nullptr);
    CHECK(FindById(content.get(), (UINT)(kIdHistoryPinMarkBase + 1)) == nullptr);
}

TEST_CASE("HPC3 图钉的 id 段与其它行内控件的 id 段互不重叠", "[historypinclick]")
{
    //各段的 [基址, 基址 + 容量)
    struct Segment
    {
        const char* m_name;
        UINT        m_base;
        UINT        m_capacity;
    };
    const Segment segs[] =
    {
        { "PageNum", (UINT)kIdHistoryPageNumBase, kIdHistoryPageNumCapacity },
        { "Locate",  (UINT)kIdHistoryLocateBase,  kIdHistoryLocateCapacity  },
        { "Pin",     (UINT)kIdHistoryPinBase,     kIdHistoryPinCapacity     },
        { "Row",     (UINT)kIdHistoryRowBase,     kIdHistoryRowCapacity     },
        { "Expand",  (UINT)kIdHistoryExpandBase,  kIdHistoryExpandCapacity  },
        { "Thumb",   (UINT)kIdHistoryThumbBase,   kIdHistoryThumbCapacity   },
        { "PinMark", (UINT)kIdHistoryPinMarkBase, kIdHistoryPinMarkCapacity },
    };
    const size_t count = sizeof(segs) / sizeof(segs[0]);
    for (size_t i = 0; i < count; ++i)
    {
        for (size_t j = i + 1; j < count; ++j)
        {
            INFO(segs[i].m_name << " vs " << segs[j].m_name);
            const bool disjoint = (segs[i].m_base + segs[i].m_capacity <= segs[j].m_base)
                               || (segs[j].m_base + segs[j].m_capacity <= segs[i].m_base);
            CHECK(disjoint);
        }
    }
    //与其它按行编号的段容量一致，每页条数再调大也够用
    CHECK(kIdHistoryPinMarkCapacity == kIdHistoryPinCapacity);
}

// =====================================================================
// I 组：图钉控件的交互
// =====================================================================

TEST_CASE("HPC4 在图钉上松开发出带图钉 id 的点击通知，松开点在图钉外则不发", "[historypinclick]")
{
    ScopedRecordingHost env;
    REQUIRE(env.Ok());
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeRow(0, false));
    rows.push_back(MakeRow(1, true));
    std::unique_ptr<DuiControl> content = BuildAndLayout(rows);
    content->AttachToHost(&env.Host());

    const UINT markId = (UINT)(kIdHistoryPinMarkBase + 1);
    DuiControl* mark = FindById(content.get(), markId);
    REQUIRE(mark != nullptr);
    const RECT rc = mark->GetRect();
    REQUIRE(rc.right > rc.left);

    SECTION("松开点在图钉上")
    {
        const POINT pt = CenterOf(rc);
        mark->OnLButtonDown(pt, MK_LBUTTON);
        CHECK(mark->OnLButtonUp(pt, 0));
        CHECK(CountNotes(env.Notes(), DUIN_CLICK, markId) == 1);
    }

    SECTION("按下后移出图钉再松开")
    {
        const POINT down = CenterOf(rc);
        POINT up = down;
        up.x = rc.right + (rc.right - rc.left);   //右移一个图钉宽度，落在图钉外
        mark->OnLButtonDown(down, MK_LBUTTON);
        mark->OnLButtonUp(up, 0);
        CHECK(CountCode(env.Notes(), DUIN_CLICK) == 0);
    }
}

TEST_CASE("HPC5 双击图钉不向外发双击通知", "[historypinclick]")
{
    //双击的第一下已经作为单击发出了取消请求；第二下若再冒 DUIN_DBLCLK，窗口会把它
    //当成双击整行去定位，取消的同时窗口跳走
    ScopedRecordingHost env;
    REQUIRE(env.Ok());
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeRow(0, true));
    std::unique_ptr<DuiControl> content = BuildAndLayout(rows);
    content->AttachToHost(&env.Host());

    DuiControl* mark = FindById(content.get(), (UINT)(kIdHistoryPinMarkBase + 0));
    REQUIRE(mark != nullptr);

    mark->OnLButtonDblClk(CenterOf(mark->GetRect()), MK_LBUTTON);

    CHECK(CountCode(env.Notes(), DUIN_DBLCLK) == 0);
}

TEST_CASE("HPC6 光标在图钉上显示为手型", "[historypinclick]")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeRow(0, true));
    std::unique_ptr<DuiControl> content = BuildAndLayout(rows);

    DuiControl* mark = FindById(content.get(), (UINT)(kIdHistoryPinMarkBase + 0));
    REQUIRE(mark != nullptr);

    CHECK(mark->OnSetCursor(CenterOf(mark->GetRect())));
}

TEST_CASE("HPC7 图钉登记了「取消 Pin」悬停提示，销毁后登记随之撤销", "[historypinclick]")
{
    DuiToolTipMgr& tips = DuiToolTipMgr::Inst();
    //只比较指针值，不解引用：控件销毁后仍可安全地拿它查登记表
    const DuiControl* rawMark = nullptr;
    {
        std::vector<HistoryRowView> rows;
        rows.push_back(MakeRow(0, true));
        std::unique_ptr<DuiControl> content = BuildAndLayout(rows);

        DuiControl* mark = FindById(content.get(), (UINT)(kIdHistoryPinMarkBase + 0));
        REQUIRE(mark != nullptr);
        rawMark = mark;
        CHECK(tips.GetText(mark) == CString(kUnpinTip));
    }
    CHECK(tips.GetText(const_cast<DuiControl*>(rawMark)).IsEmpty());
}

// =====================================================================
// R 组：id 解析
// =====================================================================

TEST_CASE("HPC8 行内 Pin 相关点击的 id 解析", "[historypinclick]")
{
    //当前页三行：第 0 行已 Pin、第 1 行未 Pin、第 2 行已 Pin
    std::vector<bool> pagePinned;
    pagePinned.push_back(true);
    pagePinned.push_back(false);
    pagePinned.push_back(true);
    int  row       = -1;
    bool targetPin = true;

    SECTION("图钉：恒为取消 Pin")
    {
        CHECK(ResolveHistoryPinClick((UINT)(kIdHistoryPinMarkBase + 2), pagePinned, row, targetPin));
        CHECK(row == 2);
        CHECK_FALSE(targetPin);
    }

    SECTION("行内按钮：已 Pin 的行解析为取消")
    {
        CHECK(ResolveHistoryPinClick((UINT)(kIdHistoryPinBase + 0), pagePinned, row, targetPin));
        CHECK(row == 0);
        CHECK_FALSE(targetPin);
    }

    SECTION("行内按钮：未 Pin 的行解析为钉住")
    {
        CHECK(ResolveHistoryPinClick((UINT)(kIdHistoryPinBase + 1), pagePinned, row, targetPin));
        CHECK(row == 1);
        CHECK(targetPin);
    }

    SECTION("其它 id 与越界的行都不解析")
    {
        CHECK_FALSE(ResolveHistoryPinClick((UINT)(kIdHistoryLocateBase + 0), pagePinned, row, targetPin));
        CHECK_FALSE(ResolveHistoryPinClick((UINT)(kIdHistoryRowBase + 0), pagePinned, row, targetPin));
        CHECK_FALSE(ResolveHistoryPinClick(0, pagePinned, row, targetPin));
        //行下标超出当前页（翻页后旧通知迟到等情形）
        CHECK_FALSE(ResolveHistoryPinClick((UINT)(kIdHistoryPinMarkBase + 3), pagePinned, row, targetPin));
        CHECK_FALSE(ResolveHistoryPinClick((UINT)(kIdHistoryPinBase + 3), pagePinned, row, targetPin));
    }
}
