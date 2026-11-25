#include "FanControl.h"

namespace trainer_fan {

Command parseCommand(std::string_view input) {
    while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
        input.remove_suffix(1);
    }

    if (input == "STATUS") {
        return {CommandType::Status};
    }
    if (input.size() == 7 && input.substr(0, 6) == "SPEED:" && input[6] >= '0' && input[6] <= '3') {
        return {CommandType::SetSpeed, static_cast<Speed>(input[6] - '0')};
    }
    return {CommandType::Invalid};
}

FanControl::FanControl(ApplySpeed applySpeed, uint32_t releaseIntervalMs)
    : applySpeed_(applySpeed), releaseIntervalMs_(releaseIntervalMs) {}

void FanControl::begin(uint32_t now) {
    applySpeed_(Speed::Off);
    active_ = Speed::Off;
    target_ = Speed::Off;
    offSince_ = now;
}

void FanControl::request(Speed speed, uint32_t now) {
    if (speed == target_) {
        return;
    }

    target_ = speed;
    if (active_ != Speed::Off) {
        applySpeed_(Speed::Off);
        active_ = Speed::Off;
        offSince_ = now;
    }
}

bool FanControl::update(uint32_t now) {
    if (active_ != Speed::Off || target_ == Speed::Off || now - offSince_ < releaseIntervalMs_) {
        return false;
    }

    applySpeed_(target_);
    active_ = target_;
    return true;
}

}  // namespace trainer_fan
