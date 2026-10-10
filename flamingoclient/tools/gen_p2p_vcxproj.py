# -*- coding: utf-8 -*-
"""
生成 flamingoclient/FlamingoP2P/FlamingoP2P.vcxproj（带 BOM、LF）与 FlamingoP2P.def（CRLF）。
FlamingoP2P.vcxproj 是生成的，不要手改：源文件清单写在 SOURCES / HEADERS 里，模块目录（MODULE_DIRS）下的
.h / .cpp / .c 按目录自动收集，所以往这些目录里加文件、或改清单之后都要重新运行本脚本，
再运行同目录的 gen_vcxproj_filters.py 生成 filters。

做法照 gen_media_vcxproj.py（FlamingoMedia 工程的生成脚本）：四种配置、静态 CRT、输出目录与 Flamingo.exe 相同。
与它的区别：P2P DLL 没有界面，不引用 balloonui、opus、speexdsp，也不需要 WTL。

用法：python flamingoclient/tools/gen_p2p_vcxproj.py

P2P 方案（docs/P2P通信方案.md）第 1 期第 1b 步写成。
balloonwj@qq.com   2026-10-03
"""
import glob
import os
import xml.dom.minidom

import version_rc   # 版本信息资源（文件属性里的文件说明、版权），见 version_rc.py

# 演练时设环境变量 FLAMINGO_TRIAL_ROOT 指向 tmp 下的副本根目录（其下有 flamingoclient\）
ROOT = os.path.join(os.environ.get('FLAMINGO_TRIAL_ROOT') or r'E:\flamingo', 'flamingoclient', 'FlamingoP2P')
GUID = '{4AE31503-AD37-47B9-960E-069E10166093}'
CONFIGS = [
    # (配置, 平台, 输出目录, 中间目录, 是否 Debug)
    ('Debug', 'Win32', '..\\Debug_Win32\\', 'Debug\\', True),
    ('Release', 'Win32', '..\\Release_Win32\\', 'Release\\', False),
    ('Debug', 'x64', '..\\Debug\\', 'Debug_x64\\', True),
    ('Release', 'x64', '..\\Release\\', 'Release_x64\\', False),
]
HEADERS = ['stdafx.h', 'FlamingoP2PApi.h',
           # 与别的工程共用的文件：客户端日志接口（DLL 内由 Host\HostLogBridge.cpp 实现）、FlamingoMedia 的日志格式化
           '..\\Source\\Common\\Log\\IULog.h', '..\\FlamingoMedia\\Host\\LogFormat.h',
           # 带本项目修正的 KCP（第 4 期 4b 起，说明见 third-party\\kcp\\README_flamingo.md）
           'third-party\\kcp\\ikcp.h']
SOURCES = ['DllMain.cpp', '..\\FlamingoMedia\\Host\\LogFormat.cpp',
           'third-party\\kcp\\ikcp.c',
           # jsoncpp：解析 2302、编解码 2303 的候选地址（第 3 期 3b 起），与 exe 用同一份源码
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_value.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_writer.cpp',
           # 回落中转（第 3 期 3e 起）：v2 帧编解码、协议加密握手与帧体加解密，与 exe、FlamingoFileShare 用同一份源码；
           # FramePayload 带压缩功能，要连同 ZlibUtil 与 zlib 一起编入（中转帧本身不压缩）
           '..\\Source\\net\\FrameCodec.cpp', '..\\Source\\net\\FrameCrypto.cpp',
           '..\\Source\\net\\FramePayload.cpp', '..\\Source\\net\\HandshakeRunner.cpp',
           '..\\Source\\Common\\Util\\ZlibUtil.cpp'] + \
          ['..\\Source\\third-party\\zlib1.2.11\\%s' % _z for _z in
           ['adler32.c', 'compress.c', 'crc32.c', 'deflate.c', 'gzclose.c', 'gzlib.c', 'gzread.c', 'gzwrite.c',
            'infback.c', 'inffast.c', 'inflate.c', 'inftrees.c', 'trees.c', 'uncompr.c', 'zutil.c']]
# 模块目录：目录下的 .h / .cpp / .c 全部编进本工程。Host：模块对象、日志出口与日志桥、P2P 服务；
# Core：链路报文、端到端加密、重放窗口（第 3 期 3a 起）；Ice：候选地址、地址探测客户端（3b 起）、
# 连通性检查（3c 起）；Link：信令编解码与一个 P2P 会话的状态机（3d 起）；Io：UDP socket、网卡枚举与 IO 线程（3d 起）；
# Chan：通道复用、数据报分片（4a 起）
MODULE_DIRS = ['Host', 'Core', 'Ice', 'Link', 'Io', 'Chan']
for _d in MODULE_DIRS:
    HEADERS += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.h')))
    SOURCES += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in
                      glob.glob(os.path.join(ROOT, _d, '*.cpp')) + glob.glob(os.path.join(ROOT, _d, '*.c')))
