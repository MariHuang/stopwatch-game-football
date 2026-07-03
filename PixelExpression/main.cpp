#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>
#include "racing.h"
#include "basket_hoop_sprite.h"

static constexpr uint8_t KEY_A_PIN = 2;  // StopWatch KEYA (yellow)
static constexpr uint8_t KEY_B_PIN = 1;  // StopWatch KEYB (blue)

static constexpr uint16_t C_WHITE = 0xFFFF;
static constexpr uint16_t C_BLACK = 0x0000;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static constexpr uint16_t C_PITCH_1 = rgb565(86, 156, 95);
static constexpr uint16_t C_PITCH_2 = rgb565(100, 170, 108);
static constexpr uint16_t C_LINE = rgb565(235, 248, 232);
static constexpr uint16_t C_SHADOW = rgb565(38, 76, 45);
static constexpr uint16_t C_BLUE = rgb565(30, 87, 188);
static constexpr uint16_t C_BLUE_DARK = rgb565(9, 40, 118);
static constexpr uint16_t C_YELLOW = rgb565(255, 214, 44);
static constexpr uint16_t C_RED = rgb565(204, 50, 50);
static constexpr uint16_t C_RED_DARK = rgb565(108, 24, 30);
static constexpr uint16_t C_SKIN = rgb565(183, 116, 86);
static constexpr uint16_t C_SKIN_DARK = rgb565(94, 52, 43);
static constexpr uint16_t C_HAIR_BLACK = rgb565(38, 30, 28);
static constexpr uint16_t C_HAIR_BROWN = rgb565(82, 52, 30);
static constexpr uint16_t C_HAIR_BLOND = rgb565(220, 188, 96);
static constexpr uint16_t C_HAIR_DARKBROWN = rgb565(60, 40, 28);
static constexpr uint16_t C_PANEL = rgb565(18, 24, 22);
static constexpr uint16_t C_PANEL_EDGE = rgb565(202, 218, 210);
static constexpr uint16_t C_DIM = rgb565(140, 160, 148);
static constexpr uint16_t C_ACCENT = rgb565(125, 220, 255);
static constexpr uint16_t C_ORANGE = rgb565(255, 170, 44);
static constexpr uint16_t C_KEEPER_HOME = rgb565(34, 205, 196);
static constexpr uint16_t C_KEEPER_AWAY = rgb565(138, 88, 224);
static constexpr uint16_t C_BALL_GRAY = rgb565(86, 92, 92);
static constexpr uint16_t C_BALL_LIGHT = rgb565(214, 226, 222);

struct Vec2 {
  float x;
  float y;
};

struct Player {
  Vec2 pos;
  Vec2 vel;
  uint8_t number;
  bool home;
  uint8_t hairStyle;   // 0=短发 1=寸头 2=中分 3=蓬松 4=马尾 5=光头(少数)
  uint16_t hairColor;
};

struct RawButton {
  uint8_t pin;
  bool idleLevel;
  bool down;
  bool pressed;
  uint32_t changedAt;
};

static M5Canvas canvas(&M5.Display);
static bool useCanvas = false;
static constexpr int RENDER_W = 466;
static constexpr int RENDER_H = 466;
static int screenW = RENDER_W;
static int screenH = RENDER_H;
static int screenOffsetX = 0;
static int screenOffsetY = 0;

// ---- 游戏模式 ----
enum GameMode { MODE_MENU, MODE_FOOTBALL, MODE_RACING, MODE_BASKETBALL };
static GameMode gameMode = MODE_MENU;
static constexpr int GAME_COUNT = 3;
static int menuSelected = 0;        // 菜单当前选中项 0=足球 1=赛车 2=空气投篮

static int fieldLeft = 20;
static int fieldRight = 448;
static int fieldTop = 48;
static int fieldBottom = 458;
static int goalLeft = 176;
static int goalRight = 292;
static float cameraX = 0.0f;
static float cameraY = 0.0f;
static constexpr int GOAL_VIEW_PAD = 82;
static constexpr int SIDE_VIEW_PAD = 76;
static constexpr int GOAL_DEPTH = 40;
static constexpr int PITCH_LINE_W = 3;
static constexpr int FIELD_SCALE_NUM = 178;
static constexpr int FIELD_SCALE_DEN = 100;
static constexpr int HOME_COUNT = 4;
static constexpr int AWAY_COUNT = 4;
static constexpr int HOME_KEEPER_CONTROL = HOME_COUNT;

static Player home[HOME_COUNT];
static Player away[AWAY_COUNT];
static Player keeper;
static Player homeKeeper;
static Vec2 ballPos;
static Vec2 ballVel;
static Vec2 moveInput = {0.0f, 0.0f};
static Vec2 lastAim = {0.0f, -1.0f};

static int ballOwner = 0;     // 0..3 home, 10..13 away, 20 away keeper, 21 home keeper, -1 loose
static int controlled = 0;
static int passTarget = -1;
static bool lastTouchHome = true;
static int playerScore = 0;
static int cpuScore = 0;

// ---- 比赛时间系统 ----
// 真实时间：上半场 180s + 补时 30~50s + 下半场 180s + 补时 30~50s
// 显示时间 = 真实时间(每秒 +1)：上半场 0:00~3:00，下半场 3:00~6:00，补时按真实秒

// 中圈开球状态：开球时需两名同队球员在中圈，一人传球给另一人才算开球
static bool kickoffPending = false;   // 是否处于"等待开球传球"状态
static int kickoffPasser = -1;        // 开球持球者索引(0..3 或 10..13)
static int kickoffReceiver = -1;      // 开球接球者索引

// 定位球准备状态(统一处理界外球/角球/球门球)：球出界后定格2s，球员再跑去发球点
// setPieceState: 0=无, 1=定格等待中, 2=球员已就位发球
// setPieceType:  0=界外球, 1=角球/球门球
static int setPieceState = 0;
static uint32_t setPieceUntil = 0;
static int setPieceType = 0;
static Vec2 setPieceSpot = {0.0f, 0.0f};
static Vec2 setPieceAim = {0.0f, 0.0f};
static bool setPieceHomeThrow = false;  // true=主队发球, false=客队发球
enum MatchPhase {
  PH_FIRST_HALF = 0,   // 上半场(常规)
  PH_FIRST_STOPPO,     // 上半场补时
  PH_HALFTIME,         // 中场休息(等待按键)
  PH_SECOND_HALF,      // 下半场(常规)
  PH_SECOND_STOPPO,    // 下半场补时
  PH_FULLTIME          // 终场(等待按键重开)
};
static MatchPhase matchPhase = PH_FIRST_HALF;
static int realSecond = 0;          // 当前阶段已过的真实秒数
static int displaySecond = 0;       // 显示用总秒数(0~90min)
static int stoppageLen = 0;         // 本阶段补时长度(秒)，补时开始时随机生成

static const int HALF_REAL_SEC = 180;              // 单半场真实 3 分钟(显示一致)
static const int STOPPAGE_MIN = 30;                // 补时下限
static const int STOPPAGE_MAX = 50;                // 补时上限

static uint32_t lastTickMs = 0;
static uint32_t lastClockMs = 0;
static uint32_t messageUntil = 0;
static uint32_t kickLockUntil = 0;

// 进球庆祝状态：进球后让球飞入网内并停留一段时间再开球
static bool celebrateActive = false;       // 是否处于进球庆祝中
static uint32_t celebrateUntil = 0;        // 庆祝结束时间
static bool celebratePlayerScored = false; // 本次进球是否是玩家进的上球门

// 足球旋转：球在移动(带球/传球/射门)时累积旋转角度
static float ballSpin = 0.0f;              // 当前旋转角度(弧度)
// 最近一次踢球类型：0=带球/无, 1=传球, 2=射门。决定松球时的旋转速度
static uint8_t lastKickType = 0;

// 球员跑动步频相位：随时间累积，速度越快累积越快，用于双脚摆动动画
static float walkPhase = 0.0f;

// 进球庆祝彩纸屑(confetti)
struct Confetti {
  float x;
  float y;
  float vx;
  float vy;
  uint16_t color;
  float rot;
  float vrot;
  int size;
  bool active;
};
static const int CONFETTI_MAX = 70;
static Confetti confetti[CONFETTI_MAX];
// confetti 函数前向声明(定义在 drawBall 之后)
void spawnConfetti();
void updateConfetti(float dt);
void drawConfetti();
void drawGoalBanner();
void drawMatchOverlay();
static char messageText[28] = "TILT TO PLAY";

static RawButton rawA = {KEY_A_PIN, HIGH, false, false, 0};
static RawButton rawB = {KEY_B_PIN, HIGH, false, false, 0};
static Vec2 touchPoint = {0.0f, 0.0f};
static bool touchActive = false;

// ---- 空气投篮游戏状态 ----
static int basketScore = 0;
static int basketBest = 0;
static int basketAttempts = 0;
static int basketMade = 0;
static bool basketShotActive = false;
static bool basketShotMade = false;
static bool basketResultReady = false;
static uint32_t basketResultUntil = 0;
static uint32_t basketLastShotMs = 0;
static float basketBallT = 0.0f;
static float basketBallX = 0.0f;
static float basketBallY = 0.0f;
static float basketBallR = 14.0f;
static float basketStartX = 0.0f;
static float basketStartY = 0.0f;
static float basketCtrlX = 0.0f;
static float basketCtrlY = 0.0f;
static float basketEndX = 0.0f;
static float basketEndY = 0.0f;
static float basketSpin = 0.0f;
static float basketPower = 0.0f;
static float basketLastAccelMag = 1.0f;
static float basketSwingMeter = 0.0f;
static bool basketImuReady = false;

