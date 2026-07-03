#include "racing.h"
#include "racing_car_sprites.h"
#include "racing_enemy_car_sprites.h"
#include <M5Unified.h>
#include <math.h>
#include <pgmspace.h>

// ---- 由 main.cpp 提供的共享绘制桥接（在 main.cpp 末尾定义）----
extern "C" {
  bool gfxUseCanvas();
  void gfxFillScreen(uint16_t c);
  void gfxBox(int x, int y, int w, int h, uint16_t c);
  void gfxPixel(int x, int y, uint16_t c);
  void gfxRoundBox(int x, int y, int w, int h, int r, uint16_t c);
  void gfxRectLine(int x, int y, int w, int h, uint16_t c);
  void gfxLine(int x0, int y0, int x1, int y1, uint16_t c);
  void gfxWideLine(int x0, int y0, int x1, int y1, int w, uint16_t c);
  void gfxFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
  void gfxCircle(int x, int y, int r, uint16_t c);
  void gfxEllipse(int x, int y, int rx, int ry, uint16_t c);
  void gfxDrawCircle(int x, int y, int r, uint16_t c);
  void gfxDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c);
  void gfxTextCenter(const char* s, int x, int y, int font, uint16_t c, uint16_t bg);
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

// ---- 伪3D 投影参数 ----
static const float HORIZON_RATIO = 0.42f;   // 地平线在屏幕 42% 处
static const float ROAD_W_NEAR   = 0.86f;   // 近端路宽占屏幕比例
static const float ROAD_W_FAR    = 0.06f;   // 远端路宽
static const float CURVE_STRENGTH = 2.2f;   // 弯道视觉强度

// ---- 游戏状态 ----
struct Obstacle {
  float z;        // 深度(远大近小)，>0
  float lane;     // 横向位置(归一化 -1..1，0=路中)
  int   type;     // 0=敌车 1=路障
  int   colorIdx; // 敌车车型索引(0..8)
  float sizeMul;  // 敌车大小倍率(0.8..1.3)
  bool  active;
};
static const int OBSTACLE_MAX = 24;
static Obstacle obstacles[OBSTACLE_MAX];

static float rSpeed      = 0.0f;     // 当前速度
static float rMaxSpeed   = 1.6f;     // 速度上限(随分数提升)
static float rMinSpeed   = 0.35f;    // 最低速度
static float rDist       = 0.0f;     // 行驶距离(=分数)
static float rCarX       = 0.0f;     // 玩家车横向位置(归一化 -1..1)
static float rSteerVis   = 0.0f;     // 玩家车转向视觉偏摆(-1左..1右)
static int8_t  rCarFrame = 0;        // 当前显示帧: -1=左 0=中 1=右(滞后切换)
static float rCurve      = 0.0f;     // 当前弯道偏移(视觉)
static float rCurveTgt   = 0.0f;     // 目标弯道
static float rSroll      = 0.0f;     // 路面纹理滚动
static float rObstTimer  = 0.0f;     // 障碍生成计时
static float rHighScore  = 0.0f;     // 最高分(内存)
static bool  rGameOver   = false;
static bool  rExitFlag   = false;
static bool  rOffTrack   = false;       // 是否冲出赛道
static uint32_t rOffTrackUntil = 0;     // 冲出赛道后 Game Over 倒计时

// ---- 伪3D 投影：把世界(横向归一化x, 深度z)映射到屏幕坐标 ----
// depth: 0=近端(屏幕底), 1=远端(地平线)
struct Proj { int x; int y; int scale; };

static int gHorizonY() { return (int)(rSH * HORIZON_RATIO); }

// 给定深度 t(0近..1远)，返回路宽(像素)和该行的 y
static void roadGeom(float t, int& y, int& halfW) {
  y = rSH - (int)((rSH - gHorizonY()) * t);
  float wRatio = ROAD_W_NEAR + (ROAD_W_FAR - ROAD_W_NEAR) * t;
  halfW = (int)(wRatio * rSW * 0.5f);
}

