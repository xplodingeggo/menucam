#include "MenuCam.h"

#include "imgui/imgui.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

BAKKESMOD_PLUGIN(MenuCam, "Menu Camera Logger", "1.0", PLUGINTYPE_FREEPLAY)

namespace {

constexpr float kRotatorPerDegree = 65536.0f / 360.0f;

float Degrees(int rotator) {
    return rotator / kRotatorPerDegree;
}

bool SamePov(const POV& a, const POV& b) {
    return a.location.X == b.location.X && a.location.Y == b.location.Y &&
           a.location.Z == b.location.Z && a.rotation.Pitch == b.rotation.Pitch &&
           a.rotation.Yaw == b.rotation.Yaw && a.rotation.Roll == b.rotation.Roll &&
           a.FOV == b.FOV;
}

// Places the live preview is applied after the game sets the camera. Which of
// these fire in the menus isn't known, so all are tried and counted (see the
// Preview check panel). Hooks that never fire cost nothing.
const char* kUpdateHooks[] = {
    "Function Engine.Camera.UpdateCamera",
    "Function Engine.Camera.UpdateViewTarget",
    "Function TAGame.CameraState_CarPreview_TA.UpdatePOV",
    "Function TAGame.CameraState_TA.UpdatePOV",
    "Function ProjectX.CameraState_X.UpdatePOV",
    "Function Engine.PlayerController.GetPlayerViewPoint",
    "Function Engine.PlayerController.PlayerTick",
    "Function TAGame.PlayerController_TA.PlayerTick",
};
constexpr size_t kHookCount = sizeof(kUpdateHooks) / sizeof(kUpdateHooks[0]);

// Functions the home-screen cars run, used to move each car from inside its
// own call. Tick should fire every frame; the others are counted in case it
// doesn't.
const char* kCarHooks[] = {
    "Function TAGame.CarPreviewActor_TA.Tick",
    "Function TAGame.CarPreviewActor_TA.UpdateBoostGlow",
    "Function TAGame.CarPreviewActor_TA.AnimateBody",
};
constexpr size_t kCarHookCount = sizeof(kCarHooks) / sizeof(kCarHooks[0]);

bool SameSpot(const Vector& a, const Rotator& ar, const Vector& b, const Rotator& br) {
    return a.X == b.X && a.Y == b.Y && a.Z == b.Z && ar.Pitch == br.Pitch && ar.Yaw == br.Yaw &&
           ar.Roll == br.Roll;
}

// The fixed cameras in MENU_Main_p.upk (object name, tag, stored location).
// Read from the 2026-10 game build; a camera that moved isn't matched.
struct KnownCamera {
    const char* name;
    const char* tag;
    float x, y, z;
};
const KnownCamera kKnownCameras[] = {
    {"CameraActor_2", "MainMenuPlay", 945.4f, -1838.6f, 65.2f},
    {"CameraActor_3", "GoalExplosionRevealEnd", 862.9f, -2590.9f, 88.7f},
    {"CameraActor", "GoalExplosionGarageHigh", 909.3f, -2749.9f, 277.0f},
    {"CameraActor_5", "GoalExplosionGarageLeft", -908.8f, -2286.5f, 64.0f},
    {"CameraActor_0", "", -29.4f, -4619.2f, 305.9f},
    {"CameraActor_6", "GoalExplosionRevealStart", 920.0f, -2410.9f, 66.1f},
    {"CameraActor_7", "GoalExplosionGarage", 995.9f, -2630.5f, 47.4f},
    {"CameraActor_9", "AnthemCam", 40.8f, 2654.4f, 77.6f},
    {"CameraActor_10", "", 568.8f, -705.6f, 54.0f},
    {"CameraActor_1", "", -2342.9f, -1086.4f, 132.0f},
    {"CameraActor_8", "", 1321.5f, -1294.7f, 215.8f},
    {"CameraActor_13", "", -2347.4f, 1265.7f, 1181.1f},
    {"CameraActor_11", "", 840.8f, -1057.6f, 77.6f},
    {"CameraActor_12", "", -1320.8f, 337.6f, 77.6f},
    {"CameraActor_15", "", 552.8f, -1057.6f, 77.6f},
    {"CameraActor_4", "", 3544.8f, 1253.7f, 215.8f},
};

const KnownCamera* MatchCamera(const Vector& at) {
    for (const auto& camera : kKnownCameras) {
        if (std::abs(camera.x - at.X) < 2 && std::abs(camera.y - at.Y) < 2 &&
            std::abs(camera.z - at.Z) < 2) {
            return &camera;
        }
    }
    return nullptr;
}

// A live preview value: a slider, plus a box for typing an exact number. The
// box accepts values past the slider's range.
struct Slider {
    const char* cvar;
    const char* label;
    float min;
    float max;
    // Above 1, the slider is finer near zero so small moves stay easy on a
    // big range.
    float power = 1.0f;
};

// Distances reach the far ends of big custom maps.
constexpr float kMaxMove = 100000.0f;
constexpr float kMovePower = 4.0f;

void SliderRow(CVarWrapper cvar, const Slider& slider) {
    float value = cvar.getFloatValue();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(slider.cvar);
    ImGui::SetNextItemWidth(width * 0.55f);
    bool changed = ImGui::SliderFloat("##slider", &value, slider.min, slider.max, "%.1f", slider.power);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width * 0.2f);
    changed |= ImGui::InputFloat("##box", &value, 0.0f, 0.0f, "%.1f");
    ImGui::SameLine();
    ImGui::TextUnformatted(slider.label);
    ImGui::PopID();
    if (changed) {
        cvar.setValue(value);
    }
}

