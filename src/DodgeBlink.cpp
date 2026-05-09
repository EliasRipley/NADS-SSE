#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

SKSEPluginInfo(
    .Version = REL::Version(1, 8, 2, 0),
    .Name = "DodgeBlinkShout",
    .Author = "EliasRipley",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = REL::Version(2, 2, 6, 0)
)

namespace DodgeBlink
{
    enum class DirectionSource : std::int32_t
    {
        kFacing = 0,
        kPlayerControls = 1,
        kLatchedInput = 2
    };

    enum class ActivationMode : std::int32_t
    {
        kShout = 0,
        kSpell = 1,
        kHotkey = 2
    };

    enum class TakeoverStyle : std::int32_t
    {
        kTkDefaultFullRoll = 0,
        kStepAndForwardRoll = 1,
        kFullStep = 2
    };

    enum class TakeoverProvider : std::int32_t
    {
        kTkDodge = 0,
        kDmco = 1
    };

    enum class DmcoTakeoverStyle : std::int32_t
    {
        kSet1 = 0,
        kSet2 = 1
    };

    struct TierConfig
    {
        float range{ 350.0f };
        float cooldownSeconds{ 2.5f };
        float dashDurationSeconds{ 0.30f };
    };

    struct Config
    {
        // index 1..3 used
        std::array<TierConfig, 4> tier{};
        float maxDistanceCap{ 2020.0f };
        float inputThreshold{ 25.0f };
        float inputYSign{ 1.0f };
        std::int32_t inputLatchWindowMs{ 250 };
        float dashMaxSpeed{ 580.0f };
        std::int32_t animHooksEnabled{ 0 };
        std::string animStartEvent{};
        std::string animStopEvent{};
        std::string animStateVar{ "bDodgeBlinking" };
        std::string animTierVar{ "iDodgeBlinkTier" };
        std::int32_t tpAnimFrameworkEnabled{ 1 };
        std::int32_t tpAnimThirdPersonOnly{ 1 };
        std::string tpAnimForwardEvent{};
        std::string tpAnimBackwardEvent{};
        std::string tpAnimLeftEvent{};
        std::string tpAnimRightEvent{};
        std::string tpAnimStopEvent{};
        std::string tpAnimStateVar{ "bNADSDodging" };
        std::string tpAnimTierVar{ "iNADSDodgeTier" };
        std::string tpAnimDirectionVar{ "iNADSDodgeDirection" };
        std::string tpAnimAngleVar{ "fNADSDodgeAngle" };
        std::string tpAnimSpeedVar{ "fNADSDodgeSpeed" };
        float tpAnimSpeedScale{ 1.0f };
        std::int32_t tpTravelProfileEnabled{ 0 };
        float tpDurationScale{ 1.0f };
        float tpMinDuration{ 0.30f };
        float tpMaxSpeed{ 580.0f };
        float tpTierDuration1{ 0.30f };
        float tpTierDuration2{ 0.38f };
        float tpTierDuration3{ 0.46f };
        float tpRecoveryExtraSeconds{ 0.00f };
        float tpCatchupStrength{ 0.45f };
        float tpCatchupStepScaleMax{ 1.35f };
        float tpRecoverySpeedScale{ 0.75f };
        std::int32_t tpSuppressVanillaShoutAnim{ 1 };
        std::string tpSuppressVanillaShoutEvent{ "IdleForceDefaultState" };
        std::int32_t tpImmediateShoutTriggerEnabled{ 0 };
        std::int32_t dashInvisibilityFxEnabled{ 1 };
        std::int32_t dashInvisibilityFxThirdPersonOnly{ 1 };
        float dashInvisibilityFadeOutSeconds{ 0.06f };
        float dashInvisibilityFadeInSeconds{ 0.12f };
        float dashInvisibilityRefraction{ 0.35f };
        std::int32_t dodgeIFramesEnabled{ 1 };
        std::int32_t activationMode{ static_cast<std::int32_t>(ActivationMode::kHotkey) };
        std::int32_t allowMultipleActivationTypes{ 0 };
        std::int32_t takeoverEnabled{ 0 };
        std::int32_t takeoverProvider{ static_cast<std::int32_t>(TakeoverProvider::kTkDodge) };
        std::int32_t takeoverAllowSheathed{ 0 };
        std::int32_t takeoverStyle{ static_cast<std::int32_t>(TakeoverStyle::kTkDefaultFullRoll) };
        std::int32_t dmcoTakeoverStyle{ static_cast<std::int32_t>(DmcoTakeoverStyle::kSet1) };
        float hotkeyStaminaCost{ 20.0f };
        std::array<float, 4> spellMagickaCost{ 0.0f, 20.0f, 25.0f, 30.0f };
        std::array<float, 4> shoutCooldownSeconds{ 0.0f, 3.0f, 4.0f, 5.0f };
        std::int32_t hotkeyTier{ 1 };
        std::int32_t hotkeyCode{ -1 };
        std::int32_t hotkeyTapMaxMs{ 200 };
    };

    static Config g_config = []() {
        Config cfg{};
        cfg.tier[1] = TierConfig{ 175.0f, 1.25f, 0.30f };
        cfg.tier[2] = TierConfig{ 275.0f, 2.50f, 0.38f };
        cfg.tier[3] = TierConfig{ 375.0f, 4.00f, 0.46f };
        return cfg;
    }();

    static std::array<std::array<std::chrono::steady_clock::time_point, 4>, 3> g_lastUsedByMode = {{
        {
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min()
        },
        {
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min()
        },
        {
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min()
        }
    }};

    static std::mutex g_cooldownMutex{};
    static std::mutex g_configMutex{};
    static std::filesystem::path g_nativeTracePath = std::filesystem::path("Data") / "SKSE" / "Plugins" / "DodgeBlinkShout.native.log";
    static std::mutex g_nativeTraceMutex{};
    static std::atomic_bool g_dashGate{ false };
    static std::mutex g_dashIFrameMutex{};
    static bool g_dashIFrameApplied{ false };
    static float g_dashIFrameProtectedHealth{ 0.0f };
    static float g_dashIFrameLowestHealth{ 0.0f };
    static bool g_dashIFrameRestoreGhost{ false };
    static RE::ObjectRefHandle g_dashIFrameActorHandle{};
    static std::int32_t g_dashIFrameTier{ 0 };
    static ActivationMode g_dashIFrameMode{ ActivationMode::kHotkey };
    static std::int32_t g_dashIFrameHitCount{ 0 };
    static std::int32_t g_dashIFrameNoDamageHitCount{ 0 };
    static std::int32_t g_dashIFrameHealthDeltaHitCount{ 0 };
    static std::atomic_bool g_activationFormsReady{ false };
    static std::atomic_bool g_activationGrantThreadRunning{ false };
    static std::atomic_bool g_activationFormsPendingLogged{ false };
    static std::mutex g_activationEnsureMutex{};
    static thread_local DirectionSource g_lastDirectionSource = DirectionSource::kFacing;
    static thread_local std::optional<ActivationMode> g_forcedActivationMode{};
    static std::mutex g_inputLatchMutex{};
    static std::optional<RE::NiPoint3> g_lastLatchedWorldDirection{};
    static std::chrono::steady_clock::time_point g_lastLatchedAt = std::chrono::steady_clock::time_point::min();
    static std::atomic_bool g_shoutHandlerHookInstalled{ false };
    static std::mutex g_hotkeyPressMutex{};
    static bool g_hotkeyPressArmed{ false };
    static std::int32_t g_hotkeyPressDevice{ -1 };
    static std::int32_t g_hotkeyPressIdCode{ -1 };
    static std::chrono::steady_clock::time_point g_hotkeyPressStartedAt = std::chrono::steady_clock::time_point::min();
    static constexpr RE::FormID kInvisibilityBodyArtFormID = 0x000339C8;
    static constexpr RE::FormID kInvisibilityShaderFormID = 0x0002DF92;
    static constexpr float kDashInvisibilityBurstDurationSeconds = 0.35f;