Player& controlledPlayer() {
  if (controlled == HOME_KEEPER_CONTROL) return homeKeeper;
  int idx = controlled;
  if (idx < 0) idx = 0;
  if (idx >= HOME_COUNT) idx = HOME_COUNT - 1;
  return home[idx];
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

float lengthOf(Vec2 v) {
  return sqrtf(v.x * v.x + v.y * v.y);
}

Vec2 add(Vec2 a, Vec2 b) {
  return {a.x + b.x, a.y + b.y};
}

Vec2 sub(Vec2 a, Vec2 b) {
  return {a.x - b.x, a.y - b.y};
}

Vec2 mul(Vec2 v, float s) {
  return {v.x * s, v.y * s};
}

Vec2 normalized(Vec2 v) {
  float len = lengthOf(v);
  if (len < 0.001f) return {0.0f, -1.0f};
  return {v.x / len, v.y / len};
}

void setMessage(const char* text, uint16_t ms = 1200) {
  strncpy(messageText, text, sizeof(messageText) - 1);
  messageText[sizeof(messageText) - 1] = 0;
  messageUntil = millis() + ms;
}

void fillScreen(uint16_t color) {
  if (useCanvas) canvas.fillScreen(color);
  else M5.Display.fillScreen(color);
}

void box(int x, int y, int w, int h, uint16_t color) {
  if (useCanvas) canvas.fillRect(x, y, w, h, color);
  else M5.Display.fillRect(x, y, w, h, color);
}

void roundBox(int x, int y, int w, int h, int r, uint16_t color) {
  if (useCanvas) canvas.fillSmoothRoundRect(x, y, w, h, r, color);
  else M5.Display.fillSmoothRoundRect(x, y, w, h, r, color);
}

void circle(int x, int y, int r, uint16_t color) {
  if (r <= 0) return;
  if (useCanvas) canvas.fillSmoothCircle(x, y, r, color);
  else M5.Display.fillSmoothCircle(x, y, r, color);
}

void line(int x0, int y0, int x1, int y1, uint16_t color) {
  if (useCanvas) canvas.drawLine(x0, y0, x1, y1, color);
  else M5.Display.drawLine(x0, y0, x1, y1, color);
}

void wideLine(int x0, int y0, int x1, int y1, int w, uint16_t color) {
  if (useCanvas) canvas.drawWideLine(x0, y0, x1, y1, w, color);
  else M5.Display.drawWideLine(x0, y0, x1, y1, w, color);
}

void rectLine(int x, int y, int w, int h, uint16_t color) {
  if (useCanvas) canvas.drawRect(x, y, w, h, color);
  else M5.Display.drawRect(x, y, w, h, color);
}

int worldX(float x) {
  return (int)roundf(x - cameraX);
}

int worldY(float y) {
  return (int)roundf(y - cameraY);
}

Vec2 screenToWorld(Vec2 p) {
  return {p.x + cameraX, p.y + cameraY};
}

Vec2 worldToScreen(Vec2 p) {
  return {p.x - cameraX, p.y - cameraY};
}

Vec2 touchToScreen(int rawX, int rawY) {
  return {
    clampf((float)(rawX - screenOffsetX), 0.0f, (float)(screenW - 1)),
    clampf((float)(rawY - screenOffsetY), 0.0f, (float)(screenH - 1))
  };
}

void worldBox(float x, float y, float w, float h, uint16_t color) {
  int sx = worldX(x);
  int sy = worldY(y);
  int sw = (int)ceilf(w);
  int sh = (int)ceilf(h);
  if (sx >= screenW || sy >= screenH || sx + sw <= 0 || sy + sh <= 0) return;
  if (sx < 0) { sw += sx; sx = 0; }
  if (sy < 0) { sh += sy; sy = 0; }
  if (sx + sw > screenW) sw = screenW - sx;
  if (sy + sh > screenH) sh = screenH - sy;
  if (sw > 0 && sh > 0) box(sx, sy, sw, sh, color);
}

void worldCircle(Vec2 p, int r, uint16_t color) {
  int x = worldX(p.x);
  int y = worldY(p.y);
  if (x < -r || y < -r || x > screenW + r || y > screenH + r) return;
  circle(x, y, r, color);
}

void worldLine(Vec2 a, Vec2 b, uint16_t color) {
  line(worldX(a.x), worldY(a.y), worldX(b.x), worldY(b.y), color);
}

void worldWideLine(Vec2 a, Vec2 b, int w, uint16_t color) {
  wideLine(worldX(a.x), worldY(a.y), worldX(b.x), worldY(b.y), w, color);
}

void worldRectLine(float x, float y, float w, float h, uint16_t color) {
  rectLine(worldX(x), worldY(y), (int)w, (int)h, color);
}

void pitchLine(Vec2 a, Vec2 b) {
  worldWideLine(a, b, PITCH_LINE_W, C_LINE);
}

void pitchRect(float x, float y, float w, float h) {
  pitchLine({x, y}, {x + w, y});
  pitchLine({x + w, y}, {x + w, y + h});
  pitchLine({x + w, y + h}, {x, y + h});
  pitchLine({x, y + h}, {x, y});
}

void pitchCircle(float x, float y, int r) {
  int sx = worldX(x);
  int sy = worldY(y);
  int half = PITCH_LINE_W / 2;
  for (int dy = -half; dy <= half; ++dy) {
    for (int dx = -half; dx <= half; ++dx) {
      if (dx * dx + dy * dy > half * half + 1) continue;
      if (useCanvas) canvas.drawCircle(sx + dx, sy + dy, r, C_LINE);
      else M5.Display.drawCircle(sx + dx, sy + dy, r, C_LINE);
    }
  }
}

void updateCamera() {
  Vec2 focus = ballPos;
  if (ballOwner == -1) {
    focus = add(ballPos, mul(ballVel, 22.0f));
  } else if (ballOwner >= 0 && ballOwner < HOME_COUNT) {
    focus.x = ballPos.x * 0.78f + home[ballOwner].pos.x * 0.22f;
    focus.y = ballPos.y * 0.78f + home[ballOwner].pos.y * 0.22f;
  }
  float targetX = clampf(focus.x - screenW * 0.5f,
                         (float)fieldLeft - SIDE_VIEW_PAD,
                         (float)(fieldRight - screenW + SIDE_VIEW_PAD));
  float targetY = clampf(focus.y - screenH * 0.5f,
                         (float)fieldTop - GOAL_VIEW_PAD,
                         (float)(fieldBottom - screenH + GOAL_VIEW_PAD));
  float follow = (ballOwner == -1) ? 0.30f : 0.20f;
  cameraX += (targetX - cameraX) * follow;
  cameraY += (targetY - cameraY) * follow;
}

void textCenter(const char* text, int x, int y, const lgfx::IFont* font, uint16_t color, uint16_t bg = C_BLACK) {
  if (useCanvas) {
    canvas.setFont(font);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(color, bg);
    canvas.drawString(text, x, y);
  } else {
    M5.Display.setFont(font);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(color, bg);
    M5.Display.drawString(text, x, y);
  }
}

void textLeft(const char* text, int x, int y, const lgfx::IFont* font, uint16_t color, uint16_t bg = C_BLACK) {
  if (useCanvas) {
    canvas.setFont(font);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(color, bg);
    canvas.drawString(text, x, y);
  } else {
    M5.Display.setFont(font);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(color, bg);
    M5.Display.drawString(text, x, y);
  }
}

void pushFrame() {
  if (useCanvas) canvas.pushSprite(screenOffsetX, screenOffsetY);
}

// ---- gfx 桥接 API（非 static，供 racing.cpp 复用绘制原语）----
// font 索引: 0=Font0 2=Font2 4=Font4
static const lgfx::IFont* gfxFontById(int id) {
  if (id == 4) return &fonts::Font4;
  if (id == 2) return &fonts::Font2;
  return &fonts::Font0;
}
extern "C" {
  bool gfxUseCanvas() { return useCanvas; }
  void gfxFillScreen(uint16_t c) { fillScreen(c); }
  void gfxBox(int x, int y, int w, int h, uint16_t c) { box(x, y, w, h, c); }
  void gfxPixel(int x, int y, uint16_t c) {
    if (useCanvas) canvas.drawPixel(x, y, c);
    else M5.Display.drawPixel(x, y, c);
  }
  void gfxRoundBox(int x, int y, int w, int h, int r, uint16_t c) { roundBox(x, y, w, h, r, c); }
  void gfxRectLine(int x, int y, int w, int h, uint16_t c) { rectLine(x, y, w, h, c); }
  void gfxLine(int x0, int y0, int x1, int y1, uint16_t c) { line(x0, y0, x1, y1, c); }
  void gfxWideLine(int x0, int y0, int x1, int y1, int w, uint16_t c) { wideLine(x0, y0, x1, y1, w, c); }
  void gfxFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    if (useCanvas) canvas.fillTriangle(x0, y0, x1, y1, x2, y2, c);
    else M5.Display.fillTriangle(x0, y0, x1, y1, x2, y2, c);
  }
  void gfxCircle(int x, int y, int r, uint16_t c) { circle(x, y, r, c); }
  void gfxEllipse(int x, int y, int rx, int ry, uint16_t c) {
    if (useCanvas) canvas.fillEllipse(x, y, rx, ry, c);
    else M5.Display.fillEllipse(x, y, rx, ry, c);
  }
  void gfxDrawCircle(int x, int y, int r, uint16_t c) {
    if (useCanvas) canvas.drawCircle(x, y, r, c);
    else M5.Display.drawCircle(x, y, r, c);
  }
  void gfxDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (useCanvas) canvas.drawRoundRect(x, y, w, h, r, c);
    else M5.Display.drawRoundRect(x, y, w, h, r, c);
  }
  void gfxTextCenter(const char* s, int x, int y, int font, uint16_t c, uint16_t bg) {
    if (useCanvas) {
      canvas.setFont(gfxFontById(font));
      canvas.setTextSize(1);
      canvas.setTextDatum(textdatum_t::middle_center);
      // bg==0xFFFF 表示透明(不画背景)
      canvas.setTextColor(c, (bg == 0xFFFF) ? c : bg);
      if (bg == 0xFFFF) canvas.drawString(s, x, y);   // 透明仍会有底色抗锯齿,但无背景块
      else { // 先填背景块再画(保留原行为)
        canvas.drawString(s, x, y);
      }
    } else {
      textCenter(s, x, y, gfxFontById(font), c, bg);
    }
  }
  void gfxTextLeft(const char* s, int x, int y, int font, uint16_t c, uint16_t bg) {
    textLeft(s, x, y, gfxFontById(font), c, bg);
  }
  void gfxPushFrame() { pushFrame(); }
}

void initRawButtons() {
  pinMode(KEY_A_PIN, INPUT);
  pinMode(KEY_B_PIN, INPUT);
  delay(8);
  rawA.idleLevel = digitalRead(KEY_A_PIN);
  rawB.idleLevel = digitalRead(KEY_B_PIN);
  rawA.changedAt = rawB.changedAt = millis();
}

void updateRawButton(RawButton& b) {
  b.pressed = false;
  bool nowDown = digitalRead(b.pin) != b.idleLevel;
  uint32_t now = millis();
  if (nowDown != b.down && now - b.changedAt > 24) {
    b.down = nowDown;
    b.changedAt = now;
    if (b.down) b.pressed = true;
  }
}

int nearestHomeToBall() {
  int nearest = 0;
  float best = 100000.0f;
  for (int i = 0; i < HOME_COUNT; ++i) {
    float dist = lengthOf(sub(home[i].pos, ballPos));
    if (dist < best) {
      best = dist;
      nearest = i;
    }
  }
  return nearest;
}

void autoSelectClosestToBall() {
  // 开球期间固定控制开球者，不自动切换
  if (kickoffPending && kickoffPasser >= 0 && kickoffPasser < HOME_COUNT) {
    controlled = kickoffPasser;
    return;
  }
  if (ballOwner == 21) {
    controlled = HOME_KEEPER_CONTROL;
    return;
  }

  int nearest = nearestHomeToBall();
  if (nearest != controlled) {
    controlled = nearest;
    passTarget = -1;
  }
  if (ballOwner >= 0 && ballOwner < HOME_COUNT && ballOwner != controlled &&
      lengthOf(sub(home[controlled].pos, ballPos)) < 18.0f) {
    ballOwner = controlled;
  }
}

void readTouchControl() {
  touchActive = false;
  Vec2 targetInput = {0.0f, 0.0f};

  if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
    auto& pt = M5.Touch.getTouchPointRaw(0);
    touchPoint = touchToScreen(pt.x, pt.y);
    touchActive = true;

    Vec2 touchWorld = screenToWorld(touchPoint);
    Vec2 toTouch = sub(touchWorld, controlledPlayer().pos);
    float dist = lengthOf(toTouch);
    if (dist > 8.0f) {
      float strength = clampf((dist - 8.0f) / 96.0f, 0.0f, 1.0f);
      targetInput = mul(normalized(toTouch), strength);
    }
  }

  moveInput.x = moveInput.x * 0.64f + targetInput.x * 0.36f;
  moveInput.y = moveInput.y * 0.64f + targetInput.y * 0.36f;

  if (lengthOf(moveInput) > 0.08f) {
    lastAim = normalized(moveInput);
  }
}

void clampToField(Vec2& p, float radius) {
  p.x = clampf(p.x, fieldLeft + radius, fieldRight - radius);
  p.y = clampf(p.y, fieldTop + radius, fieldBottom - radius);
}

// 随机分配发型(多发型避免全相同，光头概率低)
void randomizeHair(Player& p) {
  static const uint16_t colors[] = {C_HAIR_BLACK, C_HAIR_BROWN, C_HAIR_BLOND, C_HAIR_DARKBROWN};
  p.hairStyle = rand() % 6;   // 0..5，其中 5=光头(1/6 概率)
  p.hairColor = colors[rand() % 4];
}

void moveToward(Player& p, Vec2 target, float speed, float dt) {
  Vec2 to = sub(target, p.pos);
  float dist = lengthOf(to);
  if (dist < 1.0f) {
    p.vel = mul(p.vel, 0.82f);
    return;
  }
  Vec2 desired = mul(to, speed / dist);
  p.vel.x = p.vel.x * 0.80f + desired.x * 0.20f;
  p.vel.y = p.vel.y * 0.80f + desired.y * 0.20f;
  p.pos = add(p.pos, mul(p.vel, dt));
  clampToField(p.pos, 13.0f);
}

