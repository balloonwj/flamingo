/**
 *  FlamingoMarkdown.dll（Markdown 消息渲染）与宿主之间的接口约定。
 *
 *  FlamingoMarkdown.dll 把 Markdown 源文本解析成文档，按给定宽度排版，画到宿主给的 HDC 上，并提供命中测试、
 *  按范围取文字与纯文本提取（方案见 docs/Markdown消息方案.md）。宿主（Flamingo.exe）在进程启动后于后台线程
 *  用 LoadLibrary 加载它，经唯一的导出函数 FlamingoMarkdown_GetApi 取得一张函数表；DLL 不存在或加载失败时，
 *  宿主不显示任何 Markdown 入口，收到的 Markdown 消息按普通文字显示源文本。
 *
 *  版本兼容与 FlamingoOcr 相同，<u>不</u>要求双方版本号完全相同：
 *    - 带 m_cbSize 的结构都以它开头，取值为填写方编译时的结构大小；
 *    - 以后只在这些结构的<u>末尾</u>追加字段，不修改、不删除、不重排已有字段；
 *    - 读取某个字段之前，先按 m_cbSize 判断对方是否提供了它（见 MdApiHasField）。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此接口只使用 C 类型：
 *    - 文档（MdDocument）由 m_parse 创建，只能经 m_freeDoc 释放；
 *    - DLL 输出的字符串一律写进调用方提供的缓冲区：缓冲区为 NULL 或不够大时返回 md_buffer_too_small，
 *      并经出参告知所需长度（不含结尾的 0），调用方按此分配后再调用一次；
 *    - 宿主传入的指针只在本次调用期间有效（MdStyle 里的字体名由 DLL 自行复制）；
 *    - 不传递 STL 对象，异常不越过模块边界（每个导出函数都在边界上捕获并转成返回码）。
 *
 *  线程约定：m_parse、m_toPlainText、m_freeDoc 不依赖全局可变状态，可以在任意线程调用；m_layout、m_paint、
 *  m_hitTest 在界面线程调用；同一个文档不得同时在两个线程上使用。m_log 回调可以在任意线程发生。
 *
 *  坐标约定：m_layout 之后，文档的左上角为原点 (0, 0)；m_paint 把文档画在 HDC 的 (x, y) 处；m_hitTest 接收
 *  相对文档原点的坐标。所有尺寸单位都是设备像素，宿主已按 DPI 换算好字号（见 MdStyle）。
 *
 *  「渲染后文字」：文档里所有可见文字按阅读顺序连成的一串 UTF-16 文字。块与块之间、强制换行处为 '\n'，
 *  表格同一行相邻单元格之间为 '\t'；列表的项目符号与编号不在其中。选区、命中位置都以它的下标表示。
 *
 *  典型用法（宿主侧）：
 *
 *      FlamingoMarkdownGetApiFn getApi = (FlamingoMarkdownGetApiFn)::GetProcAddress(hDll, FLAMINGO_MARKDOWN_ENTRY_NAME);
 *      const flamingomd::MdApi* api = (getApi != NULL) ? getApi() : NULL;
 *      if (api != NULL && api->m_cbSize >= flamingomd::kMdApiMinSize && api->m_init(&host) == flamingomd::md_ok)
 *      {
 *          flamingomd::MdDocument* doc = NULL;
 *          if (api->m_parse(utf8, bytes, &options, &doc) == flamingomd::md_ok)
 *          {
 *              int w = 0;
 *              int h = 0;
 *              api->m_layout(doc, hdc, &style, maxWidth, &w, &h);
 *              api->m_paint(doc, hdc, left, top, &paintState);
 *              api->m_freeDoc(doc);
 *          }
 *      }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stddef.h>
#include <stdint.h>

//DLL 文件名，与宿主 exe 放在同一目录
#define FLAMINGO_MARKDOWN_DLL_NAME      L"FlamingoMarkdown.dll"

//导出函数名（GetProcAddress 使用 ANSI 名称），由 FlamingoMarkdown.def 固定，32 位与 64 位相同
#define FLAMINGO_MARKDOWN_ENTRY_NAME    "FlamingoMarkdown_GetApi"

namespace flamingomd {

//DLL 经 MdHostInfo::m_log 写日志时的级别
enum MdLogLevel
{
    md_log_info    = 1,   //普通信息：初始化结果、超长文档的排版耗时等关键路径
    md_log_warning = 2,   //警告：可以继续运行的异常情况，如文档超过嵌套层数上限被截断
    md_log_error   = 3,   //错误：某次调用无法完成，如解析器报告内存不足
};

//各导出函数的返回码
enum MdResultCode
{
    md_ok               = 0,   //成功
    md_bad_argument     = 1,   //参数非法：指针为 NULL、结构大小不足、宽度不大于 0、源文本超过长度上限等
    md_out_of_memory    = 2,   //内存不足
    md_internal_error   = 3,   //内部错误（解析器失败、意外异常），详情已写日志
    md_buffer_too_small = 4,   //输出缓冲区为 NULL 或不够大；所需长度已写进对应出参
    md_not_laid_out     = 5,   //文档尚未排版（m_paint、m_hitTest 之前须先调用 m_layout）
    md_out_of_range     = 6,   //序号越界（链接序号、代码块序号）
};

//命中测试结果的种类
enum MdHitKind
{
    md_hit_none      = 0,   //不在任何文字上（行间空白、气泡留白等）；m_charPos 仍给出最近的字符位置
    md_hit_text      = 1,   //在普通文字上（含标题、行内代码、代码块、表格单元格里的文字）
    md_hit_link      = 2,   //在链接文字上（含图片语法转成的链接）；m_linkIndex 为链接序号
    md_hit_code_copy = 3,   //在代码块右上角的「复制」按钮上；m_codeBlockIndex 为代码块序号
};

/**
 *  DLL 写日志的回调。由宿主实现，可能在任意线程被调用。
 *  @param level 日志级别，取值见 enum MdLogLevel；不认识的值按普通信息处理
 *  @param text  日志正文，ASCII 英文、以 0 结尾，只在本次调用期间有效；不含消息正文
 */
