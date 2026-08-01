#include "app/motion_gesture.h"

#include <cmath>

namespace {
constexpr float RADIANS_TO_DEGREES = 57.29578f;
constexpr float ANGLE_THRESHOLD_DEG = 25.0f;
constexpr float ANGLE_HYSTERESIS_DEG = 20.0f;
constexpr uint8_t HOLD_COUNT = 5U;
constexpr uint32_t SINGLE_TILT_DELAY_MS = 100U;

// MPU6050 mounting convention: +X is left-down and +Y is back-down.
constexpr float LEFT_RIGHT_AXIS_SIGN = 1.0f;
constexpr float FRONT_BACK_AXIS_SIGN = 1.0f;

float filtered_x_g = 0.0f;
float filtered_y_g = 0.0f;
float filtered_z_g = 0.0f;
bool filter_initialized = false;
bool gesture_armed = false;
bool baseline_ready = false;
float baseline_pitch = 0.0f;
float baseline_roll = 0.0f;
MotionGesture last_stable_gesture = MotionGesture::None;
uint8_t stable_count = 0U;
MotionGesture output_locked_gesture = MotionGesture::None;
MotionGesture pending_output_gesture = MotionGesture::None;
uint32_t pending_output_start_ms = 0U;

const char *gesture_name(MotionGesture gesture) {
    switch (gesture) {
        case MotionGesture::LeftDown: return "LEFT_DOWN";
        case MotionGesture::RightDown: return "RIGHT_DOWN";
        case MotionGesture::FrontDown: return "FRONT_DOWN";
        case MotionGesture::BackDown: return "BACK_DOWN";
        case MotionGesture::None:
        default: return "NONE";
    }
}

bool motion_available(const SensorSnapshot &snapshot) {
    return snapshot.motion.health.valid && !snapshot.motion.health.stale;
}

void load_acceleration(const SensorSnapshot &snapshot, float *ax, float *ay,
                       float *az) {
    if (ax == nullptr || ay == nullptr || az == nullptr) return;
    *ax = static_cast<float>(snapshot.motion.value.acceleration_mg[0]) * 0.001f;
    *ay = static_cast<float>(snapshot.motion.value.acceleration_mg[1]) * 0.001f;
    *az = static_cast<float>(snapshot.motion.value.acceleration_mg[2]) * 0.001f;
}

void update_filter(const SensorSnapshot &snapshot, bool initialize) {
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    load_acceleration(snapshot, &ax, &ay, &az);

    if (initialize || !filter_initialized) {
        filtered_x_g = ax;
        filtered_y_g = ay;
        filtered_z_g = az;
        filter_initialized = true;
        return;
    }

    // Match the STM32 implementation: run this IIR on every 10 ms scan.
    filtered_x_g = filtered_x_g * 0.7f + ax * 0.3f;
    filtered_y_g = filtered_y_g * 0.7f + ay * 0.3f;
    filtered_z_g = filtered_z_g * 0.7f + az * 0.3f;
}

void calculate_angles(float *pitch, float *roll) {
    if (pitch == nullptr || roll == nullptr) return;
    *pitch = std::atan2(
                 filtered_x_g,
                 std::sqrt(filtered_y_g * filtered_y_g +
                           filtered_z_g * filtered_z_g)) *
             RADIANS_TO_DEGREES;
    *roll = std::atan2(
                filtered_y_g,
                std::sqrt(filtered_x_g * filtered_x_g +
                          filtered_z_g * filtered_z_g)) *
            RADIANS_TO_DEGREES;
}

MotionGesture direction_from_delta(float pitch_delta, float roll_delta) {
    const float left_right = pitch_delta * LEFT_RIGHT_AXIS_SIGN;
    const float front_back = roll_delta * FRONT_BACK_AXIS_SIGN;
    const float left_right_magnitude = std::fabs(left_right);
    const float front_back_magnitude = std::fabs(front_back);

    if (left_right_magnitude < ANGLE_THRESHOLD_DEG &&
        front_back_magnitude < ANGLE_THRESHOLD_DEG) {
        return MotionGesture::None;
    }
    if (left_right_magnitude >= front_back_magnitude) {
        return left_right > 0.0f ? MotionGesture::LeftDown
                                 : MotionGesture::RightDown;
    }
    return front_back > 0.0f ? MotionGesture::BackDown
                              : MotionGesture::FrontDown;
}

bool returned_to_baseline(float pitch_delta, float roll_delta) {
    return std::fabs(pitch_delta) < ANGLE_HYSTERESIS_DEG &&
           std::fabs(roll_delta) < ANGLE_HYSTERESIS_DEG;
}

void reset_detection_state() {
    last_stable_gesture = MotionGesture::None;
    stable_count = 0U;
    output_locked_gesture = MotionGesture::None;
    pending_output_gesture = MotionGesture::None;
    pending_output_start_ms = 0U;
}

void capture_baseline(const SensorSnapshot &snapshot) {
    update_filter(snapshot, true);
    calculate_angles(&baseline_pitch, &baseline_roll);
    baseline_ready = true;
    reset_detection_state();
    Serial.printf("[MOTION] baseline pitch=%.1f roll=%.1f\n",
                  baseline_pitch, baseline_roll);
}

MotionGesture update_detection(MotionGesture current, uint32_t now) {
    if (current == last_stable_gesture) {
        if (stable_count < HOLD_COUNT) ++stable_count;
    } else {
        last_stable_gesture = current;
        stable_count = 0U;
        pending_output_gesture = MotionGesture::None;
        pending_output_start_ms = 0U;
    }

    if (output_locked_gesture != MotionGesture::None ||
        current == MotionGesture::None || stable_count < HOLD_COUNT) {
        return MotionGesture::None;
    }

    if (pending_output_gesture == MotionGesture::None) {
        pending_output_gesture = current;
        pending_output_start_ms = now;
        return MotionGesture::None;
    }
    if (pending_output_gesture != current) {
        pending_output_gesture = MotionGesture::None;
        pending_output_start_ms = now;
        return MotionGesture::None;
    }
    if (static_cast<uint32_t>(now - pending_output_start_ms) <
        SINGLE_TILT_DELAY_MS) {
        return MotionGesture::None;
    }

    output_locked_gesture = current;
    pending_output_gesture = MotionGesture::None;
    return current;
}
}

