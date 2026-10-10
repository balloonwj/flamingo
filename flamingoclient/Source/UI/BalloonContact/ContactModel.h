/**
 *  ContactModel.h
 *
 *  联系人资料对话框的数据模型 —— 持有被查看联系人的姓名 / 备注名 /
 *  账号 / 地区 / 部门 / 职务 / 个性签名 / 在线状态，以及对话框内可编辑
 *  的标签列表、分组、消息提醒模式、「添加到收藏」开关。
 *
 *  纯数据结构，不涉及任何控件 / 网络 / 持久化。当前以 mock 数据
 *  （「江屿白」那一组）初始化，真实数据接入待后续。
 *
 *  文件末尾还有「个人资料」区块的格子数据（BuildContactProfileCells，2026-10-04）：按模型算出
 *  区块要显示哪些字段、各显示什么文字、哪些带编辑按钮（docs/联系人资料字段扩充方案.md 4.2 节）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （需要 CString）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <vector>
#include "../../Session/ProfileFields.h"   // ProfileEditError；net::ProfileUpdateFields（就地编辑的提交判定）

namespace ballooncontact {

/**
 *  联系人在线状态。逐值对应设计稿 components.jsx 的 STATUS_LABEL：
 *  决定头像右下角状态点的颜色。
 */
enum ContactStatus
{
    kContactStatusOnline    = 0,   // 在线：状态点绿
    kContactStatusAway      = 1,   // 离开：状态点橙（本对话框 mock 取此值）
    kContactStatusBusy      = 2,   // 勿扰：状态点红
    //编号 3 曾经是「隐身」，本产品不提供该功能，2026-08-13 去掉；此处留空不再复用
    kContactStatusOffline   = 4,   // 离线：不画状态点
};

/**
 *  对话框内的布尔开关。枚举值同时用作 ContactModel 内部开关数组的下标，
 *  故必须从 0 起连续编号，删项时后面的要顺次前移。
 */
enum ContactSwitchKind
{
    kContactSwitchFavorite = 0,   // 添加到收藏：开则该联系人出现在通讯录顶部的「我的收藏」分区
    // 曾经还有 kContactSwitchMute（消息免打扰）与 kContactSwitchStarred（特别关注）两项。
    // 2026-10-03 会话的提醒设置扩展成五种互斥的模式，两个开关合并成「消息提醒」下拉框，
    // 状态改由 ContactModel::NotifyMode 表示（docs/消息提醒模式与消息盒子方案.md §4.2）。
    // 曾经还有一项 kContactSwitchPinned（置顶聊天）。置顶是「会话」的属性而不是
    // 「人」的属性 —— 会话列表的右键菜单里已经提供了该项；资料窗这一份从未接过
    // 任何协议，拨了也不生效，2026-08-17 整项移除。
    // 更早还有一项 kContactSwitchBlocked（加入黑名单）。企业版取消了好友关系，
    // 该开关连同好友功能一并移除；它此前也从未发出过任何协议，拨动只改内存、
    // 窗口一关就没了。
};

// 对话框内开关的总数。
const int kContactSwitchCount = 1;

/**
 *  资料窗头部头像要画什么（2026-10-07）。由主面板按该用户的头像设置（facetype / customface）用
 *  balloonmain::DecodeAvatar 解码后填入，模型只负责保存，窗口据此生成头像位图：有本机图片时画图片，
 *  否则画「字形 + 底色」。m_valid 为 false 时（调试入口、单元测试的默认模型）窗口沿用原来的画法：
 *  姓名首字加固定底色 kContactAvatarBgColor。
 */
struct ContactAvatarSource
{
    bool     m_valid;       // 是否已由主面板填好
    CString  m_glyph;       // 字形头像要画的字（1～2 个字符，可含 emoji）；图片载入失败时也画它
    COLORREF m_color;       // 字形头像的底色
    CString  m_imagePath;   // 图片头像在本机缓存里的完整路径；空表示画字形（没有设图片，或图片还没下载到本机）

    ContactAvatarSource()
        : m_valid(false)
        , m_color(RGB(0, 0, 0))
    {
    }
};

/**
 *  比较两份头像来源是否相同（四项都相同才算）。窗口据此判断要不要重新生成头像位图。
 *    a、b：要比较的两份来源。
 *  @return 相同返回 true。纯函数。
 */
bool SameAvatarSource(const ContactAvatarSource& a, const ContactAvatarSource& b);

