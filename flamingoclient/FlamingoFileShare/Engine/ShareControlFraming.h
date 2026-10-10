/**
 *  文件共享访问会话控制通道（lane 0）的应用帧编解码（纯逻辑），ShareControlFraming.h
 *
 *  访问会话的控制通道经 fileserver 中转（cmd_folder_relay_data），服务端只按会话配对原样转发、不解析包体；
 *  包体内部是本模块定义的应用帧（docs/文件共享方案.md 5.3）。字节布局与文件夹传输的应用帧完全相同：
 *      [1 字节 帧类型 share_frame_type]
 *      [4 字节 大端 描述符长度][描述符（紧凑 UTF-8 JSON）]
 *      [二进制附件（只有 sf_thumb_result 带 JPEG 数据，其余帧必须为空）]
 *  后两段直接复用 net::encodeAttachmentBody / net::decodeAttachmentBody。传输子会话同样经本模块编解码
 *  首帧 st_job 与分享者的应答 st_job_ack，其后是文件夹传输的 ft_* 帧；帧类型取值与 ft_*（1~9）错开，同一通道上可以按首字节区分。
 *
 *  所有帧共用一个消息结构 ShareControlMessage，按帧类型取用其中的字段；BuildFrame 编码，DecodeFrame 解码并
 *  按类型检查必填字段。列目录与搜索的结果可能很大，由 BuildListResultFrames / BuildSearchResultFrames 按条数
 *  与单帧上限拆成多帧。
 *
 *  本模块只依赖标准库、vendored 的 jsoncpp 与同为纯逻辑的 net/FrameCodec，不碰 socket、不碰 UI、不打日志。
 *  单元测试见 FlamingoFileShare/Tests/ShareControlFramingTests.cpp（标签 [shareframe]）。
 *
 *  典型用法：
 *      ShareControlMessage req;
 *      req.m_type = sf_list;
 *      req.m_reqId = nextReqId++;
 *      req.m_path = "docs";
 *      std::string body;
 *      if (ShareControlFraming::BuildFrame(req, body))
 *          ...（作为 cmd_folder_relay_data 的包体经 lane 0 发出）
 *
 *      ShareControlMessage msg;
 *      if (ShareControlFraming::DecodeFrame(body.data(), body.size(), msg))
 *          switch (msg.m_type) { ... }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace fileshare {

//控制通道单帧包体上限，单位：字节。与文件夹传输的 kFolderRelayMaxBodyBytes、fileserver 的包体上限一致（2 MiB）
const size_t kShareControlMaxFrameBytes = 2u * 1024u * 1024u;

//列目录与搜索结果每帧最多带的条目数（方案 5.3 的 kShareListBatch）。同时还受单帧字节上限约束
const size_t kShareListBatch = 500;

//控制通道应用帧的协议版本，写在 sf_hello 里；双方版本不一致时访问者提示升级
const int kShareControlVersion = 1;

//应用帧首字节：帧类型。取值从 32 起编，与文件夹传输的 folder_frame_type（ft_* 取 1~9）错开
enum share_frame_type
{
    sf_invalid       = 0,    //非法值，DecodeFrame 遇到即失败
    sf_hello         = 32,   //分享者 → 访问者：握手后首帧，带版本、共享类型、根名、是否含隐藏文件、权限
    sf_list          = 33,   //访问者 → 分享者：列一个目录
    sf_list_result   = 34,   //分享者 → 访问者：一批目录条目；more 为真时访问者带 cursor 取下一批
    sf_stat          = 35,   //访问者 → 分享者：查一个条目（单文件共享点开卡片时取文件名与大小）
    sf_stat_result   = 36,   //分享者 → 访问者：sf_stat 的结果（方案 5.3 表格未列出，第 11 步补充）
    sf_search        = 37,   //访问者 → 分享者：在某个目录之下按文件名搜索
    sf_search_result = 38,   //分享者 → 访问者：一批搜索命中，done 为真表示结束
    sf_cancel        = 39,   //访问者 → 分享者：取消一个尚未完成的搜索或缩略图请求
    sf_thumb         = 40,   //访问者 → 分享者：取一张图片的缩略图或预览图
    sf_thumb_result  = 41,   //分享者 → 访问者：缩略图结果，二进制附件为 JPEG 数据
    sf_mkdir         = 42,   //访问者 → 分享者：新建文件夹
    sf_rename        = 43,   //访问者 → 分享者：重命名一个条目
    sf_delete        = 44,   //访问者 → 分享者：删除若干条目（放进回收站）
    sf_move          = 45,   //访问者 → 分享者：把若干条目移到共享内的另一个目录
    sf_op_result     = 46,   //分享者 → 访问者：写操作的结果，多条目操作逐条给出
    sf_perms         = 47,   //分享者 → 访问者：会话中途权限变化
    sf_bye           = 48,   //双向：主动结束访问会话前的最后一帧
    st_job           = 64,   //访问者 → 分享者：传输子会话 lane 0 的首帧，说明本次下载或上传的内容；收到 st_job_ack 之前定时重发
    st_job_ack       = 65,   //分享者 → 访问者：对 st_job 的应答，code 为 0 时双方开始交换清单，否则传输以 code 失败（第 14 步补充）
};

//一个目录条目（sf_list_result、sf_stat_result）或一条搜索命中（sf_search_result）
struct ShareEntry
{
    std::string m_name;    //列目录与查条目时为条目名；搜索命中时为相对共享根的路径（'/' 分隔）
    bool        m_isDir;   //是否为目录
    int64_t     m_size;    //字节数；目录为 0
    int64_t     m_mtime;   //修改时间（Unix 毫秒）
    int         m_attrs;   //文件属性（Windows 的 FILE_ATTRIBUTE_*，只取只读、隐藏、系统几位）；搜索命中不带，为 0

    ShareEntry()
        : m_isDir(false)
        , m_size(0)
        , m_mtime(0)
        , m_attrs(0)
    {
    }
};

//写操作结果里的一条（sf_op_result 的 items）
struct ShareOpItem
{
    std::string m_path;   //相对共享根的路径
    int         m_code;   //这一条的结果：0 成功，否则为错误码

    ShareOpItem()
        : m_code(0)
    {
    }
};

//一个控制通道应用帧的全部内容。各帧类型用到的字段见 share_frame_type 与方案 5.3，未用到的字段保持默认值
struct ShareControlMessage
{
    int                      m_type;           //帧类型，取值见 share_frame_type
    int64_t                  m_reqId;          //请求号：访问者在本访问会话内递增分配，应答原样带回
    int                      m_code;           //结果码（各 *_result 帧）：0 成功，否则为错误码
    std::string              m_path;           //相对共享根的路径（列目录、查条目、搜索、缩略图、新建、重命名）
    std::string              m_cursor;         //列目录的分页游标；首批为空串
    std::string              m_keyword;        //搜索关键字
    std::string              m_newName;        //重命名的新名字
    std::string              m_toDir;          //移动的目标目录（sf_move），或上传的目标目录（st_job）
    std::string              m_rootName;       //共享根的显示名（sf_hello）
    std::vector<std::string> m_paths;          //多条目操作的路径（sf_delete、sf_move），或下载的条目（st_job）
    std::vector<ShareEntry>  m_entries;        //目录条目或搜索命中；sf_stat_result 只有一条
    std::vector<ShareOpItem> m_items;          //写操作的逐条结果（sf_op_result）
    bool                     m_more;           //sf_list_result：是否还有下一批
    bool                     m_done;           //sf_search_result：搜索是否已结束
    bool                     m_truncated;      //sf_search_result：是否因条数上限或超时而截断
    bool                     m_includeHidden;  //sf_hello：共享是否包含隐藏文件与系统文件
    int                      m_version;        //sf_hello：协议版本（kShareControlVersion）
    int                      m_kind;           //sf_hello：共享类型，取值见 share_kind
    int                      m_perms;          //sf_hello、sf_perms：访问者的写权限位
    int                      m_maxEdge;        //sf_thumb：缩略图长边像素数
    bool                     m_matchAll;       //sf_search：为真时列出目录之下的全部条目（关键字不用），拖出目录时取整棵目录树用
    int                      m_maxItems;       //sf_search：访问者要求的条数上限（只在 m_matchAll 为真时有效；0 表示按分享者的默认）
    int                      m_width;          //sf_thumb_result：图片宽度（像素）
    int                      m_height;         //sf_thumb_result：图片高度（像素）
    int                      m_direction;      //st_job：传输方向，1 下载 2 上传
    int                      m_errCode;        //sf_bye：结束原因，0 为正常关闭
    std::string              m_binary;         //sf_thumb_result 的 JPEG 数据；其余帧为空

    ShareControlMessage()
        : m_type(sf_invalid)
        , m_reqId(0)
        , m_code(0)
        , m_more(false)
        , m_done(false)
        , m_truncated(false)
        , m_includeHidden(false)
        , m_version(0)
        , m_kind(0)
        , m_perms(0)
        , m_maxEdge(0)
        , m_matchAll(false)
        , m_maxItems(0)
        , m_width(0)
        , m_height(0)
        , m_direction(0)
        , m_errCode(0)
    {
    }
};

/**
 *  ShareControlFraming —— 控制通道应用帧的编解码；全部为静态方法，不可实例化。
 */
