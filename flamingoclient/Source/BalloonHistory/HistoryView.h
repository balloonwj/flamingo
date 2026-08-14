/**
 *  历史消息记录窗口——记录到行视图的转换纯逻辑，HistoryView.h
 *
 *  承载“把本地库一条 HistoryRecord 翻译成列表一行所需展示文字”里可单测的纯逻辑：
 *  内容预览文字（按类型从归一化 content JSON 提取）、时间下拉项下标到时间范围的映射。
 *  与 UI、ATL 无关，便于单测（见 flamingoclient/Source/Tests/HistoryViewTests.cpp）。
 *  发送者昵称 / 头像色的解析、时间戳到“MM-DD HH:MM”的本地化格式化涉及联系人缓存与
 *  时区，放在窗口侧（CBalloonHistoryWindow），不在本纯逻辑模块。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>
#include <vector>
#include <stdint.h>
#include "V2MsgQuery.h"   // HistoryTypeFilter / HistoryTimeRange

namespace balloonhistory {

//分页器一次最多渲染的页码格子数（含省略号占位）。
//规则见 BuildPagerModel：首页 + 省略号 + 当前页前后各一页 + 省略号 + 末页 = 7。
//省略式分页器的全部意义就在于这个数是<u>有上限</u>的 —— 原先按总页数平铺，
//一个 5000 条消息的会话会在底部排出 600 多个按钮，整行直接失控。
constexpr int kHistoryPagerMaxCells = 7;

//分页器里的一格。
struct HistoryPagerCell
{
    int  m_page;        //页号（从1起）；省略号格无意义
    bool m_isEllipsis;  //true 表示这一格是省略号：不可点、不占控件 id
    bool m_isCurrent;   //是否为当前页（渲染成高亮样式）

    HistoryPagerCell()
        : m_page(0)
        , m_isEllipsis(false)
        , m_isCurrent(false)
    {
    }
};

//算出分页器该显示哪些格子（省略式）。
//
//规则：恒显示第 1 页与最后一页；显示当前页及其前后各 1 页；与两端不相邻处补一个
//省略号占位。总页数 <= kHistoryPagerMaxCells 时全部平铺、不出现省略号。
//例（当前第 8 页 / 共 250 页）：1 … 7 [8] 9 … 250
//
//@param currentPage  当前页号（从1起；越界自动夹到 [1, pageCount]）
//@param pageCount    总页数（<1 时按 1 处理）
//@return 各格子（自左至右）；长度恒 <= kHistoryPagerMaxCells
std::vector<HistoryPagerCell> BuildPagerModel(int currentPage, int pageCount);

//文本里一段要高亮的区间（UTF-16 字符口径，非字节）。
struct HighlightRange
{
    int m_begin;  //起始字符下标（含）
    int m_len;    //字符个数

    HighlightRange()
        : m_begin(0)
        , m_len(0)
    {
    }
};

//在一段文本里找出关键字的全部命中区间，供搜索结果高亮用。
//
//之所以用 std::wstring 而不是 std::string：区间要按<u>字符</u>口径给，而预览文字里
//有中文 —— 按 UTF-8 字节切会把一个汉字劈成几段，画出来的底色横跨半个字。Windows 上
//wchar_t 即 UTF-16，与界面层的 CString 同一口径，拿到就能直接用。
//
//大小写不敏感（只对 ASCII 有意义，中文本无大小写之分）。命中不重叠：找到一处后从
//该处末尾继续找，"aaa" 里搜 "aa" 只报一处。
//
//@param text     被搜索的文本
//@param keyword  关键字；空串返回空集
//@return 全部命中区间，按出现顺序；无命中返回空集
std::vector<HighlightRange> FindHighlightRanges(const std::wstring& text,
                                                const std::wstring& keyword);

//把"哪一天"翻成分组头上显示的文字：当天为「今天」、前一天为「昨天」，更早的写成
//「2026-08-02」。
//
//两个参数都取 年*10000 + 月*100 + 日 的形式（如 20260804），由调用方按<u>本地时区</u>
//算好 —— 纯逻辑不碰时区，否则没法稳定测试。
//
//@param dayKey    该组所属的那一天
//@param todayKey  "今天"是哪一天
//@return 分组头文字（UTF-8）；dayKey 非法（<=0）时返回空串
std::string FormatDateGroupLabel(int64_t dayKey, int64_t todayKey);

//算出列表各行要不要"折叠"——即省掉头像与发送者名，只留时间与预览。
//
//判据：与<u>上一行</u>发送者相同、且属于同一天。两个条件缺一不可：
//  · 只看发送者的话，跨天的两条会连成一片，看不出中间隔了一夜；
//  · 日期一换就该重新亮出头像，否则日后加上日期分组头（见方案 §5.2），组头下面
//    会突然冒出一条没有归属的孤儿行。
//首行恒不折叠。
//
//@param senderIds  各行的发送者 userid，按列表顺序
//@param dayKeys    各行所属的"天"，按列表顺序；由调用方按<u>本地时区</u>算好
//                  （比如 年*10000 + 月*100 + 日）。长度须与 senderIds 一致，
//                  不一致时按较短的那个算，多出来的行一律不折叠
//@return 与输入等长的折叠标志；true 表示该行省略头像与名字
std::vector<bool> ComputeCompactFlags(const std::vector<int64_t>& senderIds,
                                      const std::vector<int64_t>& dayKeys);

//列表为空时的提示文案。
struct HistoryEmptyText
{
    std::string m_title;     //主文案（UTF-8）
    std::string m_subtitle;  //副文案（UTF-8）；空串表示不显示副行

    HistoryEmptyText()
    {
    }
};

//按"是什么把结果筛空了"生成空状态文案。
//
//之所以不给一句通用的"暂无内容"：用户看到空白时最需要知道的是<u>下一步该动哪个开关</u>。
//搜不到就提示换个词，Pin 分类空就顺带告诉他怎么钉，两者要说的完全不是一回事。
//优先级：关键字 > 类型胶囊（含 Pin） > 时间范围 > 什么都没选。
//
//时间范围不单独成句，而是作为"所选时间范围内"这个限定语并进类型那句 —— 选了
//"近7天 + 图片"却搜空时若说成"这个会话还没有图片消息"，而实际更早的图片有一堆，
//那就是误导。
//
//@param filter  当前的过滤条件（关键字取 m_keyword，类型取 m_type，时间取 m_range）
//@return 主 / 副文案
HistoryEmptyText BuildEmptyStateText(const HistoryFilter& filter);

//由一条消息的内容类型 + 归一化内容JSON（BuildContentJson 产物）生成列表一行的内容
//预览文字：文本 / 链接取正文；图片→“[图片]”；文件→“[文件] 文件名”；语音→“[语音]”；
//表情→“[表情]”；窗口抖动→“[抖一抖]”（与会话列表 digestForMsgType 口径一致）；其它
//尚未单独渲染的类型→通用“[消息]”兜底。JSON 非法或缺字段时退化为对应占位。
//本函数是<u>会话列表摘要</u>的口径（表情净化成字面量“[表情]”）；消息记录窗要把表情
//画成图，用下面的 FullTextFromContent。
//@param msgtype      内容类型（取值见 net::message_type）
//@param contentJson  归一化内容JSON（UTF-8）
//@return 一行预览文字（UTF-8）
std::string PreviewFromContent(int msgtype, const std::string& contentJson);

//与 PreviewFromContent 并列的另一套口径：取一条消息的完整正文，文本消息<u>不做</u>
//表情净化，原样返回。
//
//为什么要有两个：PreviewFromContent 会把正文里的 /f["<id>"] 内联表情标记统一换成字面
//量“[表情]”（见 DigestFaceTokens），那是会话列表摘要该有的样子 —— 摘要只有一行、画
//不了图，换成文字最省事。消息记录窗则要把表情<u>画成图</u>（见方案 §5 F2），拿到的
//正文必须保留原始标记，交给 balloonchat::ParseSendBody 拆成段序列后再逐段绘制；正文
//一旦被净化过，faceId 就再也还原不回来了。
//故：会话列表摘要用 PreviewFromContent，消息记录窗的内容块用本函数。
//
//除文本外的所有类型（图片 / 文件 / 语音 / 表情 / 抖动 / 通用兜底）一律转调
//PreviewFromContent，返回值逐字相同 —— 不复制那段 switch，免得两份实现日后漂移。
//
//@param msgtype      内容类型（取值见 net::message_type）
//@param contentJson  归一化内容JSON（UTF-8）
//@return 文本 / 链接消息返回 content 里 text 字段的<u>原文</u>（含 /f["<id>"] 标记，
//        UTF-8），缺 text 字段、字段非字符串或 JSON 非法时为空串；其它类型的返回值
//        与 PreviewFromContent 完全相同
std::string FullTextFromContent(int msgtype, const std::string& contentJson);

//一条消息里可用于"行内视觉预览"的附加信息（从归一化 content JSON 里取）。
//列表行据它决定左侧那一小块画什么：图片画缩略图、文件画类型色块；文件行还据其中的
//fileid / md5 发起下载（见方案 §5 F4）。
struct HistoryContentBrief
{
    std::string m_imageMd5;   //图片消息的内容MD5；非图片或缺字段时为空串
    std::string m_fileName;   //文件消息的文件名；非文件或缺字段时为空串
    int64_t     m_fileSize;   //文件字节数；未知为 0
    //文件消息的文件ID（服务端下发的原样字符串，不作数值解析）。本地没有这个文件时，
    //拿它向服务端发起 cmd_request_download（见 net::FileTransfer::EnqueueDownload 的
    //fileid 形参）。非文件消息或缺字段时为空串 —— 空串意味着这一行下载不了。
    std::string m_fileId;
    //文件消息的内容MD5（小写十六进制）。两个用处：下载完成后校验文件完整性；以及
    //按它推导本地缓存路径，据此判断"本地是不是已经有了"。非文件或缺字段时为空串。
    //注意与 m_imageMd5 不是一回事：那个取自 content 的 image 子对象，这个取自 file
    //子对象，两者在 JSON 里恰好同名（都叫 "md5"）但分属不同消息类型。
    std::string m_fileMd5;
    //图片消息的文件ID（服务端下发的原样字符串）。本地缓存里没有原图时，拿它向服务端
    //发起下载（见 net::ImageTransfer::EnqueueDownload 的 fileid 形参）。非图片消息或
    //缺字段时为空串 —— 空串意味着这一行的原图拉不回来。
    std::string m_imageFileId;
    //图片字节数；未知为 0。只用于下载时报给传输层做日志与校验，不参与界面显示。
    int64_t     m_imageSize;

    //==== 文件夹传输记录（::message_type_folder）专用 ====
    //本次传输的会话 id。拿它去 CFolderLocalStore 查这台机器上的根目录落点：查得到且
    //目录还在，这一行才给「打开文件夹」；换了台机器一律查不到，只显示记录。非文件夹
    //记录或缺字段时为空串。
    std::string m_folderSessionId;
    //根目录名（不含路径）。非文件夹记录或缺字段时为空串。
    std::string m_folderName;
    //文件夹内的文件总数；单位：个，未知为 0。与下面的字节数一起拼成行内的规模文字。
    int64_t     m_folderTotalFiles;
    //文件夹内全部文件的字节数之和；未知为 0。
    int64_t     m_folderTotalSize;
    //这次传输的结局，取值为 net/Protocol.h 的 folder_transfer_state 终态（3~8）；
    //0 表示不是文件夹记录、或 content 里没有这个字段。行内据它显示"已完成 / 传输失败"
    //这类结局文字 —— 只写"发过一个文件夹"而不说传成没传成，等于没说。
    int         m_folderResult;

    HistoryContentBrief()
        : m_fileSize(0)
        , m_imageSize(0)
        , m_folderTotalFiles(0)
        , m_folderTotalSize(0)
        , m_folderResult(0)
    {
    }
};

//从归一化 content JSON 里取出行内预览要用的那几项。
//
//图片只取 MD5：本地缓存按内容 MD5 命名（见 net::ImageTransfer::CacheFilePathForMd5），
//有它就能算出缩略图路径。文件取名字与字节数，用来画类型色块与"名字 · 大小"；另取
//fileid 与内容 md5，供右键菜单里的"下载"用（缺其一就发不出下载请求）。
//键名以归一化 content JSON 的产出方 BuildContentJson（V2MsgQuery.cpp）为准。
//
//@param msgtype      内容类型（取值见 net::message_type）
//@param contentJson  归一化内容JSON（UTF-8）
//@return 取到的信息；JSON 非法或缺字段时对应项留空
HistoryContentBrief ParseContentBrief(int msgtype, const std::string& contentJson);

//把一次文件夹传输的结局翻成行内显示的中文文字。
//
//取值是 net/Protocol.h 的 folder_transfer_state 终态（3 已拒绝 / 4 应答前撤回 /
//5 对方离线 / 6 邀请超时 / 7 传输失败 / 8 已完成）。措辞与聊天窗气泡上那一套一致，
//同一次传输在两个界面里读到的说法因而相同。
//@param result  结局取值；非终态与未知取值返回空串（调用方据此不显示结局那一段）
//@return 结局文字（UTF-8）；未知取值返回空串
std::string FolderResultText(int result);

//拼装文件夹传输记录在消息记录窗列表里那一行的完整文字。
//
//体例：`[文件夹] 项目资料 · 128 个文件 · 2.4 GB · 已完成`。前缀与会话列表摘要一致，
//后面依次是文件夹名、文件数、总大小、结局；缺哪一项就少哪一段，不留孤零零的分隔点。
//规模统计与结局都写出来，是因为这条记录本身没有别的可点可看的内容 —— 只说"发过一个
//文件夹"而不说多大、传成没传成，用户还得去别处找。
//@param brief  从归一化 content JSON 里取出的文件夹字段（见 ParseContentBrief）
//@return 一行文字（UTF-8）；文件夹名为空时退化为不带名字的那几段
std::string BuildFolderRowText(const HistoryContentBrief& brief);

//取文件名的小写扩展名（不含点）。没有扩展名、或点在开头（隐藏文件名）时返回空串。
//用于挑类型色块的颜色与色块里那几个字。
//@param fileName  文件名（UTF-8）
//@return 小写扩展名，如 "xlsx"；无扩展名返回空串
std::string FileExtensionOf(const std::string& fileName);

//把字节数格式化成便于阅读的大小文字（如 "1.8 MB"）。0 或负数返回空串。
//@param bytes  字节数
//@return 大小文字（UTF-8）
std::string FormatFileSize(int64_t bytes);

//时间下拉选中项下标换算成 HistoryTimeRange。两者是一一对应的（0 全部 / 1 近7天 /
//2 近30天 / 3 近3个月 / 4 近半年 / 5 近一年 / 6 自定义），越界一律退回“全部时间”。
//@param comboIndex  下拉项下标
//@return 对应时间范围；越界按“全部时间”
HistoryTimeRange TimeRangeFromComboIndex(int comboIndex);

} // namespace balloonhistory
