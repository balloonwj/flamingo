/**
 *  文件上传 / 下载令牌（FileToken）。
 *
 *  v2 文件传输中，fileserver / imgserver 无需握手与登录——客户端在
 *  chatserver 上申请上传 / 下载时，由 chatserver 签发一枚自校验令牌，
 *  fileserver / imgserver 收到分片后仅凭令牌即可本地鉴权，无需查库、
 *  无需回连 chatserver。
 *
 *  令牌结构：payload + "." + HMAC-MD5(key, payload) 的十六进制串。
 *  payload 为竖线分隔的明文字段：type|scene|md5|size|uid|expire。
 *  字段均为数字或十六进制，不含 "."，故以首个 "." 切分无歧义。明文
 *  payload 不含机密信息（md5 / 大小 / userid / 过期时间均非秘密），
 *  防伪靠 HMAC 签名，故无需加密。
 *
 *  本模块为纯逻辑，只依赖 utils/MD5，不触及网络与数据库，便于单元
 *  测试（见 tests/test_filetoken.cpp）。chatserver 与 fileserver /
 *  imgserver 共同链接本模块。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stdint.h>
#include <string>

//令牌承载的载荷字段；签发时由调用方填充，校验成功后回填给调用方
struct FileTokenPayload
{
    int          type;     //令牌类型：取值见 FileToken::token_type_*
    int          scene;    //文件场景：取值见 enum file_scene（1聊天文件 2群文件 3头像 4图片消息）
    std::string  md5;      //文件内容MD5（小写十六进制，32位）
    int64_t      size;     //文件总字节数
    int64_t      uid;      //申请该令牌的用户userid
    int64_t      expire;   //过期时间，Unix毫秒；校验时 now >= expire 即判定过期

    FileTokenPayload()
        : type(0), scene(0), size(0), uid(0), expire(0)
    {
    }
};

//文件令牌签发 / 校验工具类；全部为静态方法，不可实例化
class FileToken
{
public:
    //令牌类型：上传令牌，仅可用于 cmd_file_upload
    static const int token_type_upload   = 1;
    //令牌类型：下载令牌，仅可用于 cmd_file_download
    static const int token_type_download = 2;

    /**
     *  按 payload 与密钥签发一枚令牌。
     *  @param payload 载荷字段；type/scene/size/uid/expire 由调用方填好，
     *                 md5 须为非空十六进制串
     *  @param key     HMAC 密钥，chatserver 与 fileserver/imgserver 共享，
     *                 来自各自配置文件的 filetokenkey 项
     *  @return 令牌字符串（payload + "." + 签名）
     */
    static std::string sign(const FileTokenPayload& payload, const std::string& key);

    /**
     *  校验并解析一枚令牌：验签 + 验过期。
     *  @param token 待校验的令牌字符串
     *  @param key   HMAC 密钥，须与签发时一致
     *  @param nowMs 当前时间，Unix毫秒；用于过期判定
     *  @param out   [出参] 校验通过时回填解析出的载荷字段
     *  @return 令牌合法且未过期返回 true；格式非法 / 签名不符 / 已过期返回 false
     */
    static bool verify(const std::string& token, const std::string& key,
                       int64_t nowMs, FileTokenPayload& out);

    /**
     *  判断某场景是否属于 imgserver 处理范围（头像、图片消息）。
     *  @param scene 文件场景，取值见 enum file_scene
     */
    static bool isImageScene(int scene);

    /**
     *  判断某场景是否属于 fileserver 处理范围（聊天文件、群文件）。
     *  @param scene 文件场景，取值见 enum file_scene
     */
    static bool isFileScene(int scene);

private:
    FileToken() = delete;
    ~FileToken() = delete;
    FileToken(const FileToken&) = delete;
    FileToken& operator=(const FileToken&) = delete;
};
