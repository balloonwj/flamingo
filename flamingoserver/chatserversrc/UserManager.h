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
     *  新建一个账号并落库（t_user）。
     *  @param u            账号信息，入参使用其 username/nickname/password/salt/
     *                      usertype/phonenumber；成功后回填 userid 与若干字段默认值。
     *                      **username 留空时**由本方法按 MakeEmployeeAccount 规则
     *                      根据新分配的用户ID自动生成（形如 "EMP1024"）并回填，
     *                      这是 PC 客户端自助注册走的路径；调用方已填则原样使用。
     * @param enterpriseId 所属企业的业务ID，写入 t_user.f_enterprise_id。
     *                      群账号等没有企业归属的对象传 0。
     *  @return 落库成功返回 true；失败时不会把该账号加入内存缓存。
     */
    bool addUser(User& u, int64_t enterpriseId);

    //TODO: 这类都是更新用户信息的接口，将来可以考虑统一起来
    bool updateUserInfoInDb(int32_t userid, const User& newuserinfo);
    bool modifyUserPassword(int32_t userid, const std::string& newpassword);

    //TODO: 当用户越来越多，并发量越来越多的时候，这个系列的函数效率高吗？
    bool getUserInfoByUsername(const std::string& username, User& u);
    bool getUserInfoByPhone(const std::string& phone, User& u);

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
};