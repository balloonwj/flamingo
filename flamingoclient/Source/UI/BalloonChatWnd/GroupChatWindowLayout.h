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
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

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
    kGIdInputEdit    = 501,   // 输入框（ChatInputRichEdit，无窗口富文本，支持内联图片）
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
    kGIdVoiceBar     = 541,   // 录音条（语音消息第 3b 步）：输入框后面的横向布局占位，建好后隐藏；录音时代替输入框与
                              // 底部发送行显示，控件由 CVoiceRecordBar::Build 填入
    kGIdVoiceCancel  = 542,   // 录音条上的「取消」按钮（DuiButton）
    kGIdVoiceSend    = 543,   // 录音条上的「发送」按钮（DuiButton，主色）
    kGIdInputBottomRow = 544, // 输入区底部「提示文字 + 发送按钮」那一行（横向布局）；录音时隐藏
    kGIdVoicePause   = 545,   // 录音条上的「暂停 / 继续」按钮（语音消息第 6g 步，文字随状态切换）
    kGIdVoiceStop    = 546,   // 录音条上的「停止」按钮：停止录音并进入试听
    kGIdVoicePreview = 547,   // 录音条上的「试听 / 停止试听」按钮（只在停止后的试听状态显示）
    kGIdVoiceConfirm = 548,   // 录音条上的「确认放弃」按钮（点「取消」后的确认状态显示）
    kGIdVoiceBack    = 549,   // 录音条上的「返回」按钮：不放弃，回到点「取消」之前的状态
    kGIdInputSplitter= 540,   // 左列里"消息区(上)/输入区块(下)"之间的横向可拖
                              //   分隔条，拖它即可重分两者高度；落在工具栏上边缘

    // ---- 头部右侧图标按钮 ----
    kGIdHdrVoice     = 510,   // 语音通话
    kGIdHdrVideo     = 511,   // 视频通话
    kGIdHdrHistory   = 512,   // 消息记录
    kGIdHdrAdmin     = 513,   // 群管理（齿轮）
    kGIdHdrMore      = 514,   // 更多 / 详情
    kGIdHdrScreenShare = 517, // 共享屏幕（紧挨在「视频通话」右边；点击经 FlamingoMedia.dll 弹出共享内容选择窗；模块没有加载时隐藏）
    kGIdHdrSubtitle  = 536,   // 字幕模式：紧挨在「消息记录」左边，点一下进入、再点一下退出（docs/聊天字幕模式方案.md）；
                              // 516 是作废的号（原「清空聊天记录」），不复用
    kGIdHdrShare     = 529,   // 文件共享：紧挨在「共享屏幕」右边，弹出本会话的共享列表浮层（FlamingoFileShare.dll 提供），
                              // 模块没有加载或本企业关闭了文件共享时隐藏。2026-10-03 之前在输入框工具栏（「发起接龙」左边），
                              // 按用户要求挪到头部；编号沿用原值，没有改成 51x
    kGIdHdrRelay     = 527,   // 发起接龙（写字板图标）：紧挨在「文件共享」右边，点它打开「发起接龙」窗口，见 docs/群接龙方案.md §6.7。
                              // 2026-10-03 之前在输入框工具栏（「+」左边），按用户要求挪到头部；编号沿用原值
    kGIdScreenShareBanner = 518, // 头部下方的屏幕共享横幅（ChatBannerLabel，ModeLink 可点），紧挨在「已 Pin N 条」提示带上方：
                                 // 本群有进行中的屏幕共享时显示「某某 正在共享屏幕，点击加入 ›」，点击加入；本人在共享时写
                                 // 「你正在共享屏幕」；没有共享时整条隐藏、不占位（docs/屏幕共享与录制方案.md 5.2 第 5 步）
    kGIdGroupCallBanner = 519,   // 头部下方的群通话横幅（ChatBannerLabel，ModeLink 可点），在屏幕共享横幅上方：本群有进行中的
                                 // 群通话时显示「群语音通话进行中 · N 人，点击加入 ›」，点击加入；本人在通话里时写「你正在群通话中」；
                                 // 没有群通话时整条隐藏、不占位（docs/群音视频通话方案.md 6.3）
    // 516 原为头部「清空聊天记录」按钮（扫帚），2026-10-03 起「清空聊天记录」从聊天窗移到消息记录窗（底部「导出」左边与行右键菜单），不再使用
    kGIdPinBanner    = 515,   // 头部信息行下方的「已 Pin N 条」提示带（ChatBannerLabel，
                              // ModeLink 可点）。本群没有被 Pin 的消息时整条隐藏、不占位；
                              // 点它打开消息记录窗并切到 Pin 分类。与单聊窗 kIdPinBanner 同形

    // ---- 输入区上方工具栏图标按钮 ----
    kGIdTbEmoji      = 520,   // 表情
    kGIdTbImage      = 521,   // 图片
    kGIdTbFile       = 522,   // 文件
    kGIdTbScreenshot = 523,   // 截图
    kGIdTbScreenshotMenu = 537, // 截图选项下拉按钮（▾）：紧挨在截图右边，点了弹出「截图 / 截取全屏 / 截图时隐藏当前窗口」
                              // 菜单（docs/截图快捷键与隐藏当前窗口方案.md）
    kGIdTbVoice      = 524,   // 语音
    kGIdTbPlus       = 525,   // 更多
    kGIdTbFont       = 526,   // 字体设置（AF）
    kGIdTbRecord     = 528,   // 屏幕录制：紧挨在截图右边，点了开始独立录屏，录完可一键传到本群的群文件
                              // （FlamingoMedia.dll 提供，模块没有加载时隐藏；方案原定的 527 已被「发起接龙」占用）
    kGIdTbCode       = 567,   // 发送代码：紧挨在 Markdown 开关右边，点了打开「发送代码」窗口（docs/代码片段方案.md 第 3.1 节；
                              // 564~566 已被 Markdown 模式占用）

    // ---- Markdown（docs/Markdown消息方案.md 第 11.3 节）----
    kGIdTbMarkdown    = 538,  // 工具栏的「Markdown 编辑器」按钮：紧挨在字体设置右边，打开独立的 Markdown 编辑器并预选本群；
                              // FlamingoMarkdown.dll 不可用时隐藏
    // 539、564 ~ 566、600 ~ 611 已作废（2026-10-06 去掉输入区的 Markdown 模式时删除）：原为格式条占位、预览占位、
    // 「编辑 / 预览」页签与 12 个格式按钮，不得复用

    // ---- 右侧边栏 ----
    kGIdTabMembers   = 530,   // 边栏 Tab：成员
    kGIdTabFiles     = 531,   // 边栏 Tab：群文件
    kGIdTabNotice    = 532,   // 边栏 Tab：公告
    kGIdMemberList   = 533,   // 成员列表容器
    kGIdMemberSearch = 534,   // 成员搜索框
    kGIdMemberViewToggle = 539, // 成员搜索框右边的「按部门 / 平铺」切换按钮（ChatIconButton，组织架构图标）：部门树视图下
                                // 高亮，点了切换视图（docs/群成员部门树方案.md；2026-10-07 之前是文字链接）
    kGIdTabMemo      = 535,   // 边栏 Tab：备忘（备忘与提醒，docs/备忘与提醒方案.md 1.2）
    kGIdTabQa        = 581,   // 边栏 Tab：问答（群问答，docs/群问答方案.md；530 段已满，取 581）

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

    // ---- 右侧边栏「备忘」面板（590 段）----
    // 列出本群里本人能看到的群备忘（群主、管理员看到全部，普通成员只看到提醒自己或提醒全体的）。
    // 群主、管理员可以新建、修改、删除；普通成员只能查看。数据取自备忘中枢的备忘簿。
    kGIdMemoPanel      = 590,   // 备忘内容容器（备忘 Tab 选中时显示，默认隐藏）
    //591 原为顶部「群备忘 · N 条」，2026-10-03 条数改为写在页签标题「备忘 N」上，不再使用
    kGIdMemoList       = 592,   // 备忘卡片（DuiScrollView，见 BalloonMemo/MemoCardList.h）：悬停显示「修改」「删除」，双击修改或查看，右键弹菜单
    kGIdMemoEmpty      = 593,   // 空状态提示（本群还没有群备忘 / 正在加载 / 没有加载出来），在卡片上方
    kGIdMemoAdd        = 594,   // 「＋ 新建群备忘」（只有群主、管理员可见）

    // ---- 右侧边栏「成员」页签顶部的机器人区域（620 段，docs/机器人方案.md §4.2）----
    kGIdBotSection     = 620,   // 机器人区域容器（DuiVBox）：2801 到达且本群有机器人时显示，否则隐藏
    kGIdBotTitle       = 621,   // 标题「机器人 N」
    // 622 曾是标题行右侧的「管理」链接；2026-10-08 起添加与管理群机器人的入口移到群管理窗口的「群机器人」页，该编号作废、不再复用
    kGIdBotRows        = 623,   // 机器人行的容器（DuiVBox）：每个机器人一个链接样式的 DuiLabel，点了显示资料卡
    kGIdBotRowFirst    = 630,   // 第一个机器人行的控件 id；第 i 行为 630 + i
    kGIdBotRowLast     = 689,   // 最后一个可用的机器人行控件 id（最多 60 行，超出的不显示）

    // ---- 右侧边栏「问答」面板（600 段，docs/群问答方案.md）----
    kGIdQaPanel        = 600,   // 问答内容容器（问答 Tab 选中时显示，默认隐藏）
    kGIdQaSearch       = 601,   // 搜索框：按问题或答案里的文字筛选
    kGIdQaEmpty        = 602,   // 空状态提示（正在加载 / 没有加载出来 / 本群还没有问答 / 没有找到匹配的问答）
    kGIdQaList         = 603,   // 问答卡片（DuiScrollView，见 GroupQaCardList.h）
    kGIdQaAdd          = 604,   // 「＋ 新建问答」（只有群主、管理员可见）

    // ---- 粘贴识别提示条（595 段，docs/代码片段方案.md 第 3.1 节）----
    kGIdCodePasteBar     = 595,   // 引用条与输入框之间的「看起来是一段代码」提示条（DuiVBox 套一行 DuiHBox；默认隐藏，往空输入框里
                                  // 粘贴像代码的文字时出现，继续输入、发送或点「不用了」时收起）
    kGIdCodePasteText    = 596,   // 提示条左侧的说明文字
    kGIdCodePasteSend    = 597,   // 提示条上的「以代码片段发送」链接：把输入框里的文字移进发送代码窗口
    kGIdCodePasteDismiss = 598,   // 提示条上的「不用了」链接：只收起提示条

    // ---- 仅通知模式（docs/群管理接入方案.md §14）----
    kGIdToolbarRow     = 568,   // 输入区上方的工具栏那一行（表情、图片……@）；仅通知模式下普通成员看不到
    kGIdNotifyOnlyBar  = 569,   // 仅通知模式下代替输入区显示的灰字提示「本群为仅通知模式，仅群主和管理员可以发言」，默认隐藏

    // ---- 头部左侧信息块 ----
    kGIdHeaderInfo     = 580,   // 头部左侧的群简介行（GroupChatHeaderInfo）；2026-10-03 起不再画群名与人数，
                                // 2026-10-04 起由最新公告标题改为群简介
};

