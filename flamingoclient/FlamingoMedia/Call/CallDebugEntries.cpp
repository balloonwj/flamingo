/**
 *  一对一通话命令行调试入口的实现，见 CallDebugEntries.h。各入口的说明照搬自迁移前的
 *  exe App/DebugEntryPoints.cpp。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "CallDebugEntries.h"

#include <wchar.h>
#include <string>
#include "CallRelaySelfTest.h"     //--callrelayselftest
#include "AudioLoopback.h"         //--callaudioloop
#include "CameraPreviewDebug.h"    //--camerapreview
#include "CameraDisplayTest.h"     //--cameratest
#include "H264SelfTest.h"          //--h264selftest
#include "VideoLoopbackDebug.h"    //--videoloop
#include "CallVideoUiDebug.h"      //--callvideoui

namespace call {

#ifdef _DEBUG
//中转自测连接的 imgserver 地址与端口（本机开发服务端的中转端口），以及签发令牌用的密钥
//（须与 imgserver.conf 的 relaytokenkey 一致）
static const char* kSelfTestRelayIp   = "127.0.0.1";
static const int   kSelfTestRelayPort = 20003;
static const char* kSelfTestTokenKey  = "flamingo-relay-token-secret";

//命令行里是否带某个开关
static bool HasSwitch(const wchar_t* commandLine, const wchar_t* sw)
{
    return ::wcsstr(commandLine, sw) != NULL;
}
#endif

int RunCallDebugEntry(const wchar_t* commandLine, HWND hwndHost, flamingomedia::IMediaHost* host)
{
#ifdef _DEBUG
    if (commandLine == NULL)
    {
        return flamingomedia::media_debug_not_handled;
    }
    //--callrelayselftest：进程内开两条中转连接（主叫 / 被叫，同 sessionid 相反 role）连 imgserver 中转端口，
    //双向互发测试帧验证「连接 → 握手 → 透传」整条通话中转通路。需 imgserver 在跑、relaytokenkey 一致。
    if (HasSwitch(commandLine, L"--callrelayselftest"))
    {
        bool selfTestOk = false;
        const unsigned char* serverPubKey = (host != NULL) ? host->GetServerPublicKey() : NULL;
        std::string report = RunCallRelaySelfTest(kSelfTestRelayIp, kSelfTestRelayPort, kSelfTestTokenKey,
                                                  serverPubKey, selfTestOk);
        CString text(CA2W(report.c_str(), CP_UTF8));
        ::MessageBox(hwndHost, text, _T("通话中转自环自测"), MB_OK | (selfTestOk ? MB_ICONINFORMATION : MB_ICONWARNING));
        return flamingomedia::media_debug_handled;
    }
    //--callaudioloop：本地音频回环（WASAPI 采集 → Opus 编码 → 解码 → 播放），弹框运行期间持续，点确定停止
    if (HasSwitch(commandLine, L"--callaudioloop"))
    {
        RunAudioLoopbackDebug();
        return flamingomedia::media_debug_handled;
    }
    //--camerapreview：打开默认摄像头，把采集画面实时显示在预览窗口里，关窗即停
    if (HasSwitch(commandLine, L"--camerapreview"))
    {
        RunCameraPreviewDebug();
        return flamingomedia::media_debug_handled;
    }
    //--cameratest：自包含的摄像头显示测试，逐步打日志定位「画面显示不出来」卡在哪一步
    if (HasSwitch(commandLine, L"--cameratest"))
    {
        RunCameraDisplayTest();
        return flamingomedia::media_debug_handled;
    }
    //--h264selftest：合成 NV12 → MF H.264 编码 → 解码 → 首帧落 BMP，不需要摄像头
    if (HasSwitch(commandLine, L"--h264selftest"))
    {
        RunH264SelfTest();
        return flamingomedia::media_debug_handled;
    }
    //--videoloop：摄像头采集 → H.264 编码 → 回灌解码 → 首个解码帧落 BMP，验证整条视频管线
    if (HasSwitch(commandLine, L"--videoloop"))
    {
        RunVideoLoopbackDebug();
        return flamingomedia::media_debug_handled;
    }
    //--callvideoui：以「视频通话中」形态打开通话窗，用本机摄像头回环填充画面，关窗即停
    if (HasSwitch(commandLine, L"--callvideoui"))
    {
        RunCallVideoUiDebug();
        return flamingomedia::media_debug_handled;
    }
    return flamingomedia::media_debug_not_handled;
#else
    (void)commandLine;
    (void)hwndHost;
    (void)host;
    return flamingomedia::media_debug_not_handled;
#endif
}

} // namespace call
