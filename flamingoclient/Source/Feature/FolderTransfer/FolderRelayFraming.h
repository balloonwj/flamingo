/**
 *  文件夹传输「应用帧」编解码（纯逻辑），FolderRelayFraming.h
 *
 *  文件夹传输的字节走 fileserver 的中转通道（cmd_folder_relay_data），服务端只按
 *  会话配对原样转发、不解析包体；包体内部是本模块定义的这一层「应用帧」。方案与
 *  帧语义见 docs/单聊文件夹传输方案.md 第 4.2、5.2、5.3 节。
 *
 *  应用帧的字节布局（即中转数据帧的包体）：
 *      [1 字节 帧类型 folder_frame_type]
 *      [4 字节 大端 描述符长度][描述符（紧凑 UTF-8 JSON）]
 *      [二进制附件（仅 ft_file_chunk 有，其余帧必须为空）]
 *  其中后两段沿用 v2 协议既有的「描述符 + 二进制附件」布局（docs/协议设计-v2.md
 *  §11.1），直接复用 net::encodeAttachmentBody / net::decodeAttachmentBody，不另起
 *  一套长度域格式。
 *
 *  一份文件清单在文件数很多时会超过中转单帧上限，故 ft_manifest 支持分段：描述符
 *  里带「本段序号 seg / 总段数 segs」以及「本段第一条目录 / 文件在整份清单中的序号
 *  dirbase / filebase」，发送端用 SplitManifest + BuildManifestFrames 拆分，接收端用
 *  FolderRelayManifestAssembler 逐段重组并校验连续性。其余帧都不分段，构造时超出
 *  单帧上限一律返回 false，由调用方自行减小分片。
 *
 *  典型用法（发送端）：
 *      std::vector<std::string> frames;
 *      relayframing::BuildManifestFrames(name, dirs, files,
 *                                        kFolderRelayManifestSegmentBytes, frames);
 *      // frames 逐个作为 cmd_folder_relay_data 的包体发出（控制通道）
 *
 *      std::string body;
 *      relayframing::BuildFileChunkFrame(index, offset, data, len, body);
 *
 *  典型用法（接收端）：
 *      FolderRelayInboundFrame f;
 *      if (relayframing::DecodeFrame(body, bodylen, f))
 *          switch (f.m_type) { ... }
 *
 *  本模块是纯逻辑：只依赖标准库、仓库内 vendored 的 jsoncpp 与同为纯逻辑的
 *  net/FrameCodec，不碰 socket、不碰 UI、不打日志（日志由调用方在通道层输出），
 *  便于单元测试，见 Source/Tests/FolderRelayFramingTests.cpp。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace foldertransfer {

//==================== 常量 ====================

//中转单帧包体上限，单位：字节。取 2 MiB，与 Source/net/Protocol.h 的
//MAX_BODY_FILESERVER 一致（服务端 fileserver 按同一上限解帧，超限即断连）；
//本头文件不包含 net/Protocol.h，是为了不把「不得与旧版 Msg.h 同处一个编译单元」
//这条约束传染给使用方 —— v1 协议头已于 2026-08-14 删除、该约束已消失，现在的理由
//只剩「使用方多为绘制侧，用不上协议定义」。两处取值的一致性由
//FolderRelayFraming.cpp 里的 static_assert 保证。
//出处另见 docs/单聊文件夹传输方案.md 5.3。
const size_t kFolderRelayMaxBodyBytes = 2u * 1024u * 1024u;

//文件分片的数据字节数：512 KiB。与远程桌面中转（RdRelayClient.cpp 的
//kRelayChunkSize）以及文件上传下载（net/Protocol.h 的 FILE_CHUNK_SIZE）同口径，
//四片在途正好占满 2 MiB 的流控窗口（见方案 5.3 末段）。
const size_t kFolderRelayChunkBytes = 512u * 1024u;

//清单单个分段的描述符字节预算：512 KiB。远小于单帧上限，留足余量给分段头字段
//与 JSON 转义带来的膨胀；调低它只会让分段变多，不会影响正确性。
const size_t kFolderRelayManifestSegmentBytes = 512u * 1024u;

//应用帧的固定头部字节数：1 字节帧类型 + 4 字节大端描述符长度。
const size_t kFolderRelayFrameHeadBytes = 5;

//==================== 帧类型 ====================

//应用帧首字节：帧类型。取值与 docs/单聊文件夹传输方案.md 5.3 的表格一一对应。
enum folder_frame_type
{
    //非法值：解码失败或结构未初始化时取此值，任何一端都不得发出该类型的帧
    ft_invalid = 0,
    //清单（发→收，控制通道）：文件夹名、目录相对路径列表、文件列表与总计；文件多时分多段发
    ft_manifest = 1,
    //清单回执（收→发，控制通道）：接收端把目录全部建好之后才发，带每个文件的续传起点
    ft_manifest_ack = 2,
    //文件开始（发→收，数据通道）：文件序号、字节数、内容 MD5、本次起传偏移
    ft_file_begin = 3,
    //分片（发→收，数据通道）：文件序号、偏移与本片字节数，描述符之后跟二进制附件
    ft_file_chunk = 4,
    //分片回执（收→发，数据通道）：文件序号与下一片应从哪个偏移开始，是权威续传位点
    ft_chunk_ack = 5,
    //文件结束（发→收，数据通道）：文件序号，标记该文件字节已发完
    ft_file_end = 6,
    //文件结果（收→发，数据通道）：文件序号、落盘与 MD5 校验的结果码与描述
    ft_file_result = 7,
    //中止（双向，控制通道）：任一端出错即发，带错误码与错误描述，收到即整体失败
    ft_abort = 8,
    //完成（发→收，控制通道）：全部文件传完，可带一份汇总（文件数与字节数）
    ft_complete = 9,
};

//==================== 各帧的描述符结构 ====================

//清单中的一个文件条目：相对路径与字节数。
struct FolderRelayFileEntry
{
    std::string m_relpath;   //相对根文件夹的路径，一律用 '/' 分隔，不含根文件夹名本身
    int64_t     m_size;      //文件字节数，取值 >= 0
    int64_t     m_mtime;     //最后修改时间（Unix 毫秒）；0 表示未知，此时清单里不写这个字段。单聊文件夹传输的接收端
                             //不使用它，文件共享的下载据它判断半截文件能否续传

    FolderRelayFileEntry()
        : m_size(0)
        , m_mtime(0)
    {
    }
};

//ft_manifest 的一个分段。整份清单被顺序切成 m_segcount 段，目录列表在前、文件
//列表在后，同一条目不会跨段；m_dirbase / m_filebase 给出本段首条目录 / 首个文件
//在整份清单中的序号，接收端据此校验分段连续、不重不漏。
//文件序号（后续 ft_file_begin / ft_file_chunk 等帧里的 index）即整份清单文件列表
//的下标，与分段方式无关。
struct FolderRelayManifestSegment
{
    std::string                       m_foldername;   //根文件夹名，每段都带，供接收端在任一段上核对
    int64_t                           m_totaldirs;    //整份清单的目录条数（不是本段的）
    int64_t                           m_totalfiles;   //整份清单的文件条数（不是本段的）
    int64_t                           m_totalsize;    //整份清单全部文件的字节数合计
    int64_t                           m_segindex;     //本段序号，从 0 起，须 < m_segcount
    int64_t                           m_segcount;     //总段数，>= 1
    int64_t                           m_dirbase;      //本段第一条目录在整份目录列表中的序号
    int64_t                           m_filebase;     //本段第一个文件在整份文件列表中的序号
    std::vector<std::string>          m_dirs;         //本段携带的目录相对路径（含空目录）
    std::vector<FolderRelayFileEntry> m_files;        //本段携带的文件条目

    FolderRelayManifestSegment()
        : m_totaldirs(0)
        , m_totalfiles(0)
        , m_totalsize(0)
        , m_segindex(0)
        , m_segcount(1)   //默认按单段处理，避免忘记赋值时被判为 0 段
        , m_dirbase(0)
        , m_filebase(0)
    {
    }
};

//ft_manifest_ack 里的一个续传位点：某个文件已在接收端落盘的字节数。
struct FolderRelayResumePoint
{
    int64_t m_index;    //文件序号（整份清单文件列表的下标，从 0 起）
    int64_t m_offset;   //该文件已落盘字节数，发送端从此偏移续传；0 表示从头传

    FolderRelayResumePoint()
        : m_index(0)
        , m_offset(0)
    {
    }
};

//ft_manifest_ack 的描述符：接收端建完目录后回给发送端的续传起点集合。
//为控制帧长，位点表是稀疏的——未出现在 m_points 里的文件一律按偏移 0 处理，
//故首次传输（全部从 0 开始）时 m_points 为空即可。
struct FolderRelayManifestAck
{
    int64_t                             m_totalfiles;   //接收端确认的文件总数，供发送端与清单核对
    std::vector<FolderRelayResumePoint> m_points;       //续传位点，仅列出偏移非 0 的文件

    FolderRelayManifestAck()
        : m_totalfiles(0)
    {
    }
};

//ft_file_begin 的描述符：一个文件开始传输前的元信息。
struct FolderRelayFileBegin
{
    int64_t     m_index;    //文件序号（整份清单文件列表的下标）
    int64_t     m_size;     //文件总字节数
    std::string m_md5;      //文件内容 MD5，32 位小写十六进制
    int64_t     m_offset;   //本次起传偏移；续传时为接收端给出的位点，首次为 0

    FolderRelayFileBegin()
        : m_index(0)
        , m_size(0)
        , m_offset(0)
    {
    }
};

//ft_file_chunk 的描述符：一片文件字节的定位信息，其后跟同样长度的二进制附件。
struct FolderRelayFileChunk
{
    int64_t m_index;     //文件序号
    int64_t m_offset;    //本片在文件中的起始偏移
    int64_t m_datalen;   //本片二进制附件的字节数；解码时与实际附件长度逐字节核对，不符即失败

    FolderRelayFileChunk()
        : m_index(0)
        , m_offset(0)
        , m_datalen(0)
    {
    }
};

//ft_chunk_ack 的描述符：接收端给出的权威续传位点，兼作流控回执。
struct FolderRelayChunkAck
{
    int64_t m_index;        //文件序号
    int64_t m_nextoffset;   //下一片应从哪个偏移开始发；发送端必须以此为准，不能自行累加

    FolderRelayChunkAck()
        : m_index(0)
        , m_nextoffset(0)
    {
    }
};

//ft_file_end 的描述符：标记某个文件的字节已全部发完。
struct FolderRelayFileEnd
{
    int64_t m_index;   //文件序号

    FolderRelayFileEnd()
        : m_index(0)
    {
    }
};

//ft_file_result 的描述符：接收端对某个文件落盘与校验的结果。
struct FolderRelayFileResult
{
    int64_t     m_index;   //文件序号
    int         m_code;    //结果码，0 表示该文件成功，非 0 取值见 Protocol.h 的 error_code
    std::string m_msg;     //结果描述，成功时为 "ok"，失败时为便于排查的诊断信息

    FolderRelayFileResult()
        : m_index(0)
        , m_code(0)
    {
    }
};

//ft_abort 的描述符：任一端主动中止本次传输时携带的失败原因。
struct FolderRelayAbort
{
    int         m_errcode;   //错误码，取值见 Protocol.h 的 error_code；0 保留给「未指明原因」
    std::string m_errmsg;    //错误描述，双方界面上显示的同一条原因文本

    FolderRelayAbort()
        : m_errcode(0)
    {
    }
};

//ft_complete 的描述符：全部文件传完时的汇总，两个字段都允许填 0 表示不带汇总。
struct FolderRelayComplete
{
    int64_t m_totalfiles;   //本次实际传完的文件数
    int64_t m_totalsize;    //本次实际传输的字节数

    FolderRelayComplete()
        : m_totalfiles(0)
        , m_totalsize(0)
    {
    }
};

//解码一个应用帧得到的完整结果。m_type 决定其余哪个成员有效，其它成员保持默认值；
//C++11 无 std::variant，故按仓库既有风格用平铺结构体承载，读的人一眼能看到全部帧型。
struct FolderRelayInboundFrame
{
    folder_frame_type          m_type;         //帧类型；ft_invalid 表示未解出
    FolderRelayManifestSegment m_manifest;     //m_type == ft_manifest 时有效
    FolderRelayManifestAck     m_manifestAck;  //m_type == ft_manifest_ack 时有效
    FolderRelayFileBegin       m_fileBegin;    //m_type == ft_file_begin 时有效
    FolderRelayFileChunk       m_fileChunk;    //m_type == ft_file_chunk 时有效
    std::vector<unsigned char> m_chunkData;    //m_type == ft_file_chunk 时有效，即该片二进制附件
    FolderRelayChunkAck        m_chunkAck;     //m_type == ft_chunk_ack 时有效
    FolderRelayFileEnd         m_fileEnd;      //m_type == ft_file_end 时有效
    FolderRelayFileResult      m_fileResult;   //m_type == ft_file_result 时有效
    FolderRelayAbort           m_abort;        //m_type == ft_abort 时有效
    FolderRelayComplete        m_complete;     //m_type == ft_complete 时有效

    FolderRelayInboundFrame()
        : m_type(ft_invalid)
    {
    }
};

namespace relayframing {

//==================== 编码 ====================

/**
 *  把一个清单分段封成一个 ft_manifest 应用帧。
 *  一般不直接调用，而是用 SplitManifest / BuildManifestFrames 完成拆分与封帧；
 *  单独导出是为了让调用方能对手工构造的分段封帧（单元测试即如此用）。
 *  @param seg 待封帧的分段；m_segcount 须 >= 1 且 m_segindex < m_segcount，
 *             m_dirbase / m_filebase 须为本段在整份清单中的起始序号
 *  @param out [出参] 封好的应用帧字节；函数内部**先清空**再写入
 *  @return 成功返回 true；分段字段自相矛盾、或封出的帧超过单帧上限返回 false（out 被清空）
 */
