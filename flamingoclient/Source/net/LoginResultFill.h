/**
 *  把 v2 登录成功应答写进 CLoginResult。重连线程（Threading/SendMsgThread.cpp）与登录窗
 *  （UI/BalloonAuth/BalloonLoginDlg.cpp）两条登录路径原先各写一份逐字段拷贝，2026-10-04 收成
 *  本文件的 FillLoginResultFromResponse，两边调用同一份。纯数据转换，不触网络与界面，
 *  单元测试见 Tests/ProfileFieldsTests.cpp 的 PFX3。
 *
 *  典型用法（登录线程里，应答解析成功且 code 为 0 时）：
 *      pLoginResult->m_LoginResultCode = LOGIN_SUCCESS;
 *      net::FillLoginResultFromResponse(resp, *pLoginResult);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "LoginProtocolClient.h"

class CLoginResult;

namespace net {

/**
 *  把登录成功应答里的 userid、账号、本人资料与功能开关写进登录结果。
 *  结果码 m_LoginResultCode 与「系统提示里的本人」（SetSystemTipSelfUserId）不在此列，由调用方设置。
 *  @param resp 已解析、code 为 0 的登录应答
 *  @param out  [出参] 登录结果；由调用方创建并持有
 */
void FillLoginResultFromResponse(const LoginResponse& resp, CLoginResult& out);

} // namespace net