void resetKickoff(bool playerStarts) {
  float midX = (fieldLeft + fieldRight) * 0.5f;
  float midY = (fieldTop + fieldBottom) * 0.5f;
  float width = fieldRight - fieldLeft;
  home[0] = {{midX, midY + 96.0f}, {0, 0}, 10, true};
  home[1] = {{fieldLeft + width * 0.28f, midY + 150.0f}, {0, 0}, 7, true};
  home[2] = {{fieldLeft + width * 0.72f, midY + 178.0f}, {0, 0}, 9, true};
  home[3] = {{midX, midY + 210.0f}, {0, 0}, 8, true};

  away[0] = {{midX, midY - 118.0f}, {0, 0}, 5, false};
  away[1] = {{fieldLeft + width * 0.33f, midY - 170.0f}, {0, 0}, 8, false};
  away[2] = {{fieldLeft + width * 0.67f, midY - 188.0f}, {0, 0}, 6, false};
  away[3] = {{midX, midY - 96.0f}, {0, 0}, 11, false};
  keeper = {{midX, fieldTop + 28.0f}, {0, 0}, 1, false};
  homeKeeper = {{midX, fieldBottom - 28.0f}, {0, 0}, 1, true};

  controlled = 0;
  passTarget = -1;
  moveInput = {0.0f, 0.0f};
  // 中圈开球：两名同队球员站在中圈两侧，球归其中一人，需传给另一人才算开球
  kickoffPending = true;
  if (playerStarts) {
    // 主队开球：home[0] 持球(下侧)，home[3] 接球(上侧)，两人都在中圈内
    home[0].pos = {midX - 14.0f, midY + 8.0f};
    home[3].pos = {midX + 14.0f, midY - 8.0f};
    ballOwner = 0;
    controlled = 0;
    lastAim = {0.0f, -1.0f};
    kickoffPasser = 0;
    kickoffReceiver = 3;
  } else {
    // 客队开球：away[0] 持球(上侧)，away[3] 接球(下侧)
    away[0].pos = {midX - 14.0f, midY - 8.0f};
    away[3].pos = {midX + 14.0f, midY + 8.0f};
    ballOwner = 10;
    lastAim = {0.0f, 1.0f};
    kickoffPasser = 10;
    kickoffReceiver = 13;
  }
  ballPos = {midX, midY};
  ballVel = {0.0f, 0.0f};
  // 给所有球员随机分配发型
  for (int i = 0; i < HOME_COUNT; ++i) randomizeHair(home[i]);
  for (int i = 0; i < AWAY_COUNT; ++i) randomizeHair(away[i]);
  randomizeHair(keeper);
  randomizeHair(homeKeeper);
  cameraX = clampf(ballPos.x - screenW * 0.5f,
                   (float)fieldLeft - SIDE_VIEW_PAD,
                   (float)(fieldRight - screenW + SIDE_VIEW_PAD));
  cameraY = clampf(ballPos.y - screenH * 0.5f,
                   (float)fieldTop - GOAL_VIEW_PAD,
                   (float)(fieldBottom - screenH + GOAL_VIEW_PAD));
}

void kickBall(Vec2 from, Vec2 dir, float speed, uint8_t kickType = 1) {
  if ((ballOwner >= 0 && ballOwner < HOME_COUNT) || ballOwner == 21) lastTouchHome = true;
  if ((ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT) || ballOwner == 20) lastTouchHome = false;
  dir = normalized(dir);
  ballOwner = -1;
  ballPos = add(from, mul(dir, 18.0f));
  ballVel = mul(dir, speed);
  kickLockUntil = millis() + 180;
  lastKickType = kickType;   // 记录踢球类型，用于旋转速度
}

void makePass() {
  if (!((ballOwner >= 0 && ballOwner < HOME_COUNT) || ballOwner == 21)) return;
  Player& p = (ballOwner == 21) ? homeKeeper : home[ballOwner];

  int target = -1;
  // 中圈开球：传给距离最近的队友
  if (kickoffPending && ballOwner == kickoffPasser) {
    float best = 100000.0f;
    for (int i = 0; i < HOME_COUNT; ++i) {
      if (i == ballOwner) continue;
      float d = lengthOf(sub(home[i].pos, p.pos));
      if (d < best) {
        best = d;
        target = i;
      }
    }
  } else {
    float best = 100000.0f;
    for (int i = 0; i < HOME_COUNT; ++i) {
      if (ballOwner != 21 && i == ballOwner) continue;
      float ahead = home[i].pos.y - p.pos.y;
      float score = fabsf(home[i].pos.x - p.pos.x) + max(0.0f, ahead + 40.0f) * 1.5f;
      if (home[i].pos.y < p.pos.y + 28.0f) score -= 120.0f;
      if (score < best) {
        best = score;
        target = i;
      }
    }
  }
  if (target < 0) return;

  passTarget = target;
  Vec2 dir = sub(home[target].pos, p.pos);
  kickBall(p.pos, dir, 6.5f, 1);  // 传球
  setMessage("PASS", 650);
}

void shootBall() {
  if (!((ballOwner >= 0 && ballOwner < HOME_COUNT) || ballOwner == 21)) return;
  Player& p = (ballOwner == 21) ? homeKeeper : home[ballOwner];
  Vec2 target = {(fieldLeft + fieldRight) * 0.5f + lastAim.x * 110.0f, (float)fieldTop - 34.0f};
  Vec2 touchWorld = screenToWorld(touchPoint);
  if (touchActive && touchWorld.y < p.pos.y - 12.0f) target = touchWorld;
  kickBall(p.pos, sub(target, p.pos), 9.2f, 2);  // 射门
  passTarget = -1;
  setMessage("SHOOT", 650);
}

void cpuKickDown(Player& p) {
  Vec2 target = {(fieldLeft + fieldRight) * 0.5f, (float)fieldBottom + 26.0f};
  target.x += (p.pos.x < (fieldLeft + fieldRight) * 0.5f) ? 70.0f : -70.0f;
  kickBall(p.pos, sub(target, p.pos), 5.8f);
  setMessage("INTERCEPT", 800);
}

void keeperKickUp(Player& p) {
  Vec2 target = {(fieldLeft + fieldRight) * 0.5f, (float)fieldTop - 26.0f};
  target.x += (p.pos.x < (fieldLeft + fieldRight) * 0.5f) ? 70.0f : -70.0f;
  kickBall(p.pos, sub(target, p.pos), 5.8f);
  setMessage("SAVE", 800);
}

void attachBallToOwner() {
  if (ballOwner >= 0 && ballOwner < HOME_COUNT) {
    Vec2 dir = lastAim;
    if (lengthOf(moveInput) < 0.08f) dir = {0.0f, -1.0f};
    ballPos = add(home[ballOwner].pos, mul(dir, 17.0f));
    ballVel = {0.0f, 0.0f};
  } else if (ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT) {
    int i = ballOwner - 10;
    ballPos = add(away[i].pos, {0.0f, 17.0f});
    ballVel = {0.0f, 0.0f};
  } else if (ballOwner == 20) {
    ballPos = add(keeper.pos, {0.0f, 16.0f});
    ballVel = {0.0f, 0.0f};
  } else if (ballOwner == 21) {
    ballPos = add(homeKeeper.pos, {0.0f, -16.0f});
    ballVel = {0.0f, 0.0f};
  }
}

Vec2 opponentThreatPos() {
  if (ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT) return away[ballOwner - 10].pos;
  if (ballOwner == 20) return keeper.pos;
  return ballPos;
}

int nearestAwayTo(Vec2 p) {
  int nearest = 0;
  float best = 100000.0f;
  for (int i = 0; i < AWAY_COUNT; ++i) {
    float dist = lengthOf(sub(away[i].pos, p));
    if (dist < best) {
      best = dist;
      nearest = i;
    }
  }
  return nearest;
}

float homeLaneX(int i) {
  float width = fieldRight - fieldLeft;
  static const float lanes[HOME_COUNT] = {0.50f, 0.22f, 0.78f, 0.38f};
  return fieldLeft + width * lanes[i % HOME_COUNT];
}

float awayLaneX(int i) {
  float width = fieldRight - fieldLeft;
  static const float lanes[4] = {0.28f, 0.43f, 0.57f, 0.72f};
  return fieldLeft + width * lanes[i];
}

int homeLooseBallRunner() {
  if (passTarget >= 0 && passTarget < HOME_COUNT) return passTarget;
  return nearestHomeToBall();
}

int awayLooseBallRunner() {
  int runner = 0;
  float best = 100000.0f;
  for (int i = 0; i < AWAY_COUNT; ++i) {
    float dist = lengthOf(sub(away[i].pos, ballPos));
    if (dist < best) {
      best = dist;
      runner = i;
    }
  }
  return runner;
}

void updateHome(float dt) {
  Player& p = controlledPlayer();
  float mag = clampf(lengthOf(moveInput), 0.0f, 1.0f);
  Vec2 desired = mul(normalized(moveInput), 170.0f * mag);
  if (mag < 0.06f) desired = {0.0f, 0.0f};
  p.vel.x = p.vel.x * 0.78f + desired.x * 0.22f;
  p.vel.y = p.vel.y * 0.78f + desired.y * 0.22f;
  p.pos = add(p.pos, mul(p.vel, dt));
  clampToField(p.pos, 13.0f);

  float midX = (fieldLeft + fieldRight) * 0.5f;
  Vec2 threat = opponentThreatPos();
  int primaryPresser = -1;
  if (ballOwner >= 10 || ballOwner == 20) {
    float bestPress = 100000.0f;
    for (int i = 0; i < HOME_COUNT; ++i) {
      if (i == controlled) continue;
      float dist = lengthOf(sub(home[i].pos, threat));
      if (dist < bestPress) {
        bestPress = dist;
        primaryPresser = i;
      }
    }
  }
  int looseRunner = (ballOwner == -1) ? homeLooseBallRunner() : -1;

  for (int i = 0; i < HOME_COUNT; ++i) {
    if (i == controlled) continue;
    Vec2 target = home[i].pos;
    float speed = 96.0f;

    if (ballOwner == -1) {
      if (i == looseRunner) {
        target = add(ballPos, mul(ballVel, 18.0f));
        speed = (i == passTarget) ? 144.0f : 132.0f;
      } else {
        float laneX = homeLaneX(i);
        float supportY = ballPos.y + (i == 0 ? 118.0f : -72.0f);
        if (ballVel.y > 0.2f) supportY = ballPos.y + 92.0f;
        target = {laneX, clampf(supportY, fieldTop + 95.0f, fieldBottom - 85.0f)};
        if (fabsf(target.x - ballPos.x) < 78.0f) {
          target.x += (target.x < midX) ? -78.0f : 78.0f;
        }
        target.x = clampf(target.x, fieldLeft + 46.0f, fieldRight - 46.0f);
        speed = 92.0f;
      }
    } else if ((ballOwner >= 0 && ballOwner < HOME_COUNT) || ballOwner == 21) {
      Vec2 carrierPos = (ballOwner == 21) ? homeKeeper.pos : home[ballOwner].pos;
      float laneX = homeLaneX(i);
      float depth = (i == 0) ? 118.0f : 190.0f;
      target = {laneX, clampf(carrierPos.y - depth, fieldTop + 90.0f, fieldBottom - 90.0f)};
      if (i != ballOwner && lengthOf(sub(home[i].pos, ballPos)) < 160.0f) {
        float side = (laneX < midX) ? -1.0f : 1.0f;
        target.x = clampf(ballPos.x + side * 125.0f, fieldLeft + 45.0f, fieldRight - 45.0f);
        target.y = clampf(ballPos.y - 118.0f, fieldTop + 90.0f, fieldBottom - 90.0f);
      }
      speed = 112.0f;
    } else {
      if (i == primaryPresser) {
        Vec2 towardOwnGoal = normalized({midX - threat.x, (float)fieldBottom - threat.y});
        target = add(threat, mul(towardOwnGoal, 10.0f));   // 贴近球(10px)，确保进入抢断范围
        speed = 132.0f;
      } else {
        float laneX = homeLaneX(i);
        target = {laneX, clampf(threat.y + 116.0f + i * 20.0f, fieldTop + 120.0f, fieldBottom - 80.0f)};
        speed = 98.0f;
      }
    }
    clampToField(target, 18.0f);
    moveToward(home[i], target, speed, dt);
  }
}