bool BuildManifestFrame(const FolderRelayManifestSegment& seg, std::string& out);

/**
 *  把一份完整清单按描述符字节预算切成若干分段（目录列表在前、文件列表在后，
 *  同一条目不跨段）。切分只依据描述符的估算长度，估算按 jsoncpp 实际序列化的
 *  条目文本长度累加，故不会因中文路径的 \uXXXX 转义而低估。
 *  @param foldername         根文件夹名（可为空串，接收端另有重名处理）
 *  @param dirs               目录相对路径列表（含空目录），顺序即接收端建目录的顺序
 *  @param files              文件条目列表，下标即后续各帧里的文件序号
 *  @param segmentBudgetBytes 单段描述符的字节预算，须大于分段头部开销（典型取
 *                            kFolderRelayManifestSegmentBytes；调小只会让段数变多）
 *  @param outSegments        [出参] 切出的分段，按序发送；函数内部**先清空**再写入。
 *                            清单为空（无目录也无文件）时产出恰好一个空分段
 *  @return 成功返回 true；预算过小容不下分段头部时返回 false（outSegments 被清空）
 */
bool SplitManifest(const std::string& foldername,
                   const std::vector<std::string>& dirs,
                   const std::vector<FolderRelayFileEntry>& files,
                   size_t segmentBudgetBytes,
                   std::vector<FolderRelayManifestSegment>& outSegments);