class ShareControlFraming
{
public:
    /**
     *  编码一个应用帧。只写出该帧类型用到的字段；除 sf_thumb_result 外附件必须为空。
     *  @param msg 帧内容；m_type 必须是 share_frame_type 里的合法取值
     *  @param out [出参] 帧包体
     *  @return 成功返回 true；类型非法、非缩略图帧带了附件、或包体超过 kShareControlMaxFrameBytes 时返回 false
     */
    static bool BuildFrame(const ShareControlMessage& msg, std::string& out);

    /**
     *  解码一个应用帧，并按类型检查必填字段与取值。截断、长度越界、类型未知、JSON 非法、缺必填字段、字段类型
     *  不对、非缩略图帧带附件，一律返回 false，不抛异常。
     *  @param body 帧包体（借用）
     *  @param len  包体字节数
     *  @param msg  [出参] 帧内容；返回 false 时内容未定义
     *  @return 合法返回 true
     */
    static bool DecodeFrame(const char* body, size_t len, ShareControlMessage& msg);

    /**
     *  把一个目录的全部条目拆成若干个 sf_list_result 帧：每帧不超过 kShareListBatch 条，也不超过
     *  kShareControlMaxFrameBytes；除最后一帧外 more 都为真，cursor 依次为已发出的条目数（十进制文本）。
     *  @param reqId   请求号
     *  @param entries 全部条目
     *  @param frames  [出参] 帧包体，按顺序发出；条目为空时也产出一帧（空列表、more 为假）
     *  @return 成功返回 true；某一条目单独成帧仍超过上限时返回 false
     */
    static bool BuildListResultFrames(int64_t reqId, const std::vector<ShareEntry>& entries,
                                      std::vector<std::string>& frames);

