/**
 *  GroupChatWindowLayout.h
 *
 *  新版群聊窗口的 balloonui 客户区控件树构造种子。把"群聊窗长什么样"
 *  从窗口类里抽出成一个可独立编译、可被单元测试直接调用的纯函数模块
 *  —— CBalloonGroupChatWindow 与 FlamingoUnit 测试工程共用本文件。
 *
 *  设计稿：design/pc-snapshots/08-chat-group.png。群聊窗 = 单聊窗左半
 *  （头部 + 气泡消息区 + 输入区）+ 右侧 240px 边栏（成员 / 群文件 /
 *  公告）。复用单聊窗的 Chat* 自绘控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <memory>
#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

/**
 *  GroupCtrlId —— 群聊窗里所有"需要被代码引用"的控件的数字 id。
 *
 *  群聊窗与单聊窗是两棵独立的控件树，FindCtrlById 各查各的子树；为可读
 *  起见群聊窗另用 500 段，与单聊窗的 400 段区分。纯装饰控件不写 id。
 */
enum GroupCtrlId : UINT
{
    // ---- 主体 ----
    kGIdMessageArea  = 500,   // 消息区容器（气泡列表）
    kGIdInputEdit    = 501,   // 输入框（DuiRichEditHost，富文本，支持内联图片）
    kGIdSendButton   = 502,   // 发送按钮
    kGIdMention      = 503,   // @提醒 按钮
    kGIdHintLabel    = 504,   // 输入区左下角"发送/换行"提示文字（随发送模式联动）
    kGIdReplyBar     = 506,   // 输入框上方的"引用回复"条（DuiHBox；默认隐藏，点了
                              // 气泡右键「引用回复」才出现，发送后自动收起）
    kGIdReplyName    = 507,   // 引用条里的"回复 张三："（DuiLabel，主题色；宽度按文字实测）
    kGIdReplyDigest  = 509,   // 引用条里的被引用内容摘要（DuiLabel，灰色，过长省略）
    kGIdReplyClose   = 508,   // 引用条右侧的取消按钮（DuiLabel，ModeLink 的 ✕）
    kGIdOfflineBanner= 505,   // 窗口顶部的离线 / 正在连接提示带（ChatBannerLabel，带底色
                              // 与下边框）。平时 SetVisible(false) 不占高度；离线 / 正在
                              // 连接时显示。只作告知、<u>不可点击</u>。与单聊窗的
                              // kIdOfflineBanner 同一套做法、同一套配色
    kGIdInputSplitter= 540,   // 左列里"消息区(上)/输入区块(下)"之间的横向可拖
                              //   分隔条，拖它即可重分两者高度；落在工具栏上边缘

    // ---- 头部右侧图标按钮 ----
    kGIdHdrVoice     = 510,   // 语音通话
    kGIdHdrVideo     = 511,   // 视频通话
    kGIdHdrHistory   = 512,   // 消息记录
    kGIdHdrAdmin     = 513,   // 群管理（齿轮）
    kGIdHdrMore      = 514,   // 更多 / 详情
    kGIdPinBanner    = 515,   // 头部信息行下方的「已 Pin N 条」提示带（ChatBannerLabel，
                              // ModeLink 可点）。本群没有被 Pin 的消息时整条隐藏、不占位；
                              // 点它打开消息记录窗并切到 Pin 分类。与单聊窗 kIdPinBanner 同形

    // ---- 输入区上方工具栏图标按钮 ----
    kGIdTbEmoji      = 520,   // 表情
    kGIdTbImage      = 521,   // 图片
    kGIdTbFile       = 522,   // 文件
    kGIdTbScreenshot = 523,   // 截图
    kGIdTbVoice      = 524,   // 语音
    kGIdTbPlus       = 525,   // 更多
    kGIdTbFont       = 526,   // 字体设置（AF）

    // ---- 右侧边栏 ----
    kGIdTabMembers   = 530,   // 边栏 Tab：成员
    kGIdTabFiles     = 531,   // 边栏 Tab：群文件
    kGIdTabNotice    = 532,   // 边栏 Tab：公告
    kGIdMemberList   = 533,   // 成员列表容器
    kGIdMemberSearch = 534,   // 成员搜索框

