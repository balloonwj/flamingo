/**
 *  ProfileModel.h
 *
 *  个人信息对话框的数据模型 —— 持有当前登录用户的姓名 / 账号 / 昵称 /
 *  个性签名 / 性别 / 生日 / 所在地 / 邮箱 / 在线状态。纯数据结构，不涉及
 *  网络协议与持久化（提交回服务器另议，当前仅内存暂存）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （需要 CString）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

namespace balloonprofile {

/**
 *  用户在线状态。枚举值同时用作状态胶囊数组的下标。逐值对应设计稿
 *  components.jsx 的 STATUS_LABEL（不含 offline —— 个人信息对话框里只
 *  允许在"在线 / 离开 / 勿扰 / 隐身"四态间切换）。
 */
enum ProfileStatus
{
    kProfileStatusOnline    = 0,   // 在线：绿点，正常可被联系
    kProfileStatusAway      = 1,   // 离开：橙点，暂时离开座位
    kProfileStatusBusy      = 2,   // 勿扰：红点，不希望被打扰
    kProfileStatusInvisible = 3,   // 隐身：灰点，对外显示为离线
};

// 在线状态的总数（即状态胶囊的个数）。
const int kProfileStatusCount = 4;

/**
 *  ProfileModel —— 个人信息数据模型。
 *
 *  集中持有个人信息对话框展示 / 编辑的所有字段。窗口在"查看 → 编辑"切换
 *  时把模型值灌进文本框；在"保存"时把文本框内容回写本模型。当前默认值
 *  为设计稿里"林知夏"那组 mock 数据，真实数据待接入登录会话后替换。
 */
class ProfileModel
{
public:
    // 构造：所有字段填入设计稿的 mock 数据（林知夏），默认状态为"在线"。
    ProfileModel();

    // ---- 只读字段（不随编辑改变）----------------------------------------

    // 用户姓名。显示在头部区大字号位置，编辑态不可改。
    const CString& Name() const;

    // 用户账号（数字字符串）。显示在姓名下方，编辑态不可改。
    const CString& Account() const;

    // ---- 可编辑字段：读取 ----------------------------------------------

    // 昵称。
    const CString& Nickname() const;

    // 个性签名。
    const CString& Signature() const;

    // 性别（取值为"女" / "男" / "保密"之一）。
    const CString& Gender() const;

    // 生日（形如"1996-08-12"的日期字符串）。
    const CString& Birthday() const;

    // 所在地（城市名）。
    const CString& Region() const;

    // 邮箱地址。
    const CString& Email() const;

    // 当前在线状态，取值见 ProfileStatus。
    int Status() const;

    // ---- 可编辑字段：写入 ----------------------------------------------
    //
    // 以下 setter 仅更新内存值，不触发任何网络 / 持久化动作。窗口在"保存"
    // 时调用它们把编辑态文本框内容收集回模型。

    // 设置昵称。value 为新昵称（拷贝）。
    void SetNickname(const CString& value);

    // 设置个性签名。value 为新签名（拷贝）。
    void SetSignature(const CString& value);

    // 设置性别。value 应为"女" / "男" / "保密"之一；不做合法性校验。
    void SetGender(const CString& value);

    // 设置生日。value 为新生日字符串（拷贝），不做格式校验。
    void SetBirthday(const CString& value);

    // 设置所在地。value 为新城市名（拷贝）。
    void SetRegion(const CString& value);

    // 设置邮箱。value 为新邮箱地址（拷贝），不做格式校验。
    void SetEmail(const CString& value);

    // 设置在线状态。status 越界（不在 ProfileStatus 范围内）时忽略。
    void SetStatus(int status);

private:
    // 判定 status 是否是合法的 ProfileStatus 取值。
    static bool IsValidStatus(int status);

private:
    CString m_strName;        // 姓名，只读
    CString m_strAccount;     // 账号（数字字符串），只读
    CString m_strNickname;    // 昵称，可编辑
    CString m_strSignature;   // 个性签名，可编辑
    CString m_strGender;      // 性别（女 / 男 / 保密），可编辑
    CString m_strBirthday;    // 生日（YYYY-MM-DD），可编辑
    CString m_strRegion;      // 所在地（城市名），可编辑
    CString m_strEmail;       // 邮箱地址，可编辑
    int     m_nStatus;        // 当前在线状态，取值见 ProfileStatus
};

// 某个在线状态的中文显示名（"在线" / "离开" / "勿扰" / "隐身"）。
// status 越界时返回"在线"。
const TCHAR* ProfileStatusText(int status);

// 某个在线状态的圆点颜色（COLORREF）。status 越界时返回"在线"的绿色。
COLORREF ProfileStatusColor(int status);

} // namespace balloonprofile