typedef void (__cdecl* MdLogFn)(int level, const char* text);

/**
 *  宿主交给 DLL 的信息，m_init 时传入。m_cbSize 由宿主填为它编译时的结构大小。
 */
struct MdHostInfo
{
    uint32_t m_cbSize;   //本结构在宿主编译时的大小（字节）
    MdLogFn  m_log;      //写日志的回调；为 NULL 时 DLL 不写日志
};

/**
 *  解析选项，m_parse 与 m_toPlainText 时传入；可以传 NULL，表示全部取默认值。
 *  m_cbSize 由调用方填为它编译时的结构大小。
 */
struct MdParseOptions
{
    uint32_t       m_cbSize;            //本结构在调用方编译时的大小（字节）
    const wchar_t* m_imageLinkPrefix;   //图片语法 ![说明](地址) 显示成链接时加在说明前面的文字，如 L"[图片] "；
                                        //NULL 或空串表示不加。DLL 不下载远程图片
    uint32_t       m_flags;             //预留，填 0
};

/**
 *  排版样式，m_layout 时传入。颜色均为 COLORREF（0x00BBGGRR）。尺寸均为设备像素，宿主已按 DPI 换算。
 *  m_cbSize 由调用方填为它编译时的结构大小。
 */
struct MdStyle
{
    uint32_t       m_cbSize;               //本结构在调用方编译时的大小（字节）
    const wchar_t* m_fontName;             //正文字体名（如 i18n::UiFontName() 的结果），不可为 NULL 或空串
    const wchar_t* m_codeFontName;         //代码字体名；NULL 或空串表示 Consolas。缺字形的字符改用正文字体
    int32_t        m_fontPx;               //正文字号：字符高度（像素），与 CreateFontW 传入的 -lfHeight 相同，大于 0
    int32_t        m_scalePercent;         //界面缩放百分比（100 表示 96 DPI），用于内部间距、线宽与圆角；不大于 0 时按 100
    uint32_t       m_textColor;            //正文颜色
    uint32_t       m_mutedColor;           //次要文字颜色：引用块里的文字、有序列表的编号
    uint32_t       m_linkColor;            //链接颜色（链接文字带下划线）
    uint32_t       m_codeBackColor;        //行内代码与代码块的底色
    uint32_t       m_ruleColor;            //分隔线、表格线、引用竖条、一二级标题下方细线的颜色
    uint32_t       m_tableHeaderBackColor; //表格表头行的底色
    uint32_t       m_selectionBackColor;   //选区底色
    uint32_t       m_accentColor;          //强调色：任务列表已勾选框的填充色、「复制」按钮悬停时的颜色
    //以下字段 2026-10-06 追加（DLL 1.3.0 起识别）；m_cbSize 不包含它们时按 0 处理
    uint32_t       m_layoutFlags;          //排版选项，取值为 enum MdLayoutFlag 的按位组合；0 表示文档宽度收缩到内容宽度
};

