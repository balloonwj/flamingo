# -*- coding: utf-8 -*-
"""
PC 客户端各 exe / dll 的版本信息资源：生成每个工程的 <名称>Version.rc，并给出登记它的工程文件条目。

文件属性「详细信息」页显示的文件说明、版权、版本都来自这里：每个工程的 <名称>Version.rc 只定义本文件的说明与文件名，
再包含共用模板 flamingoclient/Source/FileVersionInfo.rc2（版本号取 ClientVersion.h，版权与产品名写在模板里）。
15 个文件的说明集中写在下面的 PROJECTS 表里，改措辞只改这一处，然后重新运行本脚本与各工程的生成脚本。

用法：
  · 插件 DLL 的工程生成脚本（gen_media_vcxproj.py 等）import 本模块：insert_items() 把登记条目插进工程文件，
    sync_rc() 写出（或在 --check 时比较）该工程的 Version.rc；
  · 手写的工程（Flamingo、FlamingoUpdater、FlamingoFileShare、FlamingoLang、FlamingoOcr、FlamingoMarkdown）的 Version.rc
    由本脚本直接生成：python flamingoclient/tools/version_rc.py；加 --check 只比较不写入（全部一致时返回 0）。
    这几个工程的登记条目是手工写进 .vcxproj 的，格式与 project_items() 的输出相同。

生成的 .rc 为 UTF-8 无 BOM、CRLF 换行（rc.exe 只认 CRLF），编译时单独带 /c65001。
守卫用例：flamingoclient/Source/Tests/FileVersionInfoTests.cpp（标签 [verinfo]）。

balloonwj@qq.com   2026-10-09
"""
import os
import sys

CLIENT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# 各 exe / dll 的版本信息：(工程目录（相对 flamingoclient）, 文件名（不含扩展名）, 扩展名, 文件说明, 生成该工程文件的脚本（手写为 None）)
PROJECTS = [
    ('Source', 'Flamingo', 'exe', 'FlamingoIM 客户端主程序：登录、通讯录、单聊与群聊、消息记录', None),
    ('FlamingoUpdater', 'FlamingoUpdater', 'exe', 'FlamingoIM 自动升级程序：安装升级包，失败时回滚', None),
    ('FlamingoMedia', 'FlamingoMedia', 'dll', 'FlamingoIM 音视频模块：音视频通话、远程桌面、屏幕共享、录屏、语音消息',
     'gen_media_vcxproj.py'),
    ('FlamingoP2P', 'FlamingoP2P', 'dll', 'FlamingoIM 点对点连接模块：局域网直连、UDP 打洞与服务器中转', 'gen_p2p_vcxproj.py'),
    ('FlamingoLang', 'FlamingoLang', 'dll', 'FlamingoIM 界面语言包：繁体中文、英文、日文、韩文', None),
    ('FlamingoOcr', 'FlamingoOcr', 'dll', 'FlamingoIM 图片文字识别模块', None),
    ('FlamingoMarkdown', 'FlamingoMarkdown', 'dll', 'FlamingoIM Markdown 消息模块：解析、排版绘制与导出', None),
    ('FlamingoLock', 'FlamingoLock', 'dll', 'FlamingoIM 锁屏模块', 'gen_lock_vcxproj.py'),
    ('FlamingoScreenshot', 'FlamingoScreenshot', 'dll', 'FlamingoIM 截图模块：框选、标注、复制、保存与发送',
     'gen_screenshot_vcxproj.py'),
    ('FlamingoFileShare', 'FlamingoFileShare', 'dll', 'FlamingoIM 文件共享模块：在聊天中共享本地磁盘、文件夹或文件', None),
    ('FlamingoSecretChat', 'FlamingoSecretChat', 'dll', 'FlamingoIM 密聊模块：一对一端到端加密聊天', 'gen_secretchat_vcxproj.py'),
    ('FlamingoThreadGroup', 'FlamingoThreadGroup', 'dll', 'FlamingoIM 帖子群模块：发帖、评论与表态', 'gen_threadgroup_vcxproj.py'),
    ('FlamingoBot', 'FlamingoBot', 'dll', 'FlamingoIM 机器人管理模块：群机器人与个人机器人', 'gen_bot_vcxproj.py'),
    ('FlamingoCaption', 'FlamingoCaption', 'dll', 'FlamingoIM 语音字幕模块：实时字幕、字幕编辑与导出', 'gen_caption_vcxproj.py'),
    ('FlamingoCaptionModel', 'FlamingoCaptionModel', 'dll', 'FlamingoIM 语音识别模型（供语音字幕模块使用）', 'gen_caption_vcxproj.py'),
]