std::string Timestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &now);
    std::ostringstream out;
    out << std::put_time(&tm, "%H:%M:%S");
    return out.str();
}

} // namespace

void MenuCam::onLoad() {
    dir = gameWrapper->GetDataFolder() / "menucam";
    std::filesystem::create_directories(dir);
    LoadNames();

    cvarManager->registerCvar("menucam_overlay", "1", "Show the current menu camera on screen", true, true, 0, true, 1);
    cvarManager->registerCvar("menucam_live", "0", "Apply the offsets below to the camera live", true, true, 0, true, 1, false);
    for (const char* axis : {"menucam_x", "menucam_y", "menucam_z"}) {
        cvarManager->registerCvar(axis, "0", "Camera offset (Unreal units)", true, false, 0, false, 0, false);
    }
    cvarManager->registerCvar("menucam_pitch", "0", "Tilt offset (degrees)", true, true, -89, true, 89, false);
    cvarManager->registerCvar("menucam_yaw", "0", "Turn offset (degrees)", true, true, -180, true, 180, false);
    cvarManager->registerCvar("menucam_method", "0", "Live preview method: 0 shake offsets, 1 overwrite view", true, true, 0, true, 1, false);
    cvarManager->registerCvar("menucam_car_live", "0", "Move the home screen cars live", true, true, 0, true, 1, false);
    for (const char* axis : {"menucam_car_x", "menucam_car_y", "menucam_car_z"}) {
        cvarManager->registerCvar(axis, "0", "Car offset (Unreal units)", true, false, 0, false, 0, false);
    }
    cvarManager->registerCvar("menucam_car_yaw", "0", "Car turn (degrees)", true, true, -180, true, 180, false);
    cvarManager->registerCvar("menucam_fov", "0", "Field of view override, 0 keeps the game's", true, true, 0, true, 170, false);

    cvarManager->registerNotifier("menucam_mark", [this](std::vector<std::string> args) {
        std::string note;
        for (size_t i = 1; i < args.size(); ++i) {
            note += (i > 1 ? " " : "") + args[i];
        }
        auto r = Read();
        if (r) {
            Log(*r, note.empty() ? "mark" : note);
        } else {
            cvarManager->log("menucam: no camera right now");
        }
    }, "Log the current camera, optionally with a note: menucam_mark garage", PERMISSION_ALL);

    cvarManager->registerNotifier("menucam_name", [this](std::vector<std::string> args) {
        std::string label;
        for (size_t i = 1; i < args.size(); ++i) {
            label += (i > 1 ? " " : "") + args[i];
        }
        SaveName(label);
    }, "Give the current camera a name: menucam_name Home screen", PERMISSION_ALL);

    hookFires.assign(kHookCount + 1, 0);
    gameWrapper->HookEvent("Function Engine.GameViewportClient.Tick", [this](std::string) {
        ++hookFires[kHookCount];
        OnTick();
        OnCameraUpdated();
        PickPivotCar();
        if (hookFires[kHookCount] % 120 == 0) {
            WriteDiagnostics();
        }
    });
    for (size_t i = 0; i < kHookCount; ++i) {
        gameWrapper->HookEventPost(kUpdateHooks[i], [this, i](std::string) {
            ++hookFires[i];
            OnCameraUpdated();
        });
    }
    carHookFires.assign(kCarHookCount, 0);
    for (size_t i = 0; i < kCarHookCount; ++i) {
        gameWrapper->HookEventWithCallerPost<ActorWrapper>(kCarHooks[i], [this, i](ActorWrapper car, void*, std::string) {
            ++carHookFires[i];
            OnCarTick(car);
        });
    }
    cvarManager->registerNotifier("menucam_hooks", [this](std::vector<std::string>) {
        for (size_t i = 0; i < kHookCount; ++i) {
            cvarManager->log("menucam: " + std::to_string(hookFires[i]) + "  " + kUpdateHooks[i]);
        }
        cvarManager->log("menucam: " + std::to_string(hookFires[kHookCount]) + "  viewport tick");
        cvarManager->log("menucam: preview kept " + std::to_string(previewKept) + ", overwritten " +
                         std::to_string(previewLost));
    }, "Show which camera hooks fire and whether the preview sticks", PERMISSION_ALL);
    gameWrapper->RegisterDrawable([this](CanvasWrapper canvas) { Draw(canvas); });

    cvarManager->log("menucam: loaded, writing to " + dir.string());
}