    // ---- 右侧边栏「群文件」面板（550 段；540 已被输入分隔条占用，故从 550 起）----
    // 边栏下部是两个互斥的内容容器：成员容器（搜索框+成员列表）与群文件容器；
    // SwitchSideTab 据当前 Tab 切换二者显隐。群文件容器内含搜索 / 分类筛选 chips /
    // 文件列表滚动视口 / 分页栏 / 上传按钮，对照 design/群文件.png。
    kGIdMembersPanel   = 550,   // 成员内容容器（包搜索框 + 成员列表，成员 Tab 选中时显示）
    kGIdFilesPanel     = 551,   // 群文件内容容器（群文件 Tab 选中时显示，默认隐藏）
    kGIdFileSearch     = 552,   // 文件名搜索框（客户端按文件名过滤，服务端无此参数）
    kGIdFileChipAll    = 553,   // 分类筛选 chip：全部
    kGIdFileChipPdf    = 554,   // 分类筛选 chip：PDF
    kGIdFileChipDoc    = 555,   // 分类筛选 chip：文档
    kGIdFileChipSheet  = 556,   // 分类筛选 chip：表格
    kGIdFileChipDesign = 557,   // 分类筛选 chip：设计
    kGIdFileScroll     = 558,   // 文件列表滚动视口（balloonui DuiScrollView）
    kGIdFileList       = 559,   // 文件列表堆叠容器（GroupFileList，作滚动视口内容）
    kGIdFilePageInfo   = 560,   // 分页信息文字（"共 N · 第 x/y 页"）
    kGIdFilePagePrev   = 561,   // 上一页按钮
    kGIdFilePageNext   = 562,   // 下一页按钮
    kGIdFileUpload     = 563,   // 上传文件按钮

    // ---- 右侧边栏「公告」面板（570 段）----
    // 边栏下部现在是三个互斥的内容容器：成员 / 群文件 / 公告，SwitchSideTab 据当前
    // Tab 切换三者显隐。公告面板对照 design/群公告.png：顶部「群公告 · N 条」加
    // 「‹ x/y ›」翻页，中间一张公告卡（标题 / 正文 / 分隔线 / 发布人 + 时间）。
    // 这里<u>只读</u> —— 发布与撤回在群管理窗的「群公告」页，普通成员也进不去那里。
    kGIdNoticePanel    = 570,   // 公告内容容器（公告 Tab 选中时显示，默认隐藏）
    kGIdNoticeCount    = 571,   // 顶部「群公告 · N 条」
    kGIdNoticePrev     = 572,   // 上一条（‹）
    kGIdNoticeIndex    = 573,   // 「x/y」页码
    kGIdNoticeNext     = 574,   // 下一条（›）
    kGIdNoticeCard     = 575,   // 公告卡容器（自绘，画圆角浅底与分隔线）
    kGIdNoticeTitle    = 576,   // 公告标题
    kGIdNoticeBody     = 577,   // 公告正文（多行、自动换行）
    kGIdNoticeMeta     = 578,   // 底部「发布人 · 时间」
    kGIdNoticeEmpty    = 579,   // 空状态提示（本群还没有公告 / 正在加载）

    // ---- 头部左侧信息块 ----
    kGIdHeaderInfo     = 580,   // 群名 + (N 人) + 公告摘要行（GroupChatHeaderInfo）
};

/**
 *  构造群聊窗的客户区控件树（不含窗口边框 —— 边框由 DuiFrameWindow
 *  自身负责）。
 *    loadMock：是否用 mock 群消息 / 群成员预填消息列表与成员侧栏。真实群传
 *              false（窗口起始为空，由真实收发与 cmd_get_group_members 填充）；
 *              调试入口与视觉核对传 true。默认 true 保持既有调用不变。
 *
 *  返回：客户区根控件，调用方拿到所有权；解析失败返回 nullptr。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildGroupChatWindowBody(bool loadMock = true);

} // namespace balloonchatwnd
