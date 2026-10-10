/**
 *  主面板搜索下拉「文件」标签里群文件那一段的请求状态（纯逻辑，不依赖窗口与网络）。
 *
 *  群文件要向服务端搜索（cmd_search_group_files），应答是异步回来的。用户连续输入时，
 *  上一个关键字的应答可能在下一个关键字的请求发出之后才到，若照单全收，就会把旧关键字的
 *  结果显示在新关键字下面。本类记下最近一次发出请求的关键字，应答回显的关键字与它不同就
 *  丢弃；同时记下「没连上服务器、请求没有发出」与「服务端返回失败」两种情况，供下拉显示
 *  相应的提示文字。
 *
 *  典型用法（SearchDropdown 内）：
 *      //延迟查询到点
 *      if (宿主报告已发出) m_groupFileState.OnRequestSent(kw); else m_groupFileState.OnOffline(kw);
 *      //收到应答
 *      if (m_groupFileState.OnResponse(result.m_keyword, result.m_code == 0)) { 保存结果并刷新 }
 *      //渲染
 *      std::wstring hint = GroupFileStatusHint(m_groupFileState.StatusFor(当前关键字));
 *
 *  「消息」标签末尾的「搜索服务器上的聊天记录」（cmd_global_search）请求与应答的进展规则完全相同，
 *  同样用本类记录，提示文字换用 ServerSearchStatusHint。
 *
 *  本文件不 include stdafx.h，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

namespace balloonsearch {

// 群文件搜索针对某个关键字所处的状态。
enum GroupFileSearchStatus
{
    // 还没有为这个关键字发过请求（刚输入、延迟查询还没到点，或下拉刚打开）
    kGroupFileIdle    = 0,
    // 请求已发出，正在等服务端应答
    kGroupFilePending = 1,
    // 应答已到且成功，结果可以显示
    kGroupFileDone    = 2,
    // 当时没有连上服务器，请求没有发出
    kGroupFileOffline = 3,
    // 应答已到但服务端返回失败
    kGroupFileFailed  = 4,
};

/**
 *  GroupFileSearchState —— 记录最近一次群文件搜索请求的关键字与进展。
 *  关键字一律是去掉首尾空白后的 UTF-8 字节串，与请求里发出、应答里回显的字节完全一致。
 */
class GroupFileSearchState
{
public:
    GroupFileSearchState();

    /**
     *  清空状态（下拉收起、查询词清空、退出登录时调用）。之后迟到的应答一律丢弃。
     */
    void Reset();

    /**
     *  记下已为 keywordUtf8 发出请求，状态变为等待应答。
     *    keywordUtf8：请求里发出的关键字。
     */
    void OnRequestSent(const std::string& keywordUtf8);

    /**
     *  记下为 keywordUtf8 搜索时没有连上服务器、请求没有发出。
     *    keywordUtf8：当时的关键字。
     */
    void OnOffline(const std::string& keywordUtf8);

    /**
     *  收到一次应答。回显的关键字与最近一次发出的不同、或此刻并不在等应答时丢弃。
     *    echoedKeywordUtf8：应答里回显的关键字。
     *    success：服务端是否返回成功。
     *  @return 应答被采纳（调用方应保存并显示其结果）返回 true；被丢弃返回 false。
     */
    bool OnResponse(const std::string& echoedKeywordUtf8, bool success);

    /**
     *  取某个关键字当前的状态。与最近一次记录的关键字不同时返回 kGroupFileIdle。
     *    keywordUtf8：当前查询词。
     *  @return 状态，取值见 GroupFileSearchStatus。
     */
    GroupFileSearchStatus StatusFor(const std::string& keywordUtf8) const;

private:
    std::string           m_keyword;  // 最近一次发出（或因未连接而没能发出）请求的关键字，UTF-8
    GroupFileSearchStatus m_status;   // m_keyword 对应的状态；Reset 后为 kGroupFileIdle
};

/**
 *  按状态给出「群文件」那一段要显示的提示文字。
 *    status：StatusFor 的返回值。
 *  @return 等待应答时为「正在搜索群文件…」，没连上服务器时为「群文件需联网搜索」，
 *          服务端返回失败时为「群文件搜索失败」；其余状态返回空串（不显示提示）。
 */
std::wstring GroupFileStatusHint(GroupFileSearchStatus status);

/**
 *  按状态给出「服务器上的聊天记录」那一段要显示的提示文字。
 *    status：StatusFor 的返回值。
 *  @return 等待应答时为「正在搜索服务器…」，没连上服务器时为「搜索服务器上的聊天记录需要联网」，
 *          服务端返回失败时为「搜索服务器失败，请稍后重试」；其余状态返回空串（不显示提示）。
 */
std::wstring ServerSearchStatusHint(GroupFileSearchStatus status);

} // namespace balloonsearch
