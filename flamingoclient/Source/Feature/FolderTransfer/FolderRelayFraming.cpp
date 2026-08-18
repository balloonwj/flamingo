/**
 *  文件夹传输「应用帧」编解码（纯逻辑）实现，FolderRelayFraming.cpp
 *  balloonwj@qq.com   2026-08-13
 */
#include "FolderRelayFraming.h"

#include <stdlib.h>              // atoll
#include "../../net/FrameCodec.h"   // encodeAttachmentBody / decodeAttachmentBody
#include "../../net/Protocol.h"     // MAX_BODY_FILESERVER / FILE_CHUNK_SIZE
#include "../../third-party/jsoncpp-1.9.0/json.h"

namespace foldertransfer {

//头文件里为了不传染 net/Protocol.h 的包含约束而另立了两个常量，这里锁死它们与
//协议定义一致：一旦哪天协议侧调整了上限或分片大小而本模块没跟上，编译期即报错。
static_assert(kFolderRelayMaxBodyBytes == MAX_BODY_FILESERVER,
              "kFolderRelayMaxBodyBytes must match MAX_BODY_FILESERVER in net/Protocol.h");
static_assert(kFolderRelayChunkBytes == FILE_CHUNK_SIZE,
              "kFolderRelayChunkBytes must match FILE_CHUNK_SIZE in net/Protocol.h");

namespace relayframing {

//==================== JSON 小工具 ====================

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 文本解析成对象 Json::Value；非法 JSON 或顶层非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取 JSON 整型字段：整型直接取，字符串按十进制解析，其它（缺字段 / 类型不符）返回 0。
//对字符串做容错是沿用 net/FileProtocolClient.cpp 的既有做法，防止对端把大整数写成字符串。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
        return static_cast<int64_t>(atoll(v.asString().c_str()));
    return 0;
}

//==================== 帧头的封装与拆解 ====================

//把帧类型字节还原成枚举。取值连续（ft_manifest=1 .. ft_complete=9），故用区间判定，
//将来新增帧类型只需同步 ft_complete 这个上界。
static bool frameTypeFromByte(unsigned char v, folder_frame_type& out)
{
    if (v < static_cast<unsigned char>(ft_manifest) || v > static_cast<unsigned char>(ft_complete))
        return false;

    out = static_cast<folder_frame_type>(v);
    return true;
}

//按 [1 字节帧类型][4 字节大端描述符长度][描述符][附件] 拼出一个完整应用帧。
//out 先被清空；总长超过中转单帧上限时返回 false 并保持 out 为空。
static bool buildFrame(folder_frame_type type, const std::string& descriptor,
                       const unsigned char* data, size_t len, std::string& out)
{
    out.clear();

    //用 64 位累加判上限，避免 size_t 相加在 32 位构建下回绕导致漏判
    uint64_t total = static_cast<uint64_t>(kFolderRelayFrameHeadBytes)
                     + static_cast<uint64_t>(descriptor.size())
                     + static_cast<uint64_t>(len);
    if (total > kFolderRelayMaxBodyBytes)
        return false;

    out.reserve(static_cast<size_t>(total));
    out.push_back(static_cast<char>(type));
    //其余部分沿用 v2 协议的附件包体布局，直接复用既有编码函数
    if (!net::encodeAttachmentBody(descriptor, reinterpret_cast<const char*>(data),
                                   static_cast<uint32_t>(len), out))
    {
        out.clear();
        return false;
    }
    return true;
}

//把一个应用帧拆成「帧类型 + 描述符 + 附件位置」。附件不做拷贝，只回传其在 body 中
//的起始偏移与字节数。任何一项越界 / 非法都返回 false，不读取 body 范围之外的字节。
static bool splitFrame(const unsigned char* body, size_t bodylen,
                       folder_frame_type& outType, std::string& outDescriptor,
                       size_t& outDataOffset, size_t& outDataLen)
{
    if (body == nullptr || bodylen < kFolderRelayFrameHeadBytes)
        return false;

    //超过中转单帧上限的包体不可能是本模块封出来的，直接判非法，防御恶意大包
    if (bodylen > kFolderRelayMaxBodyBytes)
        return false;

    folder_frame_type type = ft_invalid;
    if (!frameTypeFromByte(body[0], type))
        return false;

    std::string descriptor;
    size_t binaryoff = 0;
    size_t binarylen = 0;
    if (!net::decodeAttachmentBody(reinterpret_cast<const char*>(body) + 1, bodylen - 1,
                                   descriptor, binaryoff, binarylen))
    {
        return false;
    }

    outType = type;
    outDescriptor.swap(descriptor);
    //binaryoff 是相对「帧类型字节之后」的偏移，换算回相对 body 起始的偏移
    outDataOffset = binaryoff + 1;
    outDataLen = binarylen;
    return true;
}

//==================== 描述符的构造 ====================

//把一个文件条目转成 JSON 对象 {"path":...,"size":...}
static Json::Value makeFileEntryValue(const FolderRelayFileEntry& entry)
{
    Json::Value v;
    v["path"] = entry.m_relpath;
    v["size"] = static_cast<Json::Int64>(entry.m_size);
    return v;
}

//构造 ft_manifest 分段的描述符 JSON 文本
static std::string buildManifestDescriptor(const FolderRelayManifestSegment& seg)
{
    Json::Value root;
    root["name"]       = seg.m_foldername;
    root["totaldirs"]  = static_cast<Json::Int64>(seg.m_totaldirs);
    root["totalfiles"] = static_cast<Json::Int64>(seg.m_totalfiles);
    root["totalsize"]  = static_cast<Json::Int64>(seg.m_totalsize);
    root["seg"]        = static_cast<Json::Int64>(seg.m_segindex);
    root["segs"]       = static_cast<Json::Int64>(seg.m_segcount);
    root["dirbase"]    = static_cast<Json::Int64>(seg.m_dirbase);
    root["filebase"]   = static_cast<Json::Int64>(seg.m_filebase);

    //两个数组即便为空也要写出来，让描述符的字段集合恒定，便于对端与估算逻辑处理
    Json::Value jdirs(Json::arrayValue);
    for (size_t i = 0; i < seg.m_dirs.size(); ++i)
        jdirs.append(seg.m_dirs[i]);
    root["dirs"] = jdirs;

    Json::Value jfiles(Json::arrayValue);
    for (size_t i = 0; i < seg.m_files.size(); ++i)
        jfiles.append(makeFileEntryValue(seg.m_files[i]));
    root["files"] = jfiles;

    return writeCompact(root);
}

//校验一个分段的字段是否自洽：段号落在段数内、各计数非负、本段条目不越出总计。
static bool validateManifestSegment(const FolderRelayManifestSegment& seg)
{
    if (seg.m_segcount < 1 || seg.m_segindex < 0 || seg.m_segindex >= seg.m_segcount)
        return false;

    if (seg.m_totaldirs < 0 || seg.m_totalfiles < 0 || seg.m_totalsize < 0)
        return false;

    if (seg.m_dirbase < 0 || seg.m_filebase < 0)
        return false;

    if (seg.m_dirbase + static_cast<int64_t>(seg.m_dirs.size()) > seg.m_totaldirs)
        return false;

    if (seg.m_filebase + static_cast<int64_t>(seg.m_files.size()) > seg.m_totalfiles)
        return false;

    for (size_t i = 0; i < seg.m_files.size(); ++i)
    {
        if (seg.m_files[i].m_size < 0)
            return false;
    }
    return true;
}

//==================== 编码 ====================

bool BuildManifestFrame(const FolderRelayManifestSegment& seg, std::string& out)
{
    out.clear();
    if (!validateManifestSegment(seg))
        return false;

    std::string descriptor = buildManifestDescriptor(seg);
    return buildFrame(ft_manifest, descriptor, nullptr, 0, out);
}

//估算分段描述符里「除 dirs / files 两个数组之外」的固定开销时，给
//seg / segs / dirbase / filebase 四个整型字段留出的字节数：单个 int64 最多 20 位
//十进制，四个合计 80 位，取 96 覆盖有余。
static const size_t kManifestNumberReserveBytes = 96;

//估算数组元素长度时为分隔逗号预留的字节数
static const size_t kJsonCommaBytes = 1;

//量一个分段除条目数组之外的固定字节开销。用真正的描述符构造函数来量，
//估算格式便不会与实际发出的格式脱节。
static size_t measureManifestFixedCost(const std::string& foldername,
                                       int64_t totaldirs, int64_t totalfiles, int64_t totalsize)
{
    FolderRelayManifestSegment probe;
    probe.m_foldername = foldername;
    probe.m_totaldirs  = totaldirs;
    probe.m_totalfiles = totalfiles;
    probe.m_totalsize  = totalsize;
    //seg / segs / dirbase / filebase 此刻还不知道，用默认值量，再补上 kManifestNumberReserveBytes
    return buildManifestDescriptor(probe).size() + kManifestNumberReserveBytes;
}

//量一条目录相对路径写进 dirs 数组后占的字节数（含分隔逗号）。
//用 jsoncpp 实际序列化来量，中文路径被转义成 \uXXXX 后的膨胀也就一并算进去了。
static size_t measureDirCost(const std::string& dir)
{
    Json::Value v(dir);
    return writeCompact(v).size() + kJsonCommaBytes;
}

//量一个文件条目写进 files 数组后占的字节数（含分隔逗号）
static size_t measureFileCost(const FolderRelayFileEntry& entry)
{
    return writeCompact(makeFileEntryValue(entry)).size() + kJsonCommaBytes;
}

//收束当前分段：把它放进结果列表，更新已发出的目录 / 文件条数，并把 cur 重置成
//下一个分段的初始状态（起始序号接在已发出条数之后）。
static void closeManifestSegment(std::vector<FolderRelayManifestSegment>& outSegments,
                                 FolderRelayManifestSegment& cur,
                                 size_t& emittedDirs, size_t& emittedFiles, size_t& used)
{
    outSegments.push_back(cur);
    emittedDirs  += cur.m_dirs.size();
    emittedFiles += cur.m_files.size();

    cur = FolderRelayManifestSegment();
    cur.m_dirbase  = static_cast<int64_t>(emittedDirs);
    cur.m_filebase = static_cast<int64_t>(emittedFiles);
    used = 0;
}

bool SplitManifest(const std::string& foldername,
                   const std::vector<std::string>& dirs,
                   const std::vector<FolderRelayFileEntry>& files,
                   size_t segmentBudgetBytes,
                   std::vector<FolderRelayManifestSegment>& outSegments)
{
    outSegments.clear();

    //先算总计：字节数合计要在切分前定下来，因为每一段都带同一份总计
    int64_t totalsize = 0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (files[i].m_size < 0)
            return false;
        totalsize += files[i].m_size;
    }
    int64_t totaldirs  = static_cast<int64_t>(dirs.size());
    int64_t totalfiles = static_cast<int64_t>(files.size());

