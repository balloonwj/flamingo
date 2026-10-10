/**
 *  聊天记录管理器「文件」「图片」「链接」「与我有关」四页的右侧界面，HistoryLibraryLayout.h
 *
 *  左栏顶部切到「文件」「图片」「链接」、或在「聊天」标签里选中「与我有关」时，分栏右侧不再显示某个会话的聊天记录，
 *  而是显示本页（docs/聊天记录管理器方案.md §11.5、§11.6、§12.4、§12.5，布局图见 tmp/histenhance/layout.html 图 2、图 3）：
 *      标题区     [»] 文件 · 全部会话      共 86 个文件，聊天文件只包括本机已保存的记录
 *      搜索行     [搜索文件名…………………] [□ 通配符]                       （图片页没有）
 *      筛选行     [全部][我发送的][我接收的] ……… [来源 ▾][类型 ▾][时间 ▾]    （来源、类型只有文件页有；
 *                                                                          与我有关页换成 [全部][@我的][回复我的][我发的]）
 *      列表       文件页：每行 色块 | 文件名 + 来源 | 大小 | 时间 | 状态 | 悬停时的操作按钮
 *                 图片页：按天分组的缩略图网格（与聊天记录页「图片」分类的网格同一套）
 *                 链接页：每行 链接色块 | 域名 + 链接、来源 | 时间 | 悬停时「打开」「复制链接」「定位」
 *                 与我有关页：每行 关系标签 | 正文预览、来源 | 时间 | 悬停时「复制」「定位」
 *      底部       共 N 个 · 第 x/y 页     [首页][<][1][2]…[>][末页]  [下载本页全部]（只有文件页、图片页有）
 *
 *  本文件只建控件、做局部更新，不碰数据与窗口；窗口（BalloonHistoryWindowLibrary.cpp）把查询结果翻译成
 *  HistoryLibraryViewData 交给这里。控件编号都在 kIdLib* 段（5200 起），与聊天记录页、左栏的编号不重复。
 *  布局单测见 Tests/HistoryLibraryLayoutTests.cpp（标签 [histfiles] / [histimages]）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <memory>
#include <vector>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "HistoryView.h"            // HighlightRange
#include "HistoryWindowLayout.h"    // HistoryRowView（图片页的网格沿用它）
#include "HistoryLibraryModel.h"    // 两页的每页条数

namespace balloonhistory {

//每一种按行编号的控件最多给多少个编号，须不小于两页的每页条数（kHistoryLibraryModelFilePageSize、
//kHistoryLibraryModelImagePageSize）
const int kIdLibRowCapacity = 200;

//文件 / 图片页右侧界面的控件编号
enum HistoryLibraryCtrlId
{
    kIdLibBody          = 5200,   //整个右侧界面的根容器（与聊天记录页的 kIdHistoryBody 互相替换显示）
    kIdLibHeaderRow     = 5201,   //标题区那一行；「»」显隐变化后按它重排
    kIdLibExpandLeft    = 5202,   //标题区最左边的「»」：左栏折叠后出现，点它重新展开左栏
    kIdLibTitle         = 5203,   //标题「文件 · 全部会话」「图片 · 产品研发群」
    kIdLibSubtitle      = 5204,   //标题右侧的灰色说明「共 N 个文件，聊天文件只包括本机已保存的记录」
    kIdLibSearchRow     = 5205,   //搜索行容器；图片页隐藏
    kIdLibSearch        = 5206,   //文件名搜索框
    kIdLibWildcard      = 5207,   //搜索框右侧的「通配符」复选框
    kIdLibFilterRow     = 5208,   //筛选行容器；来源、类型下拉显隐变化后按它重排
    kIdLibDirAll        = 5210,   //方向胶囊：全部（编号连续，与 HistoryLibraryDirection 的取值一一对应）
    kIdLibDirSent       = 5211,   //方向胶囊：我发送的
    kIdLibDirReceived   = 5212,   //方向胶囊：我接收的
    kIdLibSourceCombo   = 5215,   //来源下拉（下标与 HistoryLibrarySource 一一对应）；图片页隐藏
    kIdLibCategoryCombo = 5216,   //类型下拉（下标与 HistoryFileCategory 一一对应）；图片页隐藏
    kIdLibTimeCombo     = 5217,   //时间下拉（下标与 HistoryTimeRange 的前六项一一对应）
    kIdLibScroll        = 5220,   //列表滚动视口；换页、改筛选时只换它的内容
    kIdLibList          = 5221,   //滚动视口里的内容容器（文件页的行、图片页的网格）
    kIdLibEmptyTitle    = 5222,   //列表为空时的主文案
    kIdLibEmptySub      = 5223,   //列表为空时的副文案
    kIdLibFooter        = 5230,   //底部一行
    kIdLibFooterInfo    = 5231,   //底部左侧「共 N 个 · 第 x/y 页」
    kIdLibPager         = 5232,   //分页器容器（换页时清空重填）
    kIdLibPageFirst     = 5233,   //分页：首页
    kIdLibPagePrev      = 5234,   //分页：上一页
    kIdLibPageNext      = 5235,   //分页：下一页
    kIdLibPageLast      = 5236,   //分页：末页
    kIdLibBatchDownload = 5237,   //「下载本页全部」：下载本页里本机还没有的文件或原图
    kIdLibPageCellBase  = 5240,   //分页器第 i 格页码按钮的编号 = 基址 + i（i < kHistoryPagerMaxCells；省略号格不建按钮）
    //与我有关页的关系胶囊（编号连续，与 HistoryLibraryRelation 的取值一一对应；其它页隐藏，与我有关页隐藏方向胶囊）
    kIdLibRelAll        = 5260,   //全部
    kIdLibRelMention    = 5261,   //@我的
    kIdLibRelReply      = 5262,   //回复我的
    kIdLibRelMine       = 5263,   //我发的
    kIdLibRowBase       = 6000,   //文件页第 i 行行容器的编号 = 基址 + i（双击、右键、悬停按它认行）
    kIdLibOpenBase      = 6200,   //第 i 行「打开」按钮（本机有文件时）
    kIdLibFolderBase    = 6400,   //第 i 行「打开所在文件夹」按钮（本机有文件时）
    kIdLibDownloadBase  = 6600,   //第 i 行「下载 / 取消下载」按钮（本机没有、可以下载时）
    kIdLibLocateBase    = 6800,   //第 i 行「定位」按钮：跳到聊天里那条消息，群文件打开群聊窗的群文件页
    kIdLibMoreBase      = 7000,   //第 i 行「⋯」按钮：弹出更多操作（另存为、转发、复制文件名）
    kIdLibGridBase      = 7200,   //图片页第 i 张缩略图的编号 = 基址 + i
    //链接页「复制链接」、与我有关页「复制」按钮：第 i 行的编号 = 基址 + i。7400～8199 是聊天记录页时间轴的编号
    //（HistoryWindowLayout.h），这一段接在它后面
    kIdLibCopyBase      = 8200,
};

//文件页一行文件在本机的状态
enum HistoryLibraryFileState
{
    kHistoryLibFileRemote      = 0,   //本机没有，可以从服务器下载
    kHistoryLibFileLocal       = 1,   //本机有完整的文件，可以打开
    kHistoryLibFileDownloading = 2,   //本窗口正在下载
    kHistoryLibFileUnavailable = 3,   //本机没有、也无法下载（在线直传的文件、文件夹记录在本机没有落点）
};

//文件页的一行显示什么
struct HistoryLibraryFileRowView
{
    CString                     m_ext;             //小写扩展名，画类型色块用；文件夹为空
    bool                        m_isFolder;        //是否为文件夹传输记录（画文件夹色块，名称后注明「文件夹」）
    CString                     m_name;            //文件名或文件夹名
    std::vector<HighlightRange> m_nameHighlights;  //文件名里搜索命中的区间（UTF-16 下标）
    CString                     m_sourceText;      //第二行的来源说明，如「张三 · 产品研发群」「发给 林知夏」
    bool                        m_isGroupFile;     //第二行最前面是否显示「群文件」标签
    CString                     m_mineTag;         //第二行的「我发送的」「我上传的」标签；空串表示不显示
    CString                     m_sizeText;        //大小文字，如「2.1 MB」
    CString                     m_timeText;        //时间文字，如「10-01 18:12」
    int                         m_state;           //本机状态，取值见 HistoryLibraryFileState
    int                         m_progress;        //下载进度（0～100），只在下载中时有意义
    bool                        m_canLocate;       //是否可以「定位」

    HistoryLibraryFileRowView()
        : m_isFolder(false)
        , m_isGroupFile(false)
        , m_state(kHistoryLibFileRemote)
        , m_progress(0)
        , m_canLocate(true)
    {
    }
};

//链接页、与我有关页的一行显示什么（docs/聊天记录管理器方案.md §12.4、§12.5）
struct HistoryLibraryTextRowView
{
    //与我有关页的关系，取值见 HistoryLibraryRelation：行首按它显示「@我」「回复我」「我发的」标签并取标签颜色。
    //链接页不使用（行首画链接色块）
    int                         m_relation;
    //第一行粗体部分：链接页为域名，与我有关页为空串
    CString                     m_title;
    //第一行其余部分：链接页为完整链接，与我有关页为正文预览（单行，换行已替换成空格）
    CString                     m_text;
    std::vector<HighlightRange> m_textHighlights;   //m_text 里搜索命中的区间（UTF-16 下标）
    CString                     m_sourceText;       //第二行：「发送人 · 会话」，链接页多个链接时再接「 · 等 N 个链接」
    CString                     m_mineTag;          //第二行最前面的「我发送的」标签（链接页）；空串表示不显示
    CString                     m_timeText;         //时间文字
    bool                        m_canOpen;          //是否显示「打开」（链接页、链接可以打开时）

    HistoryLibraryTextRowView()
        : m_relation(0)
        , m_canOpen(false)
    {
    }
};

//文件 / 图片页的整页渲染数据
struct HistoryLibraryViewData
{
    int                                    m_kind;            //哪一页，取值见 HistoryLibraryKind
    CString                                m_title;           //标题
    CString                                m_subtitle;        //标题右侧的灰色说明
    bool                                   m_showExpandLeft;  //是否显示「»」（左栏折叠时）
    int                                    m_direction;       //选中的方向胶囊，取值见 HistoryLibraryDirection
    int                                    m_relation;        //与我有关页选中的关系胶囊，取值见 HistoryLibraryRelation
    int                                    m_source;          //来源下拉的选中项
    int                                    m_category;        //类型下拉的选中项
    int                                    m_timeIndex;       //时间下拉的选中项
    bool                                   m_wildcard;        //「通配符」复选框是否勾选
    int                                    m_currentPage;     //当前页（从 1 起）
    int                                    m_pageCount;       //总页数（不小于 1）
    int64_t                                m_total;           //当前筛选条件下的总条数
    std::vector<HistoryLibraryFileRowView> m_fileRows;        //文件页当前页各行
    std::vector<HistoryRowView>            m_imageCells;      //图片页当前页各张（只用缩略图、日期分组头与悬停提示）
    std::vector<HistoryLibraryTextRowView> m_textRows;        //链接页、与我有关页当前页各行
    CString                                m_emptyTitle;      //列表为空时的主文案
    CString                                m_emptySub;        //列表为空时的副文案
    int                                    m_pendingDownloads;//本页还没下载的个数；为 0 时「下载本页全部」不可点

    HistoryLibraryViewData()
        : m_kind(0)
        , m_showExpandLeft(false)
        , m_direction(0)
        , m_relation(0)
        , m_source(0)
        , m_category(0)
        , m_timeIndex(0)
        , m_wildcard(false)
        , m_currentPage(1)
        , m_pageCount(1)
        , m_total(0)
        , m_pendingDownloads(0)
    {
    }
};

/**
 *  建文件 / 图片页的整套右侧界面（标题区、搜索行、筛选行、列表、底部）。列表内容按 data 填好。
 *  @param data              整页渲染数据
 *  @param listWidth         列表可用宽度（像素），排图片网格的列数用；不大于 0 时按中等窗口宽度
 *  @param listRightInsetPx  列表滚动视口右侧给窗口缩放边框让出的宽度（物理像素，与聊天记录页的同名参数相同，
 *                           见 BuildHistoryWindowBody）；默认 0 表示不让出
 *  @return 根容器（id = kIdLibBody），调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryLibraryBody(const HistoryLibraryViewData& data, int listWidth,
                                                                 int listRightInsetPx = 0);

/**
 *  建列表内容（滚动视口的内容）：文件页是一行行文件，图片页是缩略图网格；没有内容时是空状态文案。
 *  窗口换页、改筛选时用它换掉滚动视口的内容。
 *  @param data       整页渲染数据
 *  @param listWidth  列表可用宽度（像素）
 *  @return 内容容器（id = kIdLibList），调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryLibraryList(const HistoryLibraryViewData& data, int listWidth);

/**
 *  局部更新标题区、方向胶囊、底部信息、分页器与「下载本页全部」的可用状态（不重建搜索框，保住输入焦点）。
 *  @param root  文件 / 图片页的根容器或它的祖先（不持有），可为 NULL
 *  @param data  整页渲染数据
 */
