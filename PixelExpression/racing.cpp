#include "racing.h"
#include "racing_car_sprites.h"
#include "racing_enemy_car_sprites.h"
#include "racing_car_preview.h"   // 选车页高清预览
#include "racing_coin_sprite.h"
#include <M5Unified.h>
#include <math.h>
#include <pgmspace.h>

// ---- 由 main.cpp 提供的共享绘制桥接（在 main.cpp 末尾定义）----
extern "C" {
  bool gfxUseCanvas();
  void gfxFillScreen(uint16_t c);
  void gfxBox(int x, int y, int w, int h, uint16_t c);
  void gfxPixel(int x, int y, uint16_t c);
  uint16_t gfxReadPixel(int x, int y);
  void gfxRoundBox(int x, int y, int w, int h, int r, uint16_t c);
  void gfxRectLine(int x, int y, int w, int h, uint16_t c);
  void gfxLine(int x0, int y0, int x1, int y1, uint16_t c);
  void gfxWideLine(int x0, int y0, int x1, int y1, int w, uint16_t c);
  void gfxFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
  void gfxCircle(int x, int y, int r, uint16_t c);
  void gfxEllipse(int x, int y, int rx, int ry, uint16_t c);
  void gfxSmoothEllipse(int x, int y, int rx, int ry, uint16_t c);
  void gfxSmoothCircle(int x, int y, int r, uint16_t c);
  void gfxDrawCircle(int x, int y, int r, uint16_t c);
  void gfxDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c);
  void gfxTextCenter(const char* s, int x, int y, int font, uint16_t c, uint16_t bg);
  void gfxTextCenterS(const char* s, int x, int y, int size, uint16_t c, uint16_t bg);
  void gfxTextCenterF4(const char* s, int x, int y, int size, uint16_t c, uint16_t bg);
  void gfxTextLeft(const char* s, int x, int y, int font, uint16_t c, uint16_t bg);
  void gfxPushFrame();
}

static inline uint16_t RRGB565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ---- 赛车游戏配色（贴近 Out Run 经典风格）----
static const uint16_t R_SKY_TOP   = RRGB565(70, 130, 210);
static const uint16_t R_SKY_BOT   = RRGB565(160, 200, 235);
static const uint16_t R_CLOUD     = RRGB565(250, 250, 252);
static const uint16_t R_GRASS_A   = RRGB565(76, 160, 76);
static const uint16_t R_GRASS_B   = RRGB565(90, 175, 84);
static const uint16_t R_ROAD      = RRGB565(50, 50, 56);       // 深灰沥青
static const uint16_t R_ROAD_DARK = RRGB565(42, 42, 48);
static const uint16_t R_LANE_LINE = RRGB565(245, 245, 230);    // 白色虚线
static const uint16_t R_CURB_R    = RRGB565(225, 55, 55);      // 红路缘石
static const uint16_t R_CURB_W    = RRGB565(245, 245, 245);    // 白路缘石
static const uint16_t R_WHITE     = 0xFFFF;
static const uint16_t R_BLACK     = 0x0000;
static const uint16_t R_RED       = RRGB565(225, 55, 55);
static const uint16_t R_YELLOW    = RRGB565(255, 214, 44);
static const uint16_t R_PANEL     = RRGB565(18, 22, 28);
static const uint16_t R_DIM       = RRGB565(120, 130, 140);
static const uint16_t R_CAR_BODY  = RRGB565(230, 50, 50);      // 红色车身
static const uint16_t R_CAR_BODY_D= RRGB565(180, 35, 35);      // 车身暗部
static const uint16_t R_CAR_GLASS = RRGB565(35, 75, 115);      // 车窗
static const uint16_t R_CAR_DARK  = RRGB565(25, 25, 30);
static const uint16_t R_TREE_TOP  = RRGB565(40, 110, 50);      // 树冠
static const uint16_t R_TREE_TRUNK= RRGB565(90, 60, 35);       // 树干
static const uint16_t R_RAIL      = RRGB565(180, 180, 175);    // 护栏
static const uint16_t R_RAIL_POST = RRGB565(120, 120, 115);    // 护栏柱
static const uint16_t R_ORANGE_CONE = RRGB565(255, 140, 30);   // 路障锥

// ---- 屏幕尺寸（从 M5 读取）----
static constexpr int RACING_RENDER_W = 466;
static constexpr int RACING_RENDER_H = 466;
static int rSW = RACING_RENDER_W;
static int rSH = RACING_RENDER_H;

// 画敌车图片精灵：9 种车型，按三车道选择左/正后/右三视图。
static const uint8_t ENEMY_CAR_BBOX[ENEMY_CAR_TYPES][ENEMY_CAR_VIEWS][4] = {
  {{6,10,89,85},{9,6,86,89},{6,8,89,86}},
  {{6,10,89,85},{7,6,88,91},{6,8,89,87}},
  {{4,6,89,89},{8,6,87,91},{5,7,89,87}},
  {{8,6,86,89},{12,6,82,89},{9,6,85,89}},
  {{6,6,89,89},{11,6,84,90},{9,6,87,89}},
  {{7,6,86,89},{14,6,81,91},{9,6,84,89}},
  {{6,12,89,82},{6,9,89,85},{6,11,89,84}},
  {{6,9,89,85},{6,6,88,89},{6,9,89,86}},
  {{5,9,89,87},{8,6,87,89},{6,7,89,87}},
};

// ---- 伪3D 投影参数 ----
static const float HORIZON_RATIO = 0.43f;   // 半圆赛道顶部位置
static const float ROAD_W_NEAR   = 0.86f;   // 近端路宽占屏幕比例
static const float ROAD_W_FAR    = 0.06f;   // 远端路宽
static const float CURVE_STRENGTH = 2.2f;   // 弯道视觉强度

// ---- 游戏状态 ----
struct Obstacle {
  float z;        // 深度(远大近小)，>0
  float lane;     // 横向位置(归一化 -1..1，0=路中)
  int   type;     // 0=敌车 1=路障 2=金币
  int   colorIdx; // 敌车车型索引(0..8)
  float sizeMul;  // 敌车大小倍率(0.8..1.3)
  float npcSpeed; // NPC 自己的前进速度，玩家靠相对速度追上它
  bool  fixed;     // 是否固定在车道(像金币一样静止，npcSpeed=玩家速度)
  bool  active;
};
static const int OBSTACLE_MAX = 24;
static Obstacle obstacles[OBSTACLE_MAX];

struct CoinPop {
  int x;
  int y;
  int size;
  uint32_t startMs;
  bool active;
};
static const int COIN_POP_MAX = 8;
static CoinPop coinPops[COIN_POP_MAX];

struct Meteor {
  bool active;
  uint32_t startMs;
  int startX;
  int startY;
  int endX;
  int endY;
};
static Meteor rMeteor = { false, 0, 0, 0, 0, 0 };

static float rSpeed      = 0.0f;     // 当前速度
static uint32_t rStartMs   = 0;        // 游戏开始时刻(用于分阶段自动提速)
static uint32_t rIntroStartMs = 0;   // 入场动画开始时刻(玩家车从底部开上来)
static bool rHudFadeActive = false;  // HUD 是否允许开始淡入
static uint32_t rHudFadeStartMs = 0; // HUD 淡入起始时刻
#define RACING_INTRO_MS  1500       // 入场动画总时长：1.0s 车开上来 + 0.5s 仪表盘渐显
#define RACING_CAR_INTRO_MS 1000    // 玩家车上移时长
static float rMaxSpeed   = 1.6f;     // 速度上限(随分数提升)
static float rMinSpeed   = 0.6f;     // 最低速度(起步60km/h)
static float rDist       = 0.0f;     // 行驶距离(=分数)
static float rCarX       = 0.0f;     // 玩家车横向位置(归一化 -1..1)
static int   rLane       = 1;        // 跑酷车道：0=左 1=中 2=右
static int   rTargetLane = 1;        // 目标车道，rCarX 平滑追随
static uint32_t rLastLaneChangeMs = 0;
static bool  rSteerLatch = false;    // 换道锁存：一次触摸只触发一次，回到中性区才复位
static bool  rLaneChangePending = false;  // 换道进行中，到位后再降速(避免转向时卡顿)
static uint32_t rBoostUntil = 0;
static bool  rBrakeHeld = false;
static float rSteerVis   = 0.0f;     // 玩家车转向视觉偏摆(-1左..1右)
static int8_t  rCarFrame = 0;        // 当前显示帧: -1=左 0=中 1=右(滞后切换)
static float rCurve      = 0.0f;     // 当前弯道偏移(视觉)
static float rCurveTgt   = 0.0f;     // 目标弯道
static float rSroll      = 0.0f;     // 路面纹理滚动
static float rObstTimer  = 0.0f;     // 障碍生成计时
static float rNextCoinDist = 0.0f;   // 下一串金币出现的行驶距离
static int   rCoinScore  = 0;        // 吃到的金币数
static int   rNextMeteorScore = 50;  // 每到 50 金币触发一次流星
static float rHighScore  = 0.0f;     // 最高分(内存)
static bool  rGameOver   = false;
static bool  rExitFlag   = false;
static bool  rOffTrack   = false;       // 是否冲出赛道
static uint32_t rOffTrackUntil = 0;     // 冲出赛道后 Game Over 倒计时
static float rCamX = 0.0f;             // 相机横向偏移(基于 rCarX)：玩家转向时整个世界反向滑动
static const float R_LANES[3] = { -1.0f, 0.0f, 1.0f };
static const float RUNNER_LANE_SPAN = 0.534f;  // 近端每车道宽 100 显示像素(194 渲染像素)
static int PLAYER_CAR_TYPE = 3;   // 默认红色吉普，可被选车页覆盖(运行时变量)
static const float COIN_SPAWN_Z = 1.08f;       // 金币先在地平线外生成，再随玩家前进进入画面
static const float COIN_CHAIN_GAP = 0.075f;    // 一串金币之间的前后间距
static const float COIN_APPROACH_RATE = 0.24f; // 世界固定金币：玩家接近时才慢慢变大