    size_t fixedCost = measureManifestFixedCost(foldername, totaldirs, totalfiles, totalsize);
    //预算连固定开销都装不下时无法切分，如实失败，不产出无意义的分段
    if (segmentBudgetBytes <= fixedCost)
        return false;
    size_t avail = segmentBudgetBytes - fixedCost;

    size_t emittedDirs  = 0;   //已放进前面各分段的目录条数
    size_t emittedFiles = 0;   //已放进前面各分段的文件条数
    size_t used = 0;           //当前分段两个数组已占用的估算字节数
    FolderRelayManifestSegment cur;

    //目录在前：接收端要先按目录清单把目录建全，再收文件字节
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        size_t cost = measureDirCost(dirs[i]);
        //装不下就换下一段；但空段里放不下的单条超长条目仍然要放进去，
        //由后续封帧时的上限校验判定它是否真的超了中转单帧上限
        if (used + cost > avail && !cur.m_dirs.empty())
            closeManifestSegment(outSegments, cur, emittedDirs, emittedFiles, used);

        cur.m_dirs.push_back(dirs[i]);
        used += cost;
    }

    for (size_t i = 0; i < files.size(); ++i)
    {
        size_t cost = measureFileCost(files[i]);
        if (used + cost > avail && (!cur.m_dirs.empty() || !cur.m_files.empty()))
            closeManifestSegment(outSegments, cur, emittedDirs, emittedFiles, used);

        cur.m_files.push_back(files[i]);
        used += cost;
    }

    //收束最后一段。清单为空（无目录也无文件）时这里产出唯一的一个空分段，
    //接收端据此照样能得到"这是一个空文件夹"的结论
    if (!cur.m_dirs.empty() || !cur.m_files.empty() || outSegments.empty())
        closeManifestSegment(outSegments, cur, emittedDirs, emittedFiles, used);

    //段数确定后回填每段共有的字段
    for (size_t i = 0; i < outSegments.size(); ++i)
    {
        outSegments[i].m_foldername = foldername;
        outSegments[i].m_totaldirs  = totaldirs;
        outSegments[i].m_totalfiles = totalfiles;
        outSegments[i].m_totalsize  = totalsize;
        outSegments[i].m_segindex   = static_cast<int64_t>(i);
        outSegments[i].m_segcount   = static_cast<int64_t>(outSegments.size());
    }
    return true;
}

