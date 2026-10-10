/**
 *  锁屏动画场景的工厂：按 LockAnimation 创建对应的场景对象（各场景的实现见同目录的 *Scene.cpp）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "../Render/LockScene.h"
#include "Scenes.h"

namespace flamingolock {

ILockScene* CreateLockScene(LockAnimation anim)
{
    switch (anim)
    {
    //火烈鸟漫步
    case kAnimFlamingo:
        return CreateFlamingoScene();

    //粒子时钟
    case kAnimParticle:
        return CreateParticleClockScene();

    //弹跳标志与烟花
    case kAnimBounce:
        return CreateBounceScene();

    //汉字数字雨
    case kAnimRain:
        return CreateGlyphRainScene();

    //八荣八耻，以及不认识的取值
    default:
        return CreateHonorScene();
    }
}

} // namespace flamingolock
