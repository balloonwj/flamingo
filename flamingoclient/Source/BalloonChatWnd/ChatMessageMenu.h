/**
 *  ChatMessageMenu.h
 *
 *  聊天气泡（ChatBubble）自身右键菜单的<u>纯模型</u>部分：给定这条消息是什么类型、
 *  图片是否已就绪、还能不能撤回，算出菜单该有哪几行。不碰 HWND、不建真菜单，
 *  便于被 FlamingoUnit 测试工程直接调用断言。
 *
 *  真正弹菜单、把选中项分发出去在 ChatBubble 与宿主聊天窗那边做；本文件只回答
 *  「这个菜单长什么样」。写法对齐同目录的 GroupMentionMenu —— 菜单模型一律做成
 *  纯函数并配测试，避免菜单逻辑散落在窗口代码里没法验证。
 *
 *  行为约定（与用户确认过）：
 *    · 图片消息且本地文件已就绪 → 在「复制」之后插入「图片另存为」「再次发送」。
 *    · 图片未就绪（上传中 / 下载中 / 失败 / 没有本地路径）→ 这两项<u>不显示</u>，
 *      而不是置灰。与「撤回」超时即整项消失是同一种处理，菜单里不留点不动的项。
 *    · 「再次发送」自己发的图与收到的图都给 —— 只要本地有文件就能再发一遍，
 *      语义是「把这张图作为一条新消息再发到当前会话」，不是重发原来那条。
 *    · 「Pin / 取消 Pin」文案随本条当前是否已钉住取反，位置在「转发」之后、第一条
 *      分隔线之前。服务器还没给这条消息分配 msgid 时（本端刚发出）整项<u>不出现</u>，
 *      与「撤回」超时即整项消失同一种处理。
 *    · 取消 Pin 的权限<u>不在客户端预判</u>：本端不一定知道一条历史消息是谁钉的
 *      （operator 只随推送来、历史行未必有），预判会出现"该显示的没显示"。故菜单项
 *      一律给出，无权时由服务端以 error_code_pin_not_allowed 拒绝并提示。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#pragma once

#include <vector>
#include <string>

namespace balloonchatwnd {

/**
 *  菜单项标识。分发时按它 switch，不要按行下标 —— 行数会随消息类型与图片状态变化。
 */
enum ChatMessageMenuId
{
    kChatMsgMenuCopy        = 1,   // 复制：有选区时复制选中那段，无选区时复制整条
    kChatMsgMenuSaveImageAs = 2,   // 图片另存为：把本地图片文件另存到用户选定位置（仅图片行出现）
    kChatMsgMenuResendImage = 3,   // 再次发送：把本地图片作为一条新的图片消息再发一遍（仅图片行出现）
    kChatMsgMenuReply       = 4,   // 引用回复：在输入区挂出对本条的引用条
    kChatMsgMenuForward     = 5,   // 转发：把本条转给别的会话
    kChatMsgMenuRecall      = 6,   // 撤回：仅自己发出、且仍在可撤回时限内时出现
    kChatMsgMenuDelete      = 7,   // 删除：只从本端列表移除这一行
    kChatMsgMenuMultiSelect = 8,   // 多选：以本行为起点进入多选模式
    kChatMsgMenuPin         = 9,   // Pin / 取消 Pin：把本条钉在会话里（会话内共享，对方与全体群成员都看得到）
};

/**
 *  菜单里的一行。
 */
struct ChatMessageMenuEntry
{
    ChatMessageMenuId id;           // 本行的标识，分发时用；分隔线该字段无意义
    const wchar_t*    label;        // 显示文字；指向静态常量串，生命周期为进程级
    bool              isSeparator;  // true 表示这一行是分隔线，其余字段无意义

    ChatMessageMenuEntry()
        : id(kChatMsgMenuCopy)
        , label(L"")
        , isSeparator(false)
    {
    }
};

//图片消息的 kind 取值。与 ChatMockData.h 里 ChatMsgKind 的 kMsgImage 必须一致 ——
//那个枚举所在的头文件依赖 ATL 的 CString，本模型要保持纯净故不引它，改在这里
//单独取一份名字。两处哪天不一致，ChatImageMenuTests 的 CIM1 会立刻失败。
const int kChatMenuKindImage = 3;

//图片就绪态。与 ChatMessage::imageState 的取值一一对应（见 ChatMockData.h，那边是
//裸数字加注释），这里给它们名字，供本模型与其测试引用。
const int kChatImageStateReady       = 0;   // 正常 / 已就绪：本地文件可用，两个图片项才出现
const int kChatImageStateUploading   = 1;   // 上传中：本地虽有文件，但这条消息还没发成功
const int kChatImageStateFailed      = 2;   // 失败：上传或下载失败
const int kChatImageStateDownloading = 3;   // 下载中：本地还没有文件

