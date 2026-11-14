//
// miniapp: Switch 硬件能力桥接实现。
//
// 线程约束：tick 与控制方法只在 UI 线程执行；状态快照加锁供脚本线程读取。
// 桌面构建（macOS 预览）没有对应硬件，统一返回 supported=false。
//

#include "miniapp/hardware.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#ifdef __SWITCH__
#include <switch.h>

#include <borealis/core/application.hpp>
#include <borealis/core/input.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/platforms/switch/switch_input.hpp>
#endif

namespace miniapp {

namespace {

#ifdef __SWITCH__
constexpr int kIrImageWidth = 320;
constexpr int kIrImageHeight = 240;
constexpr size_t kIrImageBytes = static_cast<size_t>(kIrImageWidth) * kIrImageHeight;
// libnx 的 tmemCreate 要求 0x1000 对齐，76800 不是页大小整数倍，向上取整。
constexpr size_t kIrTransferBytes = (kIrImageBytes + 0xFFF) & ~static_cast<size_t>(0xFFF);
constexpr double kIrPollIntervalMs = 32.0;   // 约 30fps，避免每帧 IPC
constexpr double kNfcPollIntervalMs = 100.0;
#endif

JsonValue makeFlagState(const char* errorText) {
    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(false));
    state.setMember("error", JsonValue::string(errorText));
    return state;
}

}  // namespace

struct HardwareManager::Impl {
    mutable std::mutex mutex;

    JsonValue motionState;
    JsonValue irState;
    JsonValue nfcState;

    std::vector<std::uint8_t> irRgba;
    int irWidth = 0;
    int irHeight = 0;
    std::uint64_t irVersion = 0;

    double lastTick = 0.0;
    double rumbleRemainingMs = 0.0;
    bool rumbleActive = false;

#ifdef __SWITCH__
    bool motionStarted = false;
    HidSixAxisSensorHandle handles[2] = {};
    int handleCount = 0;

    bool irActive = false;
    bool irImageMode = true;
    IrsIrCameraHandle irHandle = {};
    double irLastPoll = 0.0;
    std::vector<std::uint8_t> irGray;
    std::string irError;
    std::uint32_t irAmbientNoise = 0;
    std::uint64_t irSampling = 0;
    std::vector<JsonValue> irBlobs;
    JsonValue irHand = JsonValue::object();

    bool nfpInited = false;
    bool nfcDetecting = false;
    bool nfcMounted = false;
    bool nfcTagPresent = false;
    bool nfcAreaOpened = false;
    int nfcMissCount = 0;
    NfcDeviceHandle nfcHandle = {};
    double nfcLastPoll = 0.0;
    std::string nfcError;
    JsonValue nfcAmiibo = JsonValue::object();

    void ensureMotion();
    void pollMotion();
    void stopMotion();
    void stopRumbleInternal();
    bool startIr(const std::string& mode);
    void stopIr();
    void pollIr(double nowMs);
    bool startNfc();
    void stopNfc();
    void pollNfc(double nowMs);
    bool mountNfc();
    bool unmountNfc();
    bool writeAppArea(const std::string& hexData);
#endif
};

HardwareManager::HardwareManager() : impl(new Impl()) {
    impl->motionState = makeFlagState("desktop build");
    impl->irState = makeFlagState("desktop build");
    impl->nfcState = makeFlagState("desktop build");
}

HardwareManager::~HardwareManager() { shutdown(); }

void HardwareManager::shutdown() {
#ifdef __SWITCH__
    impl->stopRumbleInternal();
    impl->stopIr();
    impl->stopNfc();
    impl->stopMotion();
#endif
}

void HardwareManager::tick(double nowMs) {
#ifdef __SWITCH__
    double delta = impl->lastTick > 0.0 ? nowMs - impl->lastTick : 0.0;
    impl->lastTick = nowMs;
    if (delta < 0.0 || delta > 200.0) delta = 0.0;

    impl->pollMotion();
    if (impl->rumbleActive) {
        if (impl->rumbleRemainingMs > 0.0) {
            impl->rumbleRemainingMs -= delta;
            if (impl->rumbleRemainingMs <= 0.0) impl->stopRumbleInternal();
        }
    }
    impl->pollIr(nowMs);
    impl->pollNfc(nowMs);
#else
    (void)nowMs;
#endif
}