/**
 *  把一份完整清单切分并逐段封成 ft_manifest 应用帧，等价于 SplitManifest 之后
 *  对每段调用 BuildManifestFrame。
 *  @param foldername         根文件夹名
 *  @param dirs               目录相对路径列表
 *  @param files              文件条目列表
 *  @param segmentBudgetBytes 单段描述符的字节预算，含义同 SplitManifest
 *  @param outFrames          [出参] 每项是一个完整应用帧，须按序发出；**先清空**再写入
 *  @return 成功返回 true；切分失败、或任一段封帧后仍超过单帧上限返回 false（outFrames 被清空）
 */
bool BuildManifestFrames(const std::string& foldername,
                         const std::vector<std::string>& dirs,
                         const std::vector<FolderRelayFileEntry>& files,
                         size_t segmentBudgetBytes,
                         std::vector<std::string>& outFrames);

/**
 *  封 ft_manifest_ack 帧（接收端建完目录后回给发送端）。
 *  @param ack 续传位点集合；m_points 只需列出偏移非 0 的文件
 *  @param out [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；帧超过单帧上限返回 false（位点太多时应减少条目）
 */
bool BuildManifestAckFrame(const FolderRelayManifestAck& ack, std::string& out);

/**
 *  封 ft_file_begin 帧。
 *  @param info 文件元信息；m_index / m_size / m_offset 须 >= 0，m_md5 为 32 位小写十六进制
 *  @param out  [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；字段为负或帧超限返回 false
 */
