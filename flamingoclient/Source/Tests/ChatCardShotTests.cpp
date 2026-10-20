/**
 *  ChatCardShotTests.cpp
 *
 *  聊天消息列表里各类卡片与普通聊天气泡「现状」的截图（隐藏用例，标签 [.cardshot]，平时不运行）。
 *  背景：2026-10-14 准备统一美化聊天卡片与聊天气泡，先把单聊与群聊里现在画出来的样子存成 PNG，
 *  供美化前后逐张对比。用例只出图，不做像素断言；只 REQUIRE 宿主窗口建成、PNG 编码器取到、每张图保存成功等关键步骤。
 *
 *    - CARDSHOT1 各类卡片：每类一张，文件名 cardshot_<两位序号>_<英文短名>.png，清单 cardshot_manifest.txt。
 *    - CARDSHOT2 普通聊天气泡：两张整段会话图 bubble_chat_single.png / bubble_chat_group.png（不裁剪，高度按内容），
 *      加上各种气泡的单项特写 bubble_<两位序号>_<英文短名>.png，清单 bubble_manifest.txt。
 *    - CARDSHOT3 卡片样式件（2026-10-14，docs/聊天卡片美化方案.md 第 3 节）：十五种卡片的图标块（含已结束的灰色）、五种状态标签、
 *      三级按钮的正常 / 悬停 / 置灰，以及两张只有头部与底部一行的示例卡片，画在一张图 cardstyle_kit.png 上，供人工核对样式。
 *    清单都是 UTF-8，每行「文件名<TAB>中文说明」。
 *
 *  做法：
 *    - 单项特写每张单独建一个 ChatMessageList（宽 760 像素，常见聊天窗宽度），先放一行空的时间分隔行占位，再放要截图的消息。
 *      占位行不画任何东西，只为把消息行从列表顶端的 1 像素分隔线下面挪开，裁剪时上方能留出边距。
 *    - 消息一律走真实构造路径：各卡片的 ApplyXxxCard（content 按协议格式手写 JSON）、ChatMockData 的 MakeXxxMessage、
 *      引用回复的 BuildQuoteFromMessage / FillQuoteForRender、@ 片段的 ConvertSpansForRender、系统提示的 MakeSystemTipRow；
 *      撤回、发送失败、多选这类状态变化调用列表上与聊天窗相同的方法（RecallMessageByMsgid、MarkSendFailed、
 *      EnterMultiSelectWith）。文件夹传输气泡没有构造函数，聊天窗本身就是逐字段填写的，这里照做。
 *      例外是「大 emoji」（kMsgEmoji）：真实收发路径从不产生这种消息，只有 ChatMockData 的模拟数据里有，这里照模拟数据的写法构造。
 *    - 列表挂到一个不显示的 DuiHost 上排版，画进 32 位 DIB，按消息所在行（含头像与发送者名）上下各留 12 像素裁出来存 PNG。
 *      边距取列表自己铺的底色（暖白 #FAF9F6），与聊天窗里看到的一致。
 *    - Markdown 卡片依赖 FlamingoMarkdown.dll 的函数表：测试程序里已经编进了同一份函数表（flamingomd::GetMdApiTable），
 *      照 ChatBubbleMarkdownTests.cpp 的做法用 markdown::SetMarkdownApiOverrideForTest 接上，画出来的就是真正的卡片。
 *    - 图片气泡的图是用例自己用 GDI+ 画的一张示例照片，存在输出目录的 fixtures 子目录里；先等 ChatImageCache 在后台解码好，
 *      再绘制列表，否则画出来的是占位块。
 *    - 内联 QQ 表情（/f["5"]）没有画：表情图片按 exe 所在目录下的 Face\ 查找，测试程序所在的 Tests 目录下没有，
 *      会话里的表情一律用 Unicode emoji 字符。
 *
 *  输出：环境变量 CARD_SHOT_DIR 指定的目录；没设时为 <仓库>\tmp\cardshot（按测试程序位于 <仓库>\flamingoclient\<配置>\Tests\
 *  推算；不存在就创建）。按开发约定，临时文件只写仓库的 tmp\ 目录，不写系统临时目录。
 *
 *  运行：FlamingoUnit.exe "[.cardshot]"（两个用例都跑）；只跑一个用 FlamingoUnit.exe "CARDSHOT1*" 或 "CARDSHOT2*"
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../UI/BalloonChat/BalloonChatMsgFormat.h"
#include "../UI/BalloonChatWnd/ChatAnnouncementCard.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatCardModel.h"
#include "../UI/BalloonChatWnd/ChatCardPaint.h"
#include "../UI/BalloonChatWnd/ChatCardStyle.h"
#include "../UI/BalloonChatWnd/ChatPaintUtil.h"
#include "../UI/BalloonChatWnd/ChatCallCard.h"
#include "../UI/BalloonChatWnd/ChatCloudRecordCard.h"
#include "../UI/BalloonChatWnd/ChatFileShareCard.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../UI/BalloonChatWnd/ChatFolderStateModel.h"
#include "../UI/BalloonChatWnd/ChatGroupCallCard.h"
#include "../UI/BalloonChatWnd/ChatGroupInviteCard.h"
#include "../UI/BalloonChatWnd/ChatImageCache.h"
#include "../UI/BalloonChatWnd/ChatMemoCard.h"
#include "../UI/BalloonChatWnd/ChatMergedCard.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatQuoteModel.h"
#include "../UI/BalloonChatWnd/ChatRelayCard.h"
#include "../UI/BalloonChatWnd/ChatScreenShareCard.h"
#include "../UI/BalloonChatWnd/ChatSecretMode.h"
#include "../UI/BalloonChatWnd/ChatSendLimit.h"
#include "../UI/BalloonChatWnd/ChatSystemTip.h"
#include "../UI/BalloonChatWnd/ChatTimeSeparator.h"
#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../UI/BalloonChatWnd/FileBubbleFormat.h"
#include "../UI/BalloonChatWnd/GroupMentionModel.h"
#include "../UI/BalloonMain/AvatarPalette.h"
#include "../Common/Code/CodeLexer.h"
#include "../Feature/GroupCall/GroupCallStatusCache.h"
#include "../Feature/Markdown/MarkdownDocument.h"
#include "../Feature/Memo/MemoClock.h"
#include "../Feature/ScreenShare/ScreenShareRoomState.h"
#include "../net/Protocol.h"
#include "../net/VoiceContent.h"
#include "../../FlamingoMarkdown/MdApiTable.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// ---- 画布与裁剪 ----------------------------------------------------------

const int kShotListWidth    = 760;    // 消息列表宽（像素），常见的聊天窗宽度
const int kShotCanvasHeight = 1000;   // 单项特写的画布高（像素）：占位行 38 像素加上最高的一张卡片或两行气泡也远用不完
const int kShotConvMaxHeight = 4000;  // 整段会话图第一次排版用的高度（像素），只用来量出内容总高，须大于任何一段会话的内容总高
const int kShotConvTailPx   = 16;     // 整段会话图在最后一行下方留的空白（像素），像聊天窗消息区底部那样不贴着分隔线
const int kShotMarginPx     = 12;     // 裁剪时消息所在行上下各留的边距（像素）
const int kShotBytesPerPx   = 4;      // 32 位 DIB 每个像素的字节数
const size_t kShotRowsPerList = 2;    // CARDSHOT1 每个列表里至少的行数：一行占位、一行卡片（本人刚发出的卡片前面还会多一行时间）
const size_t kShotFirstMsgRow = 1;    // 单项特写里第一条消息所在行的下标（第 0 行是占位行）
const COLORREF kShotCanvasFill = RGB(0xFF, 0xFF, 0xFF);   // 画布初始底色；列表绘制时会在整个行区铺自己的暖白底

// ---- 输出目录与文件名 ----------------------------------------------------

const size_t kShotDirChars   = 512;   // 读环境变量 CARD_SHOT_DIR 的缓冲区长度（字符）
const size_t kShotIndexChars = 8;     // 两位序号格式化缓冲区长度（字符）
const wchar_t* const kShotDirEnv       = L"CARD_SHOT_DIR";            // 输出目录的环境变量名
// 没设环境变量时的输出目录，相对测试程序所在目录：测试程序位于 <仓库>\flamingoclient\<配置>\Tests\，往上三级是仓库根目录，
// 再进 tmp\cardshot（开发约定：临时文件只写仓库的 tmp\ 目录）
const wchar_t* const kShotDefaultDir   = L"..\\..\\..\\tmp\\cardshot";
const wchar_t* const kShotFixtureSub   = L"fixtures";                 // 输出目录下放示例图片的子目录名
const wchar_t* const kShotFilePrefix   = L"cardshot_";                // CARDSHOT1 图片文件名前缀
const wchar_t* const kShotManifestName = L"cardshot_manifest.txt";    // CARDSHOT1 清单文件名
const wchar_t* const kBubbleFilePrefix   = L"bubble_";                // CARDSHOT2 单项特写文件名前缀
const wchar_t* const kBubbleManifestName = L"bubble_manifest.txt";    // CARDSHOT2 清单文件名
const wchar_t* const kBubbleSingleConvName = L"bubble_chat_single.png";   // CARDSHOT2 单聊整段会话图的文件名
const wchar_t* const kBubbleGroupConvName  = L"bubble_chat_group.png";    // CARDSHOT2 群聊整段会话图的文件名
const wchar_t* const kBubblePhotoName      = L"bubble_photo.png";         // 图片气泡用的示例照片文件名（放在 fixtures 子目录）

// ---- 样例数据 ------------------------------------------------------------

const long long kShotMsgidBase = 990001;   // 样例消息的服务器 msgid 起始值（各张图依次加 1）
// 接龙截止时间比当前时间晚多少（毫秒）：52 小时，状态行写「截止 月-日 时:分」，接龙仍在进行中、显示「参与接龙」
const long long kShotRelayDeadlineLeadMs = 52LL * 3600LL * 1000LL;
const long long kShotMsPerSecond = 1000;   // 秒与毫秒的换算
const int kShotFileDownloadPct  = 46;      // 「下载中」文件卡片的进度（百分比）
const int kShotFolderPct        = 62;      // 「传输中」文件夹气泡的进度（百分比）
const int kShotPercentBase      = 100;     // 百分比的基数：已传字节数 = 总字节数 × 进度 / 100
const int kShotFolderCurIndex   = 79;      // 「传输中」文件夹气泡当前正在传第几个文件
const int kShotFolderFileCount  = 128;     // 样例文件夹里的文件总数
const int kShotFolderDirCount   = 12;      // 样例文件夹里的子目录数
const long long kShotFolderBytes = 2576980378LL;   // 样例文件夹总字节数（约 2.4 GB）
const long long kShotPptBytes    = 4404019LL;      // 「2026 年第四季度规划.pptx」的字节数（约 4.2 MB）
const long long kShotPdfBytes    = 2202009LL;      // 「客户访谈纪要.pdf」的字节数（约 2.1 MB）
const long long kShotDocBytes    = 1363148LL;      // 「产品需求文档 v3.2.docx」的字节数（约 1.3 MB）
const int kShotGroupCallJoined   = 4;              // 进行中的群通话里已加入的人数
const uint64_t kShotSecretSessionA = 7301;         // 被邀请方那张密聊邀请卡片的会话号
const uint64_t kShotSecretSessionB = 7302;         // 发起方那张密聊邀请卡片的会话号
const uint32_t kShotSecretRemainA  = 58;           // 被邀请方卡片上的剩余秒数
const uint32_t kShotSecretRemainB  = 45;           // 发起方卡片上的剩余秒数
const int kShotSecretProtectOk     = 1;            // 对方的防截图级别：大于 0 表示能防截图，说明行写常规说明
const int kShotVoiceSeconds        = 12;           // 语音消息时长（秒）
const long long kShotVoiceBytes    = 19200;        // 语音文件字节数

// ---- CARDSHOT2 气泡样例 ----------------------------------------------------

const long long kBubbleMsgidBase       = 995001;   // 气泡样例的服务器 msgid 起始值（逐条加 1，与卡片样例的号段错开）
const long long kShotMsPerMinute       = 60LL * 1000LL;   // 分钟与毫秒的换算
const int kBubbleConvStartMinutesAgo   = 30;       // 整段会话第一条消息比当前时间早多少分钟（开头那行时间分隔按它显示）
const int kBubbleConvStepMinutes       = 1;        // 整段会话相邻两条消息相隔的分钟数；小于 5 分钟，中间不再插时间分隔
const int kBubbleSeparatorMinutesAgo   = 10;       // 「时间分隔」特写显示的时刻比当前时间早多少分钟
const int kBubbleVoiceSeconds          = 8;        // 群聊里本人发出的语音时长（秒）
const long long kBubbleVoiceBytes      = 12800;    // 群聊里本人发出的语音文件字节数
const DWORD kBubbleDecodeTimeoutMs     = 5000;     // 等示例照片在后台解码完成的最长时间（毫秒）
const DWORD kBubbleDecodePollMs        = 20;       // 等解码时每次查询的间隔（毫秒）

// 示例照片：480×320 的「渐变天空 + 太阳 + 两道山丘」，图片气泡按 240×160 显示（与真实收图时按原图比例缩放后的尺寸同一量级）
const int kShotPhotoW     = 480;   // 示例照片宽（像素）
const int kShotPhotoH     = 320;   // 示例照片高（像素）
const int kShotPhotoDispW = 240;   // 图片气泡里的显示宽（像素）
const int kShotPhotoDispH = 160;   // 图片气泡里的显示高（像素）
const int kShotPhotoSunX  = 330;   // 太阳外接正方形左上角 x（像素）
const int kShotPhotoSunY  = 48;    // 太阳外接正方形左上角 y（像素）
const int kShotPhotoSunD  = 72;    // 太阳直径（像素）
const Gdiplus::Color kShotPhotoSkyTop(255, 0x7F, 0xB8, 0xF0);      // 天空顶部颜色（浅蓝）
const Gdiplus::Color kShotPhotoSkyBottom(255, 0xFF, 0xE2, 0xBC);   // 天空底部颜色（暖杏色）
const Gdiplus::Color kShotPhotoSun(255, 0xFF, 0xB3, 0x47);         // 太阳颜色
const Gdiplus::Color kShotPhotoHillFar(255, 0x7F, 0xB0, 0x86);     // 远处山丘颜色
const Gdiplus::Color kShotPhotoHillNear(255, 0x3E, 0x8A, 0x5C);    // 近处山丘颜色
// 远处山丘的轮廓顶点（像素，480×320 画布坐标，按顺时针排列，最后两点落在画布底边）
const Gdiplus::Point kShotPhotoHillFarPts[] =
{
    Gdiplus::Point(0, 210), Gdiplus::Point(90, 160), Gdiplus::Point(190, 190), Gdiplus::Point(300, 140),
    Gdiplus::Point(400, 175), Gdiplus::Point(480, 150), Gdiplus::Point(480, 320), Gdiplus::Point(0, 320),
};
// 近处山丘的轮廓顶点（像素，同上）
const Gdiplus::Point kShotPhotoHillNearPts[] =
{
    Gdiplus::Point(0, 260), Gdiplus::Point(120, 225), Gdiplus::Point(250, 255), Gdiplus::Point(370, 220),
    Gdiplus::Point(480, 245), Gdiplus::Point(480, 320), Gdiplus::Point(0, 320),
};

// 语音波形：40 个音量值（0~255），模拟一句话的起伏
const uint8_t kShotVoiceLevels[net::kVoiceWaveBars] =
{
    30, 60, 110, 160, 140, 90, 70, 120, 190, 230,
    210, 150, 100, 80, 130, 180, 220, 200, 140, 90,
    60, 50, 90, 150, 200, 240, 210, 160, 110, 80,
    70, 100, 140, 120, 90, 60, 40, 30, 20, 15,
};

// 样例里出现的人
struct ShotPerson
{
    const wchar_t* m_name;    // 显示名
    const wchar_t* m_glyph;   // 头像里的字
    int64_t        m_uid;     // userid，头像底色按它从调色板里取
};

// kShotPeople 的下标
enum ShotPersonIndex
{
    kShotMe    = 0,   // 本人「林知夏」：本人发出的卡片画在右侧
    kShotZhang = 1,   // 「张三」：单聊的对方，以及群聊里的成员
    kShotLi    = 2,   // 「李晓雯」：群聊成员
    kShotWang  = 3,   // 「王建国」：群聊成员
    kShotZhao  = 4,   // 「赵敏」：群聊成员
};

const ShotPerson kShotPeople[] =
{
    { L"林知夏", L"林", 100001 },
    { L"张三",   L"张", 100002 },
    { L"李晓雯", L"李", 100003 },
    { L"王建国", L"王", 100004 },
    { L"赵敏",   L"赵", 100005 },
};

// 一张要出的图
struct CardShot
{
    std::wstring m_name;         // 英文短名，拼进文件名
    std::wstring m_caption;      // 中文说明，写进清单
    ChatMessage  m_msg;          // 要画的那一条消息
    bool         m_singleChat;   // 是否单聊：单聊开启已读回执（与单聊窗相同），群聊不开
};

// 在作用域内启动 GDI+，离开时关闭。气泡底色、头像与图标都用 GDI+ 绘制，PNG 也靠它编码。
class ShotGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    ShotGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~ShotGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 在作用域内初始化 COM（单线程套间），离开时对称释放。文件共享卡片标题前的系统类型图标经 SHGetFileInfo 取得，
// 该函数要求调用线程已初始化 COM。
class ShotComInit
{
public:
    // 初始化 COM；已被别的代码初始化过（返回 S_FALSE）也算成功，同样要对称释放。
    ShotComInit()
    {
        m_inited = SUCCEEDED(::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    }

    // 释放本对象那一次初始化。
    ~ShotComInit()
    {
        if (m_inited)
        {
            ::CoUninitialize();
        }
    }

private:
    bool m_inited;   // 本对象的 CoInitializeEx 是否成功（成功时析构要配对调用 CoUninitialize）
};

// 不显示的宿主窗口，析构时自动销毁。列表与气泡排版、绘制时要用到宿主。
class ShotHiddenHost
{
public:
    // 创建宿主窗口（WS_POPUP、不带 WS_VISIBLE，屏幕上不会出现窗口）。
    ShotHiddenHost()
    {
        RECT rc = { 0, 0, kShotListWidth, kShotCanvasHeight };
        m_host.Create(NULL, rc, _T("ChatCardShotTests"), WS_POPUP);
    }

    // 销毁宿主窗口。
    ~ShotHiddenHost()
    {
        if (m_host.IsWindow())
        {
            m_host.DestroyWindow();
        }
    }

    // 返回宿主对象（由本类持有，调用方借用）。
    DuiHost& Host()
    {
        return m_host;
    }

private:
    DuiHost m_host;   // 宿主对象，生命周期与本类相同
};

// 在作用域内把 Markdown 函数表换成编进测试程序的那一份（等同于 FlamingoMarkdown.dll 已加载），析构时恢复。
// 必须比所有含 Markdown 消息的列表活得更久：气泡里解析好的文档引用着这张函数表。
class ShotMarkdownOn
{
public:
    // 接上测试程序里的 Markdown 函数表。
    ShotMarkdownOn()
    {
        markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable());
    }

    // 恢复为宿主的函数表。
    ~ShotMarkdownOn()
    {
        markdown::SetMarkdownApiOverrideForTest(false, NULL);
    }
};

// 接龙卡片的名字解析：按样例人物表把 userid 换成显示名，与聊天窗按群名片、通讯录解析的结果形式相同。
class ShotRelayNames : public IRelayNameResolver
{
public:
    // 取 userid 对应的显示名；不在样例人物表里时返回空串（卡片上写成「用户<id>」）。
    virtual CString ResolveName(int64_t userid) const
    {
        const size_t count = sizeof(kShotPeople) / sizeof(kShotPeople[0]);
        for (size_t i = 0; i < count; ++i)
        {
            if (kShotPeople[i].m_uid == userid)
            {
                return CString(kShotPeople[i].m_name);
            }
        }
        return CString();
    }
};

// 32 位、自上而下的离屏画布：DIB 加选入它的内存 DC，析构时释放。
class ShotCanvas
{
public:
    // 建一张 width × height 像素的画布并铺上 kShotCanvasFill；失败时 Ok() 返回 false。
    ShotCanvas(int width, int height)
        : m_dc(NULL)
        , m_bmp(NULL)
        , m_oldBmp(NULL)
        , m_bits(NULL)
        , m_width(width)
        , m_height(height)
    {
        HDC screen = ::GetDC(NULL);
        if (screen == NULL)
        {
            return;
        }
        m_dc = ::CreateCompatibleDC(screen);
        ::ReleaseDC(NULL, screen);
        if (m_dc == NULL)
        {
            return;
        }
        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = width;
        bi.bmiHeader.biHeight      = -height;   // 负高表示自上而下存放，第 0 行在内存最前面
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        m_bmp = ::CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &m_bits, NULL, 0);
        if (m_bmp == NULL || m_bits == NULL)
        {
            return;
        }
        m_oldBmp = ::SelectObject(m_dc, m_bmp);
        RECT all = { 0, 0, width, height };
        HBRUSH fill = ::CreateSolidBrush(kShotCanvasFill);
        ::FillRect(m_dc, &all, fill);
        ::DeleteObject(fill);
    }

    // 还原内存 DC 原来的位图，释放 DIB 与 DC。
    ~ShotCanvas()
    {
        if (m_dc != NULL && m_oldBmp != NULL)
        {
            ::SelectObject(m_dc, m_oldBmp);
        }
        if (m_bmp != NULL)
        {
            ::DeleteObject(m_bmp);
        }
        if (m_dc != NULL)
        {
            ::DeleteDC(m_dc);
        }
    }

    // 画布是否建成。
    bool Ok() const
    {
        return m_dc != NULL && m_bmp != NULL && m_bits != NULL && m_oldBmp != NULL;
    }

    // 选好画布位图的内存 DC（由本对象持有）。
    HDC Dc() const
    {
        return m_dc;
    }

    /**
     *  把画布第 top 行起、高 height 行的一条（整宽）存成 PNG。
     *  @param top    起始行（像素，含），须在画布内
     *  @param height 行数（像素），top + height 不得超过画布高
     *  @param png    PNG 编码器的 CLSID
     *  @param path   输出文件完整路径；已存在时覆盖
     *  @return 保存成功返回 true
     */
    bool SaveRows(int top, int height, const CLSID& png, const std::wstring& path) const
    {
        if (!Ok() || top < 0 || height <= 0 || top + height > m_height)
        {
            return false;
        }
        const int stride = m_width * kShotBytesPerPx;
        BYTE* scan0 = static_cast<BYTE*>(m_bits) + static_cast<size_t>(top) * static_cast<size_t>(stride);
        //PixelFormat32bppRGB 忽略第四个字节：GDI 文字会把那个字节写成 0，按带透明通道解读的话文字处会变透明
        Gdiplus::Bitmap image(m_width, height, stride, PixelFormat32bppRGB, scan0);
        return image.Save(path.c_str(), &png, NULL) == Gdiplus::Ok;
    }