JsonValue HardwareManager::motionState() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->motionState;
}

void HardwareManager::rumble(double lowFreq, double highFreq, double lowAmp, double highAmp,
                             double durationMs) {
#ifdef __SWITCH__
    auto* platform = brls::Application::getPlatform();
    brls::InputManager* input = platform ? platform->getInputManager() : nullptr;
    auto* switchInput = dynamic_cast<brls::SwitchInputManager*>(input);
    if (!switchInput) return;

    const float lowF = static_cast<float>(std::max(40.0, std::min(1250.0, lowFreq)));
    const float highF = static_cast<float>(std::max(40.0, std::min(1250.0, highFreq)));
    const float lowA = static_cast<float>(std::max(0.0, std::min(1.0, lowAmp)));
    const float highA = static_cast<float>(std::max(0.0, std::min(1.0, highAmp)));
    switchInput->sendRumbleRaw(lowF, highF, lowA, highA);
    impl->rumbleActive = true;
    impl->rumbleRemainingMs = durationMs > 0.0 ? durationMs : 0.0;
#else
    (void)lowFreq;
    (void)highFreq;
    (void)lowAmp;
    (void)highAmp;
    (void)durationMs;
#endif
}

void HardwareManager::rumbleStop() {
#ifdef __SWITCH__
    impl->stopRumbleInternal();
#endif
}

bool HardwareManager::irStart(const std::string& mode) {
#ifdef __SWITCH__
    return impl->startIr(mode);
#else
    (void)mode;
    return false;
#endif
}

void HardwareManager::irStop() {
#ifdef __SWITCH__
    impl->stopIr();
#endif
}

bool HardwareManager::irRunning() const {
#ifdef __SWITCH__
    return impl->irActive;
#else
    return false;
#endif
}

bool HardwareManager::copyIrImage(std::vector<std::uint8_t>& rgba, int& width, int& height,
                                  std::uint64_t& version) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->irRgba.empty()) return false;
    rgba = impl->irRgba;
    width = impl->irWidth;
    height = impl->irHeight;
    version = impl->irVersion;
    return true;
}

JsonValue HardwareManager::irState() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->irState;
}

bool HardwareManager::nfcStart() {
#ifdef __SWITCH__
    return impl->startNfc();
#else
    return false;
#endif
}

void HardwareManager::nfcStop() {
#ifdef __SWITCH__
    impl->stopNfc();
#endif
}

bool HardwareManager::nfcMount() {
#ifdef __SWITCH__
    return impl->mountNfc();
#else
    return false;
#endif
}

bool HardwareManager::nfcUnmount() {
#ifdef __SWITCH__
    return impl->unmountNfc();
#else
    return false;
#endif
}

bool HardwareManager::nfcWriteAppArea(const std::string& hexData) {
#ifdef __SWITCH__
    return impl->writeAppArea(hexData);
#else
    (void)hexData;
    return false;
#endif
}

JsonValue HardwareManager::nfcState() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->nfcState;
}

#ifdef __SWITCH__

namespace {

std::string resultText(Result rc) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", static_cast<unsigned>(R_VALUE(rc)));
    return buffer;
}

std::string hexBytes(const void* data, size_t size) {
    static const char* kDigits = "0123456789abcdef";
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kDigits[bytes[i] >> 4]);
        out.push_back(kDigits[bytes[i] & 0x0F]);
    }
    return out;
}

JsonValue vectorJson(const HidVector& v) {
    return JsonValue::array({JsonValue::number(v.x), JsonValue::number(v.y),
                             JsonValue::number(v.z)});
}

std::string dateText(const NfpDate& date) {
    if (date.year == 0 && date.month == 0 && date.day == 0) return "";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", date.year, date.month, date.day);
    return buffer;
}

