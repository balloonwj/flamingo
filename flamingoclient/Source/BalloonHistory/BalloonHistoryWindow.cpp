/**
 *  历史消息记录窗口（DuiFrameWindow 外壳）实现，BalloonHistoryWindow.cpp
 *
 *  组装客户区，接 HistoryModel + 本地库取数渲染列表；交互（类型胶囊 / 时间下拉 /
 *  搜索 / 翻页）经 OnDuiNotify 改模型后延迟局部刷新（只换列表 + 重填分页器 + 更新底部
 *  信息与胶囊态，不动搜索框）。调试入口用 in-memory 预置库灌假消息。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"
#include "BalloonHistoryWindow.h"

#include <atldlgs.h>   // CFileDialog（导出另存对话框）
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM（WM_MOUSEMOVE 取落点）

#include <memory>
#include <vector>
#include <string>

#include "../CustomMsgDef.h"   // WM_CLOSE_MAINDLG
#include "../IULog.h"          // LOG_INFO / LOG_ERROR
#include "../BalloonChatWnd/ChatLinkDetect.h"   // IsSafeLinkUrl（打开链接前的协议白名单）

#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

#include "HistoryView.h"       // PreviewFromContent / FullTextFromContent / TimeRangeFromComboIndex
#include "HistoryRow.h"        // 列表行（悬停态由本窗按落点驱动）
#include "../BalloonChat/BalloonChatMsgFormat.h"   // ParseSendBody（正文拆成文字段 + 表情段）
#include "../BalloonChatWnd/ChatInlineText.h"      // BuildFlatText（关键字要在扁平文本上找）
#include "../net/Protocol.h"   // message_type_file（文件行的正文沿用"名字 · 大小"那份）
#include "../BalloonMain/BalloonMainPanel.h"   // 真实会话下解析发送者昵称
#include "../net/IUSocket.h"        // 行内 Pin 按钮直接发 cmd_pin_message
#include "../net/ImageTransfer.h"
#include "../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）   // CacheFilePathForMd5（算缩略图的本地缓存路径）
#include "../FlamingoClient.h"      // 取当前登录用户 userid（缓存按账号隔离）
#include "../BalloonMain/TitleBarIcon.h"       // CreateProgramIconBitmap（标题栏左侧图标）
#include "../BalloonMain/PopupMenuPlacement.h" // ClampPopupMenuToWorkArea（菜单不越屏）
#include "HistoryRowMenu.h"                    // 行右键菜单的纯模型
#include "../BalloonChatWnd/ChatFileLocation.h"  // ResolveLocalFilePath（本地落点优先级）
#include "../BalloonChatWnd/ChatImageSaveAs.h"   // SaveChatImageAs（图片另存为）
#include "../BalloonChatWnd/ChatMessageMenu.h"   // MakeImageSaveAsDefaultName
#include "../BalloonImageViewer/BalloonImageViewerWindow.h"  // 看图窗（双击图片放大）
#include "FileLocalStore.h"                    // 文件本地落点登记库
#include "FolderLocalStore.h"                  // 文件夹本地落点登记库（按会话 id 记根目录）
#include "../net/FileTransfer.h"               // 文件下载 / 取消
#include "../ClipboardText.h"                  // CopyTextToClipboard（复制正文）
#include "../ClipboardImage.h"                 // CopyImageFileToClipboard（复制图片）
#include <shellapi.h>                          // ShellExecute（打开文件 / 定位文件夹）

using namespace balloonwjui;
using namespace balloonhistory;

// ---- 文件作用域常量 ----

//窗口初始客户区尺寸（逻辑像素），与设计稿比例接近。
static const int kHisWindowWidth  = 880;
static const int kHisWindowHeight = 690;

//窗口最小尺寸（逻辑像素）。
static const int kHisMinWidth  = 640;
static const int kHisMinHeight = 460;

//标题栏高度（逻辑像素）—— 纤细透明，仅承载关闭按钮。
static const int kHisTitleBarHeight = 32;

//标题栏文字 / 按钮 glyph 颜色（深灰，配白色透明标题栏）。
static const COLORREF kHisCaptionColor = RGB(0x55, 0x55, 0x55);

//标题栏左侧图标边长（逻辑像素）。标题栏按 16x16 绘制，取同值免去缩放。
static const int kHisTitleIconSize = 16;

//标题栏实际显示的底色：本窗标题栏透明，穿透显示的是根控件背景（白）。图标须
//合成到这个色上，否则其透明区在 SRCCOPY 搬运后会变成黑块。
static const COLORREF kHisTitleBarBg = RGB(0xFF, 0xFF, 0xFF);

//每页条数。
static const int kHisPageSize = 8;

//行右键菜单尺寸的估算值（像素）。Win32 的 HMENU 在弹出前拿不到精确尺寸，故按行数估算
//交给夹取函数 —— 估大一点只会让靠近屏幕边缘时更早翻向左 / 上，不会误伤（与 ChatBubble
//弹菜单时的做法一致）。菜单最长的一项是「打开所在文件夹」，6 个汉字。
static const int kRowMenuEstWidth      = 160;
static const int kRowMenuEstItemHeight = 26;

namespace {

//取当前 Unix 毫秒时间（供“近N天”换算与种子时间戳）。
int64_t NowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    //FILETIME 为自 1601 起的 100ns 数；减去到 1970 的偏移后换算成毫秒。
    const unsigned long long kEpochDiff100ns = 116444736000000000ULL;
    unsigned long long ms = (u.QuadPart - kEpochDiff100ns) / 10000ULL;
    return (int64_t)ms;
}

//把 Unix 毫秒格式化成“MM-DD HH:MM”（本地时区）。
CString FormatTime(int64_t timestampMs)
{
    __time64_t t = (__time64_t)(timestampMs / 1000);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return CString();
    }
    TCHAR buf[32];
    _stprintf_s(buf, _countof(buf), _T("%02d-%02d %02d:%02d"),
                tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return CString(buf);
}

//列表行内显示的时间：只到分，日期交给上方的日期分组头。
//<u>不要</u>拿它替换 FormatTime —— 导出的 .txt 是脱离界面单独看的，那里必须带完整日期。
CString FormatTimeShort(int64_t timestampMs)
{
    __time64_t t = (__time64_t)(timestampMs / 1000);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return CString();
    }
    TCHAR buf[16];
    _stprintf_s(buf, _countof(buf), _T("%02d:%02d"), tmv.tm_hour, tmv.tm_min);
    return CString(buf);
}

//把消息时间换算成"本地时区下的哪一天"，形如 20260804。供 ComputeCompactFlags 判断
//两条消息是不是同一天 —— 折叠必须按本地日历天算，直接拿毫秒数除以一天会在时区偏移
//不是整天数的地区（如印度 +5:30）切错位置。换不出来时返回 0。
int64_t LocalDayKey(int64_t timestampMs)
{
    __time64_t t = (__time64_t)(timestampMs / 1000);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return 0;
    }
    return (int64_t)(tmv.tm_year + 1900) * 10000
         + (int64_t)(tmv.tm_mon + 1) * 100
         + (int64_t)tmv.tm_mday;
}

//UTF-8 std::string → CString（UNICODE 下即 CStringW）。
CString Utf8ToCString(const std::string& s)
{
    if (s.empty())
    {
        return CString();
    }
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0)
    {
        return CString();
    }
    CString out;
    LPWSTR buf = out.GetBuffer(n);
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), buf, n);
    out.ReleaseBuffer(n);
    return out;
}

//CString（宽）→ UTF-8 std::string（供搜索关键字传给本地库）。
std::string CStringToUtf8(const CString& s)
{
    if (s.IsEmpty())
    {
        return std::string();
    }
    int n = ::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out;
    out.resize((size_t)n);
    ::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), &out[0], n, NULL, NULL);
    return out;
}

//把宽字符文本以 UTF-8（带 BOM）写入文件。BOM 让记事本等正确识别编码。成功返回 true。
bool WriteUtf8FileWithBom(LPCTSTR path, const CString& text)
{
    std::string utf8 = CStringToUtf8(text);
    HANDLE h = ::CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    bool ok = true;
    DWORD written = 0;
    const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };
    if (!::WriteFile(h, kBom, 3, &written, NULL))
    {
        ok = false;
    }
    if (ok && !utf8.empty())
    {
        if (!::WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, NULL))
        {
            ok = false;
        }
    }
    ::CloseHandle(h);
    return ok;
}

//某个路径在磁盘上是否真的存在（且不是目录）。
//本地文件"在不在"要实测磁盘 —— 用户随时会把文件移走、改名、删掉，登记表里有记录
//不代表文件还在。
bool PathExistsOnDisk(LPCWSTR path)
{
    if (path == NULL || path[0] == 0)
    {
        return false;
    }
    const DWORD attr = ::GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

//某个路径在磁盘上是否是一个真的存在的<u>目录</u>。文件夹传输记录行据它决定要不要给
//「打开文件夹」——登记表里有落点不代表目录还在，用户随时会把它改名或删掉。
//与 PathExistsOnDisk 分开两个函数而不加参数，是因为两处的判据恰好相反，合成一个再传
//个布尔量，调用点读起来反而要多想一步。
bool DirectoryExistsOnDisk(LPCWSTR path)
{
    if (path == NULL || path[0] == 0)
    {
        return false;
    }
    const DWORD attr = ::GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

//由发送者 id 取一个稳定的头像底色（小调色板取模）。
COLORREF ColorForId(int64_t id)
{
    static const COLORREF kPalette[] =
    {
        RGB(0xF2, 0x6B, 0x3E), RGB(0xF2, 0x5C, 0x54), RGB(0x2F, 0xA4, 0x7A),
        RGB(0x4F, 0x6C, 0xDF), RGB(0x8A, 0x5C, 0xF2), RGB(0xE0, 0x8A, 0x2E),
    };
    int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    int idx = (int)(((id % n) + n) % n);
    return kPalette[idx];
}

//时间范围 → 下拉项下标（与 BuildHistoryWindowBody 里下拉项顺序一致）。
//两者是一一对应的，见 HistoryTimeRange 的注释；这里只做一次范围检查兜底。
int ComboIndexFromRange(HistoryTimeRange range)
{
    if (range >= kHistoryTimeAll && range <= kHistoryTimeCustom)
    {
        return (int)range;
    }
    return (int)kHistoryTimeAll;
}

//清空容器的全部子控件（先复制指针清单再逐个 RemoveChild，避免遍历中容器变化）。
void RemoveAllChildren(DuiControl* box)
{
    if (box == NULL)
    {
        return;
    }
    std::vector<DuiControl*> kids;
    for (const auto& child : box->Children())
    {
        kids.push_back(child.get());
    }
    for (size_t i = 0; i < kids.size(); ++i)
    {
        box->RemoveChild(kids[i]);
    }
}

//自定义日期“年”下拉的起始年份；须与 HistoryWindowLayout.cpp 的 kCustomYearStart 一致。
const int kCustomDateYearBase = 2018;

//把本地日期 (year, month, day) 换算成 Unix 毫秒。endOfDay=true 取当天 23:59:59.999（区间
//右端含整天），否则取 00:00:00.000；换算失败返回 0。
int64_t DateToUnixMs(int year, int month, int day, bool endOfDay)
{
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = endOfDay ? 23 : 0;
    t.tm_min = endOfDay ? 59 : 0;
    t.tm_sec = endOfDay ? 59 : 0;
    t.tm_isdst = -1;
    __time64_t secs = _mktime64(&t);
    if (secs == (__time64_t)-1)
    {
        return 0;
    }
    return (int64_t)secs * 1000 + (endOfDay ? 999 : 0);
}

//读某下拉的当前选中下标（找不到 / 非下拉返回 0）。
int ReadComboSel(DuiControl* content, UINT ctrlId)
{
    DuiComboBox* combo = (content != NULL)
        ? dynamic_cast<DuiComboBox*>(content->FindCtrlById(ctrlId)) : NULL;
    return (combo != NULL) ? combo->GetCurSel() : 0;
}

//设某下拉的选中下标（不发通知，避免回环）。
void SetComboSel(DuiControl* content, UINT ctrlId, int index)
{
    DuiComboBox* combo = (content != NULL)
        ? dynamic_cast<DuiComboBox*>(content->FindCtrlById(ctrlId)) : NULL;
    if (combo != NULL && index >= 0)
    {
        combo->SetCurSel(index, false);
    }
}

} // anonymous namespace

CBalloonHistoryWindow::CBalloonHistoryWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_pMainPanel(NULL)
    , m_pScroll(NULL)
    , m_pPager(NULL)
    , m_pFooterInfo(NULL)
    , m_pCustomRow(NULL)
    , m_pendingViewMsgid(0)   //没有"下载完自动看图"的待办
    , m_hTitleIcon(NULL)
{
}

CBalloonHistoryWindow::~CBalloonHistoryWindow()
{
    //客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底；m_seedStore 自行析构关库。
    //标题栏图标位图归本类所有（DuiFrameWindow 只借用不释放），这里收回。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

void CBalloonHistoryWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;
    SeedDebugStore();
    if (!BuildWindow(NULL))
    {
        return;
    }
    LOG_INFO(_T("历史消息记录窗口已创建并显示。"));
}

void CBalloonHistoryWindow::OpenForConversation(CBalloonMainPanel* pMainPanel,
                                                CV2MsgStore* pStore,
                                                int ctype, int64_t cid, LPCTSTR titleName,
                                                int initialTypeFilter)
{
    //真实打开：关闭只销毁本窗、不退出程序（与调试入口区别在 m_hMainDlg=NULL）。
    m_hMainDlg = NULL;
    m_pMainPanel = pMainPanel;
    m_titleName = (titleName != NULL) ? titleName : _T("");
    m_model.SetStore(pStore);
    m_model.SetConversation(ctype, cid);
    m_model.SetPageSize(kHisPageSize);

    //预选类型胶囊。取值来自窗口消息的一个字节，可能是任何数，越界就退回"全部"，
    //免得写进模型后查询条件对不上、列表莫名其妙空掉。
    if (initialTypeFilter >= kHistoryTypeAll && initialTypeFilter <= kHistoryTypePinned)
    {
        m_model.SetTypeFilter((HistoryTypeFilter)initialTypeFilter);
    }
    else
    {
        m_model.SetTypeFilter(kHistoryTypeAll);
    }

    if (IsWindow())
    {
        //已开：重定向到新会话，刷新并置顶。
        SetTitle(CString(_T("消息记录 · ")) + m_titleName);
        RefreshContent();
        ShowWindow(SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
        return;
    }
    BuildWindow(NULL);
    LOG_INFO(_T("历史消息记录窗口已为会话打开。"));
}

void CBalloonHistoryWindow::RefreshIfConversation(int ctype, int64_t cid)
{
    if (!IsWindow())
    {
        return;
    }
    const HistoryFilter& filter = m_model.Filter();
    if (filter.m_ctype == ctype && filter.m_cid == cid)
    {
        RefreshContent();
    }
}

void CBalloonHistoryWindow::ExportToTxt()
{
    int64_t now = NowMs();
    std::vector<HistoryRecord> all;
    if (!m_model.QueryAll(now, all) || all.empty())
    {
        ::MessageBox(m_hWnd, _T("当前筛选条件下没有可导出的消息。"),
                     _T("导出消息记录"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    //默认文件名“消息记录_<对话名>.txt”。
    CString defName = CString(_T("消息记录_")) + m_titleName + _T(".txt");
    CFileDialog dlg(FALSE, _T("txt"), defName,
                    OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY,
                    _T("文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0"), m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return;
    }

    //组织文本：时间正序（旧→新）便于从上往下阅读；每行“[时间] 昵称：内容”。
    CString text;
    text += CString(_T("消息记录 · ")) + m_titleName + _T("\r\n");
    text += _T("========================================\r\n");
    for (int i = (int)all.size() - 1; i >= 0; --i)
    {
        const HistoryRecord& rec = all[(size_t)i];
        text += _T("[");
        text += FormatTime(rec.m_timestamp);
        text += _T("] ");
        text += ResolveSenderName(rec.m_fromid);
        text += _T("：");
        text += Utf8ToCString(PreviewFromContent(rec.m_msgtype, rec.m_content));
        text += _T("\r\n");
    }

    if (WriteUtf8FileWithBom(dlg.m_szFileName, text))
    {
        ::MessageBox(m_hWnd, _T("消息记录已导出。"), _T("导出消息记录"),
                     MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        ::MessageBox(m_hWnd, _T("导出失败：无法写入文件。"), _T("导出消息记录"),
                     MB_OK | MB_ICONWARNING);
    }
}

void CBalloonHistoryWindow::SeedDebugStore()
{
    if (!m_seedStore.Open(L":memory:"))
    {
        LOG_ERROR(_T("历史窗调试预置库打开失败。"));
        return;
    }
    m_titleName = _T("苏沐辰");
    m_senderNames[1001] = _T("苏沐辰");
    m_senderNames[9999] = _T("林知夏");

    const int64_t cid = 1001;
    const int64_t now = NowMs();
    const int64_t kSpacingMs = 12LL * 3600LL * 1000LL;   //每条间隔 12 小时，跨度约 13 天
    const int kCount = 27;

    //调试入口的假正文。最后两条是<u>专门用来目检新增渲染分支</u>的，不要删：
    //  · 倒数第二条带 /f["<id>"] 内联表情标记 —— 不留一条的话，调试入口永远走不到
    //    "把表情画成图"那条分支，画坏了也看不出来；
    //  · 最后一条足够长，会超过折叠行数上限，用来核对「展开 / 收起」链接与行高。
    //JSON 字符串里的引号要转义，故表情标记写成 /f[\\"5\\"]。
    const char* kTexts[] =
    {
        "改了发你看下", "你觉得这个排版怎么样？", "左对齐更好，居中显得太弱了",
        "收到，很有意思",
        "好的 /f[\\\"5\\\"] 我这就改，改完发你 /f[\\\"12\\\"]",
        "对了，关于这次品牌升级我整理了几点想法：主色调建议从橙色微调到更暖一些的"
        "珊瑚色，辅助色保留现在的灰绿搭配；字体方面思源黑体确实更适合长段落阅读，"
        "标题也可以试试更重的字重。另外 banner 那块的留白我觉得还可以再松一点，"
        "现在四周挤得有些满，视线没有落脚的地方。图标风格我倾向于统一走线性，"
        "实心的那套在小尺寸下会糊成一团，列表页里尤其明显。插画那部分先不动，"
        "等主色定下来再统一调，不然改两遍。还有一件事，移动端的按钮高度现在是"
        "40，我觉得可以提到 44，拇指点起来会舒服很多，安卓那边也好对齐。"
        "你看周五之前能不能出一版对比稿？我们约个时间一起过一遍。",
        //带网址的一条 —— 用来目检链接识别那条渲染分支（蓝色 + 下划线 + 可点）。
        //网址按原样写：正文格式里的转义只认"紧跟表情标记的双斜杠"，网址不受影响。
        "参考资料见 https://example.com/brand/guide?v=2 还有 www.example.org 都看下",
    };
    //上面那张假正文表的条数。取数组长度而不是写死 —— 往表里加一条却忘了改这里的话，
    //新加的那条永远轮不上，等于白加（本来写死的是 6）。
    const int kTextCount = (int)(sizeof(kTexts) / sizeof(kTexts[0]));

    const char* kFiles[] = { "设计规范-v2.docx", "参考案例.zip", "季度报告.xlsx" };

    int64_t msgid = 1001;
    for (int k = 0; k < kCount; ++k)
    {
        HistoryRecord rec;
        rec.m_msgid = msgid++;
        rec.m_ctype = 1;
        rec.m_cid = cid;
        rec.m_fromid = (k % 2 == 0) ? 1001 : 9999;
        rec.m_timestamp = now - (int64_t)(kCount - 1 - k) * kSpacingMs;
        rec.m_recalled = false;

        int kind = k % 5;
        if (kind == 0)
        {
            std::string text = kTexts[k % kTextCount];
            rec.m_msgtype = 1;
            rec.m_content = std::string("{\"text\":\"") + text + "\"}";
            rec.m_searchText = text;
            rec.m_isLink = false;
        }
        else if (kind == 1)
        {
            std::string text = std::string("看下这个链接 https://example.com/p") + std::to_string(k);
            rec.m_msgtype = 1;
            rec.m_content = std::string("{\"text\":\"") + text + "\"}";
            rec.m_searchText = text;
            rec.m_isLink = true;
        }
        else if (kind == 2)
        {
            rec.m_msgtype = 2;
            rec.m_content = "{\"image\":{\"fileid\":\"img\"}}";
            rec.m_searchText = "";
            rec.m_isLink = false;
        }
        else if (kind == 3)
        {
            std::string name = kFiles[k % 3];
            rec.m_msgtype = 3;
            rec.m_content = std::string("{\"file\":{\"name\":\"") + name + "\"}}";
            rec.m_searchText = name;
            rec.m_isLink = false;
        }
        else
        {
            rec.m_msgtype = 4;
            rec.m_content = "{}";
            rec.m_searchText = "";
            rec.m_isLink = false;
        }
        m_seedStore.Upsert(rec);
    }

    m_model.SetStore(&m_seedStore);
    m_model.SetConversation(1, cid);
    m_model.SetPageSize(kHisPageSize);
}

bool CBalloonHistoryWindow::BuildWindow(HWND hWndParent)
{
    if (Create(hWndParent, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("历史消息记录窗口 Create 失败。"));
        return false;
    }

    //标题栏显示完整标题“消息记录 · <对话名>”；客户区不再重复标题（仅留“共 N 条消息”）。
    SetTitle(CString(_T("消息记录 · ")) + m_titleName);
    SetTitleBarHeight(kHisTitleBarHeight);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kHisCaptionColor);
    SetCaptionGlyphColor(kHisCaptionColor);

    //标题栏左侧图标：用程序主图标。本窗标题栏透明，穿透显示的是根控件底色（白，
    //见 HistoryWindowLayout 里 root->SetBgColor），故按白底合成。
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kHisTitleIconSize, kHisTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    SetButtons(true, true, true);   //最小化 / 最大化 / 关闭
    SetResizable(true);
    SetMinSize(kHisMinWidth, kHisMinHeight);

    //开启双击合成：列表行的"双击整行即定位"要靠它，DuiHost 默认不发双击。
    EnableDoubleClick(true);

    InitUi();

    ResizeClient(kHisWindowWidth, kHisWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

HistoryViewData CBalloonHistoryWindow::BuildViewData()
{
    int64_t now = NowMs();
    m_model.Refresh(now);

    HistoryViewData data;
    data.m_title = CString(_T("消息记录 · ")) + m_titleName;
    data.m_totalCount = (int)m_model.Total();
    data.m_currentPage = m_model.CurrentPage();
    data.m_pageCount = m_model.PageCount();
    data.m_activeChip = (int)m_model.Filter().m_type;
    data.m_activeTimeIndex = ComboIndexFromRange(m_model.Filter().m_range);

    //空状态文案按当前筛选条件算：搜不到就提示换个词、Pin 分类空就顺带说怎么钉，
    //这比一句通用的"暂无内容"有用得多。列表非空时这两项用不上。
    const HistoryEmptyText emptyText = BuildEmptyStateText(m_model.Filter());
    data.m_emptyTitle    = Utf8ToCString(emptyText.m_title);
    data.m_emptySubtitle = Utf8ToCString(emptyText.m_subtitle);

    const std::vector<HistoryRecord>& recs = m_model.Records();
    for (size_t i = 0; i < recs.size(); ++i)
    {
        HistoryRowView row;
        row.m_senderName = ResolveSenderName(recs[i].m_fromid);
        row.m_avatarColor = ColorForId(recs[i].m_fromid);
        row.m_timeText = FormatTimeShort(recs[i].m_timestamp);
        row.m_previewText = Utf8ToCString(PreviewFromContent(recs[i].m_msgtype, recs[i].m_content));

        //行内左侧那一小块的数据。图片取内容MD5 换算缓存路径；文件取扩展名，
        //并把预览文字从 "[文件] 名字" 换成 "名字 · 大小" —— 类型已经由色块交代了，
        //再写一遍 "[文件]" 是浪费一行里最值钱的横向空间。
        const HistoryContentBrief brief =
            ParseContentBrief(recs[i].m_msgtype, recs[i].m_content);
        if (!brief.m_imageMd5.empty())
        {
            //缓存按登录账号隔离，故要拿当前登录用户的 userid 才能算出路径。
            //调试入口没有登录态（m_lpFMGClient 为空），此时不画缩略图。
            const int64_t selfId = (m_lpFMGClient != NULL)
                ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
            if (selfId > 0)
            {
                row.m_thumbImagePath = CString(
                    net::ImageTransfer::CacheFilePathForMd5(
                        net::CurrentUserAccount(), brief.m_imageMd5).c_str());
            }
        }
        else if (!brief.m_fileName.empty())
        {
            row.m_thumbFileExt = Utf8ToCString(FileExtensionOf(brief.m_fileName));

            CString text = Utf8ToCString(brief.m_fileName);
            const std::string sizeText = FormatFileSize(brief.m_fileSize);
            if (!sizeText.empty())
            {
                text += _T(" · ");
                text += Utf8ToCString(sizeText);
            }
            row.m_previewText = text;
        }
        else if (::message_type_folder == recs[i].m_msgtype)
        {
            //文件夹传输记录：把预览换成"[文件夹] 名字 · N 个文件 · 大小 · 结局"。
            //这条记录没有可下载的内容，也没有缩略图，一行文字就是它的全部信息，
            //故规模与结局都写进去（拼装口径见 BuildFolderRowText，有单测）。
            row.m_previewText = Utf8ToCString(BuildFolderRowText(brief));
        }
        row.m_msgid = recs[i].m_msgid;
        row.m_pinned = recs[i].m_pinned;

        //完整正文：文本消息保留 /f["<id>"] 原始标记，交给行内容块把表情画成图。
        //非文本消息（图片 / 文件 / 语音…）与预览同口径，都是"[图片]"这类占位。
        //文件行上面已经把预览换成了"名字 · 大小"，正文也跟着用那一份 —— 否则
        //展开的时候会突然变回"[文件] 名字"，前后不一致。
        if (recs[i].m_msgtype == ::message_type_file && !brief.m_fileName.empty())
        {
            row.m_fullText = row.m_previewText;
        }
        //文件夹记录同理：展开态与收起态显示同一行文字，否则展开时会突然从
        //"[文件夹] 名字 · 128 个文件 · 2.4 GB · 已完成" 变回只剩 "[文件夹] 名字"
        else if (recs[i].m_msgtype == ::message_type_folder)
        {
            row.m_fullText = row.m_previewText;
        }
        else
        {
            row.m_fullText =
                Utf8ToCString(FullTextFromContent(recs[i].m_msgtype, recs[i].m_content));
        }

        //展开态取自模型（按 msgid 记），故局部刷新与翻页往返都不会把它冲掉。
        row.m_expanded = m_model.IsExpanded(recs[i].m_msgid);

        data.m_rows.push_back(row);
    }

    //各行所属的"天"，供下面插日期分组头用。"天"须按本地时区算。
    std::vector<int64_t> dayKeys;
    dayKeys.reserve(recs.size());
    for (size_t i = 0; i < recs.size(); ++i)
    {
        dayKeys.push_back(LocalDayKey(recs[i].m_timestamp));
    }

    //注：这里<u>不再</u>把连续同一人的消息折叠成"省掉头像与名字"。
    //折叠原本是为了提高密度，但实测下来它把<u>消息之间的边界</u>抹掉了 —— 同一人连发
    //三条，看上去就是一条带换行的长消息，用户会奇怪"一条消息怎么有三个 Pin 按钮"。
    //消息记录是"按条查找"的界面，边界比密度重要，故每条都完整显示头像 / 名字 / 时间。
    //ComputeCompactFlags 与它的用例留着不删：折叠这件事本身没错，只是不适合这个界面，
    //日后若做"紧凑模式"开关可以直接复用。

    //日期分组头：每当"天"与上一行不同就插一条。首行恒插 —— 页首必须交代清楚是哪天，
    //否则翻到中间某页会看到一堆只有 HH:MM 的行、完全不知道是什么时候的。
    //分组在页内做、不跨页合并（见 HistoryRowView::m_groupLabel 的说明）。
    const int64_t todayKey = LocalDayKey((int64_t)::time(NULL) * 1000);
    for (size_t i = 0; i < data.m_rows.size() && i < dayKeys.size(); ++i)
    {
        if (i == 0 || dayKeys[i] != dayKeys[i - 1])
        {
            data.m_rows[i].m_groupLabel =
                Utf8ToCString(FormatDateGroupLabel(dayKeys[i], todayKey));
        }
    }

    //搜索命中高亮：关键字为空时区间集为空，预览标签表现与普通标签一致。
    const std::string& keyword = m_model.Filter().m_keyword;
    if (!keyword.empty())
    {
        const CString keywordW = Utf8ToCString(keyword);
        const std::wstring kw((LPCWSTR)keywordW);
        for (size_t i = 0; i < data.m_rows.size(); ++i)
        {
            //关键字要在"<u>扁平文本</u>"上找，不能在原始正文上找 —— 行内容块
            //（HistoryContentView）认的高亮区间是扁平口径：一个内联表情占<u>一个</u>
            //字符位（U+FFFC 占位）。而原始正文里一个表情是 `/f["5"]` 这么七八个字符，
            //拿它算出来的下标喂给内容块，含表情的消息高亮会整体往后错位。
            std::vector<balloonchat::ChatSegment> segs =
                balloonchat::ParseSendBody(data.m_rows[i].m_fullText);
            const std::wstring flat = balloonchatwnd::BuildFlatText(segs);
            data.m_rows[i].m_highlights = FindHighlightRanges(flat, kw);
        }
    }

    return data;
}

CString CBalloonHistoryWindow::ResolveSenderName(int64_t fromid) const
{
    //真实会话：走主面板按 userid 解析昵称（本人 / 联系人 / 回退）。
    if (m_pMainPanel != NULL)
    {
        return m_pMainPanel->ResolveSenderName(fromid);
    }
    //调试预置：查 m_senderNames。
    std::map<int64_t, CString>::const_iterator it = m_senderNames.find(fromid);
    if (it != m_senderNames.end())
    {
        return it->second;
    }
    CString name;
    name.Format(_T("用户%lld"), (long long)fromid);
    return name;
}

void CBalloonHistoryWindow::InitUi()
{
    HistoryViewData data = BuildViewData();
    SetClientContent(BuildHistoryWindowBody(data));

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    //捕获要复用的控件指针（所有权在控件树）。
    m_pScroll = dynamic_cast<DuiScrollView*>(content->FindCtrlById(kIdHistoryScroll));
    m_pPager = content->FindCtrlById(kIdHistoryPager);
    m_pFooterInfo = content->FindCtrlById(kIdHistoryFooterInfo);
    m_pCustomRow = content->FindCtrlById(kIdHistoryCustomRow);

    LOG_INFO(_T("历史消息记录窗客户区 UI 初始化完成。"));
}

LRESULT CBalloonHistoryWindow::OnMouseMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& bHandled)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    UpdateRowHover(pt);

    //只旁听、不消费：DuiHost 还要靠这条消息驱动按钮的 hover、滚动条拖动等等。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonHistoryWindow::OnMouseLeaveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/, BOOL& bHandled)
{
    //鼠标离开整个窗口：把悬停态全撤掉，否则会留一行"亮着"的。
    POINT none;
    none.x = -1;
    none.y = -1;
    UpdateRowHover(none);

    bHandled = FALSE;
    return 0;
}

void CBalloonHistoryWindow::UpdateRowHover(POINT pt)
{
    if (m_pScroll == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* content = m_pScroll->GetContent();
    if (content == NULL)
    {
        return;
    }

    //落点必须同时落在滚动视口内：滚出可视区的行其矩形仍在，只按行矩形判会让
    //视口外的行也亮起来。
    const RECT viewRc = m_pScroll->GetRect();
    const bool inView = (pt.x >= viewRc.left && pt.x < viewRc.right
                         && pt.y >= viewRc.top && pt.y < viewRc.bottom);

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = content->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        HistoryRow* row = dynamic_cast<HistoryRow*>(kids[i].get());
        if (row == NULL)
        {
            //日期分组头等非行控件跳过。
            continue;
        }
        const RECT rc = row->GetRect();
        const bool hit = inView
            && pt.x >= rc.left && pt.x < rc.right
            && pt.y >= rc.top && pt.y < rc.bottom;
        //SetHovered 对"值没变"自己会短路，这里不必再判一次。
        row->SetHovered(hit);
    }
}

// =======================================================================
// 行右键菜单（C2）与图片 / 文件的行内操作（C3 / C4）
//
// 右键与悬停一样<u>按几何位置在窗口层判</u>，不靠控件分发：DuiHost::OnRButtonDown
// 只把事件发给 HitTopMost 命中的那个最深控件，右键点在正文或某枚按钮上时，行容器
// 根本收不到。按落点算"点在哪一行"才不受子控件切换影响。
// =======================================================================

LRESULT CBalloonHistoryWindow::OnRButtonUpMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& bHandled)
{
    //只旁听、不消费 —— 置 FALSE 让 DuiFrameWindow / DuiHost 照常继续处理，
    //否则整个界面的右键交互都会失灵。
    bHandled = FALSE;

    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);

    const int row = RowIndexAtPoint(pt);
    if (row < 0)
    {
        return 0;   //没点在任何一行上（点到了筛选行、分页栏或空白处）
    }

    POINT ptScreen = pt;
    ::ClientToScreen(m_hWnd, &ptScreen);

    //TrackPopupMenu 是个模态消息循环，直接在这里跑会与 DuiHost 的鼠标捕获打架
    //（菜单收起之后界面点击失灵）。投递出去，等鼠标事件解栈之后再弹。
    ::PostMessage(m_hWnd, WM_HISTORY_SHOW_ROW_MENU, (WPARAM)row,
                  MAKELPARAM((WORD)ptScreen.x, (WORD)ptScreen.y));
    return 0;
}

LRESULT CBalloonHistoryWindow::OnChatOpenLinkMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM lParam, BOOL& /*bHandled*/)
{
    std::wstring* pUrl = (std::wstring*)lParam;
    if (pUrl == NULL)
    {
        return 0;
    }

    // 协议白名单：只放行 http / https，与两个聊天窗同一口径。
    if (balloonchatwnd::IsSafeLinkUrl(*pUrl))
    {
        ::ShellExecuteW(m_hWnd, L"open", pUrl->c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        LOG_ERROR("消息记录窗拒绝打开非 http/https 链接。");
    }
    delete pUrl;
    return 0;
}

LRESULT CBalloonHistoryWindow::OnShowRowMenuMsg(UINT /*uMsg*/, WPARAM wParam,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    const int row = (int)wParam;
    POINT ptScreen;
    //MAKELPARAM 打包时按 WORD 截断，取出时须按有符号还原 —— 多显示器布局下屏幕坐标
    //可能为负，当成无符号会把菜单弹到几万像素之外。
    ptScreen.x = GET_X_LPARAM(lParam);
    ptScreen.y = GET_Y_LPARAM(lParam);

    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return 0;
    }

    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(BuildRowMenuContext(row));
    if (model.empty())
    {
        return 0;
    }

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return 0;
    }
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].m_isSeparator)
        {
            ::AppendMenu(menu, MF_SEPARATOR, 0, NULL);
        }
        else
        {
            //菜单命令 id 直接用菜单项标识：它们都是从 1 起的小正整数，而
            //TrackPopupMenu 用 0 表示"用户没选"，不会撞。
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)model[i].m_id,
                          model[i].m_label.c_str());
        }
    }

    //落点必须夹进锚点所在显示器的工作区：列表行靠近窗口底边时，直接把光标坐标交给
    //TrackPopupMenu 会让菜单跑到桌面外看不见（开发约定文档的菜单不越屏规则）。
    //Win32 的 HMENU 在弹出前拿不到精确尺寸，按行数估算即可 —— 估大一点只会让靠边时
    //更早翻向左 / 上，不会误伤（与 ChatBubble 的做法一致）。
    SIZE menuSize;
    menuSize.cx = kRowMenuEstWidth;
    menuSize.cy = (LONG)model.size() * kRowMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(ptScreen, menuSize);

    ::SetForegroundWindow(m_hWnd);   //菜单要靠前台窗口才能在点击别处时正常收起
    const int cmd = ::TrackPopupMenu(menu,
                                     TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                     at.x, at.y, 0, m_hWnd, NULL);
    ::DestroyMenu(menu);

    if (cmd != 0)
    {
        ExecuteRowMenuCommand(cmd, row);
    }
    return 0;
}