static int runnerLaneOffset(float lane, int halfW) {
  return (int)(lane * halfW * RUNNER_LANE_SPAN);
}

static int roadCenterX(float t, int halfW) {
  float p = 1.0f - t;
  return rSW / 2 - (int)(rCamX * (0.25f + 0.75f * p)) +
         (int)(rCurve * t * t * CURVE_STRENGTH * (rSW * 0.5f));
}

static void fillQuad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c);
static void drawHeadlights(int cx, int baseY, int carW, int carH, float depth, bool player, float lane);

static int randomNpcCarType() {
  int type = rand() % max(1, ENEMY_CAR_TYPES - 1);
  if (type >= PLAYER_CAR_TYPE) type++;
  return type;
}

// ---- 伪3D 投影：把世界(横向归一化x, 深度z)映射到屏幕坐标 ----
// depth: 0=近端(屏幕底), 1=远端(地平线)
struct Proj { int x; int y; int scale; };

static int gHorizonY() { return (int)(rSH * HORIZON_RATIO); }

static int roadBodyTopY() {
  // 赛道主体的最高点下移到虚线翻上来的最高位置附近，避免半圆顶冒在虚线上方。
  return gHorizonY() + 42;
}

// 球面弧度：地平线在屏幕中央最高、两端下沉，模拟贴在球面上的赛道。
// 返回屏幕 x 处的地平线 y 值。sag 为两端最大下沉量(像素)。
static int horizonYAt(int x, int sag) {
  // 抛物线：以屏幕中心为顶点，两端下沉 sag
  float dx = (x - rSW * 0.5f) / (rSW * 0.5f);   // -1..1
  return gHorizonY() + (int)(sag * dx * dx);
}

// 给定深度 t(0近..1远)，返回路宽(像素)和该行的 y
static void roadGeom(float t, int& y, int& halfW) {
  t = constrain(t, 0.0f, 1.0f);
  float near = 1.0f - t;
  float roadTop = (float)roadBodyTopY();
  float roadH = (float)rSH - roadTop + 28.0f;
  float yy = powf(near, 0.72f);
  y = (int)(roadTop + yy * roadH);
  float circle = sqrtf(max(0.0f, 1.0f - (1.0f - yy) * (1.0f - yy)));
  halfW = (int)(circle * rSW * 0.78f);
}

// 投影一个世界点：laneX(-1..1 路面内)，t(0近..1远) → 屏幕 x
static int projectX(float laneX, float t, int halfW) {
  int cx = rSW / 2;
  // 弯道偏移：越远偏移越大
  float curveOff = rCurve * t * t * CURVE_STRENGTH * (rSW * 0.5f);
  return cx + (int)(laneX * halfW) + (int)curveOff;
}

static int cityRand(int seed, int salt, int mod);

// 设置玩家车型(0~8)，NPC 会自动跳过该车型
void racingSetPlayerCarType(int type) {
  PLAYER_CAR_TYPE = constrain(type, 0, ENEMY_CAR_TYPES - 1);
}

// ---- 初始化 ----
void racingInit() {
  rSW = min(RACING_RENDER_W, (int)M5.Display.width());
  rSH = min(RACING_RENDER_H, (int)M5.Display.height());
  rSpeed = 0.6f;   // 起始即 60km/h
  rStartMs = millis();   // 记录开始时刻，用于分阶段自动提速
  rIntroStartMs = millis();  // 入场动画开始
  rHudFadeActive = false;
  rHudFadeStartMs = 0;
  rMaxSpeed = 2.04f;  // 最高 204km/h
  rDist = 0.0f;
  rCarX = 0.0f;
  rLane = 1;
  rTargetLane = 1;
  rLastLaneChangeMs = 0;
  rSteerLatch = false;
  rLaneChangePending = false;
  rBoostUntil = 0;
  rBrakeHeld = false;
  rSteerVis = 0.0f;
  rCarFrame = 0;
  rCurve = 0.0f;
  rCurveTgt = 0.0f;
  rSroll = 0.0f;
  rObstTimer = 0.0f;
  rNextCoinDist = 180.0f;
  rCoinScore = 0;
  rNextMeteorScore = 50;
  rMeteor.active = false;
  rGameOver = false;
  rExitFlag = false;
  rOffTrack = false;
  rOffTrackUntil = 0;
  for (int i = 0; i < OBSTACLE_MAX; ++i) obstacles[i].active = false;
  for (int i = 0; i < COIN_POP_MAX; ++i) coinPops[i].active = false;
}

// ---- 障碍生成 ----
static int activeNpcCount() {
  int count = 0;
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (obstacles[i].active && obstacles[i].type == 0) count++;
  }
  return count;
}

static int activeCoinCount() {
  int count = 0;
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (obstacles[i].active && obstacles[i].type == 2) count++;
  }
  return count;
}

static bool anyNpcWithinZ(float z, float gap) {
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active || obstacles[i].type != 0) continue;
    if (fabsf(obstacles[i].z - z) < gap) return true;
  }
  return false;
}

static bool laneHasNearbyNpc(int laneIdx, float z, float gap) {
  float lane = R_LANES[constrain(laneIdx, 0, 2)];
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active || obstacles[i].type != 0) continue;
    if (fabsf(obstacles[i].lane - lane) < 0.1f && fabsf(obstacles[i].z - z) < gap) {
      return true;
    }
  }
  return false;
}

static bool laneHasNearbyObject(int laneIdx, float z, float gap) {
  float lane = R_LANES[constrain(laneIdx, 0, 2)];
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) continue;
    if (fabsf(obstacles[i].lane - lane) < 0.1f && fabsf(obstacles[i].z - z) < gap) {
      return true;
    }
  }
  return false;
}

static bool addObstacleInLane(int laneIdx, float z) {
  if (activeNpcCount() >= 4) return false;
  if (anyNpcWithinZ(z, 0.38f)) return false;
  if (laneHasNearbyObject(laneIdx, z, 0.34f)) return false;
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) {
      obstacles[i].active = true;
      obstacles[i].z = z;
      obstacles[i].lane = R_LANES[constrain(laneIdx, 0, 2)];
      obstacles[i].type = 0;
      obstacles[i].colorIdx = randomNpcCarType();
      obstacles[i].sizeMul = 1.0f;
      float pace = 0.88f + (float)(rand() % 9) * 0.01f;  // NPC 同向行驶，只比玩家慢一点
      obstacles[i].npcSpeed = max(1.15f, rSpeed * pace);
      obstacles[i].fixed = (rand() % 10 == 0);   // 10% 概率固定在车道(像金币)
      if (obstacles[i].fixed) obstacles[i].npcSpeed = rSpeed;   // 固定=和玩家同速
      return true;
    }
  }
  return false;
}

static bool addCoinInLane(int laneIdx, float z) {
  if (activeCoinCount() >= 12) return false;
  if (laneHasNearbyObject(laneIdx, z, 0.06f)) return false;
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) {
      obstacles[i].active = true;
      obstacles[i].z = z;
      obstacles[i].lane = R_LANES[constrain(laneIdx, 0, 2)];
      obstacles[i].type = 2;
      obstacles[i].colorIdx = 0;
      obstacles[i].sizeMul = 1.0f;
      obstacles[i].npcSpeed = 0.0f;
      return true;
    }
  }
  return false;
}

static void spawnObstacle() {
  if (activeNpcCount() >= 4) return;
  int lane = rand() % 3;
  addObstacleInLane(lane, 1.0f);
}

static void spawnCoin() {
  if (activeCoinCount() >= 12) return;
  int count = 1 + rand() % 6;
  count = min(count, 12 - activeCoinCount());
  int firstLane = rand() % 3;
  for (int attempt = 0; attempt < 3; ++attempt) {
    int lane = (firstLane + attempt) % 3;
    bool placedAny = false;
    for (int n = 0; n < count; ++n) {
      float z = COIN_SPAWN_Z + n * COIN_CHAIN_GAP;
      placedAny = addCoinInLane(lane, z) || placedAny;
    }
    if (placedAny) return;
  }
}

static int coinDrawSizeForBaseY(int baseY) {
  const int coinNearH = 42;   // 金币近处尺寸，按透视远小近大缩放
  const int playerBottom = rSH - 16 - 40 - 20;
  float screenRatio = (float)(baseY - gHorizonY()) / (float)max(1, playerBottom - gHorizonY());
  screenRatio = constrain(screenRatio, 0.0f, 1.18f);
  float perspective = constrain(0.18f + 0.82f * powf(screenRatio, 1.15f), 0.18f, 1.18f);
  return max(10, (int)(coinNearH * perspective));
}