bool parseHex(const std::string& hex, std::vector<std::uint8_t>& out) {
    if (hex.empty() || hex.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int high = nibble(hex[i * 2]);
        const int low = nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

}  // namespace

void HardwareManager::Impl::ensureMotion() {
    if (motionStarted) return;

    HidNpadIdType id = HidNpadIdType_Handheld;
    u32 style = hidGetNpadStyleSet(id);
    if ((style & HidNpadStyleTag_NpadHandheld) == 0) {
        id = HidNpadIdType_No1;
        style = hidGetNpadStyleSet(id);
    }
    if (style == 0) return;

    handleCount = static_cast<int>(hidGetSixAxisSensorHandles(
        handles, 2, id, static_cast<HidNpadStyleTag>(style)));
    if (handleCount <= 0) {
        handleCount = 0;
        return;
    }
    for (int i = 0; i < handleCount; ++i) {
        const Result rc = hidStartSixAxisSensor(handles[i]);
        if (R_FAILED(rc)) {
            brls::Logger::warning("miniapp motion: hidStartSixAxisSensor failed {}",
                                  resultText(rc));
        }
    }
    motionStarted = true;
}

void HardwareManager::Impl::pollMotion() {
    ensureMotion();

    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(true));
    state.setMember("connected", JsonValue::boolean(handleCount > 0));

    std::vector<JsonValue> controllers;
    JsonValue firstGyro = JsonValue::array({JsonValue::number(0), JsonValue::number(0),
                                            JsonValue::number(0)});
    JsonValue firstAccel = firstGyro;
    JsonValue firstAngle = firstGyro;

    for (int i = 0; motionStarted && i < handleCount; ++i) {
        HidSixAxisSensorState raw{};
        if (hidGetSixAxisSensorStates(handles[i], &raw, 1) == 0) continue;
        if ((raw.attributes & HidSixAxisSensorAttribute_IsConnected) == 0) continue;

        JsonValue entry = JsonValue::object();
        entry.setMember("index", JsonValue::number(i));
        entry.setMember("gyro", vectorJson(raw.angular_velocity));
        entry.setMember("accel", vectorJson(raw.acceleration));
        entry.setMember("angle", vectorJson(raw.angle));
        entry.setMember("deltaTime", JsonValue::number(static_cast<double>(raw.delta_time)));
        entry.setMember("sampling", JsonValue::number(static_cast<double>(raw.sampling_number)));
        controllers.push_back(std::move(entry));

        if (controllers.size() == 1) {
            firstGyro = vectorJson(raw.angular_velocity);
            firstAccel = vectorJson(raw.acceleration);
            firstAngle = vectorJson(raw.angle);
        }
    }

    state.setMember("count", JsonValue::number(static_cast<double>(controllers.size())));
    state.setMember("controllers", JsonValue::array(std::move(controllers)));
    state.setMember("gyro", std::move(firstGyro));
    state.setMember("accel", std::move(firstAccel));
    state.setMember("angle", std::move(firstAngle));

    std::lock_guard<std::mutex> lock(mutex);
    motionState = std::move(state);
}

void HardwareManager::Impl::stopMotion() {
    if (!motionStarted) return;
    for (int i = 0; i < handleCount; ++i) hidStopSixAxisSensor(handles[i]);
    handleCount = 0;
    motionStarted = false;
}

void HardwareManager::Impl::stopRumbleInternal() {
    rumbleActive = false;
    rumbleRemainingMs = 0.0;
    auto* platform = brls::Application::getPlatform();
    brls::InputManager* input = platform ? platform->getInputManager() : nullptr;
    auto* switchInput = dynamic_cast<brls::SwitchInputManager*>(input);
    if (switchInput) switchInput->sendRumbleRaw(160.0f, 320.0f, 0.0f, 0.0f);
}