int CBalloonHistoryWindow::RowIndexAtPoint(POINT pt) const
{
    if (m_pScroll == NULL)
    {
        return -1;
    }
    balloonwjui::DuiControl* content = m_pScroll->GetContent();
    if (content == NULL)
    {
        return -1;
    }

    //落点必须同时落在滚动视口内：滚出可视区的行其矩形仍在，只按行矩形判会把视口外的
    //行也算命中（与 UpdateRowHover 同一处理）。
    const RECT viewRc = m_pScroll->GetRect();
    if (pt.x < viewRc.left || pt.x >= viewRc.right
        || pt.y < viewRc.top || pt.y >= viewRc.bottom)
    {
        return -1;
    }

    //行下标由 ctrlId 反算（id = kIdHistoryRowBase + 行下标），<u>不能</u>按"第几个
    //HistoryRow 子控件"数 —— 中间还夹着日期分组头。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = content->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const HistoryRow* rowCtrl = dynamic_cast<const HistoryRow*>(kids[i].get());
        if (rowCtrl == NULL)
        {
            continue;
        }
        const RECT rc = rowCtrl->GetRect();
        if (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom)
        {
            const UINT id = rowCtrl->GetCtrlId();
            if (id >= (UINT)kIdHistoryRowBase
                && id < (UINT)kIdHistoryRowBase + kIdHistoryRowCapacity)
            {
                return (int)(id - (UINT)kIdHistoryRowBase);
            }
            return -1;
        }
    }
    return -1;
}

