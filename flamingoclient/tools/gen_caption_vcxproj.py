# -*- coding: utf-8 -*-
"""
生成语音字幕的两个 DLL 工程（docs/语音字幕方案.md 4.1、4.2、第 11 节）：
  · flamingoclient/FlamingoCaption/FlamingoCaption.vcxproj 与 FlamingoCaption.def —— 字幕功能 DLL；
  · flamingoclient/FlamingoCaptionModel/FlamingoCaptionModel.vcxproj 与 FlamingoCaptionModel.rc —— 只含模型资源的 DLL。
两个工程文件都是生成的，不要手改：FlamingoCaption 的模块目录（MODULE_DIRS）下的 .h / .cpp 按目录自动收集，往这些目录里
加文件之后重新运行本脚本，再运行同目录的 gen_vcxproj_filters.py 生成 filters。

与其他插件 DLL 工程（gen_media_vcxproj.py、gen_p2p_vcxproj.py）的区别：
  · 四个配置都用静态 CRT 的 Release 版（/MT），Debug 配置只是不优化、带调试信息、不定义 _DEBUG：sherpa-onnx 与 onnxruntime
    只有 Release 版静态库，/MTd 链接时报 LNK2038（方案 14.2）；balloonui 因此也一律链接它的 Release 配置。
  · balloonui 不经 ProjectReference 引用，而是由工程里的 CaptionLinkReleaseBalloonUi 目标在链接前用 MSBuild 任务编出它的
    Release 配置，再把产出的静态库加入链接输入。原因：通过解决方案编译时，MSBuild 按解决方案的配置给被引用工程指定配置，
    ProjectReference 上的 SetConfiguration 不生效，Debug 解决方案会链接到 balloonui 的 Debug 版而报 LNK2038（2026-10-09 实测）。
  · sherpa-onnx 静态库（flamingoclient/deps/sherpa-onnx/lib/<平台>/，由 build_sherpa_onnx.ps1 生成，不进版本库）存在时才定义
    CAPTION_HAS_ENGINE 并链接；不存在时照常编译，识别引擎报「不可用」，字幕功能自动隐藏，不影响解决方案的编译。
  · 模型资源 DLL 同理：模型文件（flamingoclient/deps/caption-model/out/，由 prepare_model.py 生成）存在时才编进资源。

用法：python flamingoclient/tools/gen_caption_vcxproj.py

balloonwj@qq.com   2026-10-06
"""
import glob
import os
import xml.dom.minidom

import version_rc   # 版本信息资源（文件属性里的文件说明、版权），见 version_rc.py

CLIENT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
ROOT = os.path.join(CLIENT, 'FlamingoCaption')
MODEL_ROOT = os.path.join(CLIENT, 'FlamingoCaptionModel')
GUID = '{6C1F7E2A-9B3D-4E5F-A812-3D4C5B6A7E81}'
MODEL_GUID = '{8D2E4F6A-1B3C-4D5E-9F70-A1B2C3D4E5F6}'
CONFIGS = [
    # (配置, 平台, 输出目录, 中间目录, 是否 Debug)
    ('Debug', 'Win32', '..\\Debug_Win32\\', 'Debug\\', True),
    ('Release', 'Win32', '..\\Release_Win32\\', 'Release\\', False),
    ('Debug', 'x64', '..\\Debug\\', 'Debug_x64\\', True),
    ('Release', 'x64', '..\\Release\\', 'Release_x64\\', False),
]
HEADERS = ['stdafx.h', 'CaptionApi.h',
           # 与 exe、FlamingoMedia 共用的源文件：日志接口与格式化、多语言（TR 经宿主翻译）
           '..\\Source\\Common\\Log\\IULog.h', '..\\FlamingoMedia\\Host\\LogFormat.h',
           '..\\Source\\Common\\I18n\\I18n.h', '..\\Source\\Common\\Util\\QuietLoadLibrary.h',
           '..\\FlamingoLang\\FlamingoLangApi.h',
           # 标题栏与任务栏的程序图标（各窗口共用，资源从 exe 模块加载）
           '..\\Source\\UI\\BalloonMain\\TitleBarIcon.h',
           # sherpa-onnx 的 C 接口（打过补丁的那份，入库）
           '..\\deps\\sherpa-onnx\\include\\sherpa-onnx\\c-api\\c-api.h']
