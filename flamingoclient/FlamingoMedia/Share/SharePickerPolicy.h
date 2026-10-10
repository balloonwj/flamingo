/**
 *  屏幕共享「选择要共享的内容」窗口的纯逻辑规则，SharePickerPolicy.h
 *
 *  第 12 期给选择窗加了两项能力（docs/屏幕共享与录制方案.md 第 12 期）：
 *    - 可选的 6 位数字加入密码：勾选「设置密码」时自动生成一个，发起人可以改；只有发起共享时提供，
 *      切换内容、录屏、通话中发起都不提供；勾了密码而密码不是 6 位数字时不能点「开始共享」；
 *    - 「刷新」按钮：重新列出显示器与窗口，原先选中的内容还在就继续选中。
 *  这些判断抽成不依赖窗口与控件的纯函数，便于单元测试，见 flamingoclient/Source/Tests/SharePickerPolicyTests.cpp；
 *  选择窗本身在 SharePickerDialog.h。
 *
 *  典型用法：
 *      if (screenshare::SharePickerOffersPassword(mode)) 显示「设置密码」一行;
 *      okButton->SetEnabled(screenshare::SharePickerCanSubmit(selected >= 0, pwdCheck->IsChecked(), pwdEdit->GetText()));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace screenshare {

//选择窗的用途
enum SharePickerMode
{
    share_picker_start  = 0,   //发起共享
    share_picker_switch = 1,   //共享过程中切换内容
    share_picker_record = 2,   //独立录屏选择录制范围
    share_picker_start_in_call = 3,   //一对一通话中发起共享：声音经由通话传输，不显示「同时开启麦克风」
};

//共享密码的位数（与服务端 ScreenShareRules::isValidPassword 一致）
const size_t kSharePasswordLength = 6;

/**
 *  判断是否为合法的共享密码：恰好 6 个 ASCII 数字（全角数字、空格、字母都不算）。
 *  @param text 密码框里的文字
 */
bool IsValidSharePassword(const std::wstring& text);

/**
 *  过滤密码框的输入：只保留 ASCII 数字，超过 6 位的部分截掉。
 *  @param text 密码框当前的文字
 *  @return 过滤后的文字；与输入相同表示无需改动
 */
std::wstring FilterSharePasswordInput(const std::wstring& text);

/**
 *  由一个随机数得到密码：取随机数十进制的末 6 位，不足 6 位前面补 0。
 *  @param randomValue 随机数，任意取值
 *  @return 6 位数字的密码
 */
std::wstring MakeSharePassword(uint32_t randomValue);

/**
 *  自动生成一个随机的 6 位数字密码（用系统的加密随机数 rand_s；取不到时退回按时间计数生成）。
 */
std::wstring GenerateSharePassword();

/**
 *  把密码转成协议里用的 UTF-8 字符串。合法密码只含 ASCII 数字，逐字节转换即可。
 *  @param password 密码；为空表示不设密码，返回空串
 */
std::string SharePasswordToUtf8(const std::wstring& password);

/**
 *  选择窗是否提供「设置密码」：只有发起共享（share_picker_start）时提供。
 *  @param mode 选择窗用途，取值见 SharePickerMode
 */
bool SharePickerOffersPassword(int mode);

/**
 *  选择窗是否提供「强制模式」（第 13 期）：只有发起共享（share_picker_start）时提供。
 *  @param mode 选择窗用途，取值见 SharePickerMode
 */
bool SharePickerOffersForced(int mode);

/**
 *  选择窗是否提供「禁止观看者录制」（第 13 期）：只有发起共享（share_picker_start）时提供；共享中可在控制条「更多」里改。
 *  @param mode 选择窗用途，取值见 SharePickerMode
 */
bool SharePickerOffersRecordBlock(int mode);

/**
 *  选择窗是否提供共享标题与描述两个输入框（第 15 期）：发起共享（share_picker_start）与通话中发起（share_picker_start_in_call）
 *  时提供；切换内容与录屏不提供（标题在共享中可在控制条「更多」里改）。
 *  @param mode 选择窗用途，取值见 SharePickerMode
 */
bool SharePickerOffersInfo(int mode);

/**
 *  选择窗是否可以有字幕勾选项（语音字幕）：录制时为「生成字幕」；发起共享（含通话中发起）时为「为我的发言生成字幕」
 *  （2026-10-07）。切换内容时不提供。实际是否显示还要看字幕功能是否可用（SetCaptionOptions）。
 *  @param mode 选择窗用途，取值见 SharePickerMode
 */
bool SharePickerOffersCaption(int mode);

/**
 *  「开始共享」按钮能否点。
 *  @param hasSelection 是否选中了某个屏幕或窗口
 *  @param passwordOn   是否勾了「设置密码」
 *  @param password     密码框里的文字；没勾「设置密码」时不看
 *  @return 选中了内容，且没勾密码或密码是 6 位数字时返回 true
 */
bool SharePickerCanSubmit(bool hasSelection, bool passwordOn, const std::wstring& password);

/**
 *  录制模式的「只录声音」按钮能否点（第 24 期）：不用选画面，但「录制麦克风」「录制电脑声音」至少要勾一项。
 *  @param mic    是否勾了「录制麦克风」
 *  @param system 是否勾了「录制电脑声音」
 */
bool SharePickerCanRecordAudioOnly(bool mic, bool system);

//刷新列表前后用来认出「同一个共享内容」的标识
struct ShareSourceKey
{
    bool m_monitor;   //true 为显示器，false 为窗口
    int  m_adapter;   //显示器：DXGI 显卡序号
    int  m_output;    //显示器：输出序号
    HWND m_hwnd;      //窗口：句柄；显示器项为 NULL

    ShareSourceKey();
};

/**
 *  在刷新后的列表里找刷新前选中的那一项：显示器按显卡与输出序号认，窗口按句柄认。
 *  @param previous 刷新前选中项的标识
 *  @param items    刷新后的全部可选项，顺序与选择窗的 m_items 一致
 *  @return 找到时返回它在 items 里的下标；不在了（如窗口已关闭）、或 previous 是句柄为 NULL 的窗口项时返回 -1
 */
int FindShareSourceAfterRefresh(const ShareSourceKey& previous, const std::vector<ShareSourceKey>& items);

} // namespace screenshare