int64_t CBalloonHistoryWindow::SelfUserId() const
{
    //缓存与下载目录都按登录账号隔离，取不到 userid 就什么都算不出来
    //（调试入口没有登录态，此时一律当作"本地没有文件"处理）。
    if (m_lpFMGClient == NULL)
    {
        return 0;
    }
    return (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
}

bool CBalloonHistoryWindow::ResolveRowLocalFile(int row, CString& outPath) const
{
    outPath.Empty();

    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return false;
    }
    const HistoryRecord& rec = recs[(size_t)row];

    const int64_t selfId = SelfUserId();
    if (selfId <= 0)
    {
        return false;
    }

    const HistoryContentBrief brief = ParseContentBrief(rec.m_msgtype, rec.m_content);

    if (IsHistoryRowImageType(rec.m_msgtype))
    {
        //图片只有缓存这一处落点：本地缓存按内容 MD5 命名。
        if (brief.m_imageMd5.empty())
        {
            return false;
        }
        const std::wstring cache =
            net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), brief.m_imageMd5);
        if (cache.empty() || !PathExistsOnDisk(cache.c_str()))
        {
            return false;
        }
        outPath = cache.c_str();
        return true;
    }

    if (IsHistoryRowFileType(rec.m_msgtype))
    {
        //文件有两处可能的落点，优先级与回退交给纯逻辑 ResolveLocalFilePath 决定
        //（登记路径优先、失效再回退 MD5 缓存路径）。那套规则已有单测 Suite CFL，
        //这里只负责把"路径"与"文件在不在"两样事实喂给它。
        std::wstring registered;
        CFileLocalStore::GetInstance().Query(brief.m_fileMd5, brief.m_fileName, registered);

        std::wstring cache;
        if (!brief.m_fileMd5.empty() && !brief.m_fileName.empty())
        {
            cache = net::FileTransfer::CacheFilePathFor(net::CurrentUserAccount(),
                                                        brief.m_fileMd5, brief.m_fileName);
        }

        const std::wstring picked = balloonchatwnd::ResolveLocalFilePath(
            registered, !registered.empty() && PathExistsOnDisk(registered.c_str()),
            cache, !cache.empty() && PathExistsOnDisk(cache.c_str()));
        if (picked.empty())
        {
            return false;
        }
        outPath = picked.c_str();
        return true;
    }

    if (IsHistoryRowFolderType(rec.m_msgtype))
    {
        //文件夹传输记录只有一处落点：本机按会话 id 登记的那个根目录。发送端记的是源
        //目录、接收端记的是落地目录，各记各的。换一台机器一律查不到 —— 那台机器上从
        //来没有过这个目录，只显示记录、不给「打开文件夹」，这是预期行为。
        if (brief.m_folderSessionId.empty())
        {
            return false;
        }
        std::wstring registered;
        if (!CFolderLocalStore::GetInstance().Query(brief.m_folderSessionId, registered))
        {
            return false;
        }
        //登记在、目录却已被改名或删除时同样不给按钮，免得点下去打开一个空白窗口
        if (!DirectoryExistsOnDisk(registered.c_str()))
        {
            return false;
        }
        outPath = registered.c_str();
        return true;
    }

    return false;   //其余类型没有"本地文件"这一说
}