static void coinScreenRect(const Obstacle& coin, int& x0, int& y0, int& size) {
  int baseY, halfW;
  roadGeom(coin.z, baseY, halfW);
  int cx = roadCenterX(coin.z, halfW) + runnerLaneOffset(coin.lane, halfW);
  float laneAdj = halfW * (0.214f - 0.214f * (1.0f - coin.z));
  if (coin.lane < -0.34f) cx += (int)laneAdj;
  else if (coin.lane > 0.34f) cx -= (int)laneAdj;
  size = coinDrawSizeForBaseY(baseY);
  int liftOff = (int)(size * 1.4f);
  float bob = sinf(millis() * 0.004f + cx * 0.05f) * 3.0f;
  x0 = cx - size / 2;
  y0 = baseY - size - liftOff + (int)bob;
}

static void startCoinPop(const Obstacle& coin) {
  int x0, y0, size;
  coinScreenRect(coin, x0, y0, size);
  int carBottom = rSH - 16 - 40 - 20;
  int carFrontY = carBottom - 88 - size / 2;
  y0 = min(y0, carFrontY);
  int slot = 0;
  for (int i = 0; i < COIN_POP_MAX; ++i) {
    if (!coinPops[i].active) { slot = i; break; }
  }
  coinPops[slot].x = x0;
  coinPops[slot].y = y0;
  coinPops[slot].size = size;
  coinPops[slot].startMs = millis();
  coinPops[slot].active = true;
}

static void startMeteor() {
  uint32_t now = millis();
  int starMaxY = max(24, roadBodyTopY() - 26);
  bool fromRight = ((rCoinScore / 50) & 1) == 1;
  int y0 = 12 + cityRand(rCoinScore, 73, max(1, starMaxY - 26));
  int y1 = min(starMaxY + 18, y0 + 42 + cityRand(rCoinScore, 79, 30));
  rMeteor.active = true;
  rMeteor.startMs = now;
  if (fromRight) {
    rMeteor.startX = rSW + 54;
    rMeteor.endX = -70;
  } else {
    rMeteor.startX = -54;
    rMeteor.endX = rSW + 70;
  }
  rMeteor.startY = y0;
  rMeteor.endY = y1;
}

// ---- 输入 ----
void racingHandleInput(bool accel, bool brake, float steerX) {
  if (rGameOver) return;
  uint32_t now = millis();
  if (accel) rBoostUntil = now + 650;
  rBrakeHeld = brake;
  // 边沿触发：一次触摸只换一条车道，必须回中性区才能再次触发
  if (steerX > -0.20f && steerX < 0.20f) {
    rSteerLatch = false;   // 回到中性区，复位锁存，允许下一次换道
  } else if (!rSteerLatch && (now - rLastLaneChangeMs > 120)) {
    if (steerX < -0.34f && rTargetLane > 0) {
      rTargetLane--;
      rLastLaneChangeMs = now;
      rSteerLatch = true;
      rSpeed = max(0.6f, rSpeed - 0.1f);   // 换道降速 10km/h，最低 60
    } else if (steerX > 0.34f && rTargetLane < 2) {
      rTargetLane++;
      rLastLaneChangeMs = now;
      rSteerLatch = true;
      rSpeed = max(0.6f, rSpeed - 0.1f);   // 换道降速 10km/h，最低 60
    }
  }
  rLane = rTargetLane;
  rSteerVis = constrain(steerX, -1.0f, 1.0f);
}

// ---- 更新 ----
void racingUpdate(float dt) {
  if (rExitFlag) return;
  if (rGameOver) return;
  uint32_t now = millis();
  if (!rHudFadeActive && (uint32_t)(now - rIntroStartMs) >= 1000) {
    rHudFadeActive = true;
    rHudFadeStartMs = now;
  }
  // 入场动画期间(1.5s)：路面以固定速度滚动(车在开)，但不提速/不生成NPC/不计分
  if ((uint32_t)(now - rIntroStartMs) < RACING_INTRO_MS) {
    rSroll += 0.6f * dt * 35.0f;   // 固定 60km/h 滚动速度，路面动起来
    return;
  }

  // 自动提速(无需按键)：起始即 60km/h，每 6s 提升一档
  // 0-6s: 60→120, 6-12s: 120→204, 之后保持 204
  float elapsed = (millis() - rStartMs) / 1000.0f;
  float targetSpeed;
  if (elapsed < 6.0f) {
    targetSpeed = 0.6f + 0.6f * (elapsed / 6.0f);             // 60 → 120
  } else if (elapsed < 12.0f) {
    targetSpeed = 1.2f + 0.84f * ((elapsed - 6.0f) / 6.0f);   // 120 → 204
  } else {
    targetSpeed = 2.04f;                                      // 保持 204
  }
  rMaxSpeed = 2.04f;
  if (rBrakeHeld) targetSpeed = max(0.0f, targetSpeed - 0.75f);
  rSpeed += (targetSpeed - rSpeed) * min(1.0f, dt * 3.8f);
  rSpeed = constrain(rSpeed, 0.0f, rMaxSpeed);

  float targetX = R_LANES[rTargetLane];
  float laneEase = min(1.0f, dt * 11.0f);
  rCarX += (targetX - rCarX) * laneEase;
  rSteerVis = constrain((targetX - rCarX) * 2.6f, -1.0f, 1.0f);

  // 距离/分数累积
  rDist += rSpeed * dt * 60.0f;

  // 路面纹理滚动（加速，让道路和虚线明显往后跑）
  rSroll += rSpeed * dt * 35.0f;
  // 即时回正，无需衰减

  // 赛道恒为直行：不生成弯道(rCurve 永远为 0，所有弯道相关效果自动失效)

  // NPC 在前方同向行驶；保留追车感，但给视觉位移一个最低通过速度，
  // 避免相对速度太小时车到玩家附近像突然刹住。
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) continue;
    if (obstacles[i].type == 2) {
      // 金币是路上的固定奖励点：只因玩家前进而逐渐接近、变大，不像 NPC 车辆那样快速后退。
      obstacles[i].z -= rSpeed * dt * COIN_APPROACH_RATE;
      continue;
    }
    float oldZ = obstacles[i].z;
    float closingSpeed = rSpeed - obstacles[i].npcSpeed;
    float zStep = max(rSpeed * dt * 0.32f, closingSpeed * dt * 0.55f);
    if (obstacles[i].z <= 0.72f) {
      zStep = max(zStep, rSpeed * dt * 0.95f);
    }
    if (obstacles[i].z <= 0.34f) {
      zStep = max(zStep, rSpeed * dt * 1.35f);
    }
    obstacles[i].z -= zStep;
    if (obstacles[i].z > 1.03f) obstacles[i].z = 1.03f;
    // 保险：如果 z 一帧跳过碰撞区(oldZ>0.5 → newZ<0)，说明高速掠过，立即检测同车道碰撞
    if (oldZ > 0.5f && obstacles[i].z < 0.0f) {
      float laneDiff = fabsf(obstacles[i].lane - rCarX);
      if (laneDiff < 0.5f) {
        rGameOver = true;
        if (rCoinScore > (int)rHighScore) rHighScore = (float)rCoinScore;
      }
    }
  }

  // 障碍生成
  rObstTimer -= dt;
  if (rObstTimer <= 0.0f) {
    spawnObstacle();
    float interval = 1.85f - min(rDist / 6500.0f, 0.75f);  // 越远越频繁，但避免近端堆车
    rObstTimer = max(1.10f, interval);
  }

  // 金币生成：按行驶距离触发，金币先在地平线外生成，再随玩家前进进入画面。
  if (rDist >= rNextCoinDist) {
    spawnCoin();
    rNextCoinDist = rDist + 220.0f + (float)(rand() % 220);
  }

  // 碰撞检测：碰到 NPC 车尾(同车道 z>0 还没越过)立即 Game Over；金币被吃掉并计数。
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) continue;
    if (obstacles[i].z > 0.5f || obstacles[i].z < 0.0f) continue;   // 检测窗口扩大到 0.5，防止 NPC 高速跳过
    float laneDiff = fabsf(obstacles[i].lane - rCarX);
    if (obstacles[i].type == 2 && laneDiff < 0.5f) {
      startCoinPop(obstacles[i]);
      obstacles[i].active = false;
      rCoinScore++;
      if (rCoinScore >= rNextMeteorScore) {
        startMeteor();
        rNextMeteorScore += 50;
      }
      if (rCoinScore > (int)rHighScore) rHighScore = (float)rCoinScore;
    } else if (obstacles[i].type == 0 && laneDiff < 0.5f) {
      // 撞到 NPC 车尾(同车道，z 到达玩家位置)→ Game Over
      rGameOver = true;
      if (rCoinScore > (int)rHighScore) rHighScore = (float)rCoinScore;
    }
  }

  // 近端通过区完成后清除，避免 NPC 堆在屏幕下方。
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) continue;
    if (obstacles[i].type == 2) {
      if (obstacles[i].z <= -0.04f) obstacles[i].active = false;
    } else if (obstacles[i].z <= -0.04f) {
      obstacles[i].active = false;
    }
  }

  rOffTrack = false;
}

bool racingIsGameOver() { return rGameOver; }
bool racingWantsExit()  { return rExitFlag; }

