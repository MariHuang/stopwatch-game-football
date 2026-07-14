#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>
#include "racing.h"
#include "basket_hoop_sprite.h"
#include "shot_sound.h"
#include "miss_sound.h"
#include "bgm_select.h"   // 选车场景背景音乐

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

struct RawButton {
  uint8_t pin;
  bool idleLevel;
  bool down;
  bool pressed;
  uint32_t changedAt;
};

static M5Canvas canvas(&M5.Display);
static M5Canvas hudTextMask(&M5.Display);
static bool hudTextMaskReady = false;
static bool useCanvas = false;
static constexpr int RENDER_W = 466;
static constexpr int RENDER_H = 466;
static int screenW = RENDER_W;
static int screenH = RENDER_H;
static int screenOffsetX = 0;
static int screenOffsetY = 0;

// ---- 游戏模式 ----
enum GameMode { MODE_MENU, MODE_RACING, MODE_BASKETBALL, MODE_CAR_SELECT };
static GameMode gameMode = MODE_MENU;
static constexpr int GAME_COUNT = 2;
static int menuSelected = 0;        // 菜单当前选中项 0=赛车 1=空气投篮
static int carSelectIndex = 0;     // 选车页当前轮播索引 0~8
static bool carSelectConfirmed = false;  // 是否已选中(等待二次确认开始)
static bool carSelectTouchPrev = false;  // 上一帧触摸状态(边沿触发防误触)
static int  carSelectSwipeStartX = -1;    // 滑动起点 x(-1=未按下)
static int  carSelectSwipeStartY = -1;    // 滑动起点 y
#define CAR_SWIPE_THRESHOLD  40           // 滑动判定阈值(像素)
static float carSelectOffset = 0.0f;     // 车辆图实时 x 偏移(触摸中跟随手指，松开后回弹/过渡)
static bool  carSelectDragging = false;  // 是否正在拖动(触摸中)
static float carSelectAnimFrom = 0.0f;   // 动画起点位置
static float carSelectAnimT = 1.0f;      // 动画进度 0→1(1=完成)
// 切换过渡动画：从 prevIndex 滑出，carSelectIndex 滑入
static int   carSelectPrevIndex = 0;     // 切换前的车型(动画中滑出)
static float carSelectTransT = 1.0f;     // 过渡进度 0→1(1=完成)
static int   carSelectTransDir = 0;      // 切换方向: -1=新车从右滑入(左滑切换), +1=新车从左滑入(右滑切换)
static int   carSelectLastX = 0;        // 最后触摸 x(松开时判断短按 vs 滑动)
static bool  carSelectSwipeUsed = false;   // 本次手势是否已切换过(一次手势只切一辆)
static float carSelectPos = 0.0f;        // 浮点选中位置(动画期间从 prevIndex 平滑过渡到 carSelectIndex)
static int   carNameIndex = 0;         // 车名显示的索引(动画结束后才更新)
static float carNameAlpha = 1.0f;      // 车名透明度(渐隐渐显)
static bool  carNameFading = false;    // 车名是否正在渐隐
// 9 辆车的名字(按车型顺序)
static const char* const CAR_NAMES[9] = {
  "Suzuki Swift",     // 0
  "BMW 3 Series",     // 1
  "Suzuki Vitara",    // 2
  "Jeep Wrangler",    // 3 (玩家默认)
  "Ford F-150",       // 4
  "Citroen Jumper",   // 5 (去掉变音符便于显示)
  "Porsche 718",      // 6
  "Renault Duster",   // 7
  "Fiat 500",         // 8
};

static uint32_t lastTickMs = 0;   // 上一帧时刻(loop 计算 dt 用)
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
static bool basketShotScored = false;
static bool basketScorePendingHighlight = false;   // 进球后等待网动画结束再高亮
static uint32_t basketScoreHighlightUntil = 0;     // 分数高亮结束时刻(millis)
static uint32_t basketMissShowUntil = 0;            // MISS 文字显示结束时刻(millis，与 SWISH 同时长)
static bool basketResultReady = false;
static uint32_t basketResultUntil = 0;
static uint32_t basketLastShotMs = 0;
static uint32_t basketNetAnimStart = 0;
static uint32_t basketMissHoopStart = 0;          // Miss 时篮筐抖动动画起始时刻
#define BASKET_MISS_FRAME_MS  90                   // Miss 篮筐动画每帧时长
#define BASKET_MISS_TOTAL_MS  (BASKET_MISS_FRAME_MS * 6)  // Miss 动画总时长(6 步)
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
static int basketShotPoints = 2;      // 当前投篮得分(2 或 3)，用力甩=3分球
static float basketLastAccelMag = 1.0f;
static float basketSwingMeter = 0.0f;
static bool basketImuReady = false;
static bool speakerReady = false;

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

