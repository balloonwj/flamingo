/**
 *  SensitiveWordTips.h
 *
 *  主面板一侧几处「保存 / 提交被服务端拒绝」的提示文案（docs/敏感词方案.md 第 8.6 节）。服务端按发送者所属
 *  企业的敏感词库检查资料、群资料与群名片，命中时分别以 109（资料段）、312（群段）拒绝；这里按错误码给出
 *  专门的说法，其余错误码沿用原来的通用说法。
 *
 *  纯函数，不碰窗口，便于单元测试（Tests/SensitiveSendFailTests.cpp 的 SF-C9）。
 *
 *  典型用法：
 *      ::MessageBox(m_hWnd, balloonmain::ProfileSaveFailureText(resp.code), g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <atlstr.h>

namespace balloonmain {

/**
 *  个人资料对话框保存失败的提示。
 *  @param code 应答错误码；109（error_code_profile_sensitive_word）为昵称、签名等包含敏感词
 *  @return 带句号的整句
 */
CString ProfileSaveFailureText(int code);

/**
 *  主面板头部原地修改个性签名失败的提示。
 *  @param code 应答错误码；109 为签名包含敏感词
 *  @return 带句号的整句
 */
CString SignatureSaveFailureText(int code);

/**
 *  建群失败的提示。
 *  @param code 应答错误码；312（error_code_group_sensitive_word）为群名称包含敏感词
 *  @return 带句号的整句；其余错误码带上错误码便于排查
 */
CString CreateGroupFailureText(int code);

/**
 *  群资料对话框修改「我在本群的昵称」（群名片）失败的提示。
 *  @param code      应答错误码；312 为群名片包含敏感词
 *  @param serverMsg 服务端应答里的 msg（转成宽字符），可为空；没有专门说法时附在提示后面
 *  @return 整句
 */
CString GroupCardSaveFailureText(int code, const CString& serverMsg);

} // namespace balloonmain
