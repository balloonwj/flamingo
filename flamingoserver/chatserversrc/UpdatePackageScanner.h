/**
 *  客户端升级包目录扫描器，UpdatePackageScanner.h
 *
 *  控制台（flamingoAdmin）发布新版本时，管理员需要从"服务器上已经放好的升级包"里挑一个。
 *  运维把 zip 手工放进升级包目录，文件名是人起的；而版本记录里要存的是内容 MD5 与字节数
 *  ——这两个值不能让人在界面上手打（32 位十六进制串，错一个字符就是客户端下载失败）。
 *  本模块负责把目录里的文件连同这两个值一起列出来，交给控制台做下拉选择。
 *
 *  与 fileserver 侧的 UpdatePackageIndex 的分工：
 *    - 本模块（chatserver）：把目录里<u>所有</u>文件列给人看，含"看着不像 zip"的那些，
 *      好让管理员发现自己放错了文件；不建索引、不做缓存，每次调用现扫。
 *    - UpdatePackageIndex（fileserver）：只维护「MD5 → 路径」的映射供下载时反查，
 *      带缓存与节流。
 *  两者共用 utils/FileEnum 的目录枚举与 MD5 计算，不各写一份平台分支。
 *
 *  chatserver 对升级包目录<u>只读</u>，从不往里写文件——包的落盘由运维负责。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//升级包目录里的一个候选文件
struct UpdatePackageCandidate
{
    std::string filename;   //文件名，不含路径；控制台下拉里显示的就是它
    int64_t     size;       //文件字节数
    std::string md5;        //内容MD5，小写十六进制32位；建版本记录时写进 f_package_md5
    bool        iszip;      //文件头是否为 zip 魔数 PK\x03\x04。false 表示多半放错了文件，
                            //控制台应把该项标灰、不允许选中

    UpdatePackageCandidate()
        : size(0), iszip(false)
    {
    }
};

/**
 *  扫描升级包目录，列出其中的候选包。
 *
 *    dir： 升级包目录（updatecachedir 解析后的绝对路径）；空串直接返回空列表
 *    out： [出参] 候选包列表；<u>进函数先清空</u>，按文件名字典序排列
 *
 *  只扫一层，子目录一律忽略——那里留给运维放归档或临时文件。
 *
 *  与索引不同，本函数<u>不对同内容的文件去重</u>：两个内容相同、名字不同的文件都会
 *  列出来。这是有意的——管理员需要看到目录里真实有什么，而不是一个被去重过的视图。
 *
 *  目录不存在或不可读不算错误，只是得到空列表。
 *
 *  每个文件都要完整读一遍来算 MD5，故本函数<u>耗时随目录总字节数增长</u>，
 *  调用方（HTTP 接口）应意识到它不是一个廉价调用。
 */
void ScanUpdatePackages(const std::string& dir, std::vector<UpdatePackageCandidate>& out);

//升级包目录的持有者；全局单例，经 Singleton<UpdatePackageScanner> 访问。
//只负责记住"目录在哪"并把扫描转交给上面那个自由函数——扫描逻辑留在函数里，
//是为了让它不依赖单例、可以直接单测。
class UpdatePackageScanner final
{
public:
    UpdatePackageScanner();
    ~UpdatePackageScanner();

    UpdatePackageScanner(const UpdatePackageScanner& rhs) = delete;
    UpdatePackageScanner& operator=(const UpdatePackageScanner& rhs) = delete;

    /**
     *  记录升级包目录。由 chatserver 的 main 在读完配置、把相对路径解析成
     *  绝对路径之后调用一次。
     *
     *    dir： 升级包目录的绝对路径；空串表示未配置该功能，此后扫描一律返回空列表
     */
    void init(const std::string& dir);

    //当前配置的升级包目录；未配置时为空串。控制台的应答里会带上它，
    //好让管理员知道该把包放到哪儿去
    const std::string& dir() const;

    //扫描当前目录，语义同 ScanUpdatePackages
    void scan(std::vector<UpdatePackageCandidate>& out) const;

private:
    std::string m_dir;   //升级包目录的绝对路径；空串表示未配置
};
