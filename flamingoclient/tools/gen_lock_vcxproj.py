# -*- coding: utf-8 -*-
"""
生成 flamingoclient/FlamingoLock/FlamingoLock.vcxproj（带 BOM、LF）与 FlamingoLock.def（CRLF）。
FlamingoLock.vcxproj 是生成的，不要手改：源文件清单写在 SOURCES / HEADERS 里，模块目录（MODULE_DIRS）下的
.h / .cpp 按目录自动收集，所以往这些目录里加文件、或改清单之后都要重新运行本脚本，
再运行同目录的 gen_vcxproj_filters.py 生成 filters。

做法照 gen_p2p_vcxproj.py：四种配置、静态 CRT、输出目录与 Flamingo.exe 相同。
与它的区别：锁屏模块有界面（设置临时密码的窗口用 balloonui），引用 balloonui_static 并需要 WTL；
锁屏画面用 Direct2D / DirectWrite 绘制，链接系统导入库 d2d1.lib、dwrite.lib；不需要 libsodium。

用法：python flamingoclient/tools/gen_lock_vcxproj.py [--check]
  --check：只比较生成结果与磁盘上的文件是否一致，不写文件；一致返回 0，不一致返回 1（单元测试 LG1 用）

锁屏方案（docs/锁屏方案.md）第 2a 期写成。
balloonwj@qq.com   2026-10-06
"""
import glob
import os
import sys
import xml.dom.minidom

import version_rc   # 版本信息资源（文件属性里的文件说明、版权），见 version_rc.py

ROOT = os.path.join(os.environ.get('FLAMINGO_TRIAL_ROOT') or r'E:\flamingo', 'flamingoclient', 'FlamingoLock')
GUID = '{61048DFA-D941-4948-827A-B9E6983C48D6}'
CONFIGS = [
    # (配置, 平台, 输出目录, 中间目录, 是否 Debug)
    ('Debug', 'Win32', '..\\Debug_Win32\\', 'Debug\\', True),
    ('Release', 'Win32', '..\\Release_Win32\\', 'Release\\', False),
    ('Debug', 'x64', '..\\Debug\\', 'Debug_x64\\', True),
    ('Release', 'x64', '..\\Release\\', 'Release_x64\\', False),
]
HEADERS = ['stdafx.h', 'FlamingoLockApi.h',
           # 与别的工程共用的文件：客户端日志接口（DLL 内由 Host\\HostLogBridge.cpp 实现）、FlamingoMedia 的日志格式化、
           # 多语言（界面文字经宿主翻译）、防休眠（锁屏期间阻止休眠、关屏与屏保）、
           # 标题栏程序图标（设置临时密码窗口的标题栏与任务栏图标，图标资源从 exe 模块加载；2026-10-07 加入）
           '..\\Source\\Common\\Log\\IULog.h', '..\\FlamingoMedia\\Host\\LogFormat.h',
           '..\\Source\\Common\\I18n\\I18n.h', '..\\Source\\Common\\I18n\\BalloonUiText.h',
           '..\\Source\\UI\\BalloonMain\\KeepAwakeSession.h', '..\\Source\\UI\\BalloonMain\\TitleBarIcon.h']
SOURCES = ['DllMain.cpp', '..\\FlamingoMedia\\Host\\LogFormat.cpp', '..\\Source\\Common\\I18n\\I18n.cpp',
           '..\\Source\\UI\\BalloonMain\\KeepAwakeSession.cpp', '..\\Source\\UI\\BalloonMain\\TitleBarIcon.cpp']
# 模块目录：目录下的 .h / .cpp 全部编进本工程。Host：模块对象与日志桥；Core：纯逻辑（密码、界面状态机、按键判定、
# 画面选择、布局、预览状态，同时编进单元测试）；Render：Direct2D 辅助、叠加层；Session：锁屏线程、会话、窗口、设置窗口、
# 全屏预览、DPI 工具、电源操作；Scene：五种动画场景
MODULE_DIRS = ['Host', 'Core', 'Render', 'Session', 'Scene']
for _d in MODULE_DIRS:
    HEADERS += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.h')))
    SOURCES += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.cpp')))
# 包含目录：本目录（共用源文件里的 #include "stdafx.h" 据此找到本工程的 stdafx.h）、WTL、balloonui
INCLUDE_DIRS = '$(ProjectDir);..\\Source\\third-party\\wtl10.1;..\\..\\third-party\\balloonui\\balloonui'
# 链接的系统库：Direct2D 与 DirectWrite（锁屏画面）、CNG（临时密码摘要）、DWM（帧节拍）、输入法（关闭输入法）、
# 以及 balloonui 用到的 gdiplus、comctl32、msimg32、shlwapi
LINK_LIBS = 'd2d1.lib;dwrite.lib;bcrypt.lib;dwmapi.lib;imm32.lib;gdiplus.lib;comctl32.lib;msimg32.lib;shlwapi.lib'
# 引用的静态库工程：balloonui 界面库（与 exe 工程相同）
PROJECT_REFS = [
    ('..\\..\\third-party\\balloonui\\balloonui\\balloonui_static.vcxproj', '{e5f9b2a1-6c84-4de3-9c75-3b8a4d2e7f60}'),
]