    /**
     *  从第 start 条起构造一个 sf_list_result 帧（列目录的拉取方式：访问者首次不带游标，之后每次带上一帧的游标
     *  取下一批）。分批规则与 BuildListResultFrames 相同，因此从 0 起逐批取下去得到的帧与它一次产出的完全相同。
     *  @param reqId   请求号
     *  @param entries 目录的全部条目
     *  @param start   本批第一条的下标；等于条目数时产出空列表、more 为假的一帧
     *  @param frame   [出参] 帧包体
     *  @param next    [出参] 本批之后下一条的下标，也就是写进本帧的游标；等于条目数时 more 为假
     *  @return 成功返回 true；start 超过条目数、或某一条目单独成帧仍超过上限时返回 false
     */
    static bool BuildListResultBatch(int64_t reqId, const std::vector<ShareEntry>& entries, size_t start,
                                     std::string& frame, size_t& next);

    /**
     *  解析 sf_list 的游标。
     *  @param cursor 游标文本：空串表示从头列，否则为十进制条目下标（只含数字，不超过 9 位）
     *  @param offset [出参] 条目下标；空串为 0
     *  @return 合法返回 true
     */
    static bool ParseListCursor(const std::string& cursor, size_t& offset);

    /**
     *  把一批搜索命中拆成若干个 sf_search_result 帧，规则同 BuildListResultFrames；最后一帧的 done 取 finished，
     *  truncated 取 truncated，前面各帧 done 为假。
     *  @param reqId     请求号
     *  @param hits      搜索命中（ShareEntry::m_name 为相对路径）
     *  @param finished  搜索是否已结束（为假时表示边搜边回的中间一批）
     *  @param truncated 是否因条数上限或超时而截断
     *  @param frames    [出参] 帧包体
     *  @return 成功返回 true；某一条命中单独成帧仍超过上限时返回 false
     */
    static bool BuildSearchResultFrames(int64_t reqId, const std::vector<ShareEntry>& hits, bool finished,
                                        bool truncated, std::vector<std::string>& frames);

private:
    ShareControlFraming() = delete;
};

} // namespace fileshare
