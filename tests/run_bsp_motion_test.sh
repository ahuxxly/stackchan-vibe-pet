#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bsp_src=.pio/libdeps/stackchan-vibe-pet/StackChan-BSP/src
uitk_core="$bsp_src/utils/uitk/src/core"
mkdir -p .test-bin
clang++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -g -DSMOOTH_UI_TOOLKIT_ENABLE_DEFAULT_HAL=0 \
  -Isrc -Itests/bsp_stubs -I"$bsp_src" tests/bsp_motion_test.cpp \
  "$bsp_src/utils/motion/servo.cpp" \
  "$uitk_core/animation/animate/animate.cpp" \
  "$uitk_core/animation/animate_value/animate_value.cpp" \
  "$uitk_core/animation/generators/spring/spring.cpp" \
  "$uitk_core/animation/generators/easing/easing.cpp" \
  "$uitk_core/easing/ease.cpp" "$uitk_core/hal/hal.cpp" \
  -o .test-bin/bsp_motion_test
.test-bin/bsp_motion_test
