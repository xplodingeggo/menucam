#include "MenuCam.h"

#include "imgui/imgui.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

BAKKESMOD_PLUGIN(MenuCam, "Menu Camera Logger", "1.2", PLUGINTYPE_FREEPLAY)

namespace {

constexpr float kRotatorPerDegree = 65536.0f / 360.0f;

float Degrees(int rotator) {
    return rotator / kRotatorPerDegree;
}

// The home screen camera orbits the player's car. It isn't in the menu file,
// so Hebnix can't change it.
constexpr const char* kHomeKey = "home";
constexpr const char* kPlayMenuCamera = "CameraActor_2";

// Runs while the home screen camera shows. Offsets are applied here as well
// as on the viewport tick.
constexpr const char* kHomeCameraHook = "Function TAGame.CameraState_CarPreview_TA.UpdatePOV";
constexpr const char* kViewportTick = "Function Engine.GameViewportClient.Tick";

// Functions the home-screen cars run, used to move each car from inside its
// own call. Tick fires every frame; the other is counted in case it stops.
const char* kCarHooks[] = {
    "Function TAGame.CarPreviewActor_TA.Tick",
    "Function TAGame.CarPreviewActor_TA.UpdateBoostGlow",
};
constexpr size_t kCarHookCount = sizeof(kCarHooks) / sizeof(kCarHooks[0]);

// The player's spot in MENU_Main_p (2026-10 build). Party spots fan out from
// it, and the line-up turns around it, as in Hebnix.
const Vector kPlayerCarSpot{-1345.9f, -1587.4f, 0.0f};

bool IsHomeCamera(const std::string& state) {
    return state.find("CarPreview") != std::string::npos;
}

bool IsFixedCamera(const std::string& state) {
    return state.find("MenuCamActor") != std::string::npos;
}

std::string JsonString(const std::string& text) {
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
        }
        out += ch;
    }
    return out + "\"";
}

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

const KnownCamera* FindCamera(const std::string& name) {
    for (const auto& camera : kKnownCameras) {
        if (name == camera.name) {
            return &camera;
        }
    }
    return nullptr;
}

// "CameraActor_3 (GoalExplosionRevealEnd)", or just the name when untagged.
std::string CameraLabel(const std::string& name) {
    const KnownCamera* camera = FindCamera(name);
    return camera && *camera->tag ? name + " (" + camera->tag + ")" : name;
}

// Distances reach the far ends of big custom maps.
constexpr float kMaxMove = 100000.0f;
// Above 1, the slider is finer near zero so small moves stay easy on a big
// range.
constexpr float kMovePower = 4.0f;

// A slider, plus a box for typing an exact number. The box accepts values
// past the slider's range.
bool SliderRow(const char* id, const char* label, float* value, float min, float max, float power = 1.0f) {
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width * 0.55f);
    bool changed = ImGui::SliderFloat("##slider", value, min, max, "%.1f", power);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width * 0.2f);
    changed |= ImGui::InputFloat("##box", value, 0.0f, 0.0f, "%.1f");
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
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
    LoadOffsets();

    cvarManager->registerCvar("menucam_overlay", "1", "Show the current menu camera on screen", true, true, 0, true, 1);
    cvarManager->registerCvar("menucam_car_live", "0", "Move the home screen cars live", true, true, 0, true, 1, false);
    for (const char* axis : {"menucam_car_x", "menucam_car_y", "menucam_car_z"}) {
        cvarManager->registerCvar(axis, "0", "Car offset (Unreal units)", true, false, 0, false, 0, false);
    }
    cvarManager->registerCvar("menucam_car_yaw", "0", "Car turn (degrees)", true, true, -180, true, 180, false);

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

    cvarManager->registerNotifier("menucam_export", [this](std::vector<std::string>) { ExportForHebnix(); },
                                  "Save the car and fixed camera offsets for Hebnix's Import MenuCam values button",
                                  PERMISSION_ALL);

    gameWrapper->HookEvent(kViewportTick, [this](std::string) {
        ++frame;
        OnTick();
        ApplyCameraOffsets();
        PickPivotCar();
        if (frame % 120 == 0) {
            WriteDiagnostics();
            SaveOffsets();
        }
    });
    gameWrapper->HookEventPost(kHomeCameraHook, [this](std::string) { ApplyCameraOffsets(); });
    carHookFires.assign(kCarHookCount, 0);
    for (size_t i = 0; i < kCarHookCount; ++i) {
        gameWrapper->HookEventWithCallerPost<ActorWrapper>(kCarHooks[i], [this, i](ActorWrapper car, void*, std::string) {
            ++carHookFires[i];
            OnCarTick(car);
        });
    }
    gameWrapper->RegisterDrawable([this](CanvasWrapper canvas) { Draw(canvas); });

    cvarManager->log("menucam: loaded, writing to " + dir.string());
}

