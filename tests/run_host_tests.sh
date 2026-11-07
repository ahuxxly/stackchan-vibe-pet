#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .test-bin
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/core_test.cpp -o .test-bin/core_test
.test-bin/core_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc \
  -I.pio/libdeps/stackchan-vibe-pet/ArduinoJson/src tests/status_protocol_test.cpp -o .test-bin/status_protocol_test
.test-bin/status_protocol_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/input_matrix_test.cpp -o .test-bin/input_matrix_test
.test-bin/input_matrix_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/pet_motion_test.cpp -o .test-bin/pet_motion_test
.test-bin/pet_motion_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/pet_presentation_test.cpp -o .test-bin/pet_presentation_test
.test-bin/pet_presentation_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/pet_imu_test.cpp -o .test-bin/pet_imu_test
.test-bin/pet_imu_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/servo_warmup_test.cpp -o .test-bin/servo_warmup_test
.test-bin/servo_warmup_test
sh tests/run_bsp_motion_test.sh
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/muse_policy_test.cpp -o .test-bin/muse_policy_test
.test-bin/muse_policy_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/stubs/muse -Isrc tests/muse_renderer_test.cpp src/MuseRenderer.cpp -o .test-bin/muse_renderer_test
.test-bin/muse_renderer_test
.venv/bin/python tests/quiet_boot_test.py
.venv/bin/python tests/motion_init_test.py
.venv/bin/python tests/servo_transport_test.py
.venv/bin/python tests/servo_power_test.py
.venv/bin/python tests/motion_diagnostics_test.py
.venv/bin/python tests/power_write_test.py
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/power_probe_test.cpp -o .test-bin/power_probe_test
.test-bin/power_probe_test
.venv/bin/python tests/power_app_test.py
.venv/bin/python tests/power_diagnostics_test.py

clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc -Itests/stubs/affection \
 -I.pio/libdeps/stackchan-vibe-pet/ArduinoJson/src tests/pet_affection_test.cpp src/AffectionController.cpp -o .test-bin/pet_affection_test
.test-bin/pet_affection_test
.venv/bin/python tests/servo_feedback_test.py

clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc \
 -I.pio/libdeps/stackchan-vibe-pet/ArduinoJson/src tests/snapshot_tx_test.cpp -o .test-bin/snapshot_tx_test
.test-bin/snapshot_tx_test
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/local_audio_test.cpp -o .test-bin/local_audio_test
.test-bin/local_audio_test
.venv/bin/python tests/audio_driver_test.py
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/stubs/audio -Isrc tests/local_audio_worker_test.cpp src/LocalAudio.cpp -o .test-bin/local_audio_worker_test
.test-bin/local_audio_worker_test

clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/audio_presentation_test.cpp -o .test-bin/audio_presentation_test
.test-bin/audio_presentation_test
.venv/bin/python tests/app_integration_test.py

clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc tests/runtime_health_test.cpp -o .test-bin/runtime_health_test
.test-bin/runtime_health_test