// 重开（外部按键触发）
static void racingRestart() {
  rSpeed = 0.6f;   // 起始即 60km/h
  rStartMs = millis();   // 记录开始时刻，用于分阶段自动提速
  rIntroStartMs = millis();  // 入场动画开始
  rHudFadeActive = false;
  rHudFadeStartMs = 0;
  rMaxSpeed = 2.04f;  // 最高 204km/h
  rDist = 0.0f;
  rCarX = 0.0f;
  rLane = 1;
  rTargetLane = 1;
  rLastLaneChangeMs = 0;
  rSteerLatch = false;
  rLaneChangePending = false;
  rBoostUntil = 0;
  rBrakeHeld = false;
  rSteerVis = 0.0f;
  rCarFrame = 0;
  rCurve = 0.0f;
  rCurveTgt = 0.0f;
  rSroll = 0.0f;
  rObstTimer = 0.0f;
  rNextCoinDist = 180.0f;
  rCoinScore = 0;
  rNextMeteorScore = 50;
  rMeteor.active = false;
  rGameOver = false;
  rExitFlag = false;
  rOffTrack = false;
  rOffTrackUntil = 0;
  for (int i = 0; i < OBSTACLE_MAX; ++i) obstacles[i].active = false;
  for (int i = 0; i < COIN_POP_MAX; ++i) coinPops[i].active = false;
}

// 外部调用：设置退出标志(返回菜单)
void racingRequestExit() { rExitFlag = true; }
// 外部调用：Game Over 后重开
void racingRestartFromExternal() { racingRestart(); }

// 画一朵云（平滑椭圆组合）
static void drawCloud(int cx, int cy, int scale) {
  gfxEllipse(cx, cy, scale * 2, scale, R_CLOUD);
  gfxEllipse(cx - scale, cy - scale / 3, scale, scale * 2 / 3, R_CLOUD);
  gfxEllipse(cx + scale, cy - scale / 3, scale, scale * 2 / 3, R_CLOUD);
  gfxEllipse(cx, cy - scale / 2, scale * 3 / 4, scale * 2 / 3, R_CLOUD);
}

// 画一棵树（圆形树冠 + 圆柱树干，平滑风格）
static void drawTree(int x, int baseY, int scale) {
  if (scale < 2) return;
  // 树干
  gfxBox(x - max(1, scale / 4), baseY - scale, max(1, scale / 2), scale, R_TREE_TRUNK);
  // 树冠(圆形，叠加两层增加体积感)
  gfxCircle(x, baseY - scale * 2, scale, R_TREE_TOP);
  gfxCircle(x - scale / 3, baseY - scale * 2 - scale / 3, scale * 2 / 3, RRGB565(55, 130, 60));
}

// 画一段护栏（按深度，平滑风格）
static void drawRail(int x, int y, int bandH, int scale) {
  int rh = max(1, scale / 2);
  gfxRoundBox(x, y, max(3, scale), rh, max(1, rh / 2), R_RAIL);
  gfxBox(x + max(1, scale / 4), y + rh, max(1, scale / 3), bandH - rh, R_RAIL_POST);
}

static int cityRand(int seed, int salt, int mod) {
  uint32_t x = (uint32_t)(seed * 1103515245u + salt * 2654435761u + 12345u);
  x ^= x >> 16;
  x *= 2246822519u;
  x ^= x >> 13;
  return (int)(x % (uint32_t)mod);
}

static uint16_t buildingColor(int seed, int shade) {
  // 白天建筑配色：浅灰、蓝灰、米色、砖红穿插，避免一片同色方块。
  static const uint8_t pal[8][3] = {
    {198, 205, 208}, {184, 198, 210}, {212, 199, 178}, {190, 180, 170},
    {176, 191, 188}, {205, 184, 168}, {164, 178, 196}, {216, 210, 196}
  };
  int idx = seed & 7;
  return RRGB565((uint8_t)constrain(pal[idx][0] + shade, 0, 255),
                 (uint8_t)constrain(pal[idx][1] + shade, 0, 255),
                 (uint8_t)constrain(pal[idx][2] + shade, 0, 255));
}

static void drawMeteor() {
  if (!rMeteor.active) return;
  uint32_t elapsed = millis() - rMeteor.startMs;
  const uint32_t duration = 920;
  if (elapsed >= duration) {
    rMeteor.active = false;
    return;
  }

  float t = constrain((float)elapsed / (float)duration, 0.0f, 1.0f);
  float ease = t * t * (3.0f - 2.0f * t);
  float fade = sinf(t * 3.1415926f);
  int headX = rMeteor.startX + (int)((rMeteor.endX - rMeteor.startX) * ease);
  int headY = rMeteor.startY + (int)((rMeteor.endY - rMeteor.startY) * ease);
  int dx = rMeteor.endX - rMeteor.startX;
  int dy = rMeteor.endY - rMeteor.startY;
  float len = sqrtf((float)dx * dx + (float)dy * dy);
  if (len < 1.0f) return;
  float ux = dx / len;
  float uy = dy / len;
  int tailLen = 58;
  int tailX = headX - (int)(ux * tailLen);
  int tailY = headY - (int)(uy * tailLen);
  int midX = headX - (int)(ux * 26);
  int midY = headY - (int)(uy * 26);
  int v = (int)(255.0f * fade);
  uint16_t cHead = RRGB565((uint8_t)v, (uint8_t)v, 255);
  uint16_t cMid = RRGB565((uint8_t)(90 + 110 * fade), (uint8_t)(95 + 110 * fade), (uint8_t)(130 + 95 * fade));
  uint16_t cTail = RRGB565((uint8_t)(65 + 70 * fade), (uint8_t)(60 + 65 * fade), (uint8_t)(95 + 65 * fade));
  gfxWideLine(tailX, tailY, midX, midY, 2, cTail);
  gfxWideLine(midX, midY, headX, headY, 3, cMid);
  gfxPixel(headX, headY, cHead);
  gfxPixel(headX + 1, headY, cHead);
  gfxPixel(headX - 1, headY, cHead);
  gfxPixel(headX, headY + 1, cHead);
  gfxPixel(headX, headY - 1, cHead);
}

static void drawBuilding(int x, int baseY, int w, int h, int side, int seed) {
  if (w < 4 || h < 6) return;
  int depth = max(2, w / 4);
  int topY = baseY - h;
  uint16_t face = buildingColor(seed, 0);
  uint16_t lit = buildingColor(seed, 20);
  uint16_t dark = buildingColor(seed, -32);
  uint16_t edge = buildingColor(seed, -48);
  uint16_t roof = buildingColor(seed, -42);
  uint16_t roofHi = buildingColor(seed, -18);
  uint16_t win = RRGB565(210, 236, 248);           // 窗户：白天蓝天反光
  uint16_t winDim = RRGB565(118, 142, 156);        // 暗窗

  int frontX = x;
  int sideX = x + side * depth;   // 侧面后边缘 x(右边楼在右，左边楼在左)
  int faceW = w;
  if (side < 0) frontX = x - w;   // 左边楼：正面在 x-w..x

  // 投影/底座，让近处楼和地面贴合。
  gfxBox(frontX - (side < 0 ? depth : 0), baseY - 2, faceW + depth, max(2, h / 18), RRGB565(70, 76, 78));
  gfxBox(frontX, topY, faceW, h, face);
  gfxBox(frontX, topY, faceW, 2, roofHi);
  gfxBox(frontX, topY, 2, h, lit);
  gfxBox(frontX + faceW - 2, topY, 2, h, edge);

  if (side > 0) {
    // 右边楼：侧面从正面右边缘(frontX+faceW=x)向右后方延伸到 sideX(=x+depth)
    gfxFillTriangle(frontX + faceW, topY, sideX, topY + depth, frontX + faceW, baseY, dark);
    gfxFillTriangle(sideX, topY + depth, sideX, baseY + depth, frontX + faceW, baseY, dark);
    // 屋顶梯形：正面顶边(frontX..frontX+faceW) → 后边(sideX.. 偏移)
    gfxFillTriangle(frontX, topY, frontX + faceW, topY, sideX, topY + depth, roof);
    gfxFillTriangle(frontX, topY, sideX, topY + depth, frontX, topY + depth / 2, roof);
  } else {
    // 左边楼：正面在 frontX(=x-w)..x，侧面从正面左边缘(frontX)向左后方延伸
    // sideX 应该 = frontX - depth = x - w - depth
    int leftSideX = frontX - depth;
    gfxFillTriangle(frontX, topY, leftSideX, topY + depth, frontX, baseY, lit);
    gfxFillTriangle(leftSideX, topY + depth, leftSideX, baseY + depth, frontX, baseY, lit);
    // 屋顶梯形：正面顶边(frontX..frontX+faceW) → 左后角(leftSideX, topY+depth)
    gfxFillTriangle(frontX, topY, frontX + faceW, topY, leftSideX, topY + depth, roof);
    gfxFillTriangle(frontX + faceW, topY, leftSideX, topY + depth, frontX, topY + depth / 2, roof);
  }

  // 楼顶小结构，制造高矮错落的轮廓。
  if (h > 34 && cityRand(seed, 3, 3) != 0) {
    int pentW = max(4, w / 4);
    int pentH = max(3, h / 12);
    int pentX = frontX + cityRand(seed, 4, max(1, faceW - pentW));
    gfxBox(pentX, topY - pentH, pentW, pentH, roof);
    gfxBox(pentX, topY - pentH, pentW, 1, roofHi);
  }
  if (h > 58 && cityRand(seed, 5, 4) == 0) {
    int antX = frontX + faceW / 2 + cityRand(seed, 6, max(1, faceW / 3)) - faceW / 6;
    gfxLine(antX, topY - max(10, h / 10), antX, topY, edge);
  }

  int wx0 = frontX + max(3, w / 7);
  int wy0 = topY + max(5, h / 9);
  int ww = max(2, w / 9);
  int wh = max(2, h / 15);
  int colStep = max(6, w / 4);
  int rowStep = max(7, h / 7);
  int col = 0, row = 0;
  for (int wy = wy0; wy < baseY - wh - 2; wy += rowStep, ++row, col = 0) {
    for (int wx = wx0; wx < frontX + faceW - ww - 1; wx += colStep, ++col) {
      // 用建筑内部的行列索引决定亮暗(固定，不随屏幕滚动闪烁)
      uint16_t wc = (((col * 5 + row * 7 + seed * 11) % 6) <= 1) ? win : winDim;
      gfxBox(wx, wy, ww, wh, wc);
    }
  }

  if (h > 48) {
    int bandY = topY + h / 2;
    gfxBox(frontX + 2, bandY, faceW - 4, 1, buildingColor(seed, -18));
  }
}

