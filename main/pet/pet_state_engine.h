#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <mutex>

#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>

// Lightweight persistent Mochi state. This class deliberately owns no task,
// timer, LVGL object, audio object, or network resource. Callers update/read it
// only when convenient; elapsed time is applied lazily.
class PetStateEngine {
public:
    struct Snapshot {
        uint8_t fullness = 84;    // 0 = starving, 99 = full
        uint8_t energy = 80;      // 0 = exhausted, 99 = rested
        uint8_t mood = 70;        // 0 = sad, 99 = very happy
        uint8_t friendship = 10;  // 0 = new, 99 = closest
        int32_t coins = 50;       // 0..999 in-game currency
        bool sleeping = false;
        bool manual_sleep = false;
        int32_t last_interaction_epoch = 0;
    };

    enum class LifecycleTransition : uint8_t {
        None = 0,
        AutoSleep,
        AutoWake,
    };

    static PetStateEngine& GetInstance() {
        static PetStateEngine instance;
        return instance;
    }

    PetStateEngine(const PetStateEngine&) = delete;
    PetStateEngine& operator=(const PetStateEngine&) = delete;

    void Initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return;
        }

        LoadLocked();
        last_monotonic_us_ = esp_timer_get_time();
        last_save_monotonic_us_ = last_monotonic_us_;
        initialized_ = true;

        // On this board PetStateEngine starts before the OTA/bootstrap path may
        // establish wall-clock time. Reconciliation therefore stays pending
        // until CurrentValidEpoch() becomes trustworthy later in normal runtime.
        TryReconcileOfflineSleepLocked();

        // Schema v1 stored inverse Hunger (0 full -> 100 hungry). 15D changes
        // that user-facing concept to intuitive Fullness (0 hungry -> 99 full).
        // Persist the one-time migration immediately so later boots are simple.
        if (migration_pending_) {
            dirty_ = true;
            SaveLocked(last_monotonic_us_);
            migration_pending_ = false;
        }

        ESP_LOGI(kTag,
                 "Pet core ready Full=%u E=%u M=%u F=%u coins=%ld sleep=%d manual=%d",
                 state_.fullness, state_.energy, state_.mood, state_.friendship,
                 static_cast<long>(state_.coins), state_.sleeping ? 1 : 0,
                 state_.manual_sleep ? 1 : 0);
    }

    Snapshot GetSnapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        return state_;
    }

    void Tick() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
    }

    bool Feed(int32_t price, uint8_t fullness_gain, uint8_t mood_bonus = 3) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (price < 0 || state_.coins < price) {
            return false;
        }
        state_.coins -= price;
        state_.fullness = AddClamp(state_.fullness, fullness_gain);
        state_.mood = AddClamp(state_.mood, mood_bonus);
        state_.friendship = AddClamp(state_.friendship, 1);
        state_.sleeping = false;
        state_.manual_sleep = false;
        TouchInteractionLocked();
        dirty_ = true;

        // Feeding changes both coins and core pet state, so persist it like
        // Play rather than allowing a reboot to refund the transaction.
        SaveLocked(esp_timer_get_time());
        return true;
    }

    // Basic Play action. There is intentionally no minigame here yet: Play
    // simply makes Mochi happier and spends a small amount of Energy.
    bool Play(uint8_t energy_cost = 10, uint8_t mood_bonus = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (state_.sleeping || energy_cost == 0 || state_.energy < energy_cost) {
            return false;
        }

        state_.energy = SubClamp(state_.energy, energy_cost);
        state_.mood = AddClamp(state_.mood, mood_bonus);
        state_.fullness = SubClamp(state_.fullness, 2);
        state_.friendship = AddClamp(state_.friendship, 1);
        TouchInteractionLocked();
        dirty_ = true;

        // Play is Energy-limited, so immediate persistence remains low-wear and
        // prevents power-cycling from restoring Energy that was already spent.
        SaveLocked(esp_timer_get_time());
        return true;
    }

    bool CanPlay(uint8_t energy_cost = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        return !state_.sleeping && energy_cost > 0 && state_.energy >= energy_cost;
    }

    // Future minigames call this only after their own gameplay succeeds.
    void CompletePlay(int32_t coin_reward, uint8_t mood_bonus = 10,
                      uint8_t energy_cost = 15, uint8_t fullness_cost = 3) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (state_.sleeping || state_.energy < energy_cost) {
            return;
        }
        if (coin_reward > 0) {
            state_.coins = std::min<int32_t>(kMaxCoins, state_.coins + coin_reward);
        }
        state_.mood = AddClamp(state_.mood, mood_bonus);
        state_.energy = SubClamp(state_.energy, energy_cost);
        state_.fullness = SubClamp(state_.fullness, fullness_cost);
        state_.friendship = AddClamp(state_.friendship, 2);
        TouchInteractionLocked();
        dirty_ = true;
        SaveLocked(esp_timer_get_time());
    }

    void Pet() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        state_.mood = AddClamp(state_.mood, 7);
        state_.friendship = AddClamp(state_.friendship, 1);
        TouchInteractionLocked();
        dirty_ = true;
        CheckpointIfDueLocked();
    }

    // Manual Sleep is distinct from automatic exhaustion sleep. A manually
    // sleeping pet will stay asleep until an explicit Wake() arrives later from
    // the UI/App. This avoids the auto-wake threshold fighting a Sleep button.
    void Sleep() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (!state_.sleeping || !state_.manual_sleep) {
            state_.sleeping = true;
            state_.manual_sleep = true;
            dirty_ = true;
            SaveLocked(esp_timer_get_time());
        }
    }

    void Wake() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (state_.sleeping || state_.manual_sleep) {
            state_.sleeping = false;
            state_.manual_sleep = false;
            TouchInteractionLocked();
            dirty_ = true;
            SaveLocked(esp_timer_get_time());
        }
    }

    bool ShouldAutoSleep() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        return !state_.sleeping && state_.energy <= kAutoSleepEnergy;
    }

    // Called from the Application main loop only while Idle. This is the single
    // place that advances the persistent sleep lifecycle and periodic checkpoint
    // work, keeping all NVS writes away from LVGL and audio hot paths.
    LifecycleTransition ServiceIdleLifecycle() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();

        if (!state_.sleeping && state_.energy <= kAutoSleepEnergy) {
            state_.sleeping = true;
            state_.manual_sleep = false;
            dirty_ = true;
            SaveLocked(esp_timer_get_time());
            ESP_LOGI(kTag, "Auto sleep at Energy=%u", state_.energy);
            return LifecycleTransition::AutoSleep;
        }

        if (state_.sleeping && !state_.manual_sleep && state_.energy >= kAutoWakeEnergy) {
            state_.sleeping = false;
            state_.manual_sleep = false;
            TouchInteractionLocked();
            dirty_ = true;
            SaveLocked(esp_timer_get_time());
            ESP_LOGI(kTag, "Auto wake at Energy=%u", state_.energy);
            return LifecycleTransition::AutoWake;
        }

        CheckpointIfDueLocked();
        return LifecycleTransition::None;
    }

    bool CanAfford(int32_t price) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        return price >= 0 && state_.coins >= price;
    }

    bool MaybeSave() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (!dirty_) {
            return true;
        }
        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_save_monotonic_us_ < kNormalSaveIntervalUs) {
            return true;
        }
        return SaveLocked(now_us);
    }

    // For controlled shutdown/reboot only. Never call this from the LVGL 200ms
    // animation tick or from the audio hot path.
    bool SaveNow() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (!dirty_) {
            return true;
        }
        return SaveLocked(esp_timer_get_time());
    }