private:
    HDC     m_dc;       // 内存 DC
    HBITMAP m_bmp;      // DIB 位图
    HGDIOBJ m_oldBmp;   // 选入 DIB 之前内存 DC 里的位图，析构时选回
    void*   m_bits;     // DIB 像素首地址（BGRA，自上而下），由 m_bmp 持有
    int     m_width;    // 画布宽（像素）
    int     m_height;   // 画布高（像素）
};

// 取 PNG 编码器的 CLSID；系统里找不到时返回 false。
bool ShotPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
    {
        return false;
    }
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// 逐级创建目录（已存在的层级跳过）。返回目录最终是否存在。
bool ShotEnsureDirectory(const std::wstring& dir)
{
    size_t pos = 0;
    for (;;)
    {
        pos = dir.find_first_of(L"\\/", pos + 1);
        const std::wstring part = dir.substr(0, pos);
        //「C:」这种只有盘符的前缀不是要建的目录
        if (!part.empty() && part[part.size() - 1] != L':')
        {
            ::CreateDirectoryW(part.c_str(), NULL);
        }
        if (pos == std::wstring::npos)
        {
            break;
        }
    }
    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// 输出目录：环境变量 CARD_SHOT_DIR；没设时为 <仓库>\tmp\cardshot（由测试程序所在目录推算，见 kShotDefaultDir）。
// 返回值不带结尾的反斜杠。
std::wstring ShotOutputDir()
{
    wchar_t dir[kShotDirChars] = { 0 };
    size_t len = 0;
    std::wstring out;
    if (_wgetenv_s(&len, dir, kShotDirChars, kShotDirEnv) == 0 && len > 0)
    {
        out = dir;
    }
    else
    {
        wchar_t exePath[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring relative = exePath;
        const size_t slash = relative.find_last_of(L"\\/");
        relative = (slash == std::wstring::npos) ? std::wstring() : relative.substr(0, slash + 1);
        relative += kShotDefaultDir;
        //把「..」折算掉，得到规范的绝对路径；折算失败时就用未折算的路径（Windows 同样认得）
        wchar_t full[MAX_PATH] = { 0 };
        const DWORD n = ::GetFullPathNameW(relative.c_str(), MAX_PATH, full, NULL);
        out = (n > 0 && n < MAX_PATH) ? std::wstring(full) : relative;
    }
    while (!out.empty() && (out[out.size() - 1] == L'\\' || out[out.size() - 1] == L'/'))
    {
        out.erase(out.size() - 1);
    }
    return out;
}

// UTF-16 转 UTF-8（写清单、拼系统消息正文、在 UTF-8 正文里找 @ 片段的字节位置时用）。
std::string ShotToUtf8(const std::wstring& s)
{
    if (s.empty())
    {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, NULL, NULL);
    return out;
}

// 给消息填上发送者的头像字、头像底色与 userid；群聊里对方发来的再填发送者名（气泡据此在上方画名字）。
//   m：    [入出参] 消息，fromMe 须已填好
//   who：  发送者在 kShotPeople 里的下标
//   group：是否群聊
void ShotFillSender(ChatMessage& m, int who, bool group)
{
    const ShotPerson& p = kShotPeople[who];
    m.senderAvatarText  = p.m_glyph;
    m.senderAvatarColor = balloonmain::AvatarColorForUser(p.m_uid);
    m.senderUserId      = p.m_uid;
    if (group && !m.fromMe)
    {
        m.senderName = p.m_name;
    }
}

// 造一条卡片的底子：与收发路径一样先按普通文字气泡构造，再由各卡片的 ApplyXxxCard 改成卡片。
//   fromMe：是否本人发出
//   who：   发送者在 kShotPeople 里的下标
//   group： 是否群聊
//   msgid： 服务器 msgid
ChatMessage ShotBase(bool fromMe, int who, bool group, long long msgid)
{
    ChatMessage m = MakeTextMessage(fromMe, CString());
    m.msgid = msgid;
    ShotFillSender(m, who, group);
    return m;
}

// 往图表里加一张。
void ShotAdd(std::vector<CardShot>& shots, const wchar_t* name, const wchar_t* caption, const ChatMessage& msg,
             bool singleChat)
{
    CardShot shot;
    shot.m_name       = name;
    shot.m_caption    = caption;
    shot.m_msg        = msg;
    shot.m_singleChat = singleChat;
    shots.push_back(shot);
}

// 造一条文件夹传输气泡（与 BalloonChatWindow 收发文件夹时逐字段填写的做法相同）。
//   fromMe：本人发起（右侧）还是对方发来的邀请（左侧）
//   state： 传输状态，取值见 ChatFolderState
//   pct：   整体进度 0~100（没有进度条的状态传 0）
ChatMessage ShotFolder(bool fromMe, int state, int pct)
{
    ChatMessage m;
    m.kind             = kMsgFolder;
    m.fromMe           = fromMe;
    m.folderName       = L"设计稿归档 2026Q4";
    m.folderFileCount  = kShotFolderFileCount;
    m.folderDirCount   = kShotFolderDirCount;
    m.folderSizeBytes  = kShotFolderBytes;
    m.folderSize       = CString(FormatFileSize(kShotFolderBytes).c_str());
    m.folderState      = state;
    m.folderProgress   = pct;
    m.folderTotalBytes = kShotFolderBytes;
    m.folderSentBytes  = kShotFolderBytes * pct / kShotPercentBase;
    m.folderSessionId  = fromMe ? "fd-session-out-01" : "fd-session-in-01";
    ShotFillSender(m, fromMe ? kShotMe : kShotZhang, false);
    return m;
}

// 生成全部要出的图，顺序即文件名里的序号。
//   nowMs：本机当前时间（Unix 毫秒），接龙截止时间据它往后推
std::vector<CardShot> BuildCardShots(long long nowMs)
{
    std::vector<CardShot> shots;
    long long msgid = kShotMsgidBase;
    const bool kGroup = true;     // 群聊
    const bool kSingle = false;   // 单聊（ShotBase 的 group 参数取 false）

    // ---- 文件卡片 ----
    {
        ChatMessage m = MakeIncomingFileMessage(false, L"2026 年第四季度规划.pptx", kShotPptBytes, msgid,
                                                "shot-fileid-01", "shot-md5-01", kFilePendingAccept);
        ShotFillSender(m, kShotZhang, kSingle);
        ShotAdd(shots, L"file_pending", L"文件卡片：单聊，对方发来、待接收（接收 / 拒绝）", m, true);
        ++msgid;
    }
    {
        ChatMessage m = MakeIncomingFileMessage(false, L"客户访谈纪要.pdf", kShotPdfBytes, msgid,
                                                "shot-fileid-02", "shot-md5-02", kFileDownloading);
        m.fileProgress = kShotFileDownloadPct;
        ShotFillSender(m, kShotZhang, kSingle);
        ShotAdd(shots, L"file_downloading", L"文件卡片：单聊，对方发来、下载中 46%", m, true);
        ++msgid;
    }
    {
        ChatMessage m = MakeFileMessage(true, L"产品需求文档 v3.2.docx", kShotDocBytes,
                                        L"D:\\工作\\产品需求文档 v3.2.docx", "shot-cmid-03", kFileNormal);
        m.msgid = msgid;
        m.fileMsgid = msgid;
        m.filePeerState = kFilePeerReceived;
        m.read = true;
        ShotFillSender(m, kShotMe, kSingle);
        ShotAdd(shots, L"file_sent_done", L"文件卡片：单聊，本人发出、已完成且对方已接收（下方带「已读」回执）", m, true);
        ++msgid;
    }

    // ---- 文件夹传输 ----
    {
        ChatMessage m = ShotFolder(false, kFolderInviting, 0);
        ShotAdd(shots, L"folder_invite", L"文件夹传输：单聊，对方邀请、待接收（接收 / 另存为 / 拒绝）", m, true);
    }
    {
        ChatMessage m = ShotFolder(true, kFolderTransferring, kShotFolderPct);
        m.folderCurFile  = L"首页-高保真.fig";
        m.folderCurIndex = kShotFolderCurIndex;
        ShotAdd(shots, L"folder_transferring", L"文件夹传输：单聊，本人发送、传输中 62%（第 79 / 共 128 个）", m, true);
    }

    // ---- 合并转发 ----
    {
        ChatMessage m = ShotBase(false, kShotWang, kGroup, msgid);
        const std::string content =
            "{\"title\":\"产品研发部的聊天记录\",\"messages\":["
            "{\"msgid\":\"1\",\"from\":\"100002\",\"msgtype\":1,"
            "\"content\":{\"text\":\"周五下午的版本评审改到 3 点，大家留意一下\"},\"fromname\":\"张三\"},"
            "{\"msgid\":\"2\",\"from\":\"100003\",\"msgtype\":1,"
            "\"content\":{\"text\":\"收到，设计稿我今晚更新到共享盘\"},\"fromname\":\"李晓雯\"},"
            "{\"msgid\":\"3\",\"from\":\"100004\",\"msgtype\":2,\"content\":{\"fileid\":\"9\"},\"fromname\":\"王建国\"},"
            "{\"msgid\":\"4\",\"from\":\"100005\",\"msgtype\":1,"
            "\"content\":{\"text\":\"测试环境已经部署好了，地址发在群公告里\"},\"fromname\":\"赵敏\"}]}";
        ApplyMergedCard(m, content);
        ShotAdd(shots, L"merged_group", L"合并转发：群聊，王建国转发的群聊记录（4 条摘要）", m, false);
        ++msgid;
    }

    // ---- 接龙 ----
    {
        ChatMessage m = ShotBase(false, kShotLi, kGroup, msgid);
        const std::string content =
            "{\"title\":\"周五团建报名\",\"note\":\"地点：西湖边的农家乐，费用 AA。\\n请在周三中午前接龙，注明是否携带家属。\","
            "\"deadline\":" + std::to_string(nowMs + kShotRelayDeadlineLeadMs) + ","
            "\"items\":[{\"userid\":\"100002\",\"text\":\"本人+1\"},"
            "{\"userid\":\"100004\",\"text\":\"本人，不带家属\"},"
            "{\"userid\":\"100005\",\"text\":\"本人+2\"}],"
            "\"state\":0,\"closedby\":\"0\",\"closetime\":0}";
        ShotRelayNames names;
        ApplyRelayCard(m, content, kShotPeople[kShotMe].m_uid, false, nowMs, names);
        ShotAdd(shots, L"relay_join", L"接龙：群聊，李晓雯发起，有说明、3 条接龙、带「参与接龙」按钮", m, false);
        ++msgid;
    }

    // ---- 文件共享 ----
    {
        ChatMessage m = ShotBase(false, kShotLi, kGroup, msgid);
        const std::string content =
            "{\"shareid\":\"901\",\"ownerid\":\"100003\",\"kind\":2,\"name\":\"设计稿归档\",\"sharestate\":1}";
        ShareCardLiveState live;
        live.m_moduleReady = true;
        live.m_hasState    = true;
        live.m_state       = share_state_available;
        ApplyFileShareCard(m, content, kShotPeople[kShotLi].m_name, live);
        ShotAdd(shots, L"fileshare_folder", L"文件共享：群聊，李晓雯共享的文件夹、可用，带「打开」按钮", m, false);
        ++msgid;
    }

    // ---- 屏幕共享 ----
    {
        ChatMessage m = ShotBase(false, kShotLi, kGroup, msgid);
        const std::string content =
            "{\"roomid\":\"ss-20261014-01\",\"host\":\"100003\",\"hostname\":\"李晓雯\",\"locked\":0,\"forced\":0,"
            "\"title\":\"登录页改版效果演示\",\"desc\":\"本周改版后的登录页和注册页，欢迎大家边看边提意见，会后整理成需求单\"}";
        ApplyScreenShareCard(m, content, kShotPeople[kShotLi].m_name, kScreenShareRoomActive);
        ShotAdd(shots, L"screenshare_active", L"屏幕共享：群聊，李晓雯发起、正在共享，有标题与描述，带「加入」按钮", m,
                false);
        ++msgid;
    }
    {
        ChatMessage m = ShotBase(false, kShotZhang, kSingle, msgid);
        const std::string content =
            "{\"roomid\":\"ss-20261013-07\",\"host\":\"100002\",\"hostname\":\"张三\",\"title\":\"九月数据复盘\"}";
        ApplyScreenShareCard(m, content, kShotPeople[kShotZhang].m_name, kScreenShareRoomEnded);
        ShotAdd(shots, L"screenshare_ended", L"屏幕共享：单聊，张三发起、共享已结束（「加入」置灰）", m, true);
        ++msgid;
    }

    // ---- 单聊通话 ----
    {
        ChatMessage m = ShotBase(false, kShotZhang, kSingle, msgid);
        const std::string content =
            "{\"kind\":\"call\",\"sessionid\":\"call-01\",\"media\":1,\"caller\":\"100002\",\"callee\":\"100001\","
            "\"callername\":\"张三\",\"result\":7,\"duration\":332}";
        ApplyCallCard(m, content, kShotPeople[kShotZhang].m_name, 0);
        ShotAdd(shots, L"call_voice_ended", L"单聊通话：张三发起的语音通话，已结束，通话时长 05:32", m, true);
        ++msgid;
    }
    {
        ChatMessage m = ShotBase(true, kShotMe, kSingle, msgid);
        m.read = true;
        const std::string content =
            "{\"kind\":\"call\",\"sessionid\":\"call-02\",\"media\":2,\"caller\":\"100001\",\"callee\":\"100002\","
            "\"callername\":\"林知夏\",\"result\":2}";
        ApplyCallCard(m, content, kShotPeople[kShotZhang].m_name, 0);
        ShotAdd(shots, L"call_video_rejected", L"单聊通话：本人发起的视频通话，未接通（对方已拒绝）", m, true);
        ++msgid;
    }

    // ---- 群通话 ----
    {
        ChatMessage m = ShotBase(false, kShotWang, kGroup, msgid);
        const std::string content =
            "{\"kind\":\"groupcall\",\"roomid\":\"gc-20261014-02\",\"media\":2,\"initiator\":\"100004\","
            "\"initiatorname\":\"王建国\"}";
        ApplyGroupCallCard(m, content, kShotPeople[kShotWang].m_name, balloongroupcall::kGroupCallRoomActive,
                           kShotGroupCallJoined);
        ShotAdd(shots, L"groupcall_active", L"群通话：群聊，王建国发起的群视频通话进行中（4 人），带「加入」按钮", m,
                false);
        ++msgid;
    }
    {
        ChatMessage m = ShotBase(false, kShotZhao, kGroup, msgid);
        const std::string content =
            "{\"kind\":\"groupcall\",\"roomid\":\"gc-20261013-05\",\"media\":1,\"initiator\":\"100005\","
            "\"initiatorname\":\"赵敏\",\"duration\":1520}";
        ApplyGroupCallCard(m, content, kShotPeople[kShotZhao].m_name, balloongroupcall::kGroupCallRoomEnded, 0);
        ShotAdd(shots, L"groupcall_ended", L"群通话：群聊，赵敏发起的群语音通话，已结束（时长 25:20，无按钮）", m, false);
        ++msgid;
    }

    // ---- 群公告、备忘、云端录像 ----
    {
        ChatMessage m = ShotBase(false, kShotWang, kGroup, msgid);
        const std::string content =
            "{\"announcementid\":\"501\",\"groupid\":\"3001\",\"title\":\"关于国庆假期值班安排的通知\","
            "\"body\":\"10 月 1 日至 7 日实行轮岗值班，请各位在共享表格中登记自己的值班日期，如有冲突请提前与部门负责人沟通。\","
            "\"severity\":1,\"publisherid\":\"100004\",\"publisher\":\"王建国\",\"publishtime\":\"1759190400000\"}";
        ApplyAnnouncementCard(m, content, kShotPeople[kShotWang].m_name);
        ShotAdd(shots, L"announcement", L"群公告：群聊，王建国发布，优先级「重要」，带「查看」按钮", m, false);
        ++msgid;
    }
    {
        ChatMessage m = ShotBase(false, kShotLi, kGroup, msgid);
        const std::string content =
            "{\"memoid\":\"701\",\"groupid\":\"3001\",\"change\":1,\"title\":\"第四季度规划评审会\","
            "\"note\":\"请提前准备各模块的进度汇报材料\",\"start\":\"2026-10-20 14:30\",\"advance\":15,\"repeat\":0,"
            "\"weekdays\":0,\"count\":1,\"targetall\":true,\"targetcount\":0,\"editorid\":\"100003\",\"editor\":\"李晓雯\"}";
        ApplyMemoCard(m, content, kShotPeople[kShotLi].m_name, memo::NowLocalMinute());
        ShotAdd(shots, L"memo", L"备忘：群聊，李晓雯新建的群备忘（提醒全体成员），带「查看」按钮", m, false);
        ++msgid;
    }
    {
        ChatMessage m = ShotBase(false, kShotZhang, kGroup, msgid);
        const std::string content =
            "{\"recordid\":\"801\",\"owner\":\"100002\",\"title\":\"产品评审会录像\",\"durationms\":2723000,"
            "\"bytes\":356515840,\"audioonly\":0}";
        ApplyCloudRecordCard(m, content, kShotPeople[kShotZhang].m_name);
        ShotAdd(shots, L"cloudrecord", L"云端录像：群聊，张三分享的录像（时长 45:23 · 340.0 MB），带「播放」按钮", m, false);
        ++msgid;
    }

    // ---- 入群邀请 ----
    {
        ChatMessage m = ShotBase(false, kShotZhang, kSingle, msgid);
        const std::string content =
            "{\"inviteid\":\"1001\",\"groupid\":\"3002\",\"groupname\":\"产品研发部周会群\",\"inviter\":\"张三\","
            "\"invitee\":\"林知夏\",\"state\":0,\"membercount\":32}";
        ApplyGroupInviteCard(m, content);
        ShotAdd(shots, L"group_invite", L"入群邀请：单聊，被邀请方、待加入，带「加入群聊」按钮", m, true);
        ++msgid;
    }

    // ---- 密聊邀请 ----
    {
        ChatMessage m;
        ApplySecretInviteCard(m, false, kShotPeople[kShotZhang].m_name, kShotSecretSessionA, kShotSecretRemainA,
                              kShotSecretProtectOk);
        ShotFillSender(m, kShotZhang, kSingle);
        ShotAdd(shots, L"secret_invitee", L"密聊邀请：单聊，被邀请方，带「同意」「忽略」两个按钮", m, true);
    }
    {
        ChatMessage m;
        ApplySecretInviteCard(m, true, kShotPeople[kShotZhang].m_name, kShotSecretSessionB, kShotSecretRemainB,
                              kShotSecretProtectOk);
        ShotFillSender(m, kShotMe, kSingle);
        ShotAdd(shots, L"secret_inviter", L"密聊邀请：单聊，发起方，带「取消」按钮", m, true);
    }

    // ---- 语音消息 ----
    {
        ChatMessage m = ShotBase(false, kShotZhang, kSingle, msgid);
        const std::vector<uint8_t> levels(kShotVoiceLevels, kShotVoiceLevels + net::kVoiceWaveBars);
        ApplyVoiceMessage(m, "shot-voice-fileid", "shot-voice-md5", kShotVoiceSeconds, kShotVoiceBytes,
                          net::EncodeVoiceWave(levels), false);
        ShotAdd(shots, L"voice_unplayed", L"语音消息：单聊，张三发来、未听（气泡外侧红点），时长 12 秒", m, true);
        ++msgid;
    }

    // ---- Markdown 卡片与代码片段卡片 ----
    {
        ChatMessage m = ShotBase(false, kShotZhang, kSingle, msgid);
        m.text = L"# 第四季度迭代计划\n\n本季度重点完成**聊天卡片美化**与列表滚动性能优化，下面是各模块的负责人与时间点。\n\n"
                 L"- 卡片视觉统一：李晓雯，10 月 31 日\n- 滚动性能优化：王建国，11 月 15 日\n- 3.2 版本发布：张三，12 月 5 日";
        m.isMarkdown = true;
        ShotAdd(shots, L"markdown", L"Markdown 卡片：单聊，张三发来（标题 + 三行摘要 + 底部「Markdown 文档」）", m, true);
        ++msgid;
    }
    {
        ChatMessage m = ShotBase(false, kShotWang, kGroup, msgid);
        m.text = L"// 统计会话列表里的未读总数\n"
                 L"int CountUnread(const std::vector<Conversation>& list)\n"
                 L"{\n"
                 L"    int total = 0;\n"
                 L"    for (size_t i = 0; i < list.size(); ++i)\n"
                 L"        total += list[i].m_unread;\n"
                 L"    return total;\n"
                 L"}";
        m.isCode = true;
        m.codeLang = codesnippet::code_lang_cpp;
        ShotAdd(shots, L"code", L"代码片段卡片：群聊，王建国发来的 C++ 代码（8 行）", m, false);
        ++msgid;
    }
    return shots;
}

// =====================================================================
// 两个用例共用：排版、裁剪、存图、写清单
// =====================================================================

// 在列表里放一行占位：空的时间分隔行，不画任何东西，只把后面的消息行从列表顶端的 1 像素分隔线下面挪开。
void ShotAppendSpacer(ChatMessageList& list)
{
    ChatMessage spacer;
    spacer.kind = kMsgTime;
    list.AppendMessage(spacer);
}

// 把列表挂到宿主上、按单聊 / 群聊设好已读回执，再按 height 像素高排版（宽固定为 kShotListWidth）。
// 可以重复调用：撤回、发送失败、进入多选这类状态变化之后再调一次，即按新内容重排。
//   list：      要排版的列表
//   host：      宿主窗口（借用）
//   singleChat：是否单聊；单聊开启已读回执，群聊不开
//   height：    列表高（像素）
void ShotLayoutList(ChatMessageList& list, DuiHost& host, bool singleChat, int height)
{
    list.AttachToHost(&host);
    //单聊窗建好列表后开启已读回执，群聊窗不开（与 BalloonChatWindow / BalloonGroupChatWindow 相同）；已有的行随之改掉
    list.SetReadReceiptEnabled(singleChat);
    RECT rc = { 0, 0, kShotListWidth, height };
    list.ForceLayout(rc);
}

/**
 *  把按 kShotCanvasHeight 高排好版的列表画到画布上，裁出第 firstRow ~ lastRow 行（含）、上下各留 kShotMarginPx，存成 PNG。
 *  @param list     已排好版的列表（高须为 kShotCanvasHeight）
 *  @param firstRow 要裁出的第一行在列表里的下标
 *  @param lastRow  要裁出的最后一行在列表里的下标（含），不小于 firstRow
 *  @param png      PNG 编码器的 CLSID
 *  @param path     输出文件完整路径；已存在时覆盖
 *  @return 保存成功返回 true
 */
bool ShotSaveRows(ChatMessageList& list, size_t firstRow, size_t lastRow, const CLSID& png, const std::wstring& path)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(firstRow <= lastRow);
    REQUIRE(lastRow < kids.size());
    const int top = kids[firstRow]->GetRect().top - kShotMarginPx;
    const int bottom = kids[lastRow]->GetRect().bottom + kShotMarginPx;
    //列表在消息行区最底下一行画一条 1 像素分隔线（多选模式下行区还要让出底部的操作条），裁剪范围不能碰到它
    REQUIRE(top >= 0);
    REQUIRE(bottom < list.ViewHeight() - 1);

    ShotCanvas canvas(kShotListWidth, kShotCanvasHeight);
    REQUIRE(canvas.Ok());
    RECT all = { 0, 0, kShotListWidth, kShotCanvasHeight };
    list.OnPaint(canvas.Dc(), all);
    return canvas.SaveRows(top, bottom - top, png, path);
}

/**
 *  把整段会话按内容高度画出来存成 PNG，不裁剪：先按 kShotConvMaxHeight 排一次版量出内容总高，
 *  再按「内容总高 + kShotConvTailPx」重排，整张画布保存。顶端与底端各有列表自己画的 1 像素分隔线，与聊天窗消息区相同。
 *  @param list       已加好消息、做完状态变化的列表
 *  @param host       宿主窗口（借用）
 *  @param singleChat 是否单聊（决定已读回执）
 *  @param png        PNG 编码器的 CLSID
 *  @param path       输出文件完整路径；已存在时覆盖
 *  @return 保存成功返回 true
 */
bool ShotSaveWholeList(ChatMessageList& list, DuiHost& host, bool singleChat, const CLSID& png, const std::wstring& path)
{
    ShotLayoutList(list, host, singleChat, kShotConvMaxHeight);
    const int height = list.ContentHeight() + kShotConvTailPx;
    REQUIRE(height <= kShotConvMaxHeight);
    ShotLayoutList(list, host, singleChat, height);

    ShotCanvas canvas(kShotListWidth, height);
    REQUIRE(canvas.Ok());
    RECT all = { 0, 0, kShotListWidth, height };
    list.OnPaint(canvas.Dc(), all);
    return canvas.SaveRows(0, height, png, path);
}

// 把清单文字（UTF-8）写进文件，已存在时覆盖。返回是否全部写入。
bool ShotWriteManifest(const std::wstring& path, const std::string& text)
{
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == NULL)
    {
        return false;
    }
    const size_t written = fwrite(text.data(), 1, text.size(), fp);
    fclose(fp);
    return written == text.size();
}