bool BuildFileBeginFrame(const FolderRelayFileBegin& info, std::string& out);

/**
 *  封 ft_file_chunk 帧：描述符之后紧跟本片二进制附件。
 *  描述符里的 len 字段由本函数按 len 参数写入，调用方无须、也无法与之不一致。
 *  @param index  文件序号，须 >= 0
 *  @param offset 本片在文件中的起始偏移，须 >= 0
 *  @param data   本片数据起始地址；len 为 0 时可为 nullptr
 *  @param len    本片字节数；典型取 kFolderRelayChunkBytes
 *  @param out    [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；index / offset 为负，或「5 + 描述符 + 本片」超过
 *          kFolderRelayMaxBodyBytes 时返回 false（out 被清空）
 */
bool BuildFileChunkFrame(int64_t index, int64_t offset,
                         const unsigned char* data, size_t len,
                         std::string& out);

/**
 *  封 ft_chunk_ack 帧。
 *  @param ack 文件序号与下一片起始偏移，两者须 >= 0
 *  @param out [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；字段为负返回 false
 */
bool BuildChunkAckFrame(const FolderRelayChunkAck& ack, std::string& out);

/**
 *  封 ft_file_end 帧。
 *  @param index 文件序号，须 >= 0
 *  @param out   [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；index 为负返回 false
 */