// 投影一个世界点：laneX(-1..1 路面内)，t(0近..1远) → 屏幕 x
static int projectX(float laneX, float t, int halfW) {
  int cx = rSW / 2;
  // 弯道偏移：越远偏移越大
  float curveOff = rCurve * t * t * CURVE_STRENGTH * (rSW * 0.5f);
  return cx + (int)(laneX * halfW) + (int)curveOff;
}

// ---- 初始化 ----
void racingInit() {
  rSW = min(RACING_RENDER_W, M5.Display.width());
  rSH = min(RACING_RENDER_H, M5.Display.height());
  rSpeed = 0.0f;
  rMaxSpeed = 1.6f;
  rDist = 0.0f;
  rCarX = 0.0f;
  rSteerVis = 0.0f;
  rCarFrame = 0;
  rCurve = 0.0f;
  rCurveTgt = 0.0f;
  rSroll = 0.0f;
  rObstTimer = 0.0f;
  rGameOver = false;
  rExitFlag = false;
  rOffTrack = false;
  rOffTrackUntil = 0;
  for (int i = 0; i < OBSTACLE_MAX; ++i) obstacles[i].active = false;
}

// ---- 障碍生成 ----
static void spawnObstacle() {
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) {
      obstacles[i].active = true;
      obstacles[i].z = 1.0f;                 // 从远端出现
      static const float lanes[3] = { -0.65f, 0.0f, 0.65f };
      obstacles[i].lane = lanes[rand() % 3]; // 左/中/右三视图随机出现
      obstacles[i].type = 0;  // 只生成车辆，不生成路障
      obstacles[i].colorIdx = rand() % ENEMY_CAR_TYPES; // 随机车型
      obstacles[i].sizeMul = 1.0f; // 大小只由深度决定，保证同位置同尺寸
      return;
    }
  }
}

// ---- 输入 ----
void racingHandleInput(bool accel, bool brake, float steerX) {
  if (rGameOver) return;
  // 速度
  if (accel) rSpeed += 1.8f * 0.016f;     // 加速
  else if (brake) rSpeed -= 3.5f * 0.016f; // 刹车
  else rSpeed -= 0.5f * 0.016f;            // 自然减速
  rSpeed = constrain(rSpeed, rMinSpeed, rMaxSpeed);
  // 转向：速度越快转向略弱(更难)
  float steerFactor = 1.0f - (rSpeed / rMaxSpeed) * 0.35f;
  rCarX += steerX * 0.045f * steerFactor;
  // 弯道漂移：弯道会把车往外侧甩(不主动转向就会被甩出去)
  rCarX -= rCurve * 0.018f * (rSpeed / rMaxSpeed);
  rCarX = constrain(rCarX, -1.5f, 1.5f);   // 允许冲出赛道(赛道边界=±1.0)
  rSteerVis = steerX;   // 即时回正，无延迟
}

// ---- 更新 ----
void racingUpdate(float dt) {
  if (rExitFlag) return;
  if (rGameOver) return;

  // 距离/分数累积
  rDist += rSpeed * dt * 60.0f;

  // 难度：随距离提升速度上限 + 障碍频率
  rMaxSpeed = 1.6f + min(rDist / 4000.0f, 1.4f);

  // 路面纹理滚动（加速，让道路和虚线明显往后跑）
  rSroll += rSpeed * dt * 35.0f;
  // 即时回正，无需衰减

  // 弯道随机变化
  if ((rand() % 200) < 3) rCurveTgt = ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
  rCurve += (rCurveTgt - rCurve) * 0.02f;

  // 障碍更新：向近端移动(z 减小)
  float moveAmount = rSpeed * dt * 0.65f;
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) continue;
    obstacles[i].z -= moveAmount;
    if (obstacles[i].z <= 0.0f) obstacles[i].active = false;  // 飞过玩家
  }

  // 障碍生成
  rObstTimer -= dt;
  if (rObstTimer <= 0.0f) {
    spawnObstacle();
    float interval = 1.3f - min(rDist / 6000.0f, 0.85f);  // 越远越频繁
    rObstTimer = max(0.35f, interval);
  }

  // 碰撞检测：障碍到达近端区域且横向重叠
  for (int i = 0; i < OBSTACLE_MAX; ++i) {
    if (!obstacles[i].active) continue;
    if (obstacles[i].z > 0.12f || obstacles[i].z < 0.0f) continue;
    float laneDiff = fabsf(obstacles[i].lane - rCarX);
    if (laneDiff < 0.35f) {
      rGameOver = true;
      if (rDist > rHighScore) rHighScore = rDist;
    }
  }

  // 冲出赛道检测：车偏出赛道边界(|rCarX| > 1.0)
  if (!rOffTrack && !rGameOver) {
    if (fabsf(rCarX) > 1.0f) {
      rOffTrack = true;
      rOffTrackUntil = millis() + 3000;   // 3 秒后 Game Over
    }
  } else if (rOffTrack) {
    // 回到赛道内则取消(玩家及时纠正方向)
    if (fabsf(rCarX) < 0.95f) {
      rOffTrack = false;
    } else if (millis() >= rOffTrackUntil) {
      rGameOver = true;
      if (rDist > rHighScore) rHighScore = rDist;
    }
  }
}