// ---- 绘制：天空 + 草地 + 路面 + 路边装饰 ----
static void drawSkyAndRoad() {
  int horizonY = gHorizonY();

  // 1. 背景：统一深紫色(#54009D)
  uint16_t skyC = RRGB565(0x54, 0x00, 0x9D);
  gfxBox(0, 0, rSW, rSH, skyC);

  // 2. 淡白色闪烁星星：略提亮但保持小尺寸，避免抢画面主体。
  const int starCount = 30;
  int starMaxY = max(24, roadBodyTopY() - 8);
  uint32_t starTick = millis() / 280;
  for (int i = 0; i < starCount; ++i) {
    int sx = 10 + cityRand(i, 31, max(1, rSW - 20));
    int sy = 8 + cityRand(i, 37, max(1, starMaxY - 8));
    int phase = (int)((starTick + cityRand(i, 41, 7)) % 8);
    int glow = (phase <= 3) ? phase : (7 - phase);  // 0..3..0
    int v = 125 + glow * 25;
    uint16_t starC = RRGB565(v, v, min(255, v + 18));
    gfxPixel(sx, sy, starC);
    if (glow >= 2 && (i % 5) == 0) {
      gfxPixel(sx + 1, sy, RRGB565(118, 118, 138));
      gfxPixel(sx - 1, sy, RRGB565(104, 104, 124));
      gfxPixel(sx, sy + 1, RRGB565(118, 118, 138));
      gfxPixel(sx, sy - 1, RRGB565(104, 104, 124));
    }
  }
  drawMeteor();

  // 3. 远处城市天际线和灯点，放在球形赛道后面(整体下移 60px)。
  const int SAG2 = rSH / 7;
  const int SKY_OFFSET = 60;   // 天际线整体下移量
  for (int i = 0; i < 12; ++i) {
    int bw = 22 + (i % 4) * 7;
    int bh = 28 + ((i * 11) % 38);
    // 左侧前 3 栋加长 10px；右侧从右往左数：第1栋+20，第2栋+30，第3栋+10。
    int extraDown = (i < 3) ? 10 : (i == 11 ? 20 : (i == 10 ? 30 : (i == 9 ? 10 : 0)));
    int bx = -10 + i * 42;
    int baseHY = horizonYAt(bx + bw / 2, SAG2) + SKY_OFFSET;
    gfxBox(bx, baseHY - bh, bw, bh + extraDown, RRGB565(24 + (i % 3) * 5, 18 + (i % 4) * 4, 48 + (i % 2) * 8));
    if ((i % 2) == 0) gfxBox(bx + bw / 2, baseHY - bh / 2, 3, 4, RRGB565(245, 230, 116));
  }
  // 右侧高楼框和亮灯点已移除

  // 4. 半圆弧赛道主体：顶部收窄成圆弧，越靠近玩家越宽。
  // 使用浮点边界 + 2px 颜色过渡，减少半圆边缘台阶感。
  float roadTop = (float)roadBodyTopY();
  float roadH = (float)rSH - roadTop + 28.0f;
  uint16_t roadC = RRGB565(18, 15, 44);
  auto blend565 = [](uint16_t a, uint16_t b, float t) -> uint16_t {
    t = constrain(t, 0.0f, 1.0f);
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
  };
  for (int y = roadBodyTopY(); y < rSH; y += 1) {
    float yy = ((float)y - roadTop) / roadH;
    yy = constrain(yy, 0.0f, 1.0f);
    float circle = sqrtf(max(0.0f, 1.0f - (1.0f - yy) * (1.0f - yy)));
    float halfWf = circle * rSW * 0.78f;
    int cx = rSW / 2 - (int)(rCamX * (0.25f + 0.75f * yy)) +
             (int)(rCurve * (1.0f - yy) * (1.0f - yy) * CURVE_STRENGTH * (rSW * 0.5f));
    cx -= (int)(rCarX * 39.0f * (1.0f - yy));
    float leftF = (float)cx - halfWf;
    float rightF = (float)cx + halfWf;
    int coreL = max(0, (int)ceilf(leftF + 1.4f));
    int coreR = min(rSW - 1, (int)floorf(rightF - 1.4f));
    if (coreR >= coreL) gfxBox(coreL, y, coreR - coreL + 1, 1, roadC);
    for (int x = (int)floorf(leftF) - 1; x <= (int)ceilf(leftF) + 2; ++x) {
      if (x < 0 || x >= rSW) continue;
      float cover = constrain(((float)x + 0.5f - leftF) / 2.0f, 0.0f, 1.0f);
      if (cover <= 0.0f || cover >= 1.0f) continue;
      gfxPixel(x, y, blend565(skyC, roadC, cover));
    }
    for (int x = (int)floorf(rightF) - 2; x <= (int)ceilf(rightF) + 1; ++x) {
      if (x < 0 || x >= rSW) continue;
      float cover = constrain((rightF - ((float)x + 0.5f)) / 2.0f, 0.0f, 1.0f);
      if (cover <= 0.0f || cover >= 1.0f) continue;
      gfxPixel(x, y, blend565(skyC, roadC, cover));
    }
  }

  // 3 车道：2 条分界虚线(±1/3)，每条虚线本身是 3 个断续短段(虚-断-虚-断-虚)。
  // 短段沿球面从背面翻转上来：远端(t→1)贴球背 y 抬高变窄变暗，近端正面朝向。
  static const float dashLanes[2] = { -0.5f, 0.5f };  // 车道分界(每车道 50 像素)
  const float dashSegLen = 0.11f;     // 每个短段的长度(t 单位)
  const float dashSpacing = 0.24f;    // 短段中心间距(含段长+间隔)
  const int   dashCount = 3;          // 每条虚线 3 个短段，保证向下滚动时不断档、不反跳。
  float dashPhase = fmodf(rSroll * 0.05f, dashSpacing);
  for (int d = 0; d < 2; ++d) {
    for (int seg = 0; seg < dashCount; ++seg) {
      // t 越小越靠近玩家；phase 增大时 t 变小，所以虚线稳定向屏幕下方移动。
      float dashStart = 0.84f - (seg * dashSpacing) - dashPhase;
      float dashEnd = dashStart + dashSegLen;
      if (dashStart <= 0.03f) continue;            // 越过近端不画
      dashEnd = min(0.995f, dashEnd);
      // 方角粗线段(虚线)：沿球面弧多点采样，相邻四边形共享顶点，端点方角无圆头
      const int pieces = 8;
      int prevX = -1, prevY = -1, prevHalfW = 0;
      float prevWrap = 0;
      for (int i = 0; i <= pieces; ++i) {
        float t = dashStart + (dashEnd - dashStart) * (float)i / (float)pieces;
        int yg, hwg;
        roadGeom(t, yg, hwg);
        float wrap = constrain((t - 0.55f) / 0.45f, 0.0f, 1.0f);
        float roll = wrap * wrap * (3.0f - 2.0f * wrap);
        float lanePos = dashLanes[d] * (1.0f - 0.62f * roll);
        int cx = roadCenterX(t, hwg) + runnerLaneOffset(lanePos, hwg);
        int cy = yg - (int)(sinf(roll * 3.1415926f) * 26.0f) - (int)(roll * 8.0f);
        float wrapMid = (i > 0) ? (wrap + prevWrap) * 0.5f : wrap;
        int shade = (int)(210 - 140 * wrapMid);
        uint16_t coreC = RRGB565(shade, shade, max(0, shade - 8));
        int lineW = max(2, (int)(hwg * (0.028f - 0.020f * wrapMid)));
        int halfW = lineW / 2;

        if (i > 0) {
          // 单位法向量(垂直于 prev->cur)，相邻四边形共享顶点无缝拼接
          float dx = cx - prevX, dy = cy - prevY;
          float len = sqrtf(dx * dx + dy * dy);
          if (len > 0.5f) {
            float nx = -dy / len, ny = dx / len;
            int phx = (int)(nx * prevHalfW), phy = (int)(ny * prevHalfW);
            int chx = (int)(nx * halfW), chy = (int)(ny * halfW);
            // 四边形：prev顶, prev底, cur底, cur顶(顺序保证填充正确)
            fillQuad(prevX + phx, prevY + phy, prevX - phx, prevY - phy,
                     cx - chx, cy - chy, cx + chx, cy + chy, coreC);
          }
        }
        prevX = cx; prevY = cy; prevHalfW = halfW; prevWrap = wrap;
      }
    }
  }
}

