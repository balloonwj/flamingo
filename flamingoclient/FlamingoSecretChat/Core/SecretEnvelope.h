/**
 *  密聊帧的明文信封与图片分块（docs/密聊方案.md 4.4、4.10），SecretEnvelope.h
 *
 *  每帧密文解开后是一个信封，二进制布局为：
 *      [类型 1 字节][JSON 长度 2 字节，大端][JSON 文本][原始负载]
 *  文字：类型 1，JSON 为 {"id":消息编号,"text":"正文","ts":发送时刻}，没有负载；
 *  图片块：类型 3，JSON 为 {"id":图片编号,"i":块序号,"n":块数,"size":总字节数,"w":宽,"h":高,"hash":"整图摘要的 base64"}，
 *         负载为这一块的原始字节（最多 kImageChunkBytes）。每块都带齐元数据，因此块的先后顺序不影响拼回。
 *  负载不再做 base64：整帧加密后外层才 base64 一次，一块 48 KB 的图片帧约 66 KB，低于服务端 128 KB 的单帧上限。
 *
 *  纯逻辑，便于单元测试（Tests/SecretEnvelopeTests.cpp，[secretdll]）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace secretchat {

//信封类型（第一个字节）
enum EnvelopeType
{
    envelope_text        = 1,   //一条文字
    envelope_image_chunk = 3,   //图片的一块（2 留空：早期设计里的「图片开始」帧已合并进每一块）
};

//图片每块的原始字节数：48 KB
const uint32_t kImageChunkBytes = 48 * 1024;

//信封里 JSON 部分的长度上限：两字节长度字段能表示的最大值
const uint32_t kEnvelopeMaxJsonBytes = 0xFFFF;

//一个信封的内容
struct SecretEnvelope
{
    int         type;         //类型，取值见 EnvelopeType
    uint32_t    id;           //文字为消息编号，图片为图片编号
    std::string text;         //文字正文（UTF-8）
    int64_t     ts;           //文字的发送时刻（发送方本机时间，Unix 毫秒）
    uint32_t    chunkIndex;   //图片块序号，从 0 起
    uint32_t    chunkCount;   //图片的总块数
    uint32_t    totalBytes;   //图片总字节数
    uint32_t    width;        //图片宽（像素）
    uint32_t    height;       //图片高（像素）
    std::string hash;         //整张图片的 32 字节摘要（原始字节）
    std::string payload;      //图片块的原始字节

    SecretEnvelope()
        : type(0)
        , id(0)
        , ts(0)
        , chunkIndex(0)
        , chunkCount(0)
        , totalBytes(0)
        , width(0)
        , height(0)
    {
    }
};

/**
 *  把信封编成二进制。
 *  @return JSON 部分超长或类型不认识时返回 false
 */
bool EncodeEnvelope(const SecretEnvelope& env, std::string& out);

/**
 *  从二进制解出信封。
 *  @return 长度不够、JSON 非法、字段缺失或类型不认识时返回 false
 */
bool DecodeEnvelope(const std::string& in, SecretEnvelope& env);

/**
 *  一张图片要分成的块数。
 */
uint32_t ImageChunkCount(uint32_t totalBytes);

/**
 *  把一张图片切成若干个图片块信封（不含加密）。
 *  @param imageId 图片编号
 *  @param bytes   原图字节
 *  @param width / height 图片尺寸
 *  @param out     [出参] 按块序号排列的信封
 */
void SplitImage(uint32_t imageId, const std::string& bytes, uint32_t width, uint32_t height,
                std::vector<SecretEnvelope>& out);

/**
 *  接收方拼图：收到一张图片的若干块，收齐后校验摘要交出整图。一个对象对应一个会话里的全部在收的图片。
 */
class CImageAssembler
{
public:
    //AddChunk 的结果
    enum AddResult
    {
        add_first_chunk = 1,   //这是这张图片收到的第一块（宿主据此插入占位气泡）
        add_progress    = 2,   //收到了一块，还没收齐
        add_complete    = 3,   //收齐且摘要正确，可以 Take
        add_failed      = 4,   //元数据前后矛盾、块序号越界或收齐后摘要不对：这张图片作废
        add_duplicate   = 5,   //重复的块，忽略
    };

    /**
     *  加入一块。
     *  @param env           图片块信封
     *  @param receivedBytes [出参] 这张图片已收到的字节数
     */
    int AddChunk(const SecretEnvelope& env, uint32_t& receivedBytes);

    /**
     *  取走一张已收齐的图片，取走后不再保留。
     *  @return 不存在或没收齐返回 false
     */
    bool Take(uint32_t imageId, std::string& bytes, uint32_t& width, uint32_t& height);

    /**
     *  当前还没收齐的图片编号（会话结束时据此逐张报失败）。
     */
    std::vector<uint32_t> PendingIds() const;

    /**
     *  丢弃全部在收的图片，并把已收的字节清零。
     */
    void Clear();

private:
    //一张在收的图片
    struct Partial
    {
        uint32_t          totalBytes;   //总字节数
        uint32_t          chunkCount;   //总块数
        uint32_t          width;        //宽
        uint32_t          height;       //高
        std::string       hash;         //整图摘要
        std::string       data;         //按偏移写入的原图字节，长度为 totalBytes
        std::vector<bool> got;          //每一块是否已收到
        uint32_t          received;     //已收到的字节数
        bool              complete;     //是否已收齐且摘要正确

        Partial()
            : totalBytes(0)
            , chunkCount(0)
            , width(0)
            , height(0)
            , received(0)
            , complete(false)
        {
        }
    };

    std::map<uint32_t, Partial> m_images;   //图片编号 → 在收的图片
};

} // namespace secretchat
