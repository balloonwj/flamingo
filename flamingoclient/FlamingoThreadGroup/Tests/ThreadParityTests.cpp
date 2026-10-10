/**
 *  帖子群 DLL 协议常量的一致性测试（标签 [threaddll]）：
 *    - TGD15 DLL 不包含主程序的协议头，在 Core/ThreadTypes.h 里重复了一份命令号、变化类型、排序、目标类型、
 *            角色与错误码；这里逐项与 Source/net/Protocol.h 核对，任何一边改了号另一边没跟上时失败。
 *  本文件同时包含两边的头文件，全局枚举与 threadgroup 命名空间里的同名枚举一律写全限定名。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "catch_amalgamated.hpp"

#include "../Core/ThreadTypes.h"
#include "../../Source/net/Protocol.h"

TEST_CASE("TGD15 DLL 里重复的一份协议常量与 Protocol.h 一致", "[threaddll]")
{
    CHECK(static_cast<int>(threadgroup::thread_cmd_list_posts) == static_cast<int>(cmd_thread_list_posts));
    CHECK(static_cast<int>(threadgroup::thread_cmd_get_post) == static_cast<int>(cmd_thread_get_post));
    CHECK(static_cast<int>(threadgroup::thread_cmd_publish_post) == static_cast<int>(cmd_thread_publish_post));
    CHECK(static_cast<int>(threadgroup::thread_cmd_edit_post) == static_cast<int>(cmd_thread_edit_post));
    CHECK(static_cast<int>(threadgroup::thread_cmd_delete_post) == static_cast<int>(cmd_thread_delete_post));
    CHECK(static_cast<int>(threadgroup::thread_cmd_pin_post) == static_cast<int>(cmd_thread_pin_post));
    CHECK(static_cast<int>(threadgroup::thread_cmd_list_comments) == static_cast<int>(cmd_thread_list_comments));
    CHECK(static_cast<int>(threadgroup::thread_cmd_list_replies) == static_cast<int>(cmd_thread_list_replies));
    CHECK(static_cast<int>(threadgroup::thread_cmd_add_comment) == static_cast<int>(cmd_thread_add_comment));
    CHECK(static_cast<int>(threadgroup::thread_cmd_delete_comment) == static_cast<int>(cmd_thread_delete_comment));
    CHECK(static_cast<int>(threadgroup::thread_cmd_react) == static_cast<int>(cmd_thread_react));
    CHECK(static_cast<int>(threadgroup::thread_cmd_get_reactors) == static_cast<int>(cmd_thread_get_reactors));
    CHECK(static_cast<int>(threadgroup::thread_cmd_watch) == static_cast<int>(cmd_thread_watch));
    CHECK(static_cast<int>(threadgroup::thread_cmd_add_addendum) == static_cast<int>(cmd_thread_add_addendum));
    CHECK(static_cast<int>(threadgroup::thread_cmd_delete_addendum) == static_cast<int>(cmd_thread_delete_addendum));
    CHECK(static_cast<int>(threadgroup::thread_cmd_changed_notify) == static_cast<int>(cmd_thread_changed_notify));

    CHECK(static_cast<int>(threadgroup::thread_change_post_new) == static_cast<int>(::thread_change_post_new));
    CHECK(static_cast<int>(threadgroup::thread_change_reaction) == static_cast<int>(::thread_change_reaction));
    CHECK(static_cast<int>(threadgroup::thread_change_comment_delete) == static_cast<int>(::thread_change_comment_delete));
    CHECK(static_cast<int>(threadgroup::thread_sort_activity) == static_cast<int>(::thread_sort_activity));
    CHECK(static_cast<int>(threadgroup::thread_sort_publish) == static_cast<int>(::thread_sort_publish));
    CHECK(static_cast<int>(threadgroup::thread_target_post) == static_cast<int>(::thread_target_post));
    CHECK(static_cast<int>(threadgroup::thread_target_comment) == static_cast<int>(::thread_target_comment));
    CHECK(static_cast<int>(threadgroup::thread_role_admin) == static_cast<int>(group_role_admin));
    CHECK(static_cast<int>(threadgroup::thread_role_owner) == static_cast<int>(group_role_owner));

    CHECK(static_cast<int>(threadgroup::thread_error_disabled) == static_cast<int>(error_code_thread_disabled));
    CHECK(static_cast<int>(threadgroup::thread_error_not_exist) == static_cast<int>(error_code_thread_not_exist));
    CHECK(static_cast<int>(threadgroup::thread_error_no_permission) == static_cast<int>(error_code_thread_no_permission));
    CHECK(static_cast<int>(threadgroup::thread_error_invalid) == static_cast<int>(error_code_thread_invalid));
    CHECK(static_cast<int>(threadgroup::thread_error_pin_full) == static_cast<int>(error_code_thread_pin_full));
    CHECK(static_cast<int>(threadgroup::thread_error_not_thread_group) == static_cast<int>(error_code_thread_not_thread_group));
    CHECK(static_cast<int>(threadgroup::thread_error_addendum_full) == static_cast<int>(error_code_thread_addendum_full));
    CHECK(static_cast<int>(threadgroup::thread_error_member_muted) == static_cast<int>(error_code_member_muted));
    CHECK(static_cast<int>(threadgroup::thread_error_sensitive_word) == static_cast<int>(error_code_group_sensitive_word));
    CHECK(static_cast<int>(threadgroup::thread_error_must_read_pending) == static_cast<int>(error_code_must_read_pending));
    CHECK(static_cast<int>(threadgroup::thread_error_not_group_member) == static_cast<int>(error_code_not_group_member));
}