HistoryRowMenuContext CBalloonHistoryWindow::BuildRowMenuContext(int row) const
{
    HistoryRowMenuContext ctx;

    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return ctx;   //空上下文：菜单只会剩通用项
    }
    const HistoryRecord& rec = recs[(size_t)row];

    ctx.m_msgType = rec.m_msgtype;
    ctx.m_pinned  = rec.m_pinned;
    ctx.m_msgId   = rec.m_msgid;
    ctx.m_downloading =
        (m_downloadingMsgids.find(rec.m_msgid) != m_downloadingMsgids.end());

    CString local;
    ctx.m_hasLocalFile = ResolveRowLocalFile(row, local);
    return ctx;
}

void CBalloonHistoryWindow::ExecuteRowMenuCommand(int menuId, int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];

    CString localPath;
    const bool hasLocal = ResolveRowLocalFile(row, localPath);

    switch (menuId)
    {
    case kHistoryRowMenuCopy:
        CopyRowText(row);
        break;

    case kHistoryRowMenuCopyImage:
        if (hasLocal)
        {
            if (!flamingo::CopyImageFileToClipboard((LPCWSTR)localPath))
            {
                ::MessageBox(m_hWnd, _T("复制图片失败，可能文件已被移动或删除。"),
                             _T("消息记录"), MB_OK | MB_ICONWARNING);
            }
        }
        break;

    case kHistoryRowMenuSaveImageAs:
        if (hasLocal)
        {
            //默认文件名与聊天窗里那条路同口径（按 msgid + 原图扩展名给一个可读名字）。
            balloonchatwnd::SaveChatImageAs(
                m_hWnd, std::wstring((LPCWSTR)localPath),
                balloonchatwnd::MakeImageSaveAsDefaultName(
                    std::wstring((LPCWSTR)localPath), (long long)rec.m_msgid));
        }
        break;

    case kHistoryRowMenuOpenFile:
        OpenRowFile(localPath, hasLocal);
        break;

    case kHistoryRowMenuOpenFolder:
        OpenRowFolder(localPath, hasLocal);
        break;

    //文件夹传输记录：打开那个目录<u>本身</u>，而不是像文件那样打开父目录再选中它。
    //路径必须带引号，否则含空格的路径会被 explorer 拆成多个参数。
    case kHistoryRowMenuOpenFolderRoot:
        if (hasLocal && !localPath.IsEmpty())
        {
            CString folderParam;
            folderParam.Format(_T("\"%s\""), (LPCTSTR)localPath);
            ::ShellExecute(m_hWnd, _T("open"), _T("explorer.exe"), folderParam,
                           NULL, SW_SHOWNORMAL);
        }
        break;

    case kHistoryRowMenuSaveFileAs:
        if (hasLocal)
        {
            SaveRowFileAs(row, localPath);
        }
        break;

    case kHistoryRowMenuDownload:
        //从菜单点「下载」纯粹是把文件拿到本地，下完<u>不</u>弹看图窗 ——
        //那是"双击缩略图"才有的意图。
        StartRowDownload(row, false);
        break;

    case kHistoryRowMenuCancelDownload:
        CancelRowDownload(row);
        break;

    case kHistoryRowMenuLocate:
        LocateRow(row);
        break;

    case kHistoryRowMenuPin:
        //与行内那枚 Pin 按钮完全同口径：只发请求，界面等服务端推送回来再变，
        //不做乐观更新。无权取消时由服务端拒绝并提示。
        if (m_pMainPanel != NULL && rec.m_msgid > 0)
        {
            const HistoryFilter& filter = m_model.Filter();
            CIUSocket::GetInstance().PinMessage(filter.m_ctype, filter.m_cid,
                                                (long long)rec.m_msgid, !rec.m_pinned);
        }
        break;

    default:
        break;
    }
}