//排版选项，MdStyle::m_layoutFlags 的取值（可按位组合）
enum MdLayoutFlag
{
    md_layout_fill_width = 0x1,   //文档宽度撑满 m_layout 的 maxWidth，不按内容收缩：预览窗口、导出图片与 PDF 时设置，
                                  //标题下的细线、代码块与引用的底色铺满整个宽度；聊天气泡不设
};

/**
 *  绘制状态，m_paint 时传入；可以传 NULL，表示没有选区、没有悬停、不裁剪。
 *  m_cbSize 由调用方填为它编译时的结构大小。
 */
struct MdPaintState
{
    uint32_t m_cbSize;           //本结构在调用方编译时的大小（字节）
    int32_t  m_selBegin;         //选区起点（渲染后文字的下标）；与 m_selEnd 相等或任一为负表示没有选区；两者顺序不限
    int32_t  m_selEnd;           //选区终点（渲染后文字的下标）
    int32_t  m_hoverLink;        //鼠标悬停的链接序号；-1 表示没有
    int32_t  m_hoverCodeBlock;   //鼠标所在的代码块序号（该代码块显示「复制」按钮）；-1 表示没有
    int32_t  m_hoverCopyButton;  //鼠标悬停在其「复制」按钮上的代码块序号（按钮画成强调色）；-1 表示没有
    RECT     m_clip;             //裁剪矩形（HDC 坐标）：只画与它相交的块；四个值全为 0 表示不裁剪
};

/**
 *  命中测试的出参。调用方先把 m_cbSize 置为它编译时的结构大小，DLL 只填写不超过该大小的字段。
 */
struct MdHitResult
{
    uint32_t m_cbSize;           //调用方填入本结构的大小（字节）
    int32_t  m_kind;             //命中种类，取值见 enum MdHitKind
    int32_t  m_charPos;          //离该点最近的字符边界（渲染后文字的下标，0 ~ 文字长度），拖选用；文档没有文字时为 0
    int32_t  m_linkIndex;        //m_kind 为 md_hit_link 时的链接序号，否则为 -1
    int32_t  m_codeBlockIndex;   //该点所在代码块的序号（不论是否在「复制」按钮上），不在代码块内为 -1
};

//消息卡片文字的部分，m_getCardText 的 part 参数（第二版）
enum MdCardPart
{
    md_card_title   = 0,   //卡片标题：第一个标题块的文字，没有标题时为第一个有文字的块的第一行
    md_card_summary = 1,   //卡片摘要：其余文字压成的一段
};

/**
 *  HTML 导出选项，m_exportHtml 时传入（第二版）；可以传 NULL，表示全部取默认值。
 *  m_cbSize 由调用方填为它编译时的结构大小。
 */
struct MdHtmlOptions
{
    uint32_t       m_cbSize;   //本结构在调用方编译时的大小（字节）
    const wchar_t* m_title;    //写进 <title> 的文档标题；NULL 或空串时写「Markdown」
};

/**
 *  文档概况，m_getDocInfo 的出参。调用方先把 m_cbSize 置为它编译时的结构大小。
 */
struct MdDocInfo
{
    uint32_t m_cbSize;           //调用方填入本结构的大小（字节）
    int32_t  m_textLength;       //渲染后文字的长度（UTF-16 个数）
    int32_t  m_linkCount;        //链接个数（含图片语法转成的链接）
    int32_t  m_codeBlockCount;   //代码块个数
    int32_t  m_truncated;        //非 0 表示文档超过嵌套层数、块数或表格列数上限，超出的部分按文字显示或被省略
};

//文档：不透明类型，只能经函数表操作
struct MdDocument;

/**
 *  DLL 提供的函数表。由 DLL 持有，指针在 DLL 卸载前一直有效。
 */
struct MdApi
{
    //本结构在 DLL 编译时的大小（字节）。宿主读取某个函数指针前先确认它落在这个大小之内
    uint32_t m_cbSize;

