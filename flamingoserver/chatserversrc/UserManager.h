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

#define DEFAULT_TEAMNAME  "My Friends"

enum FRIEND_OPERATION
{
    FRIEND_OPERATION_ADD,
    FRIEND_OPERATION_DELETE
};

struct FriendInfo
{
    int32_t friendid;
    string  markname;
    string  teamname;
};

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
    /*
    个人用户好友分组信息，对于群账户则为空，例如:
    [{"teamname": "我的好友"}, {"teamname": "我的同事"}, {"teamname": "企业客户"}]
    */
    string             teaminfo;       //对于普通用户，为分组信息；对于群组则为空
    int32_t            ownerid;        //对于群账号，为群主userid
    int32_t            status;         //账号状态：0=启用 1=停用（由 admin 控制台 EmployeeManager 维护，停用后禁止登录）
    string             salt;           //v2 登录口令盐值，对应 t_user.f_salt；普通账号注册时生成
    int32_t            usertype;       //v2 用户类型：0=普通用户 1=服务号用户，对应 t_user.f_usertype
    list<FriendInfo>   friends;
};

//一条加好友申请记录，对应数据库 t_friend_request 的一行
struct FriendRequestRecord
{
    int64_t id;          //申请ID
    int32_t fromuserid;  //申请发起方userid
    int32_t touserid;    //被申请方userid
    string  verifymsg;   //验证留言
    string  source;      //申请来源
    int32_t status;      //状态：0=待处理 1=已同意 2=已拒绝
    int64_t createtime;  //申请时间，Unix毫秒
    int64_t handletime;  //处理时间，Unix毫秒，0表示未处理
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
    bool makeFriendRelationshipInDB(int32_t smallUserid, int32_t greaterUserid);
    bool releaseFriendRelationshipInDBAndMemory(int32_t smallUserid, int32_t greaterUserid);
    bool updateUserRelationshipInMemory(int32_t userid, int32_t target, FRIEND_OPERATION operation);
    bool addFriendToUser(int32_t userid, int32_t friendid);
    bool deleteFriendToUser(int32_t userid, int32_t friendid);

    bool isFriend(int32_t userid, int32_t friendid);
    
    //TODO: 这类都是更新用户信息的接口，将来可以考虑统一起来
    bool updateUserInfoInDb(int32_t userid, const User& newuserinfo);
    bool modifyUserPassword(int32_t userid, const std::string& newpassword);
    //将内存和数据库中的某个用户的分组信息改成新的newteaminfo
    bool updateUserTeamInfoInDbAndMemory(int32_t userid, const std::string& newteaminfo);
    bool deleteTeam(int32_t userid, const std::string& deletedteamname);
    bool modifyTeamName(int32_t userid, const std::string& newteamname, const std::string& oldteamname);
    
    //更新用户好友备注名
    bool updateMarknameInDb(int32_t userid, int32_t friendid, const std::string& newmarkname);
    //更新用户对某好友设置的标签；tagsJson 为标签数组的JSON文本，落库前做SQL转义
    bool updateFriendTagsInDb(int32_t userid, int32_t friendid, const std::string& tagsJson);
    //移动好友至其他分组
    bool moveFriendToOtherTeam(int32_t userid, int32_t friendid, const std::string& newteamname);

    //TODO: 当用户越来越多，并发量越来越多的时候，这个系列的函数效率高吗？
    bool getUserInfoByUsername(const std::string& username, User& u);
    bool getUserInfoByPhone(const std::string& phone, User& u);

