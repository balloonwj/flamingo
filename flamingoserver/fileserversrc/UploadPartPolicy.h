/**
 *  上传分片文件的命名与收尾策略，UploadPartPolicy.h
 *
 *  fileserver 与 imgserver 收上传时，先把字节写进一个分片临时文件，收齐并校验 MD5
 *  之后再改名为最终文件 <md5>。原先所有会话一律使用 <md5>.part：同一个文件被
 *  <u>多个连接同时上传</u>时（客户端把同一个文件一次发给多个会话、或群文件与单聊同时
 *  发同一个文件），几个会话共用同一个分片文件，Windows 上先收齐的那个会话改名时，
 *  别的会话仍以写方式打开着该文件，改名失败，客户端收到「finalize file failed」；
 *  Linux 上据推测会因多方交错写入同一文件而校验 MD5 失败。
 *
 *  本文件给出的规则：
 *    - 同一 md5 的公共分片 <md5>.part 同一时刻只允许一个会话使用，由 UploadPartRegistry
 *      登记。占到登记的会话照旧用 <md5>.part，保留断点续传能力；
 *    - 没占到登记的会话改用本会话私有的分片 <md5>.<序号>.part，从头写起，
 *      上传中止或连接断开时删除（序号每个会话不同，别的会话无法续传它）；
 *    - 收尾改名时，若最终文件已由别的会话先行生成（含「改名失败但最终文件已存在」
 *      这一竞态），视为上传成功、删除本会话的分片，内容由 MD5 保证一致。
 *
 *  本文件只依赖标准库与文件系统，不依赖网络类型，便于单元测试
 *  （见 tests/test_uploadpart.cpp，Suite UPP）。
 *
 *  典型用法（见 fileserversrc/FileSession.cpp 的 onUploadChunk）：
 *      bool shared = Singleton<fileserver::UploadPartRegistry>::Instance().claim(md5);
 *      std::string partPath = fileserver::MakeUploadPartPath(baseDir, md5, shared,
 *                                 shared ? 0 : fileserver::NextUploadPartTag());
 *      ...收齐并校验 MD5 之后...
 *      fileserver::upload_finalize_result r =
 *          fileserver::FinalizeUploadPart(partPath, baseDir + md5, finalKnownExists);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once
#include <stdint.h>
#include <mutex>
#include <set>
#include <string>

namespace fileserver {

/**
 *  公共续传分片的占用登记表：记录哪些 md5 的 <md5>.part 正被某个会话写入。
 *
 *  进程内全局一份（Singleton<UploadPartRegistry>），由各 I/O 线程上的 FileSession
 *  并发访问，所有方法都在内部加锁。md5 按小写登记，大小写不同的同一 md5 视为同一项。
 */
class UploadPartRegistry
{
public:
    /**
     *  尝试占用某个 md5 的公共分片。
     *
     *  @param md5 文件 MD5 的十六进制串，大小写均可
     *  @return true 表示占用成功，调用方此后独占 <md5>.part，用完必须调用 release；
     *          false 表示已被别的会话占用，调用方应改用私有分片
     */
    bool claim(const std::string& md5);

    /**
     *  释放此前由 claim 占到的公共分片。释放一个未被占用的 md5 不做任何事。
     *
     *  @param md5 与 claim 时相同的 MD5 串（大小写不必一致）
     */
    void release(const std::string& md5);

    /**
     *  查询某个 md5 的公共分片当前是否被占用，供日志与测试使用。
     *
     *  @param md5 文件 MD5 的十六进制串，大小写均可
     *  @return true 表示已被某个会话占用
     */
    bool isClaimed(const std::string& md5) const;

private:
    mutable std::mutex    m_mutex;     //保护 m_claimed；claim / release / isClaimed 都在锁内完成
    std::set<std::string> m_claimed;   //当前被占用的 md5（小写），会话 release 后移除
};

/**
 *  取一个新的私有分片序号。进程内单调递增、线程安全，从 1 开始。
 *
 *  进程重启后序号从头计起，可能与上次异常退出时残留的私有分片重名；私有分片
 *  一律以「新建并截断」方式打开，重名只会覆盖那份无人使用的残留文件，不影响正确性。
 *
 *  @return 本次分配的序号，每次调用都不同
 */
uint64_t NextUploadPartTag();

/**
 *  生成分片临时文件的完整路径。
 *
 *  @param baseDir 文件存储根目录，须以路径分隔符结尾（与 FileSession::m_strFileBaseDir 一致）
 *  @param md5     文件 MD5 串，原样用作文件名的一部分
 *  @param shared  true 表示本会话占到了公共分片，返回 <baseDir><md5>.part；
 *                 false 表示使用私有分片，返回 <baseDir><md5>.<tag>.part
 *  @param tag     私有分片序号（取自 NextUploadPartTag）；shared 为 true 时忽略
 *  @return 分片文件的完整路径
 */
std::string MakeUploadPartPath(const std::string& baseDir, const std::string& md5,
                               bool shared, uint64_t tag);

//分片收尾（改名为最终文件）的结果
enum upload_finalize_result
{
    upload_finalize_renamed = 0,   //本会话的分片已改名为最终文件，调用方须把该文件登记进 FileManager
    upload_finalize_existed = 1,   //最终文件已由别的会话先行生成（调用前已知，或改名失败后发现已存在），
                                   //本会话的分片已删除；内容由 MD5 保证一致，按上传成功处理
    upload_finalize_failed  = 2,   //改名失败且最终文件也不存在，按服务端内部错误处理；分片文件保持原样，
                                   //由调用方决定是否删除
};

/**
 *  把已收齐并通过 MD5 校验的分片改名为最终文件。
 *
 *  @param partPath         分片文件完整路径，调用前须已关闭（Windows 上打开着的文件无法改名）
 *  @param finalPath        最终文件完整路径（<baseDir><md5>）
 *  @param finalKnownExists 调用方是否已知最终文件存在（通常取 FileManager::isFileExsit）。
 *                          为 true 时不再尝试改名，直接删除分片
 *  @return 收尾结果，见 enum upload_finalize_result
 *  副作用：改名成功时分片文件不复存在；结果为 upload_finalize_existed 时删除分片文件。
 */
upload_finalize_result FinalizeUploadPart(const std::string& partPath,
                                          const std::string& finalPath,
                                          bool finalKnownExists);

} // namespace fileserver
