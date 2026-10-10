/**
 *  屏幕共享在 ChatSession 里用到的几条业务规则，ScreenShareRules.h
 *
 *  ScreenShareManager 只管房间本身（成员、座位、权限、宽限期），不知道「谁有资格加入」「开始和结束要通知谁」
 *  「结束提示怎么写」——这些依赖会话类型与群成员名单，由 ChatSession 查好群成员后交给本类判断。把它们从
 *  ChatSession 里抽出来写成纯函数，是为了能脱离网络与数据库做单元测试，见 tests/test_screensharerules.cpp
 *  （Suite SCS）。
 *
 *  典型用法（ChatSession::onScreenShareJoin 里）：
 *      bool isMember = (room.m_ctype == conversation_group) && GroupManager::isGroupMember(room.m_convKeyA, uid);
 *      int code = ScreenShareRules::checkJoinEligibility(room, uid, isMember);
 *      if (code != error_code_ok) { 回 code 并返回 }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "ScreenShareManager.h"

/**
 *  屏幕共享标题与描述的敏感词检查（第 15 期）。ChatSession 用提交者所属企业的词库实现，命中时记一条拦截记录
 *  （来源 sensitive_source_screenshare）；单元测试传入假实现。
 */
class IScreenShareSensitiveChecker
{
public:
    virtual ~IScreenShareSensitiveChecker() {}

    /**
     *  检查标题与描述是否命中敏感词；命中时由实现方记拦截记录。
     *  @param ctype 共享所在会话的类型（写进拦截记录）
     *  @param cid   共享所在会话的 id：单聊为对端 userid，群聊为群 id（写进拦截记录）
     *  @param texts 待检查的文字（UTF-8）：依次为标题、描述
     *  @return 命中返回 true
     */
    virtual bool hitShareInfo(int ctype, int64_t cid, const std::vector<std::string>& texts) = 0;
};

//屏幕共享的业务规则工具类；全部为静态方法，不可实例化
class ScreenShareRules
{
public:
    //共享标题的字数上限（第 15 期，按字符计，一个汉字算一个）
    static const size_t kShareTitleMaxChars = 30;
    //共享描述的字数上限（第 15 期，按字符计）
    static const size_t kShareDescMaxChars = 100;

    /**
     *  检查并规整共享标题与描述（第 15 期，发起共享 1720 与修改 1733 共用）：先去掉两者首尾的空白（含全角空格），
     *  再检查标题不能为空、标题不超过 kShareTitleMaxChars 字、描述不超过 kShareDescMaxChars 字、两者都是合法 UTF-8；
     *  格式合格后才做敏感词检查，避免为格式不合法的请求记拦截记录。
     *  @param title   [入参 / 出参] 标题（UTF-8），返回时已去掉首尾空白
     *  @param desc    [入参 / 出参] 描述（UTF-8），返回时已去掉首尾空白
     *  @param ctype   共享所在会话的类型（交给敏感词检查写进拦截记录）
     *  @param cid     共享所在会话的 id（同上）
     *  @param checker 敏感词检查；为 NULL 时不检查（不拥有）
     *  @return 合格返回 error_code_ok(0)；格式不合格返回 error_code_bad_request(3)；命中敏感词返回 error_code_screenshare_sensitive(720)
     */
    static int checkShareInfo(std::string& title, std::string& desc, int ctype, int64_t cid,
                              IScreenShareSensitiveChecker* checker);

    /**
     *  判断某人有没有资格加入房间：单聊只允许会话双方（发起人重复加入由 ScreenShareManager 按幂等处理），
     *  群聊只允许群成员；被发起人移出且不允许再次加入的人没有资格（第 14 期，先于密码校验，免得先弹密码框）。
     *  @param room          要加入的房间快照
     *  @param userId        申请加入者的 userid
     *  @param isGroupMember 申请者是否为该群成员（只在群聊时有意义，由调用方查群成员名单得出；单聊时忽略）
     *  @return 有资格返回 error_code_ok(0)；不是会话成员返回 error_code_screenshare_not_member(709)；
     *          已被移出且不允许再次加入返回 error_code_screenshare_removed(719)
     */
    static int checkJoinEligibility(const ScreenShareRoom& room, int64_t userId, bool isGroupMember);

    /**
     *  加入请求带了分享链接码时的资格判定（第 25 期，docs/屏幕共享与录制方案.md 第 12.4 节，第 44、46 条）。在 checkJoinEligibility
     *  之后调用，把它的结果作为 memberCode 传入：
     *    - 本人是会话成员（memberCode 为 0）：资格不变，不算链接访客；链接有效且对应本房间时可免输密码；
     *    - 本人不是会话成员（memberCode 为 709）：链接须有效、对应本房间、与发起人同企业，且本人没有被移出且不允许再加入，
     *      满足时放行并标为链接访客；
     *    - 其余 memberCode（如 719 已被移出）原样返回。
     *  @param room           要加入的房间快照
     *  @param userId         申请加入者 userid
     *  @param memberCode     checkJoinEligibility 的结果
     *  @param linkStatus     链接码的解析结果，取值见 screenshare_link_status（ScreenShareLinkTable.h）；没带链接码时传 0（无效）
     *  @param linkRoomId     链接码对应的房间号（linkStatus 为有效或已结束时才有意义）
     *  @param linkNoPassword 链接是否打开了「点链接加入免输密码」
     *  @param sameEnterprise 申请者与发起人是否属于同一企业（企业 id 都大于 0 且相等）
     *  @param outViaLink     [出参] 是否以链接访客的身份加入（不是会话成员、凭链接放行）
     *  @param outSkipPassword [出参] 是否免输密码（链接有效、对应本房间、打开了免输密码）
     *  @return 有资格返回 error_code_ok(0)；不是会话成员且链接无效、已结束或对应别的房间时返回
     *          error_code_screenshare_link_invalid(733)；不同企业返回 error_code_screenshare_link_enterprise(734)；
     *          被移出且不允许再加入返回 719；没带链接码时原样返回 memberCode
     */
    static int checkLinkJoin(const ScreenShareRoom& room, int64_t userId, int memberCode, int linkStatus,
                             const std::string& linkRoomId, bool linkNoPassword, bool sameEnterprise, bool& outViaLink,
                             bool& outSkipPassword);