void updateAway(float dt) {
  bool homeHasBall = (ballOwner >= 0 && ballOwner < HOME_COUNT) || ballOwner == 21;
  int awayPresser = -1;
  if (homeHasBall) {
    float bestPress = 100000.0f;
    for (int i = 0; i < AWAY_COUNT; ++i) {
      float dist = lengthOf(sub(away[i].pos, ballPos));
      if (dist < bestPress) {
        bestPress = dist;
        awayPresser = i;
      }
    }
  }
  int looseRunner = (ballOwner == -1) ? awayLooseBallRunner() : -1;

  for (int i = 0; i < AWAY_COUNT; ++i) {
    Vec2 target = away[i].pos;
    if (homeHasBall) {
      if (i == awayPresser) {
        Vec2 towardOwnGoal = normalized({away[i].pos.x - (fieldLeft + fieldRight) * 0.5f, away[i].pos.y - (float)fieldTop});
        target = add(ballPos, mul(towardOwnGoal, 10.0f));   // 贴近球(10px)，确保进入抢断范围(<18px)
      } else {
        float stagger = (i % 2 == 0) ? 36.0f : 82.0f;
        target = {awayLaneX(i), clampf(ballPos.y - stagger, fieldTop + 88.0f, fieldBottom - 130.0f)};
      }
    } else if (ballOwner == -1) {
      if (i == looseRunner) {
        target = add(ballPos, mul(ballVel, 14.0f));
      } else {
        float coverY = ballPos.y - ((i % 2 == 0) ? 72.0f : 128.0f);
        target = {awayLaneX(i), clampf(coverY, fieldTop + 88.0f, fieldBottom - 135.0f)};
      }
    } else if (ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT) {
      int carrier = ballOwner - 10;
      if (i == carrier) {
        target = {away[i].pos.x, away[i].pos.y + 50.0f};
      } else {
        float depth = (i == 3) ? 170.0f : 110.0f + (i % 2) * 54.0f;
        target = {awayLaneX(i), clampf(away[carrier].pos.y + depth, fieldTop + 110.0f, fieldBottom - 95.0f)};
      }
    } else {
      target = {awayLaneX(i), fieldTop + 135.0f + (i % 2) * 70.0f};
    }
    float speed = (i == awayPresser || i == looseRunner) ? 106.0f : 82.0f;
    moveToward(away[i], target, speed, dt);
  }

  Vec2 keeperTarget = {clampf(ballPos.x, goalLeft + 18.0f, goalRight - 18.0f), fieldTop + 18.0f};
  moveToward(keeper, keeperTarget, 130.0f, dt);

  if (ballOwner != 21) {
    Vec2 homeKeeperTarget = {clampf(ballPos.x, goalLeft + 18.0f, goalRight - 18.0f), fieldBottom - 18.0f};
    moveToward(homeKeeper, homeKeeperTarget, 130.0f, dt);
  }
}

int nearestHomeTo(Vec2 p) {
  int nearest = 0;
  float best = 100000.0f;
  for (int i = 0; i < HOME_COUNT; ++i) {
    float dist = lengthOf(sub(home[i].pos, p));
    if (dist < best) {
      best = dist;
      nearest = i;
    }
  }
  return nearest;
}

void giveHomeSetPiece(Vec2 spot, Vec2 aim, const char* label, bool outOfBounds = false) {
  int i = nearestHomeTo(spot);
  controlled = i;
  ballOwner = i;
  passTarget = -1;
  moveInput = {0.0f, 0.0f};
  lastAim = normalized(aim);
  home[i].pos = sub(spot, mul(lastAim, 17.0f));
  if (!outOfBounds) clampToField(home[i].pos, 13.0f);
  ballPos = spot;
  ballVel = {0.0f, 0.0f};
  kickLockUntil = millis() + 220;
  setMessage(label, 900);
}

void giveCpuSetPiece(Vec2 spot, const char* label, bool outOfBounds = false) {
  int i = nearestAwayTo(spot);
  ballOwner = 10 + i;
  passTarget = -1;
  away[i].pos = {spot.x, clampf(spot.y - 17.0f, fieldTop + 16.0f, fieldBottom - 16.0f)};
  if (outOfBounds) {
    // 界外球：球员站在场外，不拉回场内
    away[i].pos.x = spot.x;
  }
  ballPos = spot;
  ballVel = {0.0f, 0.0f};
  kickLockUntil = millis() + 520;
  setMessage(label, 900);
}

void restartFromSide(bool leftSide, float y) {
  // 界外球：先定格2秒，再让发球方球员跑到边线外发球
  setPieceState = 1;
  setPieceUntil = millis() + 2000;
  setPieceType = 0;   // 界外球
  setPieceSpot = {leftSide ? fieldLeft - 14.0f : fieldRight + 14.0f,
                  clampf(y, fieldTop + 42.0f, fieldBottom - 42.0f)};
  setPieceAim = {leftSide ? 1.0f : -1.0f, 0.0f};
  setPieceHomeThrow = !lastTouchHome;
  ballVel = {0.0f, 0.0f};
}

// 定格结束：让发球方球员就位发球点
void startSetPiece() {
  Vec2 spot = setPieceSpot;
  ballPos = spot;
  ballVel = {0.0f, 0.0f};
  if (setPieceHomeThrow) {
    int i = nearestHomeTo(spot);
    controlled = i;
    ballOwner = i;
    lastAim = setPieceAim;
    home[i].pos = sub(spot, mul(lastAim, 17.0f));
    if (setPieceType == 0) {
      // 界外球：球员站场外，不拉回场内
    } else {
      clampToField(home[i].pos, 13.0f);
    }
  } else {
    int i = nearestAwayTo(spot);
    ballOwner = 10 + i;
    away[i].pos = {spot.x, clampf(spot.y - 17.0f, fieldTop + 16.0f, fieldBottom - 16.0f)};
    if (setPieceType == 0) away[i].pos.x = spot.x;  // 界外球站场外
  }
  passTarget = -1;
  moveInput = {0.0f, 0.0f};
  kickLockUntil = millis() + 400;
  setPieceState = 2;
}

void restartFromEnd(bool topEnd, float x) {
  bool leftCorner = x < (fieldLeft + fieldRight) * 0.5f;
  bool homeAttackingEnd = topEnd;
  bool cornerToHome = homeAttackingEnd && !lastTouchHome;
  bool cornerToCpu = !homeAttackingEnd && lastTouchHome;

  Vec2 spot;
  Vec2 aim = {0.0f, 0.0f};
  bool homeThrow;
  if (cornerToHome) {
    spot = {leftCorner ? fieldLeft + 18.0f : fieldRight - 18.0f, fieldTop + 18.0f};
    aim = {leftCorner ? 0.75f : -0.75f, -1.0f};
    homeThrow = true;
  } else if (cornerToCpu) {
    spot = {leftCorner ? fieldLeft + 18.0f : fieldRight - 18.0f, fieldBottom - 18.0f};
    homeThrow = false;
  } else if (topEnd) {
    spot = {(fieldLeft + fieldRight) * 0.5f, fieldTop + 58.0f};
    homeThrow = false;
  } else {
    spot = {(fieldLeft + fieldRight) * 0.5f, fieldBottom - 58.0f};
    aim = {0.0f, -1.0f};
    homeThrow = true;
  }
  // 角球/球门球：先定格2秒，再发球
  setPieceState = 1;
  setPieceUntil = millis() + 2000;
  setPieceType = 1;   // 角球/球门球
  setPieceSpot = spot;
  setPieceAim = aim;
  setPieceHomeThrow = homeThrow;
  ballVel = {0.0f, 0.0f};
}

void updateLooseBall(float dt) {
  if (ballOwner != -1) return;

  Vec2 prevBall = ballPos;
  ballPos = add(ballPos, mul(ballVel, 60.0f * dt));
  ballVel = mul(ballVel, 0.986f);
  if (lengthOf(ballVel) < 0.03f) ballVel = {0.0f, 0.0f};

  const float ballR = 7.0f;
  const float postR = 10.0f;
  const bool nearLeftPost = fabsf(ballPos.x - goalLeft) <= postR;
  const bool nearRightPost = fabsf(ballPos.x - goalRight) <= postR;
  const bool insideGoalMouth = ballPos.x > goalLeft + postR && ballPos.x < goalRight - postR;

  // 玩家进攻方向：球从下方越过上门线
  if (prevBall.y >= fieldTop + ballR && ballPos.y <= fieldTop + ballR) {
    if (insideGoalMouth) {
      ++playerScore;
      celebrateActive = true;
      celebratePlayerScored = true;
      celebrateUntil = millis() + 3000;
      ballVel.x *= 0.5f;
      ballVel.y = -fabsf(ballVel.y) * 0.6f;
      spawnConfetti();
      return;
    }
    if (nearLeftPost || nearRightPost) {
      ballPos.y = fieldTop + ballR;
      ballVel.y = fabsf(ballVel.y) * 0.72f;
      ballVel.x = (nearLeftPost ? -fabsf(ballVel.x) : fabsf(ballVel.x)) * 1.12f;
      return;
    }
    // 球从球门外越过底线(非进球非门柱) → 角球或球门球
    restartFromEnd(true, ballPos.x);
    return;
  }

  // CPU 进攻方向：球从上方越过下门线
  if (prevBall.y <= fieldBottom - ballR && ballPos.y >= fieldBottom - ballR) {
    if (insideGoalMouth) {
      ++cpuScore;
      celebrateActive = true;
      celebratePlayerScored = false;
      celebrateUntil = millis() + 3000;
      ballVel.x *= 0.5f;
      ballVel.y = fabsf(ballVel.y) * 0.6f;
      spawnConfetti();
      return;
    }
    if (nearLeftPost || nearRightPost) {
      ballPos.y = fieldBottom - ballR;
      ballVel.y = -fabsf(ballVel.y) * 0.72f;
      ballVel.x = (nearLeftPost ? -fabsf(ballVel.x) : fabsf(ballVel.x)) * 1.12f;
      return;
    }
    // 球从球门外越过底线 → 角球或球门球
    restartFromEnd(false, ballPos.x);
    return;
  }

  // 球进入网底后停下（贴在网里）
  if (ballPos.y < fieldTop - GOAL_DEPTH + ballR + 2) {
    ballPos.y = fieldTop - GOAL_DEPTH + ballR + 2;
    ballVel = {0.0f, 0.0f};
  }
  if (ballPos.y > fieldBottom + GOAL_DEPTH - ballR - 2) {
    ballPos.y = fieldBottom + GOAL_DEPTH - ballR - 2;
    ballVel = {0.0f, 0.0f};
  }

  // 球出界（非进球方向，且非庆祝中）
  if (ballPos.x < fieldLeft - ballR) {
    restartFromSide(true, ballPos.y);
    return;
  }
  if (ballPos.x > fieldRight + ballR) {
    restartFromSide(false, ballPos.y);
    return;
  }
}