HEADER_COMMENT = '''<!--
  FlamingoLock.vcxproj — 客户端锁屏的 DLL 工程（由 flamingoclient/tools/gen_lock_vcxproj.py 生成，不要手改）
  balloonwj@qq.com   2026-10-06

  产出 FlamingoLock.dll，由 Flamingo.exe 在启动时用 LoadLibrary 动态加载（不链接导入库）。
  接口约定见 FlamingoLockApi.h，方案见 docs/锁屏方案.md。

  关键约束：
   · CharacterSet=Unicode、CRT 静态（Debug /MTd、Release /MT），与 Flamingo.exe 及 balloonui_static 一致。
     exe 与 DLL 因此各有独立的堆，接口只用纯虚类与基本类型。
   · 四种配置都定义 WIN32（仓库代码判断的是它而不是 _WIN32），由单元测试 LG2 守住。
   · 四种配置的 OutDir 与 Flamingo.exe 相同：Debug|x64 输出到资源齐全的 ..\\Debug\\，其余三个分别输出到 ..\\Release\\、..\\Debug_Win32\\、..\\Release_Win32\\。
   · 导出名由 FlamingoLock.def 固定；导入库与 .exp 放进中间目录，不污染输出目录。
   · 所有配置都带 /utf-8：源文件为 UTF-8 无 BOM。
   · 日志：DLL 不编译 exe 的 IULog.cpp，由 Host\\HostLogBridge.cpp 实现 CIULog::Log，经宿主写日志。
   · 静态链接自己的一份 balloonui（设置临时密码的窗口），DLL 有自己的 _Module（见 DllMain.cpp）。
   · 锁屏画面用 Direct2D 1.0 / DirectWrite 绘制，只链接系统导入库，不引入新库。
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
            '    <RootNamespace>FlamingoLock</RootNamespace>',
            '    <ProjectName>FlamingoLock</ProjectName>',
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
                '    <TargetName>FlamingoLock</TargetName>',
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
                '      <ModuleDefinitionFile>FlamingoLock.def</ModuleDefinitionFile>',
                '      <ImportLibrary>$(IntDir)$(TargetName).lib</ImportLibrary>',
                '      <AdditionalDependencies>%s;%%(AdditionalDependencies)</AdditionalDependencies>' % LINK_LIBS]
        if not d:
            out += ['      <EnableCOMDATFolding>true</EnableCOMDATFolding>',
                    '      <OptimizeReferences>true</OptimizeReferences>']
        out += ['    </Link>', '  </ItemDefinitionGroup>']
    out.append('  <ItemGroup>')
    out += ['    <ClInclude Include="%s" />' % h for h in HEADERS]
    out += ['  </ItemGroup>', '  <ItemGroup>']
    out += ['    <ClCompile Include="%s" />' % s for s in SOURCES]
    out += ['  </ItemGroup>', '  <ItemGroup>', '    <None Include="FlamingoLock.def" />', '  </ItemGroup>',
            '  <ItemGroup>']
    for path, guid in PROJECT_REFS:
        out += ['    <ProjectReference Include="%s">' % path, '      <Project>%s</Project>' % guid, '    </ProjectReference>']
    out += ['  </ItemGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '  <ImportGroup Label="ExtensionTargets"></ImportGroup>', '</Project>']
    out = version_rc.insert_items(out, 'FlamingoLock')
    vcx = b'\xef\xbb\xbf' + ('\n'.join(out) + '\n').encode('utf-8')
    deflines = ['; FlamingoLock.dll 的导出表：固定导出名，不受调用约定与名称修饰影响（32 位与 64 位相同）。',
                '; balloonwj@qq.com   2026-10-06',
                'LIBRARY FlamingoLock',
                'EXPORTS',
                '    FlamingoLock_Create']
    deff = ('\r\n'.join(deflines) + '\r\n').encode('utf-8')
    return vcx, deff


def main():
    vcx, deff = build()
    vcx_path = os.path.join(ROOT, 'FlamingoLock.vcxproj')
    def_path = os.path.join(ROOT, 'FlamingoLock.def')
    if '--check' in sys.argv:
        same = (os.path.exists(vcx_path) and open(vcx_path, 'rb').read() == vcx and
                os.path.exists(def_path) and open(def_path, 'rb').read() == deff and
                version_rc.sync_rc('FlamingoLock', check=True))
        print('FlamingoLock.vcxproj / .def %s' % ('up to date' if same else 'OUT OF DATE'))
        return 0 if same else 1
    open(vcx_path, 'wb').write(vcx)
    xml.dom.minidom.parse(vcx_path)
    open(def_path, 'wb').write(deff)
    version_rc.sync_rc('FlamingoLock')
    print('ok: %d headers, %d sources' % (len(HEADERS), len(SOURCES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
