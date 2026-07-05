#!/usr/bin/env bash
# 一键编译+烧录 M5StopWatch 固件 (arduino-cli)
# 用法: ./build.sh           编译并烧录
#       ./build.sh compile   仅编译
#       ./build.sh upload    仅上传(用上次编译产物)
set -euo pipefail

PORT="/dev/cu.usbmodem101"
FQBN="m5stack:esp32:m5stack_stopwatch:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
SKETCH="PixelExpression"
EXTRA_FLAGS="-mfix-esp32-psram-cache-issue"

cd "$(dirname "$0")"

# 确保 sketch 入口桩存在(arduino-cli 需要 .ino)
if [ ! -f "$SKETCH/$SKETCH.ino" ]; then
  printf '// sketch 入口桩：实际逻辑在 main.cpp\n' > "$SKETCH/$SKETCH.ino"
fi

ACTION="${1:-all}"

compile() {
  echo "=== compile ==="
  # 把构建路径同时打印出来，供 upload 复用
  arduino-cli compile --fqbn "$FQBN" --warnings none \
    --build-property "build.extra_flags=$EXTRA_FLAGS" \
    "$SKETCH"
}

build_path() {
  arduino-cli compile --fqbn "$FQBN" --warnings none \
    --build-property "build.extra_flags=$EXTRA_FLAGS" \
    --show-properties "$SKETCH" 2>/dev/null \
    | grep '^build.path=' | cut -d= -f2
}

upload() {
  local bp; bp="$(build_path)"
  echo "=== upload -> $PORT (build: $bp) ==="
  arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$bp"
}

case "$ACTION" in
  compile) compile ;;
  upload)  upload ;;
  all)     compile; upload ;;
  *) echo "用法: $0 [compile|upload|all]"; exit 1 ;;
esac