bool BuildManifestFrames(const std::string& foldername,
                         const std::vector<std::string>& dirs,
                         const std::vector<FolderRelayFileEntry>& files,
                         size_t segmentBudgetBytes,
                         std::vector<std::string>& outFrames)
{
    outFrames.clear();

    std::vector<FolderRelayManifestSegment> segments;
    if (!SplitManifest(foldername, dirs, files, segmentBudgetBytes, segments))
        return false;

    for (size_t i = 0; i < segments.size(); ++i)
    {
        std::string frame;
        //单条超长条目撑爆中转单帧上限时在这里被拦下，不会把一个发不出去的帧交给调用方
        if (!BuildManifestFrame(segments[i], frame))
        {
            outFrames.clear();
            return false;
        }
        outFrames.push_back(frame);
    }
    return true;
}

bool BuildManifestAckFrame(const FolderRelayManifestAck& ack, std::string& out)
{
    out.clear();
    if (ack.m_totalfiles < 0)
        return false;

    Json::Value root;
    root["totalfiles"] = static_cast<Json::Int64>(ack.m_totalfiles);

    Json::Value jpoints(Json::arrayValue);
    for (size_t i = 0; i < ack.m_points.size(); ++i)
    {
        if (ack.m_points[i].m_index < 0 || ack.m_points[i].m_offset < 0)
            return false;

        Json::Value jp;
        jp["index"]  = static_cast<Json::Int64>(ack.m_points[i].m_index);
        jp["offset"] = static_cast<Json::Int64>(ack.m_points[i].m_offset);
        jpoints.append(jp);
    }
    root["points"] = jpoints;

    return buildFrame(ft_manifest_ack, writeCompact(root), nullptr, 0, out);
}

bool BuildFileBeginFrame(const FolderRelayFileBegin& info, std::string& out)
{
    out.clear();
    if (info.m_index < 0 || info.m_size < 0 || info.m_offset < 0)
        return false;

    //起传偏移不能越过文件末尾，否则接收端无从判断该写到哪里
    if (info.m_offset > info.m_size)
        return false;

    Json::Value root;
    root["index"]  = static_cast<Json::Int64>(info.m_index);
    root["size"]   = static_cast<Json::Int64>(info.m_size);
    root["md5"]    = info.m_md5;
    root["offset"] = static_cast<Json::Int64>(info.m_offset);

    return buildFrame(ft_file_begin, writeCompact(root), nullptr, 0, out);
}

bool BuildFileChunkFrame(int64_t index, int64_t offset,
                         const unsigned char* data, size_t len,
                         std::string& out)
{
    out.clear();
    if (index < 0 || offset < 0)
        return false;

    Json::Value root;
    root["index"]  = static_cast<Json::Int64>(index);
    root["offset"] = static_cast<Json::Int64>(offset);
    //len 由本函数按实际附件长度写入，故描述符与附件天然不会不一致
    root["len"]    = static_cast<Json::Int64>(len);

    return buildFrame(ft_file_chunk, writeCompact(root), data, len, out);
}