void resolvePossession() {
  uint32_t now = millis();
  if (now < kickLockUntil) return;
  // 开球期间禁止抢断，只允许开球方持球
  if (kickoffPending) return;
  // 定位球发球期间禁止抢断(状态1定格 + 状态2发球方持球)
  if (setPieceState != 0) return;

  if (ballOwner >= 0 && ballOwner < HOME_COUNT) {
    for (int i = 0; i < AWAY_COUNT; ++i) {
      if (lengthOf(sub(away[i].pos, ballPos)) < 18.0f) {
        ballOwner = 10 + i;
        lastTouchHome = false;   // 客队抢断，记为客队触球
        cpuKickDown(away[i]);
        return;
      }
    }
    if (lengthOf(sub(keeper.pos, ballPos)) < 18.0f && ballPos.y < fieldTop + 52.0f) {
      ballOwner = 20;
      lastTouchHome = false;     // 客队门将触球
      cpuKickDown(keeper);
      return;
    }
  } else if (ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT) {
    if (lengthOf(sub(homeKeeper.pos, ballPos)) < 18.0f && ballPos.y > fieldBottom - 56.0f) {
      ballOwner = 21;
      lastTouchHome = true;      // 主队门将触球
      controlled = HOME_KEEPER_CONTROL;
      passTarget = -1;
      setMessage("KEEPER", 800);
      return;
    }
  } else if (ballOwner == 21) {
    // 主队门将持球：客队靠近可抢断
    for (int i = 0; i < AWAY_COUNT; ++i) {
      if (lengthOf(sub(away[i].pos, ballPos)) < 16.0f) {
        ballOwner = 10 + i;
        lastTouchHome = false;
        cpuKickDown(away[i]);
        return;
      }
    }
  }

  if (ballOwner == -1) {
    for (int i = 0; i < HOME_COUNT; ++i) {
      if (lengthOf(sub(home[i].pos, ballPos)) < 16.0f) {
        ballOwner = i;
        lastTouchHome = true;    // 主队捡到散球
        controlled = i;
        passTarget = -1;
        return;
      }
    }
    for (int i = 0; i < AWAY_COUNT; ++i) {
      if (lengthOf(sub(away[i].pos, ballPos)) < 15.0f) {
        ballOwner = 10 + i;
        lastTouchHome = false;   // 客队捡到散球
        cpuKickDown(away[i]);
        return;
      }
    }
    if (lengthOf(sub(keeper.pos, ballPos)) < 18.0f && ballPos.y < fieldTop + 56.0f) {
      ballOwner = 20;
      lastTouchHome = false;     // 客队门将捡到散球
      cpuKickDown(keeper);
      return;
    }
    if (lengthOf(sub(homeKeeper.pos, ballPos)) < 18.0f && ballPos.y > fieldBottom - 56.0f) {
      ballOwner = 21;
      lastTouchHome = true;      // 主队门将捡到散球
      controlled = HOME_KEEPER_CONTROL;
      passTarget = -1;
      setMessage("KEEPER", 800);
      return;
    }
  }
}

void updateBallSpin(float dt) {
  // 球在移动(松球有速度 / 持球者正在跑)就让球旋转
  float spinSpeed = 0.0f;
  if (ballOwner == -1) {
    // 松球(传球/射门)：按踢球类型分级旋转速度，都叠加球速
    float base = lengthOf(ballVel);
    if (lastKickType == 2) {
      spinSpeed = base * 1.30f;   // 射门：最快
    } else {
      spinSpeed = base * 0.70f;   // 传球/CPU解围：中等
    }
  } else {
    Vec2 carrierVel = (ballOwner >= 0 && ballOwner < HOME_COUNT) ? home[ballOwner].vel
                   : (ballOwner == 21) ? homeKeeper.vel
                   : (ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT) ? away[ballOwner - 10].vel
                   : keeper.vel;
    spinSpeed = lengthOf(carrierVel) * 0.06f;       // 带球：最慢
  }
  ballSpin += spinSpeed * dt;
}

// 推进步频相位：取场上球员的平均速度作为节奏，跑得快步频高
void updateWalk(float dt) {
  float totalSpeed = 0.0f;
  int count = 0;
  for (int i = 0; i < HOME_COUNT; ++i) { totalSpeed += lengthOf(home[i].vel); ++count; }
  for (int i = 0; i < AWAY_COUNT; ++i) { totalSpeed += lengthOf(away[i].vel); ++count; }
  totalSpeed += lengthOf(homeKeeper.vel); ++count;
  totalSpeed += lengthOf(keeper.vel); ++count;
  float avgSpeed = totalSpeed / count;          // 像素/秒
  // 站立时缓慢微摆(2)，跑动时按速度加快
  walkPhase += (2.0f + avgSpeed * 0.09f) * dt;
}

// 推进比赛时钟：每真实 1 秒显示时间 +1，显示 = 真实
// 阶段转换：上半场→补时→中场→下半场→补时→终场
void startMatchClock() {
  lastClockMs = millis();
}

void advanceMatchClock() {
  if (millis() - lastClockMs < 1000) return;
  lastClockMs += 1000;

  if (matchPhase == PH_HALFTIME || matchPhase == PH_FULLTIME) return;  // 等待按键，不计时

  ++realSecond;
  if (matchPhase == PH_FIRST_HALF) {
    displaySecond += 1;                              // 每秒 +1
    if (realSecond >= HALF_REAL_SEC) {
      realSecond = 0;
      stoppageLen = STOPPAGE_MIN + (rand() % (STOPPAGE_MAX - STOPPAGE_MIN + 1));
      matchPhase = PH_FIRST_STOPPO;
      setMessage("STOPPAGE", 1500);
    }
  } else if (matchPhase == PH_FIRST_STOPPO) {
    displaySecond += 1;                              // 补时也每秒 +1
    if (realSecond >= stoppageLen) {
      realSecond = 0;
      matchPhase = PH_HALFTIME;
      setMessage("HALF TIME", 0);
    }
  } else if (matchPhase == PH_SECOND_HALF) {
    displaySecond += 1;
    if (realSecond >= HALF_REAL_SEC) {
      realSecond = 0;
      stoppageLen = STOPPAGE_MIN + (rand() % (STOPPAGE_MAX - STOPPAGE_MIN + 1));
      matchPhase = PH_SECOND_STOPPO;
      setMessage("STOPPAGE", 1500);
    }
  } else if (matchPhase == PH_SECOND_STOPPO) {
    displaySecond += 1;
    if (realSecond >= stoppageLen) {
      matchPhase = PH_FULLTIME;
      setMessage("FULL TIME", 0);
    }
  }
}

// 开始下半场
void startSecondHalf() {
  realSecond = 0;
  displaySecond = HALF_REAL_SEC;   // 下半场从 3:00(180s) 起继续累加
  matchPhase = PH_SECOND_HALF;
  resetKickoff(false);                 // 下半场换边，由玩家(上球门攻方)开球
  startMatchClock();
  setMessage("2ND HALF", 1200);
}

// 重置整场比赛(终场后重开)
void restartMatch() {
  playerScore = 0;
  cpuScore = 0;
  realSecond = 0;
  displaySecond = 0;
  matchPhase = PH_FIRST_HALF;
  resetKickoff(true);
  startMatchClock();
  setMessage("KICK OFF", 1200);
}

void updateGame(float dt) {
  // 中场休息 / 终场：冻结比赛，等待按键(在 loop 里检测 passPressed/shootPressed)
  if (matchPhase == PH_HALFTIME) {
    advanceMatchClock();
    return;
  }
  if (matchPhase == PH_FULLTIME) {
    advanceMatchClock();
    return;
  }

  // 进球庆祝：让球停在网内约 1s，期间冻结一切持球/抢断逻辑，到点后开球
  if (celebrateActive) {
    readTouchControl();
    updateLooseBall(dt);
    updateBallSpin(dt);
    updateConfetti(dt);
    updateWalk(dt);
    if (millis() >= celebrateUntil) {
      celebrateActive = false;
      // 进球后由失球方在中圈开球：玩家进球→CPU开球(false)，CPU进球→玩家开球(true)
      bool concedingSideStarts = !celebratePlayerScored;
      resetKickoff(concedingSideStarts);
    }
    advanceMatchClock();
    return;
  }

  // 定位球(界外球/角球/球门球)：状态1=定格2秒等待，状态2=球员已就位发球
  if (setPieceState == 1) {
    readTouchControl();
    moveInput = {0.0f, 0.0f};
    updateWalk(dt);
    if (millis() >= setPieceUntil) {
      startSetPiece();
    }
    advanceMatchClock();
    return;
  }
  // setPieceState == 2 时正常进入下方主逻辑，发球方持球后正常发球

  autoSelectClosestToBall();
  readTouchControl();
  if (kickoffPending) {
    if (ballOwner == -1) {
      // 球已被踢出(传球)，开球完成
      kickoffPending = false;
      kickoffPasser = -1;
      kickoffReceiver = -1;
    } else if (ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT && ballOwner == kickoffPasser) {
      // CPU 开球：延迟约 1 秒后传给最近的客队队友
      if (millis() > kickLockUntil + 700) {
        Player& passer = away[kickoffPasser - 10];
        float best = 100000.0f;
        int recv = kickoffReceiver - 10;
        for (int i = 0; i < AWAY_COUNT; ++i) {
          if (10 + i == ballOwner) continue;
          float d = lengthOf(sub(away[i].pos, passer.pos));
          if (d < best) { best = d; recv = i; }
        }
        passTarget = recv;
        Vec2 dir = sub(away[recv].pos, passer.pos);
        kickBall(passer.pos, dir, 6.0f, 1);  // CPU 开球传球
      }
    } else {
      // 玩家开球：禁止带球移动(清零输入)，等玩家按 A 传球
      moveInput = {0.0f, 0.0f};
    }
  }

  // 开球期间：所有球员保持静止，等球传出去后才解锁移动
  if (kickoffPending) {
    attachBallToOwner();
    updateLooseBall(dt);
    updateBallSpin(dt);
    updateWalk(dt);
    advanceMatchClock();
    return;
  }

  updateHome(dt);
  updateAway(dt);

  if (ballOwner >= 10 && ballOwner < 10 + AWAY_COUNT && millis() > kickLockUntil && !kickoffPending) {
    cpuKickDown(away[ballOwner - 10]);
  } else if (ballOwner == 20 && millis() > kickLockUntil) {
    cpuKickDown(keeper);
  }

  attachBallToOwner();
  updateLooseBall(dt);
  resolvePossession();
  attachBallToOwner();
  autoSelectClosestToBall();
  updateBallSpin(dt);
  updateWalk(dt);

  // 定位球状态2：球一旦被踢出(发入场内)即完成
  if (setPieceState == 2 && ballOwner == -1) {
    setPieceState = 0;
  }

  advanceMatchClock();
}

void drawPitch() {
  fillScreen(C_PITCH_1);
  for (int y = fieldTop; y < fieldBottom; y += 76) {
    uint16_t c = ((y / 76) & 1) ? C_PITCH_1 : C_PITCH_2;
    worldBox(fieldLeft, y, fieldRight - fieldLeft, min(76, fieldBottom - y), c);
  }

  float midX = (fieldLeft + fieldRight) * 0.5f;
  float midY = (fieldTop + fieldBottom) * 0.5f;
  pitchRect(fieldLeft, fieldTop, fieldRight - fieldLeft, fieldBottom - fieldTop);
  pitchLine({(float)fieldLeft, midY}, {(float)fieldRight, midY});
  pitchCircle(midX, midY, 90);
  worldCircle({midX, midY}, PITCH_LINE_W, C_LINE);

  pitchRect(midX - 170, fieldTop, 340, 150);
  pitchRect(midX - 82, fieldTop, 164, 58);
  pitchRect(midX - 170, fieldBottom - 150, 340, 150);
  pitchRect(midX - 82, fieldBottom - 58, 164, 58);

  const int goalDepth = 40;
  const uint16_t netBack = rgb565(205, 215, 218);
  const uint16_t netMesh = rgb565(150, 166, 170);

  // 顶部球门：球网向场外(上)延伸
  worldBox(goalLeft - 6, fieldTop - goalDepth, goalRight - goalLeft + 12, goalDepth, netBack);
  for (int x = goalLeft - 4; x <= goalRight + 4; x += 9)
    worldLine({(float)x, (float)(fieldTop - goalDepth + 3)}, {(float)x, (float)(fieldTop - 2)}, netMesh);
  for (int y = fieldTop - goalDepth + 5; y <= fieldTop - 4; y += 8)
    worldLine({(float)(goalLeft - 4), (float)y}, {(float)(goalRight + 4), (float)y}, netMesh);
  worldBox(goalLeft - 3, fieldTop - goalDepth, 5, goalDepth, C_WHITE);          // 左门柱
  worldBox(goalRight - 2, fieldTop - goalDepth, 5, goalDepth, C_WHITE);         // 右门柱
  worldBox(goalLeft - 3, fieldTop - goalDepth, goalRight - goalLeft + 6, 5, C_WHITE);  // 横梁

  // 底部球门：球网向场外(下)延伸
  worldBox(goalLeft - 6, fieldBottom, goalRight - goalLeft + 12, goalDepth, netBack);
  for (int x = goalLeft - 4; x <= goalRight + 4; x += 9)
    worldLine({(float)x, (float)(fieldBottom + 2)}, {(float)x, (float)(fieldBottom + goalDepth - 3)}, netMesh);
  for (int y = fieldBottom + 4; y <= fieldBottom + goalDepth - 5; y += 8)
    worldLine({(float)(goalLeft - 4), (float)y}, {(float)(goalRight + 4), (float)y}, netMesh);
  worldBox(goalLeft - 3, fieldBottom, 5, goalDepth, C_WHITE);                          // 左门柱
  worldBox(goalRight - 2, fieldBottom, 5, goalDepth, C_WHITE);                         // 右门柱
  worldBox(goalLeft - 3, fieldBottom + goalDepth - 5, goalRight - goalLeft + 6, 5, C_WHITE);  // 横梁
}

