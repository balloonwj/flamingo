/** 
 *  管理所有的用户信息，初始信息从数据库中加载, UserManager.h
 *  zhangyl 2017.03.15
 **/

#pragma once
#include <stdint.h>
#include <string>
#include <list>
#include <vector>
#include <mutex>
#include <set>
#include <map>
#include <atomic>
#include <random>

using namespace std;

#define GROUPID_BOUBDARY   0x0FFFFFFF 

//用户或者群
struct User
{
    int32_t        userid;      //0x0FFFFFFF以上是群号，以下是普通用户
    string         username;    //群账户的username也是群号userid的字符串形式
    string         password;
    string         nickname;    //群账号为群名称
    int32_t        facetype;
    string         customface;
    string         customfacefmt;//自定义头像格式
    int32_t        gender;
    int32_t        birthday;
    string         signature;
    string         address;
    string         phonenumber;
    string         mail;
    string         landline;    //座机，对应 t_user.f_landline；未填为空串
    string         post;        //岗位：本人填写的自由文本，对应 t_user.f_post，与部门职位无关；未填为空串
    int32_t        phonevisible;    //手机号是否对同事公开，取值见 Protocol.h 的 enum profile_visibility，对应 t_user.f_phone_visible；默认不公开
    int32_t        landlinevisible; //座机是否对同事公开，取值同上，对应 t_user.f_landline_visible；默认公开
    //历史遗留列 t_user.f_teaminfo（QQ 式好友分组信息）。企业版已取消好友分组，
    //本字段只在加载 / 回写用户资料时原样搬运，不再有任何业务读它。
    string             teaminfo;
    int32_t            ownerid;        //对于群账号，为群主userid
    int32_t            status;         //账号状态：0=启用 1=停用（由 admin 控制台 EmployeeManager 维护，停用后禁止登录）
    string             salt;           //v2 登录口令盐值，对应 t_user.f_salt；普通账号注册时生成
    int32_t            usertype;       //v2 用户类型：0=普通用户 1=服务号用户，对应 t_user.f_usertype
};

class UserManager final
{
public:
    UserManager();
    ~UserManager();

    bool init();

    UserManager(const UserManager& rhs) = delete;
    UserManager& operator=(const UserManager& rhs) = delete;

    /**
     *  分配一个新的用户ID。本进程内**唯一**的用户ID分配入口 —— 其它需要建号的
     *  模块（如 admin 控制台的 EmployeeManager）一律调本方法，不得再各自维护
     *  一份自增基数，否则两处会分配出同一个ID、撞上 t_user 的主键。
     *  @return 新的用户ID（正数）。基数在 init() 时取自库中最大的普通用户ID。
     */
    int32_t allocUserId();

    /**
     *  抽一个没人用的随机登录账号：前缀 + 8 位随机数字（docs/登录账号与手机号登录方案.md 第 3.3 节）。
     *  在用户缓存里按不区分大小写查重（与库上 uk_f_username 的比较规则一致），撞号重抽，最多
     *  ACCOUNT_ALLOC_MAX_TRIES 次。线程安全：内部对 m_mutex 加锁。
     *  并发：查重与写库之间不加锁，两次请求恰好抽中同一个号时，后写库的那次被唯一索引拒绝、建号失败。
     *  @param prefix 企业账号前缀（已规整，见 AccountName.h 的 NormalizeAccountPrefix）；为空时用 EMP
     *  @return 账号；重抽次数用完仍然撞号时返回空串（理论上不会发生），调用方按建号失败处理
     */
    std::string allocRandomAccount(const std::string& prefix);

    /**
     *  用固定种子重新播种抽账号的随机数发生器，只供单元测试复现抽号序列（预知下一个号、验证撞号重抽）。
     *  生产代码不要调用：固定种子会让每次启动后抽出的账号序列相同。
     *  @param seed 种子
     */
    void seedAccountRngForTest(uint32_t seed);