bool BuildChunkAckFrame(const FolderRelayChunkAck& ack, std::string& out)
{
    out.clear();
    if (ack.m_index < 0 || ack.m_nextoffset < 0)
        return false;

    Json::Value root;
    root["index"]      = static_cast<Json::Int64>(ack.m_index);
    root["nextoffset"] = static_cast<Json::Int64>(ack.m_nextoffset);

    return buildFrame(ft_chunk_ack, writeCompact(root), nullptr, 0, out);
}

bool BuildFileEndFrame(int64_t index, std::string& out)
{
    out.clear();
    if (index < 0)
        return false;

    Json::Value root;
    root["index"] = static_cast<Json::Int64>(index);

    return buildFrame(ft_file_end, writeCompact(root), nullptr, 0, out);
}

bool BuildFileResultFrame(const FolderRelayFileResult& result, std::string& out)
{
    out.clear();
    if (result.m_index < 0)
        return false;

    Json::Value root;
    root["index"] = static_cast<Json::Int64>(result.m_index);
    root["code"]  = result.m_code;
    root["msg"]   = result.m_msg;

    return buildFrame(ft_file_result, writeCompact(root), nullptr, 0, out);
}

bool BuildAbortFrame(const FolderRelayAbort& abort, std::string& out)
{
    out.clear();

    Json::Value root;
    root["errcode"] = abort.m_errcode;
    root["errmsg"]  = abort.m_errmsg;

    return buildFrame(ft_abort, writeCompact(root), nullptr, 0, out);
}

bool BuildCompleteFrame(const FolderRelayComplete& summary, std::string& out)
{
    out.clear();
    if (summary.m_totalfiles < 0 || summary.m_totalsize < 0)
        return false;

    Json::Value root;
    root["totalfiles"] = static_cast<Json::Int64>(summary.m_totalfiles);
    root["totalsize"]  = static_cast<Json::Int64>(summary.m_totalsize);

    return buildFrame(ft_complete, writeCompact(root), nullptr, 0, out);
}

//==================== 描述符的解析 ====================

//解析 ft_manifest 的描述符。结构性字段（段号、段数、各项总计、起始序号）缺失或
//取值荒谬一律判失败，条目数组只接受数组类型，元素必须是带字符串 path 的对象。
static bool parseManifestDescriptor(const std::string& descriptor,
                                    FolderRelayManifestSegment& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    if (root.isMember("name") && !root["name"].isString())
        return false;
    if (root.isMember("name"))
        out.m_foldername = root["name"].asString();

    out.m_totaldirs  = jsonToInt64(root["totaldirs"]);
    out.m_totalfiles = jsonToInt64(root["totalfiles"]);
    out.m_totalsize  = jsonToInt64(root["totalsize"]);
    out.m_segindex   = jsonToInt64(root["seg"]);
    //segs 缺失时取 0，会被下面的自洽性校验判为非法，故无须单独判存在性
    out.m_segcount   = jsonToInt64(root["segs"]);
    out.m_dirbase    = jsonToInt64(root["dirbase"]);
    out.m_filebase   = jsonToInt64(root["filebase"]);

    const Json::Value& jdirs = root["dirs"];
    if (!jdirs.isNull())
    {
        if (!jdirs.isArray())
            return false;
        for (Json::ArrayIndex i = 0; i < jdirs.size(); ++i)
        {
            if (!jdirs[i].isString())
                return false;
            out.m_dirs.push_back(jdirs[i].asString());
        }
    }

    const Json::Value& jfiles = root["files"];
    if (!jfiles.isNull())
    {
        if (!jfiles.isArray())
            return false;
        for (Json::ArrayIndex i = 0; i < jfiles.size(); ++i)
        {
            const Json::Value& jf = jfiles[i];
            if (!jf.isObject() || !jf["path"].isString())
                return false;

            FolderRelayFileEntry entry;
            entry.m_relpath = jf["path"].asString();
            entry.m_size    = jsonToInt64(jf["size"]);
            if (entry.m_size < 0)
                return false;
            out.m_files.push_back(entry);
        }
    }

    return validateManifestSegment(out);
}

