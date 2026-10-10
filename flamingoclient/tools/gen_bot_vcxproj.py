# -*- coding: utf-8 -*-
"""
生成 flamingoclient/FlamingoBot/FlamingoBot.vcxproj（带 BOM、LF）与 FlamingoBot.def（CRLF）。
FlamingoBot.vcxproj 是生成的，不要手改：源文件清单写在 SOURCES / HEADERS 里，模块目录（MODULE_DIRS）下的
.h / .cpp 按目录自动收集，所以往这些目录里加文件、或改清单之后都要重新运行本脚本，
再运行同目录的 gen_vcxproj_filters.py 生成 filters。

做法照 gen_threadgroup_vcxproj.py：四种配置、静态 CRT、输出目录与 Flamingo.exe 相同、定义 WIN32。
机器人模块有界面（群机器人管理窗、机器人编辑窗、规则编辑窗、「我的机器人」窗口），引用 balloonui_static 并需要 WTL；
不需要 libsodium。

用法：python flamingoclient/tools/gen_bot_vcxproj.py [--check]
  --check：只比较生成结果与磁盘上的文件是否一致，不写文件；一致返回 0，不一致返回 1
  （单元测试 BOTG1 另行核对工程里的文件清单与模块目录一致，加了文件忘记重新运行本脚本时它会失败）

机器人方案（docs/机器人方案.md）第二期 2b 写成。
balloonwj@qq.com   2026-10-08
"""
import glob
import os
import sys
import xml.dom.minidom

import version_rc   # 版本信息资源（文件属性里的文件说明、版权），见 version_rc.py

