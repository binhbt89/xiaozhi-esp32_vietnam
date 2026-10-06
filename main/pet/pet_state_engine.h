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
        uint8_t hunger = 15;      // 0 = full, 100 = very hungry
        uint8_t energy = 80;      // 0 = exhausted, 100 = rested
        uint8_t mood = 70;        // internal behavior bias
        uint8_t friendship = 10;  // long-term relationship, does not decay
        int32_t coins = 50;       // future minigame/food economy
        bool sleeping = false;
        int32_t last_interaction_epoch = 0;
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

        // Reconcile a bounded amount of elapsed wall time. We intentionally
        // stay conservative because the existing firmware may source time from
        // either OTA server time or SNTP. A bad/shifted clock must never punish
        // the pet with an unbounded state jump.
        const int32_t now_epoch = CurrentValidEpoch();
        if (last_update_epoch_ > 0 && now_epoch > last_update_epoch_) {
            int32_t elapsed = now_epoch - last_update_epoch_;
            elapsed = std::min<int32_t>(elapsed, kMaxOfflineSimulationSeconds);
            ApplyElapsedLocked(static_cast<uint32_t>(elapsed));
        }

        ESP_LOGI(kTag,
                 "Pet core ready H=%u E=%u M=%u F=%u coins=%ld sleep=%d",
                 state_.hunger, state_.energy, state_.mood, state_.friendship,
                 static_cast<long>(state_.coins), state_.sleeping ? 1 : 0);
    }

    Snapshot GetSnapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        return state_;
    }

    // Cheap lazy update. Safe to call often; actual stat work runs only after
    // enough monotonic time has accumulated.
    void Tick() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
    }

    // Generic food transaction. Future food items only need to provide price
    // and effects; the engine does not need to know menu/UI details.
    bool Feed(int32_t price, uint8_t hunger_relief, uint8_t mood_bonus = 3) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (price < 0 || state_.coins < price) {
            return false;
        }
        state_.coins -= price;
        state_.hunger = SubClamp(state_.hunger, hunger_relief);
        state_.mood = AddClamp(state_.mood, mood_bonus);
        state_.friendship = AddClamp(state_.friendship, 1);
        state_.sleeping = false;
        TouchInteractionLocked();
        dirty_ = true;
        return true;
    }

    // Called after a future minigame finishes. Reward and costs are arguments
    // so multiple simple games can share the same pet/economy core.
    void CompletePlay(int32_t coin_reward, uint8_t mood_bonus = 10,
                      uint8_t energy_cost = 8, uint8_t hunger_cost = 3) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (coin_reward > 0) {
            state_.coins = std::min<int32_t>(kMaxCoins, state_.coins + coin_reward);
        }
        state_.mood = AddClamp(state_.mood, mood_bonus);
        state_.energy = SubClamp(state_.energy, energy_cost);
        state_.hunger = AddClamp(state_.hunger, hunger_cost);
        state_.friendship = AddClamp(state_.friendship, 2);
        state_.sleeping = false;
        TouchInteractionLocked();
        dirty_ = true;
    }

    void Pet() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        state_.mood = AddClamp(state_.mood, 7);
        state_.friendship = AddClamp(state_.friendship, 1);
        TouchInteractionLocked();
        dirty_ = true;
    }

    void Sleep() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (!state_.sleeping) {
            state_.sleeping = true;
            dirty_ = true;
        }
    }

    void Wake() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (state_.sleeping) {
            state_.sleeping = false;
            TouchInteractionLocked();
            dirty_ = true;
        }
    }

    bool ShouldAutoSleep() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        return !state_.sleeping && state_.energy <= kAutoSleepEnergy;
    }

    bool CanAfford(int32_t price) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        return price >= 0 && state_.coins >= price;
    }

    // Designed for a future safe application-idle checkpoint. It does nothing
    // unless the state changed and at least ten minutes passed since last save.
    bool MaybeSave() {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
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
    static constexpr uint8_t kSchemaVersion = 1;
    static constexpr int32_t kValidEpochThreshold = 1704067200; // 2024-01-01
    static constexpr int32_t kMaxOfflineSimulationSeconds = 12 * 60 * 60;
    static constexpr int64_t kNormalSaveIntervalUs = 10LL * 60 * 1000 * 1000;
    static constexpr int32_t kMaxCoins = 999999;
    static constexpr uint8_t kAutoSleepEnergy = 20;

    // Gentle rates: state influences behavior, but neglect is not punitive.
    static constexpr uint32_t kAwakeHungerStepSeconds = 12 * 60;
    static constexpr uint32_t kSleepHungerStepSeconds = 20 * 60;
    static constexpr uint32_t kAwakeEnergyStepSeconds = 10 * 60;
    static constexpr uint32_t kSleepEnergyStepSeconds = 5 * 60;
    static constexpr uint32_t kMoodStepSeconds = 15 * 60;

    PetStateEngine() = default;

    Snapshot state_{};
    std::mutex mutex_;
    bool initialized_ = false;
    bool dirty_ = false;
    int32_t last_update_epoch_ = 0;
    int64_t last_monotonic_us_ = 0;
    int64_t last_save_monotonic_us_ = 0;
    uint32_t hunger_remainder_s_ = 0;
    uint32_t energy_remainder_s_ = 0;
    uint32_t mood_remainder_s_ = 0;

    static uint8_t AddClamp(uint8_t value, uint32_t amount) {
        return static_cast<uint8_t>(std::min<uint32_t>(100, value + amount));
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
    }

    static uint32_t PackStats(const Snapshot& s) {
        uint32_t packed = 0;
        packed |= static_cast<uint32_t>(std::min<uint8_t>(100, s.hunger));
        packed |= static_cast<uint32_t>(std::min<uint8_t>(100, s.energy)) << 7;
        packed |= static_cast<uint32_t>(std::min<uint8_t>(100, s.mood)) << 14;
        packed |= static_cast<uint32_t>(std::min<uint8_t>(100, s.friendship)) << 21;
        if (s.sleeping) {
            packed |= (1u << 28);
        }
        return packed;
    }

    static void UnpackStats(uint32_t packed, Snapshot& s) {
        s.hunger = static_cast<uint8_t>(std::min<uint32_t>(100, packed & 0x7F));
        s.energy = static_cast<uint8_t>(std::min<uint32_t>(100, (packed >> 7) & 0x7F));
        s.mood = static_cast<uint8_t>(std::min<uint32_t>(100, (packed >> 14) & 0x7F));
        s.friendship = static_cast<uint8_t>(std::min<uint32_t>(100, (packed >> 21) & 0x7F));
        s.sleeping = ((packed >> 28) & 0x01) != 0;
    }

    void LoadLocked() {
        nvs_handle_t handle = 0;
        const esp_err_t open_err = nvs_open(kNamespace, NVS_READONLY, &handle);
        if (open_err != ESP_OK) {
            // First boot is expected to have no pet namespace yet.
            return;
        }

        uint8_t version = 0;
        uint32_t packed = 0;
        int32_t coins = state_.coins;
        int32_t last_interaction = 0;
        int32_t last_update = 0;

        const bool valid =
            nvs_get_u8(handle, "ver", &version) == ESP_OK &&
            version == kSchemaVersion &&
            nvs_get_u32(handle, "stats", &packed) == ESP_OK;

        if (valid) {
            UnpackStats(packed, state_);
            if (nvs_get_i32(handle, "coins", &coins) == ESP_OK) {
                state_.coins = std::clamp<int32_t>(coins, 0, kMaxCoins);
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

    void UpdateRuntimeLocked() {
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

        const uint32_t hunger_step = state_.sleeping ? kSleepHungerStepSeconds
                                                      : kAwakeHungerStepSeconds;
        hunger_remainder_s_ += elapsed_s;
        if (hunger_remainder_s_ >= hunger_step) {
            const uint32_t steps = hunger_remainder_s_ / hunger_step;
            hunger_remainder_s_ %= hunger_step;
            const uint8_t before = state_.hunger;
            state_.hunger = AddClamp(state_.hunger, steps);
            dirty_ = dirty_ || state_.hunger != before;
        }

        energy_remainder_s_ += elapsed_s;
        const uint32_t energy_step = state_.sleeping ? kSleepEnergyStepSeconds
                                                      : kAwakeEnergyStepSeconds;
        if (energy_remainder_s_ >= energy_step) {
            const uint32_t steps = energy_remainder_s_ / energy_step;
            energy_remainder_s_ %= energy_step;
            const uint8_t before = state_.energy;
            state_.energy = state_.sleeping ? AddClamp(state_.energy, steps)
                                             : SubClamp(state_.energy, steps);
            dirty_ = dirty_ || state_.energy != before;
        }

        mood_remainder_s_ += elapsed_s;
        if (mood_remainder_s_ >= kMoodStepSeconds) {
            const uint32_t steps = mood_remainder_s_ / kMoodStepSeconds;
            mood_remainder_s_ %= kMoodStepSeconds;
            const uint8_t before = state_.mood;
            if (state_.hunger >= 75 || state_.energy <= 25) {
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