SOURCES = ['DllMain.cpp', '..\\FlamingoMedia\\Host\\LogFormat.cpp', '..\\Source\\Common\\I18n\\I18n.cpp',
           '..\\Source\\UI\\BalloonMain\\TitleBarIcon.cpp',
           # jsoncpp：草稿、设置、替换表的 JSON，与 exe 用同一份源码
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_value.cpp',
           '..\\Source\\third-party\\jsoncpp-1.9.0\\json_writer.cpp']
# 模块目录：Model 纯逻辑（字幕数据、切分、草稿、设置、折行），Engine 识别引擎与识别线程，Record 录制草稿，
# Render 字幕绘制，Host 模块对象与日志，Editor 字幕编辑器（第 6 期），Export 导出（第 7 期）
MODULE_DIRS = ['Model', 'Engine', 'Record', 'Render', 'Host', 'Editor', 'Export']
for _d in MODULE_DIRS:
    HEADERS += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.h')))
    SOURCES += sorted('%s\\%s' % (_d, os.path.basename(f)) for f in glob.glob(os.path.join(ROOT, _d, '*.cpp')))
SOURCE_EXTRA = {
    # json_reader.cpp 有 size_t 到 int 的窄化，与 exe、其他 DLL 一样屏蔽 C4244
    '..\\Source\\third-party\\jsoncpp-1.9.0\\json_reader.cpp':
        ['      <DisableSpecificWarnings>4244;%(DisableSpecificWarnings)</DisableSpecificWarnings>'],
}
# 包含目录：本目录（共用源文件里的 #include "stdafx.h" 据此找到本工程的 stdafx.h）、WTL、balloonui、sherpa-onnx 头文件
INCLUDE_DIRS = ('$(ProjectDir);..\\Source\\third-party\\wtl10.1;..\\..\\third-party\\balloonui\\balloonui;'
                '..\\deps\\sherpa-onnx\\include')
# 系统库：GDI+（字幕绘制）、comctl32（balloonui）、Media Foundation（编辑器预览与导出）、D3D11 / DXGI（预览取帧）、version、IMM32
LINK_LIBS = 'gdiplus.lib;comctl32.lib;version.lib;IMM32.lib;mfplat.lib;mfreadwrite.lib;mfuuid.lib;d3d11.lib;dxgi.lib'
# sherpa-onnx 与 onnxruntime 的静态库（build_sherpa_onnx.ps1 复制到 deps/sherpa-onnx/lib/<平台>/）
SHERPA_LIBS = ('sherpa-onnx-c-api.lib;sherpa-onnx-core.lib;sherpa-onnx-fstfar.lib;sherpa-onnx-fst.lib;'
               'sherpa-onnx-kaldifst-core.lib;kaldi-decoder-core.lib;kaldi-native-fbank-core.lib;ssentencepiece_core.lib;'
               'kissfft-float.lib;onnxruntime.lib')
# 链接的静态库工程：balloonui（一律编译并链接它的 Release 配置，原因见文件开头的说明）
BALLOONUI_PROJECT = '$(MSBuildThisFileDirectory)..\\..\\third-party\\balloonui\\balloonui\\balloonui_static.vcxproj'