# 个别文件的额外编译设置：json_reader.cpp 有 size_t 到 int 的窄化，与 exe、FlamingoFileShare 一样屏蔽 C4244
SOURCE_EXTRA = {
    '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp':
        ['      <DisableSpecificWarnings>4244;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
    # zlib 的 gzread.c / gzwrite.c 有 size_t 到 unsigned 的窄化，与 FlamingoFileShare 一样屏蔽 C4267
    '..\\Source\\third-party\\zlib1.2.11\\gzread.c':
        ['      <DisableSpecificWarnings>4267;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
    '..\\Source\\third-party\\zlib1.2.11\\gzwrite.c':
        ['      <DisableSpecificWarnings>4267;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
}
# 包含目录：本目录（以后编入的共用源文件里的 #include "stdafx.h" 据此找到本工程的 stdafx.h）、libsodium
INCLUDE_DIRS = '$(ProjectDir);..\\..\\third-party\\libsodium-1.0.22-RELEASE\\src\\libsodium\\include'
# 链接的系统库：libsodium（端到端加密），ws2_32（UDP 与中转的 socket），iphlpapi（枚举网卡地址，第 3b 步起用到）
LINK_LIBS = 'libsodium.lib;ws2_32.lib;iphlpapi.lib'
# libsodium 静态库目录，布局与 exe 相同：bin\<平台>\<配置>\v143\static
SODIUM_LIB_DIR = '..\\..\\third-party\\libsodium-1.0.22-RELEASE\\bin\\$(Platform)\\$(Configuration)\\v143\\static'

HEADER_COMMENT = '''<!--
  FlamingoP2P.vcxproj — 客户端 P2P 通信的 DLL 工程（由 flamingoclient/tools/gen_p2p_vcxproj.py 生成，不要手改）
  balloonwj@qq.com   2026-10-03

  产出 FlamingoP2P.dll，由 Flamingo.exe 在启动时用 LoadLibrary 动态加载（不链接导入库）。
  接口约定见 FlamingoP2PApi.h，方案见 docs/P2P通信方案.md。

  关键约束：
   · CharacterSet=Unicode、CRT 静态（Debug /MTd、Release /MT），与 Flamingo.exe 及另外两个 DLL 一致。
     exe 与 DLL 因此各有独立的堆，接口只用纯虚类与基本类型。
   · 四种配置都定义 WIN32（仓库代码判断的是它而不是 _WIN32）与 SODIUM_STATIC（libsodium 静态链接），
     由单元测试 P2PG2 守住。
   · 四种配置的 OutDir 与 Flamingo.exe 相同：Debug|x64 输出到资源齐全的 ..\\Debug\\，其余三个分别输出到 ..\\Release\\、..\\Debug_Win32\\、..\\Release_Win32\\。
   · 导出名由 FlamingoP2P.def 固定；导入库与 .exp 放进中间目录，不污染输出目录。
   · 所有配置都带 /utf-8：源文件为 UTF-8 无 BOM。
   · 日志：DLL 不编译 exe 的 IULog.cpp，由 Host\\HostLogBridge.cpp 实现 CIULog::Log，经宿主写日志。
   · 没有界面：不引用 balloonui，也不需要 WTL；DllMain 里没有 ATL 模块对象。
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
            '    <RootNamespace>FlamingoP2P</RootNamespace>',
            '    <ProjectName>FlamingoP2P</ProjectName>',
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
                '    <TargetName>FlamingoP2P</TargetName>',
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
                '      <ModuleDefinitionFile>FlamingoP2P.def</ModuleDefinitionFile>',
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
    out += ['  </ItemGroup>', '  <ItemGroup>', '    <None Include="FlamingoP2P.def" />', '  </ItemGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '  <ImportGroup Label="ExtensionTargets"></ImportGroup>', '</Project>']
    out = version_rc.insert_items(out, 'FlamingoP2P')
    path = os.path.join(ROOT, 'FlamingoP2P.vcxproj')
    open(path, 'wb').write(b'\xef\xbb\xbf' + ('\n'.join(out) + '\n').encode('utf-8'))
    xml.dom.minidom.parse(path)
    version_rc.sync_rc('FlamingoP2P')

    deflines = ['; FlamingoP2P.dll 的导出表：固定导出名，不受调用约定与名称修饰影响（32 位与 64 位相同）。',
                '; balloonwj@qq.com   2026-10-03',
                'LIBRARY FlamingoP2P',
                'EXPORTS',
                '    FlamingoP2P_Create']
    open(os.path.join(ROOT, 'FlamingoP2P.def'), 'wb').write(('\r\n'.join(deflines) + '\r\n').encode('utf-8'))
    print('ok: %d headers, %d sources' % (len(HEADERS), len(SOURCES)))


if __name__ == '__main__':
    main()