// 往清单里加一行「文件名<TAB>中文说明」（UTF-8，行尾 CRLF）。
void ShotManifestLine(std::string& manifest, const std::wstring& fileName, const std::wstring& caption)
{
    manifest += ShotToUtf8(fileName + L"\t" + caption);
    manifest += "\r\n";
}

// 把序号（从 1 起）格式化成两位文字，如 1 → "01"。
std::wstring ShotIndexText(size_t index)
{
    wchar_t text[kShotIndexChars] = { 0 };
    swprintf_s(text, kShotIndexChars, L"%02u", static_cast<unsigned>(index));
    return std::wstring(text);
}

// =====================================================================
// CARDSHOT1：卡片
// =====================================================================

/**
 *  把一张卡片图要画的消息单独放进一个列表、排版、画到画布上，裁出卡片所在行存成 PNG。
 *  @param host 宿主窗口（借用）
 *  @param shot 要出的图
 *  @param png  PNG 编码器的 CLSID
 *  @param path 输出文件完整路径
 *  @return 保存成功返回 true
 */
bool RenderCardShot(DuiHost& host, const CardShot& shot, const CLSID& png, const std::wstring& path)
{
    ChatMessageList list;
    ShotAppendSpacer(list);
    list.AppendMessage(shot.m_msg);
    ShotLayoutList(list, host, shot.m_singleChat, kShotCanvasHeight);
    //本人刚发出、还在等回执的消息（时间戳为 0、带客户端消息 ID）前面，列表会按本机时间插一行时间（2026-10-14 起，
    //docs/聊天消息时间显示方案.md 第 7 节）；截图只要卡片那一行，取最后一行
    const size_t rows = list.Children().size();
    REQUIRE(rows >= kShotRowsPerList);
    const size_t cardRow = rows - 1;
    const ChatBubble* cardBubble = dynamic_cast<const ChatBubble*>(list.Children()[cardRow].get());
    REQUIRE(cardBubble != NULL);
    REQUIRE(cardBubble->GetMessage().kind != kMsgTime);
    return ShotSaveRows(list, cardRow, cardRow, png, path);
}

