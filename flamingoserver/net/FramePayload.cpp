/**
 *  v2 帧包体编解码管线的实现，服务端侧。设计说明见 FramePayload.h。
 *  balloonwj@qq.com   2026-08-13
 */
#include "FramePayload.h"

#include "../chatserversrc/Protocol.h"   //frame_header / frame_flag_* / COMPRESS_THRESHOLD
#include "../zlib1.3.2/ZlibUtil.h"

namespace net {

bool encodeFramePayload(uint16_t cmd, uint32_t seq, const std::string& plain,
                        FrameCipher* cipher, uint8_t extraFlags,
                        payload_compress_policy policy,
                        std::string& wireOut, uint8_t& flagsOut, uint32_t& originLenOut)
{
    wireOut.clear();
    flagsOut = extraFlags;
    originLenOut = static_cast<uint32_t>(plain.size());

    //stage 指向当前这一级处理的结果，逐级往下传，避免多余的整段拷贝
    const std::string* stage = &plain;
    std::string compressed;

    //第一步：压缩。达到阈值、且压缩后确实更小才采用 —— 短包体压完往往反而变长，
    //那样既费 CPU 又多占带宽
    if (policy == payload_compress_auto && plain.size() >= COMPRESS_THRESHOLD)
    {
        if (ZlibUtil::compressBuf(plain, compressed) && compressed.size() < plain.size())
        {
            flagsOut |= frame_flag_compressed;
            stage = &compressed;
        }
    }

    //第二步：加密。这一步与中转通道完全一样，故交给 encryptFramePayload，
    //附加认证数据怎么拼、加密位怎么置只写在那一处
    return encryptFramePayload(cmd, seq, flagsOut, originLenOut, *stage, cipher,
                               wireOut, flagsOut);
}

bool decodeFramePayload(uint16_t cmd, uint32_t seq, uint8_t flags, uint32_t originlen,
                        const std::string& wire, FrameCipher* cipher,
                        std::string& plainOut, std::string& errOut)
{
    plainOut.clear();

    //第一步：解密。顺序与发送侧严格相反。这一步与中转通道完全一样，故同样
    //交给 decryptFramePayload
    std::string decrypted;
    if (!decryptFramePayload(cmd, seq, flags, originlen, wire, cipher, decrypted, errOut))
        return false;

    //第二步：解压
    if ((flags & frame_flag_compressed) != 0)
    {
        if (!ZlibUtil::uncompressBuf(decrypted, plainOut, originlen))
        {
            errOut = "failed to uncompress frame body";
            plainOut.clear();
            return false;
        }
        return true;
    }

    //未压缩：解密结果即明文，直接换手，省掉一次整段拷贝
    plainOut.swap(decrypted);
    return true;
}

bool encryptFramePayload(uint16_t cmd, uint32_t seq, uint8_t srcFlags, uint32_t originlen,
                         const std::string& plain, FrameCipher* cipher,
                         std::string& wireOut, uint8_t& flagsOut)
{
    wireOut.clear();

    //加密位一律按本次实际处理结果重新置：中转转发时传进来的是原帧的标志位，
    //而原帧加密与否说明的是<u>上一条连接</u>的情况，与本连接无关
    flagsOut = static_cast<uint8_t>(srcFlags & ~frame_flag_encrypted);

    //cipher 未就绪对应「本进程未启用加密」与「握手尚未完成」两种情形，都按明文
    //发出 —— 握手帧本身正是靠这条走明文的，那时双方都还没有密钥
    if (cipher == NULL || !cipher->ready())
    {
        wireOut = plain;
        return true;
    }

    flagsOut |= frame_flag_encrypted;

    //密文长度可以先算出来（当前长度加一个固定开销），所以附加认证数据能在加密
    //之前就拼好 —— 它要绑定的正是最终写进帧头的那几个值
    const uint32_t cipherLen = static_cast<uint32_t>(plain.size() + STREAM_OVERHEAD_BYTES);
    unsigned char ad[FRAME_AD_BYTES] = { 0 };
    buildFrameAd(cmd, seq, flagsOut, cipherLen, originlen, ad);

    if (!cipher->encrypt(plain, ad, sizeof(ad), wireOut))
    {
        wireOut.clear();
        return false;
    }

    return true;
}

bool decryptFramePayload(uint16_t cmd, uint32_t seq, uint8_t flags, uint32_t originlen,
                         const std::string& wire, FrameCipher* cipher,
                         std::string& plainOut, std::string& errOut)
{
    plainOut.clear();
    errOut.clear();

    //未置加密位：线上包体本就是明文（可能仍是压缩过的字节），原样交还
    if ((flags & frame_flag_encrypted) == 0)
    {
        plainOut = wire;
        return true;
    }

    if (cipher == NULL || !cipher->ready())
    {
        errOut = "recv encrypted frame but this connection has no session key";
        return false;
    }

    //附加认证数据按本帧帧头字段重新拼一遍，与发送方的拼法必须逐字节一致，
    //任何一个字段对不上都会解密失败
    unsigned char ad[FRAME_AD_BYTES] = { 0 };
    buildFrameAd(cmd, seq, flags, static_cast<uint32_t>(wire.size()), originlen, ad);

    if (!cipher->decrypt(wire, ad, sizeof(ad), plainOut))
    {
        errOut = "failed to decrypt frame body";
        plainOut.clear();
        return false;
    }

    return true;
}

} // namespace net