// 在屏幕指定位置画一辆车的正面大图预览(用于选车页)
// (cx, cy) = 中心，size = 目标高度(像素)
void drawCarPreview(int type, int cx, int cy, int size) {
  if (type < 0 || type >= CAR_PREVIEW_TYPES) return;
  // 用高清预览精灵(120×130)，按目标 size 缩放显示
  int srcW = CAR_PREVIEW_W;
  int srcH = CAR_PREVIEW_H;
  float scale = (float)size / srcH;
  int dstW = (int)(srcW * scale);
  int dstH = size;
  int x0 = cx - dstW / 2;
  int y0 = cy - dstH / 2;
  // 双线性插值采样：2×2 邻域加权平均，柔化放大锯齿
  // 浮点采样位置，取 4 个邻居按距离加权
  for (int dy = 0; dy < dstH; ++dy) {
    float fy = (float)dy * srcH / dstH;
    int sy0 = (int)fy;  int sy1 = min(sy0 + 1, srcH - 1);
    float ty = fy - sy0;
    for (int dx = 0; dx < dstW; ++dx) {
      float fx = (float)dx * srcW / dstW;
      int sx0 = (int)fx;  int sx1 = min(sx0 + 1, srcW - 1);
      float tx = fx - sx0;
      // 4 个邻居的 mask 和 color
      int idx00 = sy0 * srcW + sx0;
      int idx01 = sy0 * srcW + sx1;
      int idx10 = sy1 * srcW + sx0;
      int idx11 = sy1 * srcW + sx1;
      int m00 = pgm_read_byte(&CAR_PREVIEW_MASK[type][idx00]);
      int m01 = pgm_read_byte(&CAR_PREVIEW_MASK[type][idx01]);
      int m10 = pgm_read_byte(&CAR_PREVIEW_MASK[type][idx10]);
      int m11 = pgm_read_byte(&CAR_PREVIEW_MASK[type][idx11]);
      // 加权 mask(透明度)：4 邻居 mask 按距离加权
      float maskSum = (m00 * (1-tx) + m01 * tx) * (1-ty) + (m10 * (1-tx) + m11 * tx) * ty;
      if (maskSum < 0.5f) continue;   // 加权后透明，跳过(边缘半透明像素)
      // 加权颜色(只取不透明邻居)
      auto readColor = [&](int idx) -> uint32_t {
        uint16_t c = pgm_read_word(&CAR_PREVIEW_PIXELS[type][idx]);
        return ((c >> 11) & 0x1F) | (((c >> 5) & 0x3F) << 5) | ((c & 0x1F) << 11);
      };
      // 简化：取主邻居(sx0,sy0)颜色，边缘已通过 mask 加权柔化
      uint16_t c = pgm_read_word(&CAR_PREVIEW_PIXELS[type][idx00]);
      gfxPixel(x0 + dx, y0 + dy, c);
    }
  }
}


static void drawEnemyCar(int sx, int baseY, float depth, int carType, float lane) {
  // 按 NPC 所在车道选视图：lane<-0.34→左(0)，0→正后(1)，>0.34→右(2)
  int view = 1;
  if (lane < -0.34f) view = 0;
  else if (lane > 0.34f) view = 2;
  int frame = carType % ENEMY_CAR_TYPES;
  const int playerNearH = 76;
  const int playerBottom = rSH - 16 - 40 - 20;
  float screenRatio = (float)(baseY - gHorizonY()) / (float)max(1, playerBottom - gHorizonY());
  screenRatio = constrain(screenRatio, 0.0f, 1.18f);
  float perspective = constrain(0.18f + 0.82f * powf(screenRatio, 1.15f), 0.18f, 1.18f);
  // 先读 BBOX，按各车实际宽高比缩放，避免变形(窄车不拉宽、宽车不压扁)
  int srcX0 = ENEMY_CAR_BBOX[carType][view][0];
  int srcY0 = ENEMY_CAR_BBOX[carType][view][1];
  int srcX1 = ENEMY_CAR_BBOX[carType][view][2];
  int srcY1 = ENEMY_CAR_BBOX[carType][view][3];
  int srcW = srcX1 - srcX0 + 1;
  int srcH = srcY1 - srcY0 + 1;
  // 同距离时 NPC 与玩家车等高，再按近大远小的透视缩放。
  // type 6 (Porsche 718) 在游戏中比例偏大，单独缩小 15%
  float carTypeScale = (carType == 6) ? 0.85f : 1.0f;
  int dstH = max(10, (int)(playerNearH * perspective * carTypeScale));
  // 宽度 = 高度 × 该车 BBOX 实际宽高比，保持原始比例不变形
  float carAspect = (float)srcW / (float)srcH;
  int dstW = max(8, (int)(dstH * carAspect));
  int x0 = sx - dstW / 2;
  int y0 = baseY - dstH;
  int clipTop = roadBodyTopY();

  drawHeadlights(sx, baseY, dstW, dstH, depth, false, lane);
  gfxEllipse(sx, baseY - max(1, dstH / 18), max(4, dstW / 3), max(1, dstH / 14), RRGB565(16, 18, 20));
  for (int dy = 0; dy < dstH; ++dy) {
    int sy = srcY0 + (dy * srcH) / dstH;
    int py = y0 + dy;
      if (py < clipTop || py >= rSH) continue;
    for (int dx = 0; dx < dstW; ++dx) {
      int sxImg = srcX0 + (dx * srcW) / dstW;
      int px = x0 + dx;
      if (px < 0 || px >= rSW) continue;
      int srcIdx = sy * ENEMY_CAR_W + sxImg;
      if (!pgm_read_byte(&ENEMY_CAR_MASK[carType][view][srcIdx])) continue;
      uint16_t c = pgm_read_word(&ENEMY_CAR_PIXELS[carType][view][srcIdx]);
      gfxPixel(px, py, c);
    }
  }
}

// 画路障锥（平滑圆锥 + 圆底座）
static void drawCone(int sx, int baseY, int scale) {
  gfxFillTriangle(sx, baseY - scale * 2, sx - scale, baseY, sx + scale, baseY, R_ORANGE_CONE);
  gfxEllipse(sx, baseY, scale, 2, R_WHITE);
  gfxBox(sx - 1, baseY - scale, 2, max(1, scale / 2), R_WHITE);
}

static void drawCoinSpriteAt(int x0, int y0, int dstH, int clipTop, float alpha = 1.0f) {
  alpha = constrain(alpha, 0.0f, 1.0f);
  if (alpha <= 0.0f) return;
  const uint16_t fadeBg = RRGB565(18, 15, 44);
  auto fadePixel = [&](uint16_t c) {
    if (alpha >= 0.99f) return c;
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    int br = (fadeBg >> 11) & 0x1F, bg = (fadeBg >> 5) & 0x3F, bb = fadeBg & 0x1F;
    r = br + (int)((r - br) * alpha);
    g = bg + (int)((g - bg) * alpha);
    b = bb + (int)((b - bb) * alpha);
    return (uint16_t)((r << 11) | (g << 5) | b);
  };
  int dstW = dstH;
  for (int dy = 0; dy < dstH; ++dy) {
    int sy = (dy * COIN_SPRITE_H) / dstH;
    int py = y0 + dy;
    if (py < clipTop || py >= rSH) continue;
    for (int dx = 0; dx < dstW; ++dx) {
      int sxImg = (dx * COIN_SPRITE_W) / dstW;
      int px = x0 + dx;
      if (px < 0 || px >= rSW) continue;
      int srcIdx = sy * COIN_SPRITE_W + sxImg;
      if (!pgm_read_byte(&COIN_SPRITE_MASK[srcIdx])) continue;
      gfxPixel(px, py, fadePixel(pgm_read_word(&COIN_SPRITE_PIXELS[srcIdx])));
    }
  }
}

static void drawCoin(int sx, int baseY, float depth) {
  int dstH = coinDrawSizeForBaseY(baseY);
  int liftOff = (int)(dstH * 1.4f);
  int x0 = sx - dstH / 2;
  int y0 = baseY - dstH - liftOff;
  float bob = sinf(millis() * 0.004f + sx * 0.05f) * 3.0f;
  drawCoinSpriteAt(x0, y0 + (int)bob, dstH, roadBodyTopY());
}

static void drawCoinPops() {
  uint32_t now = millis();
  for (int i = 0; i < COIN_POP_MAX; ++i) {
    if (!coinPops[i].active) continue;
    uint32_t elapsed = now - coinPops[i].startMs;
    if (elapsed >= 280) {
      coinPops[i].active = false;
      continue;
    }
    float t = (float)elapsed / 280.0f;
    float ease = t * t * (3.0f - 2.0f * t);
    int rise = (int)(34.0f * ease);
    int size = max(8, (int)(coinPops[i].size * (1.0f - 0.22f * ease)));
    int cx = coinPops[i].x + coinPops[i].size / 2;
    int x0 = cx - size / 2;
    int y0 = coinPops[i].y - rise;
    float alpha = (1.0f - ease) * (1.0f - ease);
    drawCoinSpriteAt(x0, y0, size, 0, alpha);
  }
}

static void fillQuad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c) {
  gfxFillTriangle(x0, y0, x1, y1, x2, y2, c);
  gfxFillTriangle(x0, y0, x2, y2, x3, y3, c);
}