void CBalloonHistoryWindow::CopyRowText(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }

    const CString full =
        Utf8ToCString(FullTextFromContent(recs[(size_t)row].m_msgtype,
                                          recs[(size_t)row].m_content));
    if (full.IsEmpty())
    {
        return;
    }

    //正文里的 /f["<id>"] 要换成可读的「[表情]」，与气泡复制同口径 —— 粘到其它聊天软件、
    //记事本里都读得懂。原始标记只有粘回本程序输入框才有意义，那不是主要场景。
    std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(full);
    std::wstring text;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].m_bFace)
        {
            text += L"[表情]";
        }
        else
        {
            text += (LPCWSTR)segs[i].m_strText;
        }
    }

    flamingo::CopyTextToClipboard(text.c_str());
    LOG_INFO(_T("消息记录：复制了一条正文，长度 %d。"), (int)text.size());
}

void CBalloonHistoryWindow::OpenRowFile(const CString& localPath, bool hasLocal)
{
    if (!hasLocal || localPath.IsEmpty())
    {
        return;
    }
    //ShellExecute 返回值 <= 32 表示失败（没有关联程序、文件刚被删掉等）。
    const INT_PTR kShellExecOkThreshold = 32;
    HINSTANCE hr = ::ShellExecute(m_hWnd, _T("open"), localPath, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hr <= kShellExecOkThreshold)
    {
        ::MessageBox(m_hWnd, _T("打开文件失败，可能没有关联的打开方式。"),
                     _T("消息记录"), MB_OK | MB_ICONWARNING);
    }
}

