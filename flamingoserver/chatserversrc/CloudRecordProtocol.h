/**
 *  云端录制的协议层（1754～1757 的 JSON 解析与构造），CloudRecordProtocol.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 12.6，示例见 docs/协议设计-v2.md 17.10）。只做 JSON 与结构体之间的转换，不碰数据库与网络：
 *    - 1754 cmd_cloudrecord_control：{"roomid":"ss-...","action":1} → {"code":0,"msg":"","roomid":"...","action":1,"recordid":42}
 *    - 1755 cmd_cloudrecord_list：{"beforeid":0,"limit":20} → {"code":0,"records":[...],"hasmore":0,"enabled":1,
 *      "usedbytes":..,"quotabytes":..,"retaindays":30}
 *    - 1756 cmd_cloudrecord_op：{"op":3,"recordid":42,"kind":2} → {"code":0,"op":3,"recordid":42,"token":"...","host":"...",
 *      "port":20001,"filename":"rec_42.mp4","bytes":..}；重命名、发到会话、取上传令牌的应答另带整条 record
 *    - 1757 cmd_cloudrecord_notify：{"type":1,"record":{...}} 或 {"type":3,"recordid":42}
 *  一条录像的 JSON 见 recordToJson。单元测试见 tests/test_cloudrecordserver.cpp（CRP 系列）。
 *
 *  典型用法（ChatSession 里）：
 *      CloudRecordOpRequest req;
 *      if (!CloudRecordProtocol::parseOpRequest(body, req))
 *          回 error_code_bad_request;
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "../jsoncpp1.9.0/json.h"
#include "CloudRecordRules.h"

//1756 的请求：按 op 只用到其中一部分字段
struct CloudRecordOpRequest
{
    int         m_op;          //操作，取值见 cloudrecord_op_type
    int64_t     m_recordId;    //录像 id；取上传令牌新建录像时为 0
    std::string m_title;       //重命名的新标题；新建上传录像的标题
    int         m_kind;        //取下载令牌的文件种类，取值见 cloudrecord_file_kind
    int         m_ctype;       //发到会话：会话类型
    int64_t     m_cid;         //发到会话：会话 id
    int64_t     m_bytes;       //取上传令牌：文件字节数
    bool        m_audioOnly;   //取上传令牌新建录像：是否只有声音（m4a）
    int64_t     m_durationMs;  //取上传令牌：录像时长（毫秒）
    int64_t     m_startTime;   //取上传令牌新建录像：本机开始录制的时间（Unix 毫秒）

    CloudRecordOpRequest();
};

//令牌应答里的文件位置与令牌
struct CloudRecordTokenInfo
{
    std::string m_token;      //上传或下载令牌（FileToken，scene 为 file_scene_cloud_record）
    std::string m_host;       //imgserver 地址
    int         m_port;       //imgserver 图片端口
    std::string m_fileName;   //文件名，如 rec_42.mp4（客户端用于显示与续传记录）
    int64_t     m_bytes;      //文件字节数（下载时为服务端记录的大小，上传时为申请的大小）

    CloudRecordTokenInfo();
};

//云端录制的协议层；全部为静态方法
class CloudRecordProtocol
{
public:
    /**
     *  解析 1754 请求：{"roomid":"ss-...","action":1}。
     *  @param roomId [出参] 房间号
     *  @param action [出参] 动作，取值见 cloudrecord_control_action
     *  @return JSON 合法、roomid 非空、action 在 1～4 之间返回 true
     */
    static bool parseControlRequest(const std::string& json, std::string& roomId, int& action);

    //构造 1754 应答；recordId 为 0 时不写 recordid 字段
    static std::string buildControlResponse(int code, const std::string& msg, const std::string& roomId, int action,
                                            int64_t recordId);

    /**
     *  解析 1755 请求：{"beforeid":0,"limit":20}，两个字段都可省略。
     *  @param beforeId [出参] 只列 id 小于它的录像（翻页）；0 表示从最新的开始
     *  @param limit    [出参] 每页条数，限制在 1～kListPageMax，省略或不合法时为 kListPageDefault
     *  @return JSON 是对象（空包体也算）返回 true
     */
    static bool parseListRequest(const std::string& json, int64_t& beforeId, int& limit);

    //一条录像的 JSON：recordid、owner、source、roomid、ctype、cid、title、starttime、endtime、durationms、bytes、frecbytes、
    //audioonly、status、expiretime、createtime
    static Json::Value recordToJson(const CloudRecordRow& row);

    /**
     *  构造 1755 应答。
     *  @param rows       本页录像（按 id 从大到小）
     *  @param hasMore    还有更早的录像
     *  @param enabled    本企业是否开通了云端录制（关闭时仍列出已有录像，客户端隐藏新建入口）
     *  @param usedBytes  本企业已用字节数
     *  @param quotaBytes 本企业配额字节数
     *  @param retainDays 保留天数（0 为永久）
     */
    static std::string buildListResponse(int code, const std::string& msg, const std::vector<CloudRecordRow>& rows,
                                         bool hasMore, bool enabled, int64_t usedBytes, int64_t quotaBytes,
                                         int64_t retainDays);

    /**
     *  解析 1756 请求。各 op 要求的字段：重命名要 recordid 与 title；删除要 recordid；取下载令牌要 recordid 与 kind；
     *  取上传令牌要 bytes（>0），recordid 为 0 时新建录像；发到会话要 recordid、ctype（1 或 2）与 cid。
     *  @return 字段齐全、类型正确返回 true
     */
    static bool parseOpRequest(const std::string& json, CloudRecordOpRequest& out);

    /**
     *  构造 1756 应答。
     *  @param row   为 NULL 时不带 record 字段
     *  @param token 为 NULL 时不带令牌相关字段
     */
    static std::string buildOpResponse(int code, const std::string& msg, int op, int64_t recordId,
                                       const CloudRecordRow* row, const CloudRecordTokenInfo* token);

    /**
     *  构造 1757 推送：状态变化或即将到期，附整条录像。
     *  @param type 取值见 cloudrecord_notify_type（status 或 expire）
     */
    static std::string buildRecordNotify(int type, const CloudRecordRow& row);

    //构造 1757 推送：录像已删除，只带 recordid
    static std::string buildDeleteNotify(int64_t recordId);
};
