/**
 *  取光标位置与逻辑按键（屏幕共享第 16 期），ShareCursorSampler.h
 *
 *  鼠标效果要每轮知道光标在哪、左右键按没按。推流线程（共享）与录制线程（独立录屏）都要这一步，放在这里共用。
 *  真实光标用 GetCursorPos 与 GetAsyncKeyState，按系统的「切换主次按钮」设置换算成用户感知的左右键（见 LogicalMouseButtons）。
 *
 *  Debug 版联调用：环境变量 FLAMINGO_SHARE_FAKE_CURSOR_FILE 指向一个文本文件时，改读文件里的「x y 左键 右键」
 *  （后两项 1 表示按着，可以省略），最多每 50 毫秒重读一次；读不到时沿用上一次的值。这样联调时不移动用户的真实鼠标，
 *  改写文件就能模拟移动与点击。Release 版不读这个环境变量。
 *
 *  线程：一个对象只在一个线程上调用 Sample（推流线程或录制线程各持有一份）。
 *
 *  典型用法：
 *      POINT pt;
 *      bool left = false;
 *      bool right = false;
 *      m_sampler.Sample(pt, left, right, nowMs);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <windows.h>
#include <string>

namespace screenshare {

//取光标位置与逻辑按键；Debug 版可改读模拟光标文件
class ShareCursorSampler
{
public:
    //构造时读一次环境变量（Debug 版），决定用真实光标还是模拟光标文件
    ShareCursorSampler();

    /**
     *  取一次光标状态。
     *  @param pt    [出参] 光标的屏幕坐标（物理像素）
     *  @param left  [出参] 逻辑左键是否按下
     *  @param right [出参] 逻辑右键是否按下
     *  @param nowMs 当前时刻（毫秒，单调递增），读模拟光标文件的节流用
     */
    void Sample(POINT& pt, bool& left, bool& right, int64_t nowMs);

    //是否在用模拟光标文件（日志用）
    bool IsFake() const;

private:
    std::string m_fakeFile;   //模拟光标文件的路径；为空表示用真实光标
    int64_t     m_readMs;     //上次读文件的时刻（毫秒）
    POINT       m_fakePt;     //模拟光标的屏幕坐标
    bool        m_fakeLeft;   //模拟的逻辑左键是否按下
    bool        m_fakeRight;  //模拟的逻辑右键是否按下
    bool        m_everRead;   //是否读到过一次文件（没读到过时每次都读，不受 50 毫秒节流）
};

} // namespace screenshare