void drawScoreboard() {
  const int hudY = 40;
  const int hudX = 100;
  roundBox(10 + hudX, 5 + hudY, 108, 23, 3, C_PANEL);
  rectLine(10 + hudX, 5 + hudY, 108, 23, C_PANEL_EDGE);
  textCenter("BRA", 28 + hudX, 16 + hudY, &fonts::Font2, C_WHITE, C_PANEL);
  char score[12];
  snprintf(score, sizeof(score), "%d-%d", playerScore, cpuScore);
  textCenter(score, 64 + hudX, 16 + hudY, &fonts::Font2, C_WHITE, C_PANEL);
  textCenter("ENG", 100 + hudX, 16 + hudY, &fonts::Font2, C_WHITE, C_PANEL);

  roundBox(38 + hudX, 28 + hudY, 52, 17, 3, C_WHITE);
  char t[10];
  int mm = displaySecond / 60;
  int ss = displaySecond % 60;
  snprintf(t, sizeof(t), "%02d:%02d", mm, ss);
  textCenter(t, 64 + hudX, 36 + hudY, &fonts::Font2, C_BLACK, C_WHITE);
}

// 画头发(根据发型)，参数 hx/hy 为头部中心(与头部底色一致)
// 头部底色范围：hx-7 .. hx+6 (宽14)，hy-18 .. hy-7
void drawHair(const Player& p, int hx, int hy) {
  uint16_t hc = p.hairColor;
  // 头顶覆盖区域基准：宽14，从顶部向下盖若干行
  switch (p.hairStyle) {
    case 0: // 短发：头顶一整块，无缺口
      box(hx - 7, hy - 18, 14, 6, hc);
      break;
    case 1: // 寸头：薄一层贴头顶
      box(hx - 7, hy - 18, 14, 3, hc);
      break;
    case 2: // 中分：整块覆盖，中间用肤色画一条分缝线
      box(hx - 7, hy - 18, 14, 6, hc);
      box(hx - 1, hy - 18, 2, 6, C_SKIN);   // 中分缝
      break;
    case 3: // 蓬松：比头部宽一圈
      box(hx - 8, hy - 19, 16, 8, hc);
      box(hx - 7, hy - 12, 14, 2, hc);      // 两侧盖下来
      break;
    case 4: // 马尾：头顶短发 + 后侧一小撮
      box(hx - 7, hy - 18, 14, 6, hc);
      box(hx + 6, hy - 14, 3, 6, hc);       // 后侧马尾
      break;
    case 5: // 光头：头顶用肤色填满(不留透明)
      box(hx - 7, hy - 18, 14, 3, C_SKIN);
      break;
    default:
      break;
  }
}

void drawPlayerKit(const Player& p, bool selected, uint16_t shirt, uint16_t trim, uint16_t numColor, const char* label = nullptr) {
  int x = worldX(p.pos.x);
  int y = worldY(p.pos.y);
  if (x < -40 || y < -50 || x > screenW + 40 || y > screenH + 45) return;

  // 跑动幅度：速度越大幅度越大，静止时近乎不动
  float speed = lengthOf(p.vel);
  float gait = clampf(speed / 130.0f, 0.0f, 1.0f);   // 0=站立, 1=全力跑
  float phase = sinf(walkPhase) * gait;               // -1..1，左右脚相位差 π
  // 朝向：向下跑(vel.y>0)→正面(看到脸)；向上跑(vel.y<0)→背面(看到后背)；静止默认正面
  bool facingFront = !(speed > 12.0f && p.vel.y < -8.0f);

  // 椭圆阴影
  if (useCanvas) canvas.fillEllipse(x, y + 16, 13, 5, C_SHADOW);
  else M5.Display.fillEllipse(x, y + 16, 13, 5, C_SHADOW);

  // 双脚(粗腿+黑鞋)：上下交错，跑动时一只抬起一只落下；左右关于身体中心 x 对称
  int footBaseY = y + 15;
  int liftAmp = (int)(gait * 5.0f);
  int leftY = footBaseY - (int)(phase * liftAmp);
  int rightY = footBaseY + (int)(phase * liftAmp);
  int leftX = x - 5;     // 左腿: x-5 .. x-1 (宽4)
  int rightX = x + 1;    // 右腿: x+1 .. x+5 (宽4)，整体 x-5..x+5 关于 x 对称
  // 短裤下的腿(肤色)
  box(leftX, y + 7, 4, leftY - (y + 7), C_SKIN);
  box(rightX, y + 7, 4, rightY - (y + 7), C_SKIN);
  // 鞋(黑色方块，也关于 x 对称)
  box(leftX - 1, leftY, 6, 4, C_BLACK);    // x-6 .. x-1
  box(rightX, rightY, 6, 4, C_BLACK);      // x+1 .. x+6(左偏1补偿，整体居中)

  // 短裤
  box(x - 9, y + 4, 18, 5, trim);

  // 球衣(方块) + 竖条纹
  box(x - 10, y - 6, 20, 11, shirt);
  // 两条竖条纹(用 trim 色)，跳过号码区域
  box(x - 6, y - 6, 2, 4, trim);
  box(x + 4, y - 6, 2, 4, trim);
  // 衣领(只有正面才看到)
  if (facingFront) box(x - 3, y - 7, 6, 2, C_SKIN);

  // 手臂(肤色，和腿同色)：在球衣两侧，跑动时轻微摆动(与脚反向)
  int armSwing = (int)(phase * 3.0f);    // 跑动摆动幅度
  int armTopY = y - 4;
  int armBotY = y + 6;
  box(x - 13, armTopY + armSwing, 3, armBotY - armTopY, C_SKIN);   // 左臂(随相位下移)
  box(x + 10, armTopY - armSwing, 3, armBotY - armTopY, C_SKIN);   // 右臂(反向)

  // 头部(方块) + 头发
  box(x - 7, y - 18, 14, 12, C_SKIN);
  box(x - 7, y - 18, 14, 2, C_SKIN_DARK);  // 头顶阴影
  drawHair(p, x, y);   // 传头部基准(发型内部用 y-18 对齐头顶)
  // 面部：正面才画眼睛，背面不画(后脑勺)
  if (facingFront) {
    box(x - 4, y - 13, 2, 2, C_BLACK);
    box(x + 2, y - 13, 2, 2, C_BLACK);
  }

  // 号码 / 标签：正面印胸前(中部)，背面印后背(上部)
  char num[4];
  if (label) snprintf(num, sizeof(num), "%s", label);
  else snprintf(num, sizeof(num), "%u", p.number);
  int numY = facingFront ? (y + 1) : (y - 4);   // 正面中部 / 背面上部
  textCenter(num, x, numY, &fonts::Font0, numColor, shirt);

  if (selected) {
    // 倒三角(▼，顶点朝下指向球员)，距头顶 4px
    if (useCanvas) canvas.fillTriangle(x, y - 28, x - 8, y - 40, x + 8, y - 40, C_ACCENT);
    else M5.Display.fillTriangle(x, y - 28, x - 8, y - 40, x + 8, y - 40, C_ACCENT);
  }
}

void drawPlayer(const Player& p, bool selected) {
  uint16_t shirt = p.home ? C_YELLOW : C_RED;
  uint16_t trim = p.home ? C_BLUE : C_RED_DARK;
  uint16_t numColor = p.home ? C_BLACK : C_WHITE;
  drawPlayerKit(p, selected, shirt, trim, numColor);
}

void drawKeeper(const Player& p, bool homeSide, bool selected = false) {
  uint16_t shirt = homeSide ? C_KEEPER_HOME : C_KEEPER_AWAY;
  uint16_t trim = homeSide ? C_BLUE_DARK : C_ORANGE;
  uint16_t numColor = homeSide ? C_BLACK : C_WHITE;
  drawPlayerKit(p, selected, shirt, trim, numColor);   // 守门员也显示号码(=1)，不用 GK
}

void drawBall() {
  int x = worldX(ballPos.x);
  int y = worldY(ballPos.y);
  if (x < -24 || y < -24 || x > screenW + 24 || y > screenH + 24) return;

  static const char* sprite[17] = {
    ".....KKKKKKK.....",
    "...KKWWWWWWWKK...",
    "..KWWWWGWWWWWWK..",
    ".KWWKKGWWWKKWWWK.",
    ".KWKBBKWWKBBKWWK.",
    "KWWKBBKGWKBBKWWWK",
    "KWWWKKGWWGKKWWWWK",
    "KWWWWGGKKGGWWWWWK",
    "KWWWWKBBBBKWWWWWK",
    "KWWWWKBBBBKGWWWWK",
    "KWWWGGKBBKGGWWWWK",
    "KWWKKWGKKGWKKWWWK",
    ".KWKBBWWWWWBBKWK.",
    ".KWWKKWWWWWKKWWK.",
    "..KWWWWGGWWWWWK..",
    "...KKWWWWWWWKK...",
    ".....KKKKKKK....."
  };

  // 阴影(不随球旋转)
  box(x - 6, y + 9, 15, 4, C_SHADOW);

  // 旋转球体：把 17x17 sprite 每个像素绕中心旋转 ballSpin 弧度
  const int R = 8;
  const float c = cosf(ballSpin);
  const float s = sinf(ballSpin);
  for (int dy = -R; dy <= R; ++dy) {
    for (int dx = -R; dx <= R; ++dx) {
      // 落在圆形内的目标像素才画
      if (dx * dx + dy * dy > R * R) continue;
      // 反向映射到源 sprite 坐标
      float sx =  dx * c + dy * s;
      float sy = -dx * s + dy * c;
      int srcCol = (int)lroundf(sx) + R;
      int srcRow = (int)lroundf(sy) + R;
      if (srcCol < 0 || srcCol >= 17 || srcRow < 0 || srcRow >= 17) continue;
      char p = sprite[srcRow][srcCol];
      if (p == '.') continue;
      uint16_t color;
      if (p == 'K' || p == 'B') color = C_BLACK;
      else if (p == 'G') color = C_BALL_GRAY;
      else if (p == 'W') color = C_WHITE;
      else continue;
      if (p == 'W' && srcCol > 12 && srcRow > 3 && srcRow < 14) color = C_BALL_LIGHT;
      box(x + dx, y + dy, 1, 1, color);
    }
  }
}

