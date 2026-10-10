/**
 *  AutoReplySync.h
 *
 *  「离开 / 忙碌自动回复」迁移到服务端（docs/机器人方案.md §11 Q2）的纯逻辑：本机设置与服务端设置（2810 / 2811）
 *  之间的换算，以及本机还要不要自己自动回复。
 *
 *  迁移规则：
 *    · 登录后拉一次服务端设置（2810）。服务端还没有这个人的设置、本机又改过默认值时，把本机设置上传一次（2811）；
 *      服务端已有设置时以服务端为准覆盖本机，多台电脑的设置因此一致。
 *    · 设置页改了开关或文字，存盘的同时上传一次。
 *    · 企业打开了机器人（features.bot）时由服务端代答，本机不再自动回复；企业关闭了机器人、或服务端是不认识机器人的
 *      老版本时，退回本机自动回复，与改造前相同。
 *
 *  不碰窗口与网络，单测见 Source/Tests/BotClientTests.cpp（标签 [bot]）。
 *
 *  典型用法：
 *      net::AutoReplyWire w = balloonmain::BuildAutoReplyWire(awayOn, awayText, busyOn, busyText);
 *      CIUSocket::GetInstance().SendSetAutoReply(w);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <string>

#include "../../net/BotProtocolClient.h"   // net::AutoReplyWire

namespace balloonmain {

/**
 *  本机设置 → 2811 的请求内容。文字为空时填入当前界面语言的默认文字，并把 default 标记置 1。
 *    awayOn / awayText：离开时的开关与文字（用户设置的原文，可为空）。
 *    busyOn / busyText：忙碌时的开关与文字。
 *  @return 请求内容（文字为 UTF-8）
 */
net::AutoReplyWire BuildAutoReplyWire(bool awayOn, const std::wstring& awayText, bool busyOn, const std::wstring& busyText);

/**
 *  本机设置是不是从没改过（两个开关都关、两段文字都为空）。是的话首次登录不必上传。
 */
bool IsDefaultAutoReply(bool awayOn, const std::wstring& awayText, bool busyOn, const std::wstring& busyText);

/**
 *  服务端设置 → 本机配置要写的值。服务端标为默认文字的，本机文字写空（设置页显示空框，表示用默认文字）。
 *    w：2810 应答里的设置。
 *    awayOn / awayText / busyOn / busyText：[出参] 本机配置的值。
 */
void ApplyServerAutoReply(const net::AutoReplyWire& w, bool& awayOn, std::wstring& awayText, bool& busyOn,
                          std::wstring& busyText);

/**
 *  本机还要不要自己自动回复。
 *    botFeatureOn：本人所属企业是否打开了机器人（登录 features.bot 与 1013）。
 *  @return 企业打开了机器人时返回 false（由服务端代答）；否则返回 true（退回本机自动回复）
 */
bool ShouldClientAutoReply(bool botFeatureOn);

} // namespace balloonmain
