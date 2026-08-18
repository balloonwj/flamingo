/**
 *  GroupAvatarDlgRequest.h
 *
 *  群管理窗 → 主面板 的"打开修改群头像对话框"请求载荷（FMG_MSG_OPEN_GROUP_AVATAR_DLG
 *  的 lParam，堆分配，主面板处理后 delete），与 MemberPickerRequest.h 同例。
 *
 *  为什么要绕主面板一道、而不是群管理窗自己 new 一个对话框：修改头像对话框由主面板
 *  持有（改自己头像时也是它开的），而头像图片上传结果（FMG_MSG_V2_AVATAR_UPLOAD_RESULT）
 *  固定投给主面板再转交那一个实例。若群管理窗另起一个实例，上传完成的回调会落到
 *  主面板那份上，群这边永远等不到结果。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <stdint.h>
#include <string>

namespace balloongroupmanage {

struct GroupAvatarDlgRequest
{
    // 目标群ID（>0）。对话框据它发 cmd_update_group_info，也用它取自动头像底色。
    int64_t     m_groupId;

    // 群名称（UTF-8）。对话框用它取自动头像的首字、以及首字母页的默认值。
    // 传 UTF-8 而不是宽字符：对话框那边解码头像本来就要 UTF-8 的 customface，
    // 两者同一种编码，少一处转换。
    std::string m_groupNameUtf8;

    // 该群当前的头像类型（0 自动 / 1 字形 / 2 自定义图，见 BalloonMain/AvatarSpec.h）。
    int         m_facetype;

    // 该群当前的头像标识（UTF-8，协议原值）：字形头像为 "glyph|colorIdx"，
    // 自定义图为 fileid，自动头像为空串。对话框据它预选当前头像。
    std::string m_customfaceUtf8;

    GroupAvatarDlgRequest()
        : m_groupId(0)
        , m_facetype(0)
    {
    }
};

} // namespace balloongroupmanage