void MenuCam::onUnload() {
    for (const char* hook : kUpdateHooks) {
        gameWrapper->UnhookEventPost(hook);
    }
    for (const char* hook : kCarHooks) {
        gameWrapper->UnhookEventPost(hook);
    }
    gameWrapper->UnhookEvent("Function Engine.GameViewportClient.Tick");
}

std::optional<CamReading> MenuCam::Read() {
    CameraWrapper camera = gameWrapper->GetCamera();
    if (camera.IsNull()) {
        return std::nullopt;
    }
    CamReading r;
    r.focus = camera.GetFocusActor();
    r.state = camera.GetCameraState();
    POV pov = camera.GetPOV();
    // Report what the game chose, not our own live tweak.
    if (lastWritten && lastBase && SamePov(pov, *lastWritten)) {
        pov = *lastBase;
    }
    r.location = pov.location;
    r.rotation = pov.rotation;
    r.fov = pov.FOV;
    return r;
}

std::string MenuCam::KeyOf(const CamReading& r) const {
    if (!r.focus.empty() && r.focus != "None") {
        return r.focus;
    }
    // Fixed cameras stand still; anything else (the car orbit on the home
    // screen) moves every frame, so it is told apart by its state only.
    if (r.state.find("MenuCamActor") == std::string::npos) {
        return r.state;
    }
    if (const KnownCamera* camera = MatchCamera(r.location)) {
        return camera->name;
    }
    std::ostringstream key;
    key << "at:" << std::lround(r.location.X) << "," << std::lround(r.location.Y) << ","
        << std::lround(r.location.Z);
    return key.str();
}

std::string MenuCam::NameOf(const CamReading& r) const {
    auto it = names.find(KeyOf(r));
    return it == names.end() ? std::string() : it->second;
}

void MenuCam::OnTick() {
    if (lastWritten) {
        CameraWrapper camera = gameWrapper->GetCamera();
        if (!camera.IsNull()) {
            if (SamePov(camera.GetPOV(), *lastWritten)) {
                ++previewKept;
            } else {
                ++previewLost;
            }
        }
    }
    auto r = Read();
    std::string key = r ? KeyOf(*r) + "|" + r->state : std::string();
    bool changed = false;
    {
        std::lock_guard guard(lock);
        current = r;
        if (key != lastKey) {
            lastKey = key;
            changed = true;
        }
    }
    if (changed && r) {
        Log(*r, "switched");
    }
}

