/**
 *  当前登录账号的统一取值口，CurrentUserAccount.h
 *
 *  本地缓存目录（Users\<登录账号>\ChatFile / ChatImage / Avatar）要用登录账号来分，
 *  而需要它的地方散布在聊天窗、群聊窗、主面板、历史窗、搜索浮窗等处，各自都从
 *  自己手里那个 CFlamingoClient 指针里取一遍、还各写一遍空指针判断。本函数把这个
 *  取值收敛到一处，调用方直接写 net::CurrentUserAccount() 即可。
 *
 *  与 UserCachePath.h 的分工：那边是纯函数（拼路径，可单测），本文件依赖全局登录
 *  态，故单独成文件，不去污染那一份的可测性。
 *
 *  典型用法：
 *      std::wstring path = net::ImageTransfer::CacheFilePathForMd5(
 *                              net::CurrentUserAccount(), md5);
 *
 *  balloonwj@qq.com   2026-08-11
 */

#pragma once

#include <string>

namespace net {

/**
 *  取当前登录账号（形如 MY0309 / EMP5321）。
 *  尚未登录时返回空串 —— 各缓存路径函数收到空账号一律返回空串，调用方据此
 *  判定为"没有缓存"，不会把文件写到 Users 目录之外去。
 *  @return 当前登录账号；未登录返回空串。
 */
std::wstring CurrentUserAccount();

} // namespace net