// ---- 进球庆祝彩纸屑 ----
void spawnConfetti() {
  // 从屏幕顶部两侧散落
  static const uint16_t colors[] = {
    C_YELLOW, C_RED, C_BLUE, C_ACCENT, C_ORANGE, C_KEEPER_HOME, C_WHITE
  };
  static const int nColors = sizeof(colors) / sizeof(colors[0]);
  for (int i = 0; i < CONFETTI_MAX; ++i) {
    if (!confetti[i].active) {
      confetti[i].active = true;
      confetti[i].x = (float)(rand() % screenW);
      confetti[i].y = -10.0f - (float)(rand() % 80);
      confetti[i].vx = ((float)(rand() % 100) - 50.0f) * 0.6f;
      confetti[i].vy = 40.0f + (float)(rand() % 70);
      confetti[i].color = colors[rand() % nColors];
      confetti[i].rot = (float)(rand() % 360) * (3.14159265f / 180.0f);
      confetti[i].vrot = ((float)(rand() % 100) - 50.0f) * 0.05f;
      confetti[i].size = 3 + (rand() % 4);
    }
  }
}

void updateConfetti(float dt) {
  for (int i = 0; i < CONFETTI_MAX; ++i) {
    if (!confetti[i].active) continue;
    confetti[i].vy += 60.0f * dt;            // 重力
    confetti[i].vx *= (1.0f - 0.8f * dt);    // 空气阻力(水平)
    confetti[i].x += confetti[i].vx * dt;
    confetti[i].y += confetti[i].vy * dt;
    confetti[i].rot += confetti[i].vrot;
    if (confetti[i].y > screenH + 20) confetti[i].active = false;
  }
}

void drawConfetti() {
  for (int i = 0; i < CONFETTI_MAX; ++i) {
    if (!confetti[i].active) continue;
    int x = (int)confetti[i].x;
    int y = (int)confetti[i].y;
    int s = confetti[i].size;
    // 用旋转角度模拟翻面：奇偶周期切换为细条/方块，营造纸片翻转感
    float r = fmodf(fabsf(confetti[i].rot), 2.0f * 3.14159265f);
    float shrink = cosf(r);                  // -1..1，越接近0越窄(侧视)
    int w = s;
    int h = s;
    if (fabsf(shrink) < 0.55f) w = 1;        // 侧视时变细条
    box(x - w / 2, y - h / 2, w, h, confetti[i].color);
  }
}

void drawGoalBanner() {
  // 屏幕中间黄底黑字 GOAL
  const int bw = 240;
  const int bh = 70;
  int bx = screenW / 2 - bw / 2;
  int by = screenH / 2 - bh / 2;
  roundBox(bx, by, bw, bh, 10, C_YELLOW);
  rectLine(bx, by, bw, bh, C_BLACK);
  textCenter("GOAL!", screenW / 2, screenH / 2, &fonts::Font4, C_BLACK, C_YELLOW);
}

// 中场休息 / 终场覆盖层
void drawMatchOverlay() {
  // 半透明遮罩
  box(0, 0, screenW, screenH, rgb565(10, 14, 12));
  const int bw = 300;
  const int bh = 150;
  int bx = screenW / 2 - bw / 2;
  int by = screenH / 2 - bh / 2;

  bool fulltime = (matchPhase == PH_FULLTIME);
  const char* title = fulltime ? "FULL TIME" : "HALF TIME";
  uint16_t titleColor = fulltime ? C_RED : C_ACCENT;

  roundBox(bx, by, bw, bh, 12, C_PANEL);
  rectLine(bx, by, bw, bh, C_PANEL_EDGE);
  textCenter(title, screenW / 2, by + 34, &fonts::Font4, titleColor, C_PANEL);

  char score[20];
  snprintf(score, sizeof(score), "BRA %d - %d ENG", playerScore, cpuScore);
  textCenter(score, screenW / 2, by + 78, &fonts::Font4, C_WHITE, C_PANEL);

  const char* hint = fulltime ? "Press A/B to restart" : "Press A/B for 2nd half";
  textCenter(hint, screenW / 2, by + 122, &fonts::Font2, C_YELLOW, C_PANEL);
}

void drawTouchIndicator() {
  // 摇杆指示器已隐藏(触摸控制功能保留，由 readTouchControl 单独处理)
}

void drawGame() {
  updateCamera();
  drawPitch();
  for (int i = 0; i < AWAY_COUNT; ++i) drawPlayer(away[i], false);
  drawKeeper(keeper, false);
  drawKeeper(homeKeeper, true, controlled == HOME_KEEPER_CONTROL);
  for (int i = 0; i < HOME_COUNT; ++i) drawPlayer(home[i], i == controlled);
  drawBall();
  drawTouchIndicator();
  drawScoreboard();
  if (celebrateActive) {
    drawConfetti();
    drawGoalBanner();
  }
  if (matchPhase == PH_HALFTIME || matchPhase == PH_FULLTIME) {
    drawMatchOverlay();
  }
  pushFrame();
}

void basketInit() {
  basketScore = 0;
  basketAttempts = 0;
  basketMade = 0;
  basketShotActive = false;
  basketShotMade = false;
  basketResultReady = false;
  basketResultUntil = 0;
  basketLastShotMs = 0;
  basketBallX = screenW / 2.0f;
  basketBallY = screenH - 80.0f;
  basketBallR = 14.0f;
  basketSwingMeter = 0.0f;
  basketLastAccelMag = 1.0f;
  basketImuReady = M5.Imu.isEnabled();
  if (basketImuReady) {
    float ax, ay, az;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      basketLastAccelMag = sqrtf(ax * ax + ay * ay + az * az);
    }
  }
}

static bool basketReadSwing() {
  if (!basketImuReady || !M5.Imu.isEnabled()) return false;
  float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
  bool okA = M5.Imu.getAccel(&ax, &ay, &az);
  bool okG = M5.Imu.getGyro(&gx, &gy, &gz);
  if (!okA && !okG) return false;

  float accelMag = okA ? sqrtf(ax * ax + ay * ay + az * az) : basketLastAccelMag;
  float jerk = fabsf(accelMag - basketLastAccelMag);
  basketLastAccelMag = accelMag;
  float gyroMag = okG ? sqrtf(gx * gx + gy * gy + gz * gz) : 0.0f;
  float impulse = jerk * 1.35f + gyroMag * 0.0042f;
  basketSwingMeter = max(basketSwingMeter * 0.84f, clampf(impulse * 0.55f, 0.0f, 1.0f));
  basketPower = impulse;

  uint32_t now = millis();
  return (now - basketLastShotMs > 780) && (jerk > 0.48f || gyroMag > 145.0f) && impulse > 0.68f;
}

void basketStartShot(float power) {
  uint32_t now = millis();
  if (basketShotActive || now - basketLastShotMs < 520) return;
  basketLastShotMs = now;
  basketAttempts++;
  basketResultReady = false;

  float quality = 1.0f - fabsf(power - 1.35f) * 0.42f;
  quality = clampf(quality, 0.0f, 1.0f);
  int chance = 42 + (int)(quality * 48.0f);
  basketShotMade = (rand() % 100) < chance;

  basketStartX = screenW / 2.0f + (rand() % 17 - 8);
  basketStartY = screenH - 72.0f;
  basketEndX = screenW / 2.0f;
  basketEndY = 112.0f;
  if (!basketShotMade) {
    int miss = 34 + (rand() % 28);
    basketEndX += ((rand() % 2) ? miss : -miss);
    basketEndY += (rand() % 24) - 10;
  }
  basketCtrlX = (basketStartX + basketEndX) * 0.5f + (rand() % 41 - 20);
  basketCtrlY = 30.0f + (rand() % 24);
  basketBallX = basketStartX;
  basketBallY = basketStartY;
  basketBallR = 15.0f;
  basketBallT = 0.0f;
  basketSpin = 0.0f;
  basketShotActive = true;
}

void basketUpdate(float dt, bool shootInput) {
  bool touchShoot = false;
  if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
    touchShoot = true;
  }

  bool swing = basketReadSwing();
  if ((shootInput || touchShoot || swing) && !basketShotActive) {
    float p = swing ? basketPower : 1.25f;
    basketStartShot(p);
  }

  basketSwingMeter *= 0.96f;
  if (basketShotActive) {
    basketBallT += dt * 1.23f;
    basketSpin += dt * 10.0f;
    float t = min(1.0f, basketBallT);
    float u = 1.0f - t;
    basketBallX = u * u * basketStartX + 2.0f * u * t * basketCtrlX + t * t * basketEndX;
    basketBallY = u * u * basketStartY + 2.0f * u * t * basketCtrlY + t * t * basketEndY;
    basketBallR = 15.0f - t * 5.0f;

    if (basketBallT > 1.0f) {
      float fall = basketBallT - 1.0f;
      if (basketShotMade) {
        basketBallX = screenW / 2.0f;
        basketBallY = 112.0f + fall * 78.0f;
      } else {
        basketBallY = basketEndY + fall * fall * 360.0f;
      }
    }

    if (basketBallT > (basketShotMade ? 1.32f : 1.48f)) {
      basketShotActive = false;
      basketResultReady = true;
      basketResultUntil = millis() + 900;
      if (basketShotMade) {
        basketMade++;
        basketScore += 2;
        if (basketScore > basketBest) basketBest = basketScore;
      }
    }
  }

  if (basketResultReady && millis() > basketResultUntil) {
    basketResultReady = false;
    basketBallX = screenW / 2.0f;
    basketBallY = screenH - 72.0f;
    basketBallR = 15.0f;
  }
}

void drawBasketBall(int x, int y, int r) {
  circle(x + 2, y + 3, r, rgb565(124, 70, 24));
  circle(x, y, r, rgb565(232, 124, 32));
  circle(x - r / 3, y - r / 3, max(2, r / 3), rgb565(255, 170, 66));
  wideLine(x - r, y, x + r, y, 2, rgb565(80, 46, 28));
  line(x, y - r, x, y + r, rgb565(80, 46, 28));
  line(x - r / 2, y - r + 2, x + r / 2, y + r - 2, rgb565(80, 46, 28));
  line(x + r / 2, y - r + 2, x - r / 2, y + r - 2, rgb565(80, 46, 28));
}

void drawBasketHoop() {
  int x0 = screenW / 2 - BASKET_HOOP_W / 2;
  int y0 = 24;
  for (int y = 0; y < BASKET_HOOP_H; ++y) {
    int sy = y0 + y;
    if (sy < 0 || sy >= screenH) continue;
    int row = y * BASKET_HOOP_W;
    for (int x = 0; x < BASKET_HOOP_W; ++x) {
      int sx = x0 + x;
      if (sx < 0 || sx >= screenW) continue;
      int idx = row + x;
      if (!pgm_read_byte(&BASKET_HOOP_MASK[idx])) continue;
      uint16_t c = pgm_read_word(&BASKET_HOOP_PIXELS[idx]);
      if (useCanvas) canvas.drawPixel(sx, sy, c);
      else M5.Display.drawPixel(sx, sy, c);
    }
  }
}

void drawSevenSegment(int x, int y, int w, int h, int digit, uint16_t c) {
  static const uint8_t segs[10] = {
    0b0111111, // 0
    0b0000110, // 1
    0b1011011, // 2
    0b1001111, // 3
    0b1100110, // 4
    0b1101101, // 5
    0b1111101, // 6
    0b0000111, // 7
    0b1111111, // 8
    0b1101111  // 9
  };
  if (digit < 0 || digit > 9) return;
  int t = max(8, w / 7);
  int mid = y + h / 2;
  uint8_t s = segs[digit];
  auto segLine = [&](int bit, int x0, int y0, int x1, int y1) {
    if (!(s & (1 << bit))) return;
    wideLine(x0, y0, x1, y1, t, c);   // 只画本体颜色，无黑色描边
  };
  segLine(0, x + t, y, x + w - t, y);             // A
  segLine(1, x + w, y + t, x + w, mid - t);       // B
  segLine(2, x + w, mid + t, x + w, y + h - t);   // C
  segLine(3, x + t, y + h, x + w - t, y + h);     // D
  segLine(4, x, mid + t, x, y + h - t);           // E
  segLine(5, x, y + t, x, mid - t);               // F
  segLine(6, x + t, mid, x + w - t, mid);         // G
}