bool HardwareManager::Impl::startIr(const std::string& mode) {
    stopIr();

    const bool gesture = mode == "gesture";
    irImageMode = !gesture;
    irError.clear();
    irBlobs.clear();
    irHand = JsonValue::object();
    irSampling = 0;
    irAmbientNoise = 0;

    Result rc = irsInitialize();
    if (R_FAILED(rc)) {
        irError = "irsInitialize failed " + resultText(rc);
        JsonValue state = makeFlagState(irError.c_str());
        state.setMember("supported", JsonValue::boolean(true));
        std::lock_guard<std::mutex> lock(mutex);
        irState = std::move(state);
        return false;
    }

    bool found = false;
    std::string lastStatus;
    for (int i = 0; i < 8; ++i) {
        IrsIrCameraHandle handle = {};
        rc = irsGetIrCameraHandle(&handle, static_cast<HidNpadIdType>(i));
        if (R_FAILED(rc)) {
            lastStatus = resultText(rc);
            continue;
        }
        IrsIrCameraStatus status = IrsIrCameraStatus_Unconnected;
        if (R_FAILED(irsGetIrCameraStatus(handle, &status))) continue;
        if (status == IrsIrCameraStatus_Available) {
            irHandle = handle;
            found = true;
            break;
        }
        lastStatus = status == IrsIrCameraStatus_Unsupported ? "unsupported" : "unconnected";
    }

    if (!found) {
        irError = "IR camera not available";
        if (!lastStatus.empty()) irError += " (" + lastStatus + ")";
        irsExit();
        JsonValue state = makeFlagState(irError.c_str());
        state.setMember("supported", JsonValue::boolean(true));
        state.setMember("running", JsonValue::boolean(false));
        std::lock_guard<std::mutex> lock(mutex);
        irState = std::move(state);
        return false;
    }

    if (gesture) {
        IrsClusteringProcessorConfig config;
        irsGetClusteringProcessorDefaultConfig(&config);
        config.window_of_interest = {0, 0, static_cast<s16>(kIrImageWidth),
                                     static_cast<s16>(kIrImageHeight)};
        config.object_pixel_count_min = 24;
        config.object_pixel_count_max = kIrImageWidth * kIrImageHeight;
        config.object_intensity_min = 0;
        config.is_external_light_filter_enabled = 0;
        rc = irsRunClusteringProcessor(irHandle, &config);
    } else {
        IrsImageTransferProcessorConfig config;
        irsGetDefaultImageTransferProcessorConfig(&config);
        config.format = IrsImageTransferProcessorFormat_320x240;
        irGray.assign(kIrTransferBytes, 0);
        rc = irsRunImageTransferProcessor(irHandle, &config, irGray.size());
    }

    if (R_FAILED(rc)) {
        irError = "start IR processor failed " + resultText(rc);
        irsStopImageProcessor(irHandle);
        irsExit();
        JsonValue state = makeFlagState(irError.c_str());
        state.setMember("supported", JsonValue::boolean(true));
        state.setMember("running", JsonValue::boolean(false));
        std::lock_guard<std::mutex> lock(mutex);
        irState = std::move(state);
        return false;
    }

    irActive = true;
    irLastPoll = 0.0;

    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(true));
    state.setMember("running", JsonValue::boolean(true));
    state.setMember("mode", JsonValue::string(gesture ? "gesture" : "image"));
    state.setMember("width", JsonValue::number(gesture ? 0 : kIrImageWidth));
    state.setMember("height", JsonValue::number(gesture ? 0 : kIrImageHeight));
    state.setMember("frameVersion", JsonValue::number(0));
    state.setMember("blobs", JsonValue::array());
    std::lock_guard<std::mutex> lock(mutex);
    irState = std::move(state);
    return true;
}

void HardwareManager::Impl::stopIr() {
    if (!irActive) return;
    irsStopImageProcessor(irHandle);
    irsExit();
    irActive = false;
    irLastPoll = 0.0;
    irBlobs.clear();
    irHand = JsonValue::object();

    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(true));
    state.setMember("running", JsonValue::boolean(false));
    std::lock_guard<std::mutex> lock(mutex);
    irState = std::move(state);
}

