#include "maomi_learning_presentation.h"

#include <cassert>
#include <string_view>

int main() {
    using maomi::LearningVoiceHint;
    using std::string_view;
    assert(string_view(LearningVoiceHint(kDeviceStateListening, true)) == "正在聆听，请回答");
    // Listening state can precede enabling capture while playback drains.
    assert(string_view(LearningVoiceHint(kDeviceStateListening, false)) == "准备聆听，请稍候");
    assert(string_view(LearningVoiceHint(kDeviceStateSpeaking, false)) == "正在朗读，请稍候");
    assert(string_view(LearningVoiceHint(kDeviceStateSpeaking, true)) == "正在朗读，请稍候");
    assert(string_view(LearningVoiceHint(kDeviceStateIdle, false)) == "未在聆听，唤醒继续");
    for (auto state : {kDeviceStateConnecting, kDeviceStateNotifying, kDeviceStateWifiConfiguring})
        assert(string_view(LearningVoiceHint(state, true)) == "学习已暂停");
}
