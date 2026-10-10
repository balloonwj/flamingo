/**
 *  GroupQaModel.h
 *
 *  群聊窗「问答」页签（docs/群问答方案.md）的纯逻辑：按关键字筛选、从答案里找出网址、拼「发到群里」的文字、
 *  新增与修改前的本地校验、谁能修改哪一条、空状态提示。不依赖窗口与控件，单测见 Source/Tests/GroupQaTests.cpp。
 *
 *  典型用法：
 *      std::vector<size_t> shown = FilterGroupQa(items, keyword);
 *      std::vector<std::wstring> urls = ExtractQaUrls(answer, kQaMaxLinksPerCard);
 *      std::wstring text = BuildQaSendText(question, answer);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

#include "../../net/GroupQaProtocolClient.h"   // net::GroupQaItem

namespace balloonchatwnd {

// 问题最多多少个字符（按 Unicode 字符计），与服务端 kGroupQaQuestionMaxChars 一致
const size_t kQaQuestionMaxChars = 128;
// 答案最多多少个字符，与服务端 kGroupQaAnswerMaxChars 一致
const size_t kQaAnswerMaxChars = 2048;
// 每张卡片最多列出几个网址链接，其余的仍在答案原文里
const size_t kQaMaxLinksPerCard = 8;

// 新增或修改问答前的本地校验结果
enum QaInputCheck
{
    // 可以提交
    kQaInputOk = 0,
    // 问题为空（只有空白也算空）
    kQaInputEmptyQuestion = 1,
    // 答案为空（只有空白也算空）
    kQaInputEmptyAnswer = 2,
    // 问题超过 kQaQuestionMaxChars 个字符
    kQaInputQuestionTooLong = 3,
    // 答案超过 kQaAnswerMaxChars 个字符
    kQaInputAnswerTooLong = 4
};

// 卡片里各控件 ctrlId 的分段：第 i 张卡片（从 0 起）占 [kQaCardIdBase + i × kQaCardIdStride,
// kQaCardIdBase + (i + 1) × kQaCardIdStride)。备忘卡片占 20000～27999，问答卡片从 28000 起，群聊窗里没有别的控件使用
const unsigned int kQaCardIdBase = 28000;
const unsigned int kQaCardIdStride = 16;
// 最多建这么多张卡片（占用 id 28000～32799）；超出的不显示，可用搜索框找到
const int kQaCardMaxCount = 300;

// 问答卡片里的各个部分，ctrlId = kQaCardIdBase + 序号 × kQaCardIdStride + 部分
enum QaCardPart
{
    // 卡片底板（DuiVBox）：圆角浅底
    kQaCardPartBody = 0,
    // 问题（DuiLabel，加粗、自动换行）
    kQaCardPartQuestion = 1,
    // 来源与修改人一行（DuiLabel，灰字）
    kQaCardPartMeta = 2,
    // 答案（DuiLabel，自动换行）
    kQaCardPartAnswer = 3,
    // 底部按钮一行（DuiHBox）
    kQaCardPartActions = 4,
    // 「发到群里」（DuiButton）
    kQaCardPartSend = 5,
    // 「修改」（DuiButton）：只有能修改的卡片才有
    kQaCardPartEdit = 6,
    // 「删除」（DuiButton）：同上
    kQaCardPartDelete = 7,
    // 第一个网址链接（DuiLabel 链接模式）；第 k 个为 kQaCardPartLinkFirst + k，最多 kQaMaxLinksPerCard 个
    kQaCardPartLinkFirst = 8
};

/**
 *  第 index 张卡片的 part 部分的 ctrlId。
 */
unsigned int QaCardCtrlId(int index, int part);

/**
 *  从通知的 ctrlId 反推卡片序号与部分。
 *    ctrlId：DuiNotify 里的 ctrlId；index：[出参] 第几张卡片，从 0 起；part：[出参] QaCardPart 的取值。
 *  @return ctrlId 落在问答卡片的 id 段内返回 true；否则返回 false，出参不变
 */
bool QaCardFromCtrlId(unsigned int ctrlId, int& index, int& part);

// 「问答」面板的加载状态，决定空状态提示
enum QaPanelState
{
    // 还没拉过或正在拉
    kQaPanelLoading = 0,
    // 拉取成功
    kQaPanelLoaded = 1,
    // 拉取失败（网络或服务端错误）
    kQaPanelFailed = 2
};

/**
 *  按关键字筛选问答：问题或答案里包含关键字（英文不区分大小写）的条目入选，保持原顺序。
 *    items：  全部问答（UTF-8）。
 *    keyword：搜索框里的文字；去掉首尾空白后为空表示不筛选，全部入选。
 *  @return 入选条目在 items 里的下标
 */
std::vector<size_t> FilterGroupQa(const std::vector<net::GroupQaItem>& items, const std::wstring& keyword);

/**
 *  从答案文字里找出 http:// 与 https:// 开头的网址，按出现顺序、去重，最多 maxCount 个。
 *  网址在空白、引号、尖括号、中文字符与中文标点处结束；末尾的英文句末标点（. , ; : ! ? ) ]）不算在网址里。
 *  只认这两种协议：问答内容由群主维护，别的协议（file:、javascript: 等）点开有风险。
 */
std::vector<std::wstring> ExtractQaUrls(const std::wstring& text, size_t maxCount);

/**
 *  「发到群里」时发出去的文字：「问：<问题>」换行「答：<答案>」。
 */
std::wstring BuildQaSendText(const std::wstring& question, const std::wstring& answer);

/**
 *  新增或修改问答前的本地校验（服务端还会再查一遍）。问题与答案先去掉首尾空白再判断。
 *  @return 取值见 QaInputCheck
 */
int CheckQaInput(const std::wstring& question, const std::wstring& answer);

/**
 *  校验结果对应的提示文字（已翻译）；kQaInputOk 返回空串。
 */
std::wstring QaInputCheckText(int check);

/**
 *  本人能不能修改、删除这一条：只有群主与管理员，且只能改本群问答（企业问答在管理控制台维护）。
 *    myRole：本人角色，取值见 group_member_role，负值表示未知；
 *    source：条目来源，取值见 net::GroupQaSource。
 */
bool CanEditQaItem(int myRole, int source);

/**
 *  本人能不能新增问答：群主与管理员。
 */
bool CanAddQa(int myRole);

/**
 *  面板里的空状态提示（已翻译）：正在加载、没有加载出来、本群还没有问答、没有找到匹配的问答；
 *  有内容可显示时返回空串，面板据此隐藏提示行。
 *    state：      取值见 QaPanelState；
 *    totalCount： 全部问答条数；
 *    shownCount： 筛选后显示的条数；
 *    hasKeyword： 搜索框里是否有关键字。
 */
std::wstring QaEmptyHintText(int state, size_t totalCount, size_t shownCount, bool hasKeyword);

/**
 *  去掉首尾空白（空格、制表符、回车换行、全角空格）。
 */
std::wstring TrimQaText(const std::wstring& text);

/**
 *  按 Unicode 字符计数（代理对算一个字符），与服务端按 UTF-8 字符计数的口径一致。
 */
size_t CountQaChars(const std::wstring& text);

} // namespace balloonchatwnd