void HardwareManager::Impl::pollIr(double nowMs) {
    if (!irActive) return;
    if (irLastPoll > 0.0 && nowMs - irLastPoll < kIrPollIntervalMs) return;
    irLastPoll = nowMs;

    if (irImageMode) {
        if (irGray.empty()) return;
        IrsImageTransferProcessorState state = {};
        const Result rc =
            irsGetImageTransferProcessorState(irHandle, irGray.data(), irGray.size(), &state);
        if (R_FAILED(rc)) return;

        std::uint8_t maxValue = 0;
        for (size_t i = 0; i < kIrImageBytes; ++i) maxValue = std::max(maxValue, irGray[i]);
        const float gain = maxValue >= 32 ? 255.0f / static_cast<float>(maxValue) : 6.0f;

        std::vector<std::uint8_t> rgba(kIrImageBytes * 4);
        for (size_t i = 0; i < kIrImageBytes; ++i) {
            const int value = std::min(255, static_cast<int>(irGray[i] * gain));
            rgba[i * 4 + 0] = static_cast<std::uint8_t>(value);
            rgba[i * 4 + 1] = static_cast<std::uint8_t>(value);
            rgba[i * 4 + 2] = static_cast<std::uint8_t>(value);
            rgba[i * 4 + 3] = 255;
        }

        std::lock_guard<std::mutex> lock(mutex);
        irRgba = std::move(rgba);
        irWidth = kIrImageWidth;
        irHeight = kIrImageHeight;
        ++irVersion;
        irAmbientNoise = state.ambient_noise_level;
        irSampling = state.sampling_number;

        JsonValue json = JsonValue::object();
        json.setMember("supported", JsonValue::boolean(true));
        json.setMember("running", JsonValue::boolean(true));
        json.setMember("mode", JsonValue::string("image"));
        json.setMember("width", JsonValue::number(kIrImageWidth));
        json.setMember("height", JsonValue::number(kIrImageHeight));
        json.setMember("frameVersion", JsonValue::number(static_cast<double>(irVersion)));
        json.setMember("sampling", JsonValue::number(static_cast<double>(irSampling)));
        json.setMember("ambientNoise", JsonValue::number(static_cast<double>(irAmbientNoise)));
        json.setMember("blobs", JsonValue::array());
        irState = std::move(json);
        return;
    }

    IrsClusteringProcessorState state = {};
    s32 total = 0;
    const Result rc = irsGetClusteringProcessorStates(irHandle, &state, 1, &total);
    if (R_FAILED(rc) || total <= 0) return;

    std::vector<JsonValue> blobs;
    JsonValue hand = JsonValue::object();
    int bestPixels = 0;
    const int count = std::min<int>(state.object_count, 16);
    for (int i = 0; i < count; ++i) {
        const IrsClusteringData& data = state.data[i];
        const int width = data.boundt_width;
        const int height = data.bound_height;
        const int area = std::max(0, width * height);
        const double density = area > 0 ? static_cast<double>(data.pixel_count) / area : 0.0;

        JsonValue blob = JsonValue::object();
        blob.setMember("x", JsonValue::number(data.bound_x));
        blob.setMember("y", JsonValue::number(data.bound_y));
        blob.setMember("width", JsonValue::number(width));
        blob.setMember("height", JsonValue::number(height));
        blob.setMember("pixels", JsonValue::number(data.pixel_count));
        blob.setMember("density", JsonValue::number(density));
        blob.setMember("intensity", JsonValue::number(data.average_intensity));
        blob.setMember("centroidX", JsonValue::number(data.centroid_x));
        blob.setMember("centroidY", JsonValue::number(data.centroid_y));
        blobs.push_back(blob);

        if (static_cast<int>(data.pixel_count) > bestPixels) {
            bestPixels = static_cast<int>(data.pixel_count);
            hand = blobs.back();
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    irBlobs = blobs;
    irHand = hand;
    irAmbientNoise = state.ambient_noise_level;
    irSampling = static_cast<std::uint64_t>(state.sampling_number);

    JsonValue json = JsonValue::object();
    json.setMember("supported", JsonValue::boolean(true));
    json.setMember("running", JsonValue::boolean(true));
    json.setMember("mode", JsonValue::string("gesture"));
    json.setMember("frameVersion", JsonValue::number(static_cast<double>(irVersion)));
    json.setMember("sampling", JsonValue::number(static_cast<double>(irSampling)));
    json.setMember("ambientNoise", JsonValue::number(static_cast<double>(irAmbientNoise)));
    json.setMember("count", JsonValue::number(static_cast<double>(blobs.size())));
    json.setMember("blobs", JsonValue::array(blobs));
    json.setMember("hand", std::move(hand));
    irState = std::move(json);
}

bool HardwareManager::Impl::startNfc() {
    if (nfcDetecting) return true;
    nfcError.clear();
    nfcAmiibo = JsonValue::object();
    nfcTagPresent = false;
    nfcMissCount = 0;

    if (!nfpInited) {
        const Result rc = nfpInitialize(NfpServiceType_User);
        if (R_FAILED(rc)) {
            nfcError = "nfpInitialize failed " + resultText(rc);
            JsonValue state = makeFlagState(nfcError.c_str());
            state.setMember("supported", JsonValue::boolean(true));
            state.setMember("state", JsonValue::string("error"));
            std::lock_guard<std::mutex> lock(mutex);
            nfcState = std::move(state);
            return false;
        }
        nfpInited = true;
    }

    s32 total = 0;
    Result rc = nfpListDevices(&total, &nfcHandle, 1);
    if (R_FAILED(rc) || total <= 0) {
        nfcError = R_FAILED(rc) ? "nfpListDevices failed " + resultText(rc)
                                : "no NFC reader (attach the right Joy-Con)";
        JsonValue state = makeFlagState(nfcError.c_str());
        state.setMember("supported", JsonValue::boolean(true));
        state.setMember("state", JsonValue::string("unavailable"));
        std::lock_guard<std::mutex> lock(mutex);
        nfcState = std::move(state);
        return false;
    }

    rc = nfpStartDetection(&nfcHandle);
    if (R_FAILED(rc)) {
        nfcError = "nfpStartDetection failed " + resultText(rc);
        JsonValue state = makeFlagState(nfcError.c_str());
        state.setMember("supported", JsonValue::boolean(true));
        state.setMember("state", JsonValue::string("error"));
        std::lock_guard<std::mutex> lock(mutex);
        nfcState = std::move(state);
        return false;
    }

    nfcDetecting = true;
    nfcMounted = false;
    nfcLastPoll = 0.0;

    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(true));
    state.setMember("running", JsonValue::boolean(true));
    state.setMember("state", JsonValue::string("searching"));
    state.setMember("mounted", JsonValue::boolean(false));
    std::lock_guard<std::mutex> lock(mutex);
    nfcState = std::move(state);
    return true;
}

void HardwareManager::Impl::stopNfc() {
    if (nfcMounted) nfpUnmount(&nfcHandle);
    if (nfcDetecting) nfpStopDetection(&nfcHandle);
    if (nfpInited) nfpExit();

    nfcMounted = false;
    nfcDetecting = false;
    nfcTagPresent = false;
    nfcAreaOpened = false;
    nfpInited = false;
    nfcAmiibo = JsonValue::object();

    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(true));
    state.setMember("running", JsonValue::boolean(false));
    state.setMember("state", JsonValue::string("idle"));
    state.setMember("mounted", JsonValue::boolean(false));
    std::lock_guard<std::mutex> lock(mutex);
    nfcState = std::move(state);
}

