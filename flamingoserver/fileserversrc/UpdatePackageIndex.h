/**
 *  客户端升级包目录索引，UpdatePackageIndex.h
 *
 *  升级包与聊天文件<u>分目录存放</u>：聊天文件走 filecachedir、升级包走 updatecachedir。
 *  两者的差别不只是位置——聊天文件由客户端经 cmd_file_upload 上传，落盘时就以内容 MD5
 *  命名；升级包则由运维手工放进目录，文件名是 `Flamingo-3.3.0.zip` 这类人能看懂的名字。
 *  而 fileserver 收到的下载令牌里只有 MD5（`utils/FileToken.h` 的载荷是
 *  type|scene|md5|size|uid|expire 六段），没有文件名，故需要本类在两者之间搭一座桥：
 *  扫描升级包目录、逐个算 MD5，建立「MD5 → 实际文件路径」的映射。
 *
 *  为什么不换个更简单的做法（两条都评估过并否掉，记下来免得日后重复讨论）：
 *    - 要求运维把包按 MD5 重命名后再放：fileserver 可以零改动，但把"算 MD5 再改名"
 *      这件机械活推给了人，正是引入控制台版本管理时要消除的东西。
 *    - 把文件名塞进令牌：载荷是竖线分隔的定长六段，文件名里出现竖线就会把载荷切错，
 *      而且签发与校验两侧都要改。
 *
 *  三条行为约定：
 *    1. <u>只扫一层</u>，子目录一律忽略，留给运维放归档或临时文件；
 *    2. 查不到时会<u>惰性重扫</u>一次，这样刚放进目录的新包不必重启服务就能被找到；
 *    3. 重扫有<u>最小间隔节流</u>——没有它的话，一串伪造 MD5 的下载请求就能让服务器
 *       反复把整个目录的 MD5 算一遍。
 *
 *  典型用法（fileserver 侧）：
 *      UpdatePackageIndex index;
 *      index.init(updateDir, nowMs);                       //启动时全量扫一遍
 *      if (index.find(md5, nowMs, path)) → 打开 path 发送分片
 *
 *  本类不是线程安全的；fileserver 的会话都跑在同一个 EventLoop 线程上，故无需加锁。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>

//两次全量重扫之间的最小间隔，单位毫秒。取 5 秒：运维放完包、在控制台点「扫描」再点
//「新建版本」，中间怎么也不止 5 秒，正常使用感觉不到；同时又能挡住高频的伪造 MD5 探测。
const int64_t kUpdateIndexRescanMinIntervalMs = 5000;

class UpdatePackageIndex;

/**
 *  把下载令牌里的 MD5 解析成磁盘上的实际文件路径。fileserver 下载分片时调用一次。
 *
 *    scene：       文件场景，取值见 Protocol.h 的 enum file_scene
 *    md5：         令牌里携带的内容MD5
 *    nowMs：       当前时刻，Unix 毫秒；仅升级包场景用得到（重扫节流的判定）
 *    fileBaseDir： 聊天文件 / 群文件的存储根目录（filecachedir），须以分隔符结尾
 *    updateIndex： 升级包索引；仅升级包场景会用到，其余场景不碰它
 *    outPath：     [出参] 解析出的完整路径；返回 false 时置空
 *
 *  @return 解析成功返回 true。
 *
 *  两类文件的寻址方式本就不同，差异收敛在本函数里：
 *    - 聊天文件 / 群文件：由客户端上传，落盘时即以内容MD5 命名，直接拼接即可，
 *      <u>不查索引</u>（这条路径每天要走成千上万次，不能引入额外开销）；
 *    - 客户端升级包：由运维手工放入，文件名是人起的，必须经索引反查。
 *
 *  未知或越界的 scene 一律按前一类处理 —— 宁可去老目录找不到文件，也不要让一个
 *  取值异常的场景摸到升级包目录。
 */
bool ResolveDownloadPath(int scene, const std::string& md5, int64_t nowMs,
                         const std::string& fileBaseDir,
                         UpdatePackageIndex& updateIndex,
                         std::string& outPath);

//升级包目录索引：维护「内容 MD5 → 磁盘文件路径」的映射
class UpdatePackageIndex
{
public:
    UpdatePackageIndex();
    ~UpdatePackageIndex();

    UpdatePackageIndex(const UpdatePackageIndex& rhs) = delete;
    UpdatePackageIndex& operator=(const UpdatePackageIndex& rhs) = delete;

    /**
     *  绑定升级包目录并立即做一次全量扫描。
     *
     *    dir：   升级包目录；空串表示未配置该功能，此后 find 一律返回未命中
     *    nowMs： 当前时刻，Unix 毫秒。由调用方传入而非本类自取，便于测试构造确定的时间序
     *
     *  目录不存在或不可读不算错误：只是扫不出任何条目，日志里会留一条记录。
     */
    void init(const std::string& dir, int64_t nowMs);

    /**
     *  按内容 MD5 找升级包的完整路径。
     *
     *    md5：     待查的内容MD5，十六进制串，大小写不敏感（内部按小写比较）
     *    nowMs：   当前时刻，Unix 毫秒；用于重扫节流的判定
     *    outPath： [出参] 命中时回填文件完整路径；未命中时置空
     *
     *  @return 命中返回 true。未命中时会视节流条件决定是否先重扫一次再判：
     *          距上次扫描不足 kUpdateIndexRescanMinIntervalMs 则直接返回 false，
     *          不去碰磁盘。
     */
    bool find(const std::string& md5, int64_t nowMs, std::string& outPath);

    /**
     *  强制全量重扫，忽略节流。
     *
     *    nowMs： 当前时刻，Unix 毫秒；扫描完成后记为"上次扫描时刻"
     *
     *  文件的 MD5 只在「大小或修改时间发生变化」时才重算，未变动的沿用上次结果，
     *  因此重扫一个内容没动过的目录不会产生实际的哈希开销。
     */
    void rescan(int64_t nowMs);

    //当前索引里的条目数，仅供日志与测试断言使用
    size_t size() const;

private:
    //一个已扫描过的文件的元信息，用于判断下次重扫时能否复用其 MD5
    struct Entry
    {
        int64_t     m_size;   //文件字节数
        int64_t     m_mtime;  //最后修改时间，Unix 秒
        std::string m_md5;    //内容MD5，小写十六进制

        Entry()
            : m_size(0), m_mtime(0)
        {
        }
    };

    std::string                        m_dir;          //升级包目录；空串表示功能未启用
    std::map<std::string, Entry>       m_byPath;       //文件完整路径 → 元信息，用于复用 MD5
    std::map<std::string, std::string> m_byMd5;        //内容MD5 → 文件完整路径，查询用
    int64_t                            m_lastScanMs;   //上次全量扫描的时刻，Unix 毫秒；节流基准
};