bool racingIsGameOver() { return rGameOver; }
bool racingWantsExit()  { return rExitFlag; }

// 重开（外部按键触发）
static void racingRestart() {
  rSpeed = 0.0f;
  rMaxSpeed = 1.6f;
  rDist = 0.0f;
  rCarX = 0.0f;
  rSteerVis = 0.0f;
  rCarFrame = 0;
  rCurve = 0.0f;
  rCurveTgt = 0.0f;
  rSroll = 0.0f;
  rObstTimer = 0.0f;
  rGameOver = false;
  for (int i = 0; i < OBSTACLE_MAX; ++i) obstacles[i].active = false;
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

static uint16_t buildingColor(int seed, int shade) {
  // 白天建筑配色：明亮暖色调(米黄/浅灰/淡蓝/砖红等)
  static const uint8_t pal[6][3] = {
    {210, 200, 185}, {200, 185, 165}, {185, 195, 205},
    {195, 180, 170}, {180, 190, 185}, {205, 195, 180}
  };
  int idx = seed % 6;
  return RRGB565((uint8_t)constrain(pal[idx][0] + shade, 0, 255),
                 (uint8_t)constrain(pal[idx][1] + shade, 0, 255),
                 (uint8_t)constrain(pal[idx][2] + shade, 0, 255));
}

static void drawBuilding(int x, int baseY, int w, int h, int side, int seed) {
  if (w < 4 || h < 6) return;
  int depth = max(2, w / 4);
  int topY = baseY - h;
  uint16_t face = buildingColor(seed, 0);
  uint16_t lit = buildingColor(seed, 20);
  uint16_t dark = buildingColor(seed, -24);
  uint16_t roof = RRGB565(120, 110, 100);          // 屋顶暖灰(白天)
  uint16_t win = RRGB565(190, 220, 240);           // 窗户：白天蓝天反光(淡蓝)
  uint16_t winDim = RRGB565(130, 150, 165);        // 暗窗

  int frontX = x;
  int sideX = x + side * depth;   // 侧面后边缘 x(右边楼在右，左边楼在左)
  int faceW = w;
  if (side < 0) frontX = x - w;   // 左边楼：正面在 x-w..x

  gfxBox(frontX, topY, faceW, h, face);
  if (side > 0) {
    // 右边楼：侧面从正面右边缘(frontX+faceW=x)向右后方延伸到 sideX(=x+depth)
    gfxFillTriangle(frontX + faceW, topY, sideX, topY + depth, frontX + faceW, baseY, dark);
    gfxFillTriangle(sideX, topY + depth, sideX, baseY + depth, frontX + faceW, baseY, dark);
    // 屋顶梯形：正面顶边(frontX..frontX+faceW) → 后边(sideX.. 偏移)
    gfxFillTriangle(frontX, topY, frontX + faceW, topY, sideX, topY + depth, roof);
    gfxFillTriangle(frontX, topY, sideX, topY + depth, frontX, topY, roof);
  } else {
    // 左边楼：正面在 frontX(=x-w)..x，侧面从正面左边缘(frontX)向左后方延伸
    // sideX 应该 = frontX - depth = x - w - depth
    int leftSideX = frontX - depth;
    gfxFillTriangle(frontX, topY, leftSideX, topY + depth, frontX, baseY, lit);
    gfxFillTriangle(leftSideX, topY + depth, leftSideX, baseY + depth, frontX, baseY, lit);
    // 屋顶梯形：正面顶边(frontX..frontX+faceW) → 左后角(leftSideX, topY+depth)
    gfxFillTriangle(frontX, topY, frontX + faceW, topY, leftSideX, topY + depth, roof);
    gfxFillTriangle(frontX + faceW, topY, leftSideX, topY + depth, frontX, topY, roof);
  }

  int wx0 = frontX + max(2, w / 6);
  int wy0 = topY + max(3, h / 8);
  int ww = max(1, w / 8);
  int wh = max(1, h / 11);
  int colStep = max(4, w / 4);
  int rowStep = max(5, h / 5);
  int col = 0, row = 0;
  for (int wy = wy0; wy < baseY - wh - 2; wy += rowStep, ++row, col = 0) {
    for (int wx = wx0; wx < frontX + faceW - ww - 1; wx += colStep, ++col) {
      // 用建筑内部的行列索引决定亮暗(固定，不随屏幕滚动闪烁)
      uint16_t wc = (((col * 3 + row * 7 + seed * 17) % 5) == 0) ? win : winDim;
      gfxBox(wx, wy, ww, wh, wc);
    }
  }

  gfxBox(frontX, baseY - max(2, h / 18), faceW, max(2, h / 18), RRGB565(48, 50, 54));
}

// ---- 绘制：天空 + 草地 + 路面 + 路边装饰 ----
static void drawSkyAndRoad() {
  int horizonY = gHorizonY();

  // 1. 整屏先填城市地面底色（白天，明亮）
  gfxFillScreen(RRGB565(150, 158, 160));

  // 2. 天空（白天：明亮鲜蓝渐变，上深下浅）
  for (int y = 0; y < horizonY; y += 4) {
    float t = (float)y / (float)max(1, horizonY);
    int hh = min(4, horizonY - y);
    uint16_t c = RRGB565(100 + (int)(80 * t), 165 + (int)(60 * t), 230 + (int)(20 * t));
    gfxBox(0, y, rSW, hh, c);
  }

  // 3. 太阳（右上角，白天标志）
  int sunX = rSW - 80, sunY = 50;
  gfxCircle(sunX, sunY, 26, RRGB565(255, 245, 180));   // 外圈柔光
  gfxCircle(sunX, sunY, 20, RRGB565(255, 250, 210));   // 本体
  gfxCircle(sunX, sunY, 14, RRGB565(255, 253, 230));   // 高光

  // 4. 白云（白天，更白更亮）
  drawCloud(rSW * 0.2f, horizonY * 0.3f, 10);
  drawCloud(rSW * 0.7f, horizonY * 0.2f, 8);
  drawCloud(rSW * 0.5f, horizonY * 0.45f, 12);
  drawCloud(rSW * 0.88f, horizonY * 0.4f, 7);

  // 5. 远处城市天际线（白天，明亮色调）
  for (int i = 0; i < 12; ++i) {
    int bw = 22 + (i % 4) * 7;
    int bh = 22 + ((i * 11) % 34);
    int bx = -10 + i * 42;
    gfxBox(bx, horizonY - bh, bw, bh, RRGB565(140 + (i % 3) * 10, 150 + (i % 4) * 8, 165 + (i % 2) * 12));
  }

  // 6. 城市地面 + 路面：白天明亮色调
  for (int y = horizonY; y < rSH; y += 2) {
    float t = (float)(rSH - y) / (float)max(1, rSH - horizonY);  // 1远..0近
    t = constrain(t, 0.0f, 1.0f);
    int halfW = (int)((ROAD_W_NEAR + (ROAD_W_FAR - ROAD_W_NEAR) * t) * rSW * 0.5f);
    int cx = rSW / 2 + (int)(rCurve * t * t * CURVE_STRENGTH * (rSW * 0.5f));
    int roadLeft = cx - halfW;
    int roadRight = cx + halfW;

    float near = 1.0f - t;
    int sideShade = (int)(24.0f * near);
    uint16_t sideC = RRGB565(135 + sideShade, 142 + sideShade, 145 + sideShade);   // 地面更亮
    int roadBase = 95 + (int)(24.0f * t);   // 路面更亮
    int noise = (((int)(y * 17 + rSroll * 23.0f)) & 7) - 3;
    uint16_t roadC = RRGB565(roadBase + noise, roadBase + noise, roadBase + noise + 4);
    uint16_t shoulderC = RRGB565(190 + (int)(20.0f * t), 195 + (int)(18.0f * t), 195 + (int)(18.0f * t));

    if (roadLeft > 0) gfxBox(0, y, roadLeft, 2, sideC);
    gfxBox(max(0, roadLeft), y, min(rSW, roadRight) - max(0, roadLeft), 2, roadC);
    if (roadRight < rSW) gfxBox(roadRight, y, rSW - roadRight, 2, sideC);

    if (halfW > 8) {
      gfxBox(roadLeft - 2, y, 2, 2, shoulderC);
      gfxBox(roadRight, y, 2, 2, shoulderC);
    }
  }

  // 6. 细边线和车道虚线，使用透视线段而不是矩形块。
  for (float t0 = 0.02f; t0 < 0.98f; t0 += 0.055f) {
    float t1 = min(0.98f, t0 + 0.055f);
    int y0, hw0, y1, hw1;
    roadGeom(t0, y0, hw0);
    roadGeom(t1, y1, hw1);
    int cx0 = rSW / 2 + (int)(rCurve * t0 * t0 * CURVE_STRENGTH * (rSW * 0.5f));
    int cx1 = rSW / 2 + (int)(rCurve * t1 * t1 * CURVE_STRENGTH * (rSW * 0.5f));
    uint16_t edgeC = RRGB565(198, 202, 198);
    gfxWideLine(cx0 - hw0, y0, cx1 - hw1, y1, 2, edgeC);
    gfxWideLine(cx0 + hw0, y0, cx1 + hw1, y1, 2, edgeC);
  }

  float dashPhase = fmodf(rSroll * 0.045f, 0.18f);
  for (float t0 = 0.95f - dashPhase; t0 > 0.02f; t0 -= 0.18f) {
    float t1 = min(0.96f, t0 + 0.075f);
    int y0, hw0, y1, hw1;
    roadGeom(t0, y0, hw0);
    roadGeom(t1, y1, hw1);
    int cx0 = rSW / 2 + (int)(rCurve * t0 * t0 * CURVE_STRENGTH * (rSW * 0.5f));
    int cx1 = rSW / 2 + (int)(rCurve * t1 * t1 * CURVE_STRENGTH * (rSW * 0.5f));
    int lw = max(2, (int)(hw0 * 0.015f));
    gfxWideLine(cx0, y0, cx1, y1, lw, RRGB565(230, 230, 220));
  }

  // 7. 路边 3D 楼房：左右两侧各一排，高矮错落，随道路透视滚动。
  // scale 加大，让高楼与赛道比例协调(近处大楼高耸)
  for (int i = 0; i < 12; ++i) {
    float t = fmodf(i * 0.086f + rSroll * 0.014f, 0.92f);
    t = 0.92f - t; if (t >= 0.92f) t = 0.0f;   // 反向滚动：从远往近跑
    if (t < 0.08f) continue;
    int y, halfW;
    roadGeom(t, y, halfW);
    int cx = rSW / 2 + (int)(rCurve * t * t * CURVE_STRENGTH * (rSW * 0.5f));
    int scale = max(6, (int)((1.0f - t) * 60.0f));   // 加大基础比例(34→60)
    int roadLeft = cx - halfW;
    int roadRight = cx + halfW;
    int w = max(10, scale + (i % 3) * max(3, scale / 2));              // 楼宽加大
    int h = max(24, scale * (3 + (i * 5) % 5));                        // 楼高加大(2→3基数，更高)
    int baseY = y + max(4, scale / 3);
    int gap = max(3, scale / 3);
    int leftX = roadLeft - gap;
    int rightX = roadRight + gap;
    drawBuilding(leftX, baseY, w, h, -1, i + 2);
    drawBuilding(rightX, baseY, w + ((i % 2) ? scale / 2 : 0), h + ((i % 4) * scale / 2), 1, i + 9);
  }
}

// 画敌车图片精灵：9 种车型 x 3 个后视角，直接来自参考图切片。
static const uint8_t ENEMY_CAR_BBOX[ENEMY_CAR_TYPES * ENEMY_CAR_VIEWS][4] = {
  { 2, 15, 69, 70 }, { 4, 3, 66, 70 }, { 2, 15, 69, 70 },
  { 5, 20, 69, 70 }, { 3, 3, 67, 70 }, { 2, 21, 69, 70 },
  { 2, 13, 69, 70 }, { 4, 3, 66, 70 }, { 2, 14, 69, 70 },
  { 2, 6, 69, 70 }, { 7, 3, 63, 70 }, { 2, 10, 69, 70 },
  { 2, 14, 69, 70 }, { 10, 3, 60, 70 }, { 2, 14, 69, 70 },
  { 2, 8, 69, 70 }, { 11, 3, 59, 70 }, { 2, 7, 69, 70 },
  { 2, 21, 69, 70 }, { 2, 9, 69, 70 }, { 2, 21, 69, 70 },
  { 2, 15, 69, 70 }, { 2, 3, 68, 70 }, { 2, 17, 69, 70 },
  { 2, 14, 69, 70 }, { 7, 11, 63, 70 }, { 2, 13, 69, 70 },
};

static void drawEnemyCar(int sx, int baseY, float depth, int carType) {
  int view = 1;
  float turnView = (fabsf(rSteerVis) > 0.12f) ? rSteerVis : rCurve;
  if (turnView < -0.15f) view = 0;
  else if (turnView > 0.15f) view = 2;

  int frame = (carType % ENEMY_CAR_TYPES) * ENEMY_CAR_VIEWS + view;
  const float playerCarScale = 4.0f / 3.0f * 0.8f;
  const int playerNearW = (int)(CAR_SPRITE_W * playerCarScale / 2.0f);
  const int playerNearH = (int)(CAR_SPRITE_H * playerCarScale / 2.0f);
  const int playerBottom = rSH - 16 - 40 - 20;
  const float playerDepth = constrain((float)(rSH - playerBottom) / (float)(rSH - gHorizonY()), 0.05f, 0.95f);
  float depthRatio = constrain((1.0f - depth) / (1.0f - playerDepth), 0.0f, 1.20f);
  float perspective = constrain(0.18f + 0.82f * powf(depthRatio, 1.15f), 0.18f, 1.18f);
  int dstW = max(10, (int)(playerNearW * perspective));
  int dstH = max(10, (int)(playerNearH * perspective));
  int x0 = sx - dstW / 2;
  int y0 = baseY - dstH;
  int srcX0 = ENEMY_CAR_BBOX[frame][0];
  int srcY0 = ENEMY_CAR_BBOX[frame][1];
  int srcX1 = ENEMY_CAR_BBOX[frame][2];
  int srcY1 = ENEMY_CAR_BBOX[frame][3];
  int srcW = srcX1 - srcX0 + 1;
  int srcH = srcY1 - srcY0 + 1;

  gfxEllipse(sx, baseY - max(1, dstH / 18), max(4, dstW / 3), max(1, dstH / 14), RRGB565(16, 18, 20));
  for (int dy = 0; dy < dstH; ++dy) {
    int sy = srcY0 + (dy * srcH) / dstH;
    int py = y0 + dy;
    if (py < 0 || py >= rSH) continue;
    for (int dx = 0; dx < dstW; ++dx) {
      int sxImg = srcX0 + (dx * srcW) / dstW;
      int px = x0 + dx;
      if (px < 0 || px >= rSW) continue;
      int srcIdx = sy * ENEMY_CAR_W + sxImg;
      if (!pgm_read_byte(&ENEMY_CAR_MASK[frame][srcIdx])) continue;
      uint16_t c = pgm_read_word(&ENEMY_CAR_PIXELS[frame][srcIdx]);
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

static void fillQuad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c) {
  gfxFillTriangle(x0, y0, x1, y1, x2, y2, c);
  gfxFillTriangle(x0, y0, x2, y2, x3, y3, c);
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
    int cx = rSW / 2 + (int)(rCurve * t * t * CURVE_STRENGTH * (rSW * 0.5f));
    int sx = cx + (int)(o.lane * halfW);
    int scale = max(3, (int)((1.0f - t) * 26.0f));
    if (o.type == 0) drawEnemyCar(sx, y, t, o.colorIdx);
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
static void drawPlayerCar() {
  int y, halfW;
  roadGeom(0.0f, y, halfW);
  int cx = rSW / 2 + (int)(rCarX * halfW * 0.82f);

  // 即时切换帧(无滞后)，反应灵敏；配合平滑滤波+横移避免突兀
  rCarFrame = 0;
  if (rSteerVis < -0.15f) rCarFrame = -1;
  else if (rSteerVis > 0.15f) rCarFrame = 1;

  const uint16_t* pixels = CAR_SPRITE_CENTER;
  const uint8_t*  mask   = CAR_MASK_CENTER;
  if (rCarFrame == -1) { pixels = CAR_SPRITE_LEFT;  mask = CAR_MASK_LEFT; }
  else if (rCarFrame == 1) { pixels = CAR_SPRITE_RIGHT; mask = CAR_MASK_RIGHT; }

  // 车身随转向轻微横移(丝滑过渡感)
  cx += (int)(rSteerVis * 5.0f);

  // 居中绘制，底边对齐车底位置(rSH - 56)
  // 半尺寸再放大1/3 = 2/3原尺寸(100x77)，水平居中，底边对齐车底
  float carScale = 4.0f / 3.0f * 0.8f;   // 缩小五分之一(原 1.333 → 1.067)
  int dstW = (int)(CAR_SPRITE_W * carScale / 2.0f);
  int dstH = (int)(CAR_SPRITE_H * carScale / 2.0f);
  int spriteX = cx - dstW / 2;
  int carBottom = rSH - 16 - 40 - 20;   // 往上移动20px
  int spriteY = carBottom - dstH + 4;

  // 整车投影：始终跟随车身(含转向横移)，宽度覆盖整个车身
  int shadowY = carBottom + 2;
  int shadowCx = cx;   // 跟随车身中心(已含 rSteerVis 横移)
  // 投影宽度：转向时车体倾斜，投影需要更宽(覆盖整辆车的接地范围)
  float turnAmt = min(1.0f, fabsf(rSteerVis));
  int shadowW = dstW / 2 + 6 + (int)(turnAmt * 10.0f);  // 转向时加宽
  // 主投影(大椭圆，宽=车宽+边距，贴地)
  gfxEllipse(shadowCx, shadowY + 3, shadowW, 8, RRGB565(25, 27, 26));
  gfxEllipse(shadowCx, shadowY, shadowW - 8, 5, RRGB565(12, 14, 13));
  // 转向时投影往车身倾斜方向偏移(重量转移)
  if (turnAmt > 0.1f) {
    int leanSide = (rSteerVis > 0) ? 1 : -1;   // 投影跟随车身倾斜方向
    int leanX = shadowCx + leanSide * (int)(dstW * 0.15f * turnAmt);
    gfxEllipse(leanX, shadowY + 2, shadowW - 4, 6, RRGB565(20, 22, 21));

    // 前轮投影：转向时侧面露出前轮，在前轮下方补椭圆投影
    // 前轮横向位置：朝转向方向偏(精灵图中前轮在车身前侧)
    int frontWheelX = shadowCx + leanSide * (int)(dstW * 0.30f * turnAmt);
    int frontWheelY = carBottom - dstH * 30 / 100 + 2;   // 前轮高度(车身下部1/3处)
    int fwr = max(8, (int)(dstW * 0.16f * turnAmt) + 6);  // 前轮投影宽，随转向增大
    int fhr = max(3, dstH / 22);
    gfxEllipse(frontWheelX, frontWheelY + 3, fwr, fhr, RRGB565(24, 26, 25));
    gfxEllipse(frontWheelX, frontWheelY + 1, fwr - 3, max(2, fhr - 1), RRGB565(10, 12, 11));
  }

  drawCarSprite(pixels, mask, spriteX, spriteY, carScale);
}


// ---- 绘制：HUD（顶部分数条 + 底部速度仪表）----
static void drawHUD() {
  // 顶部信息条(半透明感：深色窄条)
  gfxBox(0, 0, rSW, 30, R_PANEL);
  gfxBox(0, 30, rSW, 2, R_YELLOW);
  // 分数(左)
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", (int)rDist);
  gfxTextLeft("SCORE", 12, 4, 0, R_DIM, R_PANEL);
  gfxTextLeft(buf, 12, 14, 4, R_WHITE, R_PANEL);
  // 最高分(右)
  snprintf(buf, sizeof(buf), "%d", (int)rHighScore);
  gfxTextLeft("BEST", rSW - 90, 4, 0, R_DIM, R_PANEL);
  gfxTextLeft(buf, rSW - 90, 14, 2, R_YELLOW, R_PANEL);

  // 底部速度仪表(移到玩家车下方，简化：只保留半圆+数字+km/h)
  int carBottom = rSH - 16 - 40 - 20;
  int gx = rSW / 2;                    // 居中
  int gy = carBottom + 55;             // 车底下方
  int gr = 32;
  // 半圆描边弧
  for (int a = 180; a >= 0; a -= 6) {
    float rad = a * (3.14159265f / 180.0f);
    int px = gx + (int)(cosf(rad) * gr);
    int py = gy - (int)(sinf(rad) * gr);
    gfxBox(px - 1, py - 1, 2, 2, R_WHITE);
  }
  // 指针(红色)
  float realRatio = constrain(rSpeed / 3.0f, 0.0f, 1.0f);
  float needleAngle = (180.0f - realRatio * 180.0f) * (3.14159265f / 180.0f);
  int nx = gx + (int)(cosf(needleAngle) * (gr - 6));
  int ny = gy - (int)(sinf(needleAngle) * (gr - 6));
  gfxWideLine(gx, gy, nx, ny, 2, R_RED);
  gfxCircle(gx, gy, 3, R_RED);
  // 速度数字
  int kmh = (int)(rSpeed * 100.0f);
  snprintf(buf, sizeof(buf), "%d", kmh);
  // 数字放在半圆内，Font4(大号加粗)，透明背景
  gfxTextCenter(buf, gx, gy - 2, 4, R_WHITE, 0xFFFF);
  gfxTextCenter("km/h", gx, gy + 24, 0, R_DIM, 0xFFFF);

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
  snprintf(buf, sizeof(buf), "SCORE  %d", (int)rDist);
  gfxTextCenter(buf, rSW / 2, by + 78, 4, R_WHITE, R_PANEL);
  snprintf(buf, sizeof(buf), "BEST   %d", (int)rHighScore);
  gfxTextCenter(buf, rSW / 2, by + 104, 2, R_YELLOW, R_PANEL);
  gfxTextCenter("Press A to restart", rSW / 2, by + 138, 0, R_WHITE, R_PANEL);
}

// ---- 主绘制 ----
void racingDraw() {
  drawSkyAndRoad();
  drawObstaclesSorted();
  drawPlayerCar();
  drawHUD();
  if (rGameOver) drawGameOver();
  gfxPushFrame();
}
