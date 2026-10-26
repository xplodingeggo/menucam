#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"

#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// What the menu camera is showing right now, without our own offsets.
struct CamReading {
    std::string focus;
    std::string state;
    Vector location;
    Rotator rotation;
    float fov = 0;
};

// Offsets for one camera, applied whenever that camera is on screen. Saved
// in data/menucam/cameras.txt.
struct CamOffsets {
    bool on = false;
    float x = 0;
    float y = 0;
    float z = 0;
    float pitch = 0;
    float yaw = 0;
    // Field of view to use instead of the game's; below 10 keeps the game's.
    float fov = 0;

    bool Changes() const {
        return x != 0 || y != 0 || z != 0 || pitch != 0 || yaw != 0 || fov >= 10;
    }
};

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
    void ApplyCameraOffsets();
    void ClearShake(CameraWrapper camera);
    void Draw(CanvasWrapper canvas);
    void WriteDiagnostics();
    void OnCarTick(ActorWrapper car);
    void PickPivotCar();
    void ExportForHebnix();
    void RenderCameraTab(const std::string& key, const char* note);

    std::optional<CamReading> Read();
    std::string KeyOf(const CamReading& r) const;
    std::string NameOf(const CamReading& r) const;
    void Log(const CamReading& r, const std::string& note);
    void SaveName(const std::string& label);
    void LoadNames();
    void SaveNames();
    void LoadOffsets();
    void SaveOffsets();

    std::filesystem::path dir;

    mutable std::mutex lock;
    std::optional<CamReading> current;
    std::string lastKey;
    std::map<std::string, std::string> names;

    // Per-camera offsets: "home" for the home screen orbit camera, otherwise
    // the fixed camera's object name (CameraActor_2 and so on).
    std::map<std::string, CamOffsets> offsets;
    bool offsetsDirty = false;
    // The camera on screen, as an offsets key. Empty for cameras that can't
    // be told apart (gameplay, replays).
    std::string onScreen;
    bool shakeSet = false;

    char nameBuffer[128] = {};
    // Settings window: the fixed camera picked on the Other cameras tab, and
    // a tab to bring to the front on the next frame.
    std::string otherCamera = "CameraActor_3";
    std::string selectTab;

    int frame = 0;
    std::vector<int> carHookFires;
    std::map<uintptr_t, CarSpot> cars;
    // The player's car, which the line-up turns around.
    uintptr_t pivotCar = 0;
    std::atomic<int> carCount{0};

    std::string exportStatus;
};