// =====================================================================
// CARDSHOT2：普通聊天气泡
// =====================================================================

// 单项特写在消息加进列表、排好版之后还要做的一步（真实界面里由推送或回执触发的状态变化）
enum BubbleShotAction
{
    kBubbleActionNone        = 0,   // 不做额外操作：消息加进列表就是要截的样子
    kBubbleActionRecall      = 1,   // 本人撤回最后一条：走 RecallMessageByMsgid，提示行末尾带「编辑」
    kBubbleActionSendFailed  = 2,   // 最后一条的发送被服务端拒绝：走 MarkSendFailed，原因取「包含敏感词」那一句
    kBubbleActionMultiSelect = 3,   // 以第一条为起点进入多选模式：第一条被勾选，其余未勾选
};

// 一张单项特写
struct BubbleShot
{
    std::wstring             m_name;         // 英文短名，拼进文件名
    std::wstring             m_caption;      // 中文说明，写进清单
    std::vector<ChatMessage> m_msgs;         // 依次加进列表的消息（不含占位行），都不带时间，列表不会自动插时间分隔
    bool                     m_singleChat;   // 是否单聊：单聊开启已读回执，群聊不开
    int                      m_action;       // 加进列表之后的状态变化，取值见 BubbleShotAction
};

// 造一条文字气泡：与收发路径一样用 MakeTextMessage，再填 msgid、消息时间与发送者。
//   fromMe：是否本人发出
//   who：   发送者在 kShotPeople 里的下标
//   group： 是否群聊（群聊里对方发来的才画发送者名）
//   text：  正文
//   msgid： 服务器 msgid；0 表示还没有（发送中或发送失败）
//   tsMs：  消息时间（Unix 毫秒）；0 表示不带时间，列表不会因它插时间分隔
ChatMessage ShotText(bool fromMe, int who, bool group, const wchar_t* text, long long msgid, long long tsMs)
{
    ChatMessage m = MakeTextMessage(fromMe, CString(text));
    m.msgid = msgid;
    m.timestampMs = tsMs;
    ShotFillSender(m, who, group);
    return m;
}