    /**
     *  判断「一对一通话」与「发起 / 加入屏幕共享」是否冲突。不在通话中时不冲突；在通话中时，只有
     *  通话已接通、共享在单聊里、且共享的另一方正是通话对端才允许（第 8 期：通话中共享屏幕，
     *  见 docs/屏幕共享与录制方案.md），其余情况一律冲突。
     *  @param inCall      该用户当前是否在某通通话里（含响铃中）
     *  @param callState   那通通话的状态，取值见 enum call_state；inCall 为 false 时忽略
     *  @param callPeerId  那通通话的对端 userid；inCall 为 false 时忽略
     *  @param ctype       共享所在会话的类型，取值见 enum conversation_type
     *  @param sharePeerId 共享的另一方：发起时为单聊对端（即请求里的 cid），加入时为房间发起人
     *  @return 不冲突返回 error_code_ok(0)，冲突返回 error_code_screenshare_in_call(712)
     */
    static int checkCallConflict(bool inCall, int callState, int64_t callPeerId, int ctype, int64_t sharePeerId);

    /**
     *  共享密码的格式：正好 6 位 ASCII 数字（第 12 期）。
     *  @param password 待检查的密码
     *  @return 格式正确返回 true；空串返回 false（空串表示不设密码，由调用方另行判断）
     */
    static bool isValidPassword(const std::string& password);

    /**
     *  加入时的密码校验（第 12 期）。房间没有密码、加入者是发起人、已在房间里、与发起人处于已接通的通话、
     *  是强制模式下掉线后回来的观看者（第 13 期）时都不校验；其余情况须带与房间相同的密码。
     *  @param room           要加入的房间快照
     *  @param userId         加入者 userid
     *  @param provided       加入请求里带的密码；没带时为空串
     *  @param inCallWithHost 加入者是否与发起人处于已接通的一对一通话（第 8 期通话中共享，对方自动加入）
     *  @return 通过返回 error_code_ok(0)，否则返回 error_code_screenshare_bad_password(717)
     */
    static int checkJoinPassword(const ScreenShareRoom& room, int64_t userId, const std::string& provided,
                                 bool inCallWithHost);

    /**
     *  计算「有人开始共享」推送与共享卡片要通知的人：单聊为对端一人，群聊为全体群成员去掉发起人。
     *  结果按 userid 去重，不含发起人自己（发起人从 1720 的应答得知结果）。
     *  @param room         刚创建的房间
     *  @param groupMembers 群成员 userid 列表（只在群聊时使用；单聊时传空即可）
     *  @return 要通知的 userid 列表，顺序与 groupMembers 一致（单聊时只有一个元素）
     */
    static std::vector<int64_t> startNotifyTargets(const ScreenShareRoom& room, const std::vector<int64_t>& groupMembers);

    /**
     *  计算某个接收者看到的会话 id：单聊为会话中「另一个人」的 userid，群聊为群 id。
     *  1721 / 1729 推送与系统提示都要按接收者分别填写 cid。
     *  @param room       房间快照
     *  @param receiverId 接收者 userid
     *  @return 从接收者角度看的会话 id；单聊时接收者不是会话双方之一（凭分享链接加入的链接访客，第 25 期）返回 0
     */
    static int64_t conversationIdFor(const ScreenShareRoom& room, int64_t receiverId);

    /**
     *  构造共享结束后写入会话的系统提示文本：时长不足 1 小时写作「屏幕共享已结束，时长 mm:ss」，
     *  满 1 小时写作「屏幕共享已结束，时长 h:mm:ss」（小时数不补零）。
     *  @param durationSeconds 共享时长，单位秒；负数按 0 处理
     *  @return UTF-8 编码的提示文本
     */
    static std::string formatEndTip(int64_t durationSeconds);

    /**
     *  构造共享结束后写入会话的系统提示 content（多语言第 5 期）：
     *  { text: formatEndTip 的中文, tipkind: kSystemTipScreenShareEnd, tipargs: { duration: 秒数 } }。
     *  老客户端按 text 显示，新客户端按 duration 用当前界面语言的模板生成。
     *  @param durationSeconds 共享时长，单位秒；负数按 0 处理
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildEndTipContent(int64_t durationSeconds);

    /**
     *  按房间快照构造会话里那张共享卡片的 content（修改标题与描述时改写卡片用，第 15 期）。卡片上的发起人始终写最初的发起人
     *  （m_starterId / m_starterName），移交主持人之后由新主持人修改标题也不变（2026-10-08 用户决定）；
     *  是否需要密码、是否强制模式取自房间。
     *  @param room  房间快照
     *  @param title 新的共享标题（UTF-8）
     *  @param desc  新的共享描述（UTF-8），可为空
     *  @return content 的 JSON 文本，格式同 ScreenShareProtocol::buildCardContent
     */
    static std::string buildCardContentFor(const ScreenShareRoom& room, const std::string& title,
                                           const std::string& desc);

private:
    ScreenShareRules();
};
