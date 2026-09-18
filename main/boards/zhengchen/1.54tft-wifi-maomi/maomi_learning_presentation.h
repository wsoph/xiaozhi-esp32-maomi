#pragma once

#include "device_state.h"

namespace maomi {
// Listening state alone is insufficient: capture may wait for playback to drain.
inline const char* LearningVoiceHint(DeviceState state, bool capture_running) {
    switch (state) {
        case kDeviceStateListening:
            return capture_running ? "正在聆听，请回答" : "准备聆听，请稍候";
        case kDeviceStateSpeaking:
            return "正在朗读，请稍候";
        case kDeviceStateIdle:
            return "未在聆听，唤醒继续";
        default:
            return "学习已暂停";
    }
}
}  // namespace maomi