# 版本资源里的文件类型：应用程序 VFT_APP、动态链接库 VFT_DLL（winver.h 的取值）
FILE_TYPES = {'exe': '0x1L', 'dll': '0x2L'}


def _entry(name):
    for e in PROJECTS:
        if e[1] == name:
            return e
    raise KeyError('no version info entry for ' + name)


def rc_file_name(name):
    """该工程的版本资源文件名，如 FlamingoMedia -> FlamingoMediaVersion.rc。"""
    return name + 'Version.rc'


def rc_bytes(name):
    """生成该工程 Version.rc 的完整内容（UTF-8 无 BOM，CRLF 换行）。"""
    folder, name, ext, description, generator = _entry(name)
    lines = [
        '// %s.%s 的版本信息（文件属性「详细信息」页的文件说明、版权、版本），资源内容由共用模板 FileVersionInfo.rc2 生成。'
        % (name, ext),
        '// 本文件由 flamingoclient/tools/version_rc.py 生成，不要手改：文件说明写在该脚本的 PROJECTS 表里。',
        '// 编码 UTF-8 无 BOM，编译时带 /c65001；必须保持 CRLF 换行（原因见 FileVersionInfo.rc2）。',
        '// balloonwj@qq.com   2026-10-09',
        '#include "..\\Source\\ClientVersion.h"',
        '',
        '#define FLAMINGO_FILE_DESCRIPTION    "%s"' % description,
        '#define FLAMINGO_FILE_ORIGINAL_NAME  "%s.%s"' % (name, ext),
        '#define FLAMINGO_FILE_INTERNAL_NAME  "%s"' % name,
        '#define FLAMINGO_FILE_TYPE           %s' % FILE_TYPES[ext],
        '',
        '#include "..\\Source\\FileVersionInfo.rc2"',
    ]
    return ('\r\n'.join(lines) + '\r\n').encode('utf-8')


def rc_path(name):
    """该工程 Version.rc 的绝对路径。"""
    return os.path.join(CLIENT, _entry(name)[0], rc_file_name(name))


def sync_rc(name, check=False):
    """
    写出该工程的 Version.rc；check 为 True 时只比较不写入。
    返回文件在调用后是否与应有内容一致（写入模式下恒为 True）。
    """
    data = rc_bytes(name)
    path = rc_path(name)
    same = os.path.exists(path) and open(path, 'rb').read() == data
    if not check and not same:
        open(path, 'wb').write(data)
        same = True
    return same


def project_items(name):
    """登记该工程 Version.rc 的工程文件条目（一个 ItemGroup，资源文件单独按 /c65001 编译）。"""
    return ['  <ItemGroup>',
            '    <!-- 版本信息（文件说明、版权）：UTF-8 编码，单独按 /c65001 编译；由 tools/version_rc.py 生成 -->',
            '    <ResourceCompile Include="%s">' % rc_file_name(name),
            '      <AdditionalOptions>/c65001 %(AdditionalOptions)</AdditionalOptions>',
            '    </ResourceCompile>',
            '  </ItemGroup>']


def insert_items(out, name):
    """把 project_items() 的条目插到工程文件行列表里 Microsoft.Cpp.targets 的 Import 之前，返回新的列表。"""
    for i, line in enumerate(out):
        if 'Microsoft.Cpp.targets' in line:
            return out[:i] + project_items(name) + out[i:]
    raise ValueError('Microsoft.Cpp.targets import not found')


def main():
    check = '--check' in sys.argv
    ok = True
    for folder, name, ext, description, generator in PROJECTS:
        if generator is not None:
            continue
        same = sync_rc(name, check)
        ok = ok and same
        print('%-24s %s' % (rc_file_name(name), ('up to date' if same else 'OUT OF DATE') if check else 'written'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