Vec2 touchToScreen(int rawX, int rawY) {
  return {
    clampf((float)(rawX - screenOffsetX), 0.0f, (float)(screenW - 1)),
    clampf((float)(rawY - screenOffsetY), 0.0f, (float)(screenH - 1))
  };
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
  void gfxPushImageKeyed(int x, int y, int w, int h, const uint16_t* pixels, uint16_t transparent) {
    const auto* rgbPixels = reinterpret_cast<const lgfx::rgb565_t*>(pixels);
    lgfx::rgb565_t transparentRgb(transparent);
    if (useCanvas) canvas.pushImage(x, y, w, h, rgbPixels, transparentRgb);
    else M5.Display.pushImage(x, y, w, h, rgbPixels, transparentRgb);
  }
  uint16_t gfxReadPixel(int x, int y) {
    if (useCanvas) return (uint16_t)canvas.readPixel(x, y);
    return (uint16_t)M5.Display.readPixel(x, y);
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
  // 抗锯齿椭圆：drawEllipse 描边(LGFX 内置 AA)包住 fillEllipse 填充，边缘柔化
  void gfxSmoothEllipse(int x, int y, int rx, int ry, uint16_t c) {
    if (rx < 1 || ry < 1) { gfxEllipse(x, y, rx, ry, c); return; }
    if (useCanvas) {
      canvas.fillEllipse(x, y, rx, ry, c);
      canvas.drawEllipse(x, y, rx, ry, c);
    } else {
      M5.Display.fillEllipse(x, y, rx, ry, c);
      M5.Display.drawEllipse(x, y, rx, ry, c);
    }
  }
  void gfxSmoothCircle(int x, int y, int r, uint16_t c) {
    if (r < 1) return;
    if (useCanvas) canvas.fillSmoothCircle(x, y, r, c);
    else M5.Display.fillSmoothCircle(x, y, r, c);
  }
  void gfxDrawCircle(int x, int y, int r, uint16_t c) {
    if (useCanvas) canvas.drawCircle(x, y, r, c);
    else M5.Display.drawCircle(x, y, r, c);
  }
  void gfxDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (useCanvas) canvas.drawRoundRect(x, y, w, h, r, c);
    else M5.Display.drawRoundRect(x, y, w, h, r, c);
  }
  // 带 size 参数的居中文本(用 textSize 放大 Font0 实现任意字号)
  void gfxTextCenterS(const char* s, int x, int y, int size, uint16_t c, uint16_t bg) {
    if (useCanvas) {
      canvas.setFont(&fonts::Font0);
      canvas.setTextSize(size);
      canvas.setTextDatum(textdatum_t::middle_center);
      canvas.setTextColor(c, (bg == 0xFFFF) ? c : bg);
      canvas.drawString(s, x, y);
    } else {
      M5.Display.setFont(&fonts::Font0);
      M5.Display.setTextSize(size);
      M5.Display.setTextDatum(textdatum_t::middle_center);
      M5.Display.setTextColor(c, (bg == 0xFFFF) ? c : bg);
      M5.Display.drawString(s, x, y);
    }
  }
  // Font4 + size 参数：矢量字体放大，保持抗锯齿(比 Font0 缩放清晰)
  void gfxTextCenterF4(const char* s, int x, int y, int size, uint16_t c, uint16_t bg) {
    if (useCanvas) {
      canvas.setFont(&fonts::Font4);
      canvas.setTextSize(size);
      canvas.setTextDatum(textdatum_t::middle_center);
      canvas.setTextColor(c, (bg == 0xFFFF) ? c : bg);
      canvas.drawString(s, x, y);
    } else {
      M5.Display.setFont(&fonts::Font4);
      M5.Display.setTextSize(size);
      M5.Display.setTextDatum(textdatum_t::middle_center);
      M5.Display.setTextColor(c, (bg == 0xFFFF) ? c : bg);
      M5.Display.drawString(s, x, y);
    }
  }
  // 矢量字体(DejaVu24，抗锯齿清晰) + size 放大，正常大粗体
  void gfxTextCenterBold(const char* s, int x, int y, int size, uint16_t c, uint16_t bg) {
    if (useCanvas) {
      canvas.setFont(&fonts::DejaVu24);
      canvas.setTextSize(size);
      canvas.setTextDatum(textdatum_t::middle_center);
      canvas.setTextColor(c, (bg == 0xFFFF) ? c : bg);
      canvas.drawString(s, x, y);
    } else {
      M5.Display.setFont(&fonts::DejaVu24);
      M5.Display.setTextSize(size);
      M5.Display.setTextDatum(textdatum_t::middle_center);
      M5.Display.setTextColor(c, (bg == 0xFFFF) ? c : bg);
      M5.Display.drawString(s, x, y);
    }
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
  // 真正的文字 Alpha：先把字形画入灰度遮罩，再将目标颜色逐像素混合到当前画面。
  void gfxTextCenterAlpha(const char* s, int x, int y, int font, int size, uint16_t c, float alpha) {
    alpha = constrain(alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f || !s || !*s) return;
    const lgfx::IFont* textFont = gfxFontById(font);
    if (alpha >= 0.995f) {
      if (useCanvas) {
        canvas.setFont(textFont);
        canvas.setTextSize(size);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(c, c);
        canvas.drawString(s, x, y);
      } else {
        M5.Display.setFont(textFont);
        M5.Display.setTextSize(size);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.setTextColor(c, c);
        M5.Display.drawString(s, x, y);
      }
      return;
    }

    static constexpr int MASK_W = 180;
    static constexpr int MASK_H = 64;
    if (!hudTextMaskReady) {
      hudTextMask.setColorDepth(8);
      hudTextMaskReady = hudTextMask.createSprite(MASK_W, MASK_H) != nullptr;
    }
    if (!hudTextMaskReady) return;

    hudTextMask.fillScreen(0x0000);
    hudTextMask.setFont(textFont);
    hudTextMask.setTextSize(size);
    hudTextMask.setTextDatum(textdatum_t::middle_center);
    hudTextMask.setTextColor(0xFFFF, 0x0000);
    hudTextMask.drawString(s, MASK_W / 2, MASK_H / 2);

    int textW = min(MASK_W, (int)hudTextMask.textWidth(s) + 6);
    int textH = min(MASK_H, (int)hudTextMask.fontHeight() + 6);
    int mx0 = max(0, (MASK_W - textW) / 2);
    int my0 = max(0, (MASK_H - textH) / 2);
    int mx1 = min(MASK_W, mx0 + textW);
    int my1 = min(MASK_H, my0 + textH);
    int cr = (c >> 11) & 0x1F, cg = (c >> 5) & 0x3F, cb = c & 0x1F;

    for (int my = my0; my < my1; ++my) {
      int py = y + my - MASK_H / 2;
      if (py < 0 || py >= screenH) continue;
      for (int mx = mx0; mx < mx1; ++mx) {
        int px = x + mx - MASK_W / 2;
        if (px < 0 || px >= screenW) continue;
        uint16_t maskC = (uint16_t)hudTextMask.readPixel(mx, my);
        int mr = (maskC >> 11) & 0x1F, mg = (maskC >> 5) & 0x3F, mb = maskC & 0x1F;
        float coverage = ((float)mr / 31.0f + (float)mg / 63.0f + (float)mb / 31.0f) / 3.0f;
        float a = alpha * coverage;
        if (a <= 0.002f) continue;
        uint16_t bg = useCanvas ? (uint16_t)canvas.readPixel(px, py)
                                : (uint16_t)M5.Display.readPixel(px, py);
        int br = (bg >> 11) & 0x1F, bg6 = (bg >> 5) & 0x3F, bb = bg & 0x1F;
        int r = br + (int)((cr - br) * a);
        int g = bg6 + (int)((cg - bg6) * a);
        int b = bb + (int)((cb - bb) * a);
        if (useCanvas) canvas.drawPixel(px, py, (uint16_t)((r << 11) | (g << 5) | b));
        else M5.Display.drawPixel(px, py, (uint16_t)((r << 11) | (g << 5) | b));
      }
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

void basketInit() {
  M5.Power.setVibration(0);   // 保险：重置游戏时关震动，避免退出后马达继续转
  basketScore = 0;
  basketAttempts = 0;
  basketMade = 0;
  basketShotActive = false;
  basketShotMade = false;
  basketShotScored = false;
  basketScorePendingHighlight = false;
  basketScoreHighlightUntil = 0;
  basketMissShowUntil = 0;
  basketResultReady = false;
  basketResultUntil = 0;
  basketNetAnimStart = 0;
  basketMissHoopStart = 0;
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
  basketShotScored = false;
  basketMissShowUntil = 0;        // 新投篮开始：清掉残留 MISS 文字计时
  basketMissHoopStart = 0;        // 新投篮开始：清掉残留篮筐抖动动画
  basketScoreHighlightUntil = 0;  // 新投篮开始：清掉残留分数高亮

  // 力度判定：power > 1.6 为 3 分球(用力甩=远投)，否则 2 分球
  basketShotPoints = (power > 1.6f) ? 3 : 2;
  float quality = 1.0f - fabsf(power - 1.35f) * 0.42f;
  quality = clampf(quality, 0.0f, 1.0f);
  // 3 分球远投更难，命中率额外降低 12%
  int chance = 42 + (int)(quality * 48.0f);
  if (basketShotPoints == 3) chance -= 12;
  chance = max(15, chance);
  basketShotMade = (rand() % 100) < chance;

  basketStartX = screenW / 2.0f + (rand() % 17 - 8);
  basketStartY = screenH - 72.0f;
  basketEndX = screenW / 2.0f;
  basketEndY = 138.0f;
  if (!basketShotMade) {
    int miss = 34 + (rand() % 28);
    basketEndX += ((rand() % 2) ? miss : -miss);
    basketEndY += (rand() % 24) - 10;
  }
  basketCtrlX = (basketStartX + basketEndX) * 0.5f + (rand() % 41 - 20);
  // 3 分球弧线更高(模拟远投高抛物线)，2 分球弧线较低
  basketCtrlY = (basketShotPoints == 3) ? 8.0f + (rand() % 18) : 30.0f + (rand() % 24);
  basketBallX = basketStartX;
  basketBallY = basketStartY;
  basketBallR = 15.0f;
  basketBallT = 0.0f;
  basketSpin = 0.0f;
  basketShotActive = true;
}

void playShotSound() {
  if (!speakerReady) return;
  M5.Speaker.playRaw(SHOT_SOUND_DATA, SHOT_SOUND_LEN, SHOT_SOUND_SAMPLE_RATE, false, 1, 0, true);
}

void playMissSound() {
  if (!speakerReady) return;
  M5.Speaker.playRaw(MISS_SOUND_DATA, MISS_SOUND_LEN, MISS_SOUND_SAMPLE_RATE, false, 1, 0, true);
}

// ---- 选车场景背景音乐 ----
// BGM 用固定虚拟通道，音效用 channel=-1 自动分配，两者互不影响。
static constexpr int BGM_CHANNEL = 1;        // BGM 固定通道(避开音效自动分配)
static constexpr uint8_t BGM_VOLUME = 96;    // BGM 音量(低于主音量255，不盖过音效)
static bool bgmPlaying = false;

void startCarSelectBgm() {
  if (!speakerReady || bgmPlaying) return;
  M5.Speaker.setChannelVolume(BGM_CHANNEL, BGM_VOLUME);
  // repeat=0 在 M5Unified 内部转为 ~0u(无限循环)；用固定通道不抢占音效
  M5.Speaker.playRaw(BGM_SELECT_DATA, BGM_SELECT_LEN,
                     BGM_SELECT_SAMPLE_RATE, false, 0,
                     BGM_CHANNEL, false);
  bgmPlaying = true;
}

void stopCarSelectBgm() {
  if (!bgmPlaying) return;
  M5.Speaker.stop(BGM_CHANNEL);
  bgmPlaying = false;
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
        basketBallY = 138.0f + fall * 96.0f;
        if (!basketShotScored) {
          basketShotScored = true;
          basketNetAnimStart = millis();
          basketScorePendingHighlight = true;   // 网动画播完后再加分+高亮
          playShotSound();
          M5.Power.setVibration(200);            // 进球震动：强档(随网动画开始)
        }
      } else {
        basketBallY = basketEndY + fall * fall * 360.0f;
      }
    }

    if (basketBallT > (basketShotMade ? 1.32f : 1.48f)) {
      basketShotActive = false;
      basketResultReady = true;
      basketResultUntil = millis() + 900;
      if (!basketShotMade) {
        playMissSound();
        basketMissShowUntil = millis() + 3000;   // MISS 显示时长与 SWISH/高亮一致(3s)
        basketMissHoopStart = millis();          // 启动篮筐抖动动画(用 0/1/5 帧)
      }
    }
  }

  if (basketNetAnimStart != 0 && millis() - basketNetAnimStart >= (uint32_t)(BASKET_HOOP_FRAMES * BASKET_HOOP_FRAME_MS)) {
    basketNetAnimStart = 0;
    M5.Power.setVibration(0);                  // 网动画结束：关震动
    // 网动画播完：此时再加分 + 触发高亮(分数变化滞后于篮筐动效)
    if (basketScorePendingHighlight) {
      basketScorePendingHighlight = false;
      basketMade++;
      basketScore += basketShotPoints;
      if (basketScore > basketBest) basketBest = basketScore;
      basketScoreHighlightUntil = millis() + 3000;   // 渐显0.4+保持2.2+渐隐0.4
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
  int y0 = -30;
  int frame = 0;
  if (basketNetAnimStart != 0) {
    // 进球：网动画，0→N 线性遍历
    frame = min(BASKET_HOOP_FRAMES - 1, (int)((millis() - basketNetAnimStart) / BASKET_HOOP_FRAME_MS));
  } else if (basketMissHoopStart != 0) {
    // Miss：篮筐被撞抖动，用 0/1/5 三帧序列(撞→回弹→静止)
    // 6 步序列：0 → 1 → 5 → 1 → 0 → 0
    static const uint8_t missSeq[6] = {0, 1, 5, 1, 0, 0};
    int step = (int)((millis() - basketMissHoopStart) / BASKET_MISS_FRAME_MS);
    if (step >= 6) step = 5;
    frame = missSeq[step];
  }
  for (int y = 0; y < BASKET_HOOP_H; ++y) {
    int sy = y0 + y;
    if (sy < 0 || sy >= screenH) continue;
    int row = y * BASKET_HOOP_W;
    for (int x = 0; x < BASKET_HOOP_W; ++x) {
      int sx = x0 + x;
      if (sx < 0 || sx >= screenW) continue;
      int idx = row + x;
      uint16_t c = pgm_read_word(&BASKET_HOOP_PIXELS[frame][idx]);
      if (c == 0x0000) continue;
      if (useCanvas) canvas.drawPixel(sx, sy, c);
      else M5.Display.drawPixel(sx, sy, c);
    }
  }
}

void drawSegBarCore(float cx, float cy, float len, float thick, bool horiz, uint16_t color) {
  float half = len * 0.5f;
  float ht = thick * 0.5f;
  float bevel = thick * 0.56f;
  auto tri = [&](float ax, float ay, float bx, float by, float ex, float ey) {
    int iax = (int)roundf(ax);
    int iay = (int)roundf(ay);
    int ibx = (int)roundf(bx);
    int iby = (int)roundf(by);
    int iex = (int)roundf(ex);
    int iey = (int)roundf(ey);
    if (useCanvas) canvas.fillTriangle(iax, iay, ibx, iby, iex, iey, color);
    else M5.Display.fillTriangle(iax, iay, ibx, iby, iex, iey, color);
  };
  if (horiz) {
    int x = (int)roundf(cx - half + bevel);
    int y = (int)roundf(cy - thick * 0.5f);
    int w = (int)roundf(len - bevel * 2.0f);
    int h = (int)roundf(thick);
    box(x, y, w, h, color);
    tri(cx - half, cy, cx - half + bevel, cy - ht, cx - half + bevel, cy + ht);
    tri(cx + half, cy, cx + half - bevel, cy - ht, cx + half - bevel, cy + ht);
  } else {
    int x = (int)roundf(cx - thick * 0.5f);
    int y = (int)roundf(cy - half + bevel);
    int w = (int)roundf(thick);
    int h = (int)roundf(len - bevel * 2.0f);
    box(x, y, w, h, color);
    tri(cx, cy - half, cx - ht, cy - half + bevel, cx + ht, cy - half + bevel);
    tri(cx, cy + half, cx - ht, cy + half - bevel, cx + ht, cy + half - bevel);
  }
}

void drawSegBar(float cx, float cy, float len, float thick, bool horiz, bool active, uint16_t lit) {
  if (!active) return;
  uint16_t outline = C_BLACK;
  drawSegBarCore(cx - 2.0f, cy, len, thick, horiz, outline);
  drawSegBarCore(cx + 2.0f, cy, len, thick, horiz, outline);
  drawSegBarCore(cx, cy - 2.0f, len, thick, horiz, outline);
  drawSegBarCore(cx, cy + 2.0f, len, thick, horiz, outline);
  drawSegBarCore(cx - 1.4f, cy - 1.4f, len, thick, horiz, outline);
  drawSegBarCore(cx + 1.4f, cy - 1.4f, len, thick, horiz, outline);
  drawSegBarCore(cx - 1.4f, cy + 1.4f, len, thick, horiz, outline);
  drawSegBarCore(cx + 1.4f, cy + 1.4f, len, thick, horiz, outline);
  drawSegBarCore(cx, cy, len, thick, horiz, lit);
}

void drawSevenSegment(int x, int y, int w, int h, int digit, uint16_t lit) {
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
  float t = max(w * 0.105f, h * 0.064f);
  t = max(t, 12.0f);
  float gap = t * 1.55f;
  float mid = y + h * 0.5f;
  float overlap = t * 0.36f;
  float hLen = w - 2.0f * gap + overlap * 2.0f;
  float vLen = h * 0.5f - 2.0f * gap + overlap * 2.0f;
  uint8_t s = segs[digit];
  auto seg = [&](int bit, float cx, float cy, float len, bool horiz) {
    drawSegBar(cx, cy, len, t, horiz, (s & (1 << bit)), lit);
  };
  seg(0, x + w * 0.5f, y + gap,                 hLen, true);
  seg(1, x + w - gap,   y + h * 0.255f,         vLen, false);
  seg(2, x + w - gap,   y + h * 0.745f,         vLen, false);
  seg(3, x + w * 0.5f, y + h - gap,             hLen, true);
  seg(4, x + gap,       y + h * 0.745f,         vLen, false);
  seg(5, x + gap,       y + h * 0.255f,         vLen, false);
  seg(6, x + w * 0.5f, mid,                     hLen, true);
}

// 7 列 × 10 行 点阵字模(每个数字 70 位，行优先)，1=点亮
// 高分辨率粗黑体，贴近真实 LED 记分牌大屏，识别性强
static const uint16_t DOT_MATRIX[10][10] = {
  // 0
  {0b0111110, 0b1111111, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b1111111, 0b0111110},
  // 1
  {0b0001100, 0b0011100, 0b0111100, 0b0001100, 0b0001100, 0b0001100, 0b0001100, 0b0001100, 0b0001100, 0b1111111},
  // 2
  {0b0111110, 0b1111111, 0b0000011, 0b0000110, 0b0001100, 0b0011000, 0b0110000, 0b1100000, 0b1111111, 0b1111111},
  // 3
  {0b1111110, 0b1111111, 0b0000011, 0b0000110, 0b0011110, 0b0000110, 0b0000011, 0b0000011, 0b1111111, 0b1111100},
  // 4
  {0b0001110, 0b0011110, 0b0110110, 0b1100110, 0b1100110, 0b1111111, 0b1111111, 0b0000110, 0b0000110, 0b0000110},
  // 5
  {0b1111111, 0b1111111, 0b1100000, 0b1100000, 0b1111110, 0b0000011, 0b0000011, 0b0000011, 0b1111111, 0b1111100},
  // 6
  {0b0011110, 0b0111000, 0b1100000, 0b1100000, 0b1111110, 0b1100011, 0b1100011, 0b1100011, 0b1111111, 0b0111110},
  // 7
  {0b1111111, 0b1111111, 0b0000011, 0b0000110, 0b0001100, 0b0011000, 0b0110000, 0b0110000, 0b0110000, 0b0110000},
  // 8
  {0b0111110, 0b1111111, 0b1100011, 0b1100011, 0b0111110, 0b1100011, 0b1100011, 0b1100011, 0b1111111, 0b0111110},
  // 9
  {0b0111110, 0b1111111, 0b1100011, 0b1100011, 0b0111111, 0b0000011, 0b0000011, 0b0000110, 0b0011100, 0b0111100},
};

// 画一个点阵 LED 数字：7×10 大圆点，相邻点轻微重叠 + 抗锯齿，边缘圆滑无锯齿。
// pad=圆点相对格子的收缩(0=刚好相切略有重叠，>0 留缝隙显颗粒感)。
void drawDotMatrixDigit(int x, int y, int w, int h, int digit, uint16_t lit, float pad = 0.0f) {
  if (digit < 0 || digit > 9) return;
  const int COLS = 7, ROWS = 10;
  float cellW = (float)w / COLS;
  float cellH = (float)h / ROWS;
  // 圆点直径取格子较小边，再 +6% 让相邻点重叠、消除断阶；pad 收缩则变稀疏
  float r = min(cellW, cellH) * (0.53f - pad);
  for (int row_i = 0; row_i < ROWS; ++row_i) {
    uint16_t row = DOT_MATRIX[digit][row_i];
    for (int c = 0; c < COLS; ++c) {
      if (!(row & (1 << (COLS - 1 - c)))) continue;
      float cx = x + (c + 0.5f) * cellW;
      float cy = y + (row_i + 0.5f) * cellH;
      // fillSmoothCircle 自带抗锯齿，边缘柔和
      if (useCanvas) canvas.fillSmoothCircle((int)cx, (int)cy, (int)r, lit);
      else M5.Display.fillSmoothCircle((int)cx, (int)cy, (int)r, lit);
    }
  }
}

// 在两个 RGB565 颜色间线性插值。t∈[0,1]：0=c0, 1=c1。
// 先拆回 565 再线性混合，避免直接按位运算导致色阶跳变。
static inline uint16_t lerpRGB565(uint16_t c0, uint16_t c1, float t) {
  if (t <= 0.0f) return c0;
  if (t >= 1.0f) return c1;
  int r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
  int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  int r = r0 + (int)((r1 - r0) * t);
  int g = g0 + (int)((g1 - g0) * t);
  int b = b0 + (int)((b1 - b0) * t);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// 高亮过渡时长(毫秒)：渐显 + 保持 + 渐隐 = 总 3s
#define SCORE_FADE_IN_MS   400
#define SCORE_HOLD_MS     2200
#define SCORE_FADE_OUT_MS 400
#define SCORE_TOTAL_MS    (SCORE_FADE_IN_MS + SCORE_HOLD_MS + SCORE_FADE_OUT_MS)

void drawBasketScoreBehindHoop() {
  char score[6];
  snprintf(score, sizeof(score), "%02d", basketScore);   // 两位数显示: 01, 02, 03...
  int len = strlen(score);
  if (len > 3) {
    memmove(score, score + len - 3, 4);
    len = 3;
  }
  // 点阵 LED：7列×10行，外框尺寸(宽高比 ≈ 0.7)
  int digitH = 280;
  int gap = 24;
  int digitW = (len >= 3) ? 108 : 168;       // 7列，宽 = 高×0.6 左右
  int totalW = len * digitW + (len - 1) * gap;
  int x = screenW / 2 - totalW / 2;
  int y = (screenH - digitH) / 2 - 34;
  // 记分牌配色(渐变高亮)
  // 渐变高亮：暗灰 →(0.4s)→ 亮黄 →(2.2s)→ 亮黄 →(0.4s)→ 暗灰
  const uint16_t cDim = rgb565(0x1A, 0x1A, 0x1A);   // #1A1A1A 近黑深灰(常态)
  const uint16_t cHot = rgb565(255, 220, 90);       // 暖黄(高亮)
  uint16_t lit;
  if (basketScoreHighlightUntil != 0) {
    uint32_t elapsed = millis() - (basketScoreHighlightUntil - SCORE_TOTAL_MS);
    if (elapsed >= SCORE_TOTAL_MS) {
      basketScoreHighlightUntil = 0;                // 过期清零
      lit = cDim;
    } else if (elapsed < SCORE_FADE_IN_MS) {
      lit = lerpRGB565(cDim, cHot, (float)elapsed / SCORE_FADE_IN_MS);        // 渐显
    } else if (elapsed < SCORE_FADE_IN_MS + SCORE_HOLD_MS) {
      lit = cHot;                                   // 保持全亮
    } else {
      uint32_t fe = elapsed - SCORE_FADE_IN_MS - SCORE_HOLD_MS;
      lit = lerpRGB565(cHot, cDim, (float)fe / SCORE_FADE_OUT_MS);            // 渐隐
    }
  } else {
    lit = cDim;
  }
  for (int i = 0; i < len; ++i) {
    drawDotMatrixDigit(x + i * (digitW + gap), y, digitW, digitH, score[i] - '0', lit);
  }
}

void drawBasketGame() {
  uint16_t bg = C_BLACK;
  fillScreen(bg);

  drawBasketScoreBehindHoop();
  drawBasketHoop();

  textLeft("AIR SHOT", 12, 10, &fonts::Font4, C_WHITE, bg);
  char buf[48];
  snprintf(buf, sizeof(buf), "%d/%d", basketMade, basketAttempts);
  textCenter("MADE", screenW / 2, 8, &fonts::Font0, C_DIM, bg);
  textCenter(buf, screenW / 2, 31, &fonts::Font4, C_WHITE, bg);
  textLeft("B=MENU", screenW - 74, 12, &fonts::Font0, C_WHITE, bg);

  int statusY = screenH - 38;
  // 进球(SWISH)显示时长跟随分数高亮：resultReady 期间 + 高亮 3s 期间都显示；
  // 未进(MISS)显示时长与 SWISH 一致(独立计时，不影响用户继续投篮)。
  bool highlight = basketScoreHighlightUntil != 0 && millis() < basketScoreHighlightUntil;
  bool missShow = basketMissShowUntil != 0 && millis() < basketMissShowUntil;
  if (!highlight && basketScoreHighlightUntil != 0) basketScoreHighlightUntil = 0;   // 过期清零
  if (!missShow && basketMissShowUntil != 0) basketMissShowUntil = 0;                // 过期清零
  bool swish = basketShotMade && (basketResultReady || highlight);
  bool miss = !basketShotMade && (basketResultReady || missShow);
  if (swish) {
    if (basketShotPoints == 3) {
      textCenter("3PT SWISH!", screenW / 2, statusY, &fonts::Font4, rgb565(255, 215, 0), bg);
    } else {
      textCenter("SWISH!", screenW / 2, statusY, &fonts::Font4, rgb565(242, 174, 38), bg);
    }
  } else if (miss) {
    textCenter("MISS", screenW / 2, statusY, &fonts::Font4, rgb565(150, 160, 170), bg);
  } else if (!basketShotActive) {
    const char* hint = basketImuReady ? "Swing to shoot" : "Tap / A to shoot";
    textCenter(hint, screenW / 2, statusY, &fonts::Font4, rgb565(51, 51, 51), bg);  // 20% 白(与黑底混合)
  }

  pushFrame();
}

void drawBoot() {
  fillScreen(C_PITCH_1);
  textCenter("STOPWATCH GAMES", screenW / 2, screenH / 2 - 26, &fonts::Font4, C_WHITE, C_PITCH_1);
  textCenter("Touch field to move", screenW / 2, screenH / 2 + 8, &fonts::Font2, C_WHITE, C_PITCH_1);
  textCenter("Closest player auto selected", screenW / 2, screenH / 2 + 34, &fonts::Font2, C_WHITE, C_PITCH_1);
  textCenter("A=PASS  B=SHOOT", screenW / 2, screenH / 2 + 60, &fonts::Font2, C_ACCENT, C_PITCH_1);
  pushFrame();
}

// ---- 菜单绘制 ----
// 选车页：单辆轮播，中央大图
void drawCarSelect() {
  fillScreen(rgb565(20, 24, 32));
  // 标题
  textCenter("M RACING", screenW / 2, 70, &fonts::Font4, rgb565(0x3C, 0x3E, 0x46), rgb565(20, 24, 32));
  // 画廊渲染：用浮点选中位置 carSelectPos 驱动所有车的位置和大小。
  // 每辆车的屏幕 x = cx + (车索引 - carSelectPos) × 间距
  // 大小按距中央的距离衰减：中央 160，每离一格减到 75%
  int previewSize = 160;
  int spacing = screenW * 45 / 100;   // 相邻车中心的间距
  int cx = screenW / 2;
  int cy = screenH / 2 + 10;
  float pos = carSelectPos;   // 当前浮点位置(如 3.0 = 车3在中央)

  // 先画车名(在底层)，再画车(覆盖在文字上)，让车辆遮挡文字底部
  const char* carName = (carNameIndex >= 0 && carNameIndex <= 8) ? CAR_NAMES[carNameIndex] : "?";
  // 车名真正透明：alpha 向背景色(深灰 #14181C)混合，alpha=0 完全融入背景不可见
  // 背景色 rgb565(20,24,28)，文字色 rgb565(153,153,153)
  if (carNameAlpha < 0.02f) {
    // alpha≈0：完全不画(真正透明，不可见)
  } else {
    int bgR = 20, bgG = 24, bgB = 28;     // 背景色
    int fgR = 153, fgG = 153, fgB = 153;  // 文字色(60%白)
    int r = bgR + (int)((fgR - bgR) * carNameAlpha);
    int g = bgG + (int)((fgG - bgG) * carNameAlpha);
    int b = bgB + (int)((fgB - bgB) * carNameAlpha);
    gfxTextCenterBold(carName, cx, cy - previewSize / 2 + 20, 2, rgb565(r, g, b), rgb565(20, 24, 32));
  }

  // 统一只画 3 辆，动画中用 fast 模式(最近邻，无 pushImage)
  bool isAnimating = (carSelectAnimT < 1.0f) || carSelectDragging;
  for (int i = -1; i <= 1; ++i) {
    int carIdx = (int)roundf(pos) + i;
    if (carIdx < 0 || carIdx > 8) continue;
    float relPos = (float)carIdx - pos;
    int x = cx + (int)(relPos * spacing);
    float dist = fabsf(relPos);
    int size = (int)(previewSize * (1.0f - 0.25f * dist));
    if (size < 50) continue;
    drawCarPreview(carIdx, x, cy, size, isAnimating);
  }
  // GO 按钮(车型下方)：大圆角描边 2px，黄色文字居中，上移 20px
  int goW = 160, goH = 60;   // 宽度 180→160 (两边各缩 10)
  int goX = cx - goW / 2;
  int goY = cy + previewSize / 2 + 30;   // 原 50 → 30，上移 20px
  // 全圆角(胶囊形)描边：外框黄 + 内框背景色，形成 2px 圆角描边
  uint16_t bgC = rgb565(20, 24, 32);
  roundBox(goX, goY, goW, goH, 100, C_YELLOW);          // 外框(黄，圆角自动钳制到 goH/2)
  roundBox(goX + 2, goY + 2, goW - 4, goH - 4, 100, bgC); // 内框(背景色，留 2px 黄边)
  textCenter("GO", cx, goY + goH / 2 + 2, &fonts::Font4, C_YELLOW, bgC);
  pushFrame();
}

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
    const char* name = (i == 0) ? "RACING" : "AIR SHOT";
    textCenter(name, cx + cardW / 2, cardY + cardH / 2 + 14, &fonts::Font4,   // 标题下移 20px(原 -6)
               sel ? C_WHITE : C_DIM, bg);
    // 简易图标提示
    if (i == 0) {
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
  cfg.internal_spk = true;
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

  auto spkCfg = M5.Speaker.config();
  spkCfg.magnification = 24;
  M5.Speaker.config(spkCfg);
  speakerReady = M5.Speaker.begin();
  if (speakerReady) M5.Speaker.setVolume(255);
  canvas.setColorDepth(16);
  useCanvas = canvas.createSprite(screenW, screenH) != nullptr;
  if (!useCanvas) M5.Display.fillScreen(C_BLACK);



  initRawButtons();
  drawBoot();
  lastTickMs = millis();
  gameMode = MODE_MENU;   // 开机进菜单选择游戏

  Serial.printf("[Boot] display=%dx%d canvas=%s touch=%s speaker=%s keyA_idle=%d keyB_idle=%d\n",
                screenW, screenH, useCanvas ? "ok" : "off",
                M5.Touch.isEnabled() ? "ok" : "off",
                speakerReady ? "ok" : "off",
                rawA.idleLevel, rawB.idleLevel);
}

void loop() {
  M5.update();
  updateRawButton(rawA);
  updateRawButton(rawB);

  bool passPressed = rawA.pressed || M5.BtnA.wasPressed();
  bool shootPressed = rawB.pressed || M5.BtnB.wasPressed();
  // A+B 同时按住 → 返回菜单(任何游戏状态下)
  if (gameMode != MODE_MENU && M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    gameMode = MODE_MENU;
    passPressed = false;
    shootPressed = false;
  }

  uint32_t now = millis();
  float dt = (now - lastTickMs) / 1000.0f;
  lastTickMs = now;
  dt = clampf(dt, 0.0f, 0.040f);

  // 选车场景背景音乐：进入播放、离开停止(集中守卫，覆盖所有切换路径)
  static GameMode prevGameMode = MODE_MENU;
  if (prevGameMode != MODE_CAR_SELECT && gameMode == MODE_CAR_SELECT) startCarSelectBgm();
  else if (prevGameMode == MODE_CAR_SELECT && gameMode != MODE_CAR_SELECT) stopCarSelectBgm();
  // 无论通过 A+B、Game Over 返回还是其它路径离开赛车，都统一停止循环车声并清理震动。
  if (prevGameMode == MODE_RACING && gameMode != MODE_RACING) racingRequestExit();
  prevGameMode = gameMode;

  if (gameMode == MODE_MENU) {
    // 菜单：触摸点选(点哪个卡片进哪个游戏)，A 确认当前选中
    bool touchTapped = false;
    int tapIdx = -1;
    if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
      auto& pt = M5.Touch.getTouchPointRaw(0);
      Vec2 tp = touchToScreen(pt.x, pt.y);
      // 触摸滑动选择(跟随 x 位置)
      menuSelected = clampf(tp.x / (screenW / (float)GAME_COUNT), 0.0f, (float)(GAME_COUNT - 1));
      // 判断触摸点是否落在某个卡片范围内 → 点击进入
      const int cardW = 134, cardH = 186, gap = 14;
      int totalW = cardW * GAME_COUNT + gap * (GAME_COUNT - 1);
      int startX = (screenW - totalW) / 2;
      int cardY = 142;
      for (int i = 0; i < GAME_COUNT; ++i) {
        int cx = startX + i * (cardW + gap);
        if (tp.x >= cx && tp.x < cx + cardW && tp.y >= cardY && tp.y < cardY + cardH) {
          tapIdx = i;
          menuSelected = i;   // 同时选中该卡片
          break;
        }
      }
    }
    bool enterGame = passPressed;   // A 键进入
    if (shootPressed) menuSelected = (menuSelected + 1) % GAME_COUNT;  // B 切换
    // 触摸点击卡片也进入(用 tapIdx 触发，需配合点击判定)
    // 简化：触摸点在卡片内时直接进入，避免复杂 tap 检测
    if (tapIdx >= 0) enterGame = true;
    if (enterGame) {
      int sel = (int)menuSelected;
      if (sel == 0) {
        // 进赛车：先选车
        carSelectIndex = 0;
        carSelectConfirmed = false;
        carSelectTouchPrev = true;   // 标记当前正在触摸，避免立刻触发
        carSelectSwipeStartX = -1;   // 无效起点，必须真正按下(touchJustPressed)才生效
        carSelectSwipeStartY = -1;
        carSelectLastX = -1;
        carSelectSwipeUsed = false;
        carSelectPos = 0.0f;   // 从车 0 开始
        carNameIndex = 0;
        gameMode = MODE_CAR_SELECT;
      } else if (sel == 1) {
        // 进空气投篮
        basketInit();
        gameMode = MODE_BASKETBALL;
      }
    }
    drawMenu();
    delay(16);
    return;
  }

  if (gameMode == MODE_CAR_SELECT) {
    // 选车页：跟手滑动 + 松手吸附，点击 GO/A 进入游戏
    bool touchActive = (M5.Touch.isEnabled() && M5.Touch.getCount() > 0);
    bool touchJustPressed = touchActive && !carSelectTouchPrev;
    bool touchJustReleased = !touchActive && carSelectTouchPrev;
    carSelectTouchPrev = touchActive;

    // 按下：记录起点
    if (touchJustPressed) {
      auto& pt = M5.Touch.getTouchPointRaw(0);
      Vec2 tp = touchToScreen(pt.x, pt.y);
      carSelectSwipeStartX = tp.x;
      carSelectSwipeStartY = tp.y;
      carSelectLastX = tp.x;
      carSelectDragging = true;
      carSelectSwipeUsed = false;   // 重置：本次手势还没切换
    }

    // 拖动中：超过阈值立即切换(不等松手)，一次手势只切一辆
    if (touchActive && carSelectDragging) {
      auto& pt = M5.Touch.getTouchPointRaw(0);
      Vec2 tp = touchToScreen(pt.x, pt.y);
      carSelectLastX = tp.x;
      if (!carSelectSwipeUsed) {
        int dx = tp.x - carSelectSwipeStartX;
        if (dx > 30 && carSelectIndex > 0) {
          // 右滑→上一辆，立即启动动画
          carSelectAnimFrom = carSelectPos;
          carSelectAnimT = 0.0f;
          carNameFading = true;   // 启动车名渐隐
          carSelectIndex--;
          carSelectSwipeUsed = true;   // 锁定，本次手势不再切
        } else if (dx < -30 && carSelectIndex < 8) {
          // 左滑→下一辆，立即启动动画
          carSelectAnimFrom = carSelectPos;
          carSelectAnimT = 0.0f;
          carNameFading = true;   // 启动车名渐隐
          carSelectIndex++;
          carSelectSwipeUsed = true;
        }
      }
    }

    // 松开：只判断短按(GO 按钮)
    if (touchJustReleased && carSelectDragging) {
      carSelectDragging = false;
      int totalDx = carSelectLastX - carSelectSwipeStartX;
      if (abs(totalDx) < 25) {
        // 短按：检查 GO 按钮
        int goW = 160, goH = 60;
        int goX = screenW / 2 - goW / 2;
        int goY = screenH / 2 + 10 + 160 / 2 + 30;
        if (carSelectSwipeStartX >= goX && carSelectSwipeStartX < goX + goW &&
            carSelectSwipeStartY >= goY && carSelectSwipeStartY < goY + goH) {
          racingSetPlayerCarType(carSelectIndex);
          racingInit();
          gameMode = MODE_RACING;
        }
      }
    }

    // 非拖动时：线性插值动画(匀速滑动，比指数衰减更流畅均匀)
    if (!carSelectDragging) {
      // carSelectAnimFrom 记录动画起点，carSelectAnimT 从 0→1
      if (carSelectAnimT < 1.0f) {
        carSelectAnimT += dt * 6.0f;   // 约 0.17 秒完成
        if (carSelectAnimT > 1.0f) carSelectAnimT = 1.0f;
        // ease-out 曲线(开始快结尾缓，比纯线性更自然)
        float e = 1.0f - (1.0f - carSelectAnimT) * (1.0f - carSelectAnimT);
        carSelectPos = carSelectAnimFrom + (carSelectIndex - carSelectAnimFrom) * e;
        // 车名渐隐：整个动画期间 alpha 从 1→0
        if (carNameFading) {
          carNameAlpha = max(0.0f, 1.0f - carSelectAnimT);
        }
      } else {
        carSelectPos = (float)carSelectIndex;
        carNameIndex = carSelectIndex;   // 动画结束，车名更新
        carNameFading = false;   // 停止渐隐
      }
      // 非渐隐时：alpha 渐显回 1(慢速，更明显)
      if (!carNameFading && carNameAlpha < 1.0f) {
        carNameAlpha += dt * 2.5f;
        if (carNameAlpha > 1.0f) carNameAlpha = 1.0f;
      }
    }

    carSelectTransT = 1.0f;

    // A 键：直接开始赛车
    if (passPressed) {
      racingSetPlayerCarType(carSelectIndex);
      racingInit();
      gameMode = MODE_RACING;
    }
    drawCarSelect();
    delay(16);
    return;
  }

  if (gameMode == MODE_RACING) {
    // 赛车：触摸转向，KeyA 降低音量，KeyB 增加音量；Game Over 后 A 重开，B 返回菜单
    constexpr int RACING_VOLUME_STEP = 26;  // 约 10% 音量
    constexpr uint32_t RACING_VOLUME_REPEAT_DELAY_MS = 360;
    constexpr uint32_t RACING_VOLUME_REPEAT_MS = 130;
    static uint32_t nextVolumeRepeatA = 0;
    static uint32_t nextVolumeRepeatB = 0;
    auto volumeRepeat = [&](bool pressed, bool down, uint32_t& nextRepeatAt) {
      if (!down) {
        nextRepeatAt = 0;
        return pressed;
      }
      if (pressed) {
        nextRepeatAt = now + RACING_VOLUME_REPEAT_DELAY_MS;
        return true;
      }
      if (nextRepeatAt != 0 && (int32_t)(now - nextRepeatAt) >= 0) {
        nextRepeatAt = now + RACING_VOLUME_REPEAT_MS;
        return true;
      }
      return false;
    };
    float steerX = 0.0f;
    if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
      auto& pt = M5.Touch.getTouchPointRaw(0);
      Vec2 tp = touchToScreen(pt.x, pt.y);
      steerX = clampf((tp.x - screenW / 2.0f) / (screenW / 2.0f), -1.0f, 1.0f);
    }
    bool passDown = rawA.down || M5.BtnA.isPressed();
    bool shootDown = rawB.down || M5.BtnB.isPressed();
    if (racingIsGameOver()) {
      nextVolumeRepeatA = 0;
      nextVolumeRepeatB = 0;
      if (passPressed) racingRestartFromExternal();
      else if (shootPressed) { gameMode = MODE_MENU; }
    } else if (racingIsIntroActive()) {
      if (volumeRepeat(passPressed, passDown, nextVolumeRepeatA)) racingAdjustVolume(-RACING_VOLUME_STEP);
      if (volumeRepeat(shootPressed, shootDown, nextVolumeRepeatB)) racingAdjustVolume(RACING_VOLUME_STEP);
      // 入场动画期间：只更新不处理输入，避免从选车页带入手势误触
      racingUpdate(dt);
    } else {
      if (volumeRepeat(passPressed, passDown, nextVolumeRepeatA)) racingAdjustVolume(-RACING_VOLUME_STEP);
      if (volumeRepeat(shootPressed, shootDown, nextVolumeRepeatB)) racingAdjustVolume(RACING_VOLUME_STEP);
      racingHandleInput(false, false, steerX);
      racingUpdate(dt);
    }
    racingDraw();
    uint32_t frameElapsed = millis() - now;
    delay(frameElapsed < 16 ? 16 - frameElapsed : 0);
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
  delay(16);
}