// 造一条「大 emoji」气泡（kMsgEmoji）。真实收发路径不产生这种消息（文字消息里的 emoji 一律按文字气泡画），
// 只有 ChatMockData 的模拟数据里有；这里照模拟数据的写法，把文字消息的 kind 改成 kMsgEmoji。参数同 ShotText。
ChatMessage ShotBigEmoji(bool fromMe, int who, bool group, long long msgid, long long tsMs)
{
    ChatMessage m = ShotText(fromMe, who, group, L"👍", msgid, tsMs);
    m.kind = kMsgEmoji;
    return m;
}

// 造一条图片气泡（MakeImageMessage），图片是 fixtures 里的示例照片，按 kShotPhotoDispW × kShotPhotoDispH 显示。
// 调用方须已等示例照片在 ChatImageCache 里解码好（ShotWaitImageDecoded）。其余参数同 ShotText。
ChatMessage ShotImage(bool fromMe, int who, bool group, const std::wstring& photo, long long msgid, long long tsMs)
{
    ChatMessage m = MakeImageMessage(fromMe, CString(photo.c_str()), kShotPhotoDispW, kShotPhotoDispH, std::string(),
                                     kChatImageStateReady);
    m.msgid = msgid;
    m.timestampMs = tsMs;
    ShotFillSender(m, who, group);
    return m;
}

// 造一条系统提示行：与收到服务端系统消息（msgtype=17）时相同，经 MakeSystemTipRow 构造。
//   text： 提示文字
//   msgid：系统消息的服务器 msgid
//   tsMs： 消息时间（Unix 毫秒）
ChatMessage ShotSystemTip(const wchar_t* text, long long msgid, long long tsMs)
{
    net::MessageNotify notify;
    notify.m_msgid = msgid;
    notify.m_msgtype = message_type_system;
    notify.m_text = ShotToUtf8(text);
    notify.m_timestamp = tsMs;
    return MakeSystemTipRow(notify);
}

// 给一条群消息加一个 @ 片段，走真实收消息的换算：在 UTF-8 正文里找到「@显示名」的字节区间（协议侧口径），
// 经 ConvertSpansForRender 换成渲染侧片段并按本人 userid 标好是不是 @我。按正文里出现的先后顺序调用。
//   m：  [入出参] 群消息，text 里须含有「@显示名」
//   who：被 @ 的人在 kShotPeople 里的下标
void ShotAddMention(ChatMessage& m, int who)
{
    const std::string utf8 = ShotToUtf8(std::wstring((LPCWSTR)m.text));
    const std::string target = ShotToUtf8(std::wstring(L"@") + kShotPeople[who].m_name);
    const size_t off = utf8.find(target);
    REQUIRE(off != std::string::npos);
    MentionSpan span;
    span.m_uid = kShotPeople[who].m_uid;
    span.m_off = static_cast<int>(off);
    span.m_len = static_cast<int>(target.size());
    const std::vector<MentionSpan> spans(1, span);
    const std::vector<RenderSpan> rendered = ConvertSpansForRender(utf8, spans, kShotPeople[kShotMe].m_uid);
    REQUIRE(rendered.size() == spans.size());
    m.ats.insert(m.ats.end(), rendered.begin(), rendered.end());
}

// 让 reply 引用 src，走真实收发路径：发送方按被引用的消息生成协议侧快照（BuildQuoteFromMessage），
// 收方再把快照填进气泡（FillQuoteForRender）。
//   reply： [入出参] 回复的那条消息
//   src：   被引用的消息，须已有服务器 msgid
//   srcWho：被引用消息的发送者在 kShotPeople 里的下标（快照里的显示名）
void ShotQuote(ChatMessage& reply, const ChatMessage& src, int srcWho)
{
    const net::ReplyQuote quote = BuildQuoteFromMessage(src, CString(kShotPeople[srcWho].m_name));
    REQUIRE(quote.m_valid);
    FillQuoteForRender(quote, reply);
}

// 本人撤回列表里 msgid 那一条：与收到本人撤回的推送时相同，提示行写「你撤回了一条消息」，末尾带 5 分钟内可点的「编辑」。
void ShotRecall(ChatMessageList& list, long long msgid, long long nowMs)
{
    REQUIRE(list.RecallMessageByMsgid(msgid, balloonchat::BuildRecallTipText(true, CString()), true, nowMs, NULL));
}

// 把 clientMsgId 那一条（还没有服务器 msgid 的本人消息）置为发送失败：与发送回执失败时相同，原因取「包含敏感词」那一句。
void ShotMarkFailed(ChatMessageList& list, const std::string& clientMsgId)
{
    REQUIRE(list.MarkSendFailed(clientMsgId, CString(SendFailureText(error_code_sensitive_word).c_str())));
}

// 对排好版的单项特写列表做 action 指定的状态变化。
//   list：  已加好消息并排过版的列表
//   action：取值见 BubbleShotAction
//   msgs：  加进列表的消息（与 BubbleShot::m_msgs 相同）
//   nowMs： 当前时间（Unix 毫秒），撤回时用
void ShotApplyAction(ChatMessageList& list, int action, const std::vector<ChatMessage>& msgs, long long nowMs)
{
    switch (action)
    {
    //本人撤回最后一条
    case kBubbleActionRecall:
        ShotRecall(list, msgs.back().msgid, nowMs);
        break;

    //最后一条的发送被服务端拒绝
    case kBubbleActionSendFailed:
        ShotMarkFailed(list, msgs.back().clientMsgId);
        break;

    //以第一条消息为起点进入多选模式，与右键「多选」相同
    case kBubbleActionMultiSelect:
    {
        ChatBubble* first = dynamic_cast<ChatBubble*>(list.Children()[kShotFirstMsgRow].get());
        REQUIRE(first != NULL);
        list.EnterMultiSelectWith(first);
        break;
    }

    //不做额外操作
    default:
        break;
    }
}