/**
 *  判断某条消息的右键菜单里该不该出现「图片另存为」「再次发送」两项。
 *
 *  三个条件缺一不可：是图片行、图片已就绪、本地确实有文件路径。之所以要单独
 *  判 hasLocalPath —— 收到的图在下载完成前 imagePath 是空的，而"下载完成"与
 *  "状态置就绪"是两次不同的赋值，只信状态会漏掉中间那一瞬。
 *
 *    msgKind：      消息类型，取值见 ChatMockData.h 的 ChatMsgKind。
 *    imageState：   图片就绪态，取值见上面的 kChatImageState* 常量。
 *    hasLocalPath： 该条消息的 imagePath 是否非空。
 *  @return 该出现返回 true。
 */
bool ShouldShowImageActions(int msgKind, int imageState, bool hasLocalPath);

/**
 *  算出气泡右键菜单的行模型。
 *
 *  固定骨架是：复制 / 引用回复 / 转发 / [Pin] / 分隔线 / [撤回] / 删除 / 分隔线 / 多选。
 *  图片行且已就绪时，在「复制」之后插入「图片另存为」「再次发送」两项。
 *
 *    msgKind：      消息类型，取值见 ChatMockData.h 的 ChatMsgKind。
 *    imageState：   图片就绪态，取值见 kChatImageState*；非图片行忽略此参数。
 *    hasLocalPath： 该条消息的 imagePath 是否非空；非图片行忽略此参数。
 *    canRecall：    本条现在能不能撤回（自己发出 + 未超时，由调用方判定）。
 *    canPin：       本条现在能不能 Pin —— 即服务器是否已给它分配 msgid。为 false 时
 *                   整项不出现（本端刚发出、回执还没回填的那一瞬）。
 *    pinned：       本条当前是否已被钉住，决定这一项的文案是「Pin」还是「取消 Pin」。
 *
 *  @return 行模型（值语义，调用方拥有）。
 */
std::vector<ChatMessageMenuEntry> BuildChatMessageMenuModel(int msgKind,
                                                            int imageState,
                                                            bool hasLocalPath,
                                                            bool canRecall,
                                                            bool canPin,
                                                            bool pinned);

/**
 *  算「图片另存为」对话框里的默认文件名。
 *
 *  自己发出的图，imagePath 就是用户当初选的那个源文件，文件名有意义（如
 *  photo.jpg），直接沿用；收到的图落在下载缓存里、文件名是消息的 md5 且<u>不带
 *  扩展名</u>（见 net/ImageTransfer.cpp 的 buildImageCachePath），拿它当默认名对
 *  用户毫无意义，故换成 IMG_<msgid>.png。
 *
 *  判据取"文件名里有没有扩展名"而不是"是不是自己发的"：翻历史时自己发的图同样
 *  是从缓存下载回来的，按 fromMe 分反而会错。
 *
 *  兜底扩展名固定用 .png：缓存文件是原始字节，本函数拿不到真实格式；另存为本身
 *  按字节整体拷贝、不会改动内容，这个扩展名只是给用户的一个建议，可在对话框里改。
 *
 *    imagePath： 图片本地文件完整路径，可为空。
 *    msgid：     该条消息的服务器 msgid，用于拼兜底文件名；<=0 时省略这一段。
 *  @return 默认文件名（不含目录）。
 */
std::wstring MakeImageSaveAsDefaultName(const std::wstring& imagePath, long long msgid);

/**
 *  「图片另存为」「再次发送」选中后投递给宿主聊天窗的负载
 *  （WM_CHAT_IMAGE_ACTION 的 lParam）。
 *
 *  为什么要中转：气泡是通用控件、单聊群聊共用，它既不认识"当前会话是谁"也不该
 *  持有发送链路；而「再次发送」要调宿主窗的 SendImageFile、「另存为」要弹模态
 *  文件对话框。故气泡只负责弹菜单，把"哪张图 + 选了哪一项"打包投给宿主窗执行
 *  —— 与文件气泡按钮的 ChatFileActionData 是同一套路子。
 *
 *  生命周期：调用方在堆上 new 出来随 PostMessage 投出；宿主窗处理完负责 delete。
 *  投递失败（窗口已销毁）时由投递方自行 delete，不得泄漏。
 */
struct ChatImageActionData
{
    ChatMessageMenuId id;         // 用户选中的菜单项（另存为 / 再次发送）
    std::wstring      imagePath;  // 图片本地文件完整路径
    std::wstring      saveName;   // 另存为对话框的默认文件名，已由 MakeImageSaveAsDefaultName 算好

    ChatImageActionData()
        : id(kChatMsgMenuSaveImageAs)
    {
    }
};

} // namespace balloonchatwnd