void ApplyHistoryLibraryChrome(balloonwjui::DuiControl* root, const HistoryLibraryViewData& data);

/**
 *  切换页时调整筛选区：图片页隐藏搜索行；来源、类型下拉只有文件页显示；与我有关页显示关系胶囊、隐藏方向胶囊；
 *  搜索框提示文字随页变化；「下载本页全部」只在文件页、图片页显示。随后按开发约定文档 UI 约定第 5 条重排发生显隐
 *  变化的容器。
 *  @param root  文件 / 图片页的根容器或它的祖先（不持有），可为 NULL
 *  @param kind  哪一页，取值见 HistoryLibraryKind
 */
void ApplyHistoryLibraryKind(balloonwjui::DuiControl* root, int kind);

/**
 *  只更新一个文件行第二行末尾的「来源 · 状态」文字，不重建整行（群文件下载进度变化时用）。悬停按钮不随之改变：
 *  状态本身切换（开始下载、取消、下载完成）时窗口会重取当前页、整行重建。
 *  @param root  根容器或它的祖先（不持有），可为 NULL
 *  @param row   当前页行下标
 *  @param view  这一行的新显示内容（只用来源说明、状态、进度）
 *  @return 找到这一行并已更新时返回 true
 */
bool ApplyHistoryLibraryRowState(balloonwjui::DuiControl* root, int row, const HistoryLibraryFileRowView& view);

/**
 *  分页器第 cellIndex 格对应第几页（格子的排法见 HistoryView.h 的 BuildPagerModel）。
 *  @param cellIndex   格子下标（从 0 起）
 *  @param currentPage 当前页
 *  @param pageCount   总页数
 *  @return 页号（从 1 起）；越界或这一格是省略号时返回 0
 */
int HistoryLibraryPageOfCell(int cellIndex, int currentPage, int pageCount);

/**
 *  文件页、图片页列表行的时间文字：今年的写成「MM-DD HH:MM」，往年的写成「YYYY-MM-DD」（本地时区）。
 *  @param timestampMs 时间（Unix 毫秒）
 *  @param nowMs       当前时间（Unix 毫秒），判断是不是今年
 *  @return 时间文字（UTF-8）；时间不大于 0 时为空串
 */
std::string FormatLibraryTime(int64_t timestampMs, int64_t nowMs);

} // namespace balloonhistory
