/**
 *  flamingoAdmin —— 汉字拼音首字母工具(用于"拼音首字符"搜索)。
 *
 *  把一段文本转成"拼音首字母串":每个常用汉字 → 其拼音首字母(小写),ASCII
 *  字母 / 数字 → 自身小写,其余字符(标点 / 空格等)跳过。例:
 *    "研发部"   → "yfb"
 *    "张三"     → "zs"
 *    "IT部"     → "itb"
 *  实现用 GB2312 编码区间法(轻量,无大表):把汉字转 GBK 双字节码,按各拼音
 *  首字母在 GB2312 区位的起始码边界查表定位。只覆盖 GB2312 一级常用字
 *  (3755 字),二级 / 生僻字可能取不到(跳过);对组织树这类人名 / 部门名搜索
 *  足够。纯函数,可独立单测。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include <atlstr.h>

namespace flamingoadmin {

// 取单个汉字 GBK 双字节码对应的拼音首字母(小写 a-z);非常用字 / 越界返回 0。
//   gbkCode：汉字的 GBK 码(高字节<<8 | 低字节)。
char PyInitialOf(unsigned gbkCode);

// 把文本转成拼音首字母串(小写)。汉字取首字母,ASCII 字母/数字保留(小写),
// 其余字符跳过。供"拼音首字符"搜索匹配使用。
//   text：原文本(宽字符)。
CString PinyinInitials(const CString& text);

} // namespace flamingoadmin