    /**
     *  初始化：记下宿主信息，准备 GDI+。只有第一次调用生效，之后的调用直接返回第一次的结果。
     *  @param host 宿主信息，不可为 NULL，m_cbSize 至少覆盖到 m_log
     *  @return md_ok 表示成功；host 非法时返回 md_bad_argument；其它值表示 DLL 无法工作，宿主应视为不可用
     */
    int (__cdecl* m_init)(const MdHostInfo* host);

    /**
     *  DLL 版本，如 L"1.0.0 (md4c 0.6.0)"，写日志与排查问题用。
     *  @return 静态字符串，归 DLL 所有
     */
    const wchar_t* (__cdecl* m_getVersion)();

    /**
     *  解析 Markdown 源文本，生成文档。耗时与源文本长度大致成正比。
     *  @param utf8    源文本（UTF-8，不要求以 0 结尾），bytes 为 0 时可为 NULL；换行可以是 \n、\r\n 或 \r
     *  @param bytes   源文本的字节数，不超过 kMdMaxSourceBytes
     *  @param options 解析选项，可为 NULL
     *  @param outDoc  [出参] 成功时置为新文档（经 m_freeDoc 释放），失败时置为 NULL；不可为 NULL
     *  @return 结果码，取值见 enum MdResultCode
     */
    int (__cdecl* m_parse)(const char* utf8, uint32_t bytes, const MdParseOptions* options, MdDocument** outDoc);

    /**
     *  释放文档。传 NULL 是安全的。
     *  @param doc 要释放的文档，释放后不得再访问
     */
    void (__cdecl* m_freeDoc)(MdDocument* doc);

    /**
     *  取文档概况。解析之后即可调用，不要求先排版。
     *  @param doc 文档，不可为 NULL
     *  @param out [出参] 调用方须先把 m_cbSize 置为 sizeof(MdDocInfo)
     *  @return 结果码
     */
    int (__cdecl* m_getDocInfo)(const MdDocument* doc, MdDocInfo* out);

    /**
     *  按最大宽度排版。文档的实际宽度不超过 maxWidth：内容较窄时收缩到内容宽度，有表格、代码块时按需撑开；
     *  style->m_layoutFlags 含 md_layout_fill_width 时文档宽度就是 maxWidth。
     *  样式与宽度都与上一次相同时直接返回缓存的结果。
     *  @param doc       文档，不可为 NULL
     *  @param hdc       用于测量文字的设备上下文，不可为 NULL；调用前后其中选中的对象不变
     *  @param style     排版样式，不可为 NULL，m_cbSize 至少为 kMdStyleMinSize
     *  @param maxWidth  最大宽度（像素），大于 0
     *  @param outWidth  [出参] 文档宽度（像素），不可为 NULL
     *  @param outHeight [出参] 文档高度（像素），不可为 NULL
     *  @return 结果码
     */
    int (__cdecl* m_layout)(MdDocument* doc, HDC hdc, const MdStyle* style, int maxWidth,
                            int* outWidth, int* outHeight);

    /**
     *  把已排版的文档画到 hdc 的 (x, y) 处。不画文档背景（由宿主画气泡）。调用前后 hdc 中选中的对象不变。
     *  @param doc   文档，不可为 NULL，须已排版
     *  @param hdc   目标设备上下文，不可为 NULL
     *  @param x     文档左上角在 hdc 中的横坐标
     *  @param y     文档左上角在 hdc 中的纵坐标
     *  @param state 绘制状态，可为 NULL
     *  @return 结果码；未排版时返回 md_not_laid_out
     */
    int (__cdecl* m_paint)(MdDocument* doc, HDC hdc, int x, int y, const MdPaintState* state);

    /**
     *  命中测试。
     *  @param doc 文档，不可为 NULL，须已排版
     *  @param x   相对文档原点的横坐标
     *  @param y   相对文档原点的纵坐标
     *  @param out [出参] 调用方须先把 m_cbSize 置为 sizeof(MdHitResult)
     *  @return 结果码；未排版时返回 md_not_laid_out
     */
    int (__cdecl* m_hitTest)(const MdDocument* doc, int x, int y, MdHitResult* out);

    /**
     *  按范围取渲染后的文字（复制选区用），其中的换行输出为 \r\n。
     *  @param doc       文档，不可为 NULL
     *  @param begin     起点下标，与 end 顺序不限；超出 0 ~ 文字长度的部分被截掉
     *  @param end       终点下标
     *  @param buf       输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufChars  缓冲区容量（wchar_t 个数，含结尾的 0）
     *  @param outChars  [出参] 文字长度（不含结尾的 0），不可为 NULL
     *  @return md_ok；缓冲区为 NULL 或不够大时返回 md_buffer_too_small
     */
    int (__cdecl* m_getText)(const MdDocument* doc, int32_t begin, int32_t end,
                             wchar_t* buf, uint32_t bufChars, uint32_t* outChars);