void motion_gesture_reset() {
    filtered_x_g = 0.0f;
    filtered_y_g = 0.0f;
    filtered_z_g = 0.0f;
    filter_initialized = false;
    gesture_armed = false;
    baseline_ready = false;
    baseline_pitch = 0.0f;
    baseline_roll = 0.0f;
    reset_detection_state();
}

bool motion_gesture_arm(const SensorSnapshot &snapshot) {
    motion_gesture_reset();
    gesture_armed = true;
    if (!motion_available(snapshot)) return false;
    capture_baseline(snapshot);
    return true;
}

MotionGesture motion_gesture_update(const SensorSnapshot &snapshot) {
    if (!gesture_armed || !motion_available(snapshot)) {
        return MotionGesture::None;
    }
    if (!baseline_ready) {
        capture_baseline(snapshot);
        return MotionGesture::None;
    }

    update_filter(snapshot, false);
    float pitch = 0.0f;
    float roll = 0.0f;
    calculate_angles(&pitch, &roll);

    const float pitch_delta = pitch - baseline_pitch;
    const float roll_delta = roll - baseline_roll;
    if (output_locked_gesture != MotionGesture::None &&
        returned_to_baseline(pitch_delta, roll_delta)) {
        reset_detection_state();
        return MotionGesture::None;
    }

    const MotionGesture current =
        direction_from_delta(pitch_delta, roll_delta);
    if (current != MotionGesture::None && current != last_stable_gesture) {
        Serial.printf("[MOTION] candidate=%s pitch_delta=%.1f roll_delta=%.1f\n",
                      gesture_name(current), pitch_delta, roll_delta);
    }
    return update_detection(current, millis());
}