void MenuCam::OnCameraUpdated() {
    CameraWrapper camera = gameWrapper->GetCamera();
    if (camera.IsNull()) {
        return;
    }
    bool live = cvarManager->getCvar("menucam_live").getBoolValue();
    int method = cvarManager->getCvar("menucam_method").getIntValue();
    if (!live) {
        if (shakeSet) {
            camera.SetShakeLocationOffset(Vector{0, 0, 0});
            camera.SetShakeRotationOffset(Rotator{0, 0, 0});
            camera.SetShakeFOVOffset(0);
            shakeSet = false;
        }
        lastBase.reset();
        lastWritten.reset();
        return;
    }
    Vector move{cvarManager->getCvar("menucam_x").getFloatValue(),
                cvarManager->getCvar("menucam_y").getFloatValue(),
                cvarManager->getCvar("menucam_z").getFloatValue()};
    Rotator turn{
        static_cast<int>(std::lround(cvarManager->getCvar("menucam_pitch").getFloatValue() * kRotatorPerDegree)),
        static_cast<int>(std::lround(cvarManager->getCvar("menucam_yaw").getFloatValue() * kRotatorPerDegree)),
        0};
    float fov = cvarManager->getCvar("menucam_fov").getFloatValue();

    // Method 0: the camera's own shake offsets, which the game adds to the
    // final view every frame. Method 1: overwrite the view after the game
    // computed it.
    if (method == 0) {
        camera.SetbDisableCameraShake(0);
        camera.SetShakeLocationOffset(move);
        camera.SetShakeRotationOffset(turn);
        float base = camera.GetPOV().FOV - camera.GetShakeFOVOffset();
        camera.SetShakeFOVOffset(fov >= 10 ? fov - base : 0);
        shakeSet = true;
        return;
    }
    if (shakeSet) {
        camera.SetShakeLocationOffset(Vector{0, 0, 0});
        camera.SetShakeRotationOffset(Rotator{0, 0, 0});
        camera.SetShakeFOVOffset(0);
        shakeSet = false;
    }
    POV pov = camera.GetPOV();
    POV base = (lastWritten && lastBase && SamePov(pov, *lastWritten)) ? *lastBase : pov;
    POV out = base;
    out.location = out.location + move;
    out.rotation = out.rotation + turn;
    if (fov >= 10) {
        out.FOV = fov;
    }
    camera.SetPOV(out);
    lastBase = base;
    lastWritten = camera.GetPOV();
}

void MenuCam::OnCarTick(ActorWrapper car) {
    if (car.IsNull()) {
        return;
    }
    CarSpot& spot = cars[car.memory_address];
    spot.lastSeen = hookFires[kHookCount];
    Vector at = car.GetLocation();
    Rotator rotation = car.GetRotation();
    // Anything other than our last write is the game placing the car.
    if (!spot.moved || !SameSpot(at, rotation, spot.written, spot.writtenRotation)) {
        spot.base = at;
        spot.baseRotation = rotation;
        spot.moved = false;
    }
    if (!cvarManager->getCvar("menucam_car_live").getBoolValue()) {
        if (spot.moved) {
            car.SetLocation(spot.base);
            car.SetRotation(spot.baseRotation);
            spot.moved = false;
        }
        return;
    }
    auto pivot = cars.find(pivotCar);
    if (pivot == cars.end()) {
        return;
    }
    // Same as Hebnix's Car position: turn the line-up around the player's car
    // (UE yaw turns X towards Y), then move it.
    float yaw = cvarManager->getCvar("menucam_car_yaw").getFloatValue();
    float radians = yaw * 3.14159265f / 180.0f;
    float sin = std::sin(radians);
    float cos = std::cos(radians);
    const Vector& center = pivot->second.base;
    float dx = spot.base.X - center.X;
    float dy = spot.base.Y - center.Y;
    Vector moved{center.X + dx * cos - dy * sin + cvarManager->getCvar("menucam_car_x").getFloatValue(),
                 center.Y + dx * sin + dy * cos + cvarManager->getCvar("menucam_car_y").getFloatValue(),
                 spot.base.Z + cvarManager->getCvar("menucam_car_z").getFloatValue()};
    Rotator turned = spot.baseRotation;
    turned.Yaw += static_cast<int>(std::lround(yaw * kRotatorPerDegree));
    car.SetLocation(moved);
    car.SetRotation(turned);
    spot.written = car.GetLocation();
    spot.writtenRotation = car.GetRotation();
    spot.moved = true;
}