static uint16_t blendPixelLight(uint16_t bgC, uint16_t lightC, float alpha) {
  alpha = constrain(alpha, 0.0f, 1.0f);
  int rr = (bgC >> 11) & 0x1F, rg = (bgC >> 5) & 0x3F, rb = bgC & 0x1F;
  int lr = (lightC >> 11) & 0x1F, lg = (lightC >> 5) & 0x3F, lb = lightC & 0x1F;
  int r = rr + (int)((lr - rr) * alpha);
  int g = rg + (int)((lg - rg) * alpha);
  int b = rb + (int)((lb - rb) * alpha);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static void drawSoftLightFan(int cx0, int y0, int half0, int cx1, int y1, int half1,
                             uint16_t lightC, float strength) {
  if (y0 <= y1 || half0 <= 0 || half1 <= 0) return;
  int h = max(1, y0 - y1);
  for (int y = y1; y <= y0; ++y) {
    float p = (float)(y0 - y) / (float)h;  // 0=车前近端, 1=远端
    float cx = cx0 + (cx1 - cx0) * p;
    float half = half0 + (half1 - half0) * p;
    float farFade = 1.0f - (p * p * (3.0f - 2.0f * p));
    int xl = max(0, (int)(cx - half - 1));
    int xr = min(rSW - 1, (int)(cx + half + 1));
    for (int x = xl; x <= xr; ++x) {
      float edge = 1.0f - fabsf((float)x - cx) / max(1.0f, half);
      if (edge <= 0.0f) continue;
      edge = edge * edge * (3.0f - 2.0f * edge);
      float alpha = strength * edge * farFade;
      if (alpha <= 0.035f) continue;
      uint16_t bg = gfxReadPixel(x, y);
      gfxPixel(x, y, blendPixelLight(bg, lightC, min(0.76f, alpha)));
    }
  }
}

static void drawHeadlights(int cx, int baseY, int carW, int carH, float depth, bool player, float lane) {
  int roadTop = roadBodyTopY();
  if (baseY <= roadTop + 18) return;

  int laneLightShift = 0;
  if (lane < -0.34f) laneLightShift = 10;
  else if (lane > 0.34f) laneLightShift = -10;
  cx += laneLightShift;

  float nearT = player ? 1.0f : constrain(1.0f - depth, 0.18f, 1.0f);
  int startY = baseY - carH + max(6, carH / 8);
  startY = constrain(startY, roadTop + 10, baseY - 5);
  int beamLen = player ? 42 : max(12, (int)(carH * (0.38f + nearT * 0.18f)));
  int endY = max(roadTop + 10, startY - beamLen);
  if (startY - endY < 10) return;

  int nearHalf = player ? max(10, carW * 29 / 100) : max(8, carW * 22 / 100);
  int farHalf = player ? max(42, carW * 84 / 100) : max(10, carW * 62 / 100);
  int drift = (int)(rCamX * 0.035f);
  int endX = cx + drift;
  uint16_t lightC = player ? RRGB565(238, 192, 92) : RRGB565(190, 150, 76);
  drawSoftLightFan(cx, startY, nearHalf, endX, endY + 8, farHalf,
                   lightC, player ? 0.95f : 0.68f);

  for (int side = -1; side <= 1; side += 2) {
    int lampX = cx + side * max(3, carW / 9);
    gfxPixel(lampX, startY, player ? RRGB565(255, 226, 132) : RRGB565(220, 186, 106));
    if (player) gfxPixel(lampX - side, startY, RRGB565(190, 146, 74));
  }
}

// ---- 绘制：障碍物（按 z 从远到近排序）----
static void drawObstaclesSorted() {
  int idx[OBSTACLE_MAX];
  int n = 0;
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (obstacles[i].active && obstacles[i].z > 0.0f) idx[n++] = i;
  }
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (obstacles[idx[j]].z > obstacles[idx[i]].z) {
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
      }
    }
  }
  for (int k = 0; k < n; ++k) {
    Obstacle& o = obstacles[idx[k]];
    float t = o.z;
    if (t <= 0.0f || t > 1.0f) continue;
    int y, halfW;
    roadGeom(t, y, halfW);
    int cx = roadCenterX(t, halfW);
    int sx = cx + runnerLaneOffset(o.lane, halfW);
    // NPC 左右车道向内侧偏移：近端比玩家车再向外 10 显示像素，远端与玩家车一致
    // 系数随深度 t 线性变化：近端(t=0)向内偏移减少 0.053，远端(t=1)不变
    float npcLaneAdj = halfW * (0.214f - 0.214f * (1.0f - t));
    if (o.lane < -0.34f) sx += (int)npcLaneAdj;     // 左车道 → 往右(内)
    else if (o.lane > 0.34f) sx -= (int)npcLaneAdj; // 右车道 → 往左(内)
    int scale = max(3, (int)((1.0f - t) * 26.0f));
    if (o.type == 0) drawEnemyCar(sx, y, t, o.colorIdx, o.lane);
    else if (o.type == 2) drawCoin(sx, y, t);
    else drawCone(sx, y, scale);
  }
}

static void drawCarSprite(const uint16_t* pixels, const uint8_t* mask, int x, int y, float scale = 1.0f) {
  // scale=1.0 半尺寸(75x58), scale=1.5 原尺寸, 此处用倒数算步进
  // 目标尺寸 = CAR_SPRITE_W * scale / 2
  int dstW = (int)(CAR_SPRITE_W * scale / 2.0f);
  int dstH = (int)(CAR_SPRITE_H * scale / 2.0f);
  for (int dy = 0; dy < dstH; ++dy) {
    int sy = y + dy;
    if (sy < 0 || sy >= rSH) continue;
    int srcY = (int)(dy * 2.0f / scale);   // 反向映射到源
    int row = srcY * CAR_SPRITE_W;
    for (int dx = 0; dx < dstW; ++dx) {
      int sx = x + dx;
      if (sx < 0 || sx >= rSW) continue;
      int srcX = (int)(dx * 2.0f / scale);
      int idx = row + srcX;
      if (!pgm_read_byte(&mask[idx])) continue;
      gfxPixel(sx, sy, pgm_read_word(&pixels[idx]));
    }
  }
}