    static void DoBlinkTier(RE::StaticFunctionTag*, RE::Actor* actor, std::int32_t tier);
    static void DoBlinkTierForMode(RE::StaticFunctionTag*, RE::Actor* actor, std::int32_t tier, std::int32_t activationMode);
    static bool IsFirstPersonForActor(RE::Actor* actor);
    static bool IsNadsShoutEquipped(RE::Actor* actor);
    static RE::TESWordOfPower* GetNadsShoutWord(RE::StaticFunctionTag*, std::int32_t wordIndex);
    static void AppendNativeTrace(const std::string& msg);
    static const char* ActivationModeName(ActivationMode mode);
    class InputEventSink final : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override;
    };

    class HitEventSink final : public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override;
    };

    class ShoutHandlerHook final
    {
    public:
        static void Install();

    private:
        static bool CanProcess(RE::ShoutHandler* a_this, RE::InputEvent* a_event);
        static void ProcessButton(RE::ShoutHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data);
        static inline REL::Relocation<decltype(CanProcess)> _canProcess;
        static inline REL::Relocation<decltype(ProcessButton)> _processButton;
    };

    static InputEventSink g_inputEventSink{};
    static std::atomic_bool g_inputSinkRegistered{ false };
    static HitEventSink g_hitEventSink{};
    static std::atomic_bool g_hitSinkRegistered{ false };

    template <class T>
    static T Clamp(const T& value, const T& minValue, const T& maxValue)
    {
        return std::max(minValue, std::min(maxValue, value));
    }

    static std::string Trim(std::string value)
    {
        auto isSpace = [](unsigned char c) {
            return std::isspace(c) != 0;
        };

        while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    }

    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static Config GetConfigSnapshot()
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        return g_config;
    }

    static void StoreConfigSnapshot(const Config& cfg)
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        g_config = cfg;
        logger::info("DodgeBlink: Config loaded - tpAnimFrameworkEnabled={}", cfg.tpAnimFrameworkEnabled);
    }

    static std::string NormalizeConfigKey(std::string key)
    {
        return ToLower(Trim(std::move(key)));
    }

    static void ClampConfigValues(Config& cfg)
    {
        cfg.maxDistanceCap = Clamp(cfg.maxDistanceCap, 64.0f, 2020.0f);
        cfg.inputThreshold = Clamp(cfg.inputThreshold, 0.0f, 600.0f);
        cfg.inputYSign = Clamp(cfg.inputYSign, -1.0f, 1.0f);
        cfg.inputLatchWindowMs = Clamp(cfg.inputLatchWindowMs, 0, 2000);
        cfg.dashMaxSpeed = Clamp(cfg.dashMaxSpeed, 120.0f, 2400.0f);
        cfg.animHooksEnabled = Clamp(cfg.animHooksEnabled, 0, 1);
        cfg.tpAnimFrameworkEnabled = Clamp(cfg.tpAnimFrameworkEnabled, 0, 1);
        cfg.tpAnimThirdPersonOnly = Clamp(cfg.tpAnimThirdPersonOnly, 0, 1);
        cfg.tpAnimSpeedScale = Clamp(cfg.tpAnimSpeedScale, 0.10f, 4.00f);
        cfg.tpTravelProfileEnabled = Clamp(cfg.tpTravelProfileEnabled, 0, 1);
        cfg.tpDurationScale = Clamp(cfg.tpDurationScale, 0.50f, 3.00f);
        cfg.tpMinDuration = Clamp(cfg.tpMinDuration, 0.10f, 2.00f);
        cfg.tpMaxSpeed = Clamp(cfg.tpMaxSpeed, 120.0f, 2400.0f);
        cfg.tpTierDuration1 = Clamp(cfg.tpTierDuration1, 0.10f, 2.00f);
        cfg.tpTierDuration2 = Clamp(cfg.tpTierDuration2, 0.10f, 2.00f);
        cfg.tpTierDuration3 = Clamp(cfg.tpTierDuration3, 0.10f, 2.00f);
        cfg.tpRecoveryExtraSeconds = Clamp(cfg.tpRecoveryExtraSeconds, 0.00f, 2.00f);
        cfg.tpCatchupStrength = Clamp(cfg.tpCatchupStrength, 0.00f, 2.00f);
        cfg.tpCatchupStepScaleMax = Clamp(cfg.tpCatchupStepScaleMax, 1.00f, 4.00f);
        cfg.tpRecoverySpeedScale = Clamp(cfg.tpRecoverySpeedScale, 0.10f, 2.00f);
        cfg.tpSuppressVanillaShoutAnim = Clamp(cfg.tpSuppressVanillaShoutAnim, 0, 1);
        cfg.tpImmediateShoutTriggerEnabled = Clamp(cfg.tpImmediateShoutTriggerEnabled, 0, 1);
        cfg.dashInvisibilityFxEnabled = Clamp(cfg.dashInvisibilityFxEnabled, 0, 1);
        cfg.dashInvisibilityFxThirdPersonOnly = Clamp(cfg.dashInvisibilityFxThirdPersonOnly, 0, 1);
        cfg.dashInvisibilityFadeOutSeconds = Clamp(cfg.dashInvisibilityFadeOutSeconds, 0.00f, 1.00f);
        cfg.dashInvisibilityFadeInSeconds = Clamp(cfg.dashInvisibilityFadeInSeconds, 0.00f, 1.00f);
        cfg.dashInvisibilityRefraction = Clamp(cfg.dashInvisibilityRefraction, 0.00f, 1.00f);
        cfg.dodgeIFramesEnabled = Clamp(cfg.dodgeIFramesEnabled, 0, 1);
        cfg.activationMode = Clamp(cfg.activationMode, 0, 2);
        cfg.allowMultipleActivationTypes = Clamp(cfg.allowMultipleActivationTypes, 0, 1);
        cfg.takeoverEnabled = Clamp(cfg.takeoverEnabled, 0, 1);
        cfg.takeoverProvider = Clamp(cfg.takeoverProvider, 0, 1);
        cfg.takeoverAllowSheathed = Clamp(cfg.takeoverAllowSheathed, 0, 1);
        cfg.takeoverStyle = Clamp(cfg.takeoverStyle, 0, 2);
        cfg.dmcoTakeoverStyle = Clamp(cfg.dmcoTakeoverStyle, 0, 1);
        if (cfg.takeoverEnabled != 0) {
            cfg.activationMode = static_cast<std::int32_t>(ActivationMode::kHotkey);
            cfg.allowMultipleActivationTypes = 0;
        }
        cfg.hotkeyStaminaCost = Clamp(cfg.hotkeyStaminaCost, 0.0f, 500.0f);
        cfg.hotkeyTier = Clamp(cfg.hotkeyTier, 1, 3);
        cfg.hotkeyCode = Clamp(cfg.hotkeyCode, -1, 512);
        cfg.hotkeyTapMaxMs = Clamp(cfg.hotkeyTapMaxMs, 50, 1000);
        if (std::abs(cfg.inputYSign) < 0.5f) {
            cfg.inputYSign = 1.0f;
        }

        for (std::int32_t tier = 1; tier <= 3; ++tier) {
            cfg.tier[tier].range = Clamp(cfg.tier[tier].range, 32.0f, cfg.maxDistanceCap);
            cfg.tier[tier].cooldownSeconds = Clamp(cfg.tier[tier].cooldownSeconds, 0.0f, 120.0f);
            cfg.tier[tier].dashDurationSeconds = Clamp(cfg.tier[tier].dashDurationSeconds, 0.10f, 2.00f);
            cfg.spellMagickaCost[tier] = Clamp(cfg.spellMagickaCost[tier], 0.0f, 500.0f);
            cfg.shoutCooldownSeconds[tier] = Clamp(cfg.shoutCooldownSeconds[tier], 0.0f, 120.0f);
        }
    }

    static bool SetFloatConfigByKey(Config& cfg, const std::string& key, float value)
    {
        if (key == "dist1" || key == "range1") {
            cfg.tier[1].range = value;
            return true;
        }
        if (key == "dist2" || key == "range2") {
            cfg.tier[2].range = value;
            return true;
        }
        if (key == "dist3" || key == "range3") {
            cfg.tier[3].range = value;
            return true;
        }
        if (key == "cd1" || key == "cooldown1") {
            cfg.tier[1].cooldownSeconds = value;
            return true;
        }
        if (key == "cd2" || key == "cooldown2") {
            cfg.tier[2].cooldownSeconds = value;
            return true;
        }
        if (key == "cd3" || key == "cooldown3") {
            cfg.tier[3].cooldownSeconds = value;
            return true;
        }
        if (key == "dashtime1") {
            cfg.tier[1].dashDurationSeconds = value;
            return true;
        }
        if (key == "dashtime2") {
            cfg.tier[2].dashDurationSeconds = value;
            return true;
        }
        if (key == "dashtime3") {
            cfg.tier[3].dashDurationSeconds = value;
            return true;
        }
        if (key == "maxdistancecap") {
            cfg.maxDistanceCap = value;
            return true;
        }
        if (key == "inputthreshold") {
            cfg.inputThreshold = value;
            return true;
        }
        if (key == "inputysign") {
            cfg.inputYSign = value;
            return true;
        }
        if (key == "dashmaxspeed") {
            cfg.dashMaxSpeed = value;
            return true;
        }
        if (key == "tpanimspeedscale") {
            cfg.tpAnimSpeedScale = value;
            return true;
        }
        if (key == "tpdurationscale") {
            cfg.tpDurationScale = value;
            return true;
        }
        if (key == "tpminduration") {
            cfg.tpMinDuration = value;
            return true;
        }
        if (key == "tpmaxspeed") {
            cfg.tpMaxSpeed = value;
            return true;
        }
        if (key == "tptierduration1") {
            cfg.tpTierDuration1 = value;
            return true;
        }
        if (key == "tptierduration2") {
            cfg.tpTierDuration2 = value;
            return true;
        }
        if (key == "tptierduration3") {
            cfg.tpTierDuration3 = value;
            return true;
        }
        if (key == "tprecoveryextraseconds") {
            cfg.tpRecoveryExtraSeconds = value;
            return true;
        }
        if (key == "tpcatchupstrength") {
            cfg.tpCatchupStrength = value;
            return true;
        }
        if (key == "tpcatchupstepscalemax") {
            cfg.tpCatchupStepScaleMax = value;
            return true;
        }
        if (key == "tprecoveryspeedscale") {
            cfg.tpRecoverySpeedScale = value;
            return true;
        }
        if (key == "dashinvisibilityfadeoutseconds") {
            cfg.dashInvisibilityFadeOutSeconds = value;
            return true;
        }
        if (key == "dashinvisibilityfadeinseconds") {
            cfg.dashInvisibilityFadeInSeconds = value;
            return true;
        }
        if (key == "dashinvisibilityrefraction") {
            cfg.dashInvisibilityRefraction = value;
            return true;
        }
        if (key == "hotkeystaminacost") {
            cfg.hotkeyStaminaCost = value;
            return true;
        }
        if (key == "spellmagickacost1" || key == "spellcost1") {
            cfg.spellMagickaCost[1] = value;
            return true;
        }
        if (key == "spellmagickacost2" || key == "spellcost2") {
            cfg.spellMagickaCost[2] = value;
            return true;
        }
        if (key == "spellmagickacost3" || key == "spellcost3") {
            cfg.spellMagickaCost[3] = value;
            return true;
        }
        if (key == "shoutcooldown1" || key == "shoutcd1") {
            cfg.shoutCooldownSeconds[1] = value;
            return true;
        }
        if (key == "shoutcooldown2" || key == "shoutcd2") {
            cfg.shoutCooldownSeconds[2] = value;
            return true;
        }
        if (key == "shoutcooldown3" || key == "shoutcd3") {
            cfg.shoutCooldownSeconds[3] = value;
            return true;
        }
        return false;
    }

    static bool SetIntConfigByKey(Config& cfg, const std::string& key, std::int32_t value)
    {
        if (key == "inputlatchwindowms") {
            cfg.inputLatchWindowMs = value;
            return true;
        }
        if (key == "animhooksenabled") {
            cfg.animHooksEnabled = value;
            return true;
        }
        if (key == "tpanimframeworkenabled") {
            cfg.tpAnimFrameworkEnabled = value;
            return true;
        }
        if (key == "tpanimthirdpersononly") {
            cfg.tpAnimThirdPersonOnly = value;
            return true;
        }
        if (key == "tptravelprofileenabled") {
            cfg.tpTravelProfileEnabled = value;
            return true;
        }
        if (key == "tpsuppressvanillashoutanim") {
            cfg.tpSuppressVanillaShoutAnim = value;
            return true;
        }
        if (key == "tpimmediateshouttriggerenabled") {
            cfg.tpImmediateShoutTriggerEnabled = value;
            return true;
        }
        if (key == "dashinvisibilityfxenabled") {
            cfg.dashInvisibilityFxEnabled = value;
            return true;
        }
        if (key == "dashinvisibilityfxthirdpersononly") {
            cfg.dashInvisibilityFxThirdPersonOnly = value;
            return true;
        }
        if (key == "dodgeiframesenabled") {
            cfg.dodgeIFramesEnabled = value;
            return true;
        }
        if (key == "activationmode") {
            cfg.activationMode = value;
            return true;
        }
        if (key == "allowmultipleactivationtypes" || key == "allowmultipletypes") {
            cfg.allowMultipleActivationTypes = value;
            return true;
        }
        if (key == "takeoverenabled") {
            cfg.takeoverEnabled = value;
            return true;
        }
        if (key == "takeoverprovider") {
            cfg.takeoverProvider = value;
            return true;
        }
        if (key == "takeoverallowsheathed") {
            cfg.takeoverAllowSheathed = value;
            return true;
        }
        if (key == "takeoverstyle") {
            cfg.takeoverStyle = value;
            return true;
        }
        if (key == "dmcotakeoverstyle") {
            cfg.dmcoTakeoverStyle = value;
            return true;
        }
        if (key == "hotkeytier") {
            cfg.hotkeyTier = value;
            return true;
        }
        if (key == "hotkeycode") {
            cfg.hotkeyCode = value;
            return true;
        }
        if (key == "hotkeytapmaxms") {
            cfg.hotkeyTapMaxMs = value;
            return true;
        }
        return false;
    }

    static std::optional<float> GetFloatConfigByKey(const Config& cfg, const std::string& key)
    {
        if (key == "dist1" || key == "range1") {
            return cfg.tier[1].range;
        }
        if (key == "dist2" || key == "range2") {
            return cfg.tier[2].range;
        }
        if (key == "dist3" || key == "range3") {
            return cfg.tier[3].range;
        }
        if (key == "cd1" || key == "cooldown1") {
            return cfg.tier[1].cooldownSeconds;
        }
        if (key == "cd2" || key == "cooldown2") {
            return cfg.tier[2].cooldownSeconds;
        }
        if (key == "cd3" || key == "cooldown3") {
            return cfg.tier[3].cooldownSeconds;
        }
        if (key == "dashtime1") {
            return cfg.tier[1].dashDurationSeconds;
        }
        if (key == "dashtime2") {
            return cfg.tier[2].dashDurationSeconds;
        }
        if (key == "dashtime3") {
            return cfg.tier[3].dashDurationSeconds;
        }
        if (key == "maxdistancecap") {
            return cfg.maxDistanceCap;
        }
        if (key == "inputthreshold") {
            return cfg.inputThreshold;
        }
        if (key == "inputysign") {
            return cfg.inputYSign;
        }
        if (key == "dashmaxspeed") {
            return cfg.dashMaxSpeed;
        }
        if (key == "tpanimspeedscale") {
            return cfg.tpAnimSpeedScale;
        }
        if (key == "tpdurationscale") {
            return cfg.tpDurationScale;
        }
        if (key == "tpminduration") {
            return cfg.tpMinDuration;
        }
        if (key == "tpmaxspeed") {
            return cfg.tpMaxSpeed;
        }
        if (key == "tptierduration1") {
            return cfg.tpTierDuration1;
        }
        if (key == "tptierduration2") {
            return cfg.tpTierDuration2;
        }
        if (key == "tptierduration3") {
            return cfg.tpTierDuration3;
        }
        if (key == "tprecoveryextraseconds") {
            return cfg.tpRecoveryExtraSeconds;
        }
        if (key == "tpcatchupstrength") {
            return cfg.tpCatchupStrength;
        }
        if (key == "tpcatchupstepscalemax") {
            return cfg.tpCatchupStepScaleMax;
        }
        if (key == "tprecoveryspeedscale") {
            return cfg.tpRecoverySpeedScale;
        }
        if (key == "dashinvisibilityfadeoutseconds") {
            return cfg.dashInvisibilityFadeOutSeconds;
        }
        if (key == "dashinvisibilityfadeinseconds") {
            return cfg.dashInvisibilityFadeInSeconds;
        }
        if (key == "dashinvisibilityrefraction") {
            return cfg.dashInvisibilityRefraction;
        }
        if (key == "hotkeystaminacost") {
            return cfg.hotkeyStaminaCost;
        }
        if (key == "spellmagickacost1" || key == "spellcost1") {
            return cfg.spellMagickaCost[1];
        }
        if (key == "spellmagickacost2" || key == "spellcost2") {
            return cfg.spellMagickaCost[2];
        }
        if (key == "spellmagickacost3" || key == "spellcost3") {
            return cfg.spellMagickaCost[3];
        }
        if (key == "shoutcooldown1" || key == "shoutcd1") {
            return cfg.shoutCooldownSeconds[1];
        }
        if (key == "shoutcooldown2" || key == "shoutcd2") {
            return cfg.shoutCooldownSeconds[2];
        }
        if (key == "shoutcooldown3" || key == "shoutcd3") {
            return cfg.shoutCooldownSeconds[3];
        }
        return std::nullopt;
    }

    static std::optional<std::int32_t> GetIntConfigByKey(const Config& cfg, const std::string& key)
    {
        if (key == "inputlatchwindowms") {
            return cfg.inputLatchWindowMs;
        }
        if (key == "animhooksenabled") {
            return cfg.animHooksEnabled;
        }
        if (key == "tpanimframeworkenabled") {
            return cfg.tpAnimFrameworkEnabled;
        }
        if (key == "tpanimthirdpersononly") {
            return cfg.tpAnimThirdPersonOnly;
        }
        if (key == "tptravelprofileenabled") {
            return cfg.tpTravelProfileEnabled;
        }
        if (key == "tpsuppressvanillashoutanim") {
            return cfg.tpSuppressVanillaShoutAnim;
        }
        if (key == "tpimmediateshouttriggerenabled") {
            return cfg.tpImmediateShoutTriggerEnabled;
        }
        if (key == "dashinvisibilityfxenabled") {
            return cfg.dashInvisibilityFxEnabled;
        }
        if (key == "dashinvisibilityfxthirdpersononly") {
            return cfg.dashInvisibilityFxThirdPersonOnly;
        }
        if (key == "dodgeiframesenabled") {
            return cfg.dodgeIFramesEnabled;
        }
        if (key == "activationmode") {
            return cfg.activationMode;
        }
        if (key == "allowmultipleactivationtypes" || key == "allowmultipletypes") {
            return cfg.allowMultipleActivationTypes;
        }
        if (key == "takeoverenabled") {
            return cfg.takeoverEnabled;
        }
        if (key == "takeoverprovider") {
            return cfg.takeoverProvider;
        }
        if (key == "takeoverallowsheathed") {
            return cfg.takeoverAllowSheathed;
        }
        if (key == "takeoverstyle") {
            return cfg.takeoverStyle;
        }
        if (key == "dmcotakeoverstyle") {
            return cfg.dmcoTakeoverStyle;
        }
        if (key == "hotkeytier") {
            return cfg.hotkeyTier;
        }
        if (key == "hotkeycode") {
            return cfg.hotkeyCode;
        }
        if (key == "hotkeytapmaxms") {
            return cfg.hotkeyTapMaxMs;
        }
        return std::nullopt;
    }

    static std::filesystem::path GetIniPath()
    {
        return std::filesystem::path("Data") / "SKSE" / "Plugins" / "DodgeBlinkShout.ini";
    }

    static bool SaveConfigToIni()
    {
        const Config cfg = GetConfigSnapshot();
        const auto iniPath = GetIniPath();
        std::error_code ec;
        std::filesystem::create_directories(iniPath.parent_path(), ec);

        std::ofstream out(iniPath, std::ios::trunc);
        if (!out.is_open()) {
            logger::warn("DodgeBlink: could not write INI at {}", iniPath.string());
            return false;
        }

        out << std::fixed << std::setprecision(3);
        out << "[DodgeBlink]\n";
        out << "Dist1=" << cfg.tier[1].range << "\n";
        out << "Dist2=" << cfg.tier[2].range << "\n";
        out << "Dist3=" << cfg.tier[3].range << "\n";
        out << "CD1=" << cfg.tier[1].cooldownSeconds << "\n";
        out << "CD2=" << cfg.tier[2].cooldownSeconds << "\n";
        out << "CD3=" << cfg.tier[3].cooldownSeconds << "\n";
        out << "DashTime1=" << cfg.tier[1].dashDurationSeconds << "\n";
        out << "DashTime2=" << cfg.tier[2].dashDurationSeconds << "\n";
        out << "DashTime3=" << cfg.tier[3].dashDurationSeconds << "\n";
        out << "MaxDistanceCap=" << cfg.maxDistanceCap << "\n";
        out << "InputThreshold=" << cfg.inputThreshold << "\n";
        out << "InputYSign=" << cfg.inputYSign << "\n";
        out << "InputLatchWindowMs=" << cfg.inputLatchWindowMs << "\n";
        out << "DashMaxSpeed=" << cfg.dashMaxSpeed << "\n";
        out << "AnimHooksEnabled=" << cfg.animHooksEnabled << "\n";
        out << "AnimStartEvent=" << cfg.animStartEvent << "\n";
        out << "AnimStopEvent=" << cfg.animStopEvent << "\n";
        out << "AnimStateVar=" << cfg.animStateVar << "\n";
        out << "AnimTierVar=" << cfg.animTierVar << "\n";
        out << "TPAnimFrameworkEnabled=" << cfg.tpAnimFrameworkEnabled << "\n";
        out << "TPAnimThirdPersonOnly=" << cfg.tpAnimThirdPersonOnly << "\n";
        out << "TPAnimForwardEvent=" << cfg.tpAnimForwardEvent << "\n";
        out << "TPAnimBackwardEvent=" << cfg.tpAnimBackwardEvent << "\n";
        out << "TPAnimLeftEvent=" << cfg.tpAnimLeftEvent << "\n";
        out << "TPAnimRightEvent=" << cfg.tpAnimRightEvent << "\n";
        out << "TPAnimStopEvent=" << cfg.tpAnimStopEvent << "\n";
        out << "TPAnimStateVar=" << cfg.tpAnimStateVar << "\n";
        out << "TPAnimTierVar=" << cfg.tpAnimTierVar << "\n";
        out << "TPAnimDirectionVar=" << cfg.tpAnimDirectionVar << "\n";
        out << "TPAnimAngleVar=" << cfg.tpAnimAngleVar << "\n";
        out << "TPAnimSpeedVar=" << cfg.tpAnimSpeedVar << "\n";
        out << "TPAnimSpeedScale=" << cfg.tpAnimSpeedScale << "\n";
        out << "TPTravelProfileEnabled=" << cfg.tpTravelProfileEnabled << "\n";
        out << "TPDurationScale=" << cfg.tpDurationScale << "\n";
        out << "TPMinDuration=" << cfg.tpMinDuration << "\n";
        out << "TPTierDuration1=" << cfg.tpTierDuration1 << "\n";
        out << "TPTierDuration2=" << cfg.tpTierDuration2 << "\n";
        out << "TPTierDuration3=" << cfg.tpTierDuration3 << "\n";
        out << "TPRecoveryExtraSeconds=" << cfg.tpRecoveryExtraSeconds << "\n";
        out << "TPMaxSpeed=" << cfg.tpMaxSpeed << "\n";
        out << "TPCatchupStrength=" << cfg.tpCatchupStrength << "\n";
        out << "TPCatchupStepScaleMax=" << cfg.tpCatchupStepScaleMax << "\n";
        out << "TPRecoverySpeedScale=" << cfg.tpRecoverySpeedScale << "\n";
        out << "TPSuppressVanillaShoutAnim=" << cfg.tpSuppressVanillaShoutAnim << "\n";
        out << "TPSuppressVanillaShoutEvent=" << cfg.tpSuppressVanillaShoutEvent << "\n";
        out << "TPImmediateShoutTriggerEnabled=" << cfg.tpImmediateShoutTriggerEnabled << "\n";
        out << "DashInvisibilityFxEnabled=" << cfg.dashInvisibilityFxEnabled << "\n";
        out << "DashInvisibilityFxThirdPersonOnly=" << cfg.dashInvisibilityFxThirdPersonOnly << "\n";
        out << "DashInvisibilityFadeOutSeconds=" << cfg.dashInvisibilityFadeOutSeconds << "\n";
        out << "DashInvisibilityFadeInSeconds=" << cfg.dashInvisibilityFadeInSeconds << "\n";
        out << "DashInvisibilityRefraction=" << cfg.dashInvisibilityRefraction << "\n";
        out << "DodgeIFramesEnabled=" << cfg.dodgeIFramesEnabled << "\n";
        out << "ActivationMode=" << cfg.activationMode << "\n";
        out << "AllowMultipleActivationTypes=" << cfg.allowMultipleActivationTypes << "\n";
        out << "TakeoverEnabled=" << cfg.takeoverEnabled << "\n";
        out << "TakeoverProvider=" << cfg.takeoverProvider << "\n";
        out << "TakeoverAllowSheathed=" << cfg.takeoverAllowSheathed << "\n";
        out << "TakeoverStyle=" << cfg.takeoverStyle << "\n";
        out << "DmcoTakeoverStyle=" << cfg.dmcoTakeoverStyle << "\n";
        out << "HotkeyStaminaCost=" << cfg.hotkeyStaminaCost << "\n";
        out << "SpellMagickaCost1=" << cfg.spellMagickaCost[1] << "\n";
        out << "SpellMagickaCost2=" << cfg.spellMagickaCost[2] << "\n";
        out << "SpellMagickaCost3=" << cfg.spellMagickaCost[3] << "\n";
        out << "ShoutCooldown1=" << cfg.shoutCooldownSeconds[1] << "\n";
        out << "ShoutCooldown2=" << cfg.shoutCooldownSeconds[2] << "\n";
        out << "ShoutCooldown3=" << cfg.shoutCooldownSeconds[3] << "\n";
        out << "HotkeyTier=" << cfg.hotkeyTier << "\n";
        out << "HotkeyCode=" << cfg.hotkeyCode << "\n";
        out << "HotkeyTapMaxMs=" << cfg.hotkeyTapMaxMs << "\n";

        return true;
    }

    static bool IsTakeoverEnabled(const Config& cfg)
    {
        return Clamp(cfg.takeoverEnabled, 0, 1) != 0;
    }

    static TakeoverProvider ResolveTakeoverProvider(const Config& cfg)
    {
        return static_cast<TakeoverProvider>(Clamp(cfg.takeoverProvider, 0, 1));
    }

    static bool IsTakeoverAllowSheathedEnabled(const Config& cfg)
    {
        return Clamp(cfg.takeoverAllowSheathed, 0, 1) != 0;
    }

    static bool TryReadGraphFlagTruthy(RE::Actor* actor, const RE::BSFixedString& variableName, bool& truthyOut)
    {
        truthyOut = false;
        if (!actor) {
            return false;
        }

        std::int32_t intValue = 0;
        if (actor->GetGraphVariableInt(variableName, intValue)) {
            truthyOut = intValue != 0;
            return true;
        }

        float floatValue = 0.0f;
        if (actor->GetGraphVariableFloat(variableName, floatValue)) {
            truthyOut = std::abs(floatValue) > 0.001f;
            return true;
        }

        bool boolValue = false;
        if (actor->GetGraphVariableBool(variableName, boolValue)) {
            truthyOut = boolValue;
            return true;
        }

        return false;
    }

    static bool IsDmcoGraphBusyForNewDodge(RE::Actor* actor, const char*& reasonOut)
    {
        reasonOut = "";
        if (!actor) {
            reasonOut = "actor_null";
            return true;
        }

        bool truthy = false;
        static const RE::BSFixedString kDmcoAlwaysOnVar("MCO_DodgeAlwaysOn");
        if (TryReadGraphFlagTruthy(actor, kDmcoAlwaysOnVar, truthy) && truthy) {
            reasonOut = "MCO_DodgeAlwaysOn";
            return true;
        }

        static const RE::BSFixedString kDmcoRecoveryVar("MCO_IsInRecovery");
        if (TryReadGraphFlagTruthy(actor, kDmcoRecoveryVar, truthy) && truthy) {
            reasonOut = "MCO_IsInRecovery";
            return true;
        }

        return false;
    }

    struct DmcoGraphStateSnapshot
    {
        bool hasAlwaysOn{ false };
        bool alwaysOn{ false };
        bool hasRecovery{ false };
        bool recovery{ false };
    };

    static DmcoGraphStateSnapshot CaptureDmcoGraphState(RE::Actor* actor)
    {
        DmcoGraphStateSnapshot snapshot{};
        if (!actor) {
            return snapshot;
        }

        static const RE::BSFixedString kDmcoAlwaysOnVar("MCO_DodgeAlwaysOn");
        static const RE::BSFixedString kDmcoRecoveryVar("MCO_IsInRecovery");
        snapshot.hasAlwaysOn = TryReadGraphFlagTruthy(actor, kDmcoAlwaysOnVar, snapshot.alwaysOn);
        snapshot.hasRecovery = TryReadGraphFlagTruthy(actor, kDmcoRecoveryVar, snapshot.recovery);
        return snapshot;
    }

    static bool QueryDmcoGraphStateOnGameThread(std::chrono::milliseconds timeout, DmcoGraphStateSnapshot& snapshotOut)
    {
        auto* task = SKSE::GetTaskInterface();
        if (!task) {
            return false;
        }

        auto promise = std::make_shared<std::promise<DmcoGraphStateSnapshot>>();
        auto future = promise->get_future();
        task->AddTask([promise]() {
            DmcoGraphStateSnapshot snapshot{};
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                snapshot = CaptureDmcoGraphState(player);
            }
            try {
                promise->set_value(snapshot);
            } catch (...) {
            }
        });

        if (future.wait_for(timeout) != std::future_status::ready) {
            return false;
        }
        snapshotOut = future.get();
        return true;
    }

    static bool IsDmcoGraphBusy(const DmcoGraphStateSnapshot& snapshot)
    {
        if (snapshot.hasAlwaysOn) {
            return snapshot.alwaysOn;
        }
        if (snapshot.hasRecovery) {
            return snapshot.recovery;
        }
        return false;
    }

    static TakeoverStyle ResolveTakeoverStyle(const Config& cfg)
    {
        return static_cast<TakeoverStyle>(Clamp(cfg.takeoverStyle, 0, 2));
    }

    static DmcoTakeoverStyle ResolveDmcoTakeoverStyle(const Config& cfg)
    {
        return static_cast<DmcoTakeoverStyle>(Clamp(cfg.dmcoTakeoverStyle, 0, 1));
    }

    static ActivationMode ResolveActivationMode(const Config& cfg)
    {
        if (IsTakeoverEnabled(cfg)) {
            return ActivationMode::kHotkey;
        }
        const auto mode = Clamp(cfg.activationMode, 0, 2);
        return static_cast<ActivationMode>(mode);
    }

    static bool IsAllowMultipleActivationTypesEnabled(const Config& cfg)
    {
        if (IsTakeoverEnabled(cfg)) {
            return false;
        }
        return Clamp(cfg.allowMultipleActivationTypes, 0, 1) != 0;
    }

    static bool IsActivationModeEnabled(const Config& cfg, ActivationMode mode)
    {
        if (IsAllowMultipleActivationTypesEnabled(cfg)) {
            return true;
        }
        return ResolveActivationMode(cfg) == mode;
    }

    static const char* ActivationModeName(ActivationMode mode)
    {
        switch (mode) {
        case ActivationMode::kShout:
            return "shout";
        case ActivationMode::kSpell:
            return "spell";
        case ActivationMode::kHotkey:
            return "hotkey";
        default:
            return "unknown";
        }
    }

    static const char* TakeoverStyleName(TakeoverStyle style)
    {
        switch (style) {
        case TakeoverStyle::kTkDefaultFullRoll:
            return "tk_default_full_roll";
        case TakeoverStyle::kStepAndForwardRoll:
            return "step_and_forward_roll";
        case TakeoverStyle::kFullStep:
            return "full_step";
        default:
            return "unknown";
        }
    }

    static const char* TakeoverProviderName(TakeoverProvider provider)
    {
        switch (provider) {
        case TakeoverProvider::kTkDodge:
            return "tk";
        case TakeoverProvider::kDmco:
            return "dmco";
        default:
            return "unknown";
        }
    }

    static const char* DmcoTakeoverStyleName(DmcoTakeoverStyle style)
    {
        switch (style) {
        case DmcoTakeoverStyle::kSet1:
            return "set_1";
        case DmcoTakeoverStyle::kSet2:
            return "set_2";
        default:
            return "unknown";
        }
    }

    static std::size_t ToActivationModeIndex(ActivationMode mode)
    {
        const auto raw = Clamp(static_cast<std::int32_t>(mode), 0, 2);
        return static_cast<std::size_t>(raw);
    }

    class ScopedActivationModeOverride final
    {
    public:
        explicit ScopedActivationModeOverride(std::optional<ActivationMode> mode) :
            _previous(g_forcedActivationMode)
        {
            g_forcedActivationMode = mode;
        }

        ~ScopedActivationModeOverride()
        {
            g_forcedActivationMode = _previous;
        }

    private:
        std::optional<ActivationMode> _previous{};
    };

    struct ModeResourceCost
    {
        RE::ActorValue actorValue{ RE::ActorValue::kNone };
        float amount{ 0.0f };
        const char* label{ "" };
    };

    static ActivationMode ResolveTriggerActivationMode(RE::Actor*, const Config& cfg)
    {
        if (g_forcedActivationMode.has_value()) {
            return *g_forcedActivationMode;
        }
        return ResolveActivationMode(cfg);
    }

    static std::optional<ActivationMode> ParseActivationModeOverride(std::int32_t activationMode)
    {
        if (activationMode < static_cast<std::int32_t>(ActivationMode::kShout) ||
            activationMode > static_cast<std::int32_t>(ActivationMode::kHotkey)) {
            return std::nullopt;
        }
        return static_cast<ActivationMode>(activationMode);
    }

    static void LogModeDisabledReject(std::string_view source, ActivationMode mode)
    {
        static std::mutex rejectLogMutex{};
        static std::array<std::chrono::steady_clock::time_point, 3> lastRejectLogged = {
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min(),
            std::chrono::steady_clock::time_point::min()
        };

        const auto modeIndex = ToActivationModeIndex(mode);
        const auto now = std::chrono::steady_clock::now();
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(rejectLogMutex);
            const auto last = lastRejectLogged[modeIndex];
            if (last == std::chrono::steady_clock::time_point::min() ||
                now - last > std::chrono::milliseconds(750)) {
                lastRejectLogged[modeIndex] = now;
                shouldLog = true;
            }
        }
        if (!shouldLog) {
            return;
        }

        logger::info("DodgeBlink: {} ignored; {} mode disabled.", source, ActivationModeName(mode));
        AppendNativeTrace(
            "DodgeBlink " + std::string(source) +
            " ignored mode_disabled=" + std::string(ActivationModeName(mode)));
    }

    static float ResolveTierCooldownSeconds(const Config& cfg, std::int32_t tier, ActivationMode mode)
    {
        tier = Clamp<std::int32_t>(tier, 1, 3);
        if (mode == ActivationMode::kHotkey && IsTakeoverEnabled(cfg)) {
            // Takeover cadence is stamina/dash-gate driven; do not add cooldown delay.
            return 0.0f;
        }
        if (mode == ActivationMode::kShout) {
            return cfg.shoutCooldownSeconds[tier];
        }
        return cfg.tier[tier].cooldownSeconds;
    }

    static std::optional<ModeResourceCost> ResolveModeResourceCost(const Config& cfg, std::int32_t tier, ActivationMode mode)
    {
        tier = Clamp<std::int32_t>(tier, 1, 3);
        if (mode == ActivationMode::kHotkey) {
            return ModeResourceCost{ RE::ActorValue::kStamina, cfg.hotkeyStaminaCost, "stamina" };
        }
        if (mode == ActivationMode::kSpell) {
            return ModeResourceCost{ RE::ActorValue::kMagicka, cfg.spellMagickaCost[tier], "magicka" };
        }
        return std::nullopt;
    }

    static bool HasSufficientModeResource(RE::Actor* actor, const ModeResourceCost& resourceCost, std::int32_t tier, ActivationMode mode)
    {
        if (!actor || resourceCost.amount <= 0.0f) {
            return true;
        }
        auto* avOwner = actor->AsActorValueOwner();
        if (!avOwner) {
            logger::warn(
                "DodgeBlink: {} tier {} blocked; actor has no ActorValueOwner for {} cost.",
                ActivationModeName(mode),
                tier,
                resourceCost.label);
            return false;
        }

        const float available = std::max(0.0f, avOwner->GetActorValue(resourceCost.actorValue));
        if (available + 0.001f < resourceCost.amount) {
            logger::info(
                "DodgeBlink: {} tier {} blocked by {} (need {:.1f}, have {:.1f}).",
                ActivationModeName(mode),
                tier,
                resourceCost.label,
                resourceCost.amount,
                available);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(mode)) +
                " tier " + std::to_string(tier) +
                " blocked by " + std::string(resourceCost.label) +
                " need=" + std::to_string(resourceCost.amount) +
                " have=" + std::to_string(available));
            return false;
        }
        return true;
    }

    static void ConsumeModeResource(RE::Actor* actor, const ModeResourceCost& resourceCost, std::int32_t tier, ActivationMode mode)
    {
        if (!actor || resourceCost.amount <= 0.0f) {
            return;
        }
        auto* avOwner = actor->AsActorValueOwner();
        if (!avOwner) {
            return;
        }

        // Negative restore on damage modifier applies spend to current actor value.
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, resourceCost.actorValue, -resourceCost.amount);
        const float remaining = std::max(0.0f, avOwner->GetActorValue(resourceCost.actorValue));
        logger::info(
            "DodgeBlink: {} tier {} spent {:.1f} {} (remaining {:.1f}).",
            ActivationModeName(mode),
            tier,
            resourceCost.amount,
            resourceCost.label,
            remaining);
    }

    static bool HasAnyHkxFile(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
            return false;
        }

        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            auto ext = ToLower(entry.path().extension().string());
            if (ext == ".hkx") {
                return true;
            }
        }
        return false;
    }

    static bool HasFilePath(const std::filesystem::path& filePath)
    {
        std::error_code ec;
        return std::filesystem::exists(filePath, ec) && std::filesystem::is_regular_file(filePath, ec);
    }

    static bool HasDirectionalThirdPersonAnimations()
    {
        const auto root = std::filesystem::path("Data") / "Meshes" / "OpenAnimationReplacer" / "NADS";
        static const std::array<std::string, 4> kDirectionalSubmods = {
            "TP_Dodge_Forward",
            "TP_Dodge_Right",
            "TP_Dodge_Backward",
            "TP_Dodge_Left"
        };

        for (const auto& submod : kDirectionalSubmods) {
            if (!HasAnyHkxFile(root / submod / "actors" / "character" / "animations")) {
                return false;
            }
        }
        return true;
    }

    static float ReadFloat(const std::unordered_map<std::string, std::string>& kv, const std::string& key, float fallback)
    {
        auto it = kv.find(ToLower(key));
        if (it == kv.end()) {
            return fallback;
        }

        try {
            return std::stof(it->second);
        } catch (...) {
            return fallback;
        }
    }

    static std::int32_t ReadInt(const std::unordered_map<std::string, std::string>& kv, const std::string& key, std::int32_t fallback)
    {
        auto it = kv.find(ToLower(key));
        if (it == kv.end()) {
            return fallback;
        }

        try {
            return std::stoi(it->second);
        } catch (...) {
            return fallback;
        }
    }

    static std::string ReadString(const std::unordered_map<std::string, std::string>& kv, const std::string& key, const std::string& fallback)
    {
        auto it = kv.find(ToLower(key));
        if (it == kv.end()) {
            return fallback;
        }
        return it->second;
    }

    static void InitializeLogging()
    {
        std::filesystem::path logPath = std::filesystem::path("Data") / "SKSE" / "DodgeBlinkShout.log";
        if (auto logsPath = SKSE::log::log_directory(); logsPath.has_value()) {
            logPath = *logsPath / "DodgeBlinkShout.log";
        }
        g_nativeTracePath = logPath.parent_path() / "DodgeBlinkShout.native.log";

        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
        std::filesystem::create_directories(g_nativeTracePath.parent_path(), ec);

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", sink);

#ifndef NDEBUG
        log->set_level(spdlog::level::debug);
#else
        log->set_level(spdlog::level::info);
#endif
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger::info("DodgeBlink: native log initialized at {}", logPath.string());
    }

    static void AppendNativeTrace(const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(g_nativeTraceMutex);
        std::error_code ec;
        std::filesystem::create_directories(g_nativeTracePath.parent_path(), ec);
        std::ofstream out(g_nativeTracePath, std::ios::app);
        if (!out.is_open()) {
            return;
        }
        out << msg << '\n';
    }

    static float DotXZ(const RE::NiPoint3& a, const RE::NiPoint3& b)
    {
        return a.x * b.x + a.y * b.y;
    }

    static RE::NiPoint3 NormalizeXZ(const RE::NiPoint3& v)
    {
        const float len = std::sqrt(v.x * v.x + v.y * v.y);
        if (len <= 1e-5f) {
            return RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
        }
        return RE::NiPoint3{ v.x / len, v.y / len, 0.0f };
    }

    static RE::NiPoint3 GetFacingDirectionXZ(RE::Actor* actor)
    {
        const float yaw = actor->data.angle.z;
        return RE::NiPoint3{ std::sin(yaw), std::cos(yaw), 0.0f };
    }

    static RE::NiPoint3 GetRightDirectionXZ(RE::Actor* actor)
    {
        const auto forward = GetFacingDirectionXZ(actor);
        return RE::NiPoint3{ forward.y, -forward.x, 0.0f };
    }

    static float InputThresholdNormalized(float inputThreshold)
    {
        // Input threshold interpretation:
        // - <= 1.0 : direct normalized threshold
        // - > 1.0  : percentage (25 => 0.25)
        if (inputThreshold <= 1.0f) {
            return Clamp(inputThreshold, 0.0f, 1.0f);
        }
        return Clamp(inputThreshold / 100.0f, 0.0f, 1.0f);
    }

    static float InputYScale(float inputYSign)
    {
        return inputYSign < 0.0f ? -1.0f : 1.0f;
    }

    static std::optional<RE::NiPoint3> GetMoveInputFromPlayerControls(RE::Actor* actor)
    {
        if (!actor || actor != RE::PlayerCharacter::GetSingleton()) {
            return std::nullopt;
        }

        auto* controls = RE::PlayerControls::GetSingleton();
        if (!controls) {
            return std::nullopt;
        }

        const auto move = controls->data.moveInputVec;
        const float mag = std::sqrt(move.x * move.x + move.y * move.y);
        const Config cfg = GetConfigSnapshot();
        if (mag < InputThresholdNormalized(cfg.inputThreshold)) {
            return std::nullopt;
        }

        auto forward = NormalizeXZ(GetFacingDirectionXZ(actor));
        auto right = NormalizeXZ(GetRightDirectionXZ(actor));

        RE::NiPoint3 worldDir = forward * (move.y * InputYScale(cfg.inputYSign)) + right * move.x;
        worldDir.z = 0.0f;

        const float worldLen = std::sqrt(worldDir.x * worldDir.x + worldDir.y * worldDir.y);
        if (worldLen <= 1e-5f) {
            return std::nullopt;
        }

        return NormalizeXZ(worldDir);
    }

    static void StoreLatchedDirection(const RE::NiPoint3& worldDirection)
    {
        std::lock_guard<std::mutex> lock(g_inputLatchMutex);
        g_lastLatchedWorldDirection = NormalizeXZ(worldDirection);
        g_lastLatchedAt = std::chrono::steady_clock::now();
    }

    static void ClearLatchedDirection()
    {
        std::lock_guard<std::mutex> lock(g_inputLatchMutex);
        g_lastLatchedWorldDirection.reset();
        g_lastLatchedAt = std::chrono::steady_clock::time_point::min();
    }

    static std::optional<RE::NiPoint3> GetLatchedDirection(const Config& cfg)
    {
        const auto maxAgeMs = Clamp(cfg.inputLatchWindowMs, 0, 2000);
        if (maxAgeMs <= 0) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(g_inputLatchMutex);
        if (!g_lastLatchedWorldDirection.has_value() || g_lastLatchedAt == std::chrono::steady_clock::time_point::min()) {
            return std::nullopt;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastLatchedAt).count();
        if (elapsed < 0 || elapsed > maxAgeMs) {
            return std::nullopt;
        }
        return g_lastLatchedWorldDirection;
    }

    static bool IsShoutButtonEvent(const RE::BSFixedString& userEvent)
    {
        if (userEvent.empty()) {
            return false;
        }

        const auto normalized = ToLower(std::string(userEvent.c_str()));
        return normalized == "shout" || normalized == "voice" || normalized == "power";
    }

    static constexpr std::string_view kNadsPluginName = "DodgeBlinkShout.esp";

    template <class T>
    static T* ResolvePluginFormByEditorID(std::string_view pluginName, std::string_view editorID)
    {
        if (pluginName.empty() || editorID.empty()) {
            return nullptr;
        }

        if (auto* direct = RE::TESForm::LookupByEditorID<T>(editorID.data()); direct) {
            return direct;
        }

        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return nullptr;
        }

        const auto wantedPlugin = ToLower(std::string(pluginName));
        const auto wantedEditorID = ToLower(std::string(editorID));
        const auto& forms = data->GetFormArray<T>();
        for (auto* form : forms) {
            if (!form) {
                continue;
            }

            auto* ownerFile = form->GetFile();
            if (!ownerFile) {
                continue;
            }
            if (ToLower(std::string(ownerFile->GetFilename())) != wantedPlugin) {
                continue;
            }

            const char* formEditorID = form->GetFormEditorID();
            if (!formEditorID || formEditorID[0] == '\0') {
                continue;
            }
            if (ToLower(std::string(formEditorID)) == wantedEditorID) {
                return form;
            }
        }

        return nullptr;
    }

    static RE::TESShout* GetNadsShoutForm()
    {
        static RE::TESShout* cached = nullptr;
        static bool loggedMissing = false;
        if (!cached) {
            cached = ResolvePluginFormByEditorID<RE::TESShout>(kNadsPluginName, "DBS_Blink_Shou");
        }
        if (!cached) {
            if (auto* data = RE::TESDataHandler::GetSingleton(); data) {
                const auto wantedPlugin = ToLower(std::string(kNadsPluginName));
                const auto& shouts = data->GetFormArray<RE::TESShout>();
                for (auto* shout : shouts) {
                    if (!shout) {
                        continue;
                    }
                    auto* ownerFile = shout->GetFile();
                    if (!ownerFile) {
                        continue;
                    }
                    if (ToLower(std::string(ownerFile->GetFilename())) != wantedPlugin) {
                        continue;
                    }

                    const char* fullName = shout->GetName();
                    if (fullName && ToLower(std::string(fullName)) == "nads shout") {
                        cached = shout;
                        break;
                    }
                }
            }
        }
        if (!cached && !loggedMissing) {
            loggedMissing = true;
            logger::warn("DodgeBlink: could not resolve DBS_Blink_Shou form; TP shout intercept disabled until form resolves.");
        }
        return cached;
    }

    static RE::SpellItem* GetNadsSpellForm(std::string_view editorID)
    {
        if (editorID.empty()) {
            return nullptr;
        }

        if (auto* spell = ResolvePluginFormByEditorID<RE::SpellItem>(kNadsPluginName, editorID); spell) {
            return spell;
        }

        std::string_view expectedName;
        if (editorID == "NADS_DodgeSpell_T1") {
            expectedName = "NADS SPELL T1";
        } else if (editorID == "NADS_DodgeSpell_T2") {
            expectedName = "NADS SPELL T2";
        } else if (editorID == "NADS_DodgeSpell_T3") {
            expectedName = "NADS SPELL T3";
        } else {
            return nullptr;
        }

        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return nullptr;
        }

        const auto wantedPlugin = ToLower(std::string(kNadsPluginName));
        const auto wantedName = ToLower(std::string(expectedName));
        const auto& spells = data->GetFormArray<RE::SpellItem>();
        for (auto* spell : spells) {
            if (!spell) {
                continue;
            }
            auto* ownerFile = spell->GetFile();
            if (!ownerFile) {
                continue;
            }
            if (ToLower(std::string(ownerFile->GetFilename())) != wantedPlugin) {
                continue;
            }

            const char* fullName = spell->GetName();
            if (fullName && ToLower(std::string(fullName)) == wantedName) {
                return spell;
            }
        }

        return nullptr;
    }

    static bool IsNadsShout(const RE::TESShout* shout)
    {
        auto* nadsShout = GetNadsShoutForm();
        return nadsShout && shout && shout->GetFormID() == nadsShout->GetFormID();
    }

    static RE::TESWordOfPower* GetNadsShoutWord(RE::StaticFunctionTag*, std::int32_t wordIndex)
    {
        auto* shout = GetNadsShoutForm();
        if (!shout) {
            return nullptr;
        }

        const auto idx = Clamp(wordIndex, 1, 3) - 1;
        return shout->variations[idx].word;
    }

    static RE::TESSpellList::SpellData* GetPlayerSpellList(RE::PlayerCharacter* player)
    {
        if (!player) {
            return nullptr;
        }
        auto* actorBase = player->GetActorBase();
        if (!actorBase) {
            return nullptr;
        }
        return actorBase->GetSpellList();
    }

    static bool EnsurePlayerHasShout(RE::PlayerCharacter* player, RE::TESShout* shout)
    {
        if (!player || !shout) {
            return false;
        }

        if (player->HasShout(shout)) {
            return true;
        }

        if (player->AddShout(shout)) {
            logger::info("DodgeBlink: auto-added shout DBS_Blink_Shou to player.");
            return true;
        }

        bool baseAdded = false;
        bool baseHasShout = false;
        if (auto* spellList = GetPlayerSpellList(player); spellList) {
            baseHasShout = spellList->GetIndex(shout).has_value();
            if (!baseHasShout) {
                baseAdded = spellList->AddShout(shout);
                baseHasShout = spellList->GetIndex(shout).has_value();
            }
        }
        if (baseAdded) {
            logger::info("DodgeBlink: auto-added shout DBS_Blink_Shou to player base spell list (fallback).");
        }

        return player->HasShout(shout) || baseHasShout;
    }

    static bool EnsurePlayerHasSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, std::string_view editorID)
    {
        if (!player || !spell) {
            return false;
        }

        if (player->HasSpell(spell)) {
            return true;
        }

        if (player->AddSpell(spell)) {
            logger::info("DodgeBlink: auto-added spell {} to player.", editorID);
            return true;
        }

        bool baseAdded = false;
        bool baseHasSpell = false;
        if (auto* spellList = GetPlayerSpellList(player); spellList) {
            baseHasSpell = spellList->GetIndex(spell).has_value();
            if (!baseHasSpell) {
                baseAdded = spellList->AddSpell(spell);
                baseHasSpell = spellList->GetIndex(spell).has_value();
            }
        }
        if (baseAdded) {
            logger::info("DodgeBlink: auto-added spell {} to player base spell list (fallback).", editorID);
        }

        return player->HasSpell(spell) || baseHasSpell;
    }

    static bool RemovePlayerShout(RE::PlayerCharacter* player, RE::TESShout* shout)
    {
        if (!player || !shout) {
            return false;
        }

        auto& runtimeData = player->GetActorRuntimeData();
        if (runtimeData.selectedPower == shout) {
            // Clear selected power so removed NADS shout does not remain "equipped" in power slot.
            runtimeData.selectedPower = nullptr;
            player->InterruptCast(true);
            logger::info("DodgeBlink: cleared selected NADS shout from player power slot.");
        }

        bool baseHasShout = false;
        bool baseRemoved = false;
        if (auto* spellList = GetPlayerSpellList(player); spellList) {
            baseHasShout = spellList->GetIndex(shout).has_value();
            if (baseHasShout) {
                baseRemoved = spellList->RemoveShout(shout);
                baseHasShout = spellList->GetIndex(shout).has_value();
            }
        }

        if (baseRemoved) {
            logger::info("DodgeBlink: removed NADS shout from player base spell list (activation visibility enforcement).");
        }

        const bool stillHasShout = player->HasShout(shout) || baseHasShout;
        return !stillHasShout;
    }

    static bool RemovePlayerSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, std::string_view editorID)
    {
        if (!player || !spell) {
            return false;
        }

        // Drop equipped hand references before removing spell records.
        player->DeselectSpell(spell);

        bool runtimeRemoved = false;
        if (player->HasSpell(spell)) {
            runtimeRemoved = player->RemoveSpell(spell);
        }

        bool baseHasSpell = false;
        bool baseRemoved = false;
        if (auto* spellList = GetPlayerSpellList(player); spellList) {
            baseHasSpell = spellList->GetIndex(spell).has_value();
            if (baseHasSpell) {
                baseRemoved = spellList->RemoveSpell(spell);
                baseHasSpell = spellList->GetIndex(spell).has_value();
            }
        }

        if (runtimeRemoved || baseRemoved) {
            logger::info("DodgeBlink: removed spell {} from player (activation visibility enforcement).", editorID);
        }

        const bool stillHasSpell = player->HasSpell(spell) || baseHasSpell;
        return !stillHasSpell;
    }

    static bool EnsurePlayerActivationForms()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }
        if (g_activationFormsReady.load(std::memory_order_acquire)) {
            return true;
        }

        std::lock_guard<std::mutex> ensureLock(g_activationEnsureMutex);
        if (g_activationFormsReady.load(std::memory_order_acquire)) {
            return true;
        }

        const Config cfg = GetConfigSnapshot();
        const auto activationMode = ResolveActivationMode(cfg);
        const bool allowMultipleTypes = IsAllowMultipleActivationTypesEnabled(cfg);
        const bool wantShoutActivation = allowMultipleTypes || activationMode == ActivationMode::kShout;
        const bool wantSpellActivation = allowMultipleTypes || activationMode == ActivationMode::kSpell;

        bool hasAnyRequiredActivationForm = false;
        bool grantSatisfied = true;
        if (auto* shout = GetNadsShoutForm(); shout) {
            if (wantShoutActivation) {
                hasAnyRequiredActivationForm = true;
                if (!EnsurePlayerHasShout(player, shout)) {
                    grantSatisfied = false;
                }

                std::size_t shoutWordCount = 0;
                for (const auto& variation : shout->variations) {
                    if (variation.word) {
                        ++shoutWordCount;
                    }
                }
                logger::info("DodgeBlink: shout activation ensured shout form; words detected={} (knowledge handled by Papyrus TeachWord path).", shoutWordCount);
            } else {
                (void)RemovePlayerShout(player, shout);
            }
        }

        static constexpr std::array<std::string_view, 3> kSpellEditorIDs = {
            "NADS_DodgeSpell_T1",
            "NADS_DodgeSpell_T2",
            "NADS_DodgeSpell_T3"
        };
        static std::array<bool, 3> loggedMissingSpell = { false, false, false };
        static bool loggedSpellModeT1Metadata = false;

        for (std::size_t i = 0; i < kSpellEditorIDs.size(); ++i) {
            auto* spell = GetNadsSpellForm(kSpellEditorIDs[i]);
            if (!spell) {
                if (!loggedMissingSpell[i]) {
                    loggedMissingSpell[i] = true;
                    if (wantSpellActivation) {
                        logger::info("DodgeBlink: spell {} not found (optional auto-grant skipped).", kSpellEditorIDs[i]);
                    }
                }
                continue;
            }

            if (wantSpellActivation) {
                hasAnyRequiredActivationForm = true;
                if (!EnsurePlayerHasSpell(player, spell, kSpellEditorIDs[i])) {
                    grantSatisfied = false;
                }

                if (i == 0 && !loggedSpellModeT1Metadata) {
                    loggedSpellModeT1Metadata = true;
                    logger::info(
                        "DodgeBlink: spell-mode T1 metadata spellType={} castingType={} assocSkill={}.",
                        static_cast<std::int32_t>(spell->GetSpellType()),
                        static_cast<std::int32_t>(spell->GetCastingType()),
                        static_cast<std::int32_t>(spell->GetAssociatedSkill()));
                }
            } else {
                (void)RemovePlayerSpell(player, spell, kSpellEditorIDs[i]);
            }
        }

        if (!allowMultipleTypes && activationMode == ActivationMode::kHotkey) {
            const bool wasReady = g_activationFormsReady.exchange(true, std::memory_order_acq_rel);
            g_activationFormsPendingLogged.store(false, std::memory_order_release);
            if (!wasReady) {
                logger::info("DodgeBlink: activation mode hotkey; enforced hotkey-only activation visibility.");
            }
            return true;
        }

        if (hasAnyRequiredActivationForm && grantSatisfied) {
            const bool wasReady = g_activationFormsReady.exchange(true, std::memory_order_acq_rel);
            g_activationFormsPendingLogged.store(false, std::memory_order_release);
            if (!wasReady) {
                logger::info(
                    "DodgeBlink: activation forms verified on player (mode={} allowMultipleTypes={}).",
                    ActivationModeName(activationMode),
                    allowMultipleTypes ? 1 : 0);
            }
            return true;
        }

        if (hasAnyRequiredActivationForm && !grantSatisfied) {
            bool expected = false;
            if (g_activationFormsPendingLogged.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                logger::info("DodgeBlink: activation forms found but not yet granted; will retry.");
            }
        }
        return false;
    }

    static void QueueEnsurePlayerActivationForms()
    {
        if (g_activationFormsReady.load(std::memory_order_acquire)) {
            return;
        }

        bool expected = false;
        if (!g_activationGrantThreadRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        std::thread([]() {
            for (int attempt = 0; attempt < 120; ++attempt) {
                if (g_activationFormsReady.load(std::memory_order_acquire)) {
                    break;
                }

                if (auto* task = SKSE::GetTaskInterface(); task) {
                    task->AddTask([]() {
                        (void)EnsurePlayerActivationForms();
                    });
                } else {
                    (void)EnsurePlayerActivationForms();
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            g_activationGrantThreadRunning.store(false, std::memory_order_release);
        }).detach();
    }

    static std::int32_t ToPapyrusStyleKeycode(RE::INPUT_DEVICE device, std::uint32_t idCode)
    {
        const auto raw = static_cast<std::int32_t>(idCode);
        switch (device) {
        case RE::INPUT_DEVICE::kKeyboard:
            return raw;
        case RE::INPUT_DEVICE::kMouse:
            return raw + 256;
        case RE::INPUT_DEVICE::kGamepad:
            // Gamepad ButtonEvent ids are XInput-style masks/arbitrary ids.
            // Map explicitly to Papyrus gamepad keycodes used by MCM KeyMap options.
            switch (idCode) {
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kUp):
                return 266;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kDown):
                return 267;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kLeft):
                return 268;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kRight):
                return 269;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kStart):
                return 270;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kBack):
                return 271;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kLeftThumb):
                return 272;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kRightThumb):
                return 273;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kLeftShoulder):
                return 274;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kRightShoulder):
                return 275;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kA):
                return 276;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kB):
                return 277;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kX):
                return 278;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kY):
                return 279;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kLeftTrigger):
                return 280;
            case static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kRightTrigger):
                return 281;
            default:
                return raw;
            }
        default:
            return raw;
        }
    }

    static bool IsNadsShoutEquipped(RE::Actor* actor)
    {
        if (!actor) {
            return false;
        }

        if (IsNadsShout(actor->GetCurrentShout())) {
            return true;
        }

        const auto* selectedPower = actor->GetActorRuntimeData().selectedPower;
        if (!selectedPower) {
            return false;
        }

        const auto* selectedShout = selectedPower->As<RE::TESShout>();
        return IsNadsShout(selectedShout);
    }

    static void HandlePlayerShoutPress()
    {
        const Config cfg = GetConfigSnapshot();
        if (!IsActivationModeEnabled(cfg, ActivationMode::kShout)) {
            LogModeDisabledReject("shout_press", ActivationMode::kShout);
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        if (auto inputDir = GetMoveInputFromPlayerControls(player); inputDir.has_value()) {
            StoreLatchedDirection(*inputDir);
        } else {
            ClearLatchedDirection();
        }

        if (cfg.tpImmediateShoutTriggerEnabled == 0) {
            return;
        }

        if (cfg.tpSuppressVanillaShoutAnim == 0 || cfg.tpSuppressVanillaShoutEvent.empty()) {
            return;
        }

        const bool isFirstPerson = IsFirstPersonForActor(player);
        if (isFirstPerson) {
            return;
        }

        if (!IsNadsShoutEquipped(player)) {
            return;
        }

        player->NotifyAnimationGraph(RE::BSFixedString(cfg.tpSuppressVanillaShoutEvent.c_str()));
    }

    static bool TryTriggerHotkeyDodge(RE::ButtonEvent* buttonEvent)
    {
        if (!buttonEvent || (!buttonEvent->IsDown() && !buttonEvent->IsUp())) {
            return false;
        }
        const bool isKeyDown = buttonEvent->IsDown();
        const bool isKeyUp = buttonEvent->IsUp();

        const Config cfg = GetConfigSnapshot();
        if (!IsActivationModeEnabled(cfg, ActivationMode::kHotkey)) {
            LogModeDisabledReject("hotkey_input", ActivationMode::kHotkey);
            return false;
        }
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return false;
        }

        const std::int32_t configuredHotkey = Clamp(cfg.hotkeyCode, -1, 512);
        if (configuredHotkey < 0) {
            return false;
        }

        const auto device = buttonEvent->GetDevice();
        const std::int32_t inputCode = static_cast<std::int32_t>(buttonEvent->GetIDCode());
        const auto papyrusKeyCode = ToPapyrusStyleKeycode(device, buttonEvent->GetIDCode());
        std::int32_t mappedInt = -1;
        std::int32_t mappedPapyrusKeyCode = -1;
        bool hotkeyMatch = false;
        if (device == RE::INPUT_DEVICE::kGamepad) {
            // Gamepad binds in MCM are Papyrus-style keycodes. Keep matching strict to
            // avoid false positives on other gamepad controls through fallback mapping paths.
            hotkeyMatch = (papyrusKeyCode == configuredHotkey);
        } else {
            hotkeyMatch =
                (inputCode == configuredHotkey) ||
                (papyrusKeyCode == configuredHotkey) ||
                ((inputCode + 256) == configuredHotkey);
        }
        if (!hotkeyMatch) {
            if (device != RE::INPUT_DEVICE::kGamepad) {
                if (auto* inputMgr = RE::BSInputDeviceManager::GetSingleton(); inputMgr) {
                std::uint32_t mappedCode = 0;
                if (inputMgr->GetDeviceMappedKeycode(device, buttonEvent->GetIDCode(), mappedCode)) {
                    mappedInt = static_cast<std::int32_t>(mappedCode);
                    mappedPapyrusKeyCode = ToPapyrusStyleKeycode(device, mappedCode);
                    hotkeyMatch =
                        (mappedInt == configuredHotkey) ||
                        (mappedPapyrusKeyCode == configuredHotkey) ||
                        ((mappedInt + 256) == configuredHotkey) ||
                        (mappedInt == (configuredHotkey - 256));
                }
            }
            }
        }
        if (!hotkeyMatch) {
            if (isKeyDown) {
                static std::mutex hotkeyMissLogMutex{};
                static auto lastHotkeyMissLog = std::chrono::steady_clock::time_point::min();
                const auto now = std::chrono::steady_clock::now();
                bool shouldLog = false;
                {
                    std::lock_guard<std::mutex> lock(hotkeyMissLogMutex);
                    if (lastHotkeyMissLog == std::chrono::steady_clock::time_point::min() ||
                        now - lastHotkeyMissLog > std::chrono::milliseconds(1500)) {
                        lastHotkeyMissLog = now;
                        shouldLog = true;
                    }
                }
                if (shouldLog) {
                    logger::info(
                        "DodgeBlink: hotkey mismatch cfg={} input={} papyrus={} mapped={} mappedPapyrus={} device={} userEvent={}.",
                        configuredHotkey,
                        inputCode,
                        papyrusKeyCode,
                        mappedInt,
                        mappedPapyrusKeyCode,
                        static_cast<std::int32_t>(buttonEvent->GetDevice()),
                        buttonEvent->QUserEvent().empty() ? "<empty>" : buttonEvent->QUserEvent().c_str());
                }
            }
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        const bool takeoverEnabled = IsTakeoverEnabled(cfg);

        if (isKeyDown) {
            if (auto inputDir = GetMoveInputFromPlayerControls(player); inputDir.has_value()) {
                StoreLatchedDirection(*inputDir);
            }
            const std::int32_t hotkeyTier = Clamp(cfg.hotkeyTier, 1, 3);
            if (takeoverEnabled && g_dashGate.load(std::memory_order_acquire)) {
                const auto provider = ResolveTakeoverProvider(cfg);
                logger::info(
                    "DodgeBlink: takeover hotkey ignored during active dash on key-down (provider={} tier={}).",
                    TakeoverProviderName(provider),
                    hotkeyTier);
                return true;
            }
            std::lock_guard<std::mutex> lock(g_hotkeyPressMutex);
            g_hotkeyPressArmed = true;
            g_hotkeyPressDevice = static_cast<std::int32_t>(device);
            g_hotkeyPressIdCode = inputCode;
            g_hotkeyPressStartedAt = std::chrono::steady_clock::now();
            // Always consume configured dodge hotkey press so bound vanilla actions never leak through.
            return true;
        }

        if (!isKeyUp) {
            return false;
        }

        const std::int32_t tapMaxMs = Clamp(cfg.hotkeyTapMaxMs, 50, 1000);
        const auto now = std::chrono::steady_clock::now();
        std::int32_t heldMsFromClock = -1;
        bool hadArmedPress = false;
        bool keyMatchWithArmedPress = false;
        std::int32_t armedPressDevice = -1;
        std::int32_t armedPressIdCode = -1;
        {
            std::lock_guard<std::mutex> lock(g_hotkeyPressMutex);
            if (g_hotkeyPressArmed &&
                g_hotkeyPressStartedAt != std::chrono::steady_clock::time_point::min()) {
                hadArmedPress = true;
                armedPressDevice = g_hotkeyPressDevice;
                armedPressIdCode = g_hotkeyPressIdCode;
                keyMatchWithArmedPress =
                    (armedPressDevice == static_cast<std::int32_t>(device)) &&
                    (armedPressIdCode == inputCode);
                if (keyMatchWithArmedPress) {
                    heldMsFromClock = static_cast<std::int32_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_hotkeyPressStartedAt).count());
                }
            }

            if (keyMatchWithArmedPress) {
                g_hotkeyPressArmed = false;
                g_hotkeyPressDevice = -1;
                g_hotkeyPressIdCode = -1;
                g_hotkeyPressStartedAt = std::chrono::steady_clock::time_point::min();
            }
        }
        if (hadArmedPress && !keyMatchWithArmedPress) {
            logger::info(
                "DodgeBlink: hotkey release passed through (armed key mismatch armedDevice={} armedId={} releaseDevice={} releaseId={}).",
                armedPressDevice,
                armedPressIdCode,
                static_cast<std::int32_t>(device),
                inputCode);
            return true;
        }
        if (!hadArmedPress || heldMsFromClock < 0) {
            logger::info("DodgeBlink: hotkey release consumed (no armed press).");
            return true;
        }
        const auto heldMs = std::max(0, heldMsFromClock);
        const char* heldSource = "clock";
        if (heldMs > tapMaxMs) {
            logger::info(
                "DodgeBlink: hotkey release consumed without dodge (held={}ms source={} > tapMax={}ms).",
                heldMs,
                heldSource,
                tapMaxMs);
            return true;
        }

        if (auto inputDir = GetMoveInputFromPlayerControls(player); inputDir.has_value()) {
            StoreLatchedDirection(*inputDir);
        }

        const std::int32_t hotkeyTier = Clamp(cfg.hotkeyTier, 1, 3);
        if (takeoverEnabled && g_dashGate.load(std::memory_order_acquire)) {
            const auto provider = ResolveTakeoverProvider(cfg);
            logger::info(
                "DodgeBlink: takeover hotkey ignored during active dash on release (provider={} tier={}).",
                TakeoverProviderName(provider),
                hotkeyTier);
            return true;
        }
        {
            ScopedActivationModeOverride modeOverride(ActivationMode::kHotkey);
            DoBlinkTier(nullptr, player, hotkeyTier);
        }
        logger::info(
            "DodgeBlink: consumed hotkey tap on release idCode={} held={}ms source={} tapMax={}ms tier={}.",
            inputCode,
            heldMs,
            heldSource,
            tapMaxMs,
            hotkeyTier);
        return true;
    }

    static bool ShouldConsumeThirdPersonNadsShoutInput(RE::ButtonEvent* buttonEvent, bool requireShoutEvent)
    {
        if (!buttonEvent || !buttonEvent->IsDown()) {
            return false;
        }

        if (requireShoutEvent && !IsShoutButtonEvent(buttonEvent->QUserEvent())) {
            return false;
        }

        const Config cfg = GetConfigSnapshot();
        if (!IsActivationModeEnabled(cfg, ActivationMode::kShout)) {
            return false;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || IsFirstPersonForActor(player) || !IsNadsShoutEquipped(player)) {
            return false;
        }

        return cfg.tpImmediateShoutTriggerEnabled != 0;
    }

    static bool TryInterceptThirdPersonNadsShout(RE::ButtonEvent* buttonEvent, std::string_view sourceTag, bool requireShoutEvent)
    {
        if (!ShouldConsumeThirdPersonNadsShoutInput(buttonEvent, requireShoutEvent)) {
            return false;
        }
        // Immediate TP shout trigger intentionally maps to tier-1 only.
        // Keep TPImmediateShoutTriggerEnabled=0 to preserve vanilla hold-to-word tiering.
        const std::int32_t interceptTier = 1;

        static std::mutex interceptDebounceMutex{};
        static auto lastInterceptAt = std::chrono::steady_clock::time_point::min();
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(interceptDebounceMutex);
            if (lastInterceptAt != std::chrono::steady_clock::time_point::min() &&
                now - lastInterceptAt < std::chrono::milliseconds(80)) {
                return true;
            }
            lastInterceptAt = now;
        }

        HandlePlayerShoutPress();
        if (auto* player = RE::PlayerCharacter::GetSingleton(); player) {
            ScopedActivationModeOverride modeOverride(ActivationMode::kShout);
            DoBlinkTier(nullptr, player, interceptTier);
        }
        logger::info("DodgeBlink: consumed TP NADS shout input from {} (down=1) tier={}.", sourceTag, interceptTier);
        AppendNativeTrace(
            "DodgeBlink consumed_tp_shout source=" + std::string(sourceTag) +
            " tier=" + std::to_string(interceptTier));

        return true;
    }

    bool ShoutHandlerHook::CanProcess(RE::ShoutHandler* a_this, RE::InputEvent* a_event)
    {
        if (auto* buttonEvent = a_event ? a_event->AsButtonEvent() : nullptr; ShouldConsumeThirdPersonNadsShoutInput(buttonEvent, true)) {
            // Force shout processing for NADS shout mode so ProcessButton can run our native trigger path
            // even when vanilla shout-word unlock state is not satisfied.
            return true;
        }

        return _canProcess(a_this, a_event);
    }

    void ShoutHandlerHook::ProcessButton(RE::ShoutHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
    {
        if (TryInterceptThirdPersonNadsShout(a_event, "ShoutHandler::ProcessButton", true)) {
            return;
        }

        _processButton(a_this, a_event, a_data);
    }

    void ShoutHandlerHook::Install()
    {
        bool expected = false;
        if (!g_shoutHandlerHookInstalled.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ShoutHandler[0] };
        _canProcess = vtbl.write_vfunc(0x1, CanProcess);
        _processButton = vtbl.write_vfunc(0x4, ProcessButton);
        logger::info("DodgeBlink: ShoutHandler CanProcess/ProcessButton hooks installed (TP NADS-only intercept).");
    }

    RE::BSEventNotifyControl InputEventSink::ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!a_events) {
            return RE::BSEventNotifyControl::kContinue;
        }

        bool consumedHotkey = false;
        for (auto* event = *a_events; event; event = event->next) {
            auto* button = event->AsButtonEvent();
            if (!button) {
                continue;
            }

            if (TryTriggerHotkeyDodge(button)) {
                consumedHotkey = true;
                continue;
            }
            if (!IsShoutButtonEvent(button->QUserEvent())) {
                continue;
            }

            if (button->IsDown()) {
                HandlePlayerShoutPress();
            }
        }

        if (consumedHotkey) {
            return RE::BSEventNotifyControl::kStop;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl HitEventSink::ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* target = a_event->target.get();
        if (!target || target != player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* avOwner = player->AsActorValueOwner();
        if (!avOwner) {
            return RE::BSEventNotifyControl::kContinue;
        }
        const float currentHealth = std::max(0.0f, avOwner->GetActorValue(RE::ActorValue::kHealth));

        float protectedHealth = 0.0f;
        std::int32_t tier = 0;
        ActivationMode mode = ActivationMode::kHotkey;
        std::int32_t hitCount = 0;
        std::int32_t noDamageHitCount = 0;
        std::int32_t healthDeltaHitCount = 0;
        bool healthDeltaObserved = false;
        {
            std::lock_guard<std::mutex> lock(g_dashIFrameMutex);
            if (!g_dashIFrameApplied) {
                return RE::BSEventNotifyControl::kContinue;
            }

            protectedHealth = g_dashIFrameProtectedHealth;
            tier = g_dashIFrameTier;
            mode = g_dashIFrameMode;
            healthDeltaObserved = currentHealth + 0.001f < protectedHealth;

            ++g_dashIFrameHitCount;
            if (healthDeltaObserved) {
                ++g_dashIFrameHealthDeltaHitCount;
            } else {
                ++g_dashIFrameNoDamageHitCount;
            }
            hitCount = g_dashIFrameHitCount;
            noDamageHitCount = g_dashIFrameNoDamageHitCount;
            healthDeltaHitCount = g_dashIFrameHealthDeltaHitCount;
        }

        const std::uint32_t flags = static_cast<std::uint32_t>(a_event->flags.underlying());
        const bool blocked = a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked);
        const RE::FormID causeFormID = a_event->cause ? a_event->cause->GetFormID() : 0;
        const RE::FormID sourceFormID = a_event->source;
        const RE::FormID projectileFormID = a_event->projectile;

        logger::info(
            "DodgeBlink: {} tier {} iframes hit_event hit#={} noDamageHits={} healthDeltaHits={} flags=0x{:02X} blocked={} cause=0x{:08X} source=0x{:08X} projectile=0x{:08X} health={:.2f} protectedHealth={:.2f}",
            ActivationModeName(mode),
            tier,
            hitCount,
            noDamageHitCount,
            healthDeltaHitCount,
            flags,
            blocked ? 1 : 0,
            causeFormID,
            sourceFormID,
            projectileFormID,
            currentHealth,
            protectedHealth);
        AppendNativeTrace(
            "DodgeBlink " + std::string(ActivationModeName(mode)) +
            " tier " + std::to_string(tier) +
            " iframes_hit_event hit=" + std::to_string(hitCount) +
            " noDamageHits=" + std::to_string(noDamageHitCount) +
            " healthDeltaHits=" + std::to_string(healthDeltaHitCount) +
            " flags=" + std::to_string(flags) +
            " blocked=" + std::to_string(blocked ? 1 : 0) +
            " causeFormID=" + std::to_string(causeFormID) +
            " sourceFormID=" + std::to_string(sourceFormID) +
            " projectileFormID=" + std::to_string(projectileFormID) +
            " health=" + std::to_string(currentHealth) +
            " protectedHealth=" + std::to_string(protectedHealth) +
            " healthDeltaObserved=" + std::to_string(healthDeltaObserved ? 1 : 0));

        return RE::BSEventNotifyControl::kContinue;
    }

    static void EnsureMcmQuestRunning(bool forceRestart)
    {
        auto* mcmQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("DBS_MCM_Quest");
        if (!mcmQuest) {
            logger::warn("DodgeBlink: DBS_MCM_Quest not found; MCM cannot register.");
            return;
        }

        if (!mcmQuest->IsEnabled()) {
            mcmQuest->SetEnabled(true);
            logger::info("DodgeBlink: DBS_MCM_Quest was disabled; enabled it.");
        }

        if (forceRestart && (mcmQuest->IsRunning() || mcmQuest->IsStarting())) {
            mcmQuest->Stop();
            mcmQuest->Reset();
            logger::info("DodgeBlink: DBS_MCM_Quest was restarted on load for MCM re-registration.");
        }

        if (mcmQuest->IsRunning() || mcmQuest->IsStarting()) {
            logger::info("DodgeBlink: DBS_MCM_Quest already running.");
            return;
        }

        const bool started = mcmQuest->Start();
        logger::info("DodgeBlink: DBS_MCM_Quest start requested -> started={} nowRunning={} forceRestart={}.", started ? 1 : 0, mcmQuest->IsRunning() ? 1 : 0, forceRestart ? 1 : 0);
    }

    static void RegisterInputEventSink()
    {
        bool expected = false;
        if (!g_inputSinkRegistered.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        auto* inputMgr = RE::BSInputDeviceManager::GetSingleton();
        if (!inputMgr) {
            g_inputSinkRegistered.store(false, std::memory_order_release);
            logger::warn("DodgeBlink: input manager unavailable; shout-direction latch hook not registered.");
            return;
        }

        inputMgr->AddEventSink(&g_inputEventSink);
        logger::info("DodgeBlink: input event sink registered.");
    }

    static void RegisterHitEventSink()
    {
        bool expected = false;
        if (!g_hitSinkRegistered.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        auto* scriptEvents = RE::ScriptEventSourceHolder::GetSingleton();
        if (!scriptEvents) {
            g_hitSinkRegistered.store(false, std::memory_order_release);
            logger::warn("DodgeBlink: ScriptEventSourceHolder unavailable; TESHitEvent sink not registered.");
            return;
        }

        scriptEvents->AddEventSink<RE::TESHitEvent>(&g_hitEventSink);
        logger::info("DodgeBlink: TESHitEvent sink registered.");
    }

    static void OnSkseMessage(SKSE::MessagingInterface::Message* msg)
    {
        if (!msg) {
            return;
        }

        if (msg->type == SKSE::MessagingInterface::kInputLoaded || msg->type == SKSE::MessagingInterface::kDataLoaded) {
            RegisterInputEventSink();
            RegisterHitEventSink();
            ShoutHandlerHook::Install();
        }
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            EnsureMcmQuestRunning(false);
            g_activationFormsReady.store(false, std::memory_order_release);
            g_activationFormsPendingLogged.store(false, std::memory_order_release);
            QueueEnsurePlayerActivationForms();
        }
        if (msg->type == SKSE::MessagingInterface::kPostLoadGame) {
            EnsureMcmQuestRunning(true);
            g_activationFormsReady.store(false, std::memory_order_release);
            g_activationFormsPendingLogged.store(false, std::memory_order_release);
            QueueEnsurePlayerActivationForms();
        }
        if (msg->type == SKSE::MessagingInterface::kNewGame) {
            g_activationFormsReady.store(false, std::memory_order_release);
            g_activationFormsPendingLogged.store(false, std::memory_order_release);
            QueueEnsurePlayerActivationForms();
        }
    }

    static RE::NiPoint3 GetDesiredDashDirection(RE::Actor* actor)
    {
        if (auto controls = GetMoveInputFromPlayerControls(actor); controls.has_value()) {
            g_lastDirectionSource = DirectionSource::kPlayerControls;
            return NormalizeXZ(*controls);
        }

        if (actor && actor == RE::PlayerCharacter::GetSingleton()) {
            const Config cfg = GetConfigSnapshot();
            const bool useLatchForMode =
                IsActivationModeEnabled(cfg, ActivationMode::kShout) ||
                IsActivationModeEnabled(cfg, ActivationMode::kHotkey);
            if (useLatchForMode) {
                if (auto latched = GetLatchedDirection(cfg); latched.has_value()) {
                    g_lastDirectionSource = DirectionSource::kLatchedInput;
                    const auto result = NormalizeXZ(*latched);
                    ClearLatchedDirection();
                    return result;
                }
            }
        }

        g_lastDirectionSource = DirectionSource::kFacing;
        return NormalizeXZ(GetFacingDirectionXZ(actor));
    }

    static bool IsOnCooldown(std::int32_t tier, ActivationMode mode, float cooldownSeconds, float& remainingOut)
    {
        tier = Clamp<std::int32_t>(tier, 1, 3);
        const auto modeIndex = ToActivationModeIndex(mode);
        std::lock_guard<std::mutex> lock(g_cooldownMutex);

        const auto now = std::chrono::steady_clock::now();
        const auto last = g_lastUsedByMode[modeIndex][tier];
        if (last == std::chrono::steady_clock::time_point::min()) {
            remainingOut = 0.0f;
            return false;
        }

        const float cooldown = std::max(0.0f, cooldownSeconds);
        const float elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(now - last).count();
        remainingOut = std::max(0.0f, cooldown - elapsed);
        return elapsed < cooldown;
    }

    static void MarkUsed(std::int32_t tier, ActivationMode mode)
    {
        tier = Clamp<std::int32_t>(tier, 1, 3);
        const auto modeIndex = ToActivationModeIndex(mode);
        std::lock_guard<std::mutex> lock(g_cooldownMutex);
        g_lastUsedByMode[modeIndex][tier] = std::chrono::steady_clock::now();
    }

    static void SyncPlayerShoutRecoveryCooldown(RE::Actor* actor, float cooldownSeconds, std::int32_t tier)
    {
        if (!actor || actor != RE::PlayerCharacter::GetSingleton()) {
            return;
        }

        auto* process = actor->GetActorRuntimeData().currentProcess;
        if (!process || !process->high) {
            logger::warn("DodgeBlink: could not sync shout cooldown (missing player high process).");
            return;
        }

        const float cooldown = std::max(0.0f, cooldownSeconds);
        process->high->voiceTimeElapsed = 0.0f;
        process->high->voiceRecoveryTime = cooldown;
        logger::info("DodgeBlink: synced vanilla shout recovery to {:.2f}s (tier {}).", cooldown, tier);
    }

    static RE::BGSArtObject* GetInvisibilityBodyArt()
    {
        static RE::BGSArtObject* cached = []() -> RE::BGSArtObject* {
            if (auto* byEditor = RE::TESForm::LookupByEditorID<RE::BGSArtObject>("InvisFXBody01"); byEditor) {
                return byEditor;
            }
            return RE::TESForm::LookupByID<RE::BGSArtObject>(kInvisibilityBodyArtFormID);
        }();
        return cached;
    }

    static RE::TESEffectShader* GetInvisibilityBurstShader()
    {
        static RE::TESEffectShader* cached = []() -> RE::TESEffectShader* {
            if (auto* byEditor = RE::TESForm::LookupByEditorID<RE::TESEffectShader>("InvisFXShader"); byEditor) {
                return byEditor;
            }
            return RE::TESForm::LookupByID<RE::TESEffectShader>(kInvisibilityShaderFormID);
        }();
        return cached;
    }

    static void TriggerDashInvisibilityBurst(RE::Actor* actor)
    {
        if (!actor) {
            return;
        }
        if (auto* art = GetInvisibilityBodyArt(); art) {
            actor->ApplyArtObject(art, kDashInvisibilityBurstDurationSeconds);
        }
        if (auto* shader = GetInvisibilityBurstShader(); shader) {
            actor->ApplyEffectShader(shader, kDashInvisibilityBurstDurationSeconds);
        }
    }

    static bool ShouldApplyDashInvisibilityFx(RE::Actor* actor, bool isFirstPerson, const Config& cfg)
    {
        if (!actor || cfg.dashInvisibilityFxEnabled == 0) {
            return false;
        }
        if (cfg.dashInvisibilityFxThirdPersonOnly != 0 && isFirstPerson) {
            return false;
        }

        // Respect active true invisibility from other gameplay sources.
        if (auto* avOwner = actor->AsActorValueOwner(); avOwner) {
            return avOwner->GetActorValue(RE::ActorValue::kInvisibility) <= 0.0f;
        }
        return true;
    }

    static float ResolveDashInvisibilityAlpha(float dashProgress, float durationSeconds, float fadeOutSeconds, float fadeInSeconds)
    {
        const float progress = Clamp(dashProgress, 0.0f, 1.0f);
        const float duration = std::max(durationSeconds, 0.001f);
        const float fadeOutNorm = Clamp(fadeOutSeconds / duration, 0.0f, 1.0f);
        const float fadeInNorm = Clamp(fadeInSeconds / duration, 0.0f, 1.0f);
        const float fadeInStart = Clamp(1.0f - fadeInNorm, 0.0f, 1.0f);

        if (fadeOutNorm > 0.0001f && progress <= fadeOutNorm) {
            return Clamp(1.0f - (progress / fadeOutNorm), 0.0f, 1.0f);
        }
        if (fadeInNorm > 0.0001f && progress >= fadeInStart) {
            const float denom = std::max(0.0001f, 1.0f - fadeInStart);
            return Clamp((progress - fadeInStart) / denom, 0.0f, 1.0f);
        }
        if (progress <= 0.0f || progress >= 1.0f) {
            return 1.0f;
        }
        return 0.0f;
    }

    static bool TryBeginDashGate(std::int32_t tier, ActivationMode mode)
    {
        bool expected = false;
        if (!g_dashGate.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            logger::info("DodgeBlink: {} tier {} blocked by active dash gate.", ActivationModeName(mode), tier);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(mode)) +
                " tier " + std::to_string(tier) + " blocked by active dash gate");
            return false;
        }
        return true;
    }

    static bool ShouldApplyDashIFramesForActor(RE::Actor* actor, const Config& cfg)
    {
        if (cfg.dodgeIFramesEnabled == 0 || !actor) {
            return false;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player && actor == player;
    }

    static bool SetActorGhostState(RE::Actor* actor, bool enabled)
    {
        if (!actor) {
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!vm || !policy) {
            logger::warn("DodgeBlink: SetActorGhostState failed (vm/policy unavailable) enabled={} actor=0x{:08X}.", enabled ? 1 : 0, actor->GetFormID());
            return false;
        }

        const auto invalidHandle = policy->EmptyHandle();
        const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(actor->FORMTYPE), actor);
        if (handle == invalidHandle) {
            logger::warn("DodgeBlink: SetActorGhostState failed (invalid VM handle) enabled={} actor=0x{:08X}.", enabled ? 1 : 0, actor->GetFormID());
            return false;
        }

        auto* args = RE::MakeFunctionArguments(static_cast<bool>(enabled));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
        const bool dispatched = vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("SetGhost"), args, result);
        policy->ReleaseHandle(handle);
        if (!dispatched) {
            logger::warn("DodgeBlink: SetActorGhostState dispatch failed enabled={} actor=0x{:08X}.", enabled ? 1 : 0, actor->GetFormID());
        }
        return dispatched;
    }

    static void BeginDashIFrames(RE::Actor* actor, const Config& cfg, std::int32_t tier, ActivationMode triggerMode, std::string_view routeTag)
    {
        if (!ShouldApplyDashIFramesForActor(actor, cfg)) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_dashIFrameMutex);
        if (g_dashIFrameApplied) {
            return;
        }

        auto* avOwner = actor->AsActorValueOwner();
        if (!avOwner) {
            return;
        }

        g_dashIFrameProtectedHealth = std::max(0.0f, avOwner->GetActorValue(RE::ActorValue::kHealth));
        g_dashIFrameLowestHealth = g_dashIFrameProtectedHealth;
        g_dashIFrameRestoreGhost = false;
        g_dashIFrameActorHandle = actor->CreateRefHandle();
        g_dashIFrameTier = tier;
        g_dashIFrameMode = triggerMode;
        g_dashIFrameHitCount = 0;
        g_dashIFrameNoDamageHitCount = 0;
        g_dashIFrameHealthDeltaHitCount = 0;
        g_dashIFrameApplied = true;
        if (!actor->IsGhost()) {
            g_dashIFrameRestoreGhost = SetActorGhostState(actor, true);
        }

        logger::info(
            "DodgeBlink: {} tier {} iframes begin route={} protectedHealth={:.2f} ghostApplied={}.",
            ActivationModeName(triggerMode),
            tier,
            routeTag,
            g_dashIFrameProtectedHealth,
            g_dashIFrameRestoreGhost ? 1 : 0);
        AppendNativeTrace(
            "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
            " tier " + std::to_string(tier) +
            " iframes_begin route=" + std::string(routeTag) +
            " protectedHealth=" + std::to_string(g_dashIFrameProtectedHealth) +
            " ghostApplied=" + std::to_string(g_dashIFrameRestoreGhost ? 1 : 0));
    }

    static void MaintainDashIFrames(RE::Actor* actor, std::int32_t tier, ActivationMode triggerMode, std::string_view routeTag)
    {
        if (!actor) {
            return;
        }

        float protectedHealth = 0.0f;
        {
            std::lock_guard<std::mutex> lock(g_dashIFrameMutex);
            if (!g_dashIFrameApplied) {
                return;
            }
            protectedHealth = g_dashIFrameProtectedHealth;
        }

        auto* avOwner = actor->AsActorValueOwner();
        if (!avOwner) {
            return;
        }

        const float currentHealth = std::max(0.0f, avOwner->GetActorValue(RE::ActorValue::kHealth));
        if (currentHealth + 0.001f < protectedHealth) {
            std::lock_guard<std::mutex> lock(g_dashIFrameMutex);
            if (g_dashIFrameApplied && currentHealth < g_dashIFrameLowestHealth) {
                g_dashIFrameLowestHealth = currentHealth;
                AppendNativeTrace(
                    "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                    " tier " + std::to_string(tier) +
                    " iframes_health_drop route=" + std::string(routeTag) +
                    " health=" + std::to_string(currentHealth) +
                    " protectedHealth=" + std::to_string(g_dashIFrameProtectedHealth));
            }
        }
    }

    static void EndDashIFrames(std::string_view routeTag)
    {
        float protectedHealth = 0.0f;
        float lowestHealth = 0.0f;
        bool restoreGhost = false;
        RE::ObjectRefHandle actorHandle{};
        std::int32_t tier = 0;
        ActivationMode mode = ActivationMode::kHotkey;
        std::int32_t hitCount = 0;
        std::int32_t noDamageHitCount = 0;
        std::int32_t healthDeltaHitCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_dashIFrameMutex);
            if (!g_dashIFrameApplied) {
                return;
            }
            protectedHealth = g_dashIFrameProtectedHealth;
            lowestHealth = g_dashIFrameLowestHealth;
            restoreGhost = g_dashIFrameRestoreGhost;
            actorHandle = g_dashIFrameActorHandle;
            tier = g_dashIFrameTier;
            mode = g_dashIFrameMode;
            hitCount = g_dashIFrameHitCount;
            noDamageHitCount = g_dashIFrameNoDamageHitCount;
            healthDeltaHitCount = g_dashIFrameHealthDeltaHitCount;
            g_dashIFrameApplied = false;
            g_dashIFrameProtectedHealth = 0.0f;
            g_dashIFrameLowestHealth = 0.0f;
            g_dashIFrameRestoreGhost = false;
            g_dashIFrameActorHandle.reset();
            g_dashIFrameTier = 0;
            g_dashIFrameMode = ActivationMode::kHotkey;
            g_dashIFrameHitCount = 0;
            g_dashIFrameNoDamageHitCount = 0;
            g_dashIFrameHealthDeltaHitCount = 0;
        }

        if (restoreGhost) {
            auto clearGhost = [actorHandle]() mutable {
                auto actorPtr = actorHandle.get();
                if (!actorPtr) {
                    return;
                }
                if (auto* actor = skyrim_cast<RE::Actor*>(actorPtr.get())) {
                    (void)SetActorGhostState(actor, false);
                }
            };
            if (auto* task = SKSE::GetTaskInterface(); task) {
                task->AddTask(clearGhost);
            } else {
                clearGhost();
            }
        }

        const float maxHealthDrop = std::max(0.0f, protectedHealth - lowestHealth);
        logger::info(
            "DodgeBlink: {} tier {} iframes end route={} protectedHealth={:.2f} lowestHealth={:.2f} maxHealthDrop={:.2f} hits={} noDamageHits={} healthDeltaHits={} ghostCleared={}.",
            ActivationModeName(mode),
            tier,
            routeTag,
            protectedHealth,
            lowestHealth,
            maxHealthDrop,
            hitCount,
            noDamageHitCount,
            healthDeltaHitCount,
            restoreGhost ? 1 : 0);
        AppendNativeTrace(
            "DodgeBlink " + std::string(ActivationModeName(mode)) +
            " tier " + std::to_string(tier) +
            " iframes_end route=" + std::string(routeTag) +
            " protectedHealth=" + std::to_string(protectedHealth) +
            " lowestHealth=" + std::to_string(lowestHealth) +
            " maxHealthDrop=" + std::to_string(maxHealthDrop) +
            " hits=" + std::to_string(hitCount) +
            " noDamageHits=" + std::to_string(noDamageHitCount) +
            " healthDeltaHits=" + std::to_string(healthDeltaHitCount) +
            " ghostCleared=" + std::to_string(restoreGhost ? 1 : 0));
    }

    static void ReleaseDashGate(std::string_view routeTag)
    {
        EndDashIFrames(routeTag);
        g_dashGate.store(false, std::memory_order_release);
    }

    static bool SetControllerVelocity(RE::Actor* actor, const RE::NiPoint3& velocity)
    {
        if (!actor) {
            return false;
        }
        auto* controller = actor->GetCharController();
        if (!controller) {
            return false;
        }
        controller->SetLinearVelocityImpl(RE::hkVector4(velocity));
        return true;
    }

    static RE::NiPoint3 GetControllerVelocity(RE::Actor* actor)
    {
        if (!actor) {
            return RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
        }

        auto* controller = actor->GetCharController();
        if (!controller) {
            return RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
        }

        RE::hkVector4 rawVelocity{};
        controller->GetLinearVelocityImpl(rawVelocity);
        alignas(16) float components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        _mm_storeu_ps(components, rawVelocity.quad);
        return RE::NiPoint3{ components[0], components[1], components[2] };
    }

    static RE::hkpWorld* GetActorHavokWorld(RE::Actor* actor)
    {
        if (!actor) {
            return nullptr;
        }

        if (auto* controller = actor->GetCharController(); controller) {
            if (auto* proxyController = skyrim_cast<RE::bhkCharProxyController*>(controller)) {
                if (auto* proxy = proxyController->GetCharacterProxy(); proxy && proxy->shapePhantom && proxy->shapePhantom->world) {
                    return proxy->shapePhantom->world;
                }
                if (auto* proxyWorld = proxyController->proxy.GetWorld1(); proxyWorld) {
                    return proxyWorld;
                }
            }
            if (auto* supportBody = controller->supportBody.get(); supportBody && supportBody->world) {
                return supportBody->world;
            }
        }

        auto* cell = actor->GetParentCell();
        if (!cell) {
            return nullptr;
        }

        auto* bhkWorld = cell->GetbhkWorld();
        if (!bhkWorld) {
            return nullptr;
        }

        return bhkWorld->GetWorld1();
    }

    static bool GetControllerCollisionFilterInfo(RE::Actor* actor, std::uint32_t& filterInfoOut)
    {
        filterInfoOut = 0;
        if (!actor) {
            return false;
        }

        auto* controller = actor->GetCharController();
        if (!controller) {
            return false;
        }

        controller->GetCollisionFilterInfo(filterInfoOut);
        return true;
    }

    static bool CastWorldRay(
        RE::hkpWorld* world,
        const RE::NiPoint3& from,
        const RE::NiPoint3& to,
        std::uint32_t filterInfo,
        float& hitFractionOut,
        RE::NiPoint3* hitPointOut = nullptr)
    {
        hitFractionOut = 1.0f;
        if (!world) {
            return false;
        }

        RE::hkpWorldRayCastInput input{};
        input.from = RE::hkVector4(from.x, from.y, from.z, 0.0f);
        input.to = RE::hkVector4(to.x, to.y, to.z, 0.0f);
        auto tryCast = [&](std::uint32_t candidateFilter, bool enableShapeCollectionFilter) -> bool {
            input.filterInfo = candidateFilter;
            input.enableShapeCollectionFilter = enableShapeCollectionFilter;
            RE::hkpWorldRayCastOutput output{};
            output.Reset();
            world->CastRay(input, output);
            if (!output.HasHit()) {
                return false;
            }

            const float hitFraction = Clamp(output.hitFraction, 0.0f, 1.0f);
            if (hitFraction >= 1.0f) {
                return false;
            }

            hitFractionOut = hitFraction;
            if (hitPointOut) {
                const RE::NiPoint3 delta = to - from;
                *hitPointOut = from + delta * hitFraction;
            }
            return true;
        };

        for (bool useShapeFilter : { true, false }) {
            if (tryCast(filterInfo, useShapeFilter)) {
                return true;
            }
            if (filterInfo != 0 && tryCast(0, useShapeFilter)) {
                return true;
            }
            if (filterInfo != 0xFFFFFFFF && tryCast(0xFFFFFFFF, useShapeFilter)) {
                return true;
            }
        }
        return false;
    }

    static bool TrySampleGroundPoint(
        RE::hkpWorld* world,
        const RE::NiPoint3& aroundPosition,
        const RE::NiPoint3& movementDir,
        std::uint32_t filterInfo,
        RE::NiPoint3& groundPointOut)
    {
        if (!world) {
            return false;
        }

        const RE::NiPoint3 right{ movementDir.y, -movementDir.x, 0.0f };
        const std::array<RE::NiPoint3, 5> offsets{
            RE::NiPoint3{ 0.0f, 0.0f, 0.0f },
            movementDir * 18.0f,
            movementDir * -18.0f,
            right * 18.0f,
            right * -18.0f
        };

        constexpr float kProbeLift = 96.0f;
        constexpr float kProbeDrop = 4096.0f;
        constexpr float kMinValidDrop = 2.0f;
        constexpr float kMaxAbovePosition = 256.0f;

        bool hasBest = false;
        float bestGroundZ = 0.0f;
        RE::NiPoint3 bestHit{};
        for (const auto& offset : offsets) {
            const RE::NiPoint3 from{
                aroundPosition.x + offset.x,
                aroundPosition.y + offset.y,
                aroundPosition.z + kProbeLift
            };
            const RE::NiPoint3 to{
                from.x,
                from.y,
                aroundPosition.z - kProbeDrop
            };

            float hitFraction = 1.0f;
            RE::NiPoint3 hitPoint{};
            if (!CastWorldRay(world, from, to, filterInfo, hitFraction, &hitPoint)) {
                continue;
            }

            const float drop = from.z - hitPoint.z;
            const float abovePosition = hitPoint.z - aroundPosition.z;
            if (drop < kMinValidDrop || abovePosition > kMaxAbovePosition) {
                // Likely hit own capsule/crowd body close to probe start; ignore.
                continue;
            }

            if (!hasBest || hitPoint.z > bestGroundZ) {
                hasBest = true;
                bestGroundZ = hitPoint.z;
                bestHit = hitPoint;
            }
        }

        if (!hasBest) {
            return false;
        }

        groundPointOut = bestHit;
        return true;
    }

    static bool IsFirstPersonForActor(RE::Actor* actor)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!actor || actor != player) {
            return false;
        }

        auto* camera = RE::PlayerCamera::GetSingleton();
        if (camera) {
            if (camera->IsInFirstPerson()) {
                return true;
            }
            if (camera->IsInThirdPerson()) {
                return false;
            }
        }

        std::int32_t iFirstPerson = 0;
        if (actor->GetGraphVariableInt(RE::BSFixedString("i1stPerson"), iFirstPerson)) {
            return iFirstPerson != 0;
        }

        bool graphFirstPerson = false;
        if (actor->GetGraphVariableBool(RE::BSFixedString("IsFirstPerson"), graphFirstPerson)) {
            return graphFirstPerson;
        }

        float graphFirstPersonFloat = 0.0f;
        if (actor->GetGraphVariableFloat(RE::BSFixedString("fIsFirstPerson"), graphFirstPersonFloat)) {
            return graphFirstPersonFloat > 0.5f;
        }

        return false;
    }

    enum class CardinalDirection : std::int32_t
    {
        kForward = 1,
        kRight = 2,
        kBackward = 3,
        kLeft = 4,
        kRightForward = 5,
        kRightBackward = 6,
        kLeftBackward = 7,
        kLeftForward = 8
    };

    static CardinalDirection ResolveCardinalDirection(RE::Actor* actor, const RE::NiPoint3& worldDirection, bool allowEightWay = false)
    {
        if (!actor) {
            return CardinalDirection::kForward;
        }

        const auto dir = NormalizeXZ(worldDirection);
        const auto forward = NormalizeXZ(GetFacingDirectionXZ(actor));
        const RE::NiPoint3 right{ forward.y, -forward.x, 0.0f };

        const float localX = DotXZ(dir, right);
        const float localY = DotXZ(dir, forward);
        
        if (!allowEightWay) {
            // Original 4-way logic
            if (std::abs(localY) >= std::abs(localX)) {
                return localY >= 0.0f ? CardinalDirection::kForward : CardinalDirection::kBackward;
            }
            return localX >= 0.0f ? CardinalDirection::kRight : CardinalDirection::kLeft;
        } else {
            // Enhanced 8-way logic
            constexpr float diagonalThreshold = 0.4142f; // tan(22.5 deg) - threshold for diagonal vs cardinal
            
            if (std::abs(localX) < diagonalThreshold && std::abs(localY) < diagonalThreshold) {
                return CardinalDirection::kForward; // Near center, default to forward
            }
            
            const bool isForward = localY > 0.0f;
            const bool isRight = localX > 0.0f;
            
            if (std::abs(localX) > std::abs(localY)) {
                // More horizontal than vertical
                if (std::abs(localY) < diagonalThreshold * std::abs(localX)) {
                    // Pure cardinal direction
                    return isRight ? CardinalDirection::kRight : CardinalDirection::kLeft;
                } else {
                    // Diagonal direction
                    if (isForward && isRight) return CardinalDirection::kRightForward;
                    if (!isForward && isRight) return CardinalDirection::kRightBackward;
                    if (!isForward && !isRight) return CardinalDirection::kLeftBackward;
                    return CardinalDirection::kLeftForward;
                }
            } else {
                // More vertical than horizontal
                if (std::abs(localX) < diagonalThreshold * std::abs(localY)) {
                    // Pure cardinal direction
                    return isForward ? CardinalDirection::kForward : CardinalDirection::kBackward;
                } else {
                    // Diagonal direction
                    if (isForward && isRight) return CardinalDirection::kRightForward;
                    if (!isForward && isRight) return CardinalDirection::kRightBackward;
                    if (!isForward && !isRight) return CardinalDirection::kLeftBackward;
                    return CardinalDirection::kLeftForward;
                }
            }
        }
    }

    static float ResolveSignedLocalDirectionAngle(RE::Actor* actor, const RE::NiPoint3& worldDirection)
    {
        if (!actor) {
            return 0.0f;
        }

        const auto dir = NormalizeXZ(worldDirection);
        const auto forward = NormalizeXZ(GetFacingDirectionXZ(actor));
        const RE::NiPoint3 right{ forward.y, -forward.x, 0.0f };
        const float localX = DotXZ(dir, right);
        const float localY = DotXZ(dir, forward);

        constexpr float kRadToDeg = 57.29577951308232f;
        return std::atan2(localX, localY) * kRadToDeg;
    }

    static const char* CardinalDirectionName(CardinalDirection direction)
    {
        switch (direction) {
        case CardinalDirection::kRightForward:
            return "right_forward";
        case CardinalDirection::kRight:
            return "right";
        case CardinalDirection::kRightBackward:
            return "right_backward";
        case CardinalDirection::kBackward:
            return "backward";
        case CardinalDirection::kLeftBackward:
            return "left_backward";
        case CardinalDirection::kLeft:
            return "left";
        case CardinalDirection::kLeftForward:
            return "left_forward";
        case CardinalDirection::kForward:
        default:
            return "forward";
        }
    }

    static const char* ResolveTkTakeoverEventName(CardinalDirection direction)
    {
        switch (direction) {
        case CardinalDirection::kRight:
            return "TKDodgeRight";
        case CardinalDirection::kBackward:
            return "TKDodgeBack";
        case CardinalDirection::kLeft:
            return "TKDodgeLeft";
        case CardinalDirection::kForward:
        default:
            return "TKDodgeForward";
        }
    }

    static bool ShouldUseStepForTkTakeoverDirection(TakeoverStyle style, CardinalDirection direction)
    {
        switch (style) {
        case TakeoverStyle::kTkDefaultFullRoll:
            return false;
        case TakeoverStyle::kStepAndForwardRoll:
            return direction != CardinalDirection::kForward;
        case TakeoverStyle::kFullStep:
            return true;
        default:
            return false;
        }
    }

    static bool HasTkTakeoverAssetsForStyle(TakeoverStyle style, std::string& missingOut)
    {
        const auto root = std::filesystem::path("Data") / "Meshes" / "Actors" / "Character" / "animations";
        const auto rollBack = root / "dodgeback.hkx";
        const auto rollLeft = root / "dodgeleft.hkx";
        const auto rollRight = root / "dodgeright.hkx";
        const auto stepBack = root / "TKDodge" / "StepDodgeBack.hkx";
        const auto stepLeft = root / "TKDodge" / "StepDodgeLeft.hkx";
        const auto stepRight = root / "TKDodge" / "StepDodgeRight.hkx";
        const auto stepForward = root / "TKDodge" / "StepDodgeForward.hkx";

        const auto requireRollAssets = [&]() {
            return HasFilePath(rollBack) && HasFilePath(rollLeft) && HasFilePath(rollRight);
        };
        const auto requireStepBackSides = [&]() {
            return HasFilePath(stepBack) && HasFilePath(stepLeft) && HasFilePath(stepRight);
        };

        switch (style) {
        case TakeoverStyle::kTkDefaultFullRoll:
            if (!requireRollAssets()) {
                missingOut = "missing TK roll assets (dodgeback/left/right.hkx)";
                return false;
            }
            return true;
        case TakeoverStyle::kStepAndForwardRoll:
            if (!requireRollAssets()) {
                missingOut = "missing TK roll assets (dodgeback/left/right.hkx)";
                return false;
            }
            if (!requireStepBackSides()) {
                missingOut = "missing TK step assets for back/left/right";
                return false;
            }
            return true;
        case TakeoverStyle::kFullStep:
            if (!requireStepBackSides() || !HasFilePath(stepForward)) {
                missingOut = "missing TK step assets (StepDodgeForward/Back/Left/Right.hkx)";
                return false;
            }
            return true;
        default:
            missingOut = "invalid takeover style";
            return false;
        }
    }

    static bool HasDmcoTakeoverAssets(DmcoTakeoverStyle style, std::string& missingOut)
    {
        const auto behaviorRoot = std::filesystem::path("Data") / "Meshes" / "Actors" / "character" / "behaviors";
        const auto animationRoot = std::filesystem::path("Data") / "Meshes" / "Actors" / "character" / "animations";
        const auto dmcoBehavior = behaviorRoot / "DMCO.hkx";
        const auto dmcoDodgeBehavior = behaviorRoot / "DMCO_Dodge.hkx";
        const char* styleSuffix = style == DmcoTakeoverStyle::kSet2 ? "2" : "1";

        if (!HasFilePath(dmcoBehavior) || !HasFilePath(dmcoDodgeBehavior)) {
            missingOut = "missing DMCO behavior files (DMCO.hkx / DMCO_Dodge.hkx)";
            return false;
        }

        constexpr std::array<const char*, 9> kDmcoDirectionTokens{
            "N", "F", "RF", "R", "RB", "B", "LB", "L", "LF"
        };
        const auto hasDmcoClip = [&](const std::string& filename) {
            // DMCO animation packs are commonly installed either as loose clips under
            // character\animations or inside DAR custom-condition folders.
            if (HasFilePath(animationRoot / filename)) {
                return true;
            }

            const auto darConditionsRoot = animationRoot / "DynamicAnimationReplacer" / "_CustomConditions";
            std::error_code ec;
            if (!std::filesystem::exists(darConditionsRoot, ec) || !std::filesystem::is_directory(darConditionsRoot, ec)) {
                return false;
            }

            for (const auto& conditionDir : std::filesystem::directory_iterator(darConditionsRoot, ec)) {
                if (ec) {
                    break;
                }
                if (!conditionDir.is_directory()) {
                    continue;
                }
                if (HasFilePath(conditionDir.path() / filename)) {
                    return true;
                }
            }
            return false;
        };
        for (const char* token : kDmcoDirectionTokens) {
            std::string filename = "MCO_Dodge-";
            filename += token;
            filename += "-";
            filename += styleSuffix;
            filename += ".hkx";
            if (!hasDmcoClip(filename)) {
                missingOut = "missing DMCO style assets (" + filename + " in animations root or DAR _CustomConditions)";
                return false;
            }
        }
        return true;
    }

    static std::int32_t ResolveDmcoDirectionValue(CardinalDirection direction)
    {
        // DMCO directional index order:
        // 0=N, 1=F, 2=RF, 3=R, 4=RB, 5=B, 6=LB, 7=L, 8=LF
        std::int32_t baseDirection = 1;
        switch (direction) {
        case CardinalDirection::kRightForward:
            baseDirection = 2;
            break;
        case CardinalDirection::kRight:
            baseDirection = 3;
            break;
        case CardinalDirection::kRightBackward:
            baseDirection = 4;
            break;
        case CardinalDirection::kBackward:
            baseDirection = 5;
            break;
        case CardinalDirection::kLeftBackward:
            baseDirection = 6;
            break;
        case CardinalDirection::kLeft:
            baseDirection = 7;
            break;
        case CardinalDirection::kLeftForward:
            baseDirection = 8;
            break;
        case CardinalDirection::kForward:
        default:
            baseDirection = 1;
            break;
        }
        return baseDirection;
    }

    static std::int32_t ResolveDmcoStyleValue(DmcoTakeoverStyle style)
    {
        // DMCO root graph uses MCO_dodge / MCO_nextdodge to select
        // Behaviors\DMCO_Dodge1.hkx (1) vs Behaviors\DMCO_Dodge2.hkx (2).
        return style == DmcoTakeoverStyle::kSet2 ? 2 : 1;
    }

    struct DmcoRuntimeGraphState
    {
        bool hadDirection{ false };
        float direction{ 0.0f };
        bool hadDodgeAngle{ false };
        float dodgeAngle{ 0.0f };
        bool hadDodgeSpeed{ false };
        float dodgeSpeed{ 1.0f };
        bool hadStyle{ false };
        std::int32_t style{ 1 };
        bool hadNextStyle{ false };
        std::int32_t nextStyle{ 1 };
        bool hadDodgeDirection{ false };
        std::int32_t dodgeDirection{ 0 };
        bool hadMcoDodgeDirection{ false };
        std::int32_t mcoDodgeDirection{ 0 };
    };

    struct DmcoRuntimeGraphApplyResult
    {
        bool rootDirectionSet{ false };
        bool rootDodgeAngleSet{ false };
        bool rootDodgeSpeedSet{ false };
        bool rootStyleSet{ false };
        bool rootNextStyleSet{ false };
        bool rootDodgeDirectionSet{ false };
        bool rootMcoDodgeDirectionSet{ false };
        std::int32_t visitedGraphs{ 0 };
        std::int32_t graphDirectionSet{ 0 };
        std::int32_t graphDodgeAngleSet{ 0 };
        std::int32_t graphDodgeSpeedSet{ 0 };
        std::int32_t graphStyleSet{ 0 };
        std::int32_t graphNextStyleSet{ 0 };
        std::int32_t graphDodgeDirectionSet{ 0 };
        std::int32_t graphMcoDodgeDirectionSet{ 0 };
    };

    static DmcoRuntimeGraphState CaptureDmcoRuntimeGraphState(RE::Actor* actor)
    {
        DmcoRuntimeGraphState state{};
        if (!actor) {
            return state;
        }

        static const RE::BSFixedString kDirectionVar("Direction");
        static const RE::BSFixedString kDodgeAngleVar("Dodge_Angle");
        static const RE::BSFixedString kDmcoDodgeSpeedVar("MCO_DodgeSpeed");
        static const RE::BSFixedString kDmcoStyleVar("MCO_dodge");
        static const RE::BSFixedString kDmcoNextStyleVar("MCO_nextdodge");
        static const RE::BSFixedString kDodgeDirectionVar("Dodge_Direction");
        static const RE::BSFixedString kMcoDodgeDirectionVar("MCO_DodgeDirection");

        state.hadDirection = actor->GetGraphVariableFloat(kDirectionVar, state.direction);
        state.hadDodgeAngle = actor->GetGraphVariableFloat(kDodgeAngleVar, state.dodgeAngle);
        state.hadDodgeSpeed = actor->GetGraphVariableFloat(kDmcoDodgeSpeedVar, state.dodgeSpeed);
        state.hadStyle = actor->GetGraphVariableInt(kDmcoStyleVar, state.style);
        state.hadNextStyle = actor->GetGraphVariableInt(kDmcoNextStyleVar, state.nextStyle);
        state.hadDodgeDirection = actor->GetGraphVariableInt(kDodgeDirectionVar, state.dodgeDirection);
        state.hadMcoDodgeDirection = actor->GetGraphVariableInt(kMcoDodgeDirectionVar, state.mcoDodgeDirection);
        return state;
    }

    static DmcoRuntimeGraphApplyResult ReapplyDmcoRuntimeGraphVars(
        RE::Actor* actor,
        float dmcoDirectionAngle,
        std::int32_t dmcoStyleValue,
        std::int32_t dmcoDirectionValue)
    {
        DmcoRuntimeGraphApplyResult result{};
        if (!actor) {
            return result;
        }

        static const RE::BSFixedString kDirectionVar("Direction");
        static const RE::BSFixedString kDodgeAngleVar("Dodge_Angle");
        static const RE::BSFixedString kDmcoDodgeSpeedVar("MCO_DodgeSpeed");
        static const RE::BSFixedString kDmcoStyleVar("MCO_dodge");
        static const RE::BSFixedString kDmcoNextStyleVar("MCO_nextdodge");
        static const RE::BSFixedString kDodgeDirectionVar("Dodge_Direction");
        static const RE::BSFixedString kMcoDodgeDirectionVar("MCO_DodgeDirection");

        result.rootDirectionSet = actor->SetGraphVariableFloat(kDirectionVar, dmcoDirectionAngle);
        result.rootDodgeAngleSet = actor->SetGraphVariableFloat(kDodgeAngleVar, dmcoDirectionAngle);
        result.rootDodgeSpeedSet = actor->SetGraphVariableFloat(kDmcoDodgeSpeedVar, 1.0f);
        result.rootStyleSet = actor->SetGraphVariableInt(kDmcoStyleVar, dmcoStyleValue);
        result.rootNextStyleSet = actor->SetGraphVariableInt(kDmcoNextStyleVar, dmcoStyleValue);
        result.rootDodgeDirectionSet = actor->SetGraphVariableInt(kDodgeDirectionVar, dmcoDirectionValue);
        result.rootMcoDodgeDirectionSet = actor->SetGraphVariableInt(kMcoDodgeDirectionVar, dmcoDirectionValue);
        return result;
    }

    static void RestoreDmcoRuntimeGraphVars(RE::Actor* actor, const DmcoRuntimeGraphState& state)
    {
        if (!actor) {
            return;
        }

        static const RE::BSFixedString kDirectionVar("Direction");
        static const RE::BSFixedString kDodgeAngleVar("Dodge_Angle");
        static const RE::BSFixedString kDmcoDodgeSpeedVar("MCO_DodgeSpeed");
        static const RE::BSFixedString kDmcoStyleVar("MCO_dodge");
        static const RE::BSFixedString kDmcoNextStyleVar("MCO_nextdodge");
        static const RE::BSFixedString kDodgeDirectionVar("Dodge_Direction");
        static const RE::BSFixedString kMcoDodgeDirectionVar("MCO_DodgeDirection");

        if (state.hadDirection) {
            actor->SetGraphVariableFloat(kDirectionVar, state.direction);
        }
        if (state.hadDodgeAngle) {
            actor->SetGraphVariableFloat(kDodgeAngleVar, state.dodgeAngle);
        }
        if (state.hadDodgeSpeed) {
            actor->SetGraphVariableFloat(kDmcoDodgeSpeedVar, state.dodgeSpeed);
        }
        if (state.hadStyle) {
            actor->SetGraphVariableInt(kDmcoStyleVar, state.style);
        }
        if (state.hadNextStyle) {
            actor->SetGraphVariableInt(kDmcoNextStyleVar, state.nextStyle);
        }
        if (state.hadDodgeDirection) {
            actor->SetGraphVariableInt(kDodgeDirectionVar, state.dodgeDirection);
        }
        if (state.hadMcoDodgeDirection) {
            actor->SetGraphVariableInt(kMcoDodgeDirectionVar, state.mcoDodgeDirection);
        }
    }

    static bool IsWeaponDrawnForActor(RE::Actor* actor)
    {
        if (!actor) {
            return false;
        }
        if (auto* actorState = actor->AsActorState(); actorState) {
            return actorState->IsWeaponDrawn();
        }
        return true;
    }

    static void CompleteTkTakeoverDashAfter(
        float durationSeconds,
        std::int32_t tier,
        ActivationMode triggerMode,
        CardinalDirection direction,
        bool useStep,
        TakeoverStyle style)
    {
        const float clampedDuration = Clamp(durationSeconds, 0.10f, 2.00f);
        std::thread([clampedDuration, tier, triggerMode, direction, useStep, style]() {
            const int maintainTicks = std::max(1, static_cast<int>(std::ceil(clampedDuration / 0.01f)));
            for (int tick = 0; tick < maintainTicks; ++tick) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (auto* task = SKSE::GetTaskInterface(); task) {
                    task->AddTask([tier, triggerMode]() {
                        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                            MaintainDashIFrames(player, tier, triggerMode, "takeover_step");
                        }
                    });
                }
            }
            if (auto* task = SKSE::GetTaskInterface(); task) {
                task->AddTask([clampedDuration, tier, triggerMode, direction, useStep, style]() {
                    ReleaseDashGate("takeover_end_task");
                    logger::info(
                        "DodgeBlink: {} tier {} takeover end style={} dir={} step={} duration={:.3f}s",
                        ActivationModeName(triggerMode),
                        tier,
                        TakeoverStyleName(style),
                        CardinalDirectionName(direction),
                        useStep ? 1 : 0,
                        clampedDuration);
                    AppendNativeTrace(
                        "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                        " tier " + std::to_string(tier) +
                        " takeover_end style=" + std::string(TakeoverStyleName(style)) +
                        " dir=" + std::string(CardinalDirectionName(direction)) +
                        " step=" + std::to_string(useStep ? 1 : 0) +
                        " duration=" + std::to_string(clampedDuration));
                });
                return;
            }

            ReleaseDashGate("takeover_end_thread");
            logger::info(
                "DodgeBlink: {} tier {} takeover end style={} dir={} step={} duration={:.3f}s",
                ActivationModeName(triggerMode),
                tier,
                TakeoverStyleName(style),
                CardinalDirectionName(direction),
                useStep ? 1 : 0,
                clampedDuration);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover_end style=" + std::string(TakeoverStyleName(style)) +
                " dir=" + std::string(CardinalDirectionName(direction)) +
                " step=" + std::to_string(useStep ? 1 : 0) +
                " duration=" + std::to_string(clampedDuration));
        }).detach();
    }

    static void CompleteDmcoTakeoverDashAfter(
        float durationSeconds,
        std::int32_t tier,
        ActivationMode triggerMode,
        CardinalDirection direction,
        float dmcoDirectionAngle,
        std::int32_t dmcoDirectionValue,
        std::int32_t dmcoStyleValue,
        DmcoTakeoverStyle style,
        DmcoRuntimeGraphState runtimeState)
    {
        const float clampedDuration = Clamp(durationSeconds, 0.10f, 2.00f);
        std::thread([clampedDuration, tier, triggerMode, direction, dmcoDirectionAngle, dmcoDirectionValue, dmcoStyleValue, style, runtimeState]() {
            constexpr int kPollMs = 10;
            const int fallbackTicks = std::max(1, static_cast<int>(std::ceil(clampedDuration / 0.01f)));
            const int maxWaitTicks = std::max(fallbackTicks * 8, 500);
            bool observedBusy = false;
            bool releaseByGraphClear = false;
            bool releaseByDurationFallback = false;
            int waitedTicks = 0;

            for (int tick = 0; tick < maxWaitTicks; ++tick) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
                waitedTicks = tick + 1;

                if (auto* task = SKSE::GetTaskInterface(); task) {
                    task->AddTask([tier, triggerMode, dmcoDirectionAngle, dmcoDirectionValue, dmcoStyleValue]() {
                        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                            ReapplyDmcoRuntimeGraphVars(player, dmcoDirectionAngle, dmcoStyleValue, dmcoDirectionValue);
                            MaintainDashIFrames(player, tier, triggerMode, "takeover_dmco_step");
                        }
                    });
                }

                DmcoGraphStateSnapshot snapshot{};
                if (QueryDmcoGraphStateOnGameThread(std::chrono::milliseconds(200), snapshot)) {
                    const bool hasSignal = snapshot.hasAlwaysOn || snapshot.hasRecovery;
                    const bool graphBusy = IsDmcoGraphBusy(snapshot);
                    if (hasSignal) {
                        if (graphBusy) {
                            observedBusy = true;
                        } else if (observedBusy) {
                            releaseByGraphClear = true;
                            break;
                        }
                    }
                }

                if (!observedBusy && waitedTicks >= fallbackTicks) {
                    releaseByDurationFallback = true;
                    break;
                }
            }

            const bool releaseByTimeout = !releaseByGraphClear && !releaseByDurationFallback;
            const char* releaseReason = releaseByGraphClear ? "graph_clear" :
                releaseByDurationFallback ? "duration_fallback" :
                                            "timeout";
            const char* releaseRouteTag = releaseByGraphClear ? "takeover_dmco_end_graph_clear" :
                releaseByDurationFallback ? "takeover_dmco_end_fallback" :
                                            "takeover_dmco_end_timeout";

            if (auto* task = SKSE::GetTaskInterface(); task) {
                task->AddTask([clampedDuration, tier, triggerMode, direction, dmcoDirectionAngle, style, runtimeState, releaseReason, releaseRouteTag, observedBusy, waitedTicks, fallbackTicks, releaseByTimeout]() {
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        RestoreDmcoRuntimeGraphVars(player, runtimeState);
                    }
                    ReleaseDashGate(releaseRouteTag);
                    if (releaseByTimeout) {
                        logger::warn(
                            "DodgeBlink: {} tier {} takeover(dmco) end style={} dir={} angle={:.1f} duration={:.3f}s reason={} observedBusy={} waitedTicks={} fallbackTicks={}",
                            ActivationModeName(triggerMode),
                            tier,
                            DmcoTakeoverStyleName(style),
                            CardinalDirectionName(direction),
                            dmcoDirectionAngle,
                            clampedDuration,
                            releaseReason,
                            observedBusy ? 1 : 0,
                            waitedTicks,
                            fallbackTicks);
                    } else {
                        logger::info(
                            "DodgeBlink: {} tier {} takeover(dmco) end style={} dir={} angle={:.1f} duration={:.3f}s reason={} observedBusy={} waitedTicks={} fallbackTicks={}",
                            ActivationModeName(triggerMode),
                            tier,
                            DmcoTakeoverStyleName(style),
                            CardinalDirectionName(direction),
                            dmcoDirectionAngle,
                            clampedDuration,
                            releaseReason,
                            observedBusy ? 1 : 0,
                            waitedTicks,
                            fallbackTicks);
                    }
                    AppendNativeTrace(
                        "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                        " tier " + std::to_string(tier) +
                        " takeover_end provider=dmco style=" + std::string(DmcoTakeoverStyleName(style)) +
                        " dir=" + std::string(CardinalDirectionName(direction)) +
                        " angle=" + std::to_string(dmcoDirectionAngle) +
                        " duration=" + std::to_string(clampedDuration) +
                        " release_reason=" + std::string(releaseReason) +
                        " observed_busy=" + std::to_string(observedBusy ? 1 : 0) +
                        " waited_ticks=" + std::to_string(waitedTicks) +
                        " fallback_ticks=" + std::to_string(fallbackTicks));
                });
                return;
            }

            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                RestoreDmcoRuntimeGraphVars(player, runtimeState);
            }
            ReleaseDashGate(releaseRouteTag);
            if (releaseByTimeout) {
                logger::warn(
                    "DodgeBlink: {} tier {} takeover(dmco) end style={} dir={} angle={:.1f} duration={:.3f}s reason={} observedBusy={} waitedTicks={} fallbackTicks={}",
                    ActivationModeName(triggerMode),
                    tier,
                    DmcoTakeoverStyleName(style),
                    CardinalDirectionName(direction),
                    dmcoDirectionAngle,
                    clampedDuration,
                    releaseReason,
                    observedBusy ? 1 : 0,
                    waitedTicks,
                    fallbackTicks);
            } else {
                logger::info(
                    "DodgeBlink: {} tier {} takeover(dmco) end style={} dir={} angle={:.1f} duration={:.3f}s reason={} observedBusy={} waitedTicks={} fallbackTicks={}",
                    ActivationModeName(triggerMode),
                    tier,
                    DmcoTakeoverStyleName(style),
                    CardinalDirectionName(direction),
                    dmcoDirectionAngle,
                    clampedDuration,
                    releaseReason,
                    observedBusy ? 1 : 0,
                    waitedTicks,
                    fallbackTicks);
            }
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover_end provider=dmco style=" + std::string(DmcoTakeoverStyleName(style)) +
                " dir=" + std::string(CardinalDirectionName(direction)) +
                " angle=" + std::to_string(dmcoDirectionAngle) +
                " duration=" + std::to_string(clampedDuration) +
                " release_reason=" + std::string(releaseReason) +
                " observed_busy=" + std::to_string(observedBusy ? 1 : 0) +
                " waited_ticks=" + std::to_string(waitedTicks) +
                " fallback_ticks=" + std::to_string(fallbackTicks));
        }).detach();
    }

    static bool StartTkTakeoverDodge(RE::Actor* actor, const RE::NiPoint3& desiredDir, std::int32_t tier, ActivationMode triggerMode)
    {
        if (!actor) {
            return false;
        }

        const Config cfg = GetConfigSnapshot();
        const auto style = ResolveTakeoverStyle(cfg);
        const bool allowSheathed = IsTakeoverAllowSheathedEnabled(cfg);
        if (!allowSheathed && !IsWeaponDrawnForActor(actor)) {
            logger::info(
                "DodgeBlink: {} tier {} takeover blocked while sheathed (AllowSheathed=0).",
                ActivationModeName(triggerMode),
                tier);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) + " takeover blocked sheathed");
            return false;
        }

        std::string missingReason;
        if (!HasTkTakeoverAssetsForStyle(style, missingReason)) {
            logger::warn(
                "DodgeBlink: {} tier {} takeover blocked ({}).",
                ActivationModeName(triggerMode),
                tier,
                missingReason);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover blocked assets=" + missingReason);
            return false;
        }

        const auto direction = ResolveCardinalDirection(actor, desiredDir, false);
        const bool useStep = ShouldUseStepForTkTakeoverDirection(style, direction);
        const bool isFirstPerson = IsFirstPersonForActor(actor);
        const std::int32_t stepValue = useStep ? 2 : 0;
        static const RE::BSFixedString kStepVar("iStep");
        std::int32_t existingStep = 0;
        const bool hadStepVar = actor->GetGraphVariableInt(kStepVar, existingStep);
        const bool stepSet = actor->SetGraphVariableInt(kStepVar, stepValue);
        if (!hadStepVar || !stepSet) {
            logger::warn(
                "DodgeBlink: {} tier {} takeover iStep unresolved (hadVar={} setOk={} firstPerson={} requestedStep={}); continuing.",
                ActivationModeName(triggerMode),
                tier,
                hadStepVar ? 1 : 0,
                stepSet ? 1 : 0,
                isFirstPerson ? 1 : 0,
                stepValue);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover iStep_unresolved hadVar=" + std::to_string(hadStepVar ? 1 : 0) +
                " setOk=" + std::to_string(stepSet ? 1 : 0) +
                " firstPerson=" + std::to_string(isFirstPerson ? 1 : 0) +
                " requestedStep=" + std::to_string(stepValue));
        }

        const char* eventName = ResolveTkTakeoverEventName(direction);
        const bool dispatched = actor->NotifyAnimationGraph(RE::BSFixedString(eventName));
        if (!dispatched) {
            logger::warn(
                "DodgeBlink: {} tier {} takeover blocked (event {} rejected).",
                ActivationModeName(triggerMode),
                tier,
                eventName);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover blocked event_rejected=" + std::string(eventName));
            return false;
        }

        const float baseDuration = Clamp(cfg.tier[tier].dashDurationSeconds, 0.12f, 2.00f);
        const float duration = useStep ? Clamp(baseDuration * 0.85f, 0.12f, 2.00f) : baseDuration;
        logger::info(
            "DodgeBlink: {} tier {} takeover start style={} dir={} event={} step={} src={} duration={:.3f}s",
            ActivationModeName(triggerMode),
            tier,
            TakeoverStyleName(style),
            CardinalDirectionName(direction),
            eventName,
            stepValue,
            static_cast<std::int32_t>(g_lastDirectionSource),
            duration);
        AppendNativeTrace(
            "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
            " tier " + std::to_string(tier) +
            " takeover_start style=" + std::string(TakeoverStyleName(style)) +
            " dir=" + std::string(CardinalDirectionName(direction)) +
            " event=" + std::string(eventName) +
            " step=" + std::to_string(stepValue) +
            " dir_source=" + std::to_string(static_cast<std::int32_t>(g_lastDirectionSource)) +
                " duration=" + std::to_string(duration));
        BeginDashIFrames(actor, cfg, tier, triggerMode, "takeover_start");
        CompleteTkTakeoverDashAfter(duration, tier, triggerMode, direction, useStep, style);
        return true;
    }

    static bool StartDmcoTakeoverDodge(RE::Actor* actor, const RE::NiPoint3& desiredDir, std::int32_t tier, ActivationMode triggerMode)
    {
        if (!actor) {
            return false;
        }

        if (IsFirstPersonForActor(actor)) {
            logger::info(
                "DodgeBlink: {} tier {} takeover(dmco) blocked in first-person (DMCO takeover is third-person only).",
                ActivationModeName(triggerMode),
                tier);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover blocked provider=dmco first_person_only");
            return false;
        }

        const Config cfg = GetConfigSnapshot();
        const auto style = ResolveDmcoTakeoverStyle(cfg);
        const bool allowSheathed = IsTakeoverAllowSheathedEnabled(cfg);
        if (!allowSheathed && !IsWeaponDrawnForActor(actor)) {
            logger::info(
                "DodgeBlink: {} tier {} takeover(dmco) blocked while sheathed (AllowSheathed=0).",
                ActivationModeName(triggerMode),
                tier);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) + " takeover blocked provider=dmco sheathed");
            return false;
        }

        std::string missingReason;
        if (!HasDmcoTakeoverAssets(style, missingReason)) {
            logger::warn(
                "DodgeBlink: {} tier {} takeover(dmco) blocked ({}).",
                ActivationModeName(triggerMode),
                tier,
                missingReason);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover blocked provider=dmco assets=" + missingReason);
            return false;
        }

        const char* busyReason = "";
        if (IsDmcoGraphBusyForNewDodge(actor, busyReason)) {
            logger::info(
                "DodgeBlink: {} tier {} takeover(dmco) blocked (graph busy: {}).",
                ActivationModeName(triggerMode),
                tier,
                busyReason);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover blocked provider=dmco graph_busy=" + std::string(busyReason));
            return false;
        }

        const auto direction = ResolveCardinalDirection(actor, desiredDir, true);
        const std::int32_t dmcoDirectionValue = ResolveDmcoDirectionValue(direction);
        const std::int32_t dmcoStyleValue = ResolveDmcoStyleValue(style);
        const float dmcoDirectionAngle = ResolveSignedLocalDirectionAngle(actor, desiredDir);
        const DmcoRuntimeGraphState runtimeState = CaptureDmcoRuntimeGraphState(actor);
        const DmcoRuntimeGraphApplyResult preSetResult =
            ReapplyDmcoRuntimeGraphVars(actor, dmcoDirectionAngle, dmcoStyleValue, dmcoDirectionValue);

        static const RE::BSFixedString kDmcoEvent("Dodge");
        const bool dispatched = actor->NotifyAnimationGraph(kDmcoEvent);
        if (!dispatched) {
            RestoreDmcoRuntimeGraphVars(actor, runtimeState);
            logger::warn(
                "DodgeBlink: {} tier {} takeover(dmco) blocked (event rejected: Dodge).",
                ActivationModeName(triggerMode),
                tier);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) +
                " takeover blocked provider=dmco event_rejected=Dodge");
            return false;
        }

        // DMCO can mutate vars on Dodge entry, so reinforce requested angle/style/direction immediately.
        const DmcoRuntimeGraphApplyResult postSetResult =
            ReapplyDmcoRuntimeGraphVars(actor, dmcoDirectionAngle, dmcoStyleValue, dmcoDirectionValue);

        const float duration = Clamp(cfg.tier[tier].dashDurationSeconds, 0.12f, 2.00f);
        logger::info(
            "DodgeBlink: {} tier {} takeover(dmco) start style={} styleVal={} dir={} dmcoDir={} angle={:.1f} "
            "event=Dodge eventOk={} "
            "preRoot(dir={} angle={} dodgeDir={} mcoDir={} speed={} style={} nextStyle={}) "
            "preGraph(visits={} dir={} angle={} dodgeDir={} mcoDir={} speed={} style={} nextStyle={}) "
            "postRoot(dir={} angle={} dodgeDir={} mcoDir={} speed={} style={} nextStyle={}) "
            "postGraph(visits={} dir={} angle={} dodgeDir={} mcoDir={} speed={} style={} nextStyle={}) "
            "src={} duration={:.3f}s",
            ActivationModeName(triggerMode),
            tier,
            DmcoTakeoverStyleName(style),
            dmcoStyleValue,
            CardinalDirectionName(direction),
            dmcoDirectionValue,
            dmcoDirectionAngle,
            dispatched ? 1 : 0,
            preSetResult.rootDirectionSet ? 1 : 0,
            preSetResult.rootDodgeAngleSet ? 1 : 0,
            preSetResult.rootDodgeDirectionSet ? 1 : 0,
            preSetResult.rootMcoDodgeDirectionSet ? 1 : 0,
            preSetResult.rootDodgeSpeedSet ? 1 : 0,
            preSetResult.rootStyleSet ? 1 : 0,
            preSetResult.rootNextStyleSet ? 1 : 0,
            preSetResult.visitedGraphs,
            preSetResult.graphDirectionSet,
            preSetResult.graphDodgeAngleSet,
            preSetResult.graphDodgeDirectionSet,
            preSetResult.graphMcoDodgeDirectionSet,
            preSetResult.graphDodgeSpeedSet,
            preSetResult.graphStyleSet,
            preSetResult.graphNextStyleSet,
            postSetResult.rootDirectionSet ? 1 : 0,
            postSetResult.rootDodgeAngleSet ? 1 : 0,
            postSetResult.rootDodgeDirectionSet ? 1 : 0,
            postSetResult.rootMcoDodgeDirectionSet ? 1 : 0,
            postSetResult.rootDodgeSpeedSet ? 1 : 0,
            postSetResult.rootStyleSet ? 1 : 0,
            postSetResult.rootNextStyleSet ? 1 : 0,
            postSetResult.visitedGraphs,
            postSetResult.graphDirectionSet,
            postSetResult.graphDodgeAngleSet,
            postSetResult.graphDodgeDirectionSet,
            postSetResult.graphMcoDodgeDirectionSet,
            postSetResult.graphDodgeSpeedSet,
            postSetResult.graphStyleSet,
            postSetResult.graphNextStyleSet,
            static_cast<std::int32_t>(g_lastDirectionSource),
            duration);
        AppendNativeTrace(
            "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
            " tier " + std::to_string(tier) +
            " takeover_start provider=dmco style=" + std::string(DmcoTakeoverStyleName(style)) +
            " style_value=" + std::to_string(dmcoStyleValue) +
            " dir=" + std::string(CardinalDirectionName(direction)) +
            " dmco_dir=" + std::to_string(dmcoDirectionValue) +
            " angle=" + std::to_string(dmcoDirectionAngle) +
            " pre_root_dir_set=" + std::to_string(preSetResult.rootDirectionSet ? 1 : 0) +
            " pre_root_angle_set=" + std::to_string(preSetResult.rootDodgeAngleSet ? 1 : 0) +
            " pre_root_dodge_dir_set=" + std::to_string(preSetResult.rootDodgeDirectionSet ? 1 : 0) +
            " pre_root_mco_dir_set=" + std::to_string(preSetResult.rootMcoDodgeDirectionSet ? 1 : 0) +
            " pre_root_speed_set=" + std::to_string(preSetResult.rootDodgeSpeedSet ? 1 : 0) +
            " pre_root_style_set=" + std::to_string(preSetResult.rootStyleSet ? 1 : 0) +
            " pre_root_styleNext_set=" + std::to_string(preSetResult.rootNextStyleSet ? 1 : 0) +
            " pre_graph_visits=" + std::to_string(preSetResult.visitedGraphs) +
            " pre_graph_dir_set=" + std::to_string(preSetResult.graphDirectionSet) +
            " pre_graph_angle_set=" + std::to_string(preSetResult.graphDodgeAngleSet) +
            " pre_graph_dodge_dir_set=" + std::to_string(preSetResult.graphDodgeDirectionSet) +
            " pre_graph_mco_dir_set=" + std::to_string(preSetResult.graphMcoDodgeDirectionSet) +
            " pre_graph_speed_set=" + std::to_string(preSetResult.graphDodgeSpeedSet) +
            " pre_graph_style_set=" + std::to_string(preSetResult.graphStyleSet) +
            " pre_graph_styleNext_set=" + std::to_string(preSetResult.graphNextStyleSet) +
            " post_root_dir_set=" + std::to_string(postSetResult.rootDirectionSet ? 1 : 0) +
            " post_root_angle_set=" + std::to_string(postSetResult.rootDodgeAngleSet ? 1 : 0) +
            " post_root_dodge_dir_set=" + std::to_string(postSetResult.rootDodgeDirectionSet ? 1 : 0) +
            " post_root_mco_dir_set=" + std::to_string(postSetResult.rootMcoDodgeDirectionSet ? 1 : 0) +
            " post_root_speed_set=" + std::to_string(postSetResult.rootDodgeSpeedSet ? 1 : 0) +
            " post_root_style_set=" + std::to_string(postSetResult.rootStyleSet ? 1 : 0) +
            " post_root_styleNext_set=" + std::to_string(postSetResult.rootNextStyleSet ? 1 : 0) +
            " post_graph_visits=" + std::to_string(postSetResult.visitedGraphs) +
            " post_graph_dir_set=" + std::to_string(postSetResult.graphDirectionSet) +
            " post_graph_angle_set=" + std::to_string(postSetResult.graphDodgeAngleSet) +
            " post_graph_dodge_dir_set=" + std::to_string(postSetResult.graphDodgeDirectionSet) +
            " post_graph_mco_dir_set=" + std::to_string(postSetResult.graphMcoDodgeDirectionSet) +
            " post_graph_speed_set=" + std::to_string(postSetResult.graphDodgeSpeedSet) +
            " post_graph_style_set=" + std::to_string(postSetResult.graphStyleSet) +
            " post_graph_styleNext_set=" + std::to_string(postSetResult.graphNextStyleSet) +
            " event=Dodge" +
            " event_ok=" + std::to_string(dispatched ? 1 : 0) +
            " dir_source=" + std::to_string(static_cast<std::int32_t>(g_lastDirectionSource)) +
            " duration=" + std::to_string(duration));
        BeginDashIFrames(actor, cfg, tier, triggerMode, "takeover_dmco_start");
        CompleteDmcoTakeoverDashAfter(
            duration,
            tier,
            triggerMode,
            direction,
            dmcoDirectionAngle,
            dmcoDirectionValue,
            dmcoStyleValue,
            style,
            runtimeState);
        return true;
    }

    static bool StartTakeoverDodge(RE::Actor* actor, const RE::NiPoint3& desiredDir, std::int32_t tier, ActivationMode triggerMode)
    {
        const Config cfg = GetConfigSnapshot();
        const auto provider = ResolveTakeoverProvider(cfg);
        if (provider == TakeoverProvider::kDmco) {
            return StartDmcoTakeoverDodge(actor, desiredDir, tier, triggerMode);
        }
        return StartTkTakeoverDodge(actor, desiredDir, tier, triggerMode);
    }

    static const std::string& GetFrameworkEventName(CardinalDirection direction, const Config& cfg)
    {
        switch (direction) {
        case CardinalDirection::kRight:
            return cfg.tpAnimRightEvent;
        case CardinalDirection::kBackward:
            return cfg.tpAnimBackwardEvent;
        case CardinalDirection::kLeft:
            return cfg.tpAnimLeftEvent;
        case CardinalDirection::kForward:
        default:
            return cfg.tpAnimForwardEvent;
        }
    }

    static void SignalThirdPersonFrameworkStart(RE::Actor* actor, const RE::NiPoint3& worldDirection, float dashSpeed, std::int32_t tier)
    {
        const Config cfg = GetConfigSnapshot();
        if (!actor || cfg.tpAnimFrameworkEnabled == 0) {
            return;
        }
        if (cfg.tpAnimThirdPersonOnly != 0 && IsFirstPersonForActor(actor)) {
            return;
        }

        const bool allowEightWay = true;
        CardinalDirection direction = ResolveCardinalDirection(actor, worldDirection, allowEightWay);
        const std::int32_t directionCode = static_cast<std::int32_t>(direction);
        const float directionAngle = ResolveSignedLocalDirectionAngle(actor, worldDirection);
        const float speedScale = Clamp((dashSpeed / 580.0f) * cfg.tpAnimSpeedScale, 0.10f, 4.00f);

        if (!cfg.tpAnimStateVar.empty()) {
            actor->SetGraphVariableBool(RE::BSFixedString(cfg.tpAnimStateVar.c_str()), true);
        }
        if (!cfg.tpAnimTierVar.empty()) {
            actor->SetGraphVariableInt(RE::BSFixedString(cfg.tpAnimTierVar.c_str()), tier);
        }
        if (!cfg.tpAnimDirectionVar.empty()) {
            actor->SetGraphVariableInt(RE::BSFixedString(cfg.tpAnimDirectionVar.c_str()), directionCode);
        }
        if (!cfg.tpAnimAngleVar.empty()) {
            actor->SetGraphVariableFloat(RE::BSFixedString(cfg.tpAnimAngleVar.c_str()), directionAngle);
        }
        if (!cfg.tpAnimSpeedVar.empty()) {
            actor->SetGraphVariableFloat(RE::BSFixedString(cfg.tpAnimSpeedVar.c_str()), speedScale);
        }
        const auto& eventName = GetFrameworkEventName(direction, cfg);
        if (!eventName.empty()) {
            actor->NotifyAnimationGraph(RE::BSFixedString(eventName.c_str()));
        }

        logger::info(
            "DodgeBlink: TP anim start dir={} angle={:.1f} tier={} speedScale={:.2f}",
            directionCode,
            directionAngle,
            tier,
            speedScale);
    }

    static void SignalThirdPersonFrameworkStop(RE::Actor* actor)
    {
        const Config cfg = GetConfigSnapshot();
        if (!actor || cfg.tpAnimFrameworkEnabled == 0) {
            return;
        }
        if (cfg.tpAnimThirdPersonOnly != 0 && IsFirstPersonForActor(actor)) {
            return;
        }

        if (!cfg.tpAnimStateVar.empty()) {
            actor->SetGraphVariableBool(RE::BSFixedString(cfg.tpAnimStateVar.c_str()), false);
        }
        if (!cfg.tpAnimTierVar.empty()) {
            actor->SetGraphVariableInt(RE::BSFixedString(cfg.tpAnimTierVar.c_str()), 0);
        }
        if (!cfg.tpAnimDirectionVar.empty()) {
            actor->SetGraphVariableInt(RE::BSFixedString(cfg.tpAnimDirectionVar.c_str()), 0);
        }
        if (!cfg.tpAnimAngleVar.empty()) {
            actor->SetGraphVariableFloat(RE::BSFixedString(cfg.tpAnimAngleVar.c_str()), 0.0f);
        }
        if (!cfg.tpAnimSpeedVar.empty()) {
            actor->SetGraphVariableFloat(RE::BSFixedString(cfg.tpAnimSpeedVar.c_str()), 0.0f);
        }
        if (!cfg.tpAnimStopEvent.empty()) {
            actor->NotifyAnimationGraph(RE::BSFixedString(cfg.tpAnimStopEvent.c_str()));
        }
    }

    static void SignalDashAnimationStart(RE::Actor* actor, std::int32_t tier)
    {
        const Config cfg = GetConfigSnapshot();
        if (!actor || cfg.animHooksEnabled == 0) {
            return;
        }

        if (!cfg.animStateVar.empty()) {
            actor->SetGraphVariableBool(RE::BSFixedString(cfg.animStateVar.c_str()), true);
        }
        if (!cfg.animTierVar.empty()) {
            actor->SetGraphVariableInt(RE::BSFixedString(cfg.animTierVar.c_str()), tier);
        }
        if (!cfg.animStartEvent.empty()) {
            actor->NotifyAnimationGraph(RE::BSFixedString(cfg.animStartEvent.c_str()));
        }
    }

    static void SignalDashAnimationStop(RE::Actor* actor)
    {
        const Config cfg = GetConfigSnapshot();
        if (!actor || cfg.animHooksEnabled == 0) {
            return;
        }

        if (!cfg.animStateVar.empty()) {
            actor->SetGraphVariableBool(RE::BSFixedString(cfg.animStateVar.c_str()), false);
        }
        if (!cfg.animTierVar.empty()) {
            actor->SetGraphVariableInt(RE::BSFixedString(cfg.animTierVar.c_str()), 0);
        }
        if (!cfg.animStopEvent.empty()) {
            actor->NotifyAnimationGraph(RE::BSFixedString(cfg.animStopEvent.c_str()));
        }
    }

    static bool StartControllerDashFixedYaw(RE::Actor* actor, const RE::NiPoint3& desiredDir, float targetDistance, float durationSeconds, std::int32_t tier, ActivationMode triggerMode)
    {
        if (!actor) {
            return false;
        }
        const Config cfg = GetConfigSnapshot();

        const auto dir = NormalizeXZ(desiredDir);
        if (std::abs(dir.x) <= 1e-5f && std::abs(dir.y) <= 1e-5f) {
            logger::warn("DodgeBlink: tier {} dash skipped (zero direction).", tier);
            return false;
        }

        if (!actor->GetCharController()) {
            logger::warn("DodgeBlink: tier {} dash failed (no character controller).", tier);
            return false;
        }

        const bool isFirstPerson = IsFirstPersonForActor(actor);
        const auto movementDir = dir;
        const bool preserveAirborneVerticalVelocity = true;
        const bool startedInMidair = actor->IsInMidair();
        const bool useThirdPersonProfile = (!isFirstPerson && cfg.tpTravelProfileEnabled != 0);
        const bool applyDashInvisibilityFx = ShouldApplyDashInvisibilityFx(actor, isFirstPerson, cfg);
        const float dashInvisibilityFadeOutSeconds = cfg.dashInvisibilityFadeOutSeconds;
        const float dashInvisibilityFadeInSeconds = cfg.dashInvisibilityFadeInSeconds;
        const float dashInvisibilityRefraction = cfg.dashInvisibilityRefraction;

        const float requestedDistance = Clamp(targetDistance, 32.0f, cfg.maxDistanceCap);
        const float requestedBaseDuration = Clamp(durationSeconds, 0.10f, 2.00f);
        const float requestedSpeedCap = Clamp(cfg.dashMaxSpeed, 120.0f, 2400.0f);

        float distance = requestedDistance;
        float baseDuration = requestedBaseDuration;
        float speedCap = requestedSpeedCap;
        float tpCatchupStrength = Clamp(cfg.tpCatchupStrength, 0.00f, 2.00f);
        float tpCatchupStepScaleMax = Clamp(cfg.tpCatchupStepScaleMax, 1.00f, 4.00f);
        float tpRecoverySpeedScale = Clamp(cfg.tpRecoverySpeedScale, 0.10f, 2.00f);
        float recoveryExtraSeconds = 0.0f;
        if (useThirdPersonProfile) {
            const float durationScale = Clamp(cfg.tpDurationScale, 0.50f, 3.00f);
            const float minDuration = Clamp(cfg.tpMinDuration, 0.10f, 2.00f);
            const float tierDurationFloor =
                tier <= 1 ? Clamp(cfg.tpTierDuration1, 0.10f, 2.00f) :
                tier == 2 ? Clamp(cfg.tpTierDuration2, 0.10f, 2.00f) :
                            Clamp(cfg.tpTierDuration3, 0.10f, 2.00f);
            baseDuration = std::max(requestedBaseDuration * durationScale, minDuration);
            baseDuration = std::max(baseDuration, tierDurationFloor);

            speedCap = Clamp(cfg.tpMaxSpeed, 120.0f, 2400.0f);
            recoveryExtraSeconds = Clamp(cfg.tpRecoveryExtraSeconds, 0.00f, 2.00f);
        }
        std::uint32_t collisionFilterInfo = 0;
        const bool hasCollisionFilterInfo = GetControllerCollisionFilterInfo(actor, collisionFilterInfo);
        bool hasGroundReference = false;
        float groundDrop = 0.0f;
        float forwardHitFraction = 1.0f;
        bool hasAirRayWorld = false;
        const RE::NiPoint3 origin = actor->GetPosition();
        const bool useVirtualGroundPlane = startedInMidair;
        RE::NiPoint3 measurementOrigin = origin;
        if (useVirtualGroundPlane && hasCollisionFilterInfo) {
            if (auto* world = GetActorHavokWorld(actor); world) {
                hasAirRayWorld = true;
                RE::NiPoint3 groundPoint{};
                if (TrySampleGroundPoint(world, origin, movementDir, collisionFilterInfo, groundPoint)) {
                    hasGroundReference = true;
                    groundDrop = std::max(0.0f, origin.z - groundPoint.z);
                    measurementOrigin.z = groundPoint.z;
                }

                const float referenceZ = measurementOrigin.z + 6.0f;
                const RE::NiPoint3 rayStart{
                    measurementOrigin.x + movementDir.x * 8.0f,
                    measurementOrigin.y + movementDir.y * 8.0f,
                    referenceZ
                };
                const RE::NiPoint3 rayEnd{
                    rayStart.x + movementDir.x * distance,
                    rayStart.y + movementDir.y * distance,
                    referenceZ
                };

                float hitFraction = 1.0f;
                if (CastWorldRay(world, rayStart, rayEnd, collisionFilterInfo, hitFraction)) {
                    constexpr float kCollisionMargin = 8.0f;
                    const float clampedDistance = std::max(0.0f, (distance * hitFraction) - kCollisionMargin);
                    if (clampedDistance < distance) {
                        distance = clampedDistance;
                    }
                    forwardHitFraction = hitFraction;
                }
            }
        }
        const float airborneHeightOffset = useVirtualGroundPlane ? (origin.z - measurementOrigin.z) : 0.0f;

        if (distance <= 1.0f) {
            logger::info(
                "DodgeBlink: tier {} dash aborted (distance clamped by immediate collision) requestedDist={:.1f} hasGroundRef={} groundDrop={:.1f} hitFrac={:.3f}.",
                tier,
                requestedDistance,
                hasGroundReference ? 1 : 0,
                groundDrop,
                forwardHitFraction);
            return false;
        }

        const float minDurationForSpeed = distance / std::max(speedCap, 1.0f);
        const float duration = Clamp(std::max(baseDuration, minDurationForSpeed), 0.10f, 3.00f);
        const float speed = distance / std::max(duration, 0.01f);
        const int steps = Clamp(static_cast<int>(std::ceil(duration / 0.012f)), 12, 360);
        const int recoverySteps = Clamp(static_cast<int>(std::ceil(recoveryExtraSeconds / 0.012f)), 0, 180);
        const int totalSteps = steps + recoverySteps;
        const float tickSec = duration / static_cast<float>(steps);
        const float originalYaw = actor->data.angle.z;
        auto actorHandle = actor->CreateRefHandle();
        if (!actorHandle) {
            return false;
        }

        if (startedInMidair) {
            (void)SetControllerVelocity(actor, RE::NiPoint3{ 0.0f, 0.0f, 0.0f });
        }

        const bool shouldSuppressTpShoutAnim =
            !isFirstPerson &&
            cfg.tpImmediateShoutTriggerEnabled != 0 &&
            cfg.tpSuppressVanillaShoutAnim != 0 &&
            triggerMode == ActivationMode::kShout &&
            IsNadsShoutEquipped(actor);
        if (shouldSuppressTpShoutAnim) {
            actor->InterruptCast(true);
            if (!cfg.tpSuppressVanillaShoutEvent.empty()) {
                actor->NotifyAnimationGraph(RE::BSFixedString(cfg.tpSuppressVanillaShoutEvent.c_str()));
            }
        }

        if (applyDashInvisibilityFx) {
            TriggerDashInvisibilityBurst(actor);
            actor->SetRefraction(true, dashInvisibilityRefraction);
            actor->SetAlpha(1.0f);
        }

        SignalDashAnimationStart(actor, tier);
        SignalThirdPersonFrameworkStart(actor, movementDir, speed, tier);
        BeginDashIFrames(actor, cfg, tier, triggerMode, "controller_start");

        logger::info(
            "DodgeBlink: {} tier {} controller dash start dir_source={} targetDist={:.1f} requestedDist={:.1f} duration={:.3f}s baseDuration={:.3f}s requestedBase={:.3f}s speedCap={:.1f} requestedSpeedCap={:.1f} steps={} recoverySteps={} speed={:.1f} yawLock=1 fp={} airCarry={} airStart={} airVirtualPlane={} airRayWorld={} airGroundRef={} airGroundDrop={:.1f} airHitFrac={:.3f} tpProfile={} tpSuppressShout={} animHooks={} tpAnim={}",
            ActivationModeName(triggerMode),
            tier,
            static_cast<std::int32_t>(g_lastDirectionSource),
            distance,
            requestedDistance,
            duration,
            baseDuration,
            requestedBaseDuration,
            speedCap,
            requestedSpeedCap,
            steps,
            recoverySteps,
            speed,
            isFirstPerson ? 1 : 0,
            preserveAirborneVerticalVelocity ? 1 : 0,
            startedInMidair ? 1 : 0,
            useVirtualGroundPlane ? 1 : 0,
            hasAirRayWorld ? 1 : 0,
            hasGroundReference ? 1 : 0,
            groundDrop,
            forwardHitFraction,
            useThirdPersonProfile ? 1 : 0,
            shouldSuppressTpShoutAnim ? 1 : 0,
            cfg.animHooksEnabled,
            cfg.tpAnimFrameworkEnabled);
        AppendNativeTrace(
            "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
            " tier " + std::to_string(tier) +
            " start_controller dir_source=" + std::to_string(static_cast<std::int32_t>(g_lastDirectionSource)) +
            " targetDist=" + std::to_string(distance) +
            " requestedDist=" + std::to_string(requestedDistance) +
            " duration=" + std::to_string(duration) +
            " baseDuration=" + std::to_string(baseDuration) +
            " requestedBaseDuration=" + std::to_string(requestedBaseDuration) +
            " speedCap=" + std::to_string(speedCap) +
            " requestedSpeedCap=" + std::to_string(requestedSpeedCap) +
            " steps=" + std::to_string(steps) +
            " recoverySteps=" + std::to_string(recoverySteps) +
            " fp=" + std::to_string(isFirstPerson ? 1 : 0) +
            " airCarry=" + std::to_string(preserveAirborneVerticalVelocity ? 1 : 0) +
            " airStart=" + std::to_string(startedInMidair ? 1 : 0) +
            " airVirtualPlane=" + std::to_string(useVirtualGroundPlane ? 1 : 0) +
            " airRayWorld=" + std::to_string(hasAirRayWorld ? 1 : 0) +
            " airGroundRef=" + std::to_string(hasGroundReference ? 1 : 0) +
            " airGroundDrop=" + std::to_string(groundDrop) +
            " airHitFrac=" + std::to_string(forwardHitFraction) +
            " tpProfile=" + std::to_string(useThirdPersonProfile ? 1 : 0) +
            " tpSuppressShout=" + std::to_string(shouldSuppressTpShoutAnim ? 1 : 0) +
            " yawLock=1");

        auto stepComplete = std::make_shared<std::atomic_int>(0);
        auto reachedTarget = std::make_shared<std::atomic_bool>(false);
        std::thread([actorHandle, movementDir, measurementOrigin, airborneHeightOffset, originalYaw, distance, duration, speed, speedCap, steps, totalSteps, tickSec, tier, triggerMode, preserveAirborneVerticalVelocity, useVirtualGroundPlane, hasCollisionFilterInfo, collisionFilterInfo, useThirdPersonProfile, tpCatchupStrength, tpCatchupStepScaleMax, tpRecoverySpeedScale, applyDashInvisibilityFx, dashInvisibilityFadeOutSeconds, dashInvisibilityFadeInSeconds, dashInvisibilityRefraction, stepComplete, reachedTarget]() mutable {
            for (int step = 0; step < totalSteps; ++step) {
                if (reachedTarget->load(std::memory_order_acquire)) {
                    break;
                }
                if (auto* task = SKSE::GetTaskInterface(); task) {
                    const int stepIndex = step + 1;
                    task->AddTask([actorHandle, movementDir, measurementOrigin, airborneHeightOffset, originalYaw, distance, duration, speed, speedCap, steps, tickSec, stepIndex, tier, triggerMode, preserveAirborneVerticalVelocity, useVirtualGroundPlane, hasCollisionFilterInfo, collisionFilterInfo, useThirdPersonProfile, tpCatchupStrength, tpCatchupStepScaleMax, tpRecoverySpeedScale, applyDashInvisibilityFx, dashInvisibilityFadeOutSeconds, dashInvisibilityFadeInSeconds, dashInvisibilityRefraction, stepComplete, reachedTarget]() mutable {
                        const auto markDone = [&]() {
                            stepComplete->store(stepIndex, std::memory_order_release);
                        };

                        auto actorPtr = actorHandle.get();
                        if (!actorPtr) {
                            markDone();
                            return;
                        }

                        auto* actorRaw = actorPtr.get();
                        MaintainDashIFrames(actorRaw, tier, triggerMode, "controller_step");
                        if (applyDashInvisibilityFx) {
                            const float dashProgress = static_cast<float>(std::min(stepIndex, steps)) / static_cast<float>(std::max(steps, 1));
                            const float alphaNow = ResolveDashInvisibilityAlpha(
                                dashProgress,
                                duration,
                                dashInvisibilityFadeOutSeconds,
                                dashInvisibilityFadeInSeconds);
                            actorRaw->SetRefraction(true, dashInvisibilityRefraction);
                            actorRaw->SetAlpha(alphaNow);
                        }
                        const RE::NiPoint3 current = actorRaw->GetPosition();
                        RE::NiPoint3 measuredCurrent = current;
                        if (useVirtualGroundPlane) {
                            measuredCurrent.z = measurementOrigin.z;
                        }
                        const float moved = std::max(0.0f, DotXZ(measuredCurrent - measurementOrigin, movementDir));
                        const float movedForControl = moved;
                        const float remaining = distance - movedForControl;
                        const bool inMidair = actorRaw->IsInMidair();
                        const bool steerAirborneXY = useVirtualGroundPlane;
                        const bool keepVerticalVelocity = preserveAirborneVerticalVelocity && inMidair && !steerAirborneXY;
                        const float verticalVelocity = keepVerticalVelocity ? GetControllerVelocity(actorRaw).z : 0.0f;
                        if (remaining <= 1.0f) {
                            if (steerAirborneXY) {
                                const RE::NiPoint3 finalTargetXY{
                                    measurementOrigin.x + movementDir.x * distance,
                                    measurementOrigin.y + movementDir.y * distance,
                                    0.0f
                                };
                                float targetZ = current.z;
                                if (hasCollisionFilterInfo) {
                                    if (auto* world = GetActorHavokWorld(actorRaw); world) {
                                        const RE::NiPoint3 probeAround{
                                            finalTargetXY.x,
                                            finalTargetXY.y,
                                            current.z - airborneHeightOffset
                                        };
                                        RE::NiPoint3 groundAtTarget{};
                                        if (TrySampleGroundPoint(world, probeAround, movementDir, collisionFilterInfo, groundAtTarget)) {
                                            targetZ = groundAtTarget.z + airborneHeightOffset;
                                        }
                                    }
                                }

                                RE::NiPoint3 targetPos = current;
                                targetPos.x = finalTargetXY.x;
                                targetPos.y = finalTargetXY.y;
                                targetPos.z = targetZ;
                                actorRaw->SetPosition(targetPos, true);
                            }
                            (void)SetControllerVelocity(actorRaw, RE::NiPoint3{ 0.0f, 0.0f, verticalVelocity });
                            actorRaw->SetRotationZ(originalYaw);
                            reachedTarget->store(true, std::memory_order_release);
                            markDone();
                            return;
                        }

                        float speedNow = std::min(speed, remaining / std::max(tickSec, 0.001f));
                        if (useThirdPersonProfile && !steerAirborneXY) {
                            if (stepIndex <= steps) {
                                // In third-person animation windows, movement can be partially suppressed by graph state.
                                // Closed-loop correction to reduce TP desync while preventing sudden catch-up snaps.
                                const float stepFrac = static_cast<float>(stepIndex) / static_cast<float>(std::max(steps, 1));
                                const float expectedMoved = distance * Clamp(stepFrac, 0.0f, 1.0f);
                                const float progressError = expectedMoved - moved;
                                const float nominalStepDist = distance / static_cast<float>(std::max(steps, 1));
                                const float maxStepDist = nominalStepDist * tpCatchupStepScaleMax;
                                float desiredStepDist = nominalStepDist + progressError * tpCatchupStrength;
                                desiredStepDist = Clamp(desiredStepDist, 0.0f, std::min(remaining, maxStepDist));
                                const float correctiveSpeed = desiredStepDist / std::max(tickSec, 0.001f);
                                speedNow = Clamp(correctiveSpeed, 0.0f, speedCap);
                            } else {
                                // Recovery window after planned TP dash duration.
                                const float recoverySpeedCap = speedCap * tpRecoverySpeedScale;
                                speedNow = Clamp(remaining / std::max(tickSec, 0.001f), 0.0f, recoverySpeedCap);
                            }
                        }
                        if (steerAirborneXY) {
                            // Share the same speed/distance controller as grounded dashes.
                            float stepDistance = Clamp(speedNow * tickSec, 0.0f, remaining);
                            float commandedSpeed = stepDistance / std::max(tickSec, 0.001f);
                            if (stepDistance > 0.0f && hasCollisionFilterInfo) {
                                if (auto* world = GetActorHavokWorld(actorRaw); world) {
                                    const RE::NiPoint3 from{
                                        measurementOrigin.x + movementDir.x * movedForControl,
                                        measurementOrigin.y + movementDir.y * movedForControl,
                                        measurementOrigin.z + 6.0f
                                    };
                                    const RE::NiPoint3 to{
                                        from.x + movementDir.x * stepDistance,
                                        from.y + movementDir.y * stepDistance,
                                        from.z
                                    };
                                    float hitFraction = 1.0f;
                                    if (CastWorldRay(world, from, to, collisionFilterInfo, hitFraction)) {
                                        stepDistance = std::max(0.0f, (stepDistance * hitFraction) - 4.0f);
                                        commandedSpeed = stepDistance / std::max(tickSec, 0.001f);
                                        if (stepDistance <= 0.5f) {
                                            reachedTarget->store(true, std::memory_order_release);
                                        }
                                    }

                                    const RE::NiPoint3 targetXY{
                                        current.x + movementDir.x * stepDistance,
                                        current.y + movementDir.y * stepDistance,
                                        0.0f
                                    };
                                    const RE::NiPoint3 probeAround{
                                        targetXY.x,
                                        targetXY.y,
                                        current.z - airborneHeightOffset
                                    };
                                    RE::NiPoint3 groundAtTarget{};
                                    if (TrySampleGroundPoint(world, probeAround, movementDir, collisionFilterInfo, groundAtTarget)) {
                                        RE::NiPoint3 targetPos = current;
                                        targetPos.z = groundAtTarget.z + airborneHeightOffset;
                                        actorRaw->SetPosition(targetPos, true);
                                    }
                                }
                            }

                            const RE::NiPoint3 velocity{ movementDir.x * commandedSpeed, movementDir.y * commandedSpeed, 0.0f };
                            (void)SetControllerVelocity(actorRaw, velocity);
                        } else {
                            const RE::NiPoint3 velocity{ movementDir.x * speedNow, movementDir.y * speedNow, verticalVelocity };
                            (void)SetControllerVelocity(actorRaw, velocity);
                        }
                        actorRaw->SetRotationZ(originalYaw);
                        markDone();
                    });

                    while (stepComplete->load(std::memory_order_acquire) < stepIndex) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                if (reachedTarget->load(std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::duration<float>(tickSec));
            }

            if (auto* task = SKSE::GetTaskInterface(); task) {
                task->AddTask([actorHandle, movementDir, measurementOrigin, originalYaw, distance, duration, tier, preserveAirborneVerticalVelocity, useVirtualGroundPlane, applyDashInvisibilityFx]() mutable {
                    auto actorPtr = actorHandle.get();
                    if (!actorPtr) {
                        ReleaseDashGate("controller_end_task_actor_missing");
                        return;
                    }

                    auto* actorRaw = actorPtr.get();
                    const bool keepVerticalVelocity = preserveAirborneVerticalVelocity && actorRaw->IsInMidair();
                    const float verticalVelocity = keepVerticalVelocity ? GetControllerVelocity(actorRaw).z : 0.0f;
                    (void)SetControllerVelocity(actorRaw, RE::NiPoint3{ 0.0f, 0.0f, verticalVelocity });
                    actorRaw->SetRotationZ(originalYaw);
                    SignalDashAnimationStop(actorRaw);
                    SignalThirdPersonFrameworkStop(actorRaw);
                    if (applyDashInvisibilityFx) {
                        actorRaw->SetAlpha(1.0f);
                        actorRaw->SetRefraction(false, 0.0f);
                        TriggerDashInvisibilityBurst(actorRaw);
                    }

                    const RE::NiPoint3 finalPos = actorRaw->GetPosition();
                    RE::NiPoint3 measuredFinal = finalPos;
                    if (useVirtualGroundPlane) {
                        measuredFinal.z = measurementOrigin.z;
                    }
                    const float moved = std::max(0.0f, DotXZ(measuredFinal - measurementOrigin, movementDir));
                    logger::info(
                        "DodgeBlink: tier {} controller dash end moved={:.2f}/{:.2f} duration={:.3f}s",
                        tier,
                        moved,
                        distance,
                        duration);
                    AppendNativeTrace(
                        "DodgeBlink tier " + std::to_string(tier) +
                        " end_controller moved=" + std::to_string(moved) +
                        " targetDist=" + std::to_string(distance));

                    ReleaseDashGate("controller_end_task");
                });
            } else {
                auto actorPtr = actorHandle.get();
                if (actorPtr) {
                    auto* actorRaw = actorPtr.get();
                    const bool keepVerticalVelocity = preserveAirborneVerticalVelocity && actorRaw->IsInMidair();
                    const float verticalVelocity = keepVerticalVelocity ? GetControllerVelocity(actorRaw).z : 0.0f;
                    (void)SetControllerVelocity(actorRaw, RE::NiPoint3{ 0.0f, 0.0f, verticalVelocity });
                    actorRaw->SetRotationZ(originalYaw);
                    SignalDashAnimationStop(actorRaw);
                    SignalThirdPersonFrameworkStop(actorRaw);
                    if (applyDashInvisibilityFx) {
                        actorRaw->SetAlpha(1.0f);
                        actorRaw->SetRefraction(false, 0.0f);
                        TriggerDashInvisibilityBurst(actorRaw);
                    }
                }
                ReleaseDashGate("controller_end_thread");
            }
        }).detach();

        return true;
    }

    static std::int32_t ResolveTierFromRange(float range)
    {
        const Config cfg = GetConfigSnapshot();
        range = Clamp(range, 32.0f, cfg.maxDistanceCap);
        const float split12 = (cfg.tier[1].range + cfg.tier[2].range) * 0.5f;
        const float split23 = (cfg.tier[2].range + cfg.tier[3].range) * 0.5f;
        if (range <= split12) {
            return 1;
        }
        if (range <= split23) {
            return 2;
        }
        return 3;
    }

    static bool LoadConfig()
    {
        Config cfg = GetConfigSnapshot();

        const auto iniPath = GetIniPath();
        std::ifstream in(iniPath);
        if (!in.is_open()) {
            logger::warn("DodgeBlink: could not open INI at {}. Using defaults.", iniPath.string());
            return false;
        }

        std::unordered_map<std::string, std::string> kv;
        bool inSection = false;
        std::string line;
        while (std::getline(in, line)) {
            auto commentPos = line.find(';');
            if (commentPos != std::string::npos) {
                line.erase(commentPos);
            }
            commentPos = line.find('#');
            if (commentPos != std::string::npos) {
                line.erase(commentPos);
            }

            line = Trim(line);
            if (line.empty()) {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                auto section = ToLower(Trim(line.substr(1, line.size() - 2)));
                inSection = section == "dodgebink" || section == "dodgeblink";
                continue;
            }

            if (!inSection) {
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }

            auto key = ToLower(Trim(line.substr(0, eq)));
            auto value = Trim(line.substr(eq + 1));
            if (!key.empty() && !value.empty()) {
                kv[key] = value;
            }
        }

        cfg.tier[1].range = ReadFloat(kv, "Dist1", ReadFloat(kv, "Range1", cfg.tier[1].range));
        cfg.tier[2].range = ReadFloat(kv, "Dist2", ReadFloat(kv, "Range2", cfg.tier[2].range));
        cfg.tier[3].range = ReadFloat(kv, "Dist3", ReadFloat(kv, "Range3", cfg.tier[3].range));

        cfg.tier[1].cooldownSeconds = ReadFloat(kv, "CD1", ReadFloat(kv, "Cooldown1", cfg.tier[1].cooldownSeconds));
        cfg.tier[2].cooldownSeconds = ReadFloat(kv, "CD2", ReadFloat(kv, "Cooldown2", cfg.tier[2].cooldownSeconds));
        cfg.tier[3].cooldownSeconds = ReadFloat(kv, "CD3", ReadFloat(kv, "Cooldown3", cfg.tier[3].cooldownSeconds));

        cfg.tier[1].dashDurationSeconds = ReadFloat(kv, "DashTime1", cfg.tier[1].dashDurationSeconds);
        cfg.tier[2].dashDurationSeconds = ReadFloat(kv, "DashTime2", cfg.tier[2].dashDurationSeconds);
        cfg.tier[3].dashDurationSeconds = ReadFloat(kv, "DashTime3", cfg.tier[3].dashDurationSeconds);

        cfg.maxDistanceCap = ReadFloat(kv, "MaxDistanceCap", cfg.maxDistanceCap);
        cfg.inputThreshold = ReadFloat(kv, "InputThreshold", cfg.inputThreshold);
        cfg.inputYSign = ReadFloat(kv, "InputYSign", cfg.inputYSign);
        cfg.inputLatchWindowMs = ReadInt(kv, "InputLatchWindowMs", cfg.inputLatchWindowMs);
        cfg.dashMaxSpeed = ReadFloat(kv, "DashMaxSpeed", cfg.dashMaxSpeed);
        cfg.animHooksEnabled = ReadInt(kv, "AnimHooksEnabled", cfg.animHooksEnabled);
        cfg.animStartEvent = ReadString(kv, "AnimStartEvent", cfg.animStartEvent);
        cfg.animStopEvent = ReadString(kv, "AnimStopEvent", cfg.animStopEvent);
        cfg.animStateVar = ReadString(kv, "AnimStateVar", cfg.animStateVar);
        cfg.animTierVar = ReadString(kv, "AnimTierVar", cfg.animTierVar);
        cfg.tpAnimFrameworkEnabled = ReadInt(kv, "TPAnimFrameworkEnabled", cfg.tpAnimFrameworkEnabled);
        cfg.tpAnimThirdPersonOnly = ReadInt(kv, "TPAnimThirdPersonOnly", cfg.tpAnimThirdPersonOnly);
        cfg.tpAnimForwardEvent = ReadString(kv, "TPAnimForwardEvent", cfg.tpAnimForwardEvent);
        cfg.tpAnimBackwardEvent = ReadString(kv, "TPAnimBackwardEvent", cfg.tpAnimBackwardEvent);
        cfg.tpAnimLeftEvent = ReadString(kv, "TPAnimLeftEvent", cfg.tpAnimLeftEvent);
        cfg.tpAnimRightEvent = ReadString(kv, "TPAnimRightEvent", cfg.tpAnimRightEvent);
        cfg.tpAnimStopEvent = ReadString(kv, "TPAnimStopEvent", cfg.tpAnimStopEvent);
        cfg.tpAnimStateVar = ReadString(kv, "TPAnimStateVar", cfg.tpAnimStateVar);
        cfg.tpAnimTierVar = ReadString(kv, "TPAnimTierVar", cfg.tpAnimTierVar);
        cfg.tpAnimDirectionVar = ReadString(kv, "TPAnimDirectionVar", cfg.tpAnimDirectionVar);
        cfg.tpAnimAngleVar = ReadString(kv, "TPAnimAngleVar", cfg.tpAnimAngleVar);
        cfg.tpAnimSpeedVar = ReadString(kv, "TPAnimSpeedVar", cfg.tpAnimSpeedVar);
        cfg.tpAnimSpeedScale = ReadFloat(kv, "TPAnimSpeedScale", cfg.tpAnimSpeedScale);
        cfg.tpTravelProfileEnabled = ReadInt(kv, "TPTravelProfileEnabled", cfg.tpTravelProfileEnabled);
        cfg.tpDurationScale = ReadFloat(kv, "TPDurationScale", cfg.tpDurationScale);
        cfg.tpMinDuration = ReadFloat(kv, "TPMinDuration", cfg.tpMinDuration);
        cfg.tpMaxSpeed = ReadFloat(kv, "TPMaxSpeed", cfg.tpMaxSpeed);
        cfg.tpTierDuration1 = ReadFloat(kv, "TPTierDuration1", cfg.tpTierDuration1);
        cfg.tpTierDuration2 = ReadFloat(kv, "TPTierDuration2", cfg.tpTierDuration2);
        cfg.tpTierDuration3 = ReadFloat(kv, "TPTierDuration3", cfg.tpTierDuration3);
        cfg.tpRecoveryExtraSeconds = ReadFloat(kv, "TPRecoveryExtraSeconds", cfg.tpRecoveryExtraSeconds);
        cfg.tpCatchupStrength = ReadFloat(kv, "TPCatchupStrength", cfg.tpCatchupStrength);
        cfg.tpCatchupStepScaleMax = ReadFloat(kv, "TPCatchupStepScaleMax", cfg.tpCatchupStepScaleMax);
        cfg.tpRecoverySpeedScale = ReadFloat(kv, "TPRecoverySpeedScale", cfg.tpRecoverySpeedScale);
        cfg.tpSuppressVanillaShoutAnim = ReadInt(kv, "TPSuppressVanillaShoutAnim", cfg.tpSuppressVanillaShoutAnim);
        cfg.tpSuppressVanillaShoutEvent = ReadString(kv, "TPSuppressVanillaShoutEvent", cfg.tpSuppressVanillaShoutEvent);
        cfg.tpImmediateShoutTriggerEnabled = ReadInt(kv, "TPImmediateShoutTriggerEnabled", cfg.tpImmediateShoutTriggerEnabled);
        cfg.dashInvisibilityFxEnabled = ReadInt(kv, "DashInvisibilityFxEnabled", cfg.dashInvisibilityFxEnabled);
        cfg.dashInvisibilityFxThirdPersonOnly = ReadInt(kv, "DashInvisibilityFxThirdPersonOnly", cfg.dashInvisibilityFxThirdPersonOnly);
        cfg.dashInvisibilityFadeOutSeconds = ReadFloat(kv, "DashInvisibilityFadeOutSeconds", cfg.dashInvisibilityFadeOutSeconds);
        cfg.dashInvisibilityFadeInSeconds = ReadFloat(kv, "DashInvisibilityFadeInSeconds", cfg.dashInvisibilityFadeInSeconds);
        cfg.dashInvisibilityRefraction = ReadFloat(kv, "DashInvisibilityRefraction", cfg.dashInvisibilityRefraction);
        cfg.dodgeIFramesEnabled = ReadInt(kv, "DodgeIFramesEnabled", cfg.dodgeIFramesEnabled);
        cfg.activationMode = ReadInt(kv, "ActivationMode", cfg.activationMode);
        cfg.allowMultipleActivationTypes = ReadInt(kv, "AllowMultipleActivationTypes", cfg.allowMultipleActivationTypes);
        cfg.takeoverEnabled = ReadInt(kv, "TakeoverEnabled", cfg.takeoverEnabled);
        cfg.takeoverProvider = ReadInt(kv, "TakeoverProvider", cfg.takeoverProvider);
        cfg.takeoverAllowSheathed = ReadInt(kv, "TakeoverAllowSheathed", cfg.takeoverAllowSheathed);
        cfg.takeoverStyle = ReadInt(kv, "TakeoverStyle", cfg.takeoverStyle);
        cfg.dmcoTakeoverStyle = ReadInt(kv, "DmcoTakeoverStyle", cfg.dmcoTakeoverStyle);
        cfg.hotkeyStaminaCost = ReadFloat(kv, "HotkeyStaminaCost", cfg.hotkeyStaminaCost);
        cfg.spellMagickaCost[1] = ReadFloat(kv, "SpellMagickaCost1", ReadFloat(kv, "SpellCost1", cfg.spellMagickaCost[1]));
        cfg.spellMagickaCost[2] = ReadFloat(kv, "SpellMagickaCost2", ReadFloat(kv, "SpellCost2", cfg.spellMagickaCost[2]));
        cfg.spellMagickaCost[3] = ReadFloat(kv, "SpellMagickaCost3", ReadFloat(kv, "SpellCost3", cfg.spellMagickaCost[3]));
        cfg.shoutCooldownSeconds[1] = ReadFloat(kv, "ShoutCooldown1", ReadFloat(kv, "ShoutCD1", cfg.shoutCooldownSeconds[1]));
        cfg.shoutCooldownSeconds[2] = ReadFloat(kv, "ShoutCooldown2", ReadFloat(kv, "ShoutCD2", cfg.shoutCooldownSeconds[2]));
        cfg.shoutCooldownSeconds[3] = ReadFloat(kv, "ShoutCooldown3", ReadFloat(kv, "ShoutCD3", cfg.shoutCooldownSeconds[3]));
        cfg.hotkeyTier = ReadInt(kv, "HotkeyTier", cfg.hotkeyTier);
        cfg.hotkeyCode = ReadInt(kv, "HotkeyCode", cfg.hotkeyCode);
        cfg.hotkeyTapMaxMs = ReadInt(kv, "HotkeyTapMaxMs", cfg.hotkeyTapMaxMs);

        ClampConfigValues(cfg);
        StoreConfigSnapshot(cfg);

        logger::info(
            "DodgeBlink: config loaded dist={:.1f}/{:.1f}/{:.1f} cd={:.2f}/{:.2f}/{:.2f} shoutCd={:.2f}/{:.2f}/{:.2f} dash={:.2f}/{:.2f}/{:.2f} iframes={} activationMode={} allowMultipleTypes={} takeover={} takeoverProvider={} takeoverAllowSheathed={} takeoverStyle={} dmcoStyle={} hotkeyCost={:.1f} spellCost={:.1f}/{:.1f}/{:.1f} hotkeyTier={} hotkeyCode={} hotkeyTapMaxMs={} tpProfile={} tpSuppressShout={} tpImmediateTrigger={} tpCatch={:.2f} tpStepCap={:.2f} tpRecoverSpeed={:.2f}",
            cfg.tier[1].range,
            cfg.tier[2].range,
            cfg.tier[3].range,
            cfg.tier[1].cooldownSeconds,
            cfg.tier[2].cooldownSeconds,
            cfg.tier[3].cooldownSeconds,
            cfg.shoutCooldownSeconds[1],
            cfg.shoutCooldownSeconds[2],
            cfg.shoutCooldownSeconds[3],
            cfg.tier[1].dashDurationSeconds,
            cfg.tier[2].dashDurationSeconds,
            cfg.tier[3].dashDurationSeconds,
            cfg.dodgeIFramesEnabled,
            cfg.activationMode,
            cfg.allowMultipleActivationTypes,
            cfg.takeoverEnabled,
            TakeoverProviderName(ResolveTakeoverProvider(cfg)),
            cfg.takeoverAllowSheathed,
            cfg.takeoverStyle,
            DmcoTakeoverStyleName(ResolveDmcoTakeoverStyle(cfg)),
            cfg.hotkeyStaminaCost,
            cfg.spellMagickaCost[1],
            cfg.spellMagickaCost[2],
            cfg.spellMagickaCost[3],
            cfg.hotkeyTier,
            cfg.hotkeyCode,
            cfg.hotkeyTapMaxMs,
            cfg.tpTravelProfileEnabled,
            cfg.tpSuppressVanillaShoutAnim,
            cfg.tpImmediateShoutTriggerEnabled,
            cfg.tpCatchupStrength,
            cfg.tpCatchupStepScaleMax,
            cfg.tpRecoverySpeedScale);
        return true;
    }

    static void DoBlinkTier(RE::StaticFunctionTag*, RE::Actor* actor, std::int32_t tier)
    {
        const Config cfg = GetConfigSnapshot();

        if (!actor) {
            return;
        }
        if (actor == RE::PlayerCharacter::GetSingleton()) {
            (void)EnsurePlayerActivationForms();
        }
        logger::info(
            "DodgeBlink: DoBlinkTier called - tpAnimFrameworkEnabled={} takeoverEnabled={} takeoverProvider={} takeoverStyle={} dmcoStyle={} allowSheathed={}",
            cfg.tpAnimFrameworkEnabled,
            cfg.takeoverEnabled,
            TakeoverProviderName(ResolveTakeoverProvider(cfg)),
            cfg.takeoverStyle,
            DmcoTakeoverStyleName(ResolveDmcoTakeoverStyle(cfg)),
            cfg.takeoverAllowSheathed);
        const std::int32_t requestedTier = Clamp<std::int32_t>(tier, 1, 3);
        tier = requestedTier;
        const ActivationMode triggerMode = ResolveTriggerActivationMode(actor, cfg);
        if (!IsActivationModeEnabled(cfg, triggerMode)) {
            logger::info(
                "DodgeBlink: {} tier {} blocked; mode disabled.",
                ActivationModeName(triggerMode),
                tier);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) + " blocked by mode_disabled");
            return;
        }
        const float cooldownSeconds = ResolveTierCooldownSeconds(cfg, tier, triggerMode);
        const auto resourceCost = ResolveModeResourceCost(cfg, tier, triggerMode);

        float remaining = 0.0f;
        if (IsOnCooldown(tier, triggerMode, cooldownSeconds, remaining)) {
            logger::info(
                "DodgeBlink: {} tier {} blocked by cooldown ({:.2f}s remaining).",
                ActivationModeName(triggerMode),
                tier,
                remaining);
            AppendNativeTrace(
                "DodgeBlink " + std::string(ActivationModeName(triggerMode)) +
                " tier " + std::to_string(tier) + " blocked by cooldown");
            return;
        }
        if (resourceCost.has_value() && !HasSufficientModeResource(actor, *resourceCost, tier, triggerMode)) {
            return;
        }
        if (!TryBeginDashGate(tier, triggerMode)) {
            return;
        }

        const auto dir = GetDesiredDashDirection(actor);
        const auto facing = NormalizeXZ(GetFacingDirectionXZ(actor));
        const float dirDotFacing = DotXZ(dir, facing);
        const float targetDistance = cfg.tier[tier].range;

        logger::info(
            "DodgeBlink: tier {} start dir_source={} dotFacing={:.3f}",
            tier,
            static_cast<std::int32_t>(g_lastDirectionSource),
            dirDotFacing);
        AppendNativeTrace(
            "DodgeBlink tier " + std::to_string(tier) +
            " dir_source=" + std::to_string(static_cast<std::int32_t>(g_lastDirectionSource)) +
            " dotFacing=" + std::to_string(dirDotFacing));

        bool success = false;
        if (IsTakeoverEnabled(cfg)) {
            success = StartTakeoverDodge(actor, dir, tier, triggerMode);
        } else {
            success = StartControllerDashFixedYaw(
                actor,
                dir,
                targetDistance,
                cfg.tier[tier].dashDurationSeconds,
                tier,
                triggerMode);
        }
        if (success) {
            MarkUsed(tier, triggerMode);
            if (resourceCost.has_value()) {
                ConsumeModeResource(actor, *resourceCost, tier, triggerMode);
            }
            if (triggerMode == ActivationMode::kShout && IsNadsShoutEquipped(actor)) {
                SyncPlayerShoutRecoveryCooldown(actor, cooldownSeconds, tier);
            }
            return;
        }

        ReleaseDashGate("no_dash_route_started");
        logger::info("DodgeBlink: tier {} failed (no dash route started).", tier);
        AppendNativeTrace("DodgeBlink tier " + std::to_string(tier) + " failed: no dash route started");
    }

    static void DoBlink(RE::StaticFunctionTag*, RE::Actor* actor, float range, float maxAngleDeg, float stepDeg, float clearance)
    {
        (void)maxAngleDeg;
        (void)stepDeg;
        (void)clearance;
        if (!actor) {
            return;
        }

        const std::int32_t tier = ResolveTierFromRange(range);
        DoBlinkTier(nullptr, actor, tier);
    }

    static void DoBlinkPlayerTier(RE::StaticFunctionTag*, std::int32_t tier)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        DoBlinkTier(nullptr, player, tier);
    }

    static void DoBlinkTierForMode(RE::StaticFunctionTag*, RE::Actor* actor, std::int32_t tier, std::int32_t activationMode)
    {
        const auto overrideMode = ParseActivationModeOverride(activationMode);
        if (!overrideMode.has_value()) {
            logger::warn(
                "DodgeBlink: DoBlinkTierForMode got invalid activationMode={} (expected 0..2); falling back to configured mode.",
                activationMode);
            AppendNativeTrace(
                "DodgeBlink invalid_activation_mode_override=" + std::to_string(activationMode) +
                " fallback=config");
            DoBlinkTier(nullptr, actor, tier);
            return;
        }

        ScopedActivationModeOverride modeOverride(*overrideMode);
        DoBlinkTier(nullptr, actor, tier);
    }

    static bool IsDashActive(RE::StaticFunctionTag*)
    {
        return g_dashGate.load(std::memory_order_acquire);
    }

    static bool SetFloatConfig(RE::StaticFunctionTag*, std::string key, float value)
    {
        const auto normalized = NormalizeConfigKey(std::move(key));
        if (normalized.empty()) {
            return false;
        }

        Config cfg = GetConfigSnapshot();
        if (!SetFloatConfigByKey(cfg, normalized, value)) {
            return false;
        }
        ClampConfigValues(cfg);
        StoreConfigSnapshot(cfg);
        logger::info("DodgeBlink: SetFloatConfig {}={:.3f}", normalized, value);
        return true;
    }

    static bool SetIntConfig(RE::StaticFunctionTag*, std::string key, std::int32_t value)
    {
        const auto normalized = NormalizeConfigKey(std::move(key));
        if (normalized.empty()) {
            return false;
        }

        Config cfg = GetConfigSnapshot();
        if (!SetIntConfigByKey(cfg, normalized, value)) {
            return false;
        }
        ClampConfigValues(cfg);
        StoreConfigSnapshot(cfg);
        const Config applied = GetConfigSnapshot();
        logger::info(
            "DodgeBlink: SetIntConfig {}={} (mode={} allowMultipleTypes={} takeover={} takeoverProvider={} takeoverAllowSheathed={} takeoverStyle={} dmcoStyle={} hotkeyCode={} hotkeyTier={} hotkeyTapMaxMs={}).",
            normalized,
            value,
            ActivationModeName(ResolveActivationMode(applied)),
            applied.allowMultipleActivationTypes,
            applied.takeoverEnabled,
            TakeoverProviderName(ResolveTakeoverProvider(applied)),
            applied.takeoverAllowSheathed,
            applied.takeoverStyle,
            DmcoTakeoverStyleName(ResolveDmcoTakeoverStyle(applied)),
            applied.hotkeyCode,
            applied.hotkeyTier,
            applied.hotkeyTapMaxMs);
        if (normalized == "activationmode" ||
            normalized == "allowmultipleactivationtypes" ||
            normalized == "allowmultipletypes" ||
            normalized == "takeoverenabled" ||
            normalized == "takeoverprovider" ||
            normalized == "takeoverallowsheathed") {
            g_activationFormsReady.store(false, std::memory_order_release);
            g_activationFormsPendingLogged.store(false, std::memory_order_release);
            QueueEnsurePlayerActivationForms();
        }
        return true;
    }

    static float GetFloatConfig(RE::StaticFunctionTag*, std::string key)
    {
        const auto normalized = NormalizeConfigKey(std::move(key));
        if (normalized.empty()) {
            return 0.0f;
        }
        const Config cfg = GetConfigSnapshot();
        if (const auto value = GetFloatConfigByKey(cfg, normalized); value.has_value()) {
            return *value;
        }
        return 0.0f;
    }

    static std::int32_t GetIntConfig(RE::StaticFunctionTag*, std::string key)
    {
        const auto normalized = NormalizeConfigKey(std::move(key));
        if (normalized.empty()) {
            return 0;
        }
        const Config cfg = GetConfigSnapshot();
        if (const auto value = GetIntConfigByKey(cfg, normalized); value.has_value()) {
            return *value;
        }
        return 0;
    }

    static bool SaveConfig(RE::StaticFunctionTag*)
    {
        return SaveConfigToIni();
    }

    static bool ReloadConfig(RE::StaticFunctionTag*)
    {
        return LoadConfig();
    }

    static bool HasDirectionalThirdPersonAnimationsPapyrus(RE::StaticFunctionTag*)
    {
        return HasDirectionalThirdPersonAnimations();
    }

    static void TestInputDirection(RE::StaticFunctionTag*)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* controls = RE::PlayerControls::GetSingleton();
        const Config cfg = GetConfigSnapshot();
        if (!player || !controls) {
            logger::warn("DodgeBlink: TestInputDirection unavailable (player/controls missing).");
            return;
        }

        const auto move = controls->data.moveInputVec;
        const auto actorForward = NormalizeXZ(GetFacingDirectionXZ(player));
        auto basisForward = actorForward;
        const auto basisRight = RE::NiPoint3{ basisForward.y, -basisForward.x, 0.0f };
        const auto worldDir = NormalizeXZ(basisForward * (move.y * InputYScale(cfg.inputYSign)) + basisRight * move.x);

        logger::info(
            "DodgeBlink: TestInputDirection move=({:.3f},{:.3f}) worldDir=({:.3f},{:.3f}) actorForward=({:.3f},{:.3f})",
            move.x,
            move.y,
            worldDir.x,
            worldDir.y,
            actorForward.x,
            actorForward.y);
    }

    static std::int32_t GetVersion(RE::StaticFunctionTag*)
    {
        return 802;  // v1.8.2
    }

    static bool BindPapyrusFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        vm->RegisterFunction("DoBlink", "DodgeBlinkNative", DoBlink);
        vm->RegisterFunction("DoBlinkTier", "DodgeBlinkNative", DoBlinkTier);
        vm->RegisterFunction("DoBlinkTierForMode", "DodgeBlinkNative", DoBlinkTierForMode);
        vm->RegisterFunction("DoBlinkPlayerTier", "DodgeBlinkNative", DoBlinkPlayerTier);
        vm->RegisterFunction("IsDashActive", "DodgeBlinkNative", IsDashActive);
        vm->RegisterFunction("SetFloatConfig", "DodgeBlinkNative", SetFloatConfig);
        vm->RegisterFunction("SetIntConfig", "DodgeBlinkNative", SetIntConfig);
        vm->RegisterFunction("GetFloatConfig", "DodgeBlinkNative", GetFloatConfig);
        vm->RegisterFunction("GetIntConfig", "DodgeBlinkNative", GetIntConfig);
        vm->RegisterFunction("SaveConfig", "DodgeBlinkNative", SaveConfig);
        vm->RegisterFunction("ReloadConfig", "DodgeBlinkNative", ReloadConfig);
        vm->RegisterFunction("HasDirectionalThirdPersonAnimations", "DodgeBlinkNative", HasDirectionalThirdPersonAnimationsPapyrus);
        vm->RegisterFunction("TestInputDirection", "DodgeBlinkNative", TestInputDirection);
        vm->RegisterFunction("GetNadsShoutWord", "DodgeBlinkNative", GetNadsShoutWord);
        vm->RegisterFunction("GetVersion", "DodgeBlinkNative", GetVersion);
        return true;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    DodgeBlink::InitializeLogging();
    DodgeBlink::AppendNativeTrace("DodgeBlink plugin load");

    logger::info("DodgeBlink: plugin load start");
    (void)DodgeBlink::LoadConfig();
    if (auto* messaging = SKSE::GetMessagingInterface(); messaging) {
        messaging->RegisterListener(DodgeBlink::OnSkseMessage);
    } else {
        logger::warn("DodgeBlink: messaging interface unavailable; input hooks disabled.");
    }
    SKSE::GetPapyrusInterface()->Register(DodgeBlink::BindPapyrusFunctions);
    logger::info("DodgeBlink: plugin load complete; papyrus bindings registered");
    return true;
}
