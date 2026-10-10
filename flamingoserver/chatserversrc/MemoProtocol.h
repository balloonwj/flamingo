/**
 *  「备忘与提醒」报文编解码与规则，MemoProtocol.h
 *
 *  负责 cmd_memo_list(2401) / cmd_memo_save(2402) / cmd_memo_delete(2403) 请求的解析与字段校验，
 *  应答与 cmd_memo_notify(2404) 推送的组装，以及「谁能看到这条备忘」这类与数据库无关的纯规则。
 *  落库在 MemoManager，权限与推送对象的业务判断在 ChatSession。方案见 docs/备忘与提醒方案.md。
 *
 *  典型用法：
 *      MemoRecord memo;
 *      if (!MemoProtocol::parseSaveRequest(body, memo)) { 回 error_code_bad_request; }
 *      int code = MemoProtocol::normalizeMemo(memo);
 *      if (code != error_code_ok) { 回 code; }
 *      ...落库后... send(cmd_memo_save, seq, MemoProtocol::buildSaveResponse(0, "ok", &memo));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//备忘标题最多的字符数（按 Unicode 字符计，去掉首尾空白之后），与用户确认的 50 字
const size_t kMemoTitleMaxChars = 50;

//备忘备注最多的字符数（按 Unicode 字符计，去掉末尾空白之后），与用户确认的 200 字
const size_t kMemoNoteMaxChars = 200;

//提前提醒的最大分钟数：一天
const int kMemoAdvanceMaxMinutes = 1440;

//提醒次数上限的最大取值；0 表示永不结束
const int kMemoCountMax = 999;

//每周重复时星期掩码的合法位：bit0 周一 ... bit6 周日
const int kMemoWeekdayAllMask = 0x7F;

//每人个人备忘的条数上限
const int kMaxPersonalMemos = 500;

//每群群备忘的条数上限
const int kMaxGroupMemos = 200;

//开始时间里允许的年份范围：太早或太远的时间多半是客户端时钟或输入出了问题
const int kMemoMinYear = 2000;
const int kMemoMaxYear = 2099;

//群备忘卡片（message_type_memo）的 clientmsgid 前缀，完整写法为「前缀 + 备忘id + "-" + 最后修改时间」
//（docs/备忘与提醒方案.md §7.2）：同一条备忘每保存一次 updateTime 就变一次，各自成一张卡片
const char* const kMemoCardClientMsgIdPrefix = "memo-card-";

/**
 *  一条备忘：t_memo 的一行加上它的提醒对象（t_memo_target）。
 */
struct MemoRecord
{
    int64_t memoid;                 // 备忘ID；新建请求里为 0，落库后由数据库分配
    int     ctype;                  // 所属：1 个人、2 群（取值见 conversation_type）
    int64_t owner;                  // 个人备忘为本人 userid，群备忘为群 id
    int64_t creator;                // 创建人 userid，由服务端填
    int64_t editor;                 // 最后修改人 userid，由服务端填
    std::string title;              // 标题（UTF-8，1～kMemoTitleMaxChars 字，不含换行）
    std::string note;               // 备注（UTF-8，0～kMemoNoteMaxChars 字，可含换行）
    int64_t start;                  // 首次事件的本地时间，十进制 yyyymmddhhmm
    int     advance;                // 提前分钟数，0～kMemoAdvanceMaxMinutes，0 为到点提醒
    int     repeat;                 // 重复方式，取值见 memo_repeat_type
    int     weekdays;               // 每周重复的星期掩码（bit0 周一 ... bit6 周日），其它重复方式为 0
    int     count;                  // 提醒次数上限，0 为永不结束；不重复时恒为 1
    int     style;                  // 提醒方式，取值见 memo_style
    bool    targetAll;              // 群备忘是否提醒全体成员；个人备忘恒为 false
    std::vector<int64_t> targets;   // 群备忘指定的被提醒成员（targetAll 为 true 时为空），升序、去重
    int64_t createTime;             // 创建时间，Unix 毫秒
    int64_t updateTime;             // 最后修改时间，Unix 毫秒

