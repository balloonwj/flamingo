# -*- coding: utf-8 -*-
"""
生成 flamingoclient/FlamingoSecretChat/FlamingoSecretChat.vcxproj（带 BOM、LF）与 FlamingoSecretChat.def（CRLF）。
FlamingoSecretChat.vcxproj 是生成的，不要手改：模块目录（MODULE_DIRS）下的 .h / .cpp 按目录自动收集，
往这些目录里加文件之后要重新运行本脚本，再运行同目录的 gen_vcxproj_filters.py 生成 filters。

做法照 gen_p2p_vcxproj.py：四种配置、静态 CRT、输出目录与 Flamingo.exe 相同，定义 WIN32 与 SODIUM_STATIC。
本 DLL 没有界面：不引用 balloonui，也不需要 WTL。

用法：python flamingoclient/tools/gen_secretchat_vcxproj.py [--check]
      --check 只比较、不写文件，工程与脚本不一致时返回 1（单元测试 SCG1 用它守住）

单聊密聊方案（docs/密聊方案.md）第 2a 步写成。
balloonwj@qq.com   2026-10-07
"""
import glob
import os
import sys
import xml.dom.minidom

import version_rc   # 版本信息资源（文件属性里的文件说明、版权），见 version_rc.py

ROOT = os.path.join(os.environ.get('FLAMINGO_TRIAL_ROOT') or os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')), 'flamingoclient', 'FlamingoSecretChat')
GUID = '{2D760406-E738-4AF9-917B-6D6CCD4FB55F}'
CONFIGS = [
    # (配置, 平台, 输出目录, 中间目录, 是否 Debug)
    ('Debug', 'Win32', '..\\Debug_Win32\\', 'Debug\\', True),
    ('Release', 'Win32', '..\\Release_Win32\\', 'Release\\', False),
    ('Debug', 'x64', '..\\Debug\\', 'Debug_x64\\', True),
    ('Release', 'x64', '..\\Release\\', 'Release_x64\\', False),
]
HEADERS = ['stdafx.h', 'SecretChatApi.h',
           # 与别的工程共用的文件：客户端日志接口（DLL 内由 Host\HostLogBridge.cpp 实现）、FlamingoMedia 的日志格式化、
           # 客户端协议头（cmd_secret_* 与错误码）
           '..\\Source\\Common\\Log\\IULog.h', '..\\FlamingoMedia\\Host\\LogFormat.h', '..\\Source\\net\\Protocol.h']
SOURCES = ['DllMain.cpp', '..\\FlamingoMedia\\Host\\LogFormat.cpp',
           # jsoncpp：编解码 2600 段报文与明文信封，与 exe 用同一份源码
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_value.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_writer.cpp']
# 模块目录：Host 为模块对象、日志出口与日志桥；Core 为加密、信封与图片分块、报文编解码、防截图
MODULE_DIRS = ['Host', 'Core']
for _d in MODULE_DIRS:
    HEADERS += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.h')))
    SOURCES += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.cpp')))
# json_reader.cpp 有 size_t 到 int 的窄化，与 exe、FlamingoP2P 一样屏蔽 C4244
SOURCE_EXTRA = {
    '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp':
        ['      <DisableSpecificWarnings>4244;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
}
INCLUDE_DIRS = '$(ProjectDir);..\\..\\third-party\\libsodium-1.0.22-RELEASE\\src\\libsodium\\include'
LINK_LIBS = 'libsodium.lib'
SODIUM_LIB_DIR = '..\\..\\third-party\\libsodium-1.0.22-RELEASE\\bin\\$(Platform)\\$(Configuration)\\v143\\static'

HEADER_COMMENT = '''<!--
  FlamingoSecretChat.vcxproj — 单聊密聊的 DLL 工程（由 flamingoclient/tools/gen_secretchat_vcxproj.py 生成，不要手改）
  balloonwj@qq.com   2026-10-07

  产出 FlamingoSecretChat.dll，由 Flamingo.exe 在登录成功后按企业开关用 LoadLibrary 动态加载（不链接导入库）。
  接口约定见 SecretChatApi.h，方案见 docs/密聊方案.md。

  关键约束：
   · CharacterSet=Unicode、CRT 静态（Debug /MTd、Release /MT），与 Flamingo.exe 一致；接口只用纯虚类与基本类型。
   · 四种配置都定义 WIN32 与 SODIUM_STATIC，由单元测试 SCG2 守住。
   · 四种配置的 OutDir 与 Flamingo.exe 相同：Debug|x64 输出到资源齐全的 ..\\Debug\\，其余三个分别输出到 ..\\Release\\、..\\Debug_Win32\\、..\\Release_Win32\\。
   · 导出名由 FlamingoSecretChat.def 固定；导入库放进中间目录。
   · 所有配置都带 /utf-8：源文件为 UTF-8 无 BOM。
   · 日志：DLL 不编译 exe 的 IULog.cpp，由 Host\\HostLogBridge.cpp 实现 CIULog::Log，经宿主写日志。
   · 没有界面：不引用 balloonui，也不需要 WTL。
-->'''


def cond(c, p):
    return "'$(Configuration)|$(Platform)'=='%s|%s'" % (c, p)


def build():
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
            '    <RootNamespace>FlamingoSecretChat</RootNamespace>',
            '    <ProjectName>FlamingoSecretChat</ProjectName>',
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
                '    <TargetName>FlamingoSecretChat</TargetName>',
                '    <LinkIncremental>%s</LinkIncremental>' % ('true' if d else 'false'),
                '  </PropertyGroup>']
    for c, p, o, i, d in CONFIGS:
        out += ['  <ItemDefinitionGroup Condition="%s">' % cond(c, p),
                '    <ClCompile>',
                '      <Optimization>%s</Optimization>' % ('Disabled' if d else 'MaxSpeed'),
                '      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>',
                '      <AdditionalIncludeDirectories>%s;%%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>' % INCLUDE_DIRS,
                '      <PreprocessorDefinitions>SODIUM_STATIC;WIN32;_WINDOWS;_USRDLL;%s;_CRT_SECURE_NO_WARNINGS;'
                '%%(PreprocessorDefinitions)</PreprocessorDefinitions>' % ('_DEBUG' if d else 'NDEBUG'),
                '      <RuntimeLibrary>%s</RuntimeLibrary>' % ('MultiThreadedDebug' if d else 'MultiThreaded'),
                '      <PrecompiledHeader>NotUsing</PrecompiledHeader>',
                '      <WarningLevel>Level3</WarningLevel>',
                '      <SDLCheck>true</SDLCheck>',
                '      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>',
                '    </ClCompile>',
                '    <Link>',
                '      <SubSystem>Windows</SubSystem>',
                '      <GenerateDebugInformation>true</GenerateDebugInformation>',
                '      <ModuleDefinitionFile>FlamingoSecretChat.def</ModuleDefinitionFile>',
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
    out += ['  </ItemGroup>', '  <ItemGroup>', '    <None Include="FlamingoSecretChat.def" />', '  </ItemGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '  <ImportGroup Label="ExtensionTargets"></ImportGroup>', '</Project>']
    out = version_rc.insert_items(out, 'FlamingoSecretChat')
    vcx = b'\xef\xbb\xbf' + ('\n'.join(out) + '\n').encode('utf-8')
    deflines = ['; FlamingoSecretChat.dll 的导出表：固定导出名，不受调用约定与名称修饰影响（32 位与 64 位相同）。',
                '; balloonwj@qq.com   2026-10-07',
                'LIBRARY FlamingoSecretChat',
                'EXPORTS',
                '    FlamingoSecretChat_Create']
    deff = ('\r\n'.join(deflines) + '\r\n').encode('utf-8')
    return vcx, deff


def main():
    vcx, deff = build()
    vcx_path = os.path.join(ROOT, 'FlamingoSecretChat.vcxproj')
    def_path = os.path.join(ROOT, 'FlamingoSecretChat.def')
    if '--check' in sys.argv:
        same = (os.path.exists(vcx_path) and open(vcx_path, 'rb').read() == vcx and
                os.path.exists(def_path) and open(def_path, 'rb').read() == deff and
                version_rc.sync_rc('FlamingoSecretChat', check=True))
        print('FlamingoSecretChat.vcxproj / .def %s' % ('up to date' if same else 'OUT OF DATE'))
        return 0 if same else 1
    open(vcx_path, 'wb').write(vcx)
    xml.dom.minidom.parse(vcx_path)
    open(def_path, 'wb').write(deff)
    version_rc.sync_rc('FlamingoSecretChat')
    print('ok: %d headers, %d sources' % (len(HEADERS), len(SOURCES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