/**
 *  把协议的在线状态（net/Protocol.h 的 presence_type）换算成资料窗的 ContactStatus：在线、离开、
 *  忙碌三种都按在线（状态点画绿色），离线与越界值（含已作废的 4）按离线。主面板打开资料窗时与
 *  对方状态变化时共用本函数，两处口径一致。
 *    presence：在线状态。
 *  @return kContactStatusOnline 或 kContactStatusOffline。纯函数，无副作用。
 */
int ContactStatusFromPresence(int presence);

/**
 *  ContactModel —— 联系人资料对话框的数据模型。
 *
 *  分两部分：
 *    · 只读资料区 —— 姓名 / 备注名 / 账号 / 地区 / 部门 / 职务 / 签名 /
 *      状态。由窗口在打开时一次性灌入（当前为 mock）。
 *    · 可编辑区 —— 标签列表、分组、三个开关。用户在对话框里增删标签 /
 *      切换分组 / 拨动开关时，窗口同步调用本模型的写接口。
 */
class ContactModel
{
public:
    // 构造：填入「江屿白」那一组 mock 数据（见各字段默认值说明）。
    ContactModel();

    // ---- 真实数据注入（替换 mock）----

    // 一次性灌入只读资料区（姓名 / 账号 / 地区 / 职务 / 部门路径 / 签名 / 状态）。
    // 备注名单独由 SetRemark 设；标签、分组另由 ClearTags / SetGroups 设。
    //   name      真名（昵称）
    //   account   账号
    //   region    所在地区，可空
    //   title     职务，可空
    //   dept      部门路径（如「产品部 / 设计组」），可空
    //   signature 个性签名，可空
    //   status    在线状态，取值见 ContactStatus
    void SetBasicInfo(const CString& name, const CString& account, const CString& region,
                      const CString& title, const CString& dept, const CString& signature,
                      int status);

    // 清空标签列表（真实数据暂无标签时调用，去掉 mock 标签）。
    void ClearTags();

    // 用给定分组列表替换 mock 分组，并选中 selected 项（越界则选 0）。
    //   groups   分组名列表（如仅一项部门路径）。
    //   selected 选中下标。
    void SetGroups(const std::vector<CString>& groups, int selected);

    // ---- 只读资料 ----

    // 真名（设计稿 contact.name），如「江屿白」。
    const CString& Name() const;

    // 备注名（设计稿 contact.remark），如「设计组组长」；可为空。
    const CString& Remark() const;

    // 设置备注名。供「备注名」行内编辑提交时调用。
    //   remark：新的备注名，允许为空字符串（表示清除备注）。
    void SetRemark(LPCTSTR remark);

    // 账号（设计稿 contact.account），如「10923847」。
    const CString& Account() const;

    // 所在地区（设计稿 contact.region），如「北京」。
    const CString& Region() const;

    // 职务（设计稿 contact.title），如「设计组组长」；可为空。
    const CString& Title() const;

    // 部门路径（设计稿 contact.dept），如「产品部 / 设计组」；可为空。
    const CString& Dept() const;

    // 个性签名（设计稿 contact.signature），如「在出差路上…」；可为空。
    const CString& Signature() const;

    // ---- 「个人资料」区块（2026-10-04，docs/联系人资料字段扩充方案.md）----

    // 一次性灌入个人资料区块的字段。个性签名与所在地沿用 SetBasicInfo 里的 signature / region。
    //   gender    性别编码，取值见 Protocol.h 的 enum profile_gender
    //   birthday  生日，YYYYMMDD；0 表示未填
    //   mail      邮箱，可空
    //   mobile    手机号，可空
    //   landline  座机，可空
    //   post      岗位（本人填写的自由文本，与部门职务无关），可空
    void SetProfileFields(int gender, int birthday, const CString& mail, const CString& mobile,
                          const CString& landline, const CString& post);

    // 性别编码，取值见 Protocol.h 的 enum profile_gender。
    int Gender() const;

    // 生日，YYYYMMDD；0 表示未填。
    int Birthday() const;

    // 邮箱；可为空。
    const CString& Mail() const;

    // 手机号；可为空。
    const CString& Mobile() const;

    // 座机；可为空。
    const CString& Landline() const;

    // 岗位；可为空。
    const CString& Post() const;