void HardwareManager::Impl::pollNfc(double nowMs) {
    if (!nfcDetecting || nfcMounted) return;
    if (nfcLastPoll > 0.0 && nowMs - nfcLastPoll < kNfcPollIntervalMs) return;
    nfcLastPoll = nowMs;

    NfpTagInfo tag = {};
    const Result rc = nfpGetTagInfo(&nfcHandle, &tag);
    if (R_FAILED(rc)) {
        if (nfcTagPresent) {
            if (++nfcMissCount >= 3) {
                nfcTagPresent = false;
                nfcMissCount = 0;
                JsonValue state = JsonValue::object();
                state.setMember("supported", JsonValue::boolean(true));
                state.setMember("running", JsonValue::boolean(true));
                state.setMember("state", JsonValue::string("searching"));
                state.setMember("mounted", JsonValue::boolean(false));
                std::lock_guard<std::mutex> lock(mutex);
                nfcState = std::move(state);
            }
        }
        return;
    }

    nfcTagPresent = true;
    nfcMissCount = 0;

    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(true));
    state.setMember("running", JsonValue::boolean(true));
    state.setMember("state", JsonValue::string("found"));
    state.setMember("mounted", JsonValue::boolean(false));
    state.setMember("uid", JsonValue::string(hexBytes(tag.uid.uid, tag.uid.uid_length)));
    state.setMember("tagType", JsonValue::number(tag.tag_type));
    state.setMember("protocol", JsonValue::number(tag.protocol));
    state.setMember("deviceState", JsonValue::string("tag_found"));
    std::lock_guard<std::mutex> lock(mutex);
    nfcState = std::move(state);
}