    /**
     *  取第 index 个链接的地址（已补全 http:// 或 mailto: 前缀）。
     *  @param doc      文档，不可为 NULL
     *  @param index    链接序号，0 ~ 链接个数 - 1
     *  @param buf      输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufChars 缓冲区容量（wchar_t 个数，含结尾的 0）
     *  @param outChars [出参] 地址长度（不含结尾的 0），不可为 NULL
     *  @return md_ok；序号越界返回 md_out_of_range；缓冲区不够返回 md_buffer_too_small
     */
    int (__cdecl* m_getLinkUrl)(const MdDocument* doc, int32_t index,
                                wchar_t* buf, uint32_t bufChars, uint32_t* outChars);

    /**
     *  取第 index 个代码块的原文（Tab 不展开），其中的换行输出为 \r\n。
     *  @param doc      文档，不可为 NULL
     *  @param index    代码块序号，0 ~ 代码块个数 - 1
     *  @param buf      输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufChars 缓冲区容量（wchar_t 个数，含结尾的 0）
     *  @param outChars [出参] 原文长度（不含结尾的 0），不可为 NULL
     *  @return md_ok；序号越界返回 md_out_of_range；缓冲区不够返回 md_buffer_too_small
     */
    int (__cdecl* m_getCodeBlockText)(const MdDocument* doc, int32_t index,
                                      wchar_t* buf, uint32_t bufChars, uint32_t* outChars);

    /**
     *  把 Markdown 源文本转成去掉标记的纯文本（UTF-8），用于摘要与搜索。块之间、强制换行处为 \n，
     *  表格同一行相邻单元格之间为 \t。不需要先建文档。
     *  @param utf8     源文本（UTF-8），bytes 为 0 时可为 NULL
     *  @param bytes    源文本的字节数，不超过 kMdMaxSourceBytes
     *  @param options  解析选项，可为 NULL
     *  @param buf      输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufBytes 缓冲区容量（字节，含结尾的 0）
     *  @param outBytes [出参] 纯文本长度（字节，不含结尾的 0），不可为 NULL
     *  @return md_ok；缓冲区为 NULL 或不够大时返回 md_buffer_too_small
     */
    int (__cdecl* m_toPlainText)(const char* utf8, uint32_t bytes, const MdParseOptions* options,
                                 char* buf, uint32_t bufBytes, uint32_t* outBytes);

    // ---- 第二版（2026-10-06，docs/Markdown消息方案.md 第 11 节）：宿主使用前须确认 m_cbSize 足够大 ----

    /**
     *  取消息卡片要显示的文字：标题为第一个标题块的文字（没有标题时为第一个有文字的块的第一行），摘要为其余文字
     *  压成的一段（空白合并为单个空格）。
     *  @param doc      文档，不可为 NULL
     *  @param part     取哪一部分，取值见 enum MdCardPart
     *  @param buf      输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufChars 缓冲区容量（wchar_t 个数，含结尾的 0）
     *  @param outChars [出参] 文字长度（不含结尾的 0），不可为 NULL
     *  @return md_ok；part 不认识返回 md_bad_argument；缓冲区不够返回 md_buffer_too_small
     */
    int (__cdecl* m_getCardText)(const MdDocument* doc, int32_t part, wchar_t* buf, uint32_t bufChars,
                                 uint32_t* outChars);

    /**
     *  把文档导出为单个 HTML 文件的内容（UTF-8、不带 BOM、样式内嵌）。不需要先排版。
     *  @param doc      文档，不可为 NULL
     *  @param options  导出选项，可为 NULL
     *  @param buf      输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufBytes 缓冲区容量（字节，含结尾的 0）
     *  @param outBytes [出参] 内容长度（字节，不含结尾的 0），不可为 NULL
     *  @return md_ok；缓冲区不够返回 md_buffer_too_small
     */
    int (__cdecl* m_exportHtml)(const MdDocument* doc, const MdHtmlOptions* options, char* buf, uint32_t bufBytes,
                                uint32_t* outBytes);

