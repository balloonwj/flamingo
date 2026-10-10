/**
 *  flamingoAdmin —— 界面上的时间显示格式。
 *
 *  控制台各界面显示时间统一经由本文件，按三种样式：
 *    - Date   「YYYY-MM-DD」：日期类字段 —— 企业开通日期、企业详情的「开通」、服务号「创建于」、
 *             员工入职日期；
 *    - Minute 「YYYY-MM-DD HH:MM」：列表与卡片里的时间 —— 公告列表、仪表板的公告与审计卡片、
 *             客户端版本、敏感词词条；
 *    - Second 「YYYY-MM-DD HH:MM:SS」：日志类记录与详情 —— 审计日志、拦截记录、文件共享审计、
 *             登录与操作历史、公告详情。
 *  服务端给出的时间文字是「YYYY-MM-DD HH:MM:SS」（个别字段只到分钟或只有日期），用
 *  FormatTimeText；客户端自己由时间戳换算出的 SYSTEMTIME 用 FormatSystemTime。单元测试见
 *  Tests/TimeTextTests.cpp（FMT1）。
 *
 *  典型用法：
 *      label->SetText(timetext::FormatTimeText(a.m_sentAt, timetext::TimeStyle::Minute));
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

namespace flamingoadmin {

namespace timetext {

// 时间显示样式，规则见文件头注释。
enum class TimeStyle
{
    Date,    // 只显示日期「YYYY-MM-DD」：日期类字段
    Minute,  // 显示到分钟「YYYY-MM-DD HH:MM」：列表与卡片里的时间
    Second,  // 显示到秒「YYYY-MM-DD HH:MM:SS」：日志类记录与详情
};

// 按样式格式化服务端给出的时间文字。
//   text：「YYYY-MM-DD」「YYYY-MM-DD HH:MM」或「YYYY-MM-DD HH:MM:SS」，首尾空白会去掉，日期与
//         时间之间允许是「T」，秒之后的小数部分会去掉。
//   style：显示样式。
// 返回：格式化后的文字。输入比样式要求的短时只给出输入里有的部分（不补出「:00」）；
//       开头不是「YYYY-MM-DD」的文字（空串、「—」等）原样返回。
CString FormatTimeText(const CString& text, TimeStyle style);

// 按样式格式化一个 SYSTEMTIME（本地时间）。
//   st：时间；style：显示样式。
CString FormatSystemTime(const SYSTEMTIME& st, TimeStyle style);

} // namespace timetext

} // namespace flamingoadmin
