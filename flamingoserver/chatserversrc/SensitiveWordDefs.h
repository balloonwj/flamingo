/**
 *  企业级敏感词的公共定义：词条分类、拦截来源两个枚举，以及各项上限常量。SensitiveWordDefs.h
 *
 *  敏感词按企业划分、由管理端维护、只在服务端拦截（方案见 docs/敏感词方案.md）。本文件只放
 *  枚举与常量，不包含 Protocol.h —— HttpSession 及其调用的 manager 不能包含 Protocol.h（它与
 *  AdminApiCodes.h 的 error_code 枚举冲突），而 ChatSession、HttpSession、SensitiveWordManager
 *  都要用到这些定义。
 *
 *  典型用法：
 *      if (charCount > SENSITIVE_WORD_MAX_CHARS) { ... }
 *      entry.m_category = sensitive_category_other;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

namespace chatserver {

//敏感词分类：固定六类，管理员添加词条时选择，拦截记录按它筛选与统计。
//取值写进 t_sensitive_word.f_category 与 t_sensitive_hit_log.f_category，管理端照抄一份，
//两边须保持一致；已用过的取值不得改动含义。
enum sensitive_word_category
{
    sensitive_category_illegal = 1,   //违法违规
    sensitive_category_vulgar  = 2,   //色情低俗
    sensitive_category_abuse   = 3,   //攻击辱骂
    sensitive_category_secret  = 4,   //商业机密
    sensitive_category_ad      = 5,   //广告引流
    sensitive_category_other   = 6,   //其他；管理员添加时没有选择分类的词条归入此类
};

//拦截来源：拦截记录里标明是哪一类内容被拦下，取值写进 t_sensitive_hit_log.f_source。
enum sensitive_hit_source
{
    sensitive_source_text         = 1,    //文字消息正文与引用块（cmd_send_message 1503、群发助手 1516）
    sensitive_source_forward      = 2,    //转发：逐条转发的文字、合并记录里的文字与标题（1511）
    sensitive_source_file_name    = 3,    //文件名：文件消息（1503，msgtype=3）与申请上传（1801）
    sensitive_source_card         = 4,    //卡片消息（1503，msgtype=10/11/12）与参与接龙（1603）
    sensitive_source_profile      = 5,    //个人资料：昵称、个性签名、地址、邮箱（1104）
    sensitive_source_group_info   = 6,    //群资料：建群群名、群名称、群简介、欢迎语、旧版公告字段（1301、1311）
    sensitive_source_member_card  = 7,    //群名片（1315）
    sensitive_source_join_request = 8,    //加群申请的验证消息（1306）
    sensitive_source_mute_reason  = 9,    //管理员禁言时填写的原因（1313）
    sensitive_source_folder_name  = 10,   //单聊发送文件夹时的文件夹名（1810）
    sensitive_source_register     = 11,   //自助注册时填写的昵称（1101）
    sensitive_source_announcement = 12,   //群公告的标题与正文（1324）
    sensitive_source_screenshare  = 13,   //屏幕共享的标题与描述（发起共享 1720、修改标题与描述 1733，第 15 期）
    sensitive_source_memo         = 14,   //群备忘的标题与备注（2402，见 docs/备忘与提醒方案.md）
    sensitive_source_fileshare    = 15,   //文件共享的共享名称（发起共享 2201，见 docs/文件共享方案.md）
    sensitive_source_cloudrecord  = 16,   //云端录像的标题（重命名与新建上传 1756，见 docs/屏幕共享与录制方案.md 12.6）
    sensitive_source_group_qa     = 17,   //群问答的问题与答案（1337，见 docs/群问答方案.md）
    sensitive_source_thread_post  = 18,   //帖子群的帖子标题、正文与楼主补充（2703、2704、2714，见 docs/帖子群方案.md）
    sensitive_source_thread_comment = 19, //帖子群的留言与回复（2709，见 docs/帖子群方案.md）
    sensitive_source_bot_config   = 20,   //群主、员工给群机器人与个人机器人填写的名称、简介、关键词与回复文字（2803、2807、2811，见 docs/机器人方案.md）
    sensitive_source_bot_external = 21,   //外部系统经 Webhook 推来的内容，与机器人呼叫外部服务后收到的回复（见 docs/机器人方案.md §5.6）
};

//词库写操作（添加、修改一条，或导入中的一行）的结果。管理后台接口据它换成 AdminApiCodes.h
//里的错误码（120～124）回给管理端。
enum sensitive_word_op_result
{
    sensitive_word_op_ok        = 0,   //成功
    sensitive_word_op_empty     = 1,   //词条为空，或去掉空白、符号与表情之后为空
    sensitive_word_op_too_long  = 2,   //去掉首尾空白后超过 SENSITIVE_WORD_MAX_CHARS 个字符
    sensitive_word_op_exist     = 3,   //归一化后与本企业已有词条重复；导入时也包括与同一批里靠前的行重复
    sensitive_word_op_full      = 4,   //本企业的词条数已达 SENSITIVE_WORDS_PER_ENTERPRISE
    sensitive_word_op_not_exist = 5,   //要修改的词条不存在，或不属于该企业
    sensitive_word_op_db_error  = 6,   //数据库访问失败
};

//单个词条最多的字符数，按 Unicode 字符计（去掉首尾空白之后），与 t_sensitive_word.f_word
//的 varchar(32) 一致（MySQL 的 varchar 按字符计）。
const int SENSITIVE_WORD_MAX_CHARS = 32;

//每个企业最多的词条数。
const int SENSITIVE_WORDS_PER_ENTERPRISE = 10000;

//拦截记录的内容摘要截取命中位置前后各多少个字符。
const int SENSITIVE_SNIPPET_CONTEXT_CHARS = 20;

//拦截记录的保留天数，超过的由 chatserver 定时清理。
const int SENSITIVE_HIT_RETENTION_DAYS = 180;

//管理端单次导入请求最多的行数，客户端按此分批提交。
const int SENSITIVE_IMPORT_MAX_LINES = 1000;

//管理端单次批量删除最多的词条数。
const int SENSITIVE_DELETE_MAX_IDS = 500;

//词条备注最多的字符数，与 t_sensitive_word.f_remark 的 varchar(64) 一致；超出部分按字符截掉。
const int SENSITIVE_REMARK_MAX_CHARS = 64;

//拦截记录里提交者名称、会话名称快照最多的字符数，与 t_sensitive_hit_log 对应列的 varchar(64)
//一致；超出部分按字符截掉。
const int SENSITIVE_NAME_MAX_CHARS = 64;

//管理端列表接口的分页：缺省每页条数与允许的最大每页条数。
const int SENSITIVE_DEFAULT_PAGE_SIZE = 50;
const int SENSITIVE_MAX_PAGE_SIZE = 200;

} // namespace chatserver