// 群聊窗允许缩放到的最小整窗尺寸（逻辑像素，含标题栏；见 UI 约定第 7 条）。CBalloonGroupChatWindow 据此
// 调用 SetMinSize；布局单测按这个宽度检查头部的群简介行仍不越过右侧图标按钮。
// 2026-10-06 由 620 改为 660：输入区工具栏加了 Markdown 开关（32 像素宽、间距 2）后，620 宽时左列工具栏最右边的
// 「@」按钮越过右边缘 32 像素（据实证：当时的布局单测 MC8 实测按钮右边缘 400、工具栏右边缘 368；该用例现为
// ChatMarkdownToolbarTests 的 MT3）。
// 2026-10-06 再由 660 改为 700：工具栏又加了「代码」按钮（32 像素宽、间距 2），660 宽时只剩约 8 像素、会越界约 26 像素
// （flamingo-39 用 MC8 实测），按用户要求调大窗口时一并提高。
const int kGroupChatWindowMinWidth  = 700;
const int kGroupChatWindowMinHeight = 440;

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

/**
 *  群聊窗屏幕共享横幅（kGIdScreenShareBanner）上应显示的文字。纯函数，便于单元测试直接断言文案。
 *    selfIsHost：进行中的共享是不是本人发起的；是的话只告知、不提示加入。
 *    hostName：发起人显示名；为空时写「有人」。
 *    locked：加入是否需要密码（第 12 期）；是的话在「正在共享屏幕」后标出「（需要密码）」，本人发起时不标。
 *    forced：是否强制模式（第 13 期）；第 14 期起在「点击加入 ›」之后写明强制模式的效果。
 *    title：共享标题（第 15 期）；有标题时写成「张三 正在共享：周会演示，点击加入 ›」、本人发起时写「你正在共享：周会演示」，
 *           为空（老服务端）时写法与第 14 期相同。
 *  返回：横幅文字。没有进行中的共享时调用方不调本函数，直接传空串给 UpdateScreenShareBanner 收起横幅。
 */
