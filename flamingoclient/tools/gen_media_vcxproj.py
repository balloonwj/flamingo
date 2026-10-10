# -*- coding: utf-8 -*-
"""
生成 flamingoclient/FlamingoMedia/FlamingoMedia.vcxproj（带 BOM、LF）与 FlamingoMedia.def（CRLF）。
FlamingoMedia.vcxproj 是生成的，不要手改：源文件清单写在 SOURCES / HEADERS 里，模块目录（MODULE_DIRS：Call、
RemoteDesktop、Share、Record）下的 .h / .cpp 按目录自动收集，所以往这些目录里加文件、或改清单之后都要重新运行本脚本，
再运行同目录的 gen_vcxproj_filters.py 生成 filters。

用法：python flamingoclient/tools/gen_media_vcxproj.py

屏幕共享第 1a 步写成（当时放在 tmp/share_1a/），2026-10-02 第 2c 步移到本目录纳入版本管理。
balloonwj@qq.com   2026-10-02
"""
import glob
import os
import xml.dom.minidom

import version_rc   # 版本信息资源（文件属性里的文件说明、版权），见 version_rc.py

# 演练时设环境变量 FLAMINGO_TRIAL_ROOT 指向 tmp 下的副本根目录（其下有 flamingoclient\）
ROOT = os.path.join(os.environ.get('FLAMINGO_TRIAL_ROOT') or r'E:\flamingo', 'flamingoclient', 'FlamingoMedia')
GUID = '{4F46E4F4-5A27-4D82-9888-25812A9F4B48}'
CONFIGS = [
    # (配置, 平台, 输出目录, 中间目录, 是否 Debug)
    ('Debug', 'Win32', '..\\Debug_Win32\\', 'Debug\\', True),
    ('Release', 'Win32', '..\\Release_Win32\\', 'Release\\', False),
    ('Debug', 'x64', '..\\Debug\\', 'Debug_x64\\', True),
    ('Release', 'x64', '..\\Release\\', 'Release_x64\\', False),
]
HEADERS = ['stdafx.h', 'Api\\FlamingoMediaApi.h', 'Common\\OsVersionPolicy.h', 'Host\\LogFormat.h', 'Host\\MediaLog.h',
           'Host\\MediaModule.h', 'Host\\MediaLockState.h',
           # 与 exe 共用的公共代码（第 1b 步起，两个工程编译同一份源文件）
           '..\\Source\\net\\Protocol.h', '..\\Source\\net\\FrameCodec.h', '..\\Source\\net\\FrameCrypto.h',
           '..\\Source\\net\\FramePayload.h', '..\\Source\\net\\HandshakeRunner.h', '..\\Source\\Common\\Util\\ZlibUtil.h',
           # 第 1c 步：一对一通话用到的共用界面小工具（提示音、线性图标）、路径工具与 MD5（中转自测签令牌）
           '..\\Source\\Common\\Util\\Path.h', '..\\Source\\UI\\BalloonMain\\NotificationSound.h',
           '..\\Source\\UI\\BalloonChatWnd\\ChatIcons.h', '..\\Source\\Common\\Util\\MD5Core.h',
           # Markdown 消息（2026-10-06）：ChatIcons.cpp 把 Markdown 开关与格式条的图标转交 ChatMarkdownIcons.cpp 绘制
           '..\\Source\\UI\\BalloonChatWnd\\ChatMarkdownIcons.h',
           # 第 1d 步：远程桌面会话对话框用到的标题栏程序图标与贴工作区右下角的落点计算
           '..\\Source\\UI\\BalloonMain\\TitleBarIcon.h', '..\\Source\\UI\\BalloonMain\\CornerPopupPlacement.h',
           '..\\Source\\UI\\BalloonMain\\CornerPopupCountdown.h',
           # 多语言第 2 期：CornerPopupCountdown.cpp 的文字包进了 TR，须一并编译 TR 的实现；
           # 本 DLL 不调用 i18n::Init，TR 一律返回原文（DLL 取得当前语言的办法在多语言第 3a 期设计）
           '..\\Source\\Common\\I18n\\I18n.h', '..\\FlamingoLang\\FlamingoLangApi.h',
           # P2P 方案第 6 期：远程桌面经各业务共用的 P2P 链路层走 P2P（P2P 服务由宿主经 IMediaHost::GetP2PService 交来）
           '..\\Source\\Feature\\P2P\\P2PLinkBroker.h', '..\\FlamingoP2P\\FlamingoP2PApi.h',
           # 单聊文件 P2P 直传与 P2P 徽章（2026-10-04）：远程桌面、通话、屏幕共享窗口上的「P2P」徽章
           '..\\Source\\UI\\BalloonMain\\P2PBadge.h',
           # 音视频通话优化（2026-10-05）：来电窗与通话画面的头像，与主面板联系人列表同一份画法（文件共享 DLL 也直接编译这几份）
           'Common\\MediaAvatar.h', '..\\Source\\UI\\BalloonMain\\MainPanelAvatar.h',
           '..\\Source\\UI\\BalloonMain\\AvatarGlyph.h', '..\\Source\\UI\\BalloonMain\\MainPanelIcons.h',
           '..\\Source\\UI\\BalloonMain\\PresenceBadge.h', '..\\Source\\UI\\BalloonMain\\MainPanelStatusMenu.h',
           '..\\Source\\UI\\BalloonMain\\AvatarImageUtil.h', '..\\Source\\UI\\BalloonMain\\AvatarBitmapCache.h',
           # 音视频通话优化（2026-10-05）：两个通话窗共用的图钉固定、两个来电窗共用的版式
           'Common\\WindowPin.h', 'Common\\IncomingCallLayout.h',
           # 聊天字幕模式（2026-10-05）：DirectComposition 显示层由 Share\ShareCompositionLayer 挪到 Source，exe 与本 DLL 共用
           '..\\Source\\Common\\Graphics\\CompositionLayer.h',
           # 共享与录制的轻量提示（2026-10-07）：分享链接复制 / 重置 / 停用、共享与录制互斥被拦下时的提示
           'Common\\MediaToast.h']