bool BuildFileEndFrame(int64_t index, std::string& out);

/**
 *  封 ft_file_result 帧。
 *  @param result 文件序号（须 >= 0）、结果码与结果描述
 *  @param out    [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；index 为负或帧超限返回 false
 */
bool BuildFileResultFrame(const FolderRelayFileResult& result, std::string& out);

/**
 *  封 ft_abort 帧。
 *  @param abort 错误码与错误描述；描述会原样显示给双方用户，故不应为空
 *  @param out   [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；帧超限（错误描述过长）返回 false
 */
bool BuildAbortFrame(const FolderRelayAbort& abort, std::string& out);

/**
 *  封 ft_complete 帧。
 *  @param summary 汇总信息；两个字段都填 0 即表示不带汇总
 *  @param out     [出参] 封好的应用帧字节；**先清空**再写入
 *  @return 成功返回 true；字段为负返回 false
 */
bool BuildCompleteFrame(const FolderRelayComplete& summary, std::string& out);

//==================== 解码 ====================

/**
 *  只取应用帧的帧类型，不解析描述符。用于按帧类型分流到不同处理路径而暂不关心内容。
 *  @param body    应用帧字节起始地址
 *  @param bodylen 应用帧字节数
 *  @param outType [出参] 解出的帧类型；返回 false 时置 ft_invalid
 *  @return 成功返回 true；body 为空、长度不足 1 字节、类型未知返回 false
 */
bool PeekFrameType(const unsigned char* body, size_t bodylen, folder_frame_type& outType);

/**
 *  解一个完整应用帧。对长度不足、描述符长度越界、JSON 非法、帧类型未知、
 *  非分片帧却带了多余附件字节、分片帧的附件长度与描述符 len 不符等情形，
 *  一律返回 false，绝不越界读取。
 *  @param body    应用帧字节起始地址
 *  @param bodylen 应用帧字节数；超过 kFolderRelayMaxBodyBytes 直接判失败
 *  @param out     [出参] 解码结果；先被重置为初始状态，成功时 m_type 与对应
 *                 成员有效（ft_file_chunk 时 m_fileChunk 与 m_chunkData 同时有效）
 *  @return 成功返回 true；任一项校验不通过返回 false（out 保持初始状态）
 */
bool DecodeFrame(const unsigned char* body, size_t bodylen, FolderRelayInboundFrame& out);