bool HardwareManager::Impl::mountNfc() {
    if (!nfcDetecting) return false;
    if (nfcMounted) unmountNfc();

    Result rc = nfpMount(&nfcHandle, NfpDeviceType_Amiibo, NfpMountTarget_All);
    if (R_FAILED(rc)) {
        nfcError = "nfpMount failed " + resultText(rc);
        JsonValue state = JsonValue::object();
        state.setMember("supported", JsonValue::boolean(true));
        state.setMember("running", JsonValue::boolean(true));
        state.setMember("state", JsonValue::string("error"));
        state.setMember("error", JsonValue::string(nfcError));
        state.setMember("mounted", JsonValue::boolean(false));
        std::lock_guard<std::mutex> lock(mutex);
        nfcState = std::move(state);
        return false;
    }
    nfcMounted = true;

    NfpTagInfo tag = {};
    nfpGetTagInfo(&nfcHandle, &tag);

    NfpRegisterInfo reg = {};
    const bool hasReg = R_SUCCEEDED(nfpGetRegisterInfo(&nfcHandle, &reg));
    NfpModelInfo model = {};
    const bool hasModel = R_SUCCEEDED(nfpGetModelInfo(&nfcHandle, &model));
    NfpCommonInfo common = {};
    const bool hasCommon = R_SUCCEEDED(nfpGetCommonInfo(&nfcHandle, &common));
    NfpAdminInfo admin = {};
    const bool hasAdmin = R_SUCCEEDED(nfpGetAdminInfo(&nfcHandle, &admin));

    std::vector<std::uint8_t> area;
    std::uint32_t areaSize = static_cast<std::uint32_t>(hasCommon ? common.application_area_size : 0);
    // 应用存档区必须先按 access_id 打开才能读取。
    bool areaOpened = false;
    if (hasAdmin) {
        if (admin.access_id != 0 &&
            R_SUCCEEDED(nfpOpenApplicationArea(&nfcHandle, admin.access_id))) {
            areaOpened = true;
        } else if (R_SUCCEEDED(nfpOpenApplicationArea(
                       &nfcHandle, static_cast<u32>(admin.application_id)))) {
            areaOpened = true;
        }
    }
    nfcAreaOpened = areaOpened;
    if (areaOpened && areaSize > 0) {
        areaSize = std::min<std::uint32_t>(areaSize, 0x1000);
        area.assign(areaSize, 0);
        std::uint32_t outSize = 0;
        if (R_FAILED(nfpGetApplicationArea(&nfcHandle, area.data(), area.size(), &outSize))) {
            area.clear();
        } else if (outSize > 0 && outSize <= area.size()) {
            area.resize(outSize);
        } else if (outSize == 0) {
            area.clear();
        }
    }

    JsonValue amiibo = JsonValue::object();
    amiibo.setMember("uid", JsonValue::string(hexBytes(tag.uid.uid, tag.uid.uid_length)));
    amiibo.setMember("tagType", JsonValue::number(tag.tag_type));
    amiibo.setMember("protocol", JsonValue::number(tag.protocol));
    if (hasModel) {
        amiibo.setMember("characterId", JsonValue::number(model.game_character_id));
        amiibo.setMember("characterVariant", JsonValue::number(model.character_variant));
        amiibo.setMember("seriesId", JsonValue::number(model.series_id));
        amiibo.setMember("numberingId", JsonValue::number(model.numbering_id));
        amiibo.setMember("figureType", JsonValue::number(model.nfp_type));
    }
    if (hasReg) {
        amiibo.setMember("name", JsonValue::string(reg.amiibo_name));
        amiibo.setMember("firstWriteDate", JsonValue::string(dateText(reg.first_write_date)));
    }
    if (hasCommon) {
        amiibo.setMember("appAreaSize", JsonValue::number(common.application_area_size));
        amiibo.setMember("writeCounter", JsonValue::number(common.write_counter));
        amiibo.setMember("version", JsonValue::number(common.version));
        amiibo.setMember("lastWriteDate", JsonValue::string(dateText(common.last_write_date)));
    }
    if (hasAdmin) {
        amiibo.setMember("appAreaVersion", JsonValue::number(admin.application_area_version));
    }
    amiibo.setMember("appArea", JsonValue::string(hexBytes(area.data(), area.size())));

    {
        std::lock_guard<std::mutex> lock(mutex);
        nfcAmiibo = amiibo;
        JsonValue state = JsonValue::object();
        state.setMember("supported", JsonValue::boolean(true));
        state.setMember("running", JsonValue::boolean(true));
        state.setMember("state", JsonValue::string("mounted"));
        state.setMember("mounted", JsonValue::boolean(true));
        state.setMember("uid", JsonValue::string(hexBytes(tag.uid.uid, tag.uid.uid_length)));
        state.setMember("tagType", JsonValue::number(tag.tag_type));
        state.setMember("protocol", JsonValue::number(tag.protocol));
        state.setMember("amiibo", amiibo);
        nfcState = std::move(state);
    }
    return true;
}