//解析 ft_manifest_ack 的描述符
static bool parseManifestAckDescriptor(const std::string& descriptor,
                                       FolderRelayManifestAck& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    out.m_totalfiles = jsonToInt64(root["totalfiles"]);
    if (out.m_totalfiles < 0)
        return false;

    const Json::Value& jpoints = root["points"];
    if (!jpoints.isNull())
    {
        if (!jpoints.isArray())
            return false;
        for (Json::ArrayIndex i = 0; i < jpoints.size(); ++i)
        {
            const Json::Value& jp = jpoints[i];
            if (!jp.isObject())
                return false;

            FolderRelayResumePoint point;
            point.m_index  = jsonToInt64(jp["index"]);
            point.m_offset = jsonToInt64(jp["offset"]);
            //位点必须落在文件序号范围内且偏移非负，否则发送端会据此去读一个不存在的位置
            if (point.m_index < 0 || point.m_index >= out.m_totalfiles || point.m_offset < 0)
                return false;
            out.m_points.push_back(point);
        }
    }
    return true;
}

//解析 ft_file_begin 的描述符
static bool parseFileBeginDescriptor(const std::string& descriptor, FolderRelayFileBegin& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    if (root.isMember("md5") && !root["md5"].isString())
        return false;
    if (root.isMember("md5"))
        out.m_md5 = root["md5"].asString();

    out.m_index  = jsonToInt64(root["index"]);
    out.m_size   = jsonToInt64(root["size"]);
    out.m_offset = jsonToInt64(root["offset"]);
    if (out.m_index < 0 || out.m_size < 0 || out.m_offset < 0 || out.m_offset > out.m_size)
        return false;

    return true;
}

//解析 ft_file_chunk 的描述符。len 字段必须显式存在——它是核对附件长度的依据，
//缺了就无从判断附件是否被截断，只能整帧判失败。
static bool parseFileChunkDescriptor(const std::string& descriptor, FolderRelayFileChunk& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    if (!root.isMember("len"))
        return false;

    out.m_index   = jsonToInt64(root["index"]);
    out.m_offset  = jsonToInt64(root["offset"]);
    out.m_datalen = jsonToInt64(root["len"]);
    if (out.m_index < 0 || out.m_offset < 0 || out.m_datalen < 0)
        return false;

    return true;
}

//解析 ft_chunk_ack 的描述符
static bool parseChunkAckDescriptor(const std::string& descriptor, FolderRelayChunkAck& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    out.m_index      = jsonToInt64(root["index"]);
    out.m_nextoffset = jsonToInt64(root["nextoffset"]);
    if (out.m_index < 0 || out.m_nextoffset < 0)
        return false;

    return true;
}

//解析 ft_file_end 的描述符
static bool parseFileEndDescriptor(const std::string& descriptor, FolderRelayFileEnd& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    out.m_index = jsonToInt64(root["index"]);
    if (out.m_index < 0)
        return false;

    return true;
}

//解析 ft_file_result 的描述符
static bool parseFileResultDescriptor(const std::string& descriptor, FolderRelayFileResult& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    if (root.isMember("msg") && !root["msg"].isString())
        return false;
    if (root.isMember("msg"))
        out.m_msg = root["msg"].asString();

    out.m_index = jsonToInt64(root["index"]);
    if (out.m_index < 0)
        return false;
    out.m_code = static_cast<int>(jsonToInt64(root["code"]));

    return true;
}

//解析 ft_abort 的描述符
static bool parseAbortDescriptor(const std::string& descriptor, FolderRelayAbort& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    if (root.isMember("errmsg") && !root["errmsg"].isString())
        return false;
    if (root.isMember("errmsg"))
        out.m_errmsg = root["errmsg"].asString();

    out.m_errcode = static_cast<int>(jsonToInt64(root["errcode"]));
    return true;
}

//解析 ft_complete 的描述符
static bool parseCompleteDescriptor(const std::string& descriptor, FolderRelayComplete& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    out.m_totalfiles = jsonToInt64(root["totalfiles"]);
    out.m_totalsize  = jsonToInt64(root["totalsize"]);
    if (out.m_totalfiles < 0 || out.m_totalsize < 0)
        return false;

    return true;
}

