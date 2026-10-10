/**
 *  聊天记录管理器「本机存储占用」页的统计与清理逻辑，HistoryStorageStats.h
 *
 *  本程序把聊天里的图片、文件、语音按内容 MD5 缓存在当前账号的三个目录里（docs/聊天记录管理器方案.md §12.6）：
 *      图片  <数据根>Users\<账号>\ChatImage\<md5>                （ImageTransfer::CacheFilePathForMd5）
 *      文件  <数据根>Users\<账号>\ChatFile\<md5>\<文件名>          （FileTransfer::CacheFilePathFor，按 md5 一个子目录）
 *      语音  <数据根>Users\<账号>\ChatVoice\<md5>.ogg             （balloonvoice::VoiceCachePath）
 *  本地库里每条图片、截图、文件、语音消息的内容 JSON 带着 md5。本模块把「哪个会话引用了哪个 md5」与磁盘上的缓存对起来，
 *  算出每个会话各类缓存占了多少字节；页首总数按 md5 去重（同一个文件发在几个会话里只算一次）。清理时只删这三个目录里
 *  对应 md5 的缓存，删除前逐个核对路径落在本账号的数据目录之内（IsSafeToDeleteCacheDir 同一口径）。用户另存到别处
 *  的文件不统计、不清理；聊天记录本身（msgv2.db）只显示大小，不参与清理。
 *
 *  统计要遍历磁盘，在后台查询线程上执行（V2MsgSearchWorker 的 kV2SearchStorageStats / kV2SearchStorageClean）。
 *  单测见 Tests/HistoryStorageTests.cpp（标签 [histstorage]）。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      std::vector<HistoryCacheRef> refs;
 *      store.QueryCacheRefs(refs);
 *      HistoryStorageStats stats = ComputeStorageStats(refs, dirs);
 *      std::vector<std::wstring> paths = StorageCleanTargets(refs, ctype, cid, dirs);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace balloonhistory {

//缓存的类别（页首三项分计、每个会话三列）
enum HistoryStorageKind
{
    kStorageImage     = 0,   //图片与截图：ChatImage\<md5>
    kStorageFile      = 1,   //文件：ChatFile\<md5>\ 整个子目录
    kStorageVoice     = 2,   //语音：ChatVoice\<md5>.ogg
    kStorageKindCount = 3,   //类别个数（哨兵，便于循环）
};

//当前账号的三个缓存目录与数据根（都以反斜杠结尾），以及本地聊天记录库的路径
struct HistoryStorageDirs
{
    std::wstring m_dataRoot;   //数据根目录（g_szDataRoot）；删除前的边界检查用
    std::wstring m_imageDir;   //ChatImage 目录；为空表示拼不出（未登录），这一类按 0 统计
    std::wstring m_fileDir;    //ChatFile 目录
    std::wstring m_voiceDir;   //ChatVoice 目录
    std::wstring m_dbPath;     //本地聊天记录库 msgv2.db 的完整路径；为空时库大小按 0
};

//本地库里一条消息引用的一份缓存
struct HistoryCacheRef
{
    int         m_ctype;   //会话类型：1 单聊 2 群聊
    int64_t     m_cid;     //会话 ID
    int         m_kind;    //缓存类别，取值见 HistoryStorageKind
    std::string m_md5;     //内容 MD5（UTF-8 的 32 位十六进制）

    HistoryCacheRef()
        : m_ctype(1)
        , m_cid(0)
        , m_kind(kStorageImage)
    {
    }
};

//一个会话的缓存占用
struct HistoryStorageConv
{
    int     m_ctype;                       //会话类型
    int64_t m_cid;                         //会话 ID
    int64_t m_bytes[kStorageKindCount];    //各类缓存的字节数（本会话引用、且本机缓存里存在的）
    int     m_files[kStorageKindCount];    //各类在本机缓存里存在的份数

    HistoryStorageConv()
        : m_ctype(1)
        , m_cid(0)
    {
        for (int i = 0; i < kStorageKindCount; ++i)
        {
            m_bytes[i] = 0;
            m_files[i] = 0;
        }
    }

    //三类合计的字节数
    int64_t TotalBytes() const
    {
        int64_t sum = 0;
        for (int i = 0; i < kStorageKindCount; ++i)
        {
            sum += m_bytes[i];
        }
        return sum;
    }
};

//一次统计的结果
struct HistoryStorageStats
{
    std::vector<HistoryStorageConv> m_convs;              //有缓存的会话，按合计字节数倒序（相同时按 ctype、cid 升序）
    int64_t                         m_totalBytes[kStorageKindCount];   //各类按 md5 去重后的总字节数
    int                             m_totalFiles[kStorageKindCount];   //各类按 md5 去重后的份数
    int64_t                         m_dbBytes;            //本地聊天记录库（含 -wal、-shm）的字节数

    HistoryStorageStats()
        : m_dbBytes(0)
    {
        for (int i = 0; i < kStorageKindCount; ++i)
        {
            m_totalBytes[i] = 0;
            m_totalFiles[i] = 0;
        }
    }

    //三类缓存去重后的合计字节数（不含聊天记录库）
    int64_t CacheBytes() const
    {
        int64_t sum = 0;
        for (int i = 0; i < kStorageKindCount; ++i)
        {
            sum += m_totalBytes[i];
        }
        return sum;
    }
};

/**
 *  从一条消息的归一化内容 JSON 取出它引用的缓存：图片与截图取 image.md5，文件取 file.md5，语音取 voice.md5。
 *  @param msgtype     消息类型（net::message_type）
 *  @param contentJson 归一化内容 JSON（BuildContentJson 的格式）
 *  @param out         [入出参] 成功时填 m_kind 与 m_md5（会话字段由调用方填）
 *  @return 是这三类消息、且 md5 是合法的 32 位十六进制时返回 true
 */
