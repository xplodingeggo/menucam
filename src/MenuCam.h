#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"

#include <filesystem>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// A home-screen car as the game placed it, and what the live preview last
// wrote to it. Keyed by actor address; only read or written inside that
// actor's own hook, so a stale entry is never dereferenced.
struct CarSpot {
    Vector base;
    Rotator baseRotation;
    Vector written;
    Rotator writtenRotation;
    bool moved = false;
    int lastSeen = 0;
};

// What the menu camera is showing right now.
struct CamReading {
    std::string focus;
    std::string state;
    Vector location;
    Rotator rotation;
    float fov = 0;
};

class MenuCam : public BakkesMod::Plugin::BakkesModPlugin,
                public BakkesMod::Plugin::PluginSettingsWindow {
public:
    void onLoad() override;
    void onUnload() override;

    void RenderSettings() override;
    std::string GetPluginName() override;
    void SetImGuiContext(uintptr_t ctx) override;

private:
    void OnTick();
    void OnCameraUpdated();
    void Draw(CanvasWrapper canvas);
    void WriteDiagnostics();
    void OnCarTick(ActorWrapper car);
    void PickPivotCar();

    std::optional<CamReading> Read();
    std::string KeyOf(const CamReading& r) const;
    std::string NameOf(const CamReading& r) const;
    void Log(const CamReading& r, const std::string& note);
    void SaveName(const std::string& label);
    void LoadNames();
    void SaveNames();

    std::filesystem::path dir;

    mutable std::mutex lock;
    std::optional<CamReading> current;
    std::string lastKey;
    std::map<std::string, std::string> names;

    // The live tweak is added to the camera after the game positions it. The
    // last value written is remembered so a camera that keeps its POV between
    // frames isn't pushed further every frame.
    std::optional<POV> lastBase;
    std::optional<POV> lastWritten;
    bool shakeSet = false;

    char nameBuffer[128] = {};

    // Preview check: how often each hook fired, and whether the preview POV
    // was still in place on the next tick.
    std::vector<int> hookFires;
    std::vector<int> carHookFires;
    std::map<uintptr_t, CarSpot> cars;
    // The player's car, which the line-up turns around. Chosen as the car the
    // camera looks at most directly when the car preview is switched on.
    uintptr_t pivotCar = 0;
    std::atomic<int> carCount{0};
    std::atomic<int> previewKept{0};
    std::atomic<int> previewLost{0};
};
