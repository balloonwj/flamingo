/*
 * FlamingoIM 使用教程的目录数据：三个部分、每章一个页面。
 * help.js 据此生成左侧导航、上一章 / 下一章链接与首页的章节卡片；新增章节时只改这里。
 * balloonwj@qq.com   2026-10-09
 */
window.HELP_TOC = [
  {
    id: 'client', title: '客户端使用', desc: '面向全体员工：登录、聊天、群组、音视频、文件与效率工具',
    chapters: [
      { id: 'c01', file: 'client/01-quick-start.html', title: '快速开始：登录与注册' },
      { id: 'c02', file: 'client/02-main-panel.html', title: '认识主面板' },
      { id: 'c03', file: 'client/03-profile.html', title: '个人资料与账号' },
      { id: 'c04', file: 'client/04-contacts.html', title: '通讯录与联系人' },
      { id: 'c05', file: 'client/05-conversations.html', title: '会话列表与消息提醒' },
      { id: 'c06', file: 'client/06-chat.html', title: '单聊：收发消息' },
      { id: 'c07', file: 'client/07-message-actions.html', title: '消息的更多操作' },
      { id: 'c08', file: 'client/08-files.html', title: '图片、文件与文件夹' },
      { id: 'c09', file: 'client/09-screenshot.html', title: '截图与图片文字识别' },
      { id: 'c10', file: 'client/10-voice.html', title: '语音消息' },
      { id: 'c11', file: 'client/11-code.html', title: '代码片段' },
      { id: 'c12', file: 'client/12-markdown.html', title: 'Markdown 文档' },
      { id: 'c13', file: 'client/13-group-chat.html', title: '群聊' },
      { id: 'c14', file: 'client/14-group-manage.html', title: '群管理' },
      { id: 'c15', file: 'client/15-thread-group.html', title: '帖子群' },
      { id: 'c16', file: 'client/16-calls.html', title: '语音与视频通话' },
      { id: 'c17', file: 'client/17-remote-desktop.html', title: '远程桌面' },
      { id: 'c18', file: 'client/18-screen-share.html', title: '屏幕共享' },
      { id: 'c19', file: 'client/19-recording.html', title: '屏幕录制' },
      { id: 'c20', file: 'client/20-captions.html', title: '字幕模式与字幕编辑' },
      { id: 'c21', file: 'client/21-file-share.html', title: '文件共享' },
      { id: 'c22', file: 'client/22-secret-chat.html', title: '密聊' },
      { id: 'c23', file: 'client/23-memo.html', title: '备忘与提醒' },
      { id: 'c24', file: 'client/24-history.html', title: '聊天记录管理' },
      { id: 'c25', file: 'client/25-bots.html', title: '机器人' },
      { id: 'c26', file: 'client/26-lock.html', title: '锁屏' },
      { id: 'c27', file: 'client/27-announcements.html', title: '公告与通知' },
      { id: 'c28', file: 'client/28-settings.html', title: '系统设置' },
      { id: 'c29', file: 'client/29-about-update.html', title: '关于与自动升级' },
      { id: 'c30', file: 'client/30-shortcuts-faq.html', title: '快捷键与常见问题' }
    ]
  },
  {
    id: 'admin', title: '企业管理员', desc: '使用 flamingoAdmin 管理本企业的组织、权限与内容',
    chapters: [
      { id: 'a01', file: 'admin/01-login-dashboard.html', title: '登录与仪表板' },
      { id: 'a02', file: 'admin/02-organization.html', title: '组织架构与员工' },
      { id: 'a03', file: 'admin/03-service-accounts.html', title: '服务号' },
      { id: 'a04', file: 'admin/04-groups-online.html', title: '建群与在线用户' },
      { id: 'a05', file: 'admin/05-feature-switches.html', title: '功能开关' },
      { id: 'a06', file: 'admin/06-sensitive-words.html', title: '敏感词' },
      { id: 'a07', file: 'admin/07-file-share.html', title: '文件共享管理' },
      { id: 'a08', file: 'admin/08-enterprise-qa.html', title: '企业问答' },
      { id: 'a09', file: 'admin/09-enterprise-bots.html', title: '企业机器人' },
      { id: 'a10', file: 'admin/10-audit-settings.html', title: '审计日志与平台设置' }
    ]
  },
  {
    id: 'platform', title: '平台超级管理员', desc: '管理入驻企业、发布平台公告、发布客户端版本',
    chapters: [
      { id: 'p01', file: 'platform/01-overview.html', title: '平台总览' },
      { id: 'p02', file: 'platform/02-enterprises.html', title: '企业管理' },
      { id: 'p03', file: 'platform/03-announcements.html', title: '平台公告' },
      { id: 'p04', file: 'platform/04-client-versions.html', title: '客户端版本与自动升级' }
    ]
  }
];