    // 设置本人手机号、座机对同事的公开范围（2026-10-06，docs/联系人资料字段扩充方案.md 第 9 节）。
    // 只有看本人时才有意义：看别人时服务端已把不公开的号码置为空串，模型不需要知道对方的设置。
    //   phoneVisible：手机号的公开范围，取值见 Protocol.h 的 enum profile_visibility
    //   landlineVisible：座机的公开范围，取值同上
    void SetProfileVisibility(int phoneVisible, int landlineVisible);

    // 手机号对同事的公开范围，取值见 enum profile_visibility；默认「仅本人可见」。
    int PhoneVisible() const;

    // 座机对同事的公开范围，取值同上；默认「对同事可见」。
    int LandlineVisible() const;

    // 设置被查看的是否是当前登录用户本人。由主面板判定后设入，窗口自身不做判定。
    //   self：true 表示本人（个人资料区块全部展开、可就地编辑）。
    void SetSelf(bool self);

    // 被查看的是否是当前登录用户本人；默认 false。
    bool IsSelf() const;

    // 设置本人的昵称原值（2026-10-07，看本人时「个人资料」区块第一格可就地编辑昵称）。与 Name() 不同：
    // Name() 在没有昵称时退回账号，这里保存的是昵称本身，可以为空。看别人时不用。
    //   nickname：昵称原值。
    void SetNickname(const CString& nickname);

    // 本人的昵称原值；没有设置过时为空。
    const CString& Nickname() const;

    // 设置头部头像要画什么（见 ContactAvatarSource），由主面板解码头像设置后填入。
    //   source：头像来源。
    void SetAvatarSource(const ContactAvatarSource& source);

    // 头部头像要画什么；默认 m_valid 为 false（按姓名首字加固定底色画）。
    const ContactAvatarSource& AvatarSource() const;

    // 把一次改资料要提交的字段写进模型（就地编辑提交后先把新值显示出来）。只处理「个人资料」区块里
    // 能改的八项（昵称、签名、性别、生日、所在地、邮箱、座机、岗位）与两个公开范围，没标记提交的项保持不变。
    // 昵称改了时头部与标题用的姓名 Name() 一并换成新昵称（新昵称为空时退回账号）。
    //   fields：要提交的字段。
    void ApplyProfileUpdate(const net::ProfileUpdateFields& fields);

    // 在线状态，取值见 ContactStatus。
    int Status() const;

    // 设置在线状态，取值见 ContactStatus。资料窗开着时对方上线 / 下线，由主面板经资料窗调用。
    void SetStatus(int status);

    // 设置头像是否画成灰度（离线用户头像置灰，2026-10-03）。灰彩由主面板按
    // balloonmain::ShouldGrayUserAvatar 算好后设入，模型只负责保存。
    void SetAvatarGray(bool gray);

    // 头像是否画成灰度；默认 false（彩色）。
    bool IsAvatarGray() const;

    // 头像占位字母（真名首字），如「江」。
    CString AvatarLetter() const;

    // ---- 标签 ----

    // 当前标签数。
    int TagCount() const;

    // 读取第 index 个标签文本。index 越界返回空 CString。
    //   index：取值 [0, TagCount())。
    CString Tag(int index) const;

    // 读取整份标签列表（只读引用，生命周期同本模型对象）。窗口把它拷一份出来
    // 交给判定函数，判定完再整份写回。
    const std::vector<CString>& Tags() const;

    // 用给定列表整份替换标签。标签的三种操作（增、改、删）在协议上都是整份
    // 覆盖语义，故写接口也只提供"整份替换"这一个，不再逐项增删。
    //   tags：新的完整标签列表；空列表表示这个联系人一个标签都没有。
    void SetTags(const std::vector<CString>& tags);

    // 在末尾追加一个标签。空白文本（去首尾空格后为空）忽略，返回 false。
    //   tag：标签文本。
    //   返回：true 表示已追加；false 表示文本为空被忽略。
    bool AddTag(LPCTSTR tag);

    // 删除第 index 个标签。index 越界则忽略。
    //   index：取值 [0, TagCount())。
    void RemoveTag(int index);

    // ---- 分组 ----

    // 可选分组的总数。
    int GroupCount() const;

    // 读取第 index 个分组名。index 越界返回空 CString。
    //   index：取值 [0, GroupCount())。
    CString Group(int index) const;

    // 当前选中的分组下标，取值 [0, GroupCount())。
    int SelectedGroup() const;