SOURCES = ['DllMain.cpp', 'Common\\OsVersionPolicy.cpp', 'Host\\IULogBridge.cpp', 'Host\\LogFormat.cpp',
           'Host\\MediaLog.cpp', 'Host\\MediaModule.cpp', 'Host\\MediaLockState.cpp',
           '..\\Source\\net\\FrameCodec.cpp', '..\\Source\\net\\FrameCrypto.cpp', '..\\Source\\net\\FramePayload.cpp',
           '..\\Source\\net\\HandshakeRunner.cpp', '..\\Source\\Common\\Util\\ZlibUtil.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp', '..\\Source\\third-party\\jsoncpp-1.9.0\\json_value.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_writer.cpp',
           '..\\Source\\Common\\Util\\Path.cpp', '..\\Source\\UI\\BalloonMain\\NotificationSound.cpp',
           '..\\Source\\UI\\BalloonMain\\NotificationSoundPlayer.cpp', '..\\Source\\UI\\BalloonChatWnd\\ChatIcons.cpp',
           '..\\Source\\UI\\BalloonChatWnd\\ChatMarkdownIcons.cpp',
           '..\\Source\\Common\\Util\\MD5Sum.cpp',
           '..\\Source\\UI\\BalloonMain\\TitleBarIcon.cpp', '..\\Source\\UI\\BalloonMain\\CornerPopupPlacement.cpp',
           '..\\Source\\UI\\BalloonMain\\CornerPopupCountdown.cpp', '..\\Source\\Common\\I18n\\I18n.cpp',
           '..\\Source\\Feature\\P2P\\P2PLinkBroker.cpp',
           '..\\Source\\UI\\BalloonMain\\P2PBadge.cpp',
           # 音视频通话优化（2026-10-05）：头像
           'Common\\MediaAvatar.cpp', '..\\Source\\UI\\BalloonMain\\MainPanelAvatar.cpp',
           '..\\Source\\UI\\BalloonMain\\AvatarGlyph.cpp', '..\\Source\\UI\\BalloonMain\\MainPanelIcons.cpp',
           '..\\Source\\UI\\BalloonMain\\PresenceBadge.cpp', '..\\Source\\UI\\BalloonMain\\MainPanelStatusMenu.cpp',
           '..\\Source\\UI\\BalloonMain\\AvatarImageUtil.cpp', '..\\Source\\UI\\BalloonMain\\AvatarBitmapCache.cpp',
           'Common\\WindowPin.cpp', 'Common\\IncomingCallLayout.cpp',
           '..\\Source\\Common\\Graphics\\CompositionLayer.cpp',
           # 共享与录制的轻量提示（2026-10-07）
           'Common\\MediaToast.cpp']