// ---- 绘制：玩家车辆（直接使用三视图精灵图片）----
// 用滞后区间(hysteresis)避免帧切换抖动 + 整车横移模拟过渡
// 玩家车使用 NPC #3(红色带备胎吉普)的正面精灵，固定正面视图
static void drawPlayerCar() {
  int y, halfW;
  roadGeom(0.0f, y, halfW);
  // 相机跟车后，玩家车基本钉在屏幕中央；仅保留极小的转向偏摆增强手感
  int cx = rSW / 2 + runnerLaneOffset(rCarX, halfW);
  // 两侧车道向内侧偏移 40 显示像素(≈78 渲染像素)，靠近内侧虚线
  // 用 rTargetLane(目标车道)判断，避免过渡中阈值切换导致晃动
  if (rTargetLane == 0) cx += 78;        // 目标左车道 → 往右(中心方向)移
  else if (rTargetLane == 2) cx -= 78;   // 目标右车道 → 往左(中心方向)移
  // 车身随转向轻微横移(丝滑过渡感)
  cx += (int)(rSteerVis * 5.0f);

  // 只放大玩家车；NPC 车辆仍使用 drawEnemyCar 中的原尺寸。
  const int playerNearH = 88;
  int carBottom = rSH - 16 - 40 - 20;   // 固定底边位置
  // 入场动画：玩家车从屏幕底部开上来，1s 内平滑到达 carBottom
  uint32_t introElapsed = millis() - rIntroStartMs;
  if (introElapsed < RACING_CAR_INTRO_MS) {
    float introT = constrain((float)introElapsed / RACING_CAR_INTRO_MS, 0.0f, 1.0f);
    float ease = introT * introT * (3.0f - 2.0f * introT);   // smoothstep
    int offY = (int)((1.0f - ease) * 120.0f);   // 从下方 120px 处上移
    carBottom += offY;
  }
  // 玩家车在近端，透视=1.0(全尺寸)
  const float perspective = 1.0f;
  int dstH = (int)(playerNearH * perspective);

  // 用 #8 号 NPC 车的 BBOX 计算宽高比，保持不变形
  // 玩家车按目标车道选视图(用 rTargetLane，过渡中不切换，避免晃动)
  int playerView = 1;   // 中间车道 → 正后视图
  if (rTargetLane == 0) playerView = 0;   // 左车道 → 左视图
  else if (rTargetLane == 2) playerView = 2;  // 右车道 → 右视图
  int srcX0 = ENEMY_CAR_BBOX[PLAYER_CAR_TYPE][playerView][0];
  int srcY0 = ENEMY_CAR_BBOX[PLAYER_CAR_TYPE][playerView][1];
  int srcX1 = ENEMY_CAR_BBOX[PLAYER_CAR_TYPE][playerView][2];
  int srcY1 = ENEMY_CAR_BBOX[PLAYER_CAR_TYPE][playerView][3];
  int srcW = srcX1 - srcX0 + 1;
  int srcH = srcY1 - srcY0 + 1;
  float carAspect = (float)srcW / (float)srcH;
  int dstW = max(8, (int)(dstH * carAspect));
  cx = constrain(cx, dstW / 2 + 3, rSW - dstW / 2 - 3);
  int x0 = cx - dstW / 2;
  int y0 = carBottom - dstH + 4;

  drawHeadlights(cx, carBottom, dstW, dstH, 0.0f, true, R_LANES[rTargetLane]);
  // 整车投影：使用玩家车 mask 的轮廓压扁生成，比几何梯形更自然。
  float speedRatio = constrain(rSpeed / 2.04f, 0.0f, 1.0f);
  float bobFreq = 6.0f + speedRatio * 8.0f;
  float bobAmp = 1.5f + speedRatio * 3.5f;
  float bob = sinf(millis() * 0.001f * bobFreq * 6.28318f) * bobAmp;
  float turnAmt = min(1.0f, fabsf(rSteerVis));
  int leanSide = (rSteerVis > 0) ? 1 : (rSteerVis < 0 ? -1 : 0);
  int leanOff = leanSide * (int)(dstW * 0.12f * turnAmt);
  int shadowH = max(14, dstH * 30 / 100);
  int shadowTop = carBottom - shadowH + 16 + (int)(bob * 0.35f);
  float shadowScaleX = (float)(dstW + 18) / (float)srcW;
  float srcCenter = srcX0 + srcW * 0.5f;
  for (int dy = 0; dy < shadowH; ++dy) {
    int srcY = srcY0 + (dy * srcH) / shadowH;
    int minX = srcX1 + 1;
    int maxX = srcX0 - 1;
    for (int sxImg = srcX0; sxImg <= srcX1; ++sxImg) {
      int srcIdx = srcY * ENEMY_CAR_W + sxImg;
      if (!pgm_read_byte(&ENEMY_CAR_MASK[PLAYER_CAR_TYPE][playerView][srcIdx])) continue;
      if (sxImg < minX) minX = sxImg;
      if (sxImg > maxX) maxX = sxImg;
    }
    if (maxX < minX) continue;
    float rowT = (float)dy / (float)max(1, shadowH - 1);
    int py = shadowTop + dy;
    if (py < 0 || py >= rSH) continue;
    int rowLean = (int)(leanOff * (0.35f + rowT * 0.65f));
    int outerL = cx + rowLean + (int)((minX - srcCenter) * shadowScaleX) - 3;
    int outerR = cx + rowLean + (int)((maxX - srcCenter) * shadowScaleX) + 3;
    int innerL = cx + rowLean + (int)((minX - srcCenter) * (shadowScaleX * 0.94f));
    int innerR = cx + rowLean + (int)((maxX - srcCenter) * (shadowScaleX * 0.94f));
    outerL = max(0, outerL);
    outerR = min(rSW - 1, outerR);
    innerL = max(0, innerL);
    innerR = min(rSW - 1, innerR);
    if (outerR >= outerL) gfxBox(outerL, py, outerR - outerL + 1, 1, RRGB565(22, 8, 38));
    if (innerR >= innerL) gfxBox(innerL, py, innerR - innerL + 1, 1, RRGB565(10, 4, 22));
  }

  // 绘制玩家车精灵(红色吉普，按车道切换左/正后/右视图)
  for (int dy = 0; dy < dstH; ++dy) {
    int sy = srcY0 + (dy * srcH) / dstH;
    int py = y0 + dy;
    if (py < 0 || py >= rSH) continue;
    for (int dx = 0; dx < dstW; ++dx) {
      int sxImg = srcX0 + (dx * srcW) / dstW;
      int px = x0 + dx;
      if (px < 0 || px >= rSW) continue;
      int srcIdx = sy * ENEMY_CAR_W + sxImg;
      if (!pgm_read_byte(&ENEMY_CAR_MASK[PLAYER_CAR_TYPE][playerView][srcIdx])) continue;
      uint16_t c = pgm_read_word(&ENEMY_CAR_PIXELS[PLAYER_CAR_TYPE][playerView][srcIdx]);
      gfxPixel(px, py, c);
    }
  }
}


// ---- 绘制：HUD（顶部分数条 + 底部速度仪表）----
static void drawHUD() {
  // 整个 HUD 开场完全不可见，2s 后从 0% 到 100% 淡入。
  if (!rHudFadeActive) return;
  uint32_t fadeElapsed = millis() - rHudFadeStartMs;
  float hudAlpha = constrain((float)fadeElapsed / 1000.0f, 0.0f, 1.0f);
  if (hudAlpha <= 0.01f) return;
  // 纯淡入：不混紫色，0 时完全不可见，随后从暗到原色。
  auto fadeColor = [&](uint16_t c) {
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (int)(r * hudAlpha);
    g = (int)(g * hudAlpha);
    b = (int)(b * hudAlpha);
    return (uint16_t)((r << 11) | (g << 5) | b);
  };
  uint16_t cWhite = fadeColor(R_WHITE);
  uint16_t cRed = fadeColor(R_RED);
  uint16_t cDim = fadeColor(R_DIM);
  uint16_t cYellow = fadeColor(R_YELLOW);

  // 顶部信息居中显示：标签 24px(size3) + 数字 32px(size4)，间距 8px
  char buf[32];
  int centerX = rSW / 2;
  int scoreX = centerX - 80;   // 中心偏左：SCORE
  int bestX = centerX + 80;    // 中心偏右：BEST
  snprintf(buf, sizeof(buf), "%d", rCoinScore);
  // 全部用 Font4(原生最大矢量字体)，无像素感
  gfxTextCenterS("SCORE", scoreX, 68, 2, cDim, 0xFFFF);      // 标签 Font0×2 (~16px)
  gfxTextCenterF4(buf, scoreX, 104, 1, cWhite, 0xFFFF);      // 数字 Font4(同仪表盘)
  snprintf(buf, sizeof(buf), "%d", (int)rHighScore);
  gfxTextCenterS("BEST", bestX, 68, 2, cDim, 0xFFFF);       // 标签 Font0×2 (~16px)
  gfxTextCenterF4(buf, bestX, 104, 1, cYellow, 0xFFFF);

  int carBottom = rSH - 16 - 40 - 20;
  int gx = rSW / 2;                    // 居中
  int gy = carBottom + 55;             // 车底下方
  int gr = 32;
  // 半圆描边弧
  for (int a = 180; a >= 0; a -= 6) {
    float rad = a * (3.14159265f / 180.0f);
    int px = gx + (int)(cosf(rad) * gr);
    int py = gy - (int)(sinf(rad) * gr);
    gfxBox(px - 1, py - 1, 2, 2, cWhite);
  }
  // 指针(红色)
  float realRatio = constrain(rSpeed / 3.0f, 0.0f, 1.0f);
  float needleAngle = (180.0f - realRatio * 180.0f) * (3.14159265f / 180.0f);
  int nx = gx + (int)(cosf(needleAngle) * (gr - 6));
  int ny = gy - (int)(sinf(needleAngle) * (gr - 6));
  gfxWideLine(gx, gy, nx, ny, 2, cRed);
  gfxCircle(gx, gy, 3, cRed);
  // 速度数字
  int kmh = (int)(rSpeed * 100.0f);
  snprintf(buf, sizeof(buf), "%d", kmh);
  // 数字放在半圆内，Font4(大号加粗)，透明背景
  gfxTextCenter(buf, gx, gy - 2, 4, cWhite, 0xFFFF);
  gfxTextCenter("km/h", gx, gy + 24, 0, cDim, 0xFFFF);

  // 冲出赛道警告
  if (rOffTrack && !rGameOver) {
    int remain = max(0, (int)(rOffTrackUntil - millis()) / 1000 + 1);
    gfxRoundBox(rSW / 2 - 130, 40, 260, 50, 8, R_RED);
    gfxTextCenter("OFF TRACK!", rSW / 2, 58, 4, R_WHITE, R_RED);
    char wb[16];
    snprintf(wb, sizeof(wb), "Get back! %ds", remain);
    gfxTextCenter(wb, rSW / 2, 78, 0, R_WHITE, R_RED);
  }
}

// ---- Game Over 覆盖层 ----
static void drawGameOver() {
  gfxBox(0, 0, rSW, rSH, RRGB565(10, 10, 12));
  int bw = 280, bh = 160;
  int bx = rSW / 2 - bw / 2, by = rSH / 2 - bh / 2;
  gfxRoundBox(bx, by, bw, bh, 12, R_PANEL);
  gfxRectLine(bx, by, bw, bh, R_WHITE);
  gfxTextCenter("GAME OVER", rSW / 2, by + 38, 4, R_RED, R_PANEL);
  char buf[32];
  snprintf(buf, sizeof(buf), "SCORE  %d", rCoinScore);
  gfxTextCenter(buf, rSW / 2, by + 78, 4, R_WHITE, R_PANEL);
  snprintf(buf, sizeof(buf), "BEST   %d", (int)rHighScore);
  gfxTextCenter(buf, rSW / 2, by + 104, 2, R_YELLOW, R_PANEL);
  gfxTextCenter("Press A to restart", rSW / 2, by + 138, 0, R_WHITE, R_PANEL);
}

// ---- 主绘制 ----
void racingDraw() {
  // 玩家向左时半圆向右，玩家向右时半圆向左，制造镜头反向跟随感。
  rCamX = rCarX * 52.0f;
  drawSkyAndRoad();
  drawObstaclesSorted();
  drawPlayerCar();
  drawCoinPops();
  drawHUD();
  if (rGameOver) drawGameOver();
  gfxPushFrame();
}
