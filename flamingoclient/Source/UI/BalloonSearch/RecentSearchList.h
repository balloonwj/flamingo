/**
 *  主面板搜索框的「最近搜索」列表（纯逻辑，不依赖窗口与配置文件）。
 *
 *  用户在搜索下拉里打开一条结果、或在搜索框里按回车时记一次当时的关键字；搜索框获得焦点且
 *  为空时，下拉列出这些关键字，点一下就填回搜索框再搜一次。列表按最近使用排在前面，重复的
 *  关键字只留一份，最多 kRecentSearchCapacity 条。按账号保存在用户配置（CUserConfig 的
 *  [Search] Recent），本类负责与那一行文字互相转换。
 *
 *  典型用法：
 *      balloonsearch::RecentSearchList list;
 *      list.Deserialize(config.GetRecentSearches());
 *      list.Add(L"设计稿");
 *      config.SetRecentSearches(list.Serialize().c_str());
 *
 *  本文件不 include stdafx.h，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace balloonsearch {

// 最多记住多少个关键字。下拉在搜索框下方一行一个地列出，10 个刚好不需要滚动。
const size_t kRecentSearchCapacity = 10;

// 单个关键字最多保留的字符数（UTF-16 单元）。更长的截断后再记：搜索框里一般不会输入这么长，
// 截断主要是为了限制配置文件里这一行的长度。
const size_t kRecentSearchMaxChars = 32;

// 序列化时关键字之间的分隔符（U+001F 单元分隔符）。单行搜索框里输不进控制字符，关键字本身
// 不会含有它。
const wchar_t kRecentSearchSeparator = L'\x1F';

/**
 *  RecentSearchList —— 最近搜索的关键字列表，最近使用的排在最前。
 */
class RecentSearchList
{
public:
    RecentSearchList();

    /**
     *  记一次关键字：去掉首尾空白，过长的截断到 kRecentSearchMaxChars；空串不记。已有同样的
     *  关键字（英文字母不区分大小写）时把它挪到最前并换成这次的写法；超过容量时丢掉最旧的。
     *    keyword：关键字（UTF-16）。
     *  @return 列表有变化返回 true；空关键字、或它本来就排在最前且写法相同时返回 false。
     */
    bool Add(const std::wstring& keyword);

    /**
     *  删掉一个关键字（英文字母不区分大小写）。
     *  @return 找到并删掉返回 true。
     */
    bool Remove(const std::wstring& keyword);

    // 清空列表。
    void Clear();

    // 全部关键字，最近使用的在前。
    const std::vector<std::wstring>& Items() const { return m_items; }

    /**
     *  转成存进配置文件的一行文字：关键字之间用 kRecentSearchSeparator 隔开。
     *  @return 空列表返回空串。
     */
    std::wstring Serialize() const;

    /**
     *  从配置文件读出的一行文字恢复列表（先清空）。逐个按 Add 的规则放回，因此空段、重复与
     *  超出容量的部分都会被整理掉，配置文件被手工改坏也不会出错。
     *    text：Serialize 的结果。
     */
    void Deserialize(const std::wstring& text);

private:
    std::vector<std::wstring> m_items;   // 关键字，最近使用的在前，最多 kRecentSearchCapacity 个
};

} // namespace balloonsearch