//==================== 解码 ====================

bool PeekFrameType(const unsigned char* body, size_t bodylen, folder_frame_type& outType)
{
    outType = ft_invalid;
    if (body == nullptr || bodylen < 1)
        return false;

    return frameTypeFromByte(body[0], outType);
}

bool DecodeFileChunk(const unsigned char* body, size_t bodylen,
                     FolderRelayFileChunk& outInfo, size_t& outDataOffset)
{
    outInfo = FolderRelayFileChunk();
    outDataOffset = 0;

    folder_frame_type type = ft_invalid;
    std::string descriptor;
    size_t dataOffset = 0;
    size_t dataLen = 0;
    if (!splitFrame(body, bodylen, type, descriptor, dataOffset, dataLen))
        return false;

    if (type != ft_file_chunk)
        return false;

    FolderRelayFileChunk info;
    if (!parseFileChunkDescriptor(descriptor, info))
        return false;

    //描述符自报的附件长度必须与实际剩余字节数分毫不差：短了说明附件被截断，
    //长了说明帧里夹带了不该有的字节，两种都不能按有效数据写盘
    if (info.m_datalen != static_cast<int64_t>(dataLen))
        return false;

    outInfo = info;
    outDataOffset = dataOffset;
    return true;
}

bool DecodeFrame(const unsigned char* body, size_t bodylen, FolderRelayInboundFrame& out)
{
    out = FolderRelayInboundFrame();

    folder_frame_type type = ft_invalid;
    std::string descriptor;
    size_t dataOffset = 0;
    size_t dataLen = 0;
    if (!splitFrame(body, bodylen, type, descriptor, dataOffset, dataLen))
        return false;

    //除分片帧外，其余帧一律不带二进制附件；带了说明对端行为异常，按非法帧处理
    if (type != ft_file_chunk && dataLen != 0)
        return false;

    bool ok = false;
    switch (type)
    {
    //清单分段：解出后仍需交给 FolderRelayManifestAssembler 逐段重组
    case ft_manifest:
        ok = parseManifestDescriptor(descriptor, out.m_manifest);
        break;

    //清单回执：接收端建完目录后回的续传位点集合
    case ft_manifest_ack:
        ok = parseManifestAckDescriptor(descriptor, out.m_manifestAck);
        break;

    //文件开始：一个文件的元信息与本次起传偏移
    case ft_file_begin:
        ok = parseFileBeginDescriptor(descriptor, out.m_fileBegin);
        break;

    //分片：描述符之后还带二进制附件，须额外核对附件长度并拷出附件内容
    case ft_file_chunk:
        ok = parseFileChunkDescriptor(descriptor, out.m_fileChunk);
        if (ok && out.m_fileChunk.m_datalen != static_cast<int64_t>(dataLen))
            ok = false;
        if (ok && dataLen > 0)
            out.m_chunkData.assign(body + dataOffset, body + dataOffset + dataLen);
        break;

    //分片回执：权威续传位点，兼作流控
    case ft_chunk_ack:
        ok = parseChunkAckDescriptor(descriptor, out.m_chunkAck);
        break;

    //文件结束：该文件字节已发完，接收端据此收尾校验
    case ft_file_end:
        ok = parseFileEndDescriptor(descriptor, out.m_fileEnd);
        break;

    //文件结果：接收端落盘与 MD5 校验的结论
    case ft_file_result:
        ok = parseFileResultDescriptor(descriptor, out.m_fileResult);
        break;

    //中止：带失败原因，收到即整体失败
    case ft_abort:
        ok = parseAbortDescriptor(descriptor, out.m_abort);
        break;

    //完成：全部文件传完的汇总
    case ft_complete:
        ok = parseCompleteDescriptor(descriptor, out.m_complete);
        break;

    //未知帧类型：splitFrame 已挡过一道，这里兜底，保证新增枚举值时不会静默漏处理
    default:
        ok = false;
        break;
    }

    if (!ok)
    {
        out = FolderRelayInboundFrame();
        return false;
    }

    out.m_type = type;
    return true;
}

} // namespace relayframing

//==================== 清单分段重组 ====================

