/**
 *  MemoProtocolClient.h
 *
 *  「备忘与提醒」报文（cmd_memo_list=2401 / cmd_memo_save=2402 / cmd_memo_delete=2403 /
 *  cmd_memo_notify=2404）的客户端侧编解码，与服务端 chatserversrc/MemoProtocol.{h,cpp} 一一对应。
 *  报文格式见 docs/协议设计-v2.md 第 19 节。
 *
 *  典型用法：登录成功后发 2401 拉全部可见备忘放进 MemoBook，之后靠 2404 推送增量更新；
 *  备忘窗口与群聊窗「备忘」页签保存、删除时发 2402 / 2403。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不碰窗口、不碰 socket，可被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//备忘所属：取值与 Protocol.h 的 conversation_type 一致
const int kMemoCtypePersonal = 1;   //个人备忘
const int kMemoCtypeGroup = 2;      //群备忘

/**
 *  一条备忘，字段与服务端备忘对象一一对应。文本字段一律是 <u>UTF-8</u>，交给界面前须转成 UTF-16。
 */
struct MemoItem
{
    int64_t     m_memoid;      //备忘 ID；新建请求里为 0
    int         m_ctype;       //所属：kMemoCtypePersonal / kMemoCtypeGroup
    int64_t     m_groupid;     //群备忘所属群；个人备忘为 0
    int64_t     m_creator;     //创建人 userid（服务端填）
    int64_t     m_editor;      //最后修改人 userid（服务端填）
    std::string m_title;       //标题（UTF-8），1～50 字
    std::string m_note;        //备注（UTF-8），0～200 字，可含换行
    int64_t     m_start;       //首次事件的本地时间，十进制 yyyymmddhhmm
    int         m_advance;     //提前分钟数，0～1440，0 为到点提醒
    int         m_repeat;      //重复方式，取值见 Protocol.h 的 memo_repeat_type
    int         m_weekdays;    //每周重复的星期掩码，bit0 周一 ... bit6 周日
    int         m_count;       //提醒次数上限，0 为永不结束
    int         m_style;       //提醒方式，取值见 Protocol.h 的 memo_style（1 右下角弹窗、2 全屏动画）
    bool        m_targetAll;   //群备忘是否提醒全体成员
    std::vector<int64_t> m_targets;  //群备忘指定的被提醒成员 userid
    int64_t     m_createTime;  //创建时间，Unix 毫秒（服务端填）
    int64_t     m_updateTime;  //最后修改时间，Unix 毫秒（服务端填）

    MemoItem()
        : m_memoid(0), m_ctype(kMemoCtypePersonal), m_groupid(0), m_creator(0), m_editor(0)
        , m_start(0), m_advance(0), m_repeat(0), m_weekdays(0), m_count(1), m_style(1)
        , m_targetAll(false), m_createTime(0), m_updateTime(0)
    {
    }
};

//cmd_memo_list 应答解析结果
struct MemoListResult
{
    int         m_code;              //结果码，0 成功；其余取值见 Protocol.h 的 enum error_code
    std::string m_msg;               //结果描述（UTF-8），可为空串
    std::vector<MemoItem> m_memos;   //本人可见的备忘

    MemoListResult() : m_code(-1) {}
};

//cmd_memo_save 应答解析结果
struct MemoSaveResult
{
    int         m_code;      //结果码，0 成功
    std::string m_msg;       //结果描述（UTF-8）
    bool        m_hasMemo;   //应答是否带回保存后的备忘（成功时为 true）
    MemoItem    m_memo;      //保存后的完整备忘，m_hasMemo 为 true 时有效

    MemoSaveResult() : m_code(-1), m_hasMemo(false) {}
};

//cmd_memo_delete 应答解析结果
struct MemoDeleteResult
{
    int         m_code;      //结果码，0 成功
    std::string m_msg;       //结果描述（UTF-8）
    int64_t     m_memoid;    //被删除的备忘 ID，服务端原样回带

    MemoDeleteResult() : m_code(-1), m_memoid(0) {}
};

//cmd_memo_notify 推送解析结果
struct MemoNotify
{
    int      m_changeType;   //变更类型，取值见 Protocol.h 的 memo_change_type（1 新增或修改、2 删除）
    int64_t  m_memoid;       //备忘 ID；两种变更类型都有效
    MemoItem m_memo;         //m_changeType 为 1 时是完整备忘；为 2 时只有 m_memoid 有效

    MemoNotify() : m_changeType(0), m_memoid(0) {}
};

//备忘报文编解码工具类；全部为静态方法，不可实例化
class MemoProtocolClient
{
public:
    /**
     *  构造 cmd_memo_list 请求。
     *    groupid：只拉某个群的群备忘时传群 id；传 0 拉本人可见的全部备忘。
     *  @return 紧凑 JSON，如 {} 或 {"groupid":"268435463"}
     */
    static std::string buildListRequest(int64_t groupid);

    /**
     *  构造 cmd_memo_save 请求：备忘对象去掉服务端填写的 creator / editor / 时间戳。
     *    memo：待保存的备忘；m_memoid 为 0 表示新建。
     */
    static std::string buildSaveRequest(const MemoItem& memo);

    /**
     *  构造 cmd_memo_delete 请求：{"memoid":"12"}。
     */
    static std::string buildDeleteRequest(int64_t memoid);

    /**
     *  解析 cmd_memo_list 应答。
     *  @return JSON 合法且含整型 code 时返回 true；单条备忘缺字段按默认值填，不让整条应答失败
     */
    static bool parseListResponse(const std::string& jsonbody, MemoListResult& out);

    /**
     *  解析 cmd_memo_save 应答。
     *  @return JSON 合法且含整型 code 时返回 true
     */
    static bool parseSaveResponse(const std::string& jsonbody, MemoSaveResult& out);

    /**
     *  解析 cmd_memo_delete 应答。
     *  @return JSON 合法且含整型 code 时返回 true
     */
    static bool parseDeleteResponse(const std::string& jsonbody, MemoDeleteResult& out);

    /**
     *  解析 cmd_memo_notify 推送。
     *  @return JSON 合法、changetype 为 1 或 2、备忘 ID 为正时返回 true
     */
    static bool parseNotify(const std::string& jsonbody, MemoNotify& out);

    /**
     *  把十进制 yyyymmddhhmm 格式化成 "yyyy-mm-dd hh:mm"（报文里的 start 字段）。
     */
    static std::string formatStart(int64_t start);

    /**
     *  把 "yyyy-mm-dd hh:mm" 解析成十进制 yyyymmddhhmm；格式不对返回 0。只检查格式，不检查日期是否存在。
     */
    static int64_t parseStart(const std::string& text);
};

} // namespace net