bool HardwareManager::Impl::unmountNfc() {
    if (!nfcMounted) return false;
    nfpUnmount(&nfcHandle);
    nfcMounted = false;
    nfcAreaOpened = false;
    nfcLastPoll = 0.0;

    JsonValue state = JsonValue::object();
    state.setMember("supported", JsonValue::boolean(true));
    state.setMember("running", JsonValue::boolean(nfcDetecting));
    state.setMember("state", JsonValue::string(nfcDetecting ? "searching" : "idle"));
    state.setMember("mounted", JsonValue::boolean(false));
    std::lock_guard<std::mutex> lock(mutex);
    nfcState = std::move(state);
    return true;
}

bool HardwareManager::Impl::writeAppArea(const std::string& hexData) {
    if (!nfcMounted || !nfcAreaOpened) {
        nfcError = "amiibo not mounted or app area unavailable";
        return false;
    }
    std::vector<std::uint8_t> data;
    if (!parseHex(hexData, data) || data.empty()) {
        nfcError = "invalid hex data";
        return false;
    }

    Result rc = nfpSetApplicationArea(&nfcHandle, data.data(), data.size());
    if (R_SUCCEEDED(rc)) rc = nfpFlush(&nfcHandle);

    const bool ok = R_SUCCEEDED(rc);
    if (!ok) nfcError = "write app area failed " + resultText(rc);

    std::lock_guard<std::mutex> lock(mutex);
    JsonValue state = nfcState;
    state.setMember("writeOk", JsonValue::boolean(ok));
    state.setMember("error", JsonValue::string(ok ? "" : nfcError));
    if (ok) {
        JsonValue amiibo = nfcAmiibo;
        amiibo.setMember("appArea", JsonValue::string(hexData));
        nfcAmiibo = amiibo;
        state.setMember("amiibo", amiibo);
    }
    nfcState = std::move(state);
    return ok;
}

#endif  // __SWITCH__

}  // namespace miniapp