    /**
     *  新建一个账号并落库（t_user）。
     *  @param u            账号信息，入参使用其 username/nickname/password/salt/
     *                      usertype/phonenumber；成功后回填 userid 与若干字段默认值。
     *                      **username 留空时**由本方法用 allocRandomAccount 抽一个
     *                      默认 EMP 前缀的随机账号（形如 "EMP48203917"）并回填；
     *                      PC 客户端自助注册会先按企业前缀抽好账号再调用本方法。
     *                      调用方已填则原样使用。
     * @param enterpriseId 所属企业的业务ID，写入 t_user.f_enterprise_id。
     *                      群账号等没有企业归属的对象传 0。
     *  @return 落库成功返回 true；失败时不会把该账号加入内存缓存。
     */
    bool addUser(User& u, int64_t enterpriseId);

    //TODO: 这类都是更新用户信息的接口，将来可以考虑统一起来
    bool updateUserInfoInDb(int32_t userid, const User& newuserinfo);
    bool modifyUserPassword(int32_t userid, const std::string& newpassword);

    //TODO: 当用户越来越多，并发量越来越多的时候，这个系列的函数效率高吗？
    //按账号查用户，不区分大小写（与库上 uk_f_username 的比较规则一致）
    bool getUserInfoByUsername(const std::string& username, User& u);
    //按手机号查普通用户（不看群账号）；空串返回 false
    bool getUserInfoByPhone(const std::string& phone, User& u);

    /**
     *  登录时按输入找用户（docs/登录账号与手机号登录方案.md 第 3.4 节）：先按账号（不区分大小写）；
     *  查不到、且输入是 11 位以 1 开头的数字时，再按手机号找。
     *  @param input          用户在登录框里输入的账号或手机号
     *  @param u              [出参] 找到的用户
     *  @param matchedByPhone [出参] 是按手机号找到的为 true（用于日志）
     *  @return 找到返回 true
     */
    bool findLoginUser(const std::string& input, User& u, bool& matchedByPhone);

    /**
     *  手机号是否已被别的普通用户使用（手机号可用于登录，必须全局唯一）。只看普通用户，不看群账号。
     *  @param phone         手机号；空串一律返回 false
     *  @param excludeUserId 不计入的用户ID（编辑员工时传本人，保留自己原来的号码不算重复）；新建时传 0
     *  @return 被别人用着返回 true
     */
    bool isPhoneTaken(const std::string& phone, int32_t excludeUserId);

    //按账号(查用户)或群号(查群)的前缀检索；wantGroup=false 查普通用户、true 查群
    bool searchUsersByPrefix(const std::string& prefix, bool wantGroup,
                             std::vector<User>& out, int maxResults);

    //黑名单：把 blockedUserid 加入 userid 的黑名单；已在黑名单中也视为成功（联合主键去重）
    bool addToBlacklist(int32_t userid, int32_t blockedUserid);
    //黑名单：把 blockedUserid 移出 userid 的黑名单；记录本就不存在也视为成功
    bool removeFromBlacklist(int32_t userid, int32_t blockedUserid);
    //黑名单：判断 blockedUserid 是否在 userid 的黑名单中
    bool isInBlacklist(int32_t userid, int32_t blockedUserid);
    bool getUserInfoByUserId(int32_t userid, User& u);
    bool getUserInfoByUserId(int32_t userid, User*& u);

    /**
     *  按 userid 重新从数据库同步单个用户到内存缓存，使 admin 控制台对 t_user 的
     *  增删改即时反映到 IM 侧（否则该缓存只在启动时加载，需重启 chatserver 才生效）。
     *  - 用户在库中存在、缓存中也存在：更新缓存里的标量字段；
     *  - 库中存在、缓存中不存在：作为新用户载入；
     *  - 库中不存在（已被删除）：从缓存中移除。
     *  线程安全：内部对 m_mutex 加锁，可从任意线程调用。
     *  @param userid 目标用户ID。
     *  @return true 表示同步成功（"用户已删除"也算成功）；false 表示数据库访问失败。
     */
    bool reloadUserCache(int32_t userid);

private:
    bool loadUsersFromDb();

private:
    std::atomic_int     m_baseUserId{ 0 };        //m_baseUserId, 取数据库里面userid最大值，新增用户在这个基础上递增
    std::atomic<int>    m_baseGroupId{0x0FFFFFFF};
    list<User>          m_allCachedUsers;
    mutex               m_mutex;
    std::mt19937        m_accountRng;   //抽随机账号用的随机数发生器，构造时以 std::random_device 播种；受 m_mutex 保护
};