/**
 *  CryptoBenchTests.cpp
 *
 *  协议加密的<u>吞吐实测</u>。存在的理由是方案文档里那句「加解密开销可以忽略」
 *  原本只是推测——推测不能当结论用，尤其是远程桌面这种每秒几十帧、每帧上百 KB
 *  的场景，"可以忽略"和"吃掉一个核"之间差着两个数量级。
 *
 *  测的是 XChaCha20-Poly1305 流加密的原始吞吐（加密与解密各一次往返），这正是
 *  帧体加密的全部计算量——FrameCipher 在它之上只多了一次 16 字节的附加认证数据
 *  拼装与一次字符串拷贝。
 *
 *  <u>默认不参与常规测试</u>：带 [.bench] 标签，跑起来要几秒钟，且结果依赖当时的
 *  机器负载，不适合当作回归断言。要跑就显式指定：
 *
 *      FlamingoUnit.exe "[bench]"
 *
 *  <u>结果随构建配置差别很大</u>：Debug 配置链的是 libsodium 的 StaticDebug 版，
 *  未开优化，测出来的数字是<u>下限</u>。用它来论证"开销可忽略"是保守的——Release
 *  只会更快。反过来若要引用一个乐观数字，必须在 Release 下重测。
 *
 *  balloonwj@qq.com   2026-08-13
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <chrono>
#include <stdio.h>

#include <sodium.h>

namespace {

//一次测量跑多少轮。取这个量级是为了让总耗时落在秒级，摊薄计时器本身的误差
//与偶发的调度抖动；轮数太少时单次结果会在正负几成之间跳。
const int kBenchRounds = 200;

//远程桌面一帧屏幕数据的典型大小（字节）。1080p 经编码器压缩后大致在这个量级，
//是推算实际占用时的基准。
const size_t kTypicalScreenFrameBytes = 100 * 1024;

//中转通道的分片上限（字节），见 RdRelayClient 的 kRelayChunkSize。
//超过这个大小的引擎帧会被切片，所以它是单次加密能遇到的最大输入。
const size_t kRelayChunkBytes = 512 * 1024;

//聊天消息的典型包体大小（字节）。用来说明主连接那种小包的开销量级。
const size_t kTypicalChatBodyBytes = 512;

/**
 *  测一次「加密 + 解密」往返的吞吐。
 *
 *  @param payloadBytes 单次处理的字节数
 *  @param rounds       跑多少轮
 *  @param outMBps      [出参] 往返吞吐，单位 MB/s（以处理的明文字节计）
 *  @param outUsPerOp   [出参] 单次往返的平均耗时，单位微秒
 *  @return 成功返回 true；libsodium 调用失败返回 false
 */