void drawBasketScoreBehindHoop() {
  char score[6];
  snprintf(score, sizeof(score), "%02d", basketScore);   // 两位数显示: 01, 02, 03...
  int len = strlen(score);
  if (len > 3) {
    memmove(score, score + len - 3, 4);
    len = 3;
  }
  // 数字等比缩小(高 320→220，比例 0.6875)
  int digitH = 220;
  int gap = 48;                                   // 数字间距
  int digitW = (len >= 3) ? 82 : 103;             // 120×0.6875≈82, 150×0.6875≈103
  int totalW = len * digitW + (len - 1) * gap;
  int x = screenW / 2 - totalW / 2;
  int y = (screenH - digitH) / 2 - 4 - 40;          // 往上移动40px
  // 白色 60% 透明度(255×0.6≈153，保持蓝灰色调按比例缩放)
  uint16_t translucentWhite = rgb565(153, 163, 172);
  for (int i = 0; i < len; ++i) {
    drawSevenSegment(x + i * (digitW + gap), y, digitW, digitH, score[i] - '0', translucentWhite);
  }
}

void drawBasketGame() {
  uint16_t bg = C_BLACK;
  fillScreen(bg);
  box(0, screenH - 132, screenW, 132, rgb565(188, 118, 56));
  wideLine(0, screenH - 132, screenW, screenH - 132, 4, rgb565(238, 214, 164));
  rectLine(screenW / 2 - 72, screenH - 126, 144, 116, rgb565(238, 214, 164));
  circle(screenW / 2, screenH - 68, 36, rgb565(238, 214, 164));
  box(screenW / 2 - 34, screenH - 126, 68, 4, rgb565(238, 214, 164));

  drawBasketScoreBehindHoop();
  drawBasketHoop();

  int meterW = 126;
  int meterX = screenW / 2 - meterW / 2;
  int meterY = screenH - 38;
  roundBox(meterX, meterY, meterW, 12, 6, rgb565(48, 56, 62));
  box(meterX + 3, meterY + 3, (int)((meterW - 6) * clampf(basketSwingMeter, 0.0f, 1.0f)), 6, C_YELLOW);

  textLeft("AIR SHOT", 12, 10, &fonts::Font4, C_WHITE, bg);
  char buf[48];
  snprintf(buf, sizeof(buf), "%d/%d", basketMade, basketAttempts);
  textLeft("MADE", screenW - 82, 46, &fonts::Font0, C_DIM, bg);
  textLeft(buf, screenW - 82, 58, &fonts::Font2, C_WHITE, bg);
  textLeft("B=MENU", screenW - 74, 12, &fonts::Font0, C_WHITE, bg);

  if (!basketShotActive) {
    drawBasketBall((int)basketBallX, (int)basketBallY, (int)basketBallR);
  }
  if (basketShotActive) {
    for (float t = 0.15f; t < min(1.0f, basketBallT); t += 0.18f) {
      float u = 1.0f - t;
      int px = (int)(u * u * basketStartX + 2.0f * u * t * basketCtrlX + t * t * basketEndX);
      int py = (int)(u * u * basketStartY + 2.0f * u * t * basketCtrlY + t * t * basketEndY);
      circle(px, py, 2, rgb565(255, 228, 150));
    }
    drawBasketBall((int)basketBallX, (int)basketBallY, max(5, (int)basketBallR));
  }

  if (basketResultReady) {
    const char* result = basketShotMade ? "SWISH!" : "MISS";
    uint16_t bg = basketShotMade ? rgb565(242, 174, 38) : rgb565(72, 78, 88);
    roundBox(screenW / 2 - 78, 190, 156, 48, 8, bg);
    textCenter(result, screenW / 2, 214, &fonts::Font4, C_WHITE, bg);
  } else if (!basketShotActive) {
    const char* hint = basketImuReady ? "Swing to shoot" : "Tap / A to shoot";
    textCenter(hint, screenW / 2, 306, &fonts::Font2, C_WHITE, bg);
  }

  pushFrame();
}

void drawBoot() {
  fillScreen(C_PITCH_1);
  textCenter("STOPWATCH FOOTBALL", screenW / 2, screenH / 2 - 26, &fonts::Font4, C_WHITE, C_PITCH_1);
  textCenter("Touch field to move", screenW / 2, screenH / 2 + 8, &fonts::Font2, C_WHITE, C_PITCH_1);
  textCenter("Closest player auto selected", screenW / 2, screenH / 2 + 34, &fonts::Font2, C_WHITE, C_PITCH_1);
  textCenter("A=PASS  B=SHOOT", screenW / 2, screenH / 2 + 60, &fonts::Font2, C_ACCENT, C_PITCH_1);
  pushFrame();
}

// ---- 菜单绘制 ----
void drawMenu() {
  fillScreen(rgb565(20, 24, 32));
  // 标题
  textCenter("SELECT GAME", screenW / 2, 58, &fonts::Font4, C_YELLOW, rgb565(20, 24, 32));
  // 三个卡片
  const int cardW = 134;
  const int cardH = 186;
  const int gap = 14;
  int totalW = cardW * GAME_COUNT + gap * (GAME_COUNT - 1);
  int startX = (screenW - totalW) / 2;
  int cardY = 142;
  for (int i = 0; i < GAME_COUNT; ++i) {
    int cx = startX + i * (cardW + gap);
    bool sel = (i == menuSelected);
    uint16_t bg = sel ? rgb565(45, 90, 140) : rgb565(34, 40, 52);
    uint16_t edge = sel ? C_ACCENT : C_DIM;
    roundBox(cx, cardY, cardW, cardH, 8, bg);
    rectLine(cx, cardY, cardW, cardH, edge);
    const char* name = (i == 0) ? "FOOTBALL" : (i == 1 ? "RACING" : "AIR SHOT");
    textCenter(name, cx + cardW / 2, cardY + cardH / 2 - 6, (i == 2 ? &fonts::Font2 : &fonts::Font4),
               sel ? C_WHITE : C_DIM, bg);
    // 简易图标提示
    if (i == 0) {
      circle(cx + cardW / 2, cardY + 50, 18, C_WHITE);
      circle(cx + cardW / 2, cardY + 50, 5, C_BLACK);
    } else if (i == 1) {
      roundBox(cx + cardW / 2 - 22, cardY + 36, 44, 28, 4, C_RED);
      box(cx + cardW / 2 - 16, cardY + 42, 32, 10, C_BLACK);
    } else {
      box(cx + cardW / 2 - 30, cardY + 30, 60, 34, rgb565(210, 224, 230));
      wideLine(cx + cardW / 2 - 20, cardY + 72, cx + cardW / 2 + 20, cardY + 72, 4, C_ORANGE);
      circle(cx + cardW / 2, cardY + 44, 13, C_ORANGE);
    }
  }
  // 操作提示
  textCenter("Touch / B to choose", screenW / 2, screenH - 50, &fonts::Font2, C_DIM, rgb565(20, 24, 32));
  textCenter("A=start", screenW / 2, screenH - 28, &fonts::Font0, C_DIM, rgb565(20, 24, 32));
  pushFrame();
}

void setup() {
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5StopWatch;
  cfg.serial_baudrate = 115200;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.internal_imu = true;
  cfg.internal_rtc = false;
  M5.begin(cfg);

  int displayW = M5.Display.width();
  int displayH = M5.Display.height();
  screenW = min(RENDER_W, displayW);
  screenH = min(RENDER_H, displayH);
  screenOffsetX = max(0, (displayW - screenW) / 2);
  screenOffsetY = max(0, (displayH - screenH) / 2);
  M5.Display.setBrightness(190);
  M5.Display.setColorDepth(16);
  M5.Display.fillScreen(C_BLACK);
  canvas.setColorDepth(16);
  useCanvas = canvas.createSprite(screenW, screenH) != nullptr;
  if (!useCanvas) M5.Display.fillScreen(C_BLACK);

  fieldLeft = 0;
  fieldTop = 0;
  fieldRight = fieldLeft + (screenW * FIELD_SCALE_NUM) / FIELD_SCALE_DEN;
  fieldBottom = fieldTop + (screenH * FIELD_SCALE_NUM) / FIELD_SCALE_DEN;
  goalLeft = (fieldLeft + fieldRight) / 2 - 84;
  goalRight = (fieldLeft + fieldRight) / 2 + 84;
  cameraX = 0.0f;
  cameraY = 0.0f;

  initRawButtons();
  drawBoot();
  resetKickoff(true);
  matchPhase = PH_FIRST_HALF;
  realSecond = 0;
  displaySecond = 0;
  playerScore = 0;
  cpuScore = 0;
  lastTickMs = millis();
  startMatchClock();
  gameMode = MODE_MENU;   // 开机进菜单选择游戏

  Serial.printf("[Boot] display=%dx%d canvas=%s touch=%s keyA_idle=%d keyB_idle=%d\n",
                screenW, screenH, useCanvas ? "ok" : "off",
                M5.Touch.isEnabled() ? "ok" : "off",
                rawA.idleLevel, rawB.idleLevel);
}

void loop() {
  M5.update();
  updateRawButton(rawA);
  updateRawButton(rawB);

  bool passPressed = rawA.pressed || M5.BtnA.wasPressed();
  bool shootPressed = rawB.pressed || M5.BtnB.wasPressed();

  uint32_t now = millis();
  float dt = (now - lastTickMs) / 1000.0f;
  lastTickMs = now;
  dt = clampf(dt, 0.0f, 0.040f);

  if (gameMode == MODE_MENU) {
    // 菜单：触摸左/右选游戏，A 确认
    if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
      auto& pt = M5.Touch.getTouchPointRaw(0);
      Vec2 tp = touchToScreen(pt.x, pt.y);
      menuSelected = clampf(tp.x / (screenW / (float)GAME_COUNT), 0.0f, (float)(GAME_COUNT - 1));
    }
    if (shootPressed) menuSelected = (menuSelected + 1) % GAME_COUNT;  // B 切换
    if (passPressed) {
      if (menuSelected == 0) {
        // 进足球
        resetKickoff(true);
        matchPhase = PH_FIRST_HALF;
        realSecond = 0; displaySecond = 0;
        playerScore = 0; cpuScore = 0;
        startMatchClock();
        gameMode = MODE_FOOTBALL;
      } else if (menuSelected == 1) {
        // 进赛车
        racingInit();
        gameMode = MODE_RACING;
      } else {
        // 进空气投篮
        basketInit();
        gameMode = MODE_BASKETBALL;
      }
    }
    drawMenu();
    delay(16);
    return;
  }

  if (gameMode == MODE_RACING) {
    // 赛车：触摸转向，A 加速，B 刹车；Game Over 后 A 重开，B 返回菜单
    float steerX = 0.0f;
    if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
      auto& pt = M5.Touch.getTouchPointRaw(0);
      Vec2 tp = touchToScreen(pt.x, pt.y);
      steerX = clampf((tp.x - screenW / 2.0f) / (screenW / 2.0f), -1.0f, 1.0f);
    }
    if (racingIsGameOver()) {
      if (passPressed) racingRestartFromExternal();
      else if (shootPressed) { gameMode = MODE_MENU; }
    } else {
      racingHandleInput(passPressed, shootPressed, steerX);
      racingUpdate(dt);
    }
    racingDraw();
    delay(16);
    return;
  }

  if (gameMode == MODE_BASKETBALL) {
    if (shootPressed) {
      gameMode = MODE_MENU;
      drawMenu();
      delay(16);
      return;
    }
    basketUpdate(dt, passPressed);
    drawBasketGame();
    delay(16);
    return;
  }

  // ---- 足球游戏（原逻辑）----
  // 中场/终场：等待任意键继续
  if (matchPhase == PH_HALFTIME) {
    if (passPressed || shootPressed) startSecondHalf();
  } else if (matchPhase == PH_FULLTIME) {
    if (passPressed || shootPressed) restartMatch();
  } else if (!celebrateActive) {
    if (passPressed) makePass();
    if (shootPressed && !kickoffPending) shootBall();
  }

  updateGame(dt);
  drawGame();
  delay(16);
}
