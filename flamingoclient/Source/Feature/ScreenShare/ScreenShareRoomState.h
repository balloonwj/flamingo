/**
 *  ScreenShareRoomState.h
 *
 *  屏幕共享卡片所指的那一场共享，在本客户端看来处于什么状态。exe 侧的共享状态缓存（ScreenShareStatusCache）
 *  按它回答「这张卡片的共享还在不在」，聊天窗按它决定卡片的状态文字与「加入」按钮是否可点。
 *  单独成一个头文件，是为了让界面层（ChatScreenShareCard）与状态层（ScreenShareStatusCache）互不包含对方。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

//屏幕共享房间在本客户端看来的状态
enum ScreenShareRoomState
{
    kScreenShareRoomUnknown = 0,   //还不知道：本次登录还没有收到这个会话的开始推送、结束推送或查询应答
    kScreenShareRoomActive  = 1,   //正在共享：这个会话当前进行中的共享就是这一场
    kScreenShareRoomEnded   = 2,   //已结束：这个会话当前没有共享，或者进行中的是另一场
};