HEADER_COMMENT = '''<!--
  FlamingoCaption.vcxproj — 语音字幕功能 DLL 工程（由 flamingoclient/tools/gen_caption_vcxproj.py 生成，不要手改）
  balloonwj@qq.com   2026-10-06

  产出 FlamingoCaption.dll，由 FlamingoMedia.dll 在第一次用到字幕时用 LoadLibrary 动态加载（不链接导入库）。
  接口约定见 CaptionApi.h，方案见 docs/语音字幕方案.md。

  关键约束：
   · CharacterSet=Unicode。四个配置都用静态 CRT 的 Release 版（/MT），Debug 配置只是不优化、带调试信息、
     不定义 _DEBUG；balloonui 也一律链接它的 Release 配置。原因：sherpa-onnx 只有 Release 版静态库。
   · balloonui 不用 ProjectReference 引用，由文件末尾的 CaptionLinkReleaseBalloonUi 目标编出并链接它的 Release 配置，
     原因见该目标前的注释。
   · 四种配置都定义 WIN32（仓库代码判断的是它而不是 _WIN32）。
   · sherpa-onnx 静态库存在时（deps\\sherpa-onnx\\lib\\<平台>\\onnxruntime.lib）才定义 CAPTION_HAS_ENGINE 并链接，
     否则照常编译、识别引擎报不可用。
   · 四种配置的 OutDir 与 Flamingo.exe 相同：Debug|x64 输出到资源齐全的 ..\\Debug\\，其余三个分别输出到 ..\\Release\\、..\\Debug_Win32\\、..\\Release_Win32\\。
   · 导出名由 FlamingoCaption.def 固定；导入库与 .exp 放进中间目录，不污染输出目录。
   · 所有配置都带 /utf-8：源文件为 UTF-8 无 BOM。
   · 日志：DLL 不编译 exe 的 IULog.cpp，由 Host\\IULogBridge.cpp 实现 CIULog::Log，经宿主写日志。
   · 不增量链接、打开 /OPT:REF /OPT:ICF：onnxruntime 静态链接，增量链接时 DLL 体积约为 3 倍（方案 14.2）。
-->'''

MODEL_COMMENT = '''<!--
  FlamingoCaptionModel.vcxproj — 语音字幕模型资源 DLL 工程（由 flamingoclient/tools/gen_caption_vcxproj.py 生成，不要手改）
  balloonwj@qq.com   2026-10-06

  产出 FlamingoCaptionModel.dll：没有代码，只有 RCDATA 资源（encoder、decoder、joiner、tokens、模型信息），由
  FlamingoCaption.dll 以数据文件方式加载（32 位与 64 位进程都能加载同一个文件，方案 14.3）。
  模型文件（deps\\caption-model\\out\\，由 prepare_model.py 生成，不进版本库）存在时，资源编译才定义 CAPTION_HAS_MODEL
  把模型编进去；不存在时编出一个只有版本信息的空 DLL，识别引擎加载时报「模型缺失」，字幕功能自动隐藏。
-->'''


def cond(c, p):
    return "'$(Configuration)|$(Platform)'=='%s|%s'" % (c, p)


def project_head(out, guid, name, comment, config_type):
    out += ['<?xml version="1.0" encoding="utf-8"?>', comment,
            '<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">',
            '  <ItemGroup Label="ProjectConfigurations">']
    for c, p, o, i, d in CONFIGS:
        out += ['    <ProjectConfiguration Include="%s|%s">' % (c, p),
                '      <Configuration>%s</Configuration>' % c,
                '      <Platform>%s</Platform>' % p,
                '    </ProjectConfiguration>']
    out += ['  </ItemGroup>',
            '  <PropertyGroup Label="Globals">',
            '    <ProjectGuid>%s</ProjectGuid>' % guid,
            '    <RootNamespace>%s</RootNamespace>' % name,
            '    <ProjectName>%s</ProjectName>' % name,
            '    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>',
            '  </PropertyGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />']
    for c, p, o, i, d in CONFIGS:
        out += ['  <PropertyGroup Condition="%s" Label="Configuration">' % cond(c, p),
                '    <ConfigurationType>%s</ConfigurationType>' % config_type,
                '    <PlatformToolset>v143</PlatformToolset>',
                '    <CharacterSet>Unicode</CharacterSet>',
                '    <UseDebugLibraries>false</UseDebugLibraries>',
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
                '    <TargetName>%s</TargetName>' % name,
                '    <LinkIncremental>false</LinkIncremental>',
                '  </PropertyGroup>']