void MenuCam::PickPivotCar() {
    bool live = cvarManager->getCvar("menucam_car_live").getBoolValue();
    int frame = hookFires[kHookCount];
    // Forget cars that stopped running (left the menu, or a party member left).
    for (auto it = cars.begin(); it != cars.end();) {
        if (frame - it->second.lastSeen > 300) {
            it = cars.erase(it);
        } else {
            ++it;
        }
    }
    carCount = static_cast<int>(cars.size());
    if (!live) {
        pivotCar = 0;
        return;
    }
    if (pivotCar && cars.count(pivotCar)) {
        return;
    }
    CameraWrapper camera = gameWrapper->GetCamera();
    if (camera.IsNull() || cars.empty()) {
        return;
    }
    // The home camera orbits the player's car and looks at it, so pick the
    // car closest to the line of sight.
    POV pov = camera.GetPOV();
    if (lastWritten && lastBase && SamePov(pov, *lastWritten)) {
        pov = *lastBase;
    }
    float pitch = pov.rotation.Pitch / kRotatorPerDegree * 3.14159265f / 180.0f;
    float turn = pov.rotation.Yaw / kRotatorPerDegree * 3.14159265f / 180.0f;
    Vector look{std::cos(pitch) * std::cos(turn), std::cos(pitch) * std::sin(turn), std::sin(pitch)};
    float best = 0;
    for (const auto& [address, spot] : cars) {
        Vector to{spot.base.X - pov.location.X, spot.base.Y - pov.location.Y, spot.base.Z - pov.location.Z};
        float along = to.X * look.X + to.Y * look.Y + to.Z * look.Z;
        float length2 = to.X * to.X + to.Y * to.Y + to.Z * to.Z;
        float off = length2 - along * along;
        if (along > 0 && (pivotCar == 0 || off < best)) {
            pivotCar = address;
            best = off;
        }
    }
}

void MenuCam::WriteDiagnostics() {
    CameraWrapper camera = gameWrapper->GetCamera();
    std::ofstream out(dir / "preview-check.txt", std::ios::trunc);
    out << std::fixed << std::setprecision(1) << Timestamp() << "\n";
    out << "live=" << cvarManager->getCvar("menucam_live").getBoolValue()
        << " method=" << cvarManager->getCvar("menucam_method").getIntValue()
        << " move=" << cvarManager->getCvar("menucam_x").getFloatValue() << ","
        << cvarManager->getCvar("menucam_y").getFloatValue() << ","
        << cvarManager->getCvar("menucam_z").getFloatValue() << "\n";
    for (size_t i = 0; i < kHookCount; ++i) {
        out << hookFires[i] << "\t" << kUpdateHooks[i] << "\n";
    }
    out << hookFires[kHookCount] << "\tviewport tick\n";
    out << "SetPOV kept=" << previewKept << " overwritten=" << previewLost << "\n";
    out << "car live=" << cvarManager->getCvar("menucam_car_live").getBoolValue()
        << " move=" << cvarManager->getCvar("menucam_car_x").getFloatValue() << ","
        << cvarManager->getCvar("menucam_car_y").getFloatValue() << ","
        << cvarManager->getCvar("menucam_car_z").getFloatValue()
        << " yaw=" << cvarManager->getCvar("menucam_car_yaw").getFloatValue() << "\n";
    for (size_t i = 0; i < kCarHookCount; ++i) {
        out << carHookFires[i] << "\t" << kCarHooks[i] << "\n";
    }
    for (const auto& [address, spot] : cars) {
        out << (address == pivotCar ? "* car " : "  car ") << std::hex << address << std::dec
            << " base=" << spot.base.X << "," << spot.base.Y << "," << spot.base.Z
            << " yaw=" << Degrees(spot.baseRotation.Yaw) << " moved=" << spot.moved << "\n";
    }
    if (!camera.IsNull()) {
        POV pov = camera.GetPOV();
        Vector shake = camera.GetShakeLocationOffset();
        out << "state=" << camera.GetCameraState() << "\n";
        out << "pov=" << pov.location.X << "," << pov.location.Y << "," << pov.location.Z
            << " rot=" << Degrees(pov.rotation.Pitch) << "," << Degrees(pov.rotation.Yaw)
            << " fov=" << pov.FOV << "\n";
        out << "camera actor location=" << camera.GetLocation().X << "," << camera.GetLocation().Y
            << "," << camera.GetLocation().Z << "\n";
        out << "shake=" << shake.X << "," << shake.Y << "," << shake.Z
            << " shakeFov=" << camera.GetShakeFOVOffset()
            << " shakeDisabled=" << camera.GetbDisableCameraShake() << "\n";
    }
}

