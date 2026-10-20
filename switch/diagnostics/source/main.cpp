#include "probe_common.hpp"
#include <switch.h>
#include <cstdio>

using namespace gamelex;
using namespace gamelex::probe;
int main() {
    consoleInit(nullptr);
    directories();
    Json info = device_info();
    info["operation_mode_detected"] = static_cast<int>(appletGetOperationMode());
    info["application_keyboard"] = {{"status", "not_tested"}};
    std::string message;
    bool docked_test = false;
    bool dirty = true;
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        const auto down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_A) { info = device_info(); info["operation_mode_detected"] = static_cast<int>(appletGetOperationMode()); dirty = true; }
        if (down & (HidNpadButton_Left | HidNpadButton_Right)) { docked_test = !docked_test; dirty = true; }
        if (down & HidNpadButton_X) {
            info["application_keyboard"] = keyboard_probe(true);
            message = "Keyboard result recorded. Typed text was not saved."; dirty = true;
        }
        if (down & HidNpadButton_Y) {
            info["test_mode_declared_by_tester"] = docked_test ? "docked" : "handheld";
            Json backend;
            if (read_json(runtime_dir() + "/backend.json", backend).ok() && backend.is_object()) {
                if (backend.contains("metrics")) info["backend_metrics"] = backend["metrics"];
            }
            auto s = atomic_json(reports_dir() + "/device-" + unique_id() + ".json", info);
            message = s.ok() ? "Report saved to /config/gamelex/reports/" : "Report write failed.";
            dirty = true;
        }
        if (dirty) {
            consoleClear();
            std::printf("GameLex - Phase 1 device probe\n\n");
            std::printf("Firmware: %s\nAtmosphere: %s\n", field(info, "firmware", "unknown").c_str(), field(info, "atmosphere", "unknown").c_str());
            std::printf("Test mode (manual label): %s\n\n", docked_test ? "TV/docked" : "handheld");
            std::printf("A: refresh   X: keyboard test   Y: save report\nLeft/Right: change test label   +: exit\n\n");
            std::printf("Keyboard: %s\n\n%s\n", field(info["application_keyboard"], "status", "not_tested").c_str(), message.c_str());
            std::printf("This is a diagnostic app, not the word-book app.\nRun the overlay while a game is open to test capture.\n");
            dirty = false;
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 0;
}