    /**
     *  把已排版的文档按页切分（导出 PDF 用）：只在不落在任何一行中间的位置切开，单行超过一页时从页面高度处硬切。
     *  @param doc        文档，不可为 NULL，须已按页面宽度排版
     *  @param pageHeight 一页可用的高度（与排版同一单位），须大于 0
     *  @param starts     [出参] 各页在文档里的起始纵坐标，升序，第一页为 0；可为 NULL（只查询页数）
     *  @param capacity   starts 的容量（个数）
     *  @param outCount   [出参] 页数，不可为 NULL
     *  @return md_ok；未排版返回 md_not_laid_out；starts 为 NULL 或不够大时返回 md_buffer_too_small
     */
    int (__cdecl* m_paginate)(const MdDocument* doc, int32_t pageHeight, int32_t* starts, uint32_t capacity,
                              uint32_t* outCount);

    // ---- 第三版（2026-10-06，docs/Markdown消息方案.md 第 11.6 节）：宿主使用前须确认 m_cbSize 不小于 kMdApiV3Size ----

    /**
     *  把文档导出为 Word 文档（.docx）的全部字节：标题用内置标题样式、列表用真正的列表编号、表格为 Word 表格。
     *  不需要先排版。
     *  @param doc      文档，不可为 NULL
     *  @param buf      输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufBytes 缓冲区容量（字节）
     *  @param outBytes [出参] 内容长度（字节），不可为 NULL
     *  @return md_ok；缓冲区为 NULL 或不够大时返回 md_buffer_too_small（.docx 是二进制，不写结尾的 0）
     */
    int (__cdecl* m_exportDocx)(const MdDocument* doc, unsigned char* buf, uint32_t bufBytes, uint32_t* outBytes);

    // ---- 第四版（2026-10-07，docs/Markdown消息方案.md 第 12 节）：宿主使用前须确认 m_cbSize 不小于 kMdApiV4Size ----

    /**
     *  把渲染后文字里 [begin, end) 这一段导出为 HTML，复制到剪贴板（CF_HTML）用。输出是完整的 HTML 文档，头部与样式
     *  和 m_exportHtml 相同；正文只含与这一段相交的块：段落、标题、单元格里只留下选中的文字（行内样式照留），列表只含
     *  相交的项（有序列表的起始编号随之调整），表格只含相交的行（不在选区内的单元格留空，列数不变），代码块只含选中的
     *  部分；正文前后带 <!--StartFragment--> 与 <!--EndFragment--> 注释。不需要先排版。
     *  @param doc      文档，不可为 NULL
     *  @param options  导出选项，可为 NULL
     *  @param begin    选区起点（渲染后文字的下标，与 m_getText 相同），与 end 顺序不限，越界截断
     *  @param end      选区终点（不含）
     *  @param buf      输出缓冲区，可为 NULL（只查询所需长度）
     *  @param bufBytes 缓冲区容量（字节，含结尾的 0）
     *  @param outBytes [出参] 内容长度（字节，不含结尾的 0），不可为 NULL；截断后起点等于终点时为 0
     *  @return md_ok；缓冲区不够返回 md_buffer_too_small
     */
    int (__cdecl* m_exportHtmlRange)(const MdDocument* doc, const MdHtmlOptions* options, int32_t begin, int32_t end,
                                     char* buf, uint32_t bufBytes, uint32_t* outBytes);

    // ---- 第五版（2026-10-07，docs/Markdown消息方案.md 第 13 节）：宿主使用前须确认 m_cbSize 不小于 kMdApiV5Size ----

    /**
     *  设置警告框（> [!NOTE] 等）的标题，即界面语言的译文。标题是界面文字，DLL 不自带；没有设置过时显示类型名本身
     *  （NOTE、TIP、IMPORTANT、WARNING、CAUTION）。排版、HTML 与 Word 导出都使用这里的标题。
     *  宿主应在 m_init 之后、第一次排版或导出之前调用一次（DLL 范围的设置，之后不要在其它线程排版的同时修改）。
     *  @param titles 标题数组，按「提示、技巧、重要、警告、注意」（NOTE、TIP、IMPORTANT、WARNING、CAUTION）的顺序；
     *                DLL 复制内容，调用返回后即可释放；其中的 NULL 视为未设置；整个数组为 NULL 时清除全部标题
     *  @param count  数组元素个数，超过 5 的部分忽略
     *  @return md_ok
     */
    int (__cdecl* m_setAlertTitles)(const wchar_t* const* titles, uint32_t count);
};