    MemoRecord()
        : memoid(0), ctype(0), owner(0), creator(0), editor(0), start(0), advance(0)
        , repeat(0), weekdays(0), count(1), style(0), targetAll(false), createTime(0), updateTime(0)
    {
    }
};

/**
 *  群成员与其角色，计算群备忘的可见范围时用。与 GroupMemberRecord 分开定义，
 *  是为了让本文件的纯规则不依赖群模块。
 */
struct MemoMemberRole
{
    int64_t userid;     // 成员 userid
    int     role;       // 角色，取值见 group_member_role

    MemoMemberRole() : userid(0), role(0) {}
    MemoMemberRole(int64_t uid, int r) : userid(uid), role(r) {}
};

class MemoProtocol
{
public:
    /**
     *  解析 cmd_memo_list 请求：{} 或 {"groupid":"群id"}。
     *    json：      请求包体（UTF-8 JSON）。
     *    outGroupid：[出参] 只拉某个群的群备忘时为群 id，拉全部时为 0。
     *  @return 是合法的 JSON 对象返回 true；groupid 不是字符串或为负数返回 false。
     */
    static bool parseListRequest(const std::string& json, int64_t& outGroupid);

    /**
     *  解析 cmd_memo_save 请求，只做结构解析（字段类型、开始时间格式），取值范围的校验在 normalizeMemo。
     *    json：[入参] 请求包体。
     *    out： [出参] 解析结果；ctype 为 2 时 owner 为请求里的 groupid，ctype 为 1 时 owner 为 0，
     *          由会话层填成本人。creator / editor / 时间戳不从请求读。
     *  @return 结构合法返回 true；缺字段、类型不对、开始时间格式不对返回 false（回 error_code_bad_request）。
     */
    static bool parseSaveRequest(const std::string& json, MemoRecord& out);

    /**
     *  校验并规整一条备忘的取值：标题去掉首尾空白、备注去掉末尾空白；不重复时次数置 1；
     *  非每周重复时星期掩码置 0；个人备忘清空提醒对象；提醒对象去重并升序。
     *    memo：[入参/出参] 待校验的备忘，校验通过时被就地规整。
     *  @return error_code_ok 合法；error_code_memo_invalid 字段取值不合法；
     *          error_code_memo_target_invalid 群备忘既不提醒全体也没有指定任何成员。
     */
    static int normalizeMemo(MemoRecord& memo);

    /**
     *  解析 cmd_memo_delete 请求：{"memoid":"备忘id"}。
     *  @return memoid 是正数的字符串返回 true。
     */
    static bool parseDeleteRequest(const std::string& json, int64_t& outMemoid);

    /**
     *  把 "yyyy-mm-dd hh:mm" 解析成十进制 yyyymmddhhmm，并检查是一个存在的日期时间。
     *  @return 格式与取值都合法返回 true。
     */
    static bool parseStart(const std::string& text, int64_t& outStart);

    /**
     *  把十进制 yyyymmddhhmm 格式化成 "yyyy-mm-dd hh:mm"。取值不合法时原样按位拆开格式化，不报错。
     */
    static std::string formatStart(int64_t start);

    /**
     *  十进制 yyyymmddhhmm 是不是一个存在的日期时间（年份 kMemoMinYear～kMemoMaxYear，含闰年判断）。
     */
    static bool isValidStart(int64_t start);

    /**
     *  某用户能不能看到这条备忘。
     *    memo：     备忘，只读。
     *    userid：   用户 userid。
     *    groupRole：用户在该备忘所属群里的角色（取值见 group_member_role），不是群成员时传 -1；个人备忘不看本参数。
     *  @return 个人备忘只有本人能看；群备忘对群主、管理员可见，对普通成员仅在提醒全体或被指定时可见。
     */
    static bool isVisibleTo(const MemoRecord& memo, int64_t userid, int groupRole);

    /**
     *  某用户能不能新建、修改、删除这条备忘。
     *  @return 个人备忘只有本人；群备忘只有群主、管理员。参数同 isVisibleTo。
     */
    static bool canEdit(const MemoRecord& memo, int64_t userid, int groupRole);