void CBalloonHistoryWindow::OpenRowFolder(const CString& localPath, bool hasLocal)
{
    if (!hasLocal || localPath.IsEmpty())
    {
        return;
    }
    //资源管理器定位并选中该文件。路径<u>必须带引号</u> —— 不带的话含空格的路径会被
    //explorer 拆成多个参数，结果打开的是"我的文档"而不是定位到文件。
    CString param;
    param.Format(_T("/select,\"%s\""), (LPCTSTR)localPath);
    ::ShellExecute(m_hWnd, _T("open"), _T("explorer.exe"), param, NULL, SW_SHOWNORMAL);
}

void CBalloonHistoryWindow::SaveRowFileAs(int row, const CString& localPath)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size() || localPath.IsEmpty())
    {
        return;
    }

    const HistoryContentBrief brief =
        ParseContentBrief(recs[(size_t)row].m_msgtype, recs[(size_t)row].m_content);
    CString defName = Utf8ToCString(brief.m_fileName);
    if (defName.IsEmpty())
    {
        defName = _T("文件");
    }

    //默认扩展名取原文件名的后缀，使另存出来的文件仍能被正确打开。
    CString defExt;
    const int dot = defName.ReverseFind(_T('.'));
    if (dot > 0 && dot + 1 < defName.GetLength())
    {
        defExt = defName.Mid(dot + 1);
    }

    CFileDialog dlg(FALSE, defExt.IsEmpty() ? NULL : (LPCTSTR)defExt, defName,
                    OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY,
                    _T("所有文件 (*.*)\0*.*\0"), m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return;
    }

    //整字节拷贝，不做任何转换 —— 另存出来的必须与原文件逐字节一致。
    if (!::CopyFile(localPath, dlg.m_szFileName, FALSE))
    {
        ::MessageBox(m_hWnd, _T("另存文件失败，请检查目标位置是否可写。"),
                     _T("消息记录"), MB_OK | MB_ICONWARNING);
        return;
    }
    LOG_INFO(_T("消息记录：文件另存为完成。"));
}

void CBalloonHistoryWindow::StartRowDownload(int row, bool viewAfter)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];

    //调试入口用的是预置假库、没有真实连接，发出去也没意义。
    if (m_pMainPanel == NULL)
    {
        return;
    }
    const int64_t selfId = SelfUserId();
    if (selfId <= 0 || rec.m_msgid <= 0)
    {
        return;
    }

    const HistoryContentBrief brief = ParseContentBrief(rec.m_msgtype, rec.m_content);

    if (IsHistoryRowImageType(rec.m_msgtype))
    {
        if (brief.m_imageFileId.empty())
        {
            //没有 fileid 就无从下载（老消息缺字段等）。告诉用户，别让他一直等。
            ::MessageBox(m_hWnd, _T("这条消息缺少图片信息，无法下载原图。"),
                         _T("消息记录"), MB_OK | MB_ICONWARNING);
            return;
        }
        net::ImageTransfer::GetInstance().EnqueueDownload(
            (int64_t)rec.m_msgid, brief.m_imageFileId, brief.m_imageMd5,
            brief.m_imageSize, net::CurrentUserAccount());
    }
    else if (IsHistoryRowFileType(rec.m_msgtype))
    {
        if (brief.m_fileId.empty())
        {
            ::MessageBox(m_hWnd, _T("这条消息缺少文件信息，无法下载。"),
                         _T("消息记录"), MB_OK | MB_ICONWARNING);
            return;
        }
        net::FileTransfer::GetInstance().EnqueueDownload(
            (int64_t)rec.m_msgid, brief.m_fileId, brief.m_fileName,
            brief.m_fileMd5, brief.m_fileSize, net::CurrentUserAccount());
    }
    else
    {
        return;   //其余类型没有可下载的东西
    }

    m_downloadingMsgids.insert(rec.m_msgid);
    if (viewAfter)
    {
        m_pendingViewMsgid = rec.m_msgid;
    }
    LOG_INFO(_T("消息记录：发起下载 msgid=%lld, 下完是否看图=%d。"),
             (long long)rec.m_msgid, (int)viewAfter);

    //菜单项要从「下载」变成「取消下载」，刷一次列表。
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::CancelRowDownload(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];

    if (IsHistoryRowFileType(rec.m_msgtype))
    {
        net::FileTransfer::GetInstance().CancelDownload((int64_t)rec.m_msgid);
    }
    //图片下载没有取消接口（net::ImageTransfer 只提供 EnqueueDownload）。图片通常只有
    //几百 KB、几秒就完，为它单开一条取消链路不划算；这里只把本窗口的跟踪状态撤掉，
    //传输仍会跑完并落进缓存 —— 那份缓存下次看图时还用得上，不算浪费。
    m_downloadingMsgids.erase(rec.m_msgid);
    if (m_pendingViewMsgid == rec.m_msgid)
    {
        m_pendingViewMsgid = 0;   //撤掉"下完自动看图"的待办
    }
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::ViewRowImage(int row)
{
    CString localPath;
    if (ResolveRowLocalFile(row, localPath))
    {
        //本地已有原图：直接开看图窗（进程内共享一个实例，已开则换图置前）。
        CBalloonImageViewerWindow::OpenShared(m_hWnd, localPath);
        return;
    }

    //本地没有：发起下载，下完自动开看图窗。双击的意图就是"我要看这张图"，
    //下载只是达成它的手段，故这里 viewAfter 传 true。
    StartRowDownload(row, true);
}

