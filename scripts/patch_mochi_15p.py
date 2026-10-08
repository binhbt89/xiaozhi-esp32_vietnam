from pathlib import Path

DISPLAY = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')
PET = Path('main/pet/pet_state_engine.h')


def one(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f'{label}: expected 1 match, got {n}')
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# Display: keep the good 15N geometry, but use a fixed 68x54 canvas and native
# 38x38 approved-art heart. No runtime interpolation and no transform leakage.
# -----------------------------------------------------------------------------
s = DISPLAY.read_text(encoding='utf-8')

# 15N currently uses 58px side offset / y58. The 68px-wide fixed canvas uses a
# 66px center offset: at reaction_base_x <= 8 its right edge remains <=250,
# otherwise the food flips left. Across the full -50..50 walk range, bounds are
# safely inside the scene. Vertically the 54px canvas spans y=157..211.
s = one(s,
        'reaction_side_ = (mochi_x_ >= 20) ? -1 : 1;',
        'reaction_side_ = (mochi_x_ > 8) ? -1 : 1;',
        'safe food side threshold')
s = one(s,
        'const int food_x = reaction_base_x_ + reaction_side_ * 58;',
        'const int food_x = reaction_base_x_ + reaction_side_ * 66;',
        'fixed food x center')
s = one(s,
        'food_x, 58);',
        'food_x, 64);',
        'fixed food y center')

# Reset scale whenever the reused LVGL reaction image changes source.
s = one(s,
'''        if (obj == nullptr || src == nullptr) return;
        lv_image_set_src(obj, src);
        lv_obj_align(obj, LV_ALIGN_CENTER, x, y);''',
'''        if (obj == nullptr || src == nullptr) return;
        lv_image_set_src(obj, src);
        lv_image_set_scale(obj, LV_SCALE_NONE);
        lv_obj_align(obj, LV_ALIGN_CENTER, x, y);''',
        'reaction scale reset')

# 15P emits the approved 15M heart as native 38x38 nearest-neighbor art, so
# remove 15N's runtime 1.25x interpolation. Keep the close y=10 placement.
s = one(s,
'''    void ShowHappyHeartLvgl(int x = 0, int y = 10) {
        SetReactionImageLvgl(reaction_aux_, kMochiActionHeart, x, y);
        if (reaction_aux_ != nullptr) lv_image_set_scale(reaction_aux_, 320);
    }''',
'''    void ShowHappyHeartLvgl(int x = 0, int y = 10) {
        SetReactionImageLvgl(reaction_aux_, kMochiActionHeart, x, y);
    }''',
        'native approved heart')

DISPLAY.write_text(s, encoding='utf-8')


# -----------------------------------------------------------------------------
# Pet state: one-time +300 test-coin migration. It is stored in the same NVS
# commit as the new coin balance, so power cycling cannot repeatedly grant it.
# -----------------------------------------------------------------------------
p = PET.read_text(encoding='utf-8')

p = one(p,
'''    bool migration_pending_ = false;
    bool offline_sleep_reconciled_ = false;''',
'''    bool migration_pending_ = false;
    bool test_coin_grant_applied_ = false;
    bool offline_sleep_reconciled_ = false;''',
        'coin grant field')

# Public Initialize() path.
p = one(p,
'''        LoadLocked();
        last_monotonic_us_ = esp_timer_get_time();
        last_save_monotonic_us_ = last_monotonic_us_;
        initialized_ = true;

        // On this board PetStateEngine starts before the OTA/bootstrap path may''',
'''        LoadLocked();
        ApplyOneTimeTestCoinGrantLocked();
        last_monotonic_us_ = esp_timer_get_time();
        last_save_monotonic_us_ = last_monotonic_us_;
        initialized_ = true;

        // On this board PetStateEngine starts before the OTA/bootstrap path may''',
        'public initialize grant')

# Add the helper immediately before the lazy initialization path.
p = one(p,
'''    void EnsureInitializedLocked() {
        if (initialized_) {
            return;
        }
        LoadLocked();
        last_monotonic_us_ = esp_timer_get_time();''',
'''    void ApplyOneTimeTestCoinGrantLocked() {
        if (test_coin_grant_applied_) return;
        state_.coins = std::min<int32_t>(kMaxCoins, state_.coins + 300);
        test_coin_grant_applied_ = true;
        migration_pending_ = true;
        dirty_ = true;
        ESP_LOGI(kTag, "15P one-time test coin grant applied: coins=%ld",
                 static_cast<long>(state_.coins));
    }

    void EnsureInitializedLocked() {
        if (initialized_) {
            return;
        }
        LoadLocked();
        ApplyOneTimeTestCoinGrantLocked();
        last_monotonic_us_ = esp_timer_get_time();''',
        'lazy initialize grant')

# Load the migration flag. 15J already introduces free_day/free_mask here.
p = one(p,
'''        int32_t free_day = 0;
        uint8_t free_mask = 0;
''',
'''        int32_t free_day = 0;
        uint8_t free_mask = 0;
        uint8_t coin300 = 0;
''',
        'coin grant load var')
p = one(p,
'''            if (nvs_get_u8(handle, "free_mask", &free_mask) == ESP_OK) {
                daily_free_mask_ = static_cast<uint8_t>(free_mask & 0x07u);
            }
''',
'''            if (nvs_get_u8(handle, "free_mask", &free_mask) == ESP_OK) {
                daily_free_mask_ = static_cast<uint8_t>(free_mask & 0x07u);
            }
            if (nvs_get_u8(handle, "coin300", &coin300) == ESP_OK) {
                test_coin_grant_applied_ = coin300 != 0;
            }
''',
        'coin grant load key')

# Persist the flag in the same transaction as coins and daily freebies.
p = one(p,
'''            (err = nvs_set_i32(handle, "free_day", daily_free_day_key_)) == ESP_OK &&
            (err = nvs_set_u8(handle, "free_mask", daily_free_mask_)) == ESP_OK) {
            err = nvs_commit(handle);
        }
''',
'''            (err = nvs_set_i32(handle, "free_day", daily_free_day_key_)) == ESP_OK &&
            (err = nvs_set_u8(handle, "free_mask", daily_free_mask_)) == ESP_OK &&
            (err = nvs_set_u8(handle, "coin300", test_coin_grant_applied_ ? 1 : 0)) == ESP_OK) {
            err = nvs_commit(handle);
        }
''',
        'coin grant save key')

PET.write_text(p, encoding='utf-8')
print('Applied Mochi 15P: approved-art fixed canvas + native heart + one-time 300 test coins')