    /**
     *  算出能看到这条备忘的全部用户，即保存、删除后要推送的对象。
     *    memo：   备忘，只读。
     *    members：所属群的全部成员与角色；个人备忘不看本参数（可传空）。
     *    out：    [出参] 先清空，再按升序填入 userid，不重复。个人备忘只有 owner 一人。
     */
    static void collectViewers(const MemoRecord& memo, const std::vector<MemoMemberRole>& members,
                               std::vector<int64_t>& out);

    /**
     *  修改前能看到、修改后看不到的那些人，他们要收到删除推送。
     *    before：修改前的可见用户，升序。
     *    after： 修改后的可见用户，升序。
     *    out：   [出参] 先清空，再填入 before 有而 after 没有的 userid，升序。
     */
    static void collectRemovedViewers(const std::vector<int64_t>& before, const std::vector<int64_t>& after,
                                      std::vector<int64_t>& out);

    /**
     *  取一条备忘里要过企业敏感词的文字。群备忘会推给其他成员，标题与备注都要查；
     *  个人备忘只有本人能看，不查。
     *    memo：备忘，须已经过 normalizeMemo。
     *    out： [出参] 先清空；群备忘填入标题，备注不为空时再填入备注。
     */
    static void collectSensitiveTexts(const MemoRecord& memo, std::vector<std::string>& out);

    /**
     *  组装 cmd_memo_list 应答：{code, msg, memos:[...]}，code 非 0 时不带 memos。
     */
    static std::string buildListResponse(int code, const std::string& msg, const std::vector<MemoRecord>& memos);

    /**
     *  组装 cmd_memo_save 应答：{code, msg, memo}；memo 为 NULL 时不带该字段。
     */
    static std::string buildSaveResponse(int code, const std::string& msg, const MemoRecord* memo);

    /**
     *  组装 cmd_memo_delete 应答：{code, msg, memoid}。
     */
    static std::string buildDeleteResponse(int code, const std::string& msg, int64_t memoid);

    /**
     *  组装 cmd_memo_notify 的新增或修改推送：{changetype:1, memo}。
     */
    static std::string buildUpsertNotify(const MemoRecord& memo);

    /**
     *  组装 cmd_memo_notify 的删除推送：{changetype:2, memoid}。
     */
    static std::string buildDeleteNotify(int64_t memoid);

    /**
     *  拼一张群备忘卡片的 clientmsgid：kMemoCardClientMsgIdPrefix + 备忘id + "-" + 最后修改时间。
     *  @param memoid     备忘 id
     *  @param updateTime 这次保存后的最后修改时间，Unix 毫秒
     *  @return 形如 memo-card-12-1790000000000 的字符串
     */
    static std::string memoCardClientMsgId(int64_t memoid, int64_t updateTime);

    /**
     *  构造群备忘卡片（message_type_memo）的 content：
     *  { memoid, groupid, change, title, note, start, advance, repeat, weekdays, count, targetall, targetcount,
     *    editorid, editor, text }，字段含义见 docs/备忘与提醒方案.md §7.2。卡片是保存那一刻的快照，之后不改写。
     *  @param memo       刚保存成功的群备忘（memoid、editor 须已填好），只读
     *  @param change     这次操作，取值见 memo_card_change
     *  @param editorName 操作人昵称（UTF-8）；取不到时传空串，客户端按群成员表或通讯录补
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildMemoCardContent(const MemoRecord& memo, int change, const std::string& editorName);

    /**
     *  算出群备忘卡片的可见名单（写进 t_message_audience 的那一份）。
     *    memo：   刚保存成功的群备忘，只读。
     *    viewers：collectViewers 对这条备忘算出的可见用户（升序、去重）。
     *    out：    [出参] 先清空；返回 true 时填入 viewers 里的全部 userid。
     *  @return 提醒全体成员时返回 false，表示卡片全员可见、不限定范围（saveMessage 传 NULL）；
     *          否则返回 true，卡片只给 out 里的人（与发送者本人）看
     */
    static bool memoCardAudience(const MemoRecord& memo, const std::vector<int64_t>& viewers,
                                 std::vector<int32_t>& out);

    /**
     *  按 Unicode 字符数统计一段 UTF-8 文字的长度（与 CardProtocol::utf8CharCount 同口径）。
     */
    static size_t utf8CharCount(const std::string& text);
};
