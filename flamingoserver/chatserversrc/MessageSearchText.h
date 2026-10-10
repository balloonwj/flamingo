/**
 *  从消息内容生成用于关键字搜索的纯文本，MessageSearchText.h
 *
 *  t_chatmsg.f_content 是 jsoncpp 写出的 JSON，中文被转义成 \uXXXX，直接对它做 LIKE 搜不到
 *  中文、搜英文又会命中键名（2026-10-04 修复）。消息入库与编辑时由本文件的函数提取一份纯文本
 *  存进 f_search_text，会话内搜索（cmd_search_messages 1515）与全局搜索（cmd_global_search 1904）
 *  都查这一列。提取口径与 PC 客户端本地库的 search_text 相同（客户端 V2MsgQuery.cpp 的
 *  BuildSearchText）：
 *    · 文本：正文，内联表情 /f["数字"] 换成「[表情]」，转义的「//」还原为「/」；
 *    · 系统提示：提示文字；
 *    · 文件：文件名；文件夹传输记录：文件夹名；文件共享卡片：共享名；
 *    · 群接龙卡片：标题，有说明时再加一个空格与说明；
 *    · 合并转发：标题加上每条子消息的搜索文本，以空格分隔，嵌套的记录逐层展开；
 *    · 其余类型：空串。
 *  结果去掉四字节的 UTF-8 字符（MySQL 的 utf8 字符集存不下），并按字符边界截到
 *  kSearchTextMaxBytes 字节。本文件只放纯函数，不碰数据库，便于单元测试。
 *
 *  典型用法：
 *      std::string searchText = BuildStoredSearchText(msgtype, storedContent);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace chatserver {

// 搜索文本最多保留多少字节（UTF-8）。普通消息远小于它；合并转发的长记录可能有上百条子消息，
// 截到这个长度足够覆盖用户会去搜的内容，也不让这一列比消息本身还大。
const size_t kSearchTextMaxBytes = 4000;

// 合并转发最多展开几层嵌套。客户端转发时最多嵌套几层由界面限制，这里另设上限，防止构造出来的
// 深层嵌套 JSON 让递归过深。
const int kSearchTextMaxMergedDepth = 8;

/**
 *  从入库时的打包内容（MessageProtocol::buildStoredContent 的结果，形如
 *  {"content":{...},"mentions":[...],...}）生成搜索文本。
 *    msgtype：消息类型，取值见 Protocol.h 的 message_type。
 *    storedContent：打包内容 JSON（UTF-8）。解析失败、没有 content 对象时返回空串。
 *  @return 搜索文本（UTF-8，已去掉四字节字符、已截断）；没有可搜文字的类型返回空串。
 */
std::string BuildStoredSearchText(int32_t msgtype, const std::string& storedContent);

/**
 *  从消息的 content 对象 JSON 文本（协议里消息体的 content 字段）生成搜索文本，口径同上。
 *    msgtype：消息类型。
 *    contentJson：content 对象的 JSON 文本；解析失败或不是对象时返回空串。
 *  @return 搜索文本（UTF-8，已去掉四字节字符、已截断）。
 */
std::string BuildContentSearchText(int32_t msgtype, const std::string& contentJson);

/**
 *  文本消息正文里的内联表情 /f["数字"] 换成「[表情]」，转义的「//」还原为「/」，残缺的写法
 *  按普通文字保留。与客户端 DigestFaceTokens 同一规则。
 *    body：正文（UTF-8）。
 *  @return 处理后的正文。
 */
std::string DigestFaceTokensForSearch(const std::string& body);

/**
 *  去掉四字节的 UTF-8 字符（U+10000 以上，表情符号等）。MySQL 的 utf8 字符集每个字符最多三个
 *  字节，写入四字节字符会报错或被截断。不完整的多字节序列也一并去掉。
 *    text：UTF-8 文本。
 *  @return 只含一至三字节字符的文本。
 */
std::string StripFourByteUtf8(const std::string& text);

/**
 *  按字符边界截断 UTF-8 文本，不会截出半个字符。
 *    text：UTF-8 文本。
 *    maxBytes：最多保留的字节数。
 *  @return 截断后的文本；原本不超过 maxBytes 时原样返回。
 */
std::string TruncateUtf8(const std::string& text, size_t maxBytes);

/**
 *  从文字里截取关键字附近的一段，作为搜索结果的摘录：关键字前面至多保留 maxBytes 的四分之一，
 *  整段（含省略号）不超过 maxBytes 字节，按字符边界截断；前面或后面被截掉时分别加省略号「…」。
 *  换行、回车、制表符换成空格。找不到关键字（ASCII 字母不区分大小写，与 LIKE 一致）时取开头一段。
 *    text：搜索文本（UTF-8）。
 *    keyword：关键字（UTF-8）。
 *    maxBytes：摘录最多多少字节，须不小于 kExcerptMinBytes，更小时按它处理。
 *  @return 摘录（UTF-8）。
 */
std::string MakeSearchExcerpt(const std::string& text, const std::string& keyword, size_t maxBytes);

/**
 *  把关键字转成「包含」的 LIKE 匹配串：前后各加一个 %，并在 ! % _ 三个字符前面加 !，配合语句里的
 *  ESCAPE '!' 让它们按普通字符匹配（否则搜「50%」会把「500」也搜出来）。写法与
 *  GroupFileManager::buildNameLikePattern 相同。结果还要再经 dbapi::EscapeString 转义引号才能拼进 SQL。
 *    keyword：关键字（UTF-8）。
 *  @return 匹配串。
 */
std::string MakeLikeContainsPattern(const std::string& keyword);

// MakeSearchExcerpt 的 maxBytes 下限：两个省略号（各三字节）之外至少还能放下几个字。
const size_t kExcerptMinBytes = 24;

} // namespace chatserver
