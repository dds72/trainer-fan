#include <Arduino.h>
#include <BLE.h>
#include <driver/gpio.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <array>
#include <mutex>
#include <optional>

#include "FanControl.h"

#ifndef TRAINER_FAN_ENABLE_RELAYS
#define TRAINER_FAN_ENABLE_RELAYS 0
#endif

#ifndef TRAINER_FAN_RELAY_ACTIVE_LOW
#define TRAINER_FAN_RELAY_ACTIVE_LOW 1
#endif

namespace {

using trainer_fan::Command;
using trainer_fan::CommandType;
using trainer_fan::FanControl;
using trainer_fan::Speed;

constexpr bool EnableRelays = TRAINER_FAN_ENABLE_RELAYS == 1;
constexpr uint32_t ActiveRelayLevel = TRAINER_FAN_RELAY_ACTIVE_LOW == 1 ? 0 : 1;
constexpr uint32_t IdleRelayLevel = 1 - ActiveRelayLevel;
constexpr uint32_t RelayReleaseIntervalMs = 250;
constexpr char DeviceName[] = "Trainer Fan";
constexpr std::array<gpio_num_t, 3> RelayPins{GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27};
const BLEUUID UartService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
const BLEUUID CommandUuid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
const BLEUUID StatusUuid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

void applySpeed(Speed speed) {
    if constexpr (EnableRelays) {
        for (size_t index = 0; index < RelayPins.size(); ++index) {
            gpio_set_level(RelayPins[index], index + 1 == static_cast<size_t>(speed) ? ActiveRelayLevel : IdleRelayLevel);
        }
    }
}

FanControl fan(applySpeed, RelayReleaseIntervalMs);
std::mutex stateMutex;
std::optional<uint16_t> controller;
QueueHandle_t commands = nullptr;
BLECharacteristic statusCharacteristic;

void formatStatus(char (&message)[20]) {
    snprintf(message, sizeof(message), "SPEED:%u TARGET:%u\n",
             static_cast<unsigned>(fan.active()), static_cast<unsigned>(fan.target()));
}

void connected(BLEServer server, const BLEConnInfo &connection) {
    {
        std::lock_guard lock(stateMutex);
        if (!controller.has_value()) {
            controller = connection.getHandle();
            return;
        }
    }
    server.disconnect(connection.getHandle());
}

void disconnected(BLEServer, const BLEConnInfo &connection, uint8_t) {
    std::lock_guard lock(stateMutex);
    if (controller == connection.getHandle()) {
        controller.reset();
        xQueueReset(commands);
        fan.request(Speed::Off, millis());
    }
}

void receiveCommand(BLECharacteristic &characteristic, const BLEConnInfo &connection) {
    size_t length = 0;
    const uint8_t *data = characteristic.getValue(&length);
    Command command = trainer_fan::parseCommand(
        data ? std::string_view(reinterpret_cast<const char *>(data), length) : std::string_view{});

    std::lock_guard lock(stateMutex);
    if (controller != connection.getHandle()) {
        return;
    }
    // Off takes priority over all buffered commands and cancels any active transition.
    if (command.type == CommandType::SetSpeed && command.speed == Speed::Off) {
        xQueueReset(commands);
        fan.request(Speed::Off, millis());
    }
    if (xQueueSend(commands, &command, 0) != pdTRUE) {
        xQueueReset(commands);
        fan.request(Speed::Off, millis());
        command = {CommandType::QueueFull};
        xQueueSend(commands, &command, 0);
    }
}

bool startBluetooth() {
    if (!BLE.begin(DeviceName)) {
        return false;
    }

    if constexpr (EnableRelays) {
        BLESecurity security = BLE.getSecurity();
        security.setIOCapability(BLEIOCapability::DisplayOnly);
        security.setAuthenticationMode(true, true, true);
        security.setStaticPassKey(100000 + esp_random() % 900000);
        security.onPassKeyDisplay([](const BLEConnInfo &, uint32_t passkey) {
            Serial.printf("Trainer Fan pairing passkey: %06lu\n", static_cast<unsigned long>(passkey));
        });
    }

    BLEServer server = BLE.createServer();
    if (!server) {
        return false;
    }
    server.onConnect(connected);
    server.onDisconnect(disconnected);
    server.advertiseOnDisconnect(true);

    BLEService service = server.createService(UartService);
    if (!service) {
        return false;
    }
    auto permission = EnableRelays ? BLEPermission::WriteAuthenticated : BLEPermission::WriteOpen;
    BLECharacteristic commandCharacteristic = service.createCharacteristic(CommandUuid, BLEProperty::Write, permission);
    statusCharacteristic = service.createCharacteristic(StatusUuid, BLEProperty::Read | BLEProperty::Notify, BLEPermission::ReadOpen);
    if (!commandCharacteristic || !statusCharacteristic) {
        return false;
    }
    commandCharacteristic.onWrite(receiveCommand);
    statusCharacteristic.onRead([](BLECharacteristic &characteristic, const BLEConnInfo &) {
        char message[20];
        {
            std::lock_guard lock(stateMutex);
            formatStatus(message);
        }
        characteristic.setValue(reinterpret_cast<const uint8_t *>(message), strlen(message));
    });

    if (!server.start()) {
        return false;
    }
    BLEAdvertising advertising = server.getAdvertising();
    advertising.addServiceUUID(UartService);
    advertising.setScanResponse(true);
    return static_cast<bool>(advertising.start());
}

}  // namespace

void setup() {
    fan.begin(millis());
    if constexpr (EnableRelays) {
        for (gpio_num_t pin : RelayPins) {
            gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        }
    }

    Serial.begin(115200);
    commands = xQueueCreate(8, sizeof(Command));
    if (commands == nullptr || !startBluetooth()) {
        Serial.println("Trainer Fan startup failed");
        return;
    }
    Serial.println(EnableRelays ? "Trainer Fan ready (relay outputs enabled)" : "Trainer Fan ready (bench mode)");
}

void loop() {
    char message[20]{};
    std::optional<uint16_t> peer;
    {
        std::lock_guard lock(stateMutex);
        peer = controller;
        if (peer.has_value()) {
            const uint32_t now = millis();
            Command command{};
            bool hasCommand = xQueueReceive(commands, &command, 0) == pdTRUE;
            if (hasCommand && command.type == CommandType::SetSpeed) {
                fan.request(command.speed, now);
            }
            bool speedChanged = fan.update(now);
            if (hasCommand && command.type == CommandType::Invalid) {
                strcpy(message, "ERR COMMAND\n");
            } else if (hasCommand && command.type == CommandType::QueueFull) {
                strcpy(message, "ERR BUSY\n");
            } else if (hasCommand || speedChanged) {
                formatStatus(message);
            }
        }
    }

    if (peer.has_value() && message[0] != '\0') {
        statusCharacteristic.notify(*peer, reinterpret_cast<const uint8_t *>(message), strlen(message));
    }
    delay(5);
}
