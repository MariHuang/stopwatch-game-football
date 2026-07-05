#pragma once
#include <Arduino.h>

// ---- Vec2 / 数学辅助（与 main.cpp 一致，赛车游戏独立使用）----
struct RVec2 { float x, y; };

// ---- 赛车游戏对外接口 ----
// 初始化赛车游戏状态（开始/重开时调用）
void racingInit();
void racingSetPlayerCarType(int type);   // 设置玩家车型(0~8)，NPC 自动跳过
void drawCarPreview(int type, int cx, int cy, int size);  // 选车页大图预览

// 模拟一步（dt 秒）
void racingUpdate(float dt);

// 渲染一帧（内部调用 pushFrame）
void racingDraw();

// 处理输入：accel=加速(A), brake=刹车(B), steerX=转向(-1左..+1右)
void racingHandleInput(bool accel, bool brake, float steerX);

// 赛车游戏是否处于 Game Over 状态
bool racingIsGameOver();

// 重开赛车（Game Over 后由外部按键触发）
void racingRestartFromExternal();

// 请求返回菜单
void racingRequestExit();
