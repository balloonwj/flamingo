/**
 *  文件上传 / 下载令牌单元测试（Suite T），test_filetoken.cpp
 *  覆盖：上传 / 下载令牌的签发与校验往返、过期拒绝、payload 篡改拒绝、
 *       签名篡改拒绝、错误密钥拒绝、畸形令牌拒绝、场景字段往返与判定。
 *  balloonwj@qq.com   2026-05-22
 */
#include "doctest.h"
#include "../utils/FileToken.h"
#include "../chatserversrc/Protocol.h"

//构造一枚上传令牌的载荷，便于各用例复用
static FileTokenPayload makeUploadPayload()
{
    FileTokenPayload p;
    p.type   = FileToken::token_type_upload;
    p.scene  = file_scene_image_msg;
    p.md5    = "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5";
    p.size   = 4404019;
    p.uid    = 100007;
    p.expire = 5000000000000LL;   //远未到期的时间点
    return p;
}

TEST_CASE("T1 上传令牌签发后校验往返一致")
{
    const std::string key = "flamingo-secret-key";
    FileTokenPayload in = makeUploadPayload();
    std::string token = FileToken::sign(in, key);
    REQUIRE_FALSE(token.empty());

    FileTokenPayload out;
    REQUIRE(FileToken::verify(token, key, 1716268800000LL, out));
    CHECK(out.type == FileToken::token_type_upload);
    CHECK(out.scene == file_scene_image_msg);
    CHECK(out.md5 == in.md5);
    CHECK(out.size == in.size);
    CHECK(out.uid == in.uid);
    CHECK(out.expire == in.expire);
}

TEST_CASE("T2 下载令牌签发后校验往返一致")
{
    const std::string key = "another-key-2026";
    FileTokenPayload in;
    in.type   = FileToken::token_type_download;
    in.scene  = file_scene_avatar;
    in.md5    = "00112233445566778899aabbccddeeff";
    in.size   = 20480;
    in.uid    = 200001;
    in.expire = 5000000000000LL;
    std::string token = FileToken::sign(in, key);

    FileTokenPayload out;
    REQUIRE(FileToken::verify(token, key, 1716268800000LL, out));
    CHECK(out.type == FileToken::token_type_download);
    CHECK(out.scene == file_scene_avatar);
    CHECK(out.size == 20480);
}

TEST_CASE("T3 过期令牌被拒")
{
    const std::string key = "k";
    FileTokenPayload in = makeUploadPayload();
    in.expire = 1716268800000LL;   //过期时间
    std::string token = FileToken::sign(in, key);

    FileTokenPayload out;
    //当前时间刚好到达过期时间——判定过期
    CHECK_FALSE(FileToken::verify(token, key, 1716268800000LL, out));
    //当前时间越过过期时间——判定过期
    CHECK_FALSE(FileToken::verify(token, key, 1716268800001LL, out));
    //当前时间早于过期时间——仍有效
    CHECK(FileToken::verify(token, key, 1716268799999LL, out));
}

TEST_CASE("T4 篡改 payload 后校验失败")
{
    const std::string key = "k";
    std::string token = FileToken::sign(makeUploadPayload(), key);

    //把 payload 里的 size 字段 4404019 改成 9404019（签名段不变）
    std::string tampered = token;
    std::string::size_type pos = tampered.find("4404019");
    REQUIRE(pos != std::string::npos);
    tampered[pos] = '9';

    FileTokenPayload out;
    CHECK_FALSE(FileToken::verify(tampered, key, 1716268800000LL, out));
}

TEST_CASE("T5 篡改签名段后校验失败")
{
    const std::string key = "k";
    std::string token = FileToken::sign(makeUploadPayload(), key);

    //翻转签名段最后一个字符
    std::string tampered = token;
    char& last = tampered[tampered.size() - 1];
    last = (last == '0') ? '1' : '0';

    FileTokenPayload out;
    CHECK_FALSE(FileToken::verify(tampered, key, 1716268800000LL, out));
}

TEST_CASE("T6 错误密钥校验失败")
{
    std::string token = FileToken::sign(makeUploadPayload(), "right-key");

    FileTokenPayload out;
    CHECK_FALSE(FileToken::verify(token, "wrong-key", 1716268800000LL, out));
    //正确密钥仍能通过
    CHECK(FileToken::verify(token, "right-key", 1716268800000LL, out));
}

TEST_CASE("T7 畸形令牌被拒且不崩溃")
{
    const std::string key = "k";
    FileTokenPayload out;
    //空串
    CHECK_FALSE(FileToken::verify("", key, 1716268800000LL, out));
    //无 "." 分隔符
    CHECK_FALSE(FileToken::verify("nodothere", key, 1716268800000LL, out));
    //仅有分隔符
    CHECK_FALSE(FileToken::verify(".", key, 1716268800000LL, out));
    //随意字符串
    CHECK_FALSE(FileToken::verify("garbage.garbage", key, 1716268800000LL, out));
    //payload 字段数不足却带貌似合法的结构
    CHECK_FALSE(FileToken::verify("1|2|3.abcdef", key, 1716268800000LL, out));
}

TEST_CASE("T8 场景字段往返与 imgserver/fileserver 归属判定")
{
    const std::string key = "k";

    //scene=4 图片消息：属于 imgserver 允许集
    FileTokenPayload img = makeUploadPayload();
    img.scene = file_scene_image_msg;
    FileTokenPayload imgOut;
    REQUIRE(FileToken::verify(FileToken::sign(img, key), key, 1, imgOut));
    CHECK(imgOut.scene == file_scene_image_msg);
    CHECK(FileToken::isImageScene(imgOut.scene));
    CHECK_FALSE(FileToken::isFileScene(imgOut.scene));

    //scene=1 聊天文件：不属于 imgserver 允许集
    FileTokenPayload file = makeUploadPayload();
    file.scene = file_scene_chat_file;
    FileTokenPayload fileOut;
    REQUIRE(FileToken::verify(FileToken::sign(file, key), key, 1, fileOut));
    CHECK(fileOut.scene == file_scene_chat_file);
    CHECK_FALSE(FileToken::isImageScene(fileOut.scene));
    CHECK(FileToken::isFileScene(fileOut.scene));
}

TEST_CASE("T9 客户端升级包场景归 fileserver")
{
    const std::string key = "k";

    //scene=5 升级包：走 fileserver，不走 imgserver。
    //与前四种场景不同，这枚令牌由 chatserver 在 cmd_check_update 应答里直接签发，
    //未登录时 uid 为 0，故此处特意用 uid=0 构造，确认 0 不影响签发与校验。
    FileTokenPayload update = makeUploadPayload();
    update.type   = FileToken::token_type_download;
    update.scene  = file_scene_update;
    update.uid    = 0;
    FileTokenPayload updateOut;
    REQUIRE(FileToken::verify(FileToken::sign(update, key), key, 1, updateOut));
    CHECK(updateOut.scene == file_scene_update);
    CHECK(updateOut.uid == 0);
    CHECK(updateOut.type == FileToken::token_type_download);
    CHECK(FileToken::isFileScene(updateOut.scene));
    CHECK_FALSE(FileToken::isImageScene(updateOut.scene));
}
