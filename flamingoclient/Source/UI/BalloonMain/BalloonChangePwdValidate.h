/**
 *  BalloonChangePwdValidate.h
 *
 *  修改密码对话框的输入校验 —— 纯函数，无 UI / HWND / 业务对象依赖，便于被
 *  FlamingoUnit 测试工程直接调用断言（对话框 CBalloonChangePwdDlg 调它得出
 *  校验结论，再决定提示哪句话 / 是否发请求）。
 *
 *  设计稿：design/修改密码.png（提示"密码至少 8 位"）。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

namespace balloonmain {

//新密码最小长度（设计稿"密码至少 8 位"；"建议含字母/数字/符号"仅为提示文案、不强制）。
const int kChangePwdMinLen = 8;

/**
 *  修改密码输入校验结果。按"从上到下、先发现先返回"的顺序取值。
 */
enum ChangePwdInputResult
{
    kCpwOk = 0,      //校验通过，可发起改密请求
    kCpwEmptyOld,    //当前密码为空
    kCpwEmptyNew,    //新密码为空
    kCpwTooShort,    //新密码不足 kChangePwdMinLen 位
    kCpwMismatch,    //两次输入的新密码不一致
    kCpwSameAsOld,   //新密码与当前密码相同
};

/**
 *  校验修改密码三项输入。nullptr 视同空串。
 *    oldPwd     当前密码明文（宽字符）。
 *    newPwd     新密码明文。
 *    confirmPwd 确认新密码明文。
 *  @return 校验结论，见 ChangePwdInputResult。
 */
ChangePwdInputResult ValidateChangePwdInput(const wchar_t* oldPwd,
                                            const wchar_t* newPwd,
                                            const wchar_t* confirmPwd);

} // namespace balloonmain
