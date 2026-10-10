/**
 *  发起共享与调整成员的纯逻辑，ShareCreateModel.h
 *
 *  发起共享对话框与成员编辑部件（docs/文件共享方案.md 7.4、8.1、8.7）的数据都在这里：
 *    - CShareMemberTable：成员的勾选与逐人的五个写权限位，列标题的「全选此列」，单文件共享没有写权限；
 *      生成 2201 的成员列表，或与调整前比较生成 2204 的追加、移除、改权限三组；2201 回 821 / 819 时按应答标出不满足条件的成员；
 *    - 共享的显示名与显示路径、敏感位置的判断（系统盘根目录、Windows 目录、Program Files、用户目录根）、本机驱动器列表；
 *    - clientshareid 的生成。
 *
 *  除 ListLocalDrives、CollectSensitivePaths、NewClientShareId 要调系统接口外都是纯函数。
 *  单元测试见 FlamingoFileShare/Tests/ShareCreateModelTests.cpp（标签 [sharecreate]）。
 *
 *  典型用法（发起共享对话框）：
 *      CShareMemberTable table;
 *      table.Reset(groupMembers, false);
 *      table.SetChecked(10002, true);
 *      table.SetColumn(share_perm_upload, true);
 *      ShareCreateParams params;
 *      params.m_members = table.CheckedMembers();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ShareProtocolCodec.h"          //ShareMemberPerm / ShareCreateParams
#include "../../Source/net/Protocol.h"   //share_perm / share_kind

namespace fileshare {

//五个写权限位，按界面上列的顺序：上传、新建文件夹、重命名、删除、移动
const int kShareMemberPermColumns[] = { share_perm_upload, share_perm_mkdir, share_perm_rename, share_perm_delete,
                                        share_perm_move };

//写权限的列数
const size_t kShareMemberPermColumnCount = sizeof(kShareMemberPermColumns) / sizeof(kShareMemberPermColumns[0]);

//一列的勾选状态（列标题的「全选此列」显示用）
enum share_column_state
{
    share_column_none  = 0,   //勾选的成员里没有人有这一位（或没有勾选任何成员）
    share_column_some  = 1,   //部分人有
    share_column_all   = 2,   //勾选的成员都有
};

//成员表里的一行
struct ShareMemberRow
{
    int64_t     m_userId;    //成员 userid
    std::string m_name;      //显示名（UTF-8）
    bool        m_checked;   //是否勾选为成员
    int         m_perms;     //写权限位
    bool        m_fixed;     //是否固定勾选、不可取消（单聊的对方）
    bool        m_invalid;   //服务端拒绝的成员（821 跨企业、819 不属于会话），界面上标出

    ShareMemberRow()
        : m_userId(0)
        , m_checked(false)
        , m_perms(0)
        , m_fixed(false)
        , m_invalid(false)
    {
    }
};

//一个敏感位置（发起共享时弹风险提示）
struct ShareSensitivePath
{
    std::wstring m_path;          //敏感目录的本机路径
    bool         m_withSubtree;   //其下的子目录是否也算敏感：Windows 目录、Program Files 算；系统盘根目录与用户目录根不算
                                  //（用户目录下的「文档」等是正常的共享对象）

    ShareSensitivePath()
        : m_withSubtree(false)
    {
    }
};

//多人共享（「文件共享」窗口按选好的对象发起、勾选了多人）新建的群的群名最多的字符数（UTF-16 码元），
//与服务端 t_group.f_name 的 varchar(64) 一致（见 flamingoserver/chatserversrc/GroupAutoName.cpp）
const size_t kShareGroupNameMaxChars = 64;

//共享名称最多的字符数（UTF-16 码元）。共享名称是分享者以外的人唯一能看到的称呼（2026-10-04 起），
//30 个汉字按 UTF-8 最多 90 字节，在服务端 kShareNameMaxBytes（260 字节）之内
const size_t kShareTitleNameMaxChars = 30;

//本机的一个驱动器（发起「磁盘」共享时选）
struct ShareDriveInfo
{
    std::wstring m_root;    //根目录，如 "D:\"
    std::wstring m_label;   //卷标；没有时为空
    bool         m_removable; //是否可移动磁盘（U 盘等）
    bool         m_system;  //是否系统盘

    ShareDriveInfo()
        : m_removable(false)
        , m_system(false)
    {
    }
};

/**
 *  CShareMemberTable —— 成员与逐人写权限的表。只在 UI 线程使用。
 */
class CShareMemberTable
{
public:
    CShareMemberTable();

    /**
     *  用候选成员重置表格：全部不勾选、没有权限。
     *  @param candidates 候选成员（userid 与显示名）
     *  @param fileShare  是否为单文件共享（单文件共享没有写权限，SetPerm / SetColumn 一律无效）
     */
    void Reset(const std::vector<ShareMemberRow>& candidates, bool fileShare);

    /**
     *  把某个成员设为固定勾选（单聊的对方：只能共享给他一个人，且不能取消）。
     */
    void SetFixed(int64_t userId);

    /**
     *  按已有成员预先勾选并填权限（调整成员时用）。不在候选里的成员追加到表末尾。
     */
    void ApplyExisting(const std::vector<ShareMemberPerm>& members, const std::vector<ShareMemberRow>& names);

    //勾选或取消一个成员；固定成员不能取消；取消时清掉他的权限
    void SetChecked(int64_t userId, bool checked);

    //设置或清除一个成员的某个权限位；未勾选的成员、单文件共享时无效
    void SetPerm(int64_t userId, int perm, bool on);