/**
 *  只解 ft_file_chunk 帧，并且**不拷贝**附件二进制——回传其在 body 中的起始偏移，
 *  由调用方按 (body + outDataOffset, outInfo.m_datalen) 直接写盘。接收端的热路径
 *  应当用这个版本，避免每片 512 KiB 多一次内存拷贝。
 *  @param body          应用帧字节起始地址
 *  @param bodylen       应用帧字节数
 *  @param outInfo       [出参] 分片描述符；m_datalen 即附件字节数
 *  @param outDataOffset [出参] 附件二进制在 body 中的起始偏移
 *  @return 成功返回 true；帧类型不是 ft_file_chunk、或任一项校验不通过返回 false
 */
bool DecodeFileChunk(const unsigned char* body, size_t bodylen,
                     FolderRelayFileChunk& outInfo, size_t& outDataOffset);

} // namespace relayframing

//清单分段重组器：接收端按到达顺序逐段喂入 ft_manifest 解出的分段，重组出整份清单。
//每次文件夹传输的接收端持有一个本对象；重试续传时应先 Reset 再重新收清单。
//重组过程中会校验分段的自洽性（段数与文件夹名各段一致、dirbase / filebase 与已累积的条数吻合、
//收齐后条数与总计吻合），任一项不符即返回错误并重置，避免把一份残缺的清单当成完整清单用下去。
//发送端在回执到达前会重发整份清单（2026-10-05 起），故段序号的问题不算错误：0 号段总是从头
//开始一份；缺了开头的一份、跳号或重复的一份丢弃后等下一份；收齐之后多余的整份丢弃（folder_manifest_ignored）。
class FolderRelayManifestAssembler
{
public:
    FolderRelayManifestAssembler();

    //Feed 的返回码。
    enum FeedResult
    {
        //本段已收下，还缺后续分段，应继续等待
        folder_manifest_need_more = 0,
        //已收齐全部分段且各项校验通过，可用 GetXxx 取整份清单
        folder_manifest_done = 1,
        //分段不连续或字段自相矛盾，内部状态已重置，调用方应中止本次传输
        folder_manifest_error = -1,
        //本段来自一份不完整或多余的清单，已丢弃，调用方继续等待即可（2026-10-05）：
        //已经收齐之后又来的整份清单（发送端在回执到达前重发的），或开头几段被 fileserver 丢弃的那一份的后续段
        folder_manifest_ignored = 2,
    };

    /**
     *  喂入一个清单分段（DecodeFrame 解出的 m_manifest）。
     *  @param seg 待重组的分段
     *  @return 见 enum FeedResult
     */
    FeedResult Feed(const FolderRelayManifestSegment& seg);

    //清空重组状态（新一次传输开始、或收到中止帧时调用）。
    void Reset();

    //是否已收齐全部分段。收齐前 GetXxx 返回的是不完整的中间结果。
    bool IsComplete() const;

    //根文件夹名（取自第一段；未收到任何分段时为空串）。
    const std::string& GetFolderName() const;

    //整份清单的目录相对路径列表，顺序与发送端一致。
    const std::vector<std::string>& GetDirs() const;

    //整份清单的文件条目列表，下标即后续各帧里的文件序号。
    const std::vector<FolderRelayFileEntry>& GetFiles() const;

    //整份清单全部文件的字节数合计（取自分段里的总计字段，收齐时已与逐条累加值核对过）。
    int64_t GetTotalSize() const;

private:
    bool                              m_started;      //是否已收到第一段（用于区分"尚未开始"与"已开始"）
    bool                              m_complete;     //是否已收齐全部分段
    int64_t                           m_segcount;     //总段数，取自第一段
    int64_t                           m_nextseg;      //下一个期望收到的段序号，从 0 起递增
    std::string                       m_foldername;   //根文件夹名，取自第一段，后续段须一致
    int64_t                           m_totaldirs;    //目录总数，取自第一段，后续段须一致
    int64_t                           m_totalfiles;   //文件总数，取自第一段，后续段须一致
    int64_t                           m_totalsize;    //字节数合计，取自第一段，后续段须一致
    std::vector<std::string>          m_dirs;         //已累积的目录相对路径
    std::vector<FolderRelayFileEntry> m_files;        //已累积的文件条目
};

} // namespace foldertransfer