/**
 *  把一张单项特写的消息放进一个列表，排版、做完状态变化后重排，裁出这些消息所在的行存成 PNG。
 *  @param host  宿主窗口（借用）
 *  @param shot  要出的图
 *  @param nowMs 当前时间（Unix 毫秒）
 *  @param png   PNG 编码器的 CLSID
 *  @param path  输出文件完整路径
 *  @return 保存成功返回 true
 */
bool RenderBubbleShot(DuiHost& host, const BubbleShot& shot, long long nowMs, const CLSID& png, const std::wstring& path)
{
    REQUIRE(!shot.m_msgs.empty());
    ChatMessageList list;
    ShotAppendSpacer(list);
    for (size_t i = 0; i < shot.m_msgs.size(); ++i)
    {
        list.AppendMessage(shot.m_msgs[i]);
    }
    ShotLayoutList(list, host, shot.m_singleChat, kShotCanvasHeight);
    ShotApplyAction(list, shot.m_action, shot.m_msgs, nowMs);
    ShotLayoutList(list, host, shot.m_singleChat, kShotCanvasHeight);
    //消息都不带时间，但本人发出的回显（带客户端消息 ID）按本机当前时间判定，列表会在它前面自动插一行时间分隔
    //（docs/聊天消息时间显示方案.md 第 7 节，2026-10-14 起）。真实聊天窗里同样有这一行，特写照常截进去；
    //行数断言只数自动插入的时间分隔行以外的行：占位行加上各条消息
    const size_t rows = list.Children().size();
    size_t separators = 0;
    for (size_t i = kShotFirstMsgRow; i < rows; ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(list.Children()[i].get());
        if (b != NULL && b->GetMessage().isTimeSeparator)
        {
            ++separators;
        }
    }
    REQUIRE(rows - separators == shot.m_msgs.size() + kShotFirstMsgRow);
    return ShotSaveRows(list, kShotFirstMsgRow, rows - 1, png, path);
}

// 画一张示例照片（渐变天空、太阳、远近两道山丘）存成 PNG，供图片气泡显示。返回是否保存成功。
bool ShotMakePhoto(const std::wstring& path, const CLSID& png)
{
    Gdiplus::Bitmap photo(kShotPhotoW, kShotPhotoH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&photo);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::LinearGradientBrush sky(Gdiplus::Point(0, 0), Gdiplus::Point(0, kShotPhotoH), kShotPhotoSkyTop,
                                         kShotPhotoSkyBottom);
        g.FillRectangle(&sky, 0, 0, kShotPhotoW, kShotPhotoH);
        Gdiplus::SolidBrush sunBrush(kShotPhotoSun);
        g.FillEllipse(&sunBrush, kShotPhotoSunX, kShotPhotoSunY, kShotPhotoSunD, kShotPhotoSunD);
        Gdiplus::SolidBrush hillFarBrush(kShotPhotoHillFar);
        g.FillPolygon(&hillFarBrush, kShotPhotoHillFarPts,
                      static_cast<INT>(sizeof(kShotPhotoHillFarPts) / sizeof(kShotPhotoHillFarPts[0])));
        Gdiplus::SolidBrush hillNearBrush(kShotPhotoHillNear);
        g.FillPolygon(&hillNearBrush, kShotPhotoHillNearPts,
                      static_cast<INT>(sizeof(kShotPhotoHillNearPts) / sizeof(kShotPhotoHillNearPts[0])));
    }
    return photo.Save(path.c_str(), &png, NULL) == Gdiplus::Ok;
}

// 请 ChatImageCache 按图片气泡的显示尺寸解码 path，并等它在后台解码完成。图片气泡绘制时只从缓存取解码好的图，
// 取不到就画占位块，所以截图前必须先等到。解码成功返回 true；解码失败或超时返回 false。
bool ShotWaitImageDecoded(const std::wstring& path, int w, int h)
{
    ChatImageCache::Inst().Request(path, w, h);
    const DWORD start = ::GetTickCount();
    for (;;)
    {
        if (ChatImageCache::Inst().Get(path, w, h) != NULL)
        {
            return true;
        }
        if (ChatImageCache::Inst().IsFailed(path, w, h) || ::GetTickCount() - start >= kBubbleDecodeTimeoutMs)
        {
            return false;
        }
        ::Sleep(kBubbleDecodePollMs);
    }
}

// 整段会话里依次分配 msgid 与消息时间
struct ShotConvCursor
{
    long long m_msgid;   // 下一条消息的服务器 msgid
    long long m_tsMs;    // 下一条消息的时间（Unix 毫秒）
};

// 把游标往后挪一条：msgid 加 1，时间往后推 kBubbleConvStepMinutes 分钟。
void ShotAdvance(ShotConvCursor& cur)
{
    ++cur.m_msgid;
    cur.m_tsMs += kBubbleConvStepMinutes * kShotMsPerMinute;
}

// 单聊与群聊里共用的一段长文字（三四行）
const wchar_t* const kBubbleLongPeerText =
    L"这次 3.2 版本主要做聊天卡片的视觉统一，涉及文件、接龙、屏幕共享等十几种卡片。我按优先级把改动分成了三档："
    L"第一档月底前完成，第二档放到下个迭代，第三档视情况再定。评审时想请你重点看看第一档的范围是否合适。";

// 本人发的一段长文字（三行左右）
const wchar_t* const kBubbleLongSelfText =
    L"好的，我今晚过一遍。卡片部分我建议统一成同一个宽度，按钮统一放在分隔线下方，"
    L"这样文件、接龙和屏幕共享几类卡片看起来是一个体系。明天上午给大家发一张对比图。";

/**
 *  往列表里加单聊整段会话的消息，时间从 kBubbleConvStartMinutesAgo 分钟前起依次往后排（开头自动插一行时间分隔）。
 *  倒数两条要在排版后分别置为「发送失败」与「撤回」，它们的 clientMsgId 与 msgid 由出参带回。
 *  @param list        [入出参] 空列表
 *  @param nowMs       当前时间（Unix 毫秒）
 *  @param photo       示例照片路径（须已解码好）
 *  @param failedCmid  [出参] 要置为发送失败的那一条的 clientMsgId
 *  @param recallMsgid [出参] 要撤回的那一条的 msgid
 */