private:
    static constexpr const char* kTag = "MochiPet";
    static constexpr const char* kNamespace = "mochi_pet";
    static constexpr uint8_t kSchemaVersion = 2;
    static constexpr uint8_t kLegacySchemaVersion = 1;
    static constexpr int32_t kValidEpochThreshold = 1704067200; // 2024-01-01
    static constexpr int32_t kMaxOfflineSimulationSeconds = 12 * 60 * 60;
    static constexpr int64_t kNormalSaveIntervalUs = 10LL * 60 * 1000 * 1000;
    static constexpr uint8_t kMaxStat = 99;
    static constexpr int32_t kMaxCoins = 999;
    static constexpr uint8_t kAutoSleepEnergy = 20;
    static constexpr uint8_t kAutoWakeEnergy = 90;

    // Fullness gently falls over time. Energy never changes merely because
    // Mochi is awake: activities spend it, and only logical Sleep restores it.
    static constexpr uint32_t kAwakeFullnessStepSeconds = 12 * 60;
    static constexpr uint32_t kSleepFullnessStepSeconds = 20 * 60;
    static constexpr uint32_t kSleepEnergyStepSeconds = 3 * 60;
    static constexpr uint32_t kMoodStepSeconds = 15 * 60;

    PetStateEngine() = default;

    Snapshot state_{};
    std::mutex mutex_;
    bool initialized_ = false;
    bool dirty_ = false;
    bool migration_pending_ = false;
    bool offline_sleep_reconciled_ = false;
    int32_t last_update_epoch_ = 0;
    int64_t last_monotonic_us_ = 0;
    int64_t last_save_monotonic_us_ = 0;
    uint32_t fullness_remainder_s_ = 0;
    uint32_t energy_remainder_s_ = 0;
    uint32_t mood_remainder_s_ = 0;

    static uint8_t AddClamp(uint8_t value, uint32_t amount) {
        return static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, value + amount));
    }

    static uint8_t SubClamp(uint8_t value, uint32_t amount) {
        return static_cast<uint8_t>(amount >= value ? 0 : value - amount);
    }

    static int32_t CurrentValidEpoch() {
        const time_t now = time(nullptr);
        if (now < kValidEpochThreshold || now > INT32_MAX) {
            return 0;
        }
        return static_cast<int32_t>(now);
    }

    void EnsureInitializedLocked() {
        if (initialized_) {
            return;
        }
        LoadLocked();
        last_monotonic_us_ = esp_timer_get_time();
        last_save_monotonic_us_ = last_monotonic_us_;
        initialized_ = true;
        TryReconcileOfflineSleepLocked();
        if (migration_pending_) {
            dirty_ = true;
            SaveLocked(last_monotonic_us_);
            migration_pending_ = false;
        }
    }

    static uint32_t PackStats(const Snapshot& s) {
        uint32_t packed = 0;
        packed |= static_cast<uint32_t>(std::min<uint8_t>(kMaxStat, s.fullness));
        packed |= static_cast<uint32_t>(std::min<uint8_t>(kMaxStat, s.energy)) << 7;
        packed |= static_cast<uint32_t>(std::min<uint8_t>(kMaxStat, s.mood)) << 14;
        packed |= static_cast<uint32_t>(std::min<uint8_t>(kMaxStat, s.friendship)) << 21;
        if (s.sleeping) {
            packed |= (1u << 28);
        }
        if (s.manual_sleep) {
            packed |= (1u << 29);
        }
        return packed;
    }

    static void UnpackStatsV2(uint32_t packed, Snapshot& s) {
        s.fullness = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, packed & 0x7F));
        s.energy = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, (packed >> 7) & 0x7F));
        s.mood = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, (packed >> 14) & 0x7F));
        s.friendship = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, (packed >> 21) & 0x7F));
        s.sleeping = ((packed >> 28) & 0x01) != 0;
        s.manual_sleep = s.sleeping && (((packed >> 29) & 0x01) != 0);
    }

    static void UnpackStatsV1(uint32_t packed, Snapshot& s) {
        const uint8_t old_hunger = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, packed & 0x7F));
        s.fullness = static_cast<uint8_t>(kMaxStat - old_hunger);
        s.energy = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, (packed >> 7) & 0x7F));
        s.mood = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, (packed >> 14) & 0x7F));
        s.friendship = static_cast<uint8_t>(std::min<uint32_t>(kMaxStat, (packed >> 21) & 0x7F));
        s.sleeping = ((packed >> 28) & 0x01) != 0;
        s.manual_sleep = s.sleeping && (((packed >> 29) & 0x01) != 0);
    }

    void LoadLocked() {
        nvs_handle_t handle = 0;
        const esp_err_t open_err = nvs_open(kNamespace, NVS_READONLY, &handle);
        if (open_err != ESP_OK) {
            return; // First boot: defaults are intentional.
        }

        uint8_t version = 0;
        uint32_t packed = 0;
        int32_t coins = state_.coins;
        int32_t last_interaction = 0;
        int32_t last_update = 0;

        const bool header_valid =
            nvs_get_u8(handle, "ver", &version) == ESP_OK &&
            nvs_get_u32(handle, "stats", &packed) == ESP_OK;

        if (header_valid && (version == kSchemaVersion || version == kLegacySchemaVersion)) {
            if (version == kLegacySchemaVersion) {
                UnpackStatsV1(packed, state_);
                migration_pending_ = true;
                ESP_LOGI(kTag, "Migrating pet NVS v1 Hunger -> v2 Fullness");
            } else {
                UnpackStatsV2(packed, state_);
            }

            if (nvs_get_i32(handle, "coins", &coins) == ESP_OK) {
                const int32_t clamped = std::clamp<int32_t>(coins, 0, kMaxCoins);
                state_.coins = clamped;
                if (clamped != coins) {
                    migration_pending_ = true;
                }
            }
            if (nvs_get_i32(handle, "last_int", &last_interaction) == ESP_OK) {
                state_.last_interaction_epoch = last_interaction;
            }
            if (nvs_get_i32(handle, "last_upd", &last_update) == ESP_OK) {
                last_update_epoch_ = last_update;
            }
        }
        nvs_close(handle);
    }

    void TryReconcileOfflineSleepLocked() {
        if (offline_sleep_reconciled_) {
            return;
        }
        if (last_update_epoch_ <= 0) {
            offline_sleep_reconciled_ = true;
            return;
        }

        const int32_t now_epoch = CurrentValidEpoch();
        if (now_epoch <= 0) {
            return; // Clock not ready yet; retry lazily later.
        }

        if (state_.sleeping && now_epoch > last_update_epoch_) {
            int32_t elapsed = now_epoch - last_update_epoch_;
            elapsed = std::min<int32_t>(elapsed, kMaxOfflineSimulationSeconds);

            // Only sleep Energy is reconciled across reboot. This deliberately
            // avoids punishing Fullness/Mood when upstream time semantics differ.
            energy_remainder_s_ += static_cast<uint32_t>(elapsed);
            if (energy_remainder_s_ >= kSleepEnergyStepSeconds) {
                const uint32_t steps = energy_remainder_s_ / kSleepEnergyStepSeconds;
                energy_remainder_s_ %= kSleepEnergyStepSeconds;
                const uint8_t before = state_.energy;
                state_.energy = AddClamp(state_.energy, steps);
                dirty_ = dirty_ || state_.energy != before;
                ESP_LOGI(kTag, "Offline sleep recovery: +%u Energy", state_.energy - before);
            }
        }

        offline_sleep_reconciled_ = true;
    }

    void UpdateRuntimeLocked() {
        TryReconcileOfflineSleepLocked();

        const int64_t now_us = esp_timer_get_time();
        if (last_monotonic_us_ == 0) {
            last_monotonic_us_ = now_us;
            return;
        }
        const int64_t delta_us = now_us - last_monotonic_us_;
        if (delta_us < 1000000) {
            return;
        }
        const uint32_t elapsed_s = static_cast<uint32_t>(delta_us / 1000000);
        last_monotonic_us_ += static_cast<int64_t>(elapsed_s) * 1000000;
        ApplyElapsedLocked(elapsed_s);
    }

    void ApplyElapsedLocked(uint32_t elapsed_s) {
        if (elapsed_s == 0) {
            return;
        }

        const uint32_t fullness_step = state_.sleeping ? kSleepFullnessStepSeconds
                                                        : kAwakeFullnessStepSeconds;
        fullness_remainder_s_ += elapsed_s;
        if (fullness_remainder_s_ >= fullness_step) {
            const uint32_t steps = fullness_remainder_s_ / fullness_step;
            fullness_remainder_s_ %= fullness_step;
            const uint8_t before = state_.fullness;
            state_.fullness = SubClamp(state_.fullness, steps);
            dirty_ = dirty_ || state_.fullness != before;
        }

        // Energy recovery is strictly sleep-only. Awake elapsed time neither
        // restores nor drains Energy; Play/minigames are the consumption path.
        if (state_.sleeping) {
            energy_remainder_s_ += elapsed_s;
            if (energy_remainder_s_ >= kSleepEnergyStepSeconds) {
                const uint32_t steps = energy_remainder_s_ / kSleepEnergyStepSeconds;
                energy_remainder_s_ %= kSleepEnergyStepSeconds;
                const uint8_t before = state_.energy;
                state_.energy = AddClamp(state_.energy, steps);
                dirty_ = dirty_ || state_.energy != before;
            }
        }

        mood_remainder_s_ += elapsed_s;
        if (mood_remainder_s_ >= kMoodStepSeconds) {
            const uint32_t steps = mood_remainder_s_ / kMoodStepSeconds;
            mood_remainder_s_ %= kMoodStepSeconds;
            const uint8_t before = state_.mood;
            if (state_.fullness <= 24 || state_.energy <= 20) {
                state_.mood = SubClamp(state_.mood, steps);
            } else if (state_.mood < 70) {
                state_.mood = AddClamp(state_.mood, std::max<uint32_t>(1, steps / 2));
            }
            dirty_ = dirty_ || state_.mood != before;
        }
    }

    void TouchInteractionLocked() {
        const int32_t now_epoch = CurrentValidEpoch();
        if (now_epoch > 0) {
            state_.last_interaction_epoch = now_epoch;
        }
    }

    void CheckpointIfDueLocked() {
        if (!dirty_) {
            return;
        }
        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_save_monotonic_us_ >= kNormalSaveIntervalUs) {
            SaveLocked(now_us);
        }
    }

    bool SaveLocked(int64_t now_us) {
        nvs_handle_t handle = 0;
        esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "NVS open failed: %s", esp_err_to_name(err));
            return false;
        }

        const int32_t now_epoch = CurrentValidEpoch();
        if (now_epoch > 0) {
            last_update_epoch_ = now_epoch;
        }

        if ((err = nvs_set_u8(handle, "ver", kSchemaVersion)) == ESP_OK &&
            (err = nvs_set_u32(handle, "stats", PackStats(state_))) == ESP_OK &&
            (err = nvs_set_i32(handle, "coins", state_.coins)) == ESP_OK &&
            (err = nvs_set_i32(handle, "last_int", state_.last_interaction_epoch)) == ESP_OK &&
            (err = nvs_set_i32(handle, "last_upd", last_update_epoch_)) == ESP_OK) {
            err = nvs_commit(handle);
        }

        nvs_close(handle);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "NVS save failed: %s", esp_err_to_name(err));
            return false;
        }

        dirty_ = false;
        last_save_monotonic_us_ = now_us;
        ESP_LOGI(kTag, "Pet state checkpoint saved");
        return true;
    }
};
