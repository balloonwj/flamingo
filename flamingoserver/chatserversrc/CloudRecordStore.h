/**
 *  云端录像的存储层（t_cloud_record / t_cloud_record_share 的读写），CloudRecordStore.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 12.6）。每个方法自己从 DbFactory 取一条连接、执行完即释放，不缓存任何数据，
 *  因此可从任意线程调用（ChatSession 的业务线程、控制连接线程、到期清理定时器）。MySQL 与 SQLite 两种后端都用这里的 SQL，
 *  只用两者共有的写法（分享记录的「有则更新」写成先删后插）。单元测试见 tests/test_cloudrecordserver.cpp（CRS 系列，SQLite）。
 *
 *  典型用法：
 *      CloudRecordStore store;
 *      CloudRecordRow row;  ...填好字段...
 *      if (store.insert(row))  →  row.m_id 为新录像 id
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "CloudRecordRules.h"

//云端录像的存储层；无状态，可直接构造使用
class CloudRecordStore
{
public:
    /**
     *  新建一条录像。
     *  @param row [入出参] 除 m_id 外的字段由调用方填好；成功后回填 m_id
     *  @return 写库成功返回 true
     */
    bool insert(CloudRecordRow& row);

    /**
     *  按 id 取一条录像。
     *  @param found [出参] 有没有这条录像
     *  @return 查询成功（含没找到）返回 true；数据库出错返回 false
     */
    bool get(int64_t id, CloudRecordRow& out, bool& found);

    /**
     *  分页列出某人开启的录像，按 id 从大到小。
     *  @param owner    开启者
     *  @param beforeId 只列 id 小于它的；0 表示从最新的开始
     *  @param limit    最多几条
     *  @param out      [出参] 本页录像
     *  @param hasMore  [出参] 还有没有更早的
     */
    bool listByOwner(int64_t owner, int64_t beforeId, int limit, std::vector<CloudRecordRow>& out, bool& hasMore);

    /**
     *  imgserver 回报录制结束：写结束时间、时长、原始帧文件大小、状态与到期时间。只改状态为「录制中」的录像，
     *  重复的回报不会改动已经结束的录像。
     *  @param changed [出参] 是否确实改了（false 表示录像不存在或已不在录制中）
     */
    bool finishRecording(int64_t id, int status, int64_t endTime, int64_t durationMs, int64_t frecBytes,
                         int64_t expireTime, bool& changed);

    /**
     *  最终文件已上传完：写文件名、大小，状态改为可观看；来源为上传的录像同时写到期时间（expireTime 大于 0 时）。
     *  @param durationMs 大于 0 时同时更新时长（合成后的实际时长）
     */
    bool markReady(int64_t id, const std::string& file, int64_t bytes, int64_t durationMs, int64_t expireTime);

    //原始帧文件已删除：清空文件名与大小（不再计入配额）
    bool clearFrec(int64_t id);

    //改标题
    bool rename(int64_t id, const std::string& title);

    //删除录像及其全部分享记录
    bool remove(int64_t id);

    /**
     *  企业已用字节数：该企业全部录像的原始帧文件与最终文件之和。来源为上传、尚未传完的录像按申请的大小
     *  计入（f_bytes 在申请上传令牌时写入），避免多个并发上传一起超出配额。
     */
    bool usedBytes(int64_t enterpriseId, int64_t& out);

    /**
     *  写文件名（新建记录之后才知道录像 id，文件名随之确定）。来源为共享的录像写原始帧文件名；来源为上传的录像写
     *  最终文件名与申请的大小，大小先计入已用空间，避免多个并发上传一起超出配额。
     *  @param frecFile 原始帧文件名；不需要时为空串
     *  @param file     最终文件名；不需要时为空串
     *  @param bytes    写入 f_bytes 的大小
     */
    bool setFiles(int64_t id, const std::string& frecFile, const std::string& file, int64_t bytes);

    //已到期的录像（f_expire_time 大于 0 且不晚于 nowMs），最多 limit 条
    bool listExpired(int64_t nowMs, int limit, std::vector<CloudRecordRow>& out);

    //该推送到期提醒的录像（未提醒过、在 nowMs 之后 warnBeforeMs 内到期），最多 limit 条
    bool listToWarn(int64_t nowMs, int64_t warnBeforeMs, int limit, std::vector<CloudRecordRow>& out);

    //记下已推送到期提醒
    bool markWarned(int64_t id);

    //某状态的全部录像（启动时补齐「录制中」的录像用）
    bool listByStatus(int status, std::vector<CloudRecordRow>& out);

    //写一条分享记录；同一录像同一会话已有记录时更新发送人与时间
    bool addShare(const CloudRecordShareRow& share);

    //某条录像的全部分享记录
    bool listShares(int64_t recordId, std::vector<CloudRecordShareRow>& out);
};