void CBalloonHistoryWindow::OnImageDownloadFinished(int64_t msgid, bool ok,
                                                    const std::wstring& localPath)
{
    //不是本窗口发起的那几笔一概不管 —— 聊天窗自己的下载不该在这里弹窗。
    if (m_downloadingMsgids.find(msgid) == m_downloadingMsgids.end())
    {
        return;
    }
    m_downloadingMsgids.erase(msgid);

    const bool wantView = (m_pendingViewMsgid == msgid);
    if (wantView)
    {
        m_pendingViewMsgid = 0;
    }

    if (!ok)
    {
        LOG_ERROR(_T("消息记录：图片下载失败 msgid=%lld。"), (long long)msgid);
        ::MessageBox(m_hWnd, _T("图片下载失败，请检查网络后重试。"),
                     _T("消息记录"), MB_OK | MB_ICONWARNING);
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
        return;
    }

    //成功：缩略图这下能画出来了，刷一次列表；若这笔下载本就是"双击要看大图"引起的，
    //顺带把看图窗打开。
    if (wantView && !localPath.empty())
    {
        CBalloonImageViewerWindow::OpenShared(m_hWnd, localPath.c_str());
    }
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::OnFileDownloadFinished(int64_t msgid, bool ok,
                                                   const std::wstring& /*localPath*/,
                                                   bool cancelled)
{
    if (m_downloadingMsgids.find(msgid) == m_downloadingMsgids.end())
    {
        return;
    }
    m_downloadingMsgids.erase(msgid);

    //用户主动取消不算失败，不弹提示 —— 他知道自己做了什么。
    if (!ok && !cancelled)
    {
        LOG_ERROR(_T("消息记录：文件下载失败 msgid=%lld。"), (long long)msgid);
        ::MessageBox(m_hWnd, _T("文件下载失败，请检查网络后重试。"),
                     _T("消息记录"), MB_OK | MB_ICONWARNING);
    }

    //无论成败都刷一次：成功了菜单要从「下载」变成「打开 / 另存为」，
    //失败或取消了要从「取消下载」变回「下载」。
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::LocateRow(int row)
{
    //跳到聊天窗并滚动定位到该消息。调试入口没有主面板（m_pMainPanel==NULL），
    //也就没有可跳的聊天窗，直接忽略。
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (m_pMainPanel == NULL || row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryFilter& filter = m_model.Filter();
    m_pMainPanel->LocateMessageInChat(filter.m_ctype, filter.m_cid,
                                      (long long)recs[(size_t)row].m_msgid);
}

void CBalloonHistoryWindow::JumpToPage(int page)
{
    //页号夹取交给模型（GoToPage 自己会夹到 [1, PageCount]），这里只挡掉"根本没输
    //数字"的情形 —— _ttoi 对空串与非数字都返回 0，直接翻过去会跳到第 1 页，
    //而用户其实什么都没提交。
    if (page <= 0)
    {
        return;
    }
    if (page == m_model.CurrentPage())
    {
        return;
    }
    m_model.GoToPage(page);
    //与其余翻页入口同口径：延迟刷新，避免在被点击控件自身的事件回调里销毁它。
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::RefreshContent()
{
    HistoryViewData data = BuildViewData();

    //1. 列表：整体换新内容。
    if (m_pScroll != NULL)
    {
        //行高按正文在列表实际宽度下折出几行来算，故必须把真实宽度传下去 ——
        //传 0 会走兜底宽度，窗口一旦不是那个宽度，长消息的行高就会偏（要么被下一行
        //压住，要么下面空出一截）。
        const RECT rcScroll = m_pScroll->GetRect();
        const int listWidth = rcScroll.right - rcScroll.left;
        m_pScroll->SetContent(BuildListContent(data.m_rows, data.m_emptyTitle,
                                               data.m_emptySubtitle, listWidth));
        m_pScroll->ForceLayout(m_pScroll->GetRect());
        m_pScroll->Invalidate();
    }

    //2. 分页器：清空旧按钮 + 按新页数重填。
    if (m_pPager != NULL)
    {
        RemoveAllChildren(m_pPager);
        PopulatePagerButtons(m_pPager, data.m_currentPage, data.m_pageCount);
        m_pPager->ForceLayout(m_pPager->GetRect());
        m_pPager->Invalidate();
    }

    //3. 底部信息与标题副行（总数 / 页码可能变）。
    if (m_pFooterInfo != NULL)
    {
        DuiLabel* info = dynamic_cast<DuiLabel*>(m_pFooterInfo);
        if (info != NULL)
        {
            CString text;
            text.Format(_T("共 %d 条 · 第 %d/%d 页"),
                        data.m_totalCount, data.m_currentPage, data.m_pageCount);
            info->SetText(text);
        }
    }

    DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        //跳页微调框：范围随总页数走、当前值随当前页走。它在底部行里、<u>不</u>随局部
        //刷新重建（重建的只有列表与分页器），故要在这里就地同步。SetValue 传
        //notify=false —— 同步显示不该反过来再触发一次翻页。
        DuiSpinBox* jump = dynamic_cast<DuiSpinBox*>(content->FindCtrlById(kIdHistoryPageJump));
        if (jump != NULL)
        {
            jump->SetRange(1, (data.m_pageCount > 0) ? data.m_pageCount : 1);
            jump->SetValue(data.m_currentPage, false);
            jump->Invalidate();
        }

        DuiLabel* sub = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdHistorySubtitle));
        if (sub != NULL)
        {
            CString text;
            text.Format(_T("共 %d 条消息"), data.m_totalCount);
            sub->SetText(text);
        }

        //4. 类型胶囊选中态（就地改 Variant，不重建，保住搜索框焦点）。
        //   下标须与 HistoryTypeFilter 的取值一一对应，新增胶囊时两处一起改。
        const int kChipCount = 7;
        const UINT kChipIds[kChipCount] =
        {
            kIdHistoryChipAll, kIdHistoryChipText, kIdHistoryChipImage,
            kIdHistoryChipFile, kIdHistoryChipVoice, kIdHistoryChipLink,
            kIdHistoryChipPin,
        };
        for (int i = 0; i < kChipCount; ++i)
        {
            DuiButton* chip = dynamic_cast<DuiButton*>(content->FindCtrlById(kChipIds[i]));
            if (chip != NULL)
            {
                chip->SetVariant((i == data.m_activeChip)
                                 ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
                chip->Invalidate();
            }
        }
    }
}

void CBalloonHistoryWindow::SetCustomDateVisible(bool show)
{
    if (m_pCustomRow == NULL)
    {
        return;
    }
    m_pCustomRow->SetVisible(show);
    //切可见性后须强制重排客户区，否则隐藏 / 显示的行不让出 / 占用空间（balloonui 坑）。
    DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        content->ForceLayout(content->GetRect());
        content->Invalidate();
    }
}

void CBalloonHistoryWindow::DefaultCustomSelections()
{
    int64_t now = NowMs();
    __time64_t t = (__time64_t)(now / 1000);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return;
    }
    int curY = tmv.tm_year + 1900;
    int curM = tmv.tm_mon + 1;
    int curD = tmv.tm_mday;
    DuiControl* content = GetClientContent();
    //起：本月 1 日；止：今天。下拉项下标 = 值 - 基值（年基 kCustomDateYearBase，月/日基 1）。
    SetComboSel(content, kIdHistoryCustomStartY, curY - kCustomDateYearBase);
    SetComboSel(content, kIdHistoryCustomStartM, curM - 1);
    SetComboSel(content, kIdHistoryCustomStartD, 0);
    SetComboSel(content, kIdHistoryCustomEndY, curY - kCustomDateYearBase);
    SetComboSel(content, kIdHistoryCustomEndM, curM - 1);
    SetComboSel(content, kIdHistoryCustomEndD, curD - 1);
}

void CBalloonHistoryWindow::ApplyCustomRangeFromCombos()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    int sy = kCustomDateYearBase + ReadComboSel(content, kIdHistoryCustomStartY);
    int sm = 1 + ReadComboSel(content, kIdHistoryCustomStartM);
    int sd = 1 + ReadComboSel(content, kIdHistoryCustomStartD);
    int ey = kCustomDateYearBase + ReadComboSel(content, kIdHistoryCustomEndY);
    int em = 1 + ReadComboSel(content, kIdHistoryCustomEndM);
    int ed = 1 + ReadComboSel(content, kIdHistoryCustomEndD);
    int64_t startMs = DateToUnixMs(sy, sm, sd, false);
    int64_t endMs = DateToUnixMs(ey, em, ed, true);
    m_model.SetCustomRange(startMs, endMs);
}

