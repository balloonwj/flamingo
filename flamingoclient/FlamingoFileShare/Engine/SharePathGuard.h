/**
 *  文件共享的路径安全检查（分享者一端，纯逻辑 + 少量文件系统查询），SharePathGuard.h
 *
 *  访问者在控制通道与传输子会话里送来的路径一律是不可信输入（docs/文件共享方案.md 9.1）：相对共享根、
 *  用 '/' 分隔、UTF-8 编码，共享根本身为空串。分享者执行任何读写之前，都要经过本类的三道检查：
 *    1. IsValidRelativePath：逐段检查相对路径的写法，拒绝一切可能跳出共享根、或在 Windows 上有特殊含义的写法；
 *    2. ResolveUnderRoot：拼成本机绝对路径，并确认仍在共享根之内（按目录边界比较前缀，不区分大小写）；
 *    3. IsReparseSafe：取目标（不存在时取最深一层已存在的上级）的最终路径，确认经目录联接、符号链接解析后
 *       仍落在共享根的最终路径之内。
 *
 *  前两项是纯逻辑；第三项要查询文件系统。本类不打日志，判定失败时调用方回 error_code_share_path_invalid（808）。
 *  单元测试见 FlamingoFileShare/Tests/SharePathGuardTests.cpp（标签 [sharepath]）。
 *
 *  典型用法（分享者处理 sf_list）：
 *      std::wstring abs;
 *      if (!SharePathGuard::ResolveUnderRoot(rootPath, req.path, isSingleFile, abs)
 *          || !SharePathGuard::IsReparseSafe(rootPath, abs))
 *          return error_code_share_path_invalid;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <string>

namespace fileshare {

//相对路径的最大字节数（UTF-8）。超过 MAX_PATH 的长路径是允许的，打开文件时由调用方加 \\?\ 前缀；
//这里只给一个宽松的上限，防止畸形请求带来过长的字符串
const size_t kShareRelPathMaxBytes = 4096;

/**
 *  SharePathGuard —— 文件共享的路径安全检查；全部为静态方法，不可实例化。
 */
class SharePathGuard
{
public:
    /**
     *  检查访问者送来的相对路径的写法。拒绝：反斜杠、以 '/' 开头、空段（含结尾的 '/'）、"." 与 ".."、
     *  含 ':'（盘符与备用数据流）、非法字符 < > " | ? * 与控制字符、段首或段尾的空格、段尾的点、
     *  Windows 保留名（CON、PRN、AUX、NUL、COM1~9、LPT1~9，不区分大小写，带扩展名的也算）、非法 UTF-8、
     *  超过 kShareRelPathMaxBytes。空串表示共享根，合法。
     *  @param utf8Path        相对路径（UTF-8）
     *  @param singleFileShare 是否为单文件共享；为真时只接受空串
     *  @return 写法合法返回 true
     */
    static bool IsValidRelativePath(const std::string& utf8Path, bool singleFileShare);

    /**
     *  把相对路径拼到共享根下，得到本机绝对路径。先按 IsValidRelativePath 检查，再确认结果仍在共享根之内。
     *  @param root            共享根的本机路径（目录可带或不带结尾反斜杠；单文件共享时是文件本身）
     *  @param utf8RelPath     相对路径（UTF-8，'/' 分隔）
     *  @param singleFileShare 是否为单文件共享
     *  @param out             [出参] 本机绝对路径（'\' 分隔，不带结尾反斜杠；盘符根目录保留 "X:\"）
     *  @return 成功返回 true；相对路径非法或结果不在根内返回 false
     */
    static bool ResolveUnderRoot(const std::wstring& root, const std::string& utf8RelPath, bool singleFileShare,
                                 std::wstring& out);

    /**
     *  判断 path 是否就是 root 或位于 root 之下：给 root 补上结尾反斜杠后按前缀比较，不区分大小写，
     *  因此 D:\Projects2 不算 D:\Projects 的子目录。纯字符串比较，不访问文件系统。
     *  @param root 根路径
     *  @param path 待判断的路径
     *  @return 在根内返回 true
     */
    static bool IsUnderRoot(const std::wstring& root, const std::wstring& path);

    /**
     *  检查目录联接、符号链接等重解析点：取 absPath（不存在时取它最深一层已存在的上级）与 root 的最终路径
     *  （GetFinalPathNameByHandleW），要求前者在后者之内。
     *  @param root    共享根的本机路径，必须存在
     *  @param absPath 待检查的本机绝对路径（通常是 ResolveUnderRoot 的结果）
     *  @return 安全返回 true；经重解析点走出共享根、或共享根本身取不到最终路径时返回 false
     */
    static bool IsReparseSafe(const std::wstring& root, const std::wstring& absPath);

private:
    SharePathGuard() = delete;
};

} // namespace fileshare