//源文本的长度上限（字节）：1 MiB，大于聊天消息的内容上限（256 KiB）
const uint32_t kMdMaxSourceBytes = 1024u * 1024u;

//宿主可以使用的函数表最小大小：第一版函数表到 m_toPlainText 为止
const uint32_t kMdApiMinSize = (uint32_t)(offsetof(MdApi, m_toPlainText) + sizeof(void*));

//MdHostInfo 的最小大小：DLL 至少要能读到 m_log
const uint32_t kMdHostInfoMinSize = (uint32_t)(offsetof(MdHostInfo, m_log) + sizeof(void*));

//MdParseOptions 的最小大小：第一版到 m_flags 为止
const uint32_t kMdParseOptionsMinSize = (uint32_t)(offsetof(MdParseOptions, m_flags) + sizeof(uint32_t));

//MdStyle 的最小大小：第一版到 m_accentColor 为止
const uint32_t kMdStyleMinSize = (uint32_t)(offsetof(MdStyle, m_accentColor) + sizeof(uint32_t));

//MdStyle 第二版的大小：到 m_layoutFlags 为止；m_cbSize 不小于它时 DLL 才读取 m_layoutFlags
const uint32_t kMdStyleV2Size = (uint32_t)(offsetof(MdStyle, m_layoutFlags) + sizeof(uint32_t));

//MdPaintState 的最小大小：第一版到 m_clip 为止
const uint32_t kMdPaintStateMinSize = (uint32_t)(offsetof(MdPaintState, m_clip) + sizeof(RECT));

//MdHitResult 的最小大小：DLL 至少要能写 m_kind 与 m_charPos
const uint32_t kMdHitResultMinSize = (uint32_t)(offsetof(MdHitResult, m_charPos) + sizeof(int32_t));

//MdDocInfo 的最小大小：DLL 至少要能写 m_textLength
const uint32_t kMdDocInfoMinSize = (uint32_t)(offsetof(MdDocInfo, m_textLength) + sizeof(int32_t));

//第二版函数表的大小：到 m_paginate 为止（卡片文字、HTML 导出、按页切分）。宿主用这三个函数前须确认 m_cbSize 不小于它
const uint32_t kMdApiV2Size = (uint32_t)(offsetof(MdApi, m_paginate) + sizeof(void*));

//第三版函数表的大小：到 m_exportDocx 为止（Word 导出）。宿主用 m_exportDocx 前须确认 m_cbSize 不小于它
const uint32_t kMdApiV3Size = (uint32_t)(offsetof(MdApi, m_exportDocx) + sizeof(void*));

//第四版函数表的大小：到 m_exportHtmlRange 为止（按选区导出 HTML）。宿主用它之前须确认 m_cbSize 不小于它；
//旧版 DLL 没有这个函数时，复制只写纯文字
const uint32_t kMdApiV4Size = (uint32_t)(offsetof(MdApi, m_exportHtmlRange) + sizeof(void*));

//第五版函数表的大小：到 m_setAlertTitles 为止（警告框标题）。旧版 DLL 没有这个函数，也不认得警告框写法
//（按普通引用显示「[!NOTE]」这一行），宿主不调用即可
const uint32_t kMdApiV5Size = (uint32_t)(offsetof(MdApi, m_setAlertTitles) + sizeof(void*));

//MdHtmlOptions 的最小大小：第一版到 m_title 为止
const uint32_t kMdHtmlOptionsMinSize = (uint32_t)(offsetof(MdHtmlOptions, m_title) + sizeof(void*));

/**
 *  判断 DLL 提供的函数表是否包含某个字段（以字段末尾的偏移量判断）。
 *  @param api          函数表，可为 NULL
 *  @param fieldEndOffs 字段末尾的偏移量，即 offsetof(MdApi, 字段) + sizeof(字段)
 *  @return 包含时返回 true
 */
inline bool MdApiHasField(const MdApi* api, size_t fieldEndOffs)
{
    return api != NULL && api->m_cbSize >= fieldEndOffs;
}

} // namespace flamingomd

//导出函数 FlamingoMarkdown_GetApi 的类型。不接收参数，返回 DLL 持有的函数表，永不返回 NULL
typedef const flamingomd::MdApi* (__cdecl* FlamingoMarkdownGetApiFn)();