std::wstring MakeScreenShareBannerText(bool selfIsHost, const std::wstring& hostName, bool locked = false,
                                       bool forced = false, const std::wstring& title = std::wstring());

/**
 *  刷新群聊窗屏幕共享横幅：有文字就显示并写上，文字为空就整条收起。
 *    root：窗口客户区根控件；传 NULL 直接返回 false。所有权不转移。
 *    text：横幅文字（见 MakeScreenShareBannerText）；空串表示本群没有进行中的共享。
 *  返回：横幅的<u>显隐</u>是否发生了变化。为 true 时调用方必须补一次布局（UI 约定第 5 条）。
 */
bool UpdateScreenShareBanner(balloonwjui::DuiControl* root, const std::wstring& text);

/**
 *  群聊窗群通话横幅（kGIdGroupCallBanner）上应显示的文字。纯函数，便于单元测试直接断言文案。
 *    selfInCall：本人是否已在这场群通话里；是的话只告知、不提示加入。
 *    video：是否群视频（决定写「群视频通话」还是「群语音通话」）。
 *    joinedCount：已加入人数；不大于 0 时不写人数。
 *  返回：横幅文字。没有进行中的群通话时调用方不调本函数，直接传空串给 UpdateGroupCallBanner 收起横幅。
 */
std::wstring MakeGroupCallBannerText(bool selfInCall, bool video, int joinedCount);