ROOT = os.path.join(os.environ.get('FLAMINGO_TRIAL_ROOT') or os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')), 'flamingoclient', 'FlamingoBot')
GUID = '{3237003F-5C82-40C9-AF40-C77AF13C8FC0}'
CONFIGS = [
    # (配置, 平台, 输出目录, 中间目录, 是否 Debug)
    ('Debug', 'Win32', '..\\Debug_Win32\\', 'Debug\\', True),
    ('Release', 'Win32', '..\\Release_Win32\\', 'Release\\', False),
    ('Debug', 'x64', '..\\Debug\\', 'Debug_x64\\', True),
    ('Release', 'x64', '..\\Release\\', 'Release_x64\\', False),
]
HEADERS = ['stdafx.h', 'BotApi.h',
           # 与别的工程共用的文件：客户端日志接口（DLL 内由 Host\\HostLogBridge.cpp 实现）、FlamingoMedia 的日志格式化、
           # 多语言（界面文字经宿主翻译）、标题栏程序图标（各窗口标题栏左侧的图标，图标资源从 exe 模块加载）
           '..\\Source\\Common\\Log\\IULog.h', '..\\FlamingoMedia\\Host\\LogFormat.h',
           '..\\Source\\Common\\I18n\\I18n.h', '..\\Source\\Common\\I18n\\BalloonUiText.h',
           '..\\Source\\UI\\BalloonMain\\TitleBarIcon.h']
SOURCES = ['DllMain.cpp', '..\\FlamingoMedia\\Host\\LogFormat.cpp', '..\\Source\\Common\\I18n\\I18n.cpp',
           '..\\Source\\UI\\BalloonMain\\TitleBarIcon.cpp',
           # jsoncpp：编解码 2802～2809 报文，与 exe 用同一份源码
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_value.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_writer.cpp']
# 模块目录：目录下的 .h / .cpp 全部编进本工程。Host：模块对象、日志出口与日志桥；Core：纯逻辑（数据结构、报文编解码、
# 本地校验、界面文字、规则与界面字段互转、请求登记簿，同时编进单元测试）；UI：各窗口与它们的控件树
MODULE_DIRS = ['Host', 'Core', 'UI']
for _d in MODULE_DIRS:
    HEADERS += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.h')))
    SOURCES += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.cpp')))
# json_reader.cpp 有 size_t 到 int 的窄化，与 exe、FlamingoP2P、FlamingoThreadGroup 一样屏蔽 C4244
SOURCE_EXTRA = {
    '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp':
        ['      <DisableSpecificWarnings>4244;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
}
# 包含目录：本目录（共用源文件里的 #include "stdafx.h" 据此找到本工程的 stdafx.h）、WTL、balloonui
INCLUDE_DIRS = '$(ProjectDir);..\\Source\\third-party\\wtl10.1;..\\..\\third-party\\balloonui\\balloonui'
# 链接的系统库：balloonui 用到的 gdiplus、comctl32、msimg32、shlwapi
LINK_LIBS = 'gdiplus.lib;comctl32.lib;msimg32.lib;shlwapi.lib'
# 引用的静态库工程：balloonui 界面库（与 exe 工程相同）
PROJECT_REFS = [
    ('..\\..\\third-party\\balloonui\\balloonui\\balloonui_static.vcxproj', '{e5f9b2a1-6c84-4de3-9c75-3b8a4d2e7f60}'),
]

HEADER_COMMENT = '''<!--
  FlamingoBot.vcxproj — 机器人管理界面的 DLL 工程（由 flamingoclient/tools/gen_bot_vcxproj.py 生成，不要手改）
  balloonwj@qq.com   2026-10-08

  产出 FlamingoBot.dll，由 Flamingo.exe 在登录成功后按企业开关 features.bot 用 LoadLibrary 动态加载（不链接导入库）。
  接口约定见 BotApi.h，方案见 docs/机器人方案.md 8.2。

  关键约束：
   · CharacterSet=Unicode、CRT 静态（Debug /MTd、Release /MT），与 Flamingo.exe 及 balloonui_static 一致。
     exe 与 DLL 因此各有独立的堆，接口只用纯虚类、基本类型与固定布局的结构体。
   · 四种配置都定义 WIN32（仓库代码判断的是它而不是 _WIN32），由单元测试 BOTG2 守住。
   · 四种配置的 OutDir 与 Flamingo.exe 相同：Debug|x64 输出到资源齐全的 ..\\Debug\\，其余三个分别输出到 ..\\Release\\、..\\Debug_Win32\\、..\\Release_Win32\\。
   · 导出名由 FlamingoBot.def 固定；导入库与 .exp 放进中间目录，不污染输出目录。
   · 所有配置都带 /utf-8：源文件为 UTF-8 无 BOM。
   · 日志：DLL 不编译 exe 的 IULog.cpp，由 Host\\HostLogBridge.cpp 实现 CIULog::Log，经宿主写日志。
   · 静态链接自己的一份 balloonui（各管理窗口），DLL 有自己的 _Module（见 DllMain.cpp）。
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
            '    <RootNamespace>FlamingoBot</RootNamespace>',
            '    <ProjectName>FlamingoBot</ProjectName>',
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
                '    <TargetName>FlamingoBot</TargetName>',
                '    <LinkIncremental>%s</LinkIncremental>' % ('true' if d else 'false'),
                '  </PropertyGroup>']
    for c, p, o, i, d in CONFIGS:
        out += ['  <ItemDefinitionGroup Condition="%s">' % cond(c, p),
                '    <ClCompile>',
                '      <Optimization>%s</Optimization>' % ('Disabled' if d else 'MaxSpeed'),
                '      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>',
                '      <AdditionalIncludeDirectories>%s;%%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>' % INCLUDE_DIRS,
                '      <PreprocessorDefinitions>WIN32;_WINDOWS;_USRDLL;%s;_CRT_SECURE_NO_WARNINGS;'
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
                '      <ModuleDefinitionFile>FlamingoBot.def</ModuleDefinitionFile>',
                '      <ImportLibrary>$(IntDir)$(TargetName).lib</ImportLibrary>',
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
    out += ['  </ItemGroup>', '  <ItemGroup>', '    <None Include="FlamingoBot.def" />', '  </ItemGroup>',
            '  <ItemGroup>']
    for path, guid in PROJECT_REFS:
        out += ['    <ProjectReference Include="%s">' % path, '      <Project>%s</Project>' % guid, '    </ProjectReference>']
    out += ['  </ItemGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '  <ImportGroup Label="ExtensionTargets"></ImportGroup>', '</Project>']
    out = version_rc.insert_items(out, 'FlamingoBot')
    vcx = b'\xef\xbb\xbf' + ('\n'.join(out) + '\n').encode('utf-8')
    deflines = ['; FlamingoBot.dll 的导出表：固定导出名，不受调用约定与名称修饰影响（32 位与 64 位相同）。',
                '; balloonwj@qq.com   2026-10-08',
                'LIBRARY FlamingoBot',
                'EXPORTS',
                '    FlamingoBot_Create']
    deff = ('\r\n'.join(deflines) + '\r\n').encode('utf-8')
    return vcx, deff


def main():
    vcx, deff = build()
    vcx_path = os.path.join(ROOT, 'FlamingoBot.vcxproj')
    def_path = os.path.join(ROOT, 'FlamingoBot.def')
    if '--check' in sys.argv:
        same = (os.path.exists(vcx_path) and open(vcx_path, 'rb').read() == vcx and
                os.path.exists(def_path) and open(def_path, 'rb').read() == deff and
                version_rc.sync_rc('FlamingoBot', check=True))
        print('FlamingoBot.vcxproj / .def %s' % ('up to date' if same else 'OUT OF DATE'))
        return 0 if same else 1
    open(vcx_path, 'wb').write(vcx)
    xml.dom.minidom.parse(vcx_path)
    open(def_path, 'wb').write(deff)
    version_rc.sync_rc('FlamingoBot')
    print('ok: %d headers, %d sources' % (len(HEADERS), len(SOURCES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