LRESULT CBalloonHistoryWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonHistoryWindow::OnRefreshContentMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    RefreshContent();
    return 0;
}

LRESULT CBalloonHistoryWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    DuiControl* content = GetClientContent();
    UINT id = pNotify->ctrlId;

    //改完模型状态后统一 PostMessage 延迟刷新——避免在被点击控件（页码按钮等）的事件
    //回调里销毁它本身。
    if (pNotify->code == DUIN_CLICK)
    {
        //类型胶囊：id 连续，510..516 → 类型枚举 0..6（末尾那枚是 Pin）。
        if (id >= kIdHistoryChipAll && id <= kIdHistoryChipPin)
        {
            m_model.SetTypeFilter((HistoryTypeFilter)(id - kIdHistoryChipAll));
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        //分页固定按钮。
        if (id == kIdHistoryPageFirst)
        {
            m_model.GoToPage(1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        if (id == kIdHistoryPagePrev)
        {
            m_model.GoToPage(m_model.CurrentPage() - 1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        if (id == kIdHistoryPageNext)
        {
            m_model.GoToPage(m_model.CurrentPage() + 1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        if (id == kIdHistoryPageLast)
        {
            m_model.GoToPage(m_model.PageCount());
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        //页码按钮：id = 基址 + (页号-1)。上界按<u>本段自己的容量</u>算，不拿下一段的
        //基址当界 —— 那样一旦本段用满就会溢进下一段、被别的分支接走（见
        //HistoryWindowLayout.h 里三段 id 的说明）。
        if (id >= (UINT)kIdHistoryPageNumBase
            && id < (UINT)kIdHistoryPageNumBase + kIdHistoryPageNumCapacity)
        {
            m_model.GoToPage((int)(id - kIdHistoryPageNumBase) + 1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        //导出按钮：把当前筛选条件下的全部消息导出为 .txt（弹另存对话框，同步阻塞；
        //导出按钮自身不会被刷新销毁，无 use-after-free 风险）。
        if (id == kIdHistoryExport)
        {
            ExportToTxt();
            return 0;
        }
        //行内“定位”按钮：跳到聊天窗并滚动定位到该消息（仅真实会话、有主面板时生效；
        //调试入口 m_pMainPanel==NULL 不处理）。按钮 id = 基址 + 当前页行下标。
        if (id >= (UINT)kIdHistoryLocateBase
            && id < (UINT)kIdHistoryLocateBase + kIdHistoryLocateCapacity)
        {
            LocateRow((int)(id - kIdHistoryLocateBase));
            return 0;
        }
        //行内“展开 / 收起”链接：翻转该条的展开态并局部刷新。
        //
        //与“定位 / Pin”不同，这一个<u>纯粹是本地视图状态</u>，不发任何请求、不等服务端。
        //状态记在模型里（按 msgid），故翻页离开再翻回来仍然是展开的。
        //链接 id = 基址 + 当前页行下标；只有正文超出折叠行数的行才有这个控件，
        //故这一段的 id 在同一页里是稀疏的，不能假设“第 i 个链接就是第 i 行”。
        if (id >= (UINT)kIdHistoryExpandBase
            && id < (UINT)kIdHistoryExpandBase + kIdHistoryExpandCapacity)
        {
            const int row = (int)(id - kIdHistoryExpandBase);
            const std::vector<HistoryRecord>& recs = m_model.Records();
            if (row >= 0 && row < (int)recs.size())
            {
                m_model.ToggleExpanded(recs[(size_t)row].m_msgid);
                //行高变了，整个列表要重建。延迟到事件解栈之后再做 —— 与其它交互同路子，
                //避免在 DUI 通知的同步派发栈里销毁刚刚发出通知的那个控件。
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            }
            return 0;
        }
        //行内“Pin / 取消”按钮：只发请求，界面等服务端推 cmd_message_pinned_notify 回来、
        //主面板刷新本窗口时才变——与气泡右键菜单那条路完全同口径，不做乐观更新。
        //按钮 id = 基址 + 当前页行下标。
        if (id >= (UINT)kIdHistoryPinBase
            && id < (UINT)kIdHistoryPinBase + kIdHistoryPinCapacity)
        {
            int row = (int)(id - kIdHistoryPinBase);
            const std::vector<HistoryRecord>& recs = m_model.Records();
            if (row >= 0 && row < (int)recs.size())
            {
                const HistoryFilter& filter = m_model.Filter();
                const HistoryRecord& rec = recs[(size_t)row];
                //调试入口用的是预置假库、没有真实会话，发出去也没意义。
                if (m_pMainPanel != NULL && rec.m_msgid > 0)
                {
                    CIUSocket::GetInstance().PinMessage(filter.m_ctype, filter.m_cid,
                                                        (long long)rec.m_msgid,
                                                        !rec.m_pinned);
                }
            }
            return 0;
        }
    }
    else if (pNotify->code == DUIN_VALUECHANGED)
    {
        //搜索框文字变化：取最新文本设进模型（搜索框持久、不会被刷新重建）。
        if (id == kIdHistorySearch && content != NULL)
        {
            DuiEdit* search = dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdHistorySearch));
            if (search != NULL)
            {
                m_model.SetKeyword(CStringToUtf8(search->GetText()));
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            }
            return 0;
        }
        //时间下拉切换。
        if (id == kIdHistoryTimeCombo && content != NULL)
        {
            DuiComboBox* combo = dynamic_cast<DuiComboBox*>(content->FindCtrlById(kIdHistoryTimeCombo));
            if (combo != NULL)
            {
                HistoryTimeRange range = TimeRangeFromComboIndex(combo->GetCurSel());
                if (range == kHistoryTimeCustom)
                {
                    //自定义：默认“本月 1 日 → 今天”，显示日期行并据下拉算出区间。
                    DefaultCustomSelections();
                    SetCustomDateVisible(true);
                    ApplyCustomRangeFromCombos();
                }
                else
                {
                    SetCustomDateVisible(false);
                    m_model.SetTimeRange(range);
                }
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            }
            return 0;
        }
        //自定义日期的 6 个下拉任一变化：重算自定义区间并刷新。
        if (id >= kIdHistoryCustomStartY && id <= kIdHistoryCustomEndD)
        {
            ApplyCustomRangeFromCombos();
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        //跳页微调框的 ▲▼ 被点：extra 是新值，直接翻过去。手输入的提交走下面的
        //KILLFOCUS 分支（DuiSpinBox 不会把手输入自动同步到自身的值）。
        if (id == kIdHistoryPageJump)
        {
            JumpToPage((int)pNotify->extra);
            return 0;
        }
    }
    else if (pNotify->code == DUIN_DBLCLK)
    {
        //双击整行 = 点「定位」。给没注意到悬停按钮的用户留的一条更快的路。
        //行 id = 基址 + 当前页行下标，与「定位」按钮认的是同一个下标。
        if (id >= (UINT)kIdHistoryRowBase
            && id < (UINT)kIdHistoryRowBase + kIdHistoryRowCapacity)
        {
            const int row = (int)(id - kIdHistoryRowBase);
            const std::vector<HistoryRecord>& recs = m_model.Records();
            //图片行的双击改成"看大图"（本地没有原图时先下载、下完自动开），
            //其余类型仍是"定位到聊天窗"。图片行的定位只留行内那枚「定位」按钮。
            //这条取舍是与用户确认过的，见 docs/消息记录内容交互方案.md §10-2。
            if (row >= 0 && row < (int)recs.size()
                && IsHistoryRowImageType(recs[(size_t)row].m_msgtype))
            {
                ViewRowImage(row);
                return 0;
            }
            LocateRow(row);
            return 0;
        }
    }
    else if (pNotify->code == DUIN_KILLFOCUS)
    {
        //跳页微调框内部 EDIT 失焦：把用户手敲的页号提交。放在失焦而不是每键提交，
        //是因为敲 "137" 的过程中会依次经过 1、13，每一步都翻一次页毫无意义。
        if (id == kIdHistoryPageJumpEdit && content != NULL)
        {
            DuiSpinBox* jump = dynamic_cast<DuiSpinBox*>(content->FindCtrlById(kIdHistoryPageJump));
            if (jump != NULL && jump->GetEdit() != NULL)
            {
                JumpToPage(_ttoi(jump->GetEdit()->GetText()));
            }
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}