void ShotFillSingleConversation(ChatMessageList& list, long long nowMs, const std::wstring& photo,
                                std::string& failedCmid, long long& recallMsgid)
{
    const bool kGroupChat = false;   // 单聊：对方气泡上方不画发送者名
    ShotConvCursor cur;
    cur.m_msgid = kBubbleMsgidBase;
    cur.m_tsMs = nowMs - kBubbleConvStartMinutesAgo * kShotMsPerMinute;

    //对方两条连续短文字（第一条之前列表自动插入时间分隔）
    list.AppendMessage(ShotText(false, kShotZhang, kGroupChat, L"知夏，在吗？", cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);
    list.AppendMessage(ShotText(false, kShotZhang, kGroupChat, L"下午的版本评审想跟你先对一下", cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);

    //本人短文字，对方已读
    ChatMessage reply = ShotText(true, kShotMe, kGroupChat, L"在的，你说", cur.m_msgid, cur.m_tsMs);
    reply.read = true;
    list.AppendMessage(reply);
    ShotAdvance(cur);

    //对方一条三四行的长文字
    list.AppendMessage(ShotText(false, kShotZhang, kGroupChat, kBubbleLongPeerText, cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);

    //对方发来的图片
    list.AppendMessage(ShotImage(false, kShotZhang, kGroupChat, photo, cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);

    //本人一个大 emoji
    ChatMessage emoji = ShotBigEmoji(true, kShotMe, kGroupChat, cur.m_msgid, cur.m_tsMs);
    emoji.read = true;
    list.AppendMessage(emoji);
    ShotAdvance(cur);

    //本人一条带链接的文字
    ChatMessage link = ShotText(true, kShotMe, kGroupChat,
                                L"改动清单我放在这里了：https://docs.flamingoim.org/q4/card-restyle 你先看看",
                                cur.m_msgid, cur.m_tsMs);
    link.read = true;
    list.AppendMessage(link);
    ShotAdvance(cur);

    //对方引用本人上一条（带链接的那条）回复
    ChatMessage quoted = ShotText(false, kShotZhang, kGroupChat, L"好的，我这就去看，有问题直接在文档里批注", cur.m_msgid,
                                  cur.m_tsMs);
    ShotQuote(quoted, link, kShotMe);
    list.AppendMessage(quoted);
    ShotAdvance(cur);

    //本人一条发送失败的文字：发出时还没有服务器 msgid，只有 clientMsgId
    failedCmid = "shot-bubble-conv-failed";
    ChatMessage failed = ShotText(true, kShotMe, kGroupChat, L"客户那边的报价细节我也贴过来", 0, cur.m_tsMs);
    failed.clientMsgId = failedCmid;
    list.AppendMessage(failed);
    ShotAdvance(cur);

    //本人一条随后撤回的文字
    recallMsgid = cur.m_msgid;
    list.AppendMessage(ShotText(true, kShotMe, kGroupChat, L"评审改到四点了", cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);
}

/**
 *  往列表里加群聊整段会话的消息（四个发送者），时间从 kBubbleConvStartMinutesAgo 分钟前起依次往后排
 *  （开头自动插一行时间分隔）。
 *  @param list  [入出参] 空列表
 *  @param nowMs 当前时间（Unix 毫秒）
 */
void ShotFillGroupConversation(ChatMessageList& list, long long nowMs)
{
    const bool kGroupChat = true;   // 群聊：对方气泡上方画发送者名
    ShotConvCursor cur;
    cur.m_msgid = kBubbleMsgidBase;
    cur.m_tsMs = nowMs - kBubbleConvStartMinutesAgo * kShotMsPerMinute;

    //同一人连续两条
    list.AppendMessage(ShotText(false, kShotZhang, kGroupChat, L"大家下午好，3.2 版本的评审定在周五下午三点", cur.m_msgid,
                                cur.m_tsMs));
    ShotAdvance(cur);
    list.AppendMessage(ShotText(false, kShotZhang, kGroupChat, L"材料我已经放到群文件里了", cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);

    //系统提示
    list.AppendMessage(ShotSystemTip(L"赵敏 加入了群聊", cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);

    //一条 @我
    ChatMessage atMe = ShotText(false, kShotLi, kGroupChat, L"@林知夏 设计稿的终版你来确认一下？", cur.m_msgid, cur.m_tsMs);
    ShotAddMention(atMe, kShotMe);
    list.AppendMessage(atMe);
    ShotAdvance(cur);

    //本人一条长文字
    list.AppendMessage(ShotText(true, kShotMe, kGroupChat, kBubbleLongSelfText, cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);

    //一条 @其他人
    ChatMessage atOther = ShotText(false, kShotWang, kGroupChat, L"@李晓雯 图标那部分也一起出一版吧", cur.m_msgid,
                                   cur.m_tsMs);
    ShotAddMention(atOther, kShotLi);
    list.AppendMessage(atOther);
    ShotAdvance(cur);

    //一条被 Pin 的消息
    ChatMessage pinned = ShotText(false, kShotZhao, kGroupChat,
                                  L"测试环境地址：https://test.flamingoim.org ，账号密码见群公告", cur.m_msgid, cur.m_tsMs);
    pinned.pinned = true;
    list.AppendMessage(pinned);
    ShotAdvance(cur);

    //本人一条语音
    ChatMessage voice = ShotBase(true, kShotMe, kGroupChat, cur.m_msgid);
    const std::vector<uint8_t> levels(kShotVoiceLevels, kShotVoiceLevels + net::kVoiceWaveBars);
    ApplyVoiceMessage(voice, "shot-bubble-voice-fileid", "shot-bubble-voice-md5", kBubbleVoiceSeconds, kBubbleVoiceBytes,
                      net::EncodeVoiceWave(levels), true);
    voice.timestampMs = cur.m_tsMs;
    list.AppendMessage(voice);
    ShotAdvance(cur);

    //收尾的一条短文字
    list.AppendMessage(ShotText(false, kShotZhao, kGroupChat, L"收到，谢谢大家", cur.m_msgid, cur.m_tsMs));
    ShotAdvance(cur);
}

// 往特写表里加一张。
void BubbleAdd(std::vector<BubbleShot>& shots, const wchar_t* name, const wchar_t* caption,
               const std::vector<ChatMessage>& msgs, bool singleChat, int action)
{
    BubbleShot shot;
    shot.m_name       = name;
    shot.m_caption    = caption;
    shot.m_msgs       = msgs;
    shot.m_singleChat = singleChat;
    shot.m_action     = action;
    shots.push_back(shot);
}

// 往特写表里加一张只有一条消息、不做状态变化的图。
void BubbleAddOne(std::vector<BubbleShot>& shots, const wchar_t* name, const wchar_t* caption, const ChatMessage& msg,
                  bool singleChat)
{
    BubbleAdd(shots, name, caption, std::vector<ChatMessage>(1, msg), singleChat, kBubbleActionNone);
}

/**
 *  生成全部单项特写，顺序即文件名里的序号。消息都不带时间（列表不会自动插时间分隔）。
 *  @param nowMs 当前时间（Unix 毫秒），「时间分隔」特写按它算显示的时刻
 *  @param photo 示例照片路径（须已解码好）
 */
std::vector<BubbleShot> BuildBubbleShots(long long nowMs, const std::wstring& photo)
{
    std::vector<BubbleShot> shots;
    long long msgid = kBubbleMsgidBase;
    const bool kGroup = true;     // 群聊
    const bool kSingle = false;   // 单聊（ShotText 的 group 参数取 false）

    BubbleAddOne(shots, L"peer_text_single", L"对方短文字（单聊，不画发送者名）",
                 ShotText(false, kShotZhang, kSingle, L"好的，我下午三点前把方案发你", msgid, 0), true);
    ++msgid;
    BubbleAddOne(shots, L"peer_text_group", L"对方短文字（群聊，气泡上方画发送者名）",
                 ShotText(false, kShotLi, kGroup, L"收到，设计稿今晚更新到共享盘", msgid, 0), false);
    ++msgid;
    {
        ChatMessage m = ShotText(true, kShotMe, kSingle, L"辛苦了，周五见", msgid, 0);
        m.read = true;
        BubbleAddOne(shots, L"self_text_read", L"本人短文字（单聊，下方「已读」回执）", m, true);
        ++msgid;
    }
    BubbleAddOne(shots, L"peer_text_long", L"对方长文字（单聊，自动折成多行）",
                 ShotText(false, kShotZhang, kSingle, kBubbleLongPeerText, msgid, 0), true);
    ++msgid;
    BubbleAddOne(shots, L"self_text_long", L"本人长文字（单聊，自动折成多行，下方「未读」回执）",
                 ShotText(true, kShotMe, kSingle, kBubbleLongSelfText, msgid, 0), true);
    ++msgid;
    {
        ChatMessage m = ShotText(false, kShotZhang, kGroup, L"@林知夏 @王建国 评审改到周五下午三点，麻烦两位提前看一下材料",
                                 msgid, 0);
        ShotAddMention(m, kShotMe);
        ShotAddMention(m, kShotWang);
        BubbleAddOne(shots, L"mention", L"群聊 @ 高亮：「@林知夏」是 @我 的配色，「@王建国」是 @别人 的配色", m, false);
        ++msgid;
    }
    {
        ChatMessage m = ShotText(true, kShotMe, kSingle,
                                 L"改动清单在这里：https://docs.flamingoim.org/q4/card-restyle 有问题随时说", msgid, 0);
        m.read = true;
        BubbleAddOne(shots, L"link", L"本人带链接的文字（单聊，链接按链接色画）", m, true);
        ++msgid;
    }
    {
        ChatMessage src = ShotText(true, kShotMe, kSingle, L"评审材料我放在共享盘的「3.2 评审」文件夹里了", msgid, 0);
        ++msgid;
        ChatMessage m = ShotText(false, kShotZhang, kSingle, L"收到，我先看看第一档的范围", msgid, 0);
        ShotQuote(m, src, kShotMe);
        BubbleAddOne(shots, L"quote", L"引用回复（单聊，对方引用本人的一条消息）", m, true);
        ++msgid;
    }
    {
        ChatMessage m = ShotBigEmoji(true, kShotMe, kSingle, msgid, 0);
        m.read = true;
        BubbleAddOne(shots, L"big_emoji", L"大 emoji（kMsgEmoji，本人发出；真实收发路径不产生这种消息，仅模拟数据有）", m, true);
        ++msgid;
    }
    BubbleAddOne(shots, L"image", L"图片（单聊，对方发来，显示 240×160）",
                 ShotImage(false, kShotZhang, kSingle, photo, msgid, 0), true);
    ++msgid;
    {
        ChatMessage m = ShotText(true, kShotMe, kSingle, L"客户那边的报价细节我也贴过来", 0, 0);
        m.clientMsgId = "shot-bubble-item-failed";
        BubbleAdd(shots, L"send_failed", L"发送失败（单聊，左侧红色感叹号，下方写原因）",
                  std::vector<ChatMessage>(1, m), true, kBubbleActionSendFailed);
    }
    BubbleAdd(shots, L"recall", L"撤回提示（本人撤回，末尾带 5 分钟内可点的「编辑」）",
              std::vector<ChatMessage>(1, ShotText(true, kShotMe, kSingle, L"评审改到四点了", msgid, 0)), true,
              kBubbleActionRecall);
    ++msgid;
    BubbleAddOne(shots, L"system_tip", L"系统提示（群聊，「赵敏 加入了群聊」）",
                 ShotSystemTip(L"赵敏 加入了群聊", msgid, 0), false);
    ++msgid;
    {
        ChatMessage m;
        m.kind = kMsgTime;
        m.text = FormatChatSeparatorTime(nowMs - kBubbleSeparatorMinutesAgo * kShotMsPerMinute, nowMs);
        BubbleAddOne(shots, L"time_separator", L"时间分隔（今天的消息只写时:分）", m, true);
    }
    {
        ChatMessage m = ShotText(false, kShotZhang, kSingle, L"评审时间：周五下午三点，三楼大会议室", msgid, 0);
        m.pinned = true;
        BubbleAddOne(shots, L"pinned", L"被 Pin 的消息（单聊，气泡外侧上角画图钉）", m, true);
        ++msgid;
    }
    {
        std::vector<ChatMessage> msgs;
        msgs.push_back(ShotText(false, kShotZhang, kSingle, L"这两条一起转发给产品组吧", msgid, 0));
        ++msgid;
        ChatMessage mine = ShotText(true, kShotMe, kSingle, L"好的", msgid, 0);
        mine.read = true;
        msgs.push_back(mine);
        ++msgid;
        BubbleAdd(shots, L"multi_select", L"多选模式（单聊）：第一条已勾选、第二条未勾选（底部操作条不在裁剪范围内）", msgs, true,
                  kBubbleActionMultiSelect);
    }
    return shots;
}

} // anonymous namespace

TEST_CASE("CARDSHOT1 chat card screenshots before restyling", "[.cardshot]")
{
    ShotComInit com;
    ShotGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    CLSID png;
    REQUIRE(ShotPngEncoder(png));
    ShotHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    //Markdown 函数表要比下面每个列表都活得久，故最先接上
    ShotMarkdownOn markdownOn;

    const std::wstring dir = ShotOutputDir();
    REQUIRE(ShotEnsureDirectory(dir));

    const long long nowMs = static_cast<long long>(::time(NULL)) * kShotMsPerSecond;
    const std::vector<CardShot> shots = BuildCardShots(nowMs);

    std::string manifest;
    for (size_t i = 0; i < shots.size(); ++i)
    {
        const std::wstring fileName = std::wstring(kShotFilePrefix) + ShotIndexText(i + 1) + L"_" + shots[i].m_name + L".png";
        INFO("card shot: " << ShotToUtf8(fileName));
        REQUIRE(RenderCardShot(host.Host(), shots[i], png, dir + L"\\" + fileName));
        ShotManifestLine(manifest, fileName, shots[i].m_caption);
    }
    REQUIRE(ShotWriteManifest(dir + L"\\" + kShotManifestName, manifest));
}

TEST_CASE("CARDSHOT2 chat bubble screenshots before restyling", "[.cardshot]")
{
    ShotComInit com;
    ShotGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    CLSID png;
    REQUIRE(ShotPngEncoder(png));
    ShotHiddenHost host;
    REQUIRE(host.Host().IsWindow());

    const std::wstring dir = ShotOutputDir();
    REQUIRE(ShotEnsureDirectory(dir));
    const std::wstring fixtureDir = dir + L"\\" + kShotFixtureSub;
    REQUIRE(ShotEnsureDirectory(fixtureDir));
    const std::wstring photo = fixtureDir + L"\\" + kBubblePhotoName;
    REQUIRE(ShotMakePhoto(photo, png));
    REQUIRE(ShotWaitImageDecoded(photo, kShotPhotoDispW, kShotPhotoDispH));

    const long long nowMs = static_cast<long long>(::time(NULL)) * kShotMsPerSecond;
    std::string manifest;

    //整段会话：单聊。先排一次版再置发送失败、撤回（与真实界面里回执、推送到达时的顺序相同），保存时按新内容重排
    {
        ChatMessageList list;
        std::string failedCmid;
        long long recallMsgid = 0;
        ShotFillSingleConversation(list, nowMs, photo, failedCmid, recallMsgid);
        ShotLayoutList(list, host.Host(), true, kShotConvMaxHeight);
        ShotMarkFailed(list, failedCmid);
        ShotRecall(list, recallMsgid, nowMs);
        INFO("bubble shot: single conversation");
        REQUIRE(ShotSaveWholeList(list, host.Host(), true, png, dir + L"\\" + kBubbleSingleConvName));
        ShotManifestLine(manifest, kBubbleSingleConvName,
                         L"单聊整段会话：时间分隔、对方连续两条、本人已读、对方长文字、图片、大 emoji、本人带链接、对方引用回复、"
                         L"本人发送失败、本人撤回（带「编辑」）");
    }

    //整段会话：群聊
    {
        ChatMessageList list;
        ShotFillGroupConversation(list, nowMs);
        INFO("bubble shot: group conversation");
        REQUIRE(ShotSaveWholeList(list, host.Host(), false, png, dir + L"\\" + kBubbleGroupConvName));
        ShotManifestLine(manifest, kBubbleGroupConvName,
                         L"群聊整段会话（四个发送者）：时间分隔、同一人连续两条、系统提示、@我、本人长文字、@别人、"
                         L"被 Pin 的消息、本人语音、收尾短文字");
    }

    //单项特写
    const std::vector<BubbleShot> shots = BuildBubbleShots(nowMs, photo);
    for (size_t i = 0; i < shots.size(); ++i)
    {
        const std::wstring fileName = std::wstring(kBubbleFilePrefix) + ShotIndexText(i + 1) + L"_" + shots[i].m_name + L".png";
        INFO("bubble shot: " << ShotToUtf8(fileName));
        REQUIRE(RenderBubbleShot(host.Host(), shots[i], nowMs, png, dir + L"\\" + fileName));
        ShotManifestLine(manifest, fileName, shots[i].m_caption);
    }
    REQUIRE(ShotWriteManifest(dir + L"\\" + kBubbleManifestName, manifest));
}

namespace {

// ---- CARDSHOT3 卡片样式件 --------------------------------------------------

const int kKitWidth = 760;                  // 样式件画布宽（像素），与消息列表同宽
const int kKitHeight = 640;                 // 样式件画布高（像素）
const COLORREF kKitBg = RGB(0xFA, 0xF9, 0xF6);   // 画布底色：消息列表的暖白底
const int kKitMargin = 24;                  // 画布四周留白
const int kKitTileStep = 92;                // 相邻两个图标块的水平间距（含下方的说明文字宽度）
const int kKitTilesPerRow = 8;              // 每行放几个图标块
const int kKitTileRowH = 74;                // 一行图标块（含说明文字）的高度
const int kKitCaptionGap = 6;               // 图标块与下方说明文字之间的间距
const int kKitCaptionH = 16;                // 说明文字行高
const int kKitCaptionPx = 11;               // 说明文字字号
const COLORREF kKitCaptionColor = RGB(0x6F, 0x6E, 0x73);   // 说明文字颜色
const int kKitChipGap = 12;                 // 相邻状态标签之间的间距
const int kKitRowGap = 26;                  // 各段之间的垂直间距
const int kKitButtonColW = 110;             // 按钮矩阵每列宽
const int kKitButtonW = 84;                 // 按钮矩阵里主要、查看按钮的宽度
const int kKitButtonRowH = 40;              // 按钮矩阵每行高
const int kKitSampleTitleTop = 14;          // 示例卡片里标题距卡片顶（与卡片上内边距相同）
const int kKitSampleHeaderH = 36;           // 示例卡片头部高（与图标块同高）
const int kKitSampleGap = 20;               // 两张示例卡片之间的水平间距
const int kKitSampleRoomActive = 1;         // 示例用的房间状态：进行中
const int kKitSampleRoomEnded = 2;          // 示例用的房间状态：已结束
const int kKitMediaVideo = 2;               // 示例用的通话媒体类型：视频
const int kKitShareFolder = 2;              // 示例用的共享类型：文件夹

// 一个要画的图标块：消息与说明文字
struct KitTile
{
    ChatMessage  m_msg;       // 用来取类型样式的消息
    std::wstring m_caption;   // 图标块下方的说明文字
};

// 造一张 kMsgScreenShare 一族的卡片消息
ChatMessage KitShareFamily(bool call, bool groupCall, bool announcement, bool memo, bool cloud)
{
    ChatMessage m;
    m.kind = kMsgScreenShare;
    m.screenShareIsCallCard = call;
    m.screenShareIsGroupCall = groupCall;
    m.screenShareIsAnnouncement = announcement;
    m.screenShareIsMemo = memo;
    m.screenShareIsCloudRecord = cloud;
    m.screenShareHasButton = true;
    m.screenShareState = kKitSampleRoomActive;
    return m;
}

// 造一条指定种类的消息
ChatMessage KitMsg(int kind)
{
    ChatMessage m;
    m.kind = kind;
    return m;
}

// 十五种卡片外加两种已结束的灰色图标块
std::vector<KitTile> BuildKitTiles()
{
    std::vector<KitTile> tiles;
    KitTile t;
    t.m_msg = KitMsg(kMsgFolder);
    t.m_caption = L"文件夹";
    tiles.push_back(t);
    t.m_msg = KitMsg(kMsgMerged);
    t.m_caption = L"聊天记录";
    tiles.push_back(t);
    t.m_msg = KitMsg(kMsgRelay);
    t.m_caption = L"接龙";
    tiles.push_back(t);
    t.m_msg = KitMsg(kMsgFileShare);
    t.m_msg.shareKind = kKitShareFolder;
    t.m_caption = L"文件共享";
    tiles.push_back(t);
    t.m_msg = KitShareFamily(false, false, false, false, false);
    t.m_caption = L"屏幕共享";
    tiles.push_back(t);
    t.m_msg.screenShareState = kKitSampleRoomEnded;
    t.m_caption = L"屏幕共享·结束";
    tiles.push_back(t);
    t.m_msg = KitShareFamily(true, false, false, false, false);
    t.m_caption = L"语音通话";
    tiles.push_back(t);
    t.m_msg = KitShareFamily(false, true, false, false, false);
    t.m_msg.groupCallMedia = kKitMediaVideo;
    t.m_caption = L"群视频通话";
    tiles.push_back(t);
    t.m_msg.screenShareState = kKitSampleRoomEnded;
    t.m_caption = L"群通话·结束";
    tiles.push_back(t);
    t.m_msg = KitShareFamily(false, false, true, false, false);
    t.m_caption = L"群公告";
    tiles.push_back(t);
    t.m_msg = KitShareFamily(false, false, false, true, false);
    t.m_caption = L"群备忘";
    tiles.push_back(t);
    t.m_msg = KitShareFamily(false, false, false, false, true);
    t.m_caption = L"云端录像";
    tiles.push_back(t);
    t.m_msg = KitMsg(kMsgGroupInvite);
    t.m_caption = L"入群邀请";
    tiles.push_back(t);
    t.m_msg = KitMsg(kMsgSecretInvite);
    t.m_caption = L"密聊";
    tiles.push_back(t);
    t.m_msg = KitMsg(kMsgText);
    t.m_msg.isMarkdown = true;
    t.m_caption = L"Markdown";
    tiles.push_back(t);
    return tiles;
}

// 画一张只有头部（图标块 + 标题 + 副标题）与底部一行的示例卡片，返回卡片底边
int KitSampleCard(HDC hdc, Gdiplus::Graphics& g, int left, int top, const ChatMessage& msg, const wchar_t* title,
                  const wchar_t* subtitle, const std::vector<ChatCardButton>& buttons)
{
    using namespace balloonchatwnd::cardstyle;
    const int height = kCardPadTop + kKitSampleHeaderH + kCardPadBottom + kFooterDividerH + kFooterH;
    const RECT card = { left, top, left + kCardWidth, top + height };
    cardpaint::PaintCardFrame(g, card);
    const ChatCardTypeStyle style = ChatCardTypeStyleOf(msg);
    cardpaint::PaintCardTile(g, left + kCardPadH, top + kCardPadTop, style);
    const int textLeft = left + kCardPadH + kTileSize + kTileTextGap;
    RECT titleRc = { textLeft, top + kKitSampleTitleTop - 2, card.right - kCardPadH, top + kKitSampleTitleTop - 2 + kTitleLineH };
    chatpaint::DrawGdiText(hdc, title, titleRc, kTitlePx, true, kTitleColor, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RECT subRc = { textLeft, titleRc.bottom, card.right - kCardPadH, titleRc.bottom + kSubtitleLineH };
    chatpaint::DrawGdiText(hdc, subtitle, subRc, kSubtitlePx, false, kSubtitleColor, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    const RECT footer = { left, card.bottom - kFooterH, card.right, card.bottom };
    cardpaint::PaintCardFooter(hdc, g, footer, style, ChatCardTypeLabel(msg), buttons, -1);
    return card.bottom;
}

} // anonymous namespace

TEST_CASE("CARDSHOT3 chat card style kit", "[.cardshot]")
{
    ShotGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    CLSID png;
    REQUIRE(ShotPngEncoder(png));
    const std::wstring dir = ShotOutputDir();
    REQUIRE(ShotEnsureDirectory(dir));

    HDC screen = ::GetDC(NULL);
    HDC dc = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, kKitWidth, kKitHeight);
    ::ReleaseDC(NULL, screen);
    REQUIRE(dc != NULL);
    REQUIRE(bmp != NULL);
    HGDIOBJ old = ::SelectObject(dc, bmp);
    RECT all = { 0, 0, kKitWidth, kKitHeight };
    HBRUSH bg = ::CreateSolidBrush(kKitBg);
    ::FillRect(dc, &all, bg);
    ::DeleteObject(bg);
    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        //图标块
        const std::vector<KitTile> tiles = BuildKitTiles();
        int y = kKitMargin;
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            const int col = (int)(i % kKitTilesPerRow);
            const int row = (int)(i / kKitTilesPerRow);
            const int x = kKitMargin + col * kKitTileStep;
            const int top = y + row * kKitTileRowH;
            cardpaint::PaintCardTile(g, x, top, ChatCardTypeStyleOf(tiles[i].m_msg));
            RECT cap = { x - kKitMargin / 2, top + cardstyle::kTileSize + kKitCaptionGap, x + kKitTileStep - kKitMargin / 2,
                         top + cardstyle::kTileSize + kKitCaptionGap + kKitCaptionH };
            chatpaint::DrawGdiText(dc, tiles[i].m_caption.c_str(), cap, kKitCaptionPx, false, kKitCaptionColor,
                                   DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
        y += ((int)(tiles.size() + kKitTilesPerRow - 1) / kKitTilesPerRow) * kKitTileRowH + kKitRowGap;

        //五种状态标签
        const wchar_t* chipTexts[] = { L"正在共享", L"待接收", L"对方已拒绝", L"已结束", L"提醒全体成员" };
        const int chipTones[] = { kCardChipLive, kCardChipPending, kCardChipBad, kCardChipEnded, kCardChipNeutral };
        int x = kKitMargin;
        for (int i = 0; i < 5; ++i)
            x = cardpaint::PaintCardChip(dc, g, x, y, y + cardstyle::kChipH, CString(chipTexts[i]), chipTones[i]) + kKitChipGap;
        y += cardstyle::kChipH + kKitRowGap;

        //三级按钮 × 正常 / 悬停 / 置灰
        const wchar_t* btnTexts[] = { L"加入", L"查看", L"忽略" };
        const int levels[] = { kCardButtonPrimary, kCardButtonView, kCardButtonSecondary };
        const int states[] = { kCardButtonNormal, kCardButtonHover, kCardButtonDisabled };
        for (int l = 0; l < 3; ++l)
        {
            for (int s = 0; s < 3; ++s)
            {
                const int left = kKitMargin + s * kKitButtonColW;
                const int width = (levels[l] == kCardButtonSecondary)
                                ? cardpaint::MeasureCardButtonWidth(dc, CString(btnTexts[l]), levels[l]) : kKitButtonW;
                const RECT rc = { left, y + l * kKitButtonRowH, left + width, y + l * kKitButtonRowH + cardstyle::kButtonH };
                cardpaint::PaintCardButton(dc, g, rc, CString(btnTexts[l]), levels[l], states[s]);
            }
        }
        y += 3 * kKitButtonRowH + kKitRowGap;

        //两张示例卡片：进行中的屏幕共享（主要按钮）与群公告（查看按钮），文字取自效果图
        ChatMessage share = KitShareFamily(false, false, false, false, false);
        std::vector<ChatCardButton> joinBtn;
        joinBtn.push_back(ChatCardButton(CString(L"加入"), kCardButtonPrimary, true, 0));
        KitSampleCard(dc, g, kKitMargin, y, share, L"登录页改版效果演示", L"李晓雯发起的屏幕共享", joinBtn);
        ChatMessage notice = KitShareFamily(false, false, true, false, false);
        std::vector<ChatCardButton> viewBtn;
        viewBtn.push_back(ChatCardButton(CString(L"查看"), kCardButtonView, true, 0));
        KitSampleCard(dc, g, kKitMargin + cardstyle::kCardWidth + kKitSampleGap, y, notice, L"关于国庆假期值班安排的通知",
                      L"王建国发布的群公告", viewBtn);
    }
    ::SelectObject(dc, old);
    bool saved = false;
    {
        Gdiplus::Bitmap image(bmp, NULL);
        const std::wstring file = dir + L"\\cardstyle_kit.png";
        saved = (image.Save(file.c_str(), &png, NULL) == Gdiplus::Ok);
    }
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
    REQUIRE(saved);
}