/**
 *  刷新群聊窗群通话横幅：有文字就显示并写上，文字为空就整条收起。
 *    root：窗口客户区根控件；传 NULL 直接返回 false。所有权不转移。
 *    text：横幅文字（见 MakeGroupCallBannerText）；空串表示本群没有进行中的群通话。
 *  返回：横幅的<u>显隐</u>是否发生了变化。为 true 时调用方必须补一次布局（UI 约定第 5 条）。
 */
bool UpdateGroupCallBanner(balloonwjui::DuiControl* root, const std::wstring& text);

/**
 *  按右栏四个 Tab（成员 / 群文件 / 公告 / 备忘）当前文字的实际宽度分配它们的宽度（多语言第 4 期）。
 *  右栏只有 240 像素，四等分时英文等译文放不下；简体中文界面保持原设计的四等分，不做任何改动。
 *  Tab 文字（含数量）变化后调用，例如 CBalloonGroupChatWindow::RefreshSideTabCounts 的末尾。
 *    root：窗口客户区根控件；传 NULL 直接返回。所有权不转移。
 */
void ApplyGroupSideTabWeights(balloonwjui::DuiControl* root);

/**
 *  按仅通知模式切换输入区（docs/群管理接入方案.md §14）：notifyOnly 为 true 时隐藏工具栏、引用条、粘贴识别提示条、
 *  输入框、录音条与底部「提示 + 发送」行，只显示灰字提示行 kGIdNotifyOnlyBar；为 false 时恢复工具栏、输入框与底部行，
 *  隐藏提示行（引用条、粘贴识别提示条、录音条都是临时出现的，恢复时保持隐藏）。显隐变化后对输入区块 ForceLayout
 *  （开发约定文档 UI 约定 5），输入框里没发出的草稿不受影响。
 *    root：      群聊窗客户区根控件（BuildGroupChatWindowBody 的返回值）；为 NULL 时什么都不做。
 *    notifyOnly：本人此刻是否不能发言（CanSpeakInGroup 为 false）。
 *  @return 输入区块找到并完成切换返回 true。
 */
bool ApplyNotifyOnlyInput(balloonwjui::DuiControl* root, bool notifyOnly);

/**
 *  按成员列表当前的视图设置「按部门 / 平铺」切换按钮（docs/群成员部门树方案.md）：部门树视图下高亮（图标变蓝），
 *  悬停提示为「平铺显示」；平铺视图下不高亮，悬停提示为「按部门显示」。提示说明的是点了之后切到哪种视图。
 *    root：    群聊窗客户区根控件；为 NULL 或找不到按钮时什么都不做。
 *    treeMode：成员列表当前是否为部门树视图。
 *  @return 找到按钮并完成设置返回 true。
 */
bool ApplyMemberViewToggle(balloonwjui::DuiControl* root, bool treeMode);

} // namespace balloonchatwnd
