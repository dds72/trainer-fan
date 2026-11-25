#include "FanControl.h"

#include <cassert>
#include <vector>

using namespace trainer_fan;

namespace {

std::vector<Speed> outputs;

void recordSpeed(Speed speed) {
    if (!outputs.empty() && outputs.back() != Speed::Off) {
        assert(speed == Speed::Off);
    }
    outputs.push_back(speed);
}

void testCommandParsing() {
    for (char index = '0'; index <= '3'; ++index) {
        char input[] = "SPEED:0\r\n";
        input[6] = index;
        const auto command = parseCommand(input);
        assert(command.type == CommandType::SetSpeed);
        assert(command.speed == static_cast<Speed>(index - '0'));
    }
    assert(parseCommand("STATUS\n").type == CommandType::Status);
    assert(parseCommand("SPEED:4").type == CommandType::Invalid);
    assert(parseCommand("SPEED:1\nSPEED:2").type == CommandType::Invalid);
    assert(parseCommand(std::string_view("SPEED:1\0", 8)).type == CommandType::Invalid);
    assert(parseCommand("").type == CommandType::Invalid);
}

void testSpeedChanges() {
    FanControl fan(recordSpeed, 250);
    fan.begin(0);
    fan.request(Speed::Low, 20);
    assert(!fan.update(249));
    assert(fan.update(250));
    fan.request(Speed::Low, 300);
    assert(fan.active() == Speed::Low);

    fan.request(Speed::High, 500);
    assert(fan.active() == Speed::Off);
    assert(fan.target() == Speed::High);
    fan.request(Speed::Medium, 630);
    assert(!fan.update(749));
    assert(fan.update(750));
    assert(fan.active() == Speed::Medium);

    fan.request(Speed::Off, 800);
    fan.request(Speed::Low, 801);
    assert(!fan.update(1049));
    fan.request(Speed::Off, 1049);
    assert(!fan.update(1050));
    assert(!fan.update(1200));
    assert(fan.target() == Speed::Off);

    const std::vector<Speed> expected{Speed::Off, Speed::Low, Speed::Off, Speed::Medium, Speed::Off};
    assert(outputs == expected);
}

void testClockWrap() {
    outputs.clear();
    FanControl fan(recordSpeed, 250);
    fan.begin(UINT32_MAX - 99);
    fan.request(Speed::High, UINT32_MAX - 50);
    assert(!fan.update(149));
    assert(fan.update(150));
}

}  // namespace

int main() {
    testCommandParsing();
    testSpeedChanges();
    testClockWrap();
}
