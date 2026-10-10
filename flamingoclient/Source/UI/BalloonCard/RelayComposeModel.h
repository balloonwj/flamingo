/**
 *  RelayComposeModel.h
 *
 *  「发起接龙」窗口（RelayComposeDlg）的纯逻辑：截止时间的年月日时分秒与 Unix 毫秒的换算，以及点「发起」
 *  之前对标题、说明、截止时间、「我的接龙」的检查与提示文字。不碰窗口，可被 FlamingoUnit 直接测试。
 *  交互设计见 docs/群接龙方案.md §3.1、§3.5。
 *
 *  2026-10-03 起截止时间必须设置，由用户填写年月日时分秒，不能早于当前时间 + 5 分钟（原先是「不限、
 *  今天 18:00、明天 18:00、3 天后、7 天后」几个档位，已删除）。
 *
 *  典型用法（发起窗口打开时填初始值，点「发起」时检查）：
 *      ballooncard::RelayDeadlineFields f = ballooncard::DefaultRelayDeadlineFields(nowMs);
 *      ...（用户在六个微调框里修改 f）
 *      std::string contentJson, entry;
 *      int check = ballooncard::CheckRelayCompose(title, note, ballooncard::RelayDeadlineFieldsToMs(f), nowMs,
 *                                                 myEntry, contentJson, entry);
 *      if (check != ballooncard::kRelayComposeOk) 提示 RelayComposeCheckText(check)
 *
 *  本头文件使用 CString，包含它之前须已引入 ATL 的 atlstr.h（实现文件见 RelayComposeModel.cpp 开头）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace ballooncard {

/**
 *  截止时间的年月日时分秒，按本机时区理解，即发起窗口里六个微调框的取值。
 */
struct RelayDeadlineFields
{
    int m_year;     //年，四位数，如 2026
    int m_month;    //月，1~12
    int m_day;      //日，1~31；这一天是否存在（如 2 月 30 日）由 RelayDeadlineFieldsToMs 判断
    int m_hour;     //时，0~23
    int m_minute;   //分，0~59
    int m_second;   //秒，0~59

    RelayDeadlineFields()
        : m_year(0)
        , m_month(0)
        , m_day(0)
        , m_hour(0)
        , m_minute(0)
        , m_second(0)
    {
    }
};

/**
 *  打开发起窗口时截止时间的初始值：当前时间加 1 小时，秒取 0（本机时区）。这个值一定满足
 *  「不早于当前时间 + 5 分钟」，用户不改也能直接发起。
 *  @param nowMs 本机当前时间，Unix 毫秒
 *  @return 初始的年月日时分秒；本机时间换算失败时各项为 0（随后 RelayDeadlineFieldsToMs 返回 0）
 */
RelayDeadlineFields DefaultRelayDeadlineFields(int64_t nowMs);

/**
 *  把年月日时分秒（本机时区）换算成 Unix 毫秒。
 *  @param fields 年月日时分秒
 *  @return 换算结果；某一项超出范围、或日期不存在（如 2026 年 2 月 29 日、4 月 31 日）时返回 0。
 *          不存在的日期不能交给 mktime 顺延成别的日子，否则用户填的时间会被悄悄改掉
 */
int64_t RelayDeadlineFieldsToMs(const RelayDeadlineFields& fields);

//点「发起」之前检查的结果，由 CheckRelayCompose 给出
enum RelayComposeCheck
{
    kRelayComposeOk             = 0,   //可以发起
    kRelayComposeTitleEmpty     = 1,   //标题去掉首尾空白后为空
    kRelayComposeTitleMultiline = 2,   //标题中间含换行
    kRelayComposeTitleTooLong   = 3,   //标题超过 kRelayTitleMaxChars 个字符
    kRelayComposeNoteTooLong    = 4,   //说明超过 kRelayNoteMaxChars 个字符
    kRelayComposeEntryMultiline = 5,   //「我的接龙」中间含换行
    kRelayComposeEntryTooLong   = 6,   //「我的接龙」超过 kRelayTextMaxChars 个字符
    kRelayComposeDeadlineInvalid = 7,  //截止时间不是有效的日期时间（RelayDeadlineFieldsToMs 换算失败，传进来的是 0）
    kRelayComposeDeadlineTooEarly = 8, //截止时间早于当前时间 + net::kRelayDeadlineMinLeadMs（5 分钟）
};

/**
 *  点「发起」之前检查窗口里填的内容，规则与服务端相同（标题、说明见
 *  net::CardProtocolClient::buildRelayCardContent，截止时间见 net::kRelayDeadlineMinLeadMs，
 *  「我的接龙」见 normalizeRelayText）。按窗口里的顺序检查：标题、说明、截止时间、「我的接龙」。
 *  「我的接龙」是选填的：只含空白时按没填处理。
 *  @param title          标题（UTF-8）
 *  @param note           说明（UTF-8），可以为空
 *  @param deadlineMs     截止时间，Unix 毫秒，由 RelayDeadlineFieldsToMs 换算；0 表示换算失败（日期无效）
 *  @param nowMs          本机当前时间，Unix 毫秒；截止时间不能早于 nowMs + net::kRelayDeadlineMinLeadMs
 *  @param myEntry        「我的接龙」（UTF-8），可以为空
 *  @param outContentJson [出参] 发卡片用的 content；只在返回 kRelayComposeOk 时写入
 *  @param outEntry       [出参] 去掉首尾空白后的「我的接龙」，没填时为空串；只在返回 kRelayComposeOk 时写入
 *  @return 取值见 RelayComposeCheck
 */
int CheckRelayCompose(const std::string& title, const std::string& note, int64_t deadlineMs, int64_t nowMs,
                      const std::string& myEntry, std::string& outContentJson, std::string& outEntry);

/**
 *  检查不通过时显示在窗口里的提示文字。
 *  @param check CheckRelayCompose 的返回值
 *  @return 提示文字；kRelayComposeOk 返回空串
 */
CString RelayComposeCheckText(int check);

/**
 *  发出卡片之后，发送回执失败（cmd_send_message 应答 code 非 0）时显示在窗口里的提示文字。
 *  @param code 回执里的错误码，取值见 enum error_code
 *  @return 提示文字；认不出的错误码写成「发起接龙失败（错误码 N）。」
 */
CString RelaySendFailureText(int code);

} // namespace ballooncard