SOURCES += ['..\\Source\\third-party\\zlib1.2.11\\%s.c' % n for n in
            ('adler32', 'compress', 'crc32', 'deflate', 'gzclose', 'gzlib', 'gzread', 'gzwrite', 'infback',
             'inffast', 'inflate', 'inftrees', 'trees', 'uncompr', 'zutil')]
# 模块目录：目录下的 .h / .cpp 全部编进本工程（第 1c 步：一对一通话；第 1d 步：远程桌面；第 2c 步：屏幕共享；
# 录屏；语音消息的录音与播放，见 docs/语音消息方案.md；语音字幕与 FlamingoCaption.dll 的衔接，见 docs/语音字幕方案.md）
MODULE_DIRS = ['Call', 'RemoteDesktop', 'Share', 'Record', 'Voice', 'GroupCall', 'Caption']
for _d in MODULE_DIRS:
    HEADERS += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.h')))
    SOURCES += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.cpp')))
# 个别文件的额外编译设置：jsoncpp 的 json_reader.cpp 有一处 double→int 的隐式转换告警（C4244），
# 与 exe、测试工程的处理一致，只在该文件上屏蔽
# zlib 的 gzread.c / gzwrite.c 在 x64 下有 size_t→unsigned 的告警（C4267），同样与 exe 工程一致地按文件屏蔽
SOURCE_EXTRA = {
    '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp':
        ['      <DisableSpecificWarnings>4244;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
    '..\\Source\\third-party\\zlib1.2.11\\gzread.c':
        ['      <DisableSpecificWarnings>4267;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
    '..\\Source\\third-party\\zlib1.2.11\\gzwrite.c':
        ['      <DisableSpecificWarnings>4267;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
}
# 包含目录：本目录（与 exe 共用的源文件里的 #include "stdafx.h" 据此找到本工程的 stdafx.h）、WTL、libsodium，
# 以及第 1c 步迁入通话后用到的 balloonui、opus、speexdsp（与 exe 工程的包含目录一致）
INCLUDE_DIRS = ('$(ProjectDir);..\\Source\\third-party\\wtl10.1;..\\..\\third-party\\libsodium-1.0.22-RELEASE\\src\\libsodium\\include;'
                '..\\..\\third-party\\balloonui\\balloonui;..\\..\\third-party\\opus-1.5.2\\include;'
                '..\\..\\third-party\\speexdsp-1.2.1\\include')
# 链接的系统库：libsodium 之外与 exe 工程一致（去掉 exe 才用的 odbc，以及 exe 目录自带、DLL 用不到的 RichEd20.Lib）；Media Foundation、winmm 等由源文件里的
# #pragma comment(lib) 自行引入。wininet.lib 是第 1d 步远程桌面的 until.cpp（http_get）要用的
LINK_LIBS = 'libsodium.lib;version.lib;IMM32.lib;ws2_32.lib;gdiplus.lib;comctl32.lib;wininet.lib'
# 引用的静态库工程（与 exe 工程相同）：balloonui 界面库、opus 编解码、speexdsp 回声消除与重采样
PROJECT_REFS = [
    ('..\\..\\third-party\\balloonui\\balloonui\\balloonui_static.vcxproj', '{e5f9b2a1-6c84-4de3-9c75-3b8a4d2e7f60}'),
    ('..\\..\\third-party\\opus-1.5.2\\opus_static.vcxproj', '{b1e2c3d4-0a1b-4c2d-9e3f-0a1b2c3d4e5f}'),
    ('..\\..\\third-party\\speexdsp-1.2.1\\speexdsp_static.vcxproj', '{c2d3e4f5-1b2c-4d3e-af40-1b2c3d4e5f60}'),
]
# libsodium 静态库目录，布局与 exe 相同：bin\<平台>\<配置>\v143\static
SODIUM_LIB_DIR = '..\\..\\third-party\\libsodium-1.0.22-RELEASE\\bin\\$(Platform)\\$(Configuration)\\v143\\static'

HEADER_COMMENT = '''<!--
  FlamingoMedia.vcxproj — 客户端音视频与屏幕类功能的 DLL 工程
  balloonwj@qq.com   2026-10-02

  产出 FlamingoMedia.dll，由 Flamingo.exe 在启动时用 LoadLibrary 动态加载（不链接导入库）。
  接口约定见 Api/FlamingoMediaApi.h，方案见 docs/屏幕共享与录制方案.md。

  关键约束：
   · CharacterSet=Unicode、CRT 静态（Debug /MTd、Release /MT），与 Flamingo.exe 及
     balloonui_static 一致。exe 与 DLL 因此各有独立的堆，接口只用纯虚类与基本类型。
   · 四个配置的 OutDir 与 Flamingo.exe 相同：Debug|x64 输出到资源齐全的 ..\\Debug\\，
     其余三个分别输出到 ..\\Release\\、..\\Debug_Win32\\、..\\Release_Win32\\。
   · 导出名由 FlamingoMedia.def 固定；导入库与 .exp 放进中间目录，不污染输出目录。
   · 所有配置都带 /utf-8：源文件为 UTF-8 无 BOM。
   · 与 exe 共用的公共代码（Source/net 的帧编解码与加密握手、ZlibUtil、zlib、jsoncpp）直接列在
     本工程里，与 exe 编译同一份源文件（2026-10-02 与用户确认）。这些文件里的 #include "stdafx.h"
     借助包含路径里的 $(ProjectDir) 找到本工程的 stdafx.h。
   · 日志：DLL 不编译 exe 的 IULog.cpp，由 Host/IULogBridge.cpp 实现 CIULog::Log，经宿主写日志。
   · 第 1c 步迁入一对一通话（Call\\ 目录，含通话窗），与 exe 一样静态链接 balloonui_static、
     opus_static、speexdsp_static；DLL 有自己的一份 _Module（见 DllMain.cpp）。
   · 第 1d 步迁入远程桌面（RemoteDesktop\\ 目录，含会话对话框、观看窗、受控提示条）。图标资源仍在 exe 里，
     DLL 内的代码一律用 GetModuleHandle(NULL) 从 exe 模块加载。
-->'''


def cond(c, p):
    return "'$(Configuration)|$(Platform)'=='%s|%s'" % (c, p)


def main():
    out = ['<?xml version="1.0" encoding="utf-8"?>', HEADER_COMMENT,
           '<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">',
           '  <ItemGroup Label="ProjectConfigurations">']
    for c, p, o, i, d in CONFIGS:
        out += ['    <ProjectConfiguration Include="%s|%s">' % (c, p),
                '      <Configuration>%s</Configuration>' % c,
                '      <Platform>%s</Platform>' % p,
                '    </ProjectConfiguration>']
    out += ['  </ItemGroup>',
            '  <PropertyGroup Label="Globals">',
            '    <ProjectGuid>%s</ProjectGuid>' % GUID,
            '    <RootNamespace>FlamingoMedia</RootNamespace>',
            '    <ProjectName>FlamingoMedia</ProjectName>',
            '    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>',
            '  </PropertyGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />']
    for c, p, o, i, d in CONFIGS:
        out += ['  <PropertyGroup Condition="%s" Label="Configuration">' % cond(c, p),
                '    <ConfigurationType>DynamicLibrary</ConfigurationType>',
                '    <PlatformToolset>v143</PlatformToolset>',
                '    <CharacterSet>Unicode</CharacterSet>',
                '    <UseDebugLibraries>%s</UseDebugLibraries>' % ('true' if d else 'false'),
                '  </PropertyGroup>']
    out += ['  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />',
            '  <ImportGroup Label="ExtensionSettings"></ImportGroup>']
    for c, p, o, i, d in CONFIGS:
        out += ['  <ImportGroup Condition="%s" Label="PropertySheets">' % cond(c, p),
                '    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" '
                'Condition="exists(\'$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\')" Label="LocalAppDataPlatform" />',
                '  </ImportGroup>']
    out.append('  <PropertyGroup Label="UserMacros" />')
    for c, p, o, i, d in CONFIGS:
        out += ['  <PropertyGroup Condition="%s">' % cond(c, p),
                '    <OutDir>%s</OutDir>' % o,
                '    <IntDir>%s</IntDir>' % i,
                '    <TargetName>FlamingoMedia</TargetName>',
                '    <LinkIncremental>%s</LinkIncremental>' % ('true' if d else 'false'),
                '  </PropertyGroup>']
    for c, p, o, i, d in CONFIGS:
        out += ['  <ItemDefinitionGroup Condition="%s">' % cond(c, p),
                '    <ClCompile>',
                '      <Optimization>%s</Optimization>' % ('Disabled' if d else 'MaxSpeed'),
                '      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>',
                '      <AdditionalIncludeDirectories>%s;%%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>' % INCLUDE_DIRS,
                '      <PreprocessorDefinitions>SODIUM_STATIC;WIN32;_WINDOWS;_USRDLL;%s;_CRT_SECURE_NO_WARNINGS;'
                '_WINSOCK_DEPRECATED_NO_WARNINGS;%%(PreprocessorDefinitions)</PreprocessorDefinitions>'
                % ('_DEBUG' if d else 'NDEBUG'),
                '      <RuntimeLibrary>%s</RuntimeLibrary>' % ('MultiThreadedDebug' if d else 'MultiThreaded'),
                '      <PrecompiledHeader>NotUsing</PrecompiledHeader>',
                '      <WarningLevel>Level3</WarningLevel>',
                '      <SDLCheck>true</SDLCheck>',
                '      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>',
                '    </ClCompile>',
                '    <Link>',
                '      <SubSystem>Windows</SubSystem>',
                '      <GenerateDebugInformation>true</GenerateDebugInformation>',
                '      <ModuleDefinitionFile>FlamingoMedia.def</ModuleDefinitionFile>',
                '      <ImportLibrary>$(IntDir)$(TargetName).lib</ImportLibrary>',
                '      <AdditionalLibraryDirectories>%s;%%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>' % SODIUM_LIB_DIR,
                '      <AdditionalDependencies>%s;%%(AdditionalDependencies)</AdditionalDependencies>' % LINK_LIBS]
        if not d:
            out += ['      <EnableCOMDATFolding>true</EnableCOMDATFolding>',
                    '      <OptimizeReferences>true</OptimizeReferences>']
        out += ['    </Link>', '  </ItemDefinitionGroup>']
    out.append('  <ItemGroup>')
    out += ['    <ClInclude Include="%s" />' % h for h in HEADERS]
    out += ['  </ItemGroup>', '  <ItemGroup>']
    for s in SOURCES:
        if s in SOURCE_EXTRA:
            out += ['    <ClCompile Include="%s">' % s] + SOURCE_EXTRA[s] + ['    </ClCompile>']
        else:
            out.append('    <ClCompile Include="%s" />' % s)
    out += ['  </ItemGroup>', '  <ItemGroup>']
    for path, guid in PROJECT_REFS:
        out += ['    <ProjectReference Include="%s">' % path, '      <Project>%s</Project>' % guid, '    </ProjectReference>']
    out += ['  </ItemGroup>', '  <ItemGroup>', '    <None Include="FlamingoMedia.def" />', '  </ItemGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '  <ImportGroup Label="ExtensionTargets"></ImportGroup>', '</Project>']
    out = version_rc.insert_items(out, 'FlamingoMedia')
    path = os.path.join(ROOT, 'FlamingoMedia.vcxproj')
    open(path, 'wb').write(b'\xef\xbb\xbf' + ('\n'.join(out) + '\n').encode('utf-8'))
    xml.dom.minidom.parse(path)
    version_rc.sync_rc('FlamingoMedia')

    deflines = ['; FlamingoMedia.dll 的导出表：固定导出名，不受调用约定与名称修饰影响（32 位与 64 位相同）。',
                '; balloonwj@qq.com   2026-10-02',
                'LIBRARY FlamingoMedia',
                'EXPORTS',
                '    FlamingoMedia_Create']
    open(os.path.join(ROOT, 'FlamingoMedia.def'), 'wb').write(('\r\n'.join(deflines) + '\r\n').encode('utf-8'))
    print('ok')


if __name__ == '__main__':
    main()
