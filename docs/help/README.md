# FlamingoIM 网页版使用教程

用浏览器直接打开 `docs/help/index.html` 即可阅读，不需要网络与服务器。教程分三部分：

| 部分 | 目录 | 读者 |
| --- | --- | --- |
| 客户端使用（30 章） | `client/` | 全体员工 |
| 企业管理员（10 章） | `admin/` | 使用 flamingoAdmin 管理本企业的管理员 |
| 平台超级管理员（4 章） | `platform/` | 平台运营 |

## 目录结构

- `index.html`：首页，章节卡片由脚本按目录数据生成。
- `assets/toc.js`：**唯一的目录数据**（部分、章节 id、文件名、标题）。新增、删除、调整章节顺序只改这里。
- `assets/help.js`：给每个章节页补上顶栏、左侧导航、本页目录、上一章 / 下一章、章节序号、站内搜索、看大图与深浅色切换。
- `assets/help.css`：全部样式；颜色定义为 `:root` 变量，深色模式在同一文件里重新定义。
- `assets/search-index.js`：站内搜索数据，**由脚本生成，不要手工修改**。
- `img/`：全部截图（PNG）。文件名前缀对应章节，例如 `d_` 单聊、`j_` 群管理、`ad_` 企业管理员、`sa_` 平台超管。
- `tools/build_index.py`：生成搜索数据；`tools/check_links.py`：检查链接、锚点、图片引用和闲置图片。
- `help.md`：与网页内容相同的单文件 Markdown 版，**由 `tools/build_markdown.py` 生成，不要手工修改**；其中带编号圆点的截图另存在 `img-md/`（圆点在网页上由 CSS 叠加，Markdown 里只能画进图片）。

## 修改章节之后

```sh
python docs/help/tools/build_index.py    # 重建站内搜索数据
python docs/help/tools/check_links.py    # 检查链接与图片，有问题时退出码为 1
python docs/help/tools/build_markdown.py # 重新生成 help.md 与 img-md/
```

每个章节页只写 `<article class="doc">` 里的正文，`<body>` 上的 `data-root`（到本目录的相对路径）和 `data-chapter`
（`toc.js` 里的章节 id）必须正确。正文可用的组件（步骤、带编号标注的配图、提示框等）见 `assets/help.css` 里的注释，
写法可参照 `client/01-quick-start.html`。

`help.md` 里的跳转链接指向由标题文字生成的锚点（如 `#第-1-章-快速开始登录与注册`），不在标题里写 `<a id>`：
Typora 会把这种没有内容的 HTML 标签原样显示成源码。GitHub 与 Typora 处理同名标题的编号规则不同，
所以**被其他地方链接到的标题，不能与它前面的任何标题同名**（整份教程范围内）。出现这种情况时
`build_markdown.py` 会报错并列出标题，请在章节页面里把它改得更具体，例如「群通话的成员管理」。

## 截图的来源

截图拍摄于 2026-10-09 的一套演示环境：服务端是开发库的副本（清理了测试数据，换成虚构的「明远科技有限公司」与演示对话），
客户端为 x64 Release 版。界面里的人名、群名、地址、端口都是演示数据。界面改版后需要重拍时，请保持文件名不变，
直接替换 `img/` 下的对应图片。

教程编写过程中发现的几处界面与说明不一致（例如系统设置里部分开关尚未生效），正文已按实际行为描述，
并在相应位置标注「暂未生效」或「暂未开放」；功能完善后请同步修改对应章节。