bool BenchRoundTrip(size_t payloadBytes, int rounds, double& outMBps, double& outUsPerOp)
{
    outMBps = 0.0;
    outUsPerOp = 0.0;

    unsigned char key[crypto_secretstream_xchacha20poly1305_KEYBYTES] = { 0 };
    unsigned char streamHeader[crypto_secretstream_xchacha20poly1305_HEADERBYTES] = { 0 };
    crypto_secretstream_xchacha20poly1305_keygen(key);

    crypto_secretstream_xchacha20poly1305_state pushState;
    crypto_secretstream_xchacha20poly1305_state pullState;
    if (crypto_secretstream_xchacha20poly1305_init_push(&pushState, streamHeader, key) != 0)
        return false;
    if (crypto_secretstream_xchacha20poly1305_init_pull(&pullState, streamHeader, key) != 0)
        return false;

    //明文填成非全零，免得被某些实现的特判路径带偏
    std::vector<unsigned char> plain(payloadBytes);
    for (size_t i = 0; i < payloadBytes; ++i)
        plain[i] = static_cast<unsigned char>(i & 0xFF);

    std::vector<unsigned char> cipher(payloadBytes + crypto_secretstream_xchacha20poly1305_ABYTES);
    std::vector<unsigned char> decrypted(payloadBytes);

    //附加认证数据取与真实帧一致的 16 字节，使测量包含它那部分开销
    unsigned char ad[16] = { 0 };

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    for (int i = 0; i < rounds; ++i)
    {
        unsigned long long cipherLen = 0;
        if (crypto_secretstream_xchacha20poly1305_push(
                &pushState, &cipher[0], &cipherLen,
                &plain[0], plain.size(), ad, sizeof(ad),
                crypto_secretstream_xchacha20poly1305_TAG_MESSAGE) != 0)
        {
            return false;
        }

        unsigned long long plainLen = 0;
        unsigned char tag = 0;
        if (crypto_secretstream_xchacha20poly1305_pull(
                &pullState, &decrypted[0], &plainLen, &tag,
                &cipher[0], static_cast<size_t>(cipherLen), ad, sizeof(ad)) != 0)
        {
            return false;
        }
    }

    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const double elapsedSec =
        std::chrono::duration_cast<std::chrono::duration<double> >(end - start).count();
    if (elapsedSec <= 0.0)
        return false;

    const double totalBytes = static_cast<double>(payloadBytes) * static_cast<double>(rounds);
    outMBps = totalBytes / elapsedSec / (1024.0 * 1024.0);
    outUsPerOp = elapsedSec * 1000000.0 / static_cast<double>(rounds);
    return true;
}

} // anonymous namespace

TEST_CASE("BENCH1 帧体加解密的吞吐实测", "[.bench][crypto]")
{
    REQUIRE(sodium_init() >= 0);

    ::printf("\n================ 帧体加解密吞吐（一次加密 + 一次解密为一轮）================\n");
    ::printf("%-16s %10s %14s %14s\n", "单帧大小", "轮数", "往返耗时(us)", "往返吞吐(MB/s)");

    double mbps = 0.0;
    double usPerOp = 0.0;

    //小包：聊天消息那一档
    REQUIRE(BenchRoundTrip(kTypicalChatBodyBytes, kBenchRounds * 20, mbps, usPerOp));
    ::printf("%-16s %10d %14.2f %14.1f\n", "512 B", kBenchRounds * 20, usPerOp, mbps);
    CHECK(mbps > 0.0);

    //典型屏幕帧
    REQUIRE(BenchRoundTrip(kTypicalScreenFrameBytes, kBenchRounds, mbps, usPerOp));
    ::printf("%-16s %10d %14.2f %14.1f\n", "100 KB", kBenchRounds, usPerOp, mbps);
    const double screenFrameUs = usPerOp;
    CHECK(mbps > 0.0);

    //分片上限
    REQUIRE(BenchRoundTrip(kRelayChunkBytes, kBenchRounds, mbps, usPerOp));
    ::printf("%-16s %10d %14.2f %14.1f\n", "512 KB", kBenchRounds, usPerOp, mbps);
    CHECK(mbps > 0.0);

    //按远程桌面的实际用量折算。一帧屏幕数据在链路上一共要过四次运算：
    //发送端加密一次、中转服务端解密再加密两次、接收端解密一次。对单个进程而言，
    //客户端各承担一次（发送端加密 / 接收端解密），服务端承担两次。
    const double halfRoundTripUs = screenFrameUs / 2.0;   //单次运算 ≈ 往返的一半
    const double fps = 30.0;
    ::printf("\n---- 按 1080p / %.0f fps / 每帧 100 KB 折算 ----\n", fps);
    ::printf("客户端（每帧一次运算）: 每秒 %.2f ms，占单核 %.3f%%\n",
             halfRoundTripUs * fps / 1000.0, halfRoundTripUs * fps / 10000.0);
    ::printf("中转服务端（每帧两次）: 每秒 %.2f ms，占单核 %.3f%%\n",
             screenFrameUs * fps / 1000.0, screenFrameUs * fps / 10000.0);
    ::printf("注：本数字取自当前构建配置；Debug 链的是未优化的 libsodium，为下限。\n");
    ::printf("============================================================================\n\n");
}