void MenuCam::onUnload() {
    gameWrapper->UnhookEvent(kViewportTick);
    gameWrapper->UnhookEventPost(kHomeCameraHook);
    for (const char* hook : kCarHooks) {
        gameWrapper->UnhookEventPost(hook);
    }
    CameraWrapper camera = gameWrapper->GetCamera();
    if (!camera.IsNull()) {
        ClearShake(camera);
    }
    SaveOffsets();
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
    r.location = pov.location;
    r.rotation = pov.rotation;
    r.fov = pov.FOV;
    // Report what the game chose, not our own offsets.
    if (shakeSet) {
        Vector move = camera.GetShakeLocationOffset();
        Rotator turn = camera.GetShakeRotationOffset();
        r.location = Vector{r.location.X - move.X, r.location.Y - move.Y, r.location.Z - move.Z};
        r.rotation.Pitch -= turn.Pitch;
        r.rotation.Yaw -= turn.Yaw;
        r.fov -= camera.GetShakeFOVOffset();
    }
    return r;
}

std::string MenuCam::KeyOf(const CamReading& r) const {
    if (!r.focus.empty() && r.focus != "None") {
        return r.focus;
    }
    // Fixed cameras stand still; anything else (the car orbit on the home
    // screen) moves every frame, so it is told apart by its state only.
    if (!IsFixedCamera(r.state)) {
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
    auto r = Read();
    std::string key = r ? KeyOf(*r) + "|" + r->state : std::string();
    bool changed = false;
    {
        std::lock_guard guard(lock);
        current = r;
        if (r && IsHomeCamera(r->state)) {
            onScreen = kHomeKey;
        } else if (r && IsFixedCamera(r->state)) {
            // A camera blending in sits between spots; keep the last match
            // until it settles on a known one.
            if (const KnownCamera* known = MatchCamera(r->location)) {
                onScreen = known->name;
            } else if (onScreen == kHomeKey) {
                onScreen.clear();
            }
        } else {
            onScreen.clear();
        }
        if (key != lastKey) {
            lastKey = key;
            changed = true;
        }
    }
    if (changed && r) {
        Log(*r, "switched");
    }
}

void MenuCam::ClearShake(CameraWrapper camera) {
    if (!shakeSet) {
        return;
    }
    camera.SetShakeLocationOffset(Vector{0, 0, 0});
    camera.SetShakeRotationOffset(Rotator{0, 0, 0});
    camera.SetShakeFOVOffset(0);
    shakeSet = false;
}

void MenuCam::ApplyCameraOffsets() {
    CameraWrapper camera = gameWrapper->GetCamera();
    if (camera.IsNull()) {
        return;
    }
    CamOffsets use;
    {
        std::lock_guard guard(lock);
        auto it = offsets.find(onScreen);
        if (it != offsets.end()) {
            use = it->second;
        }
    }
    if (!use.on || !use.Changes()) {
        ClearShake(camera);
        return;
    }
    // The camera's own shake offsets, which the game adds to the final view
    // every frame. Overwriting the view itself doesn't stick in the menus.
    camera.SetbDisableCameraShake(0);
    camera.SetShakeLocationOffset(Vector{use.x, use.y, use.z});
    camera.SetShakeRotationOffset(Rotator{static_cast<int>(std::lround(use.pitch * kRotatorPerDegree)),
                                          static_cast<int>(std::lround(use.yaw * kRotatorPerDegree)), 0});
    float base = camera.GetPOV().FOV - camera.GetShakeFOVOffset();
    camera.SetShakeFOVOffset(use.fov >= 10 ? use.fov - base : 0);
    shakeSet = true;
}

void MenuCam::OnCarTick(ActorWrapper car) {
    if (car.IsNull()) {
        return;
    }
    CarSpot& spot = cars[car.memory_address];
    spot.lastSeen = frame;
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
    // Forget cars that stopped running (left the menu, or a party member left).
    for (auto it = cars.begin(); it != cars.end();) {
        if (frame - it->second.lastSeen > 300) {
            it = cars.erase(it);
        } else {
            ++it;
        }
    }
    carCount = static_cast<int>(cars.size());
    if (!cvarManager->getCvar("menucam_car_live").getBoolValue()) {
        pivotCar = 0;
        return;
    }
    if (pivotCar && cars.count(pivotCar)) {
        return;
    }
    for (const auto& [address, spot] : cars) {
        if (std::abs(spot.base.X - kPlayerCarSpot.X) < 2 && std::abs(spot.base.Y - kPlayerCarSpot.Y) < 2 &&
            std::abs(spot.base.Z - kPlayerCarSpot.Z) < 2) {
            pivotCar = address;
            return;
        }
    }
    // Not the stored spot (another game build): the home camera orbits the
    // player's car and looks at it, so pick the car closest to the line of
    // sight.
    auto r = Read();
    if (!r || cars.empty()) {
        return;
    }
    float pitch = Degrees(r->rotation.Pitch) * 3.14159265f / 180.0f;
    float turn = Degrees(r->rotation.Yaw) * 3.14159265f / 180.0f;
    Vector look{std::cos(pitch) * std::cos(turn), std::cos(pitch) * std::sin(turn), std::sin(pitch)};
    float best = 0;
    for (const auto& [address, spot] : cars) {
        Vector to{spot.base.X - r->location.X, spot.base.Y - r->location.Y, spot.base.Z - r->location.Z};
        float along = to.X * look.X + to.Y * look.Y + to.Z * look.Z;
        float length2 = to.X * to.X + to.Y * to.Y + to.Z * to.Z;
        float off = length2 - along * along;
        if (along > 0 && (pivotCar == 0 || off < best)) {
            pivotCar = address;
            best = off;
        }
    }
}

void MenuCam::ExportForHebnix() {
    auto value = [this](const char* name) { return cvarManager->getCvar(name).getFloatValue(); };
    std::ostringstream json;
    json << std::fixed << std::setprecision(1) << "{\n";
    bool hasCar = value("menucam_car_x") != 0 || value("menucam_car_y") != 0 || value("menucam_car_z") != 0 ||
                  value("menucam_car_yaw") != 0;
    if (hasCar) {
        json << "  \"car\": {\"offset\": [" << value("menucam_car_x") << ", " << value("menucam_car_y") << ", "
             << value("menucam_car_z") << "], \"yaw\": " << value("menucam_car_yaw") << "}";
    }
    std::map<std::string, CamOffsets> saved;
    {
        std::lock_guard guard(lock);
        saved = offsets;
    }
    // Only fixed cameras from the menu file can go to Hebnix.
    std::vector<std::string> exported;
    for (const auto& [key, use] : saved) {
        if (key == kHomeKey || !use.on || !use.Changes()) {
            continue;
        }
        json << (hasCar || !exported.empty() ? ",\n" : "") << (exported.empty() ? "  \"cameras\": [\n" : "")
             << "    {\"camera\": " << JsonString(key) << ", \"offset\": [" << use.x << ", " << use.y << ", "
             << use.z << "], \"pitch\": " << use.pitch << ", \"yaw\": " << use.yaw << ", \"fov\": ";
        if (use.fov >= 10) {
            json << use.fov;
        } else {
            json << "null";
        }
        json << "}";
        exported.push_back(CameraLabel(key));
    }
    if (!exported.empty()) {
        json << "\n  ]";
    }
    json << "\n}\n";
    std::string status;
    if (!hasCar && exported.empty()) {
        status = "Nothing to export: move the car, or turn on offsets for a fixed menu camera, first.";
    } else {
        std::ofstream(dir / "hebnix.json", std::ios::trunc) << json.str();
        std::string what = hasCar ? "the car position" : "";
        for (const auto& label : exported) {
            what += (what.empty() ? "" : ", ") + label;
        }
        status = "Saved " + what + ". In Hebnix, press Import MenuCam values on the Menu Map page.";
    }
    {
        std::lock_guard guard(lock);
        exportStatus = status;
    }
    cvarManager->log("menucam: " + status);
}

void MenuCam::LoadOffsets() {
    std::ifstream in(dir / "cameras.txt");
    std::string key;
    CamOffsets use;
    while (in >> key >> use.on >> use.x >> use.y >> use.z >> use.pitch >> use.yaw >> use.fov) {
        offsets[key] = use;
    }
}

void MenuCam::SaveOffsets() {
    std::map<std::string, CamOffsets> saved;
    {
        std::lock_guard guard(lock);
        if (!offsetsDirty) {
            return;
        }
        offsetsDirty = false;
        saved = offsets;
    }
    std::ofstream out(dir / "cameras.txt", std::ios::trunc);
    out << std::setprecision(9);
    for (const auto& [key, use] : saved) {
        out << key << " " << use.on << " " << use.x << " " << use.y << " " << use.z << " " << use.pitch << " "
            << use.yaw << " " << use.fov << "\n";
    }
}

void MenuCam::WriteDiagnostics() {
    CameraWrapper camera = gameWrapper->GetCamera();
    std::string screen;
    CamOffsets use;
    {
        std::lock_guard guard(lock);
        screen = onScreen;
        auto it = offsets.find(onScreen);
        if (it != offsets.end()) {
            use = it->second;
        }
    }
    std::ofstream out(dir / "preview-check.txt", std::ios::trunc);
    out << std::fixed << std::setprecision(1) << Timestamp() << "  frame " << frame << "\n";
    out << "on screen=" << (screen.empty() ? "(none)" : screen) << " offsets on=" << use.on << " move=" << use.x
        << "," << use.y << "," << use.z << " tilt=" << use.pitch << " turn=" << use.yaw << " fov=" << use.fov
        << "\n";
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

void MenuCam::RenderCameraTab(const std::string& key, const char* note) {
    CamOffsets use;
    bool showing;
    {
        std::lock_guard guard(lock);
        auto it = offsets.find(key);
        if (it != offsets.end()) {
            use = it->second;
        }
        showing = onScreen == key;
    }
    ImGui::PushID(key.c_str());
    ImGui::TextDisabled(showing ? "On screen now." : "Not on screen. Changes show when it is.");
    bool changed = ImGui::Checkbox("Use these offsets", &use.on);
    changed |= SliderRow("x", "Move X", &use.x, -kMaxMove, kMaxMove, kMovePower);
    changed |= SliderRow("y", "Move Y", &use.y, -kMaxMove, kMaxMove, kMovePower);
    changed |= SliderRow("z", "Move Z", &use.z, -kMaxMove, kMaxMove, kMovePower);
    changed |= SliderRow("pitch", "Tilt", &use.pitch, -89, 89);
    changed |= SliderRow("yaw", "Turn", &use.yaw, -180, 180);
    changed |= SliderRow("fov", "FOV (0 = game's)", &use.fov, 0, 170);
    if (ImGui::Button("Reset")) {
        use = CamOffsets{use.on};
        changed = true;
    }
    ImGui::TextWrapped("%s", note);
    ImGui::PopID();
    if (changed) {
        std::lock_guard guard(lock);
        offsets[key] = use;
        offsetsDirty = true;
    }
}

void MenuCam::RenderSettings() {
    std::optional<CamReading> r;
    std::string name;
    std::string screen;
    std::map<std::string, std::string> saved;
    {
        std::lock_guard guard(lock);
        r = current;
        screen = onScreen;
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

    if (ImGui::CollapsingHeader("Camera offsets", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Each camera keeps its own offsets. They apply whenever that camera is on "
                           "screen, and are saved for next time.");
        if (screen == kHomeKey) {
            ImGui::TextUnformatted("On screen: the home screen camera.");
        } else if (!screen.empty()) {
            ImGui::Text("On screen: %s", CameraLabel(screen).c_str());
        } else {
            ImGui::TextUnformatted("On screen: a camera without offsets (gameplay, replays and so on).");
        }
        if (!screen.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Open its tab")) {
                selectTab = screen;
            }
        }
        const char* fixedNote = "Export for Hebnix includes this camera when its offsets are on.";
        auto tabFlags = [this](const std::string& key) {
            return selectTab == key ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };
        if (ImGui::BeginTabBar("cameras")) {
            if (ImGui::BeginTabItem("Home screen", nullptr, tabFlags(kHomeKey))) {
                RenderCameraTab(kHomeKey, "The camera that orbits your car. Hebnix can't change this one, so "
                                          "these offsets only work with this plugin loaded.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Play menu", nullptr, tabFlags(kPlayMenuCamera))) {
                RenderCameraTab(kPlayMenuCamera, fixedNote);
                ImGui::EndTabItem();
            }
            bool other = !selectTab.empty() && selectTab != kHomeKey && selectTab != kPlayMenuCamera;
            if (other) {
                otherCamera = selectTab;
            }
            if (ImGui::BeginTabItem("Other cameras", nullptr,
                                    other ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                if (ImGui::BeginCombo("Camera", CameraLabel(otherCamera).c_str())) {
                    for (const auto& camera : kKnownCameras) {
                        if (camera.name == std::string(kPlayMenuCamera)) {
                            continue;
                        }
                        if (ImGui::Selectable(CameraLabel(camera.name).c_str(), otherCamera == camera.name)) {
                            otherCamera = camera.name;
                        }
                    }
                    ImGui::EndCombo();
                }
                RenderCameraTab(otherCamera, fixedNote);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        selectTab.clear();
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
        struct CarSlider {
            const char* cvar;
            const char* label;
            float min;
            float max;
            float power;
        };
        const CarSlider sliders[] = {
            {"menucam_car_x", "Car X", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_car_y", "Car Y", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_car_z", "Car Z", -kMaxMove, kMaxMove, kMovePower},
            {"menucam_car_yaw", "Car turn", -180, 180, 1.0f},
        };
        for (const auto& slider : sliders) {
            auto cvar = cvarManager->getCvar(slider.cvar);
            float value = cvar.getFloatValue();
            if (SliderRow(slider.cvar, slider.label, &value, slider.min, slider.max, slider.power)) {
                cvar.setValue(value);
            }
        }
        if (ImGui::Button("Reset car")) {
            for (const auto& slider : sliders) {
                cvarManager->getCvar(slider.cvar).setValue(0.0f);
            }
        }
        ImGui::Text("Cars found: %d", carCount.load());
    }

    if (ImGui::CollapsingHeader("Send to Hebnix")) {
        ImGui::TextWrapped("Saves the car position, and the offsets of every fixed camera that has them "
                           "on, to data/menucam/hebnix.json. Then press Import MenuCam values on "
                           "Hebnix's Menu Map page.");
        if (ImGui::Button("Export for Hebnix")) {
            gameWrapper->Execute([this](GameWrapper*) { ExportForHebnix(); });
        }
        std::string status;
        {
            std::lock_guard guard(lock);
            status = exportStatus;
        }
        if (!status.empty()) {
            ImGui::TextWrapped("%s", status.c_str());
        }
    }
}