bool CacheRefFromContent(int msgtype, const std::string& contentJson, HistoryCacheRef& out);

/**
 *  md5 是否可以拼进缓存路径：恰好 32 个十六进制字符。其它内容（含路径分隔符、点）一律拒绝，避免拼出缓存目录之外的路径。
 *  @param md5  内容 MD5
 *  @return 合法时返回 true
 */
bool IsValidCacheMd5(const std::string& md5);

/**
 *  一份缓存在磁盘上的路径：图片是文件，文件类是 md5 子目录（以反斜杠结尾），语音是 .ogg 文件。
 *  @param dirs  缓存目录
 *  @param kind  缓存类别，取值见 HistoryStorageKind
 *  @param md5   内容 MD5（须通过 IsValidCacheMd5）
 *  @return 路径；对应目录为空、md5 不合法或类别越界时返回空串
 */
std::wstring StorageCachePath(const HistoryStorageDirs& dirs, int kind, const std::string& md5);

/**
 *  一个路径在磁盘上占的字节数：文件取大小，目录递归累加其中各文件的大小。
 *  @param path  文件或目录（目录可带或不带结尾反斜杠）
 *  @return 字节数；不存在时为 0
 */
int64_t StoragePathBytes(const std::wstring& path);

/**
 *  统计各会话与总的缓存占用，并取聊天记录库的大小。会访问磁盘，应在后台线程上调用。
 *  @param refs  本地库里全部的缓存引用（CV2MsgStore::QueryCacheRefs）
 *  @param dirs  缓存目录
 *  @return 统计结果；本机缓存里不存在的引用不计入（份数与字节数都不加）
 */
HistoryStorageStats ComputeStorageStats(const std::vector<HistoryCacheRef>& refs, const HistoryStorageDirs& dirs);

/**
 *  清理某个会话（或全部会话）的缓存时要删的路径：该会话引用的各 md5 对应的缓存路径，去重。
 *  @param refs   全部缓存引用
 *  @param ctype  会话类型；cid 为 0 时不使用
 *  @param cid    会话 ID；0 表示全部会话
 *  @param dirs   缓存目录
 *  @return 路径表（不检查是否存在）；按出现先后排列、不重复
 */
std::vector<std::wstring> StorageCleanTargets(const std::vector<HistoryCacheRef>& refs, int ctype, int64_t cid,
                                              const HistoryStorageDirs& dirs);

/**
 *  删除一份缓存：先核对路径落在 <数据根>Users\ 之下、且就在三个缓存目录之一的里面，再删（文件直接删，目录连同其中
 *  的文件一起删）。不存在时算成功。
 *  @param path  StorageCachePath 拼出的路径
 *  @param dirs  缓存目录（取数据根与三个缓存目录做边界检查）
 *  @return 删除成功或本来就不存在时返回 true；边界检查不通过或有文件删不掉时返回 false
 */
bool DeleteStoragePath(const std::wstring& path, const HistoryStorageDirs& dirs);

} // namespace balloonhistory