def write_project(path, out):
    open(path, 'wb').write(b'\xef\xbb\xbf' + ('\n'.join(out) + '\n').encode('utf-8'))
    xml.dom.minidom.parse(path)


def gen_caption():
    out = []
    project_head(out, GUID, 'FlamingoCaption', HEADER_COMMENT, 'DynamicLibrary')
    # 静态库是否就绪：按平台判断
    out += ['  <PropertyGroup>',
            '    <CaptionSherpaLibDir>$(MSBuildThisFileDirectory)..\\deps\\sherpa-onnx\\lib\\$(Platform)\\</CaptionSherpaLibDir>',
            '    <CaptionHasEngine Condition="Exists(\'$(CaptionSherpaLibDir)onnxruntime.lib\')">true</CaptionHasEngine>',
            '  </PropertyGroup>']
    for c, p, o, i, d in CONFIGS:
        out += ['  <ItemDefinitionGroup Condition="%s">' % cond(c, p),
                '    <ClCompile>',
                '      <Optimization>%s</Optimization>' % ('Disabled' if d else 'MaxSpeed'),
                '      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>',
                '      <AdditionalIncludeDirectories>%s;%%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>' % INCLUDE_DIRS,
                '      <PreprocessorDefinitions>WIN32;_WINDOWS;_USRDLL;NDEBUG;_CRT_SECURE_NO_WARNINGS;'
                '_WINSOCK_DEPRECATED_NO_WARNINGS;NOMINMAX;%(PreprocessorDefinitions)</PreprocessorDefinitions>',
                '      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>',
                '      <PrecompiledHeader>NotUsing</PrecompiledHeader>',
                '      <WarningLevel>Level3</WarningLevel>',
                '      <SDLCheck>true</SDLCheck>',
                '      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>',
                '    </ClCompile>',
                '    <Link>',
                '      <SubSystem>Windows</SubSystem>',
                '      <GenerateDebugInformation>true</GenerateDebugInformation>',
                '      <ModuleDefinitionFile>FlamingoCaption.def</ModuleDefinitionFile>',
                '      <ImportLibrary>$(IntDir)$(TargetName).lib</ImportLibrary>',
                '      <AdditionalDependencies>%s;%%(AdditionalDependencies)</AdditionalDependencies>' % LINK_LIBS,
                '      <EnableCOMDATFolding>true</EnableCOMDATFolding>',
                '      <OptimizeReferences>true</OptimizeReferences>',
                '    </Link>',
                '  </ItemDefinitionGroup>']
    # 识别引擎：静态库就绪时追加宏与链接项
    out += ['  <ItemDefinitionGroup Condition="\'$(CaptionHasEngine)\'==\'true\'">',
            '    <ClCompile>',
            '      <PreprocessorDefinitions>CAPTION_HAS_ENGINE;%(PreprocessorDefinitions)</PreprocessorDefinitions>',
            '    </ClCompile>',
            '    <Link>',
            '      <AdditionalLibraryDirectories>$(CaptionSherpaLibDir);%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>',
            '      <AdditionalDependencies>%s;%%(AdditionalDependencies)</AdditionalDependencies>' % SHERPA_LIBS,
            '    </Link>',
            '  </ItemDefinitionGroup>']
    out.append('  <ItemGroup>')
    out += ['    <ClInclude Include="%s" />' % h for h in HEADERS]
    out += ['  </ItemGroup>', '  <ItemGroup>']
    for s in SOURCES:
        if s in SOURCE_EXTRA:
            out += ['    <ClCompile Include="%s">' % s] + SOURCE_EXTRA[s] + ['    </ClCompile>']
        else:
            out.append('    <ClCompile Include="%s" />' % s)
    out += ['  </ItemGroup>', '  <ItemGroup>', '    <None Include="FlamingoCaption.def" />', '  </ItemGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '  <ImportGroup Label="ExtensionTargets"></ImportGroup>']
    # balloonui：链接前编出它的 Release 配置并加入链接输入（不用 ProjectReference，原因写在生成的注释里）
    out += ['  <!-- balloonui：四个配置一律链接它的 Release 配置（原因见文件头）。不用 ProjectReference 的 SetConfiguration：',
            '       通过解决方案编译时，MSBuild 按解决方案的配置给被引用工程指定配置，SetConfiguration 不生效，Debug 解决方案',
            '       因此链接到 balloonui 的 Debug 版而报 LNK2038（2026-10-09 实测）。本目标在链接之前用 MSBuild 任务编出 balloonui',
            '       的 Release 配置，再把它产出的静态库加入链接输入，单独编译本工程与通过解决方案编译链接的都是这一份。',
            '       Release 解决方案里，这次调用与解决方案自身编译 balloonui 时的全局属性相同，MSBuild 只编一次。 -->',
            '  <PropertyGroup>',
            '    <CaptionBalloonUiProject>%s</CaptionBalloonUiProject>' % BALLOONUI_PROJECT,
            '  </PropertyGroup>',
            '  <!-- 原生 C++ 工程的 Build 目标不返回产物路径（2026-10-09 实测为空），库路径取自紧接着调用的 GetTargetPath 目标。 -->',
            '  <Target Name="CaptionLinkReleaseBalloonUi" BeforeTargets="Link">',
            '    <MSBuild Projects="$(CaptionBalloonUiProject)" Targets="Build;GetTargetPath" Properties="Configuration=Release;Platform=$(Platform)">',
            '      <Output TaskParameter="TargetOutputs" ItemName="CaptionBalloonUiLib" />',
            '    </MSBuild>',
            '    <ItemGroup>',
            '      <Link Include="@(CaptionBalloonUiLib)" />',
            '    </ItemGroup>',
            '    <Message Importance="high" Text="FlamingoCaption links balloonui (Release): @(CaptionBalloonUiLib)" />',
            '  </Target>',
            '</Project>']
    out = version_rc.insert_items(out, 'FlamingoCaption')
    write_project(os.path.join(ROOT, 'FlamingoCaption.vcxproj'), out)
    version_rc.sync_rc('FlamingoCaption')
    deflines = ['; FlamingoCaption.dll 的导出表：固定导出名，不受调用约定与名称修饰影响（32 位与 64 位相同）。',
                '; balloonwj@qq.com   2026-10-06',
                'LIBRARY FlamingoCaption',
                'EXPORTS',
                '    FlamingoCaption_Create']
    open(os.path.join(ROOT, 'FlamingoCaption.def'), 'wb').write(('\r\n'.join(deflines) + '\r\n').encode('utf-8'))
    print('FlamingoCaption: %d headers, %d sources' % (len(HEADERS), len(SOURCES)))


def gen_model():
    os.makedirs(MODEL_ROOT, exist_ok=True)
    out = []
    project_head(out, MODEL_GUID, 'FlamingoCaptionModel', MODEL_COMMENT, 'DynamicLibrary')
    out += ['  <PropertyGroup>',
            '    <CaptionModelDir>$(MSBuildThisFileDirectory)..\\deps\\caption-model\\out\\</CaptionModelDir>',
            '    <CaptionHasModel Condition="Exists(\'$(CaptionModelDir)encoder.int8.ort\')">true</CaptionHasModel>',
            '  </PropertyGroup>']
    for c, p, o, i, d in CONFIGS:
        out += ['  <ItemDefinitionGroup Condition="%s">' % cond(c, p),
                '    <Link>',
                '      <SubSystem>Windows</SubSystem>',
                '      <NoEntryPoint>true</NoEntryPoint>',
                '      <GenerateDebugInformation>false</GenerateDebugInformation>',
                '      <IgnoreAllDefaultLibraries>true</IgnoreAllDefaultLibraries>',
                '      <ImportLibrary>$(IntDir)$(TargetName).lib</ImportLibrary>',
                '    </Link>',
                '  </ItemDefinitionGroup>']
    # 资源编译按 UTF-8 读文件：CaptionModelRes.h 的注释是 UTF-8 中文，按系统代码页（GBK）读时，误读的多字节字符会吞掉换行，
    # 使下一行的 #define 并进注释而失效（2026-10-06 实测：五个资源编号只有最后一个生效）
    out += ['  <ItemDefinitionGroup>',
            '    <ResourceCompile>',
            '      <AdditionalOptions>/c65001 %(AdditionalOptions)</AdditionalOptions>',
            '    </ResourceCompile>',
            '  </ItemDefinitionGroup>']
    out += ['  <ItemDefinitionGroup Condition="\'$(CaptionHasModel)\'==\'true\'">',
            '    <ResourceCompile>',
            '      <PreprocessorDefinitions>CAPTION_HAS_MODEL;%(PreprocessorDefinitions)</PreprocessorDefinitions>',
            '    </ResourceCompile>',
            '  </ItemDefinitionGroup>',
            '  <ItemGroup>',
            '    <ClInclude Include="..\\FlamingoCaption\\Engine\\CaptionModelRes.h" />',
            '  </ItemGroup>',
            '  <ItemGroup>',
            '    <ResourceCompile Include="FlamingoCaptionModel.rc" />',
            '  </ItemGroup>',
            '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '  <ImportGroup Label="ExtensionTargets"></ImportGroup>', '</Project>']
    out = version_rc.insert_items(out, 'FlamingoCaptionModel')
    write_project(os.path.join(MODEL_ROOT, 'FlamingoCaptionModel.vcxproj'), out)
    version_rc.sync_rc('FlamingoCaptionModel')
    # 版本信息不再写在本 .rc 里，改由 version_rc.py 生成的 FlamingoCaptionModelVersion.rc 提供（2026-10-09），
    # 两处都写会出现两个编号为 1 的版本资源，链接时报资源重复
    rc = ['// FlamingoCaptionModel.dll resources (generated by flamingoclient/tools/gen_caption_vcxproj.py, do not edit):',
          '// the speech recognition model for voice captions, embedded as RCDATA (voice caption plan in docs/, section 4.2).',
          '// The model files are not in the repository; flamingoclient/deps/caption-model/prepare_model.py creates them.',
          '// When they are missing the project does not define CAPTION_HAS_MODEL and this DLL only has version info.',
          '// The version info itself is in FlamingoCaptionModelVersion.rc (generated by flamingoclient/tools/version_rc.py).',
          '#include "..\\FlamingoCaption\\Engine\\CaptionModelRes.h"',
          '',
          '#ifdef CAPTION_HAS_MODEL',
          'IDR_CAPTION_MODEL_ENCODER RCDATA "..\\\\deps\\\\caption-model\\\\out\\\\encoder.int8.ort"',
          'IDR_CAPTION_MODEL_DECODER RCDATA "..\\\\deps\\\\caption-model\\\\out\\\\decoder.onnx"',
          'IDR_CAPTION_MODEL_JOINER  RCDATA "..\\\\deps\\\\caption-model\\\\out\\\\joiner.int8.onnx"',
          'IDR_CAPTION_MODEL_TOKENS  RCDATA "..\\\\deps\\\\caption-model\\\\out\\\\tokens.txt"',
          'IDR_CAPTION_MODEL_INFO    RCDATA "..\\\\deps\\\\caption-model\\\\out\\\\model_info.txt"',
          '#endif']
    open(os.path.join(MODEL_ROOT, 'FlamingoCaptionModel.rc'), 'wb').write(('\r\n'.join(rc) + '\r\n').encode('ascii'))
    print('FlamingoCaptionModel: ok')


def main():
    gen_caption()
    gen_model()


if __name__ == '__main__':
    main()