    /**
     *  列标题的「全选此列」：对全部已勾选的成员设置或清除这一位。
     */
    void SetColumn(int perm, bool on);

    //一列在已勾选成员里的状态
    int ColumnState(int perm) const;

    //全部行（界面按它绘制）
    const std::vector<ShareMemberRow>& Rows() const;

    //已勾选的成员及其权限（生成 2201 的 members）
    std::vector<ShareMemberPerm> CheckedMembers() const;

    /**
     *  与调整前的成员比较，生成 2204 的三组。
     *  @param before 调整前的成员与权限
     *  @param add    [出参] 新增的成员
     *  @param remove [出参] 移除的成员
     *  @param update [出参] 权限有变化的成员
     *  @return 有任何变化返回 true
     */
    bool Diff(const std::vector<ShareMemberPerm>& before, std::vector<ShareMemberPerm>& add,
              std::vector<int64_t>& remove, std::vector<ShareMemberPerm>& update) const;

    /**
     *  标出服务端拒绝的成员（2201 / 2204 回 821 或 819 时应答里的 invalidmembers），其余清除标记。
     */
    void MarkInvalid(const std::vector<int64_t>& userIds);

    //是否为单文件共享
    bool IsFileShare() const;

    /**
     *  改共享类型（发起时换了要共享的对象）：改为单文件共享时清掉全部写权限，勾选状态保留。
     *  @param fileShare 是否为单文件共享
     */
    void SetFileShare(bool fileShare);

private:
    //按 userid 找行；没有返回 NULL
    ShareMemberRow* Find(int64_t userId);

    std::vector<ShareMemberRow> m_rows;        //全部行
    bool                        m_fileShare;   //是否为单文件共享
};

/**
 *  ShareCreateModel —— 发起共享的其余纯逻辑；全部为静态方法，不可实例化。
 */
class ShareCreateModel
{
public:
    /**
     *  共享对象在分享者自己界面上的称呼：磁盘为「本地磁盘 (D:)」（有卷标时用卷标），文件夹与文件为最后一段。
     *  只用于分享者本人看到的地方（磁盘菜单、已选对象）；2026-10-04 起不再用作共享名称，因为它取自真实路径。
     *  @param path  共享根的本机路径
     *  @param kind  共享类型
     *  @param label 卷标（磁盘时用，可为空）
     */
    static std::wstring DisplayName(const std::wstring& path, int kind, const std::wstring& label);

    /**
     *  默认的共享名称「XXX的文件共享」（2026-10-04 用户要求）：分享者没有起名字时用它。
     *  @param selfName 分享者本人的名字（昵称，昵称为空时为账号）；为空时返回「我的文件共享」整句
     *  @return 默认名称
     */
    static std::wstring DefaultShareName(const std::wstring& selfName);

    /**
     *  按分享者在「共享名称」框里填的内容得出实际使用的共享名称：去掉首尾空白，为空时用 DefaultShareName，
     *  超过 kShareTitleNameMaxChars 时截短（截断处不拆开 UTF-16 代理对）。
     *  @param input    名称框里的文字
     *  @param selfName 分享者本人的名字，见 DefaultShareName
     *  @return 共享名称，非空
     */
    static std::wstring ResolveShareName(const std::wstring& input, const std::wstring& selfName);

    /**
     *  判断共享根是否涉及敏感位置，三种情况都算：就是某个敏感目录；是它的上级（共享整个系统盘也就包含了其下的
     *  Windows 目录）；位于 m_withSubtree 为 true 的敏感目录之下。不区分大小写，不访问文件系统。
     *  @param path      共享根的本机路径
     *  @param sensitive 敏感位置（CollectSensitivePaths 的结果，测试可自行构造）
     *  @return 敏感返回 true（发起前弹风险提示）
     */
    static bool IsSensitivePath(const std::wstring& path, const std::vector<ShareSensitivePath>& sensitive);

    /**
     *  取本机的敏感位置：系统盘根目录、Windows 目录、Program Files（含 x86）、用户目录根。取不到的一项不列出。
     */
    static std::vector<ShareSensitivePath> CollectSensitivePaths();

    /**
     *  列出本机可以共享的驱动器（固定磁盘与可移动磁盘，按盘符排序）。
     */
    static std::vector<ShareDriveInfo> ListLocalDrives();

    /**
     *  生成一个新的 clientshareid（GUID 的小写十六进制，带连字符）。
     */
    static std::string NewClientShareId();

    /**
     *  2201 / 2204 应答的结果码对应的提示文字（821、819、815 等有专门的说法，其余按通用错误）。
     */
    static std::wstring CreateErrorText(int code);

    /**
     *  多人共享新建的群的群名（2026-10-03 用户要求）：「共享 <共享名> 给 <成员名>」，成员名用「、」分隔。超过 maxChars 时
     *  依次少列几个人、以「等 N 人」收尾；只列一个人仍超长时截短共享名、以「…」收尾；成员名本身就超长时整体截断。
     *  截断处不拆开 UTF-16 代理对。
     *  @param shareName   共享名称（ResolveShareName 的结果；不能用 DisplayName，群名所有成员都看得到）
     *  @param memberNames 成员的显示名，按成员表顺序；为空时只写「共享 <共享名>」
     *  @param maxChars    群名最多的字符数（UTF-16 码元），通常为 kShareGroupNameMaxChars
     *  @return 群名，长度不超过 maxChars
     */
    static std::wstring DirectShareGroupName(const std::wstring& shareName, const std::vector<std::wstring>& memberNames,
                                             size_t maxChars);

private:
    ShareCreateModel() = delete;
};

} // namespace fileshare
