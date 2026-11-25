#pragma once

#include <cstdint>
#include <string_view>

namespace trainer_fan {

enum class Speed : uint8_t { Off, Low, Medium, High };
enum class CommandType : uint8_t { SetSpeed, Status, Invalid, QueueFull };

struct Command {
    CommandType type;
    Speed speed = Speed::Off;
};

Command parseCommand(std::string_view input);

class FanControl {
public:
    using ApplySpeed = void (*)(Speed);

    FanControl(ApplySpeed applySpeed, uint32_t releaseIntervalMs);
    void begin(uint32_t now);
    void request(Speed speed, uint32_t now);
    bool update(uint32_t now);
    Speed active() const { return active_; }
    Speed target() const { return target_; }

private:
    const ApplySpeed applySpeed_;
    const uint32_t releaseIntervalMs_;
    uint32_t offSince_ = 0;
    Speed active_ = Speed::Off;
    Speed target_ = Speed::Off;
};

}  // namespace trainer_fan