void MenuCam::Log(const CamReading& r, const std::string& note) {
    std::ostringstream line;
    const KnownCamera* known = MatchCamera(r.location);
    line << std::fixed << std::setprecision(1) << Timestamp() << "\t" << note << "\t"
         << "camera=" << KeyOf(r) << "\ttag=" << (known ? known->tag : "") << "\tfocus=" << r.focus << "\tstate=" << r.state << "\tloc=" << r.location.X << ","
         << r.location.Y << "," << r.location.Z << "\trot=" << Degrees(r.rotation.Pitch) << ","
         << Degrees(r.rotation.Yaw) << "," << Degrees(r.rotation.Roll) << "\tfov=" << r.fov;
    std::string name = NameOf(r);
    if (!name.empty()) {
        line << "\tname=" << name;
    }
    cvarManager->log("menucam: " + line.str());
    std::ofstream(dir / "log.txt", std::ios::app) << line.str() << "\n";
}

void MenuCam::SaveName(const std::string& label) {
    auto r = Read();
    if (!r) {
        cvarManager->log("menucam: no camera right now");
        return;
    }
    {
        std::lock_guard guard(lock);
        if (label.empty()) {
            names.erase(KeyOf(*r));
        } else {
            names[KeyOf(*r)] = label;
        }
    }
    SaveNames();
    Log(*r, label.empty() ? "unnamed" : "named");
}