FolderRelayManifestAssembler::FolderRelayManifestAssembler()
{
    Reset();
}

void FolderRelayManifestAssembler::Reset()
{
    m_started    = false;
    m_complete   = false;
    m_segcount   = 0;
    m_nextseg    = 0;
    m_foldername.clear();
    m_totaldirs  = 0;
    m_totalfiles = 0;
    m_totalsize  = 0;
    m_dirs.clear();
    m_files.clear();
}

bool FolderRelayManifestAssembler::IsComplete() const
{
    return m_complete;
}

const std::string& FolderRelayManifestAssembler::GetFolderName() const
{
    return m_foldername;
}

const std::vector<std::string>& FolderRelayManifestAssembler::GetDirs() const
{
    return m_dirs;
}

const std::vector<FolderRelayFileEntry>& FolderRelayManifestAssembler::GetFiles() const
{
    return m_files;
}

int64_t FolderRelayManifestAssembler::GetTotalSize() const
{
    return m_totalsize;
}

FolderRelayManifestAssembler::FeedResult
FolderRelayManifestAssembler::Feed(const FolderRelayManifestSegment& seg)
{
    //先做单段自洽性校验，与解码路径共用同一份规则
    if (!relayframing::validateManifestSegment(seg))
    {
        Reset();
        return folder_manifest_error;
    }

    //已经收齐之后又来一段：对端行为异常，整份清单不再可信
    if (m_complete)
    {
        Reset();
        return folder_manifest_error;
    }

    if (!m_started)
    {
        //第一段必须是 0 号段，否则前面的段已经丢了
        if (seg.m_segindex != 0)
        {
            Reset();
            return folder_manifest_error;
        }
        m_started    = true;
        m_segcount   = seg.m_segcount;
        m_foldername = seg.m_foldername;
        m_totaldirs  = seg.m_totaldirs;
        m_totalfiles = seg.m_totalfiles;
        m_totalsize  = seg.m_totalsize;
        m_nextseg    = 0;
    }
    else if (seg.m_segcount != m_segcount
             || seg.m_foldername != m_foldername
             || seg.m_totaldirs != m_totaldirs
             || seg.m_totalfiles != m_totalfiles
             || seg.m_totalsize != m_totalsize)
    {
        //后续段的段数、文件夹名与三项总计必须与第一段完全一致
        Reset();
        return folder_manifest_error;
    }

    //中转通道是有序可靠的，段号必须严格递增且不跳号；重复的同一段同样按错误处理
    if (seg.m_segindex != m_nextseg)
    {
        Reset();
        return folder_manifest_error;
    }

    //本段的起始序号必须正好接在已累积条数之后，否则中间有条目丢失或重复
    if (seg.m_dirbase != static_cast<int64_t>(m_dirs.size())
        || seg.m_filebase != static_cast<int64_t>(m_files.size()))
    {
        Reset();
        return folder_manifest_error;
    }

    //累积条数不得超过总计
    if (static_cast<int64_t>(m_dirs.size() + seg.m_dirs.size()) > m_totaldirs
        || static_cast<int64_t>(m_files.size() + seg.m_files.size()) > m_totalfiles)
    {
        Reset();
        return folder_manifest_error;
    }

    m_dirs.insert(m_dirs.end(), seg.m_dirs.begin(), seg.m_dirs.end());
    m_files.insert(m_files.end(), seg.m_files.begin(), seg.m_files.end());
    ++m_nextseg;

    if (m_nextseg < m_segcount)
        return folder_manifest_need_more;

    //末段收齐：条数与字节数合计都必须与总计吻合，任一项不符即整份清单作废
    if (static_cast<int64_t>(m_dirs.size()) != m_totaldirs
        || static_cast<int64_t>(m_files.size()) != m_totalfiles)
    {
        Reset();
        return folder_manifest_error;
    }

    int64_t sum = 0;
    for (size_t i = 0; i < m_files.size(); ++i)
        sum += m_files[i].m_size;
    if (sum != m_totalsize)
    {
        Reset();
        return folder_manifest_error;
    }

    m_complete = true;
    return folder_manifest_done;
}

} // namespace foldertransfer
