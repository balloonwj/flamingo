/**
 *  联系人标签存储管理器，ContactTagManager.h
 *
 *  负责 t_contact_tag 表的落库与查询。标签是给联系人贴的短词（形如"设计""外包"），
 *  在联系人资料对话框里成排显示。标签与备注名一样是<u>单向</u>的 —— 一行记录的含义是
 *  "f_userid 给 f_target_userid 贴了一个标签"，只影响本人这一侧的显示，对方看不到
 *  也不受影响，所以同一个人在不同人的通讯录里标签可以完全不同。
 *
 *  为什么一行只存一个标签、而不是把多个标签打包成逗号串：打包串既没法按标签检索，
 *  "同一个标签重复添加"也只能靠字符串比对去重；一行一个标签则由主键
 *  (f_userid, f_target_userid, f_tag) 天然保证不重复。
 *
 *  写入语义是<u>整份覆盖</u>：调用方给出某个联系人在本人视角下的完整标签列表，
 *  本类先删掉该联系人名下的全部标签行、再按列表逐条插入，空列表即清空。增、改、删
 *  三种意图共用这一条路径，客户端不必分别发三种请求，服务端也不必判断"改的是哪一条"。
 *  删与插包在同一个事务里，避免出现"删完了只插进去一半"的中间状态。
 *
 *  每次操作单独建立一条数据库连接（与 ContactRemarkManager / FavoriteManager 同一风格），
 *  不在内存做缓存 —— 标签是低频写、按用户一次性读，缓存不划算还要处理失效。
 *
 *  典型用法：
 *      会话层 ChatSession::onSetContactTags 收到 cmd_set_contact_tags 后调
 *      Singleton<ContactTagManager>::Instance().setTags(...)；
 *      ChatSession::onGetContacts 组装通讯录时调 getTagsAll 一次取全，
 *      再逐个填进各 ContactInfo::tags。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

//单个标签的最大字符数。与 t_contact_tag.f_tag 的列宽 varchar(16) 一致：
//MySQL 侧超长会被静默截断（严格模式下则直接报错），与其让用户贴上一个"看起来
//成功、回头一看少半截"的标签，不如在入口处直接拒绝。
const int kMaxContactTagChars = 16;

//单个标签的最大字节数。库的字符集是 utf8（一个汉字占 3 字节），故按字符上限的
//3 倍换算。服务端不解析 UTF-8，只能按字节判，口径与 kMaxContactRemarkBytes 一致。
//已知的宽松之处：纯 ASCII 标签写满 48 字节就是 48 个字符，超过了列宽 varchar(16)，
//MySQL 侧会截断到 16 个字符。标签的实际用法是"设计""外包"这类中文短词，
//48 字节对应 16 个汉字、正好与列宽相当，故按字节判在实际使用中够用。
const int kMaxContactTagBytes = kMaxContactTagChars * 3;

//同一个联系人下最多能贴多少个标签。标签是用来一眼扫过去认人的，贴上几十个
//既排不下也失去了区分度；设上限同时也避免有人恶意刷表。超出时以
//error_code_tag_limit 拒绝，且判定发生在归一之后（见 normalizeTags）。
const int kMaxContactTagCount = 10;

//联系人标签存储管理器；全局单例，经 Singleton<ContactTagManager> 访问。
//本类无需 init()：不持有任何启动期加载的状态，每次调用现开连接现查。
class ContactTagManager final
{
public:
    ContactTagManager();
    ~ContactTagManager();

    ContactTagManager(const ContactTagManager& rhs) = delete;
    ContactTagManager& operator=(const ContactTagManager& rhs) = delete;

    /**
     *  把一份待提交的标签列表归一成"服务端真正会存下来的样子"。纯函数，不碰数据库。
     *
     *  归一分三步，顺序固定：
     *    1. 去掉每条标签首尾的空白字符（空格 / 制表符 / 回车 / 换行）；
     *    2. 丢掉去空白后变成空串的条目 —— 客户端的标签输入框里留下一个空行是常事，
     *       不该因此在库里存一个看不见的标签；
     *    3. <u>不区分大小写</u>去重，保留首次出现者的<u>原始大小写</u>。不区分大小写是
     *       为了与库的行为一致：MySQL 侧 utf8_general_ci 与 SQLite 侧 COLLATE NOCASE
     *       都把 UI 与 ui 当作同一个主键值，代码若不先去重，两条只差大小写的标签
     *       递到库里就会撞主键。保留原始大小写则是尊重用户输入。
     *
     *    in：  待归一的原始列表，顺序即用户期望的显示顺序。
     *    out： [出参] 归一结果；<u>进函数先清空</u>。相对顺序与 in 一致。
     *
     *  本方法<u>不做</u>长度与条数校验 —— 那两项是"拒绝"而不是"归一"，放在 setTags 里，
     *  否则调用方会以为提交成功了、实际却被悄悄砍掉几条。
     *
     *  之所以做成 public：会话层组装应答要回带"服务端归一后的最终列表"，复用本方法
     *  就不必为了拿这份列表再查一次库。
     */
    static void normalizeTags(const std::vector<std::string>& in, std::vector<std::string>& out);

    /**
     *  一次性取某用户贴出的全部标签，按被贴标签的联系人归组，供通讯录应答
     *  （cmd_get_contacts）逐个填充。
     *
     *  为什么要"一次取全"而不是每个联系人查一次：企业通讯录一次要下发成百上千个
     *  联系人，逐个查就是成百上千次数据库往返，登录会被拖到几秒。理由同
     *  ContactRemarkManager::getRemarks。
     *
     *    userid： 以谁的视角看（标签是单向的，只属于这个人）；须为正数。
     *    out：    [出参] 目标userid → 该联系人名下的标签列表 的映射；<u>进函数先清空</u>。
     *             每个列表内按 f_order 升序排列，即用户当初提交时的顺序。
     *             表里只存"确实贴过标签"的行，故映射里不会出现空列表。
     *
     *  @return true 查询成功（一个标签都没贴也算成功，此时 out 为空）；
     *          false 表示数据库不可用。调用方在 false 时应按"没有任何标签"降级处理，
     *          不要因此让整个通讯录应答失败 —— 标签拉不到只是资料卡上少几个词，
     *          不该连部门树也看不成。
     */
    bool getTagsAll(int64_t userid, std::map<int32_t, std::vector<std::string> >& out);

    /**
     *  整份覆盖某个联系人在本人视角下的标签列表。增、改、删共用本方法。
     *
     *    userid：       标签归属用户的userid（"谁贴的"）；须为正数。
     *    targetUserId： 被贴标签的联系人userid（"贴给谁"）；须为正数，且不得等于 userid
     *                   —— 给自己贴标签没有意义，通讯录里也不显示本人。
     *    tags：         该联系人在本人视角下的<u>完整</u>标签列表，UTF-8 文本。
     *                   <u>空列表表示清空</u>该联系人名下的全部标签。
     *                   列表内容先经 normalizeTags 归一，再逐条落库，下标即 f_order。
     *                   标签文本不受内容限制，含单引号 / 反斜杠 / 中文都会被正确转义后落库。
     *    now：          当前时刻，Unix 毫秒；写入 f_update_time。由调用方传入而非
     *                   本类自取，便于测试构造确定的时间序。清空时忽略该值。
     *    outErrorCode： [出参] 失败原因（取值见 Protocol.h 的 enum error_code）；
     *                   成功时置为 error_code_ok。可能的失败：
     *                     error_code_bad_request     —— userid / targetUserId 取值非法，
     *                                                   或某条标签归一后仍超过 kMaxContactTagBytes；
     *                     error_code_tag_limit       —— 归一后的条数超过 kMaxContactTagCount；
     *                     error_code_server_internal —— 数据库不可用或语句执行失败。
     *
     *  @return true 操作成功；false 时看 outErrorCode。
     *
     *  校验顺序是<u>先归一、再判条数</u>：用户提交 11 条、其中两条只差大小写时，归一后
     *  只剩 10 条，应当放行而不是以"超出上限"拒绝。单条超长则在归一之后逐条判，
     *  只要有一条超长就整份拒绝、<u>一行都不写入</u>，不做"留下合格的几条"这种半吊子处理。
     *
     *  写入是<u>幂等</u>的：把同一份列表重复提交两次，第二次的结果与第一次完全一样。
     *
     *  本方法<u>不校验</u>"目标用户是否存在""是不是同一家企业"，那属于会话层的业务判定。
     */
    bool setTags(int64_t userid, int64_t targetUserId, const std::vector<std::string>& tags,
                 int64_t now, int& outErrorCode);

    /**
     *  删掉<u>所有人</u>贴给某个联系人的标签。
     *
     *    target： 被贴标签的联系人userid；须为正数。
     *
     *  @return true 删除成功（一行都没有也算成功）；false 表示数据库不可用或语句失败。
     *
     *  用于账号停用、成员被移出企业这类"这个人从此不该出现在任何人通讯录里"的场合，
     *  与 StarredManager::removeStarredForAll 同一用途。目前尚无调用点，接口先备着 ——
     *  清理动作散落在各处比集中在一处更难补全，宁可先把入口留出来。
     */
    bool removeTagsForAll(int64_t target);
};