    // 设置当前选中的分组。index 越界则忽略。
    //   index：取值 [0, GroupCount())。
    void SetSelectedGroup(int index);

    // ---- 开关 ----

    // 读取某个开关的状态。kind 越界返回 false。
    //   kind：开关类别，取值见 ContactSwitchKind。
    bool Switch(int kind) const;

    // 设置某个开关的状态。kind 越界则忽略。
    //   kind：开关类别，取值见 ContactSwitchKind；on：true 开 / false 关。
    void SetSwitch(int kind, bool on);

    // ---- 消息提醒 ----

    // 与该联系人单聊会话的消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode。
    // 默认允许消息提醒。
    int NotifyMode() const;

    // 设置消息提醒模式。不做取值校验：模型只负责保存，取值由主面板按模式表给出。
    //   mode：enum conversation_notify_mode 的取值。
    void SetNotifyMode(int mode);

private:
    // 判定 kind 是否是合法的开关下标。
    static bool IsValidSwitch(int kind);

private:
    CString m_name;        // 真名（contact.name）
    CString m_remark;      // 备注名（contact.remark），可空
    CString m_account;     // 账号（contact.account）
    CString m_region;      // 所在地区（contact.region）
    CString m_title;       // 职务（contact.title），可空
    CString m_dept;        // 部门路径（contact.dept），可空
    CString m_signature;   // 个性签名（contact.signature），可空
    int     m_status;      // 在线状态，见 ContactStatus
    bool    m_avatarGray;  // 头像是否画成灰度（对方离线时为 true），默认 false
    int     m_gender;      // 性别编码（enum profile_gender），0 表示未设置
    int     m_birthday;    // 生日 YYYYMMDD，0 表示未填
    CString m_mail;        // 邮箱，可空
    CString m_mobile;      // 手机号，可空
    CString m_landline;    // 座机，可空
    CString m_post;        // 岗位，可空
    int     m_phoneVisible;     // 本人手机号对同事的公开范围（enum profile_visibility），默认仅本人可见；只在看本人时有意义
    int     m_landlineVisible;  // 本人座机对同事的公开范围，默认对同事可见；只在看本人时有意义
    bool    m_self;        // 被查看的是否是当前登录用户本人，默认 false
    CString m_nickname;    // 本人的昵称原值（看本人时「昵称」格显示与编辑的就是它），可空；看别人时不用
    ContactAvatarSource m_avatarSource;   // 头部头像要画什么，由主面板填入；默认按姓名首字加固定底色画

    std::vector<CString> m_tags;    // 标签列表，用户可增删
    std::vector<CString> m_groups;  // 可选分组名（mock 固定列表）
    int     m_selectedGroup;        // 当前选中的分组下标

    bool    m_switches[kContactSwitchCount];   // 各开关状态，下标见 ContactSwitchKind
    int     m_notifyMode;   // 与该联系人单聊会话的消息提醒模式（enum conversation_notify_mode），默认允许提醒
};

// =====================================================================
// 「个人资料」区块的格子数据（2026-10-04）
// =====================================================================
//
// 规则：
//   - 看别人：只列出填了的字段（性别为「未设置」也算没填），全部只读；同事设为不公开的号码服务端
//     不下发，与没填相同、不显示；昵称已在头部显示，不列出；
//   - 看本人：九个字段都列出，没填的显示「未填写」；除手机号外都带编辑按钮；手机号、座机两格另带
//     「仅自己可见 ▾ / 对同事可见 ▾」公开范围开关（2026-10-06）；昵称在第一格（2026-10-07，本人资料
//     对话框删除后改昵称的入口移到这里）。
// 纯逻辑，不碰控件，单元测试见 Tests/ProfileFieldsTests.cpp 的 PFX9、PFX10、PFX23 与
// Tests/ContactWindowSetTests.cpp 的 CWS3、CWS4。

//「个人资料」区块里的字段。枚举值即在区块里的先后顺序：昵称、个性签名各独占一行，其余按两列排
enum ContactProfileField
{
    kProfileFieldNickname  = 0,   //昵称：只在看本人时列出，独占一整行（2026-10-07）
    kProfileFieldSignature = 1,   //个性签名：独占一整行
    kProfileFieldGender    = 2,   //性别：取值见 Protocol.h 的 enum profile_gender，显示为保密 / 男 / 女
    kProfileFieldBirthday  = 3,   //生日：显示为 YYYY-MM-DD
    kProfileFieldAddress   = 4,   //所在地
    kProfileFieldPost      = 5,   //岗位：本人填写的自由文本，与部门职务无关
    kProfileFieldMail      = 6,   //邮箱
    kProfileFieldLandline  = 7,   //座机
    kProfileFieldMobile    = 8,   //手机号：本人也不能修改，没有编辑按钮
};