    //按账号(查用户)或群号(查群)的前缀检索；wantGroup=false 查普通用户、true 查群
    bool searchUsersByPrefix(const std::string& prefix, bool wantGroup,
                             std::vector<User>& out, int maxResults);
    //新增一条加好友申请，返回新申请ID（自增主键）；失败返回 0
    int64_t addFriendRequest(int32_t fromUserid, int32_t toUserid,
                             const std::string& verifymsg, const std::string& source,
                             int64_t createTime);
    //按申请ID取一条加好友申请；不存在返回 false
    bool getFriendRequestById(int64_t requestId, FriendRequestRecord& out);
    //更新加好友申请的处理状态与处理时间
    bool updateFriendRequestStatus(int64_t requestId, int32_t status, int64_t handleTime);
    //取某用户名下所有待处理(status=0)的加好友申请，按申请时间倒序
    bool getPendingFriendRequests(int32_t toUserid, std::vector<FriendRequestRecord>& out);

    //黑名单：把 blockedUserid 加入 userid 的黑名单；已在黑名单中也视为成功（联合主键去重）
    bool addToBlacklist(int32_t userid, int32_t blockedUserid);
    //黑名单：把 blockedUserid 移出 userid 的黑名单；记录本就不存在也视为成功
    bool removeFromBlacklist(int32_t userid, int32_t blockedUserid);
    //黑名单：判断 blockedUserid 是否在 userid 的黑名单中
    bool isInBlacklist(int32_t userid, int32_t blockedUserid);
    bool getUserInfoByUserId(int32_t userid, User& u);
    bool getUserInfoByUserId(int32_t userid, User*& u);
    bool getFriendInfoByUserId(int32_t userid, std::list<User>& friends);
    //获取好友的备注名
    bool getFriendMarknameByUserId(int32_t userid1, int32_t friendid, std::string& markname);

    /**
     *  一次性取某用户的全部好友备注名（friendid → 备注名）。
     *
     *  与 getFriendMarknameByUserId 的差别只在批量：后者每查一个好友都要把全量
     *  用户缓存重扫一遍找 userid，而通讯录应答要给企业里成百上千个联系人各查一次
     *  备注，逐个查就是 O(用户数 × 联系人数)——万人规模下每次登录上亿次迭代。
     *  本方法扫一遍就把该用户的好友备注全取回来，通讯录组装用这个。
     *
     *    userid：以谁的视角看（备注名是单向的，只属于这个人）。
     *    out：   [出参] friendid → 备注名的映射；进函数先清空。备注为空串的好友
     *            也会入表（表示"是好友但没设备注"）。
     *  返回：该用户在缓存里返回 true；不在（未加载 / 已注销）返回 false，此时
     *        out 为空，调用方按"没有任何备注"处理即可。
     */
    bool getFriendMarknames(int32_t userid, std::map<int32_t, std::string>& out);
    bool getTeamInfoByUserId(int32_t userid, std::string& teaminfo);

    /**
     *  按 userid 重新从数据库同步单个用户到内存缓存，使 admin 控制台对 t_user 的
     *  增删改即时反映到 IM 侧（否则该缓存只在启动时加载，需重启 chatserver 才生效）。
     *  - 用户在库中存在、缓存中也存在：更新缓存里的标量字段，保留其好友列表；
     *  - 库中存在、缓存中不存在：作为新用户载入（连带加载好友关系）；
     *  - 库中不存在（已被删除）：从缓存中移除。
     *  线程安全：内部对 m_mutex 加锁，可从任意线程调用。
     *  @param userid 目标用户ID。
     *  @return true 表示同步成功（"用户已删除"也算成功）；false 表示数据库访问失败。
     */
    bool reloadUserCache(int32_t userid);

private:
    bool loadUsersFromDb();
    bool loadRelationshipFromDb(int32_t userid, std::list<FriendInfo>& r);
    //启动时一次性加载全部好友关系：一条 SQL 查全表 t_user_relationship，
    //内存里按 userid 分发到各普通用户（群账号 / 服务号跳过）。替代逐用户
    //各开一条远程连接的旧做法。
    bool loadAllRelationshipsFromDb();

private:
    std::atomic_int     m_baseUserId{ 0 };        //m_baseUserId, 取数据库里面userid最大值，新增用户在这个基础上递增
    std::atomic<int>    m_baseGroupId{0x0FFFFFFF};
    list<User>          m_allCachedUsers;
    mutex               m_mutex;
};