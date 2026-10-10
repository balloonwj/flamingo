/**
 *  把 v2 登录成功应答写进 CLoginResult 的实现，LoginResultFill.cpp
 *  balloonwj@qq.com   2026-10-04
 */
#include <windows.h>

#include "LoginResultFill.h"
#include "IUProtocolData.h"

namespace net {

void FillLoginResultFromResponse(const LoginResponse& resp, CLoginResult& out)
{
    out.m_uAccountID     = (UINT)resp.userid;
    out.m_strAccountName = resp.profile.username;
    out.m_strNickName    = resp.profile.nickname;
    out.m_nFace          = resp.profile.facetype;
    out.m_nGender        = resp.profile.gender;
    out.m_nBirthday      = resp.profile.birthday;
    out.m_strSignature   = resp.profile.signature;
    out.m_strAddress     = resp.profile.address;
    out.m_strCustomFace  = resp.profile.customface;
    out.m_strPhoneNumber = resp.profile.phonenumber;
    out.m_strMail        = resp.profile.mail;
    out.m_strLandline    = resp.profile.landline;
    out.m_strPost        = resp.profile.post;
    out.m_nPhoneVisible    = resp.profile.phonevisible;
    out.m_nLandlineVisible = resp.profile.landlinevisible;
    //本人所属企业的功能开关（第 3 期第 1 步）：主面板登录成功时交给文件共享宿主
    out.m_nFileShareFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureFileShare) ? 1 : 0;
    //「邀请入群需被邀请人同意」：邀请对话框据此写说明行
    out.m_nInviteConfirmFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureInviteConfirm) ? 1 : 0;
    out.m_nGroupCallFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureGroupCall) ? 1 : 0;
    //语音字幕：主面板登录成功时交给字幕宿主（CCaptionHost::OnLoginSucceeded）
    out.m_nCaptionFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureCaption) ? 1 : 0;
    //云端录制：FlamingoMedia 经 IMediaHost::IsCloudRecordEnabled 读取
    out.m_nCloudRecordFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureCloudRecord) ? 1 : 0;
    //单聊密聊：主面板登录成功时交给密聊宿主（CSecretChatHost::SetFeatureEnabled）
    out.m_nSecretChatFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureSecretChat) ? 1 : 0;
    //帖子群：主面板登录成功时交给帖子群宿主（CThreadGroupHost::SetFeatureEnabled）
    out.m_nThreadGroupFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureThreadGroup) ? 1 : 0;
    //机器人的四项开关：主面板登录成功时交给机器人宿主（CBotHost），群聊窗与设置页据此显隐入口
    out.m_nBotFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureBot) ? 1 : 0;
    out.m_nBotGroupFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureBotGroup) ? 1 : 0;
    out.m_nBotPersonalFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureBotPersonal) ? 1 : 0;
    out.m_nBotGroupExternalFeature = LoginProtocolClient::isFeatureOn(resp.features, kFeatureBotGroupExternal) ? 1 : 0;
}

} // namespace net