//字段总数
const int kContactProfileFieldCount = 9;

/**
 *  某个字段是否独占「个人资料」区块的一整行（昵称与个性签名），其余字段按两列排。
 *  @param field 字段，取值见 ContactProfileField
 *  @return 独占一行返回 true。纯函数。
 */
bool IsFullRowProfileField(int field);

//区块里一个要显示的格子
struct ContactProfileCell
{
    int     m_field;      //字段，取值见 ContactProfileField
    CString m_label;      //字段名，已按界面语言翻译
    CString m_value;      //要显示的文字；本人没填的字段为「未填写」
    bool    m_isEmpty;    //字段本身没有填写（只在看本人时出现，界面用弱色显示「未填写」）
    bool    m_editable;   //是否带编辑按钮：只有看本人、且该字段允许本人修改时为 true
    bool    m_hasVisibility;  //是否带公开范围开关：只有看本人时的手机号、座机两格为 true
    int     m_visibility;     //m_hasVisibility 为 true 时是当前的公开范围，取值见 enum profile_visibility

    ContactProfileCell()
        : m_field(kProfileFieldSignature)
        , m_isEmpty(false)
        , m_editable(false)
        , m_hasVisibility(false)
        , m_visibility(profile_visibility_self_only)
    {
    }
};

/**
 *  按资料模型算出「个人资料」区块要显示的格子，顺序即界面上的顺序（ContactProfileField 的顺序）。
 *  @param model 资料模型；看的是不是本人取 model.IsSelf()
 *  @return 要显示的格子；看别人且一个字段都没填时为空，此时整个区块不显示
 */
std::vector<ContactProfileCell> BuildContactProfileCells(const ContactModel& model);

/**
 *  格子排成几行：昵称、个性签名各独占一行，其余每两个一行。
 *  @param cells BuildContactProfileCells 的结果
 *  @return 行数；没有格子时为 0
 */
int ContactProfileRowCount(const std::vector<ContactProfileCell>& cells);

/**
 *  进入就地编辑时输入框里的初始文字：没填的字段为空串（不是「未填写」），生日为 YYYY-MM-DD，
 *  其余为原值。性别用下拉框，不经本函数。
 *  @param model 资料模型
 *  @param field 字段，取值见 ContactProfileField
 *  @return 初始文字
 */
CString ProfileCellEditText(const ContactModel& model, int field);

//就地编辑一格后（回车、失去焦点或下拉框选中）的提交判定结果
enum ProfileCellCommitResult
{
    profile_cell_unchanged = 0,   //与原值相同（或字段不可修改）：直接退出编辑态，不发请求
    profile_cell_submit    = 1,   //有改动且通过本地校验：退出编辑态并提交
    profile_cell_invalid   = 2,   //有改动但没通过本地校验：给出提示后留在编辑态
};

/**
 *  就地编辑一格后的提交判定（纯函数）：按字段取新值（文字去掉首尾空白；生日按 YYYY-MM-DD 解析；
 *  性别取下拉框序号对应的编码），与模型里的原值比较；有改动时组装只含这一项的改资料请求，并做本地校验
 *  （Session/ProfileFields.h 的 ValidateProfileUpdate；昵称清空或超长不合格）。
 *  @param model        资料模型（原值）
 *  @param field        字段，取值见 ContactProfileField；手机号不可修改，恒为 profile_cell_unchanged
 *  @param text         输入框里的文字（性别不用）
 *  @param genderIndex  性别下拉框的序号（只有性别用）
 *  @param outFields    [出参] 结果为 profile_cell_submit 时是要提交的字段
 *  @param outError     [出参] 结果为 profile_cell_invalid 时是不合格的原因，提示文字取 ProfileEditErrorText
 *  @return 判定结果
 */
ProfileCellCommitResult DecideProfileCellCommit(const ContactModel& model, int field, const CString& text,
                                                int genderIndex, net::ProfileUpdateFields& outFields,
                                                profilefields::ProfileEditError& outError);

} // namespace ballooncontact
