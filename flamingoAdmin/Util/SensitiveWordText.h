/**
 *  flamingoAdmin —— 敏感词管理页用到的纯文本逻辑：导入文件的解码、按行切分与分批、导出文件的
 *  存盘格式，以及分类名与拦截来源名。全部为纯函数，不依赖界面与网络，单元测试见
 *  Tests/SensitiveWordTests.cpp 的 SA-3～SA-6；设计见 docs/敏感词方案.md 第 7.4 节。
 *
 *  每一行的「词条[Tab 分类名[Tab 备注]]」解析、去首尾空白与校验都在服务端完成
 *  （chatserversrc/SensitiveWordManager 的 ParseImportLine），客户端只负责把文字切成行、
 *  分批提交，并保证服务端报回的行号能换算回原文行号。
 *
 *  典型用法（导入）：
 *      CString text = swtext::DecodeImportFile(fileBytes, NULL);
 *      std::vector<std::string> lines = swtext::SplitLines(text);
 *      std::vector<swtext::ImportBatch> batches =
 *          swtext::MakeImportBatches(lines, swtext::kImportBatchMaxLines);
 *      //逐批调用 import.do；服务端报回的行号加上 m_firstLine - 1 即为原文行号
 *
 *  典型用法（导出）：
 *      std::string fileBytes = swtext::BuildExportFileBytes(exportResult.m_csvUtf8);
 *      //写进 .txt 文件
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include <vector>

namespace flamingoadmin { namespace swtext {

// 导入文件的大小上限（字节）：2 MB。按一行十几个字节估算约十万行，远超每个企业 10000 条的词库上限，
// 再大多半是选错了文件
const size_t kImportFileMaxBytes = 2 * 1024 * 1024;

// 每次 import.do 最多提交的行数，须与服务端 SENSITIVE_IMPORT_MAX_LINES 一致
const int kImportBatchMaxLines = 1000;

// 每个企业的词条上限，须与服务端 SENSITIVE_WORDS_PER_ENTERPRISE 一致（界面显示「已用 N / 10000」）
const int kWordsPerEnterprise = 10000;

// 单个词条最多字符数，须与服务端 SENSITIVE_WORD_MAX_CHARS 一致（添加 / 编辑弹窗的输入框按它限长）
const int kWordMaxChars = 32;

// 备注最多字符数，须与服务端 SENSITIVE_REMARK_MAX_CHARS 一致
const int kRemarkMaxChars = 64;

// 分类的取值个数：取值为 1～kCategoryCount，与服务端 sensitive_word_category 一致
const int kCategoryCount = 6;

// 「其他」分类的取值：添加时未选分类、导入时只有词条或分类名写错的词条归入此类
const int kCategoryOther = 6;

// 拦截来源的取值个数：取值为 1～kHitSourceCount，与服务端 sensitive_hit_source 一致
const int kHitSourceCount = 21;

// 导入文件识别出的编码
enum class TextEncoding
{
    Utf8Bom,   // 以 EF BB BF 开头的 UTF-8（记事本「UTF-8 带 BOM」、本程序导出的文件）
    Utf16Le,   // 以 FF FE 开头的 UTF-16 小端（记事本旧版的「Unicode」）
    Utf16Be,   // 以 FE FF 开头的 UTF-16 大端
    Utf8,      // 没有 BOM、且整段都是合法的 UTF-8
    Gbk,       // 没有 BOM、且不是合法的 UTF-8：按代码页 936 解码（中文 Windows 上记事本的「ANSI」）
};

// 一批要提交给 import.do 的行
struct ImportBatch
{
    int                      m_firstLine;  // 本批第一行在原文里的行号（从 1 起）
    std::vector<std::string> m_lines;      // 本批的各行（UTF-8），空白行保留在原位

    ImportBatch()
        : m_firstLine(1)
    {
    }
};

/**
 *  解码导入文件的原始字节。有 BOM 时按 BOM 解码；没有 BOM 时，整段是合法 UTF-8 就按 UTF-8，
 *  否则按 GBK。
 *  @param bytes    文件内容（原始字节）
 *  @param detected [出参，可为 NULL] 识别出的编码；空文件记为 Utf8
 *  @return 解码后的文字，不含 BOM；空文件返回空串。UTF-16 的字节数为奇数时，最后一个字节舍去
 */
CString DecodeImportFile(const std::string& bytes, TextEncoding* detected);

/**
 *  把文字切成行，转成 UTF-8。\r\n、\n、\r 都算一个换行。空行保留在原位，使第 i 个元素就是
 *  原文第 i + 1 行；文字以换行结尾时，最后那个换行之后的空串不算一行。首尾空白原样保留，
 *  交给服务端处理。
 *  @param text 文字
 *  @return 各行（UTF-8）；空文字返回空数组
 */
std::vector<std::string> SplitLines(const CString& text);

/**
 *  判断一行是否只有空白：ASCII 空白字符（空格、Tab、\r、\n、\v、\f）与全角空格（U+3000），
 *  口径与服务端去首尾空白时相同。
 *  @param line 一行（UTF-8）
 *  @return 空串或只有空白时返回 true
 */
bool IsBlankLine(const std::string& line);

/**
 *  统计有内容的行数（不是只有空白的行），导入弹窗显示「共 N 行」时用。
 *  @param lines 各行（UTF-8）
 *  @return 行数
 */
int CountNonBlankLines(const std::vector<std::string>& lines);

/**
 *  把各行按每批最多 maxLines 行分批，每批记下第一行在原文里的行号。全是空白行的批次
 *  不收进结果（提交了也一条都不会加），但后面批次的行号照常按原文计算。
 *  @param lines    各行（UTF-8），通常是 SplitLines 的结果
 *  @param maxLines 每批最多行数；不大于 0 时按 kImportBatchMaxLines 处理
 *  @return 各批，按原文顺序排列；没有可提交的内容时为空
 */
std::vector<ImportBatch> MakeImportBatches(const std::vector<std::string>& lines, int maxLines);

/**
 *  把 export.do 返回的文字转成存盘用的字节：开头加 UTF-8 BOM，换行统一为 \r\n，便于管理员用
 *  记事本直接打开。本程序导入时能原样读回（DecodeImportFile + SplitLines）。
 *  @param exportUtf8 export.do 返回的文字（UTF-8，每行以 \n 结尾）
 *  @return 存盘用的字节
 */
std::string BuildExportFileBytes(const std::string& exportUtf8);

/**
 *  分类的中文名，与服务端导入导出用的分类名一致。
 *  @param category 分类取值，1～kCategoryCount
 *  @return 中文名；取值不认识时返回「其他」
 */
CString CategoryName(int category);

/**
 *  拦截来源的中文名，拦截记录页签的「来源」列与来源筛选用。
 *  @param source 来源取值，1～kHitSourceCount
 *  @return 中文名；取值不认识时返回「未知来源」
 */
CString HitSourceName(int source);

}} // namespace flamingoadmin::swtext