void MenuCam::LoadNames() {
    std::ifstream in(dir / "names.txt");
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            names[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
}

void MenuCam::SaveNames() {
    std::ofstream out(dir / "names.txt", std::ios::trunc);
    std::lock_guard guard(lock);
    for (const auto& [key, label] : names) {
        out << key << "=" << label << "\n";
    }
}

void MenuCam::Draw(CanvasWrapper canvas) {
    if (!cvarManager->getCvar("menucam_overlay").getBoolValue()) {
        return;
    }
    std::optional<CamReading> r;
    std::string name;
    {
        std::lock_guard guard(lock);
        r = current;
        if (r) {
            auto it = names.find(KeyOf(*r));
            if (it != names.end()) {
                name = it->second;
            }
        }
    }
    if (!r) {
        return;
    }
    std::ostringstream text;
    const KnownCamera* known = MatchCamera(r->location);
    text << std::fixed << std::setprecision(0) << (name.empty() ? "(no name yet)" : name) << "  |  "
         << KeyOf(*r) << (known && *known->tag ? std::string(" (") + known->tag + ")" : "") << "\n"
         << "X " << r->location.X << "  Y " << r->location.Y << "  Z " << r->location.Z
         << std::setprecision(1) << "  |  pitch " << Degrees(r->rotation.Pitch) << "  yaw "
         << Degrees(r->rotation.Yaw) << "  |  FOV " << r->fov;
    canvas.SetPosition(Vector2{20, 20});
    canvas.SetColor(255, 255, 255, 230);
    std::istringstream lines(text.str());
    std::string line;
    int y = 20;
    while (std::getline(lines, line)) {
        canvas.SetPosition(Vector2{20, y});
        canvas.DrawString(line, 1.5f, 1.5f, true);
        y += 24;
    }
}

std::string MenuCam::GetPluginName() {
    return "Menu Camera Logger";
}

void MenuCam::SetImGuiContext(uintptr_t ctx) {
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

void MenuCam::RenderSettings() {
    std::optional<CamReading> r;
    std::string name;
    std::map<std::string, std::string> saved;
    {
        std::lock_guard guard(lock);
        r = current;
        saved = names;
        if (r) {
            auto it = names.find(KeyOf(*r));
            if (it != names.end()) {
                name = it->second;
            }
        }
    }

    ImGui::TextWrapped("1. Go to a menu screen.  2. Type what it is below.  3. Press Save name.  "
                       "Repeat for each screen.");
    ImGui::Separator();
    if (r) {
        const KnownCamera* known = MatchCamera(r->location);
        ImGui::Text("Camera now: %s%s%s%s", KeyOf(*r).c_str(), known && *known->tag ? " (" : "",
                    known && *known->tag ? known->tag : "", known && *known->tag ? ")" : "");
        ImGui::Text("Your name for it: %s", name.empty() ? "(none yet)" : name.c_str());
    } else {
        ImGui::TextUnformatted("No camera right now.");
    }
    ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Save name")) {
        std::string label = nameBuffer;
        gameWrapper->Execute([this, label](GameWrapper*) { SaveName(label); });
        nameBuffer[0] = '\0';
    }

    if (!saved.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Named so far:");
        for (const auto& [key, label] : saved) {
            ImGui::BulletText("%s  =  %s", label.c_str(), key.c_str());
        }
    }

    ImGui::Spacing();
    auto overlay = cvarManager->getCvar("menucam_overlay");
    bool showOverlay = overlay.getBoolValue();
    if (ImGui::Checkbox("Show the camera in the corner of the screen", &showOverlay)) {
        overlay.setValue(showOverlay);
    }

    if (ImGui::CollapsingHeader("Live preview (optional)")) {
        ImGui::TextWrapped("Moves whatever camera is showing, only until you turn it off. "
                           "Copy numbers you like into Hebnix's Menu Map page.");
        auto live = cvarManager->getCvar("menucam_live");
        bool liveOn = live.getBoolValue();
        if (ImGui::Checkbox("Preview on", &liveOn)) {
            live.setValue(liveOn);
        }
        auto methodCvar = cvarManager->getCvar("menucam_method");
        int method = methodCvar.getIntValue();
        ImGui::SameLine();
        if (ImGui::RadioButton("Method A", method == 0)) {
            methodCvar.setValue(0);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Method B", method == 1)) {
            methodCvar.setValue(1);
        }
        const Slider sliders[] = {
            {"menucam_x", "Move X", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_y", "Move Y", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_z", "Move Z", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_pitch", "Tilt", -89, 89},
            {"menucam_yaw", "Turn", -180, 180},
            {"menucam_fov", "FOV (0 = game's)", 0, 170},
        };
        for (const auto& slider : sliders) {
            SliderRow(cvarManager->getCvar(slider.cvar), slider);
        }
        if (ImGui::Button("Reset")) {
            for (const auto& slider : sliders) {
                cvarManager->getCvar(slider.cvar).setValue(0.0f);
            }
        }
        if (ImGui::TreeNode("Preview check")) {
            for (size_t i = 0; i < kCarHookCount; ++i) {
                ImGui::Text("%8d  %s", carHookFires[i], kCarHooks[i]);
            }
            ImGui::TextWrapped("With Preview on and a slider moved, these show where the plugin "
                               "gets to move the camera. Screenshot this if the camera doesn't move.");
            for (size_t i = 0; i < kHookCount; ++i) {
                ImGui::Text("%8d  %s", hookFires[i], kUpdateHooks[i]);
            }
            ImGui::Text("%8d  viewport tick", hookFires[kHookCount]);
            ImGui::Text("Preview kept %d times, overwritten by the game %d times", previewKept.load(),
                        previewLost.load());
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Car position (live preview)")) {
        ImGui::TextWrapped("Moves the cars on the home screen, only until you turn it off. The home "
                           "camera follows your car. Party cars turn with yours. These are the same "
                           "numbers as Car position on Hebnix's Menu Map page.");
        auto live = cvarManager->getCvar("menucam_car_live");
        bool liveOn = live.getBoolValue();
        if (ImGui::Checkbox("Move the cars", &liveOn)) {
            live.setValue(liveOn);
        }
        const Slider sliders[] = {
            {"menucam_car_x", "Car X", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_car_y", "Car Y", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_car_z", "Car Z", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_car_yaw", "Car turn", -180, 180},
        };
        for (const auto& slider : sliders) {
            SliderRow(cvarManager->getCvar(slider.cvar), slider);
        }
        if (ImGui::Button("Reset car")) {
            for (const auto& slider : sliders) {
                cvarManager->getCvar(slider.cvar).setValue(0.0f);
            }
        }
        ImGui::Text("Cars found: %d", carCount.load());
    }
}
