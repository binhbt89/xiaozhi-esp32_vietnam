from pathlib import Path

DISPLAY = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')
PET = Path('main/pet/pet_state_engine.h')


def replace_once(text, old, new, label):
    count=text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one match, got {count}')
    return text.replace(old,new,1)

# -----------------------------------------------------------------------------
# Pet economy: three foods, first serving of EACH food free once per device day.
# -----------------------------------------------------------------------------
pet=PET.read_text(encoding='utf-8')
pet=replace_once(pet,
'''    enum class LifecycleTransition : uint8_t {
        None = 0,
        AutoSleep,
        AutoWake,
    };
''',
'''    enum class LifecycleTransition : uint8_t {
        None = 0,
        AutoSleep,
        AutoWake,
    };

    enum class FoodType : uint8_t {
        FishCake = 0,
        Meal,
        Dessert,
        Count,
    };

    enum class FeedResult : uint8_t {
        SuccessFree = 0,
        SuccessPaid,
        InsufficientCoins,
        InvalidFood,
    };

    static constexpr int32_t GetFoodPrice(FoodType type) {
        switch (type) {
            case FoodType::FishCake: return 5;
            case FoodType::Meal: return 12;
            case FoodType::Dessert: return 18;
            default: return 0;
        }
    }
''','food enums')

anchor='''    void Pet() {
'''
food_methods=r'''    bool IsDailyFoodFreeAvailable(FoodType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        if (type >= FoodType::Count) return false;
        const int32_t day = RefreshDailyFreeLocked();
        if (day <= 0) return false;
        const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(type));
        return (daily_free_mask_ & bit) == 0;
    }

    FeedResult FeedFood(FoodType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureInitializedLocked();
        UpdateRuntimeLocked();
        if (type >= FoodType::Count) return FeedResult::InvalidFood;

        uint8_t fullness_gain = 0;
        uint8_t mood_bonus = 0;
        switch (type) {
            case FoodType::FishCake:
                fullness_gain = 12;
                mood_bonus = 2;
                break;
            case FoodType::Meal:
                fullness_gain = 28;
                mood_bonus = 3;
                break;
            case FoodType::Dessert:
                fullness_gain = 12;
                mood_bonus = 10;
                break;
            default:
                return FeedResult::InvalidFood;
        }

        const int32_t day = RefreshDailyFreeLocked();
        const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(type));
        const bool free_today = day > 0 && (daily_free_mask_ & bit) == 0;
        const int32_t price = free_today ? 0 : GetFoodPrice(type);
        if (state_.coins < price) {
            return FeedResult::InsufficientCoins;
        }

        state_.coins -= price;
        state_.fullness = AddClamp(state_.fullness, fullness_gain);
        state_.mood = AddClamp(state_.mood, mood_bonus);
        state_.friendship = AddClamp(state_.friendship, 1);
        state_.sleeping = false;
        state_.manual_sleep = false;
        if (free_today) daily_free_mask_ = static_cast<uint8_t>(daily_free_mask_ | bit);
        TouchInteractionLocked();
        dirty_ = true;
        SaveLocked(esp_timer_get_time());

        ESP_LOGI(kTag, "Feed food=%u free=%d price=%ld Full=%u Mood=%u coins=%ld",
                 static_cast<unsigned>(type), free_today ? 1 : 0,
                 static_cast<long>(price), state_.fullness, state_.mood,
                 static_cast<long>(state_.coins));
        return free_today ? FeedResult::SuccessFree : FeedResult::SuccessPaid;
    }

'''
if pet.count(anchor)!=1: raise SystemExit('Pet() anchor mismatch')
pet=pet.replace(anchor,food_methods+anchor,1)

pet=replace_once(pet,
'''    int32_t last_update_epoch_ = 0;
    int64_t last_monotonic_us_ = 0;
''',
'''    int32_t last_update_epoch_ = 0;
    int32_t daily_free_day_key_ = 0;
    uint8_t daily_free_mask_ = 0;
    int64_t last_monotonic_us_ = 0;
''','daily fields')

pet=replace_once(pet,
'''    static int32_t CurrentValidEpoch() {
        const time_t now = time(nullptr);
        if (now < kValidEpochThreshold || now > INT32_MAX) {
            return 0;
        }
        return static_cast<int32_t>(now);
    }
''',
'''    static int32_t CurrentValidEpoch() {
        const time_t now = time(nullptr);
        if (now < kValidEpochThreshold || now > INT32_MAX) {
            return 0;
        }
        return static_cast<int32_t>(now);
    }

    static int32_t CurrentDayKey() {
        const int32_t now = CurrentValidEpoch();
        return now > 0 ? now / (24 * 60 * 60) : 0;
    }

    int32_t RefreshDailyFreeLocked() {
        const int32_t day = CurrentDayKey();
        if (day <= 0) return 0;
        if (daily_free_day_key_ != day) {
            daily_free_day_key_ = day;
            daily_free_mask_ = 0;
            dirty_ = true;
            ESP_LOGI(kTag, "Daily food freebies reset for day=%ld", static_cast<long>(day));
        }
        return day;
    }
''','day helper')

pet=replace_once(pet,
'''        int32_t last_interaction = 0;
        int32_t last_update = 0;
''',
'''        int32_t last_interaction = 0;
        int32_t last_update = 0;
        int32_t free_day = 0;
        uint8_t free_mask = 0;
''','load daily vars')

pet=replace_once(pet,
'''            if (nvs_get_i32(handle, "last_upd", &last_update) == ESP_OK) {
                last_update_epoch_ = last_update;
            }
''',
'''            if (nvs_get_i32(handle, "last_upd", &last_update) == ESP_OK) {
                last_update_epoch_ = last_update;
            }
            if (nvs_get_i32(handle, "free_day", &free_day) == ESP_OK) {
                daily_free_day_key_ = free_day;
            }
            if (nvs_get_u8(handle, "free_mask", &free_mask) == ESP_OK) {
                daily_free_mask_ = static_cast<uint8_t>(free_mask & 0x07u);
            }
''','load daily keys')

pet=replace_once(pet,
'''            (err = nvs_set_i32(handle, "last_int", state_.last_interaction_epoch)) == ESP_OK &&
            (err = nvs_set_i32(handle, "last_upd", last_update_epoch_)) == ESP_OK) {
            err = nvs_commit(handle);
        }
''',
'''            (err = nvs_set_i32(handle, "last_int", state_.last_interaction_epoch)) == ESP_OK &&
            (err = nvs_set_i32(handle, "last_upd", last_update_epoch_)) == ESP_OK &&
            (err = nvs_set_i32(handle, "free_day", daily_free_day_key_)) == ESP_OK &&
            (err = nvs_set_u8(handle, "free_mask", daily_free_mask_)) == ESP_OK) {
            err = nvs_commit(handle);
        }
''','save daily keys')
PET.write_text(pet,encoding='utf-8')

# -----------------------------------------------------------------------------
# UI: Feed submenu with approved 28x28 food art.
# -----------------------------------------------------------------------------
display=DISPLAY.read_text(encoding='utf-8')
display=replace_once(display,
'#include "mochi_hud_icon_assets.h"',
'#include "mochi_hud_icon_assets.h"\n#include "mochi_food_assets.h"',
'food asset include')

display=replace_once(display,
'''    enum class PetMenuItem : uint8_t {
        Pet = 0,
        Play,
        SleepWake,
        Close,
        Count
    };
''',
'''    enum class PetMenuItem : uint8_t {
        Feed = 0,
        Pet,
        Play,
        SleepWake,
        Close,
        Count
    };

    enum class FoodMenuItem : uint8_t {
        FishCake = 0,
        Meal,
        Dessert,
        Back,
        Count
    };

    enum class MenuPage : uint8_t {
        Main = 0,
        Food,
    };
''','menu enums')

display=replace_once(display,
'''    static constexpr size_t kMenuItemCount = static_cast<size_t>(PetMenuItem::Count);
''',
'''    static constexpr size_t kMainMenuItemCount = static_cast<size_t>(PetMenuItem::Count);
    static constexpr size_t kFoodMenuItemCount = static_cast<size_t>(FoodMenuItem::Count);
    static constexpr size_t kMenuRowCapacity = kMainMenuItemCount;
    static_assert(kMenuRowCapacity >= kFoodMenuItemCount);
''','menu constants')

display=replace_once(display,
'''    lv_obj_t* menu_overlay_ = nullptr;
    lv_obj_t* menu_card_ = nullptr;
    std::array<lv_obj_t*, kMenuItemCount> menu_rows_{};
    std::array<lv_obj_t*, kMenuItemCount> menu_row_labels_{};
    uint8_t menu_selected_ = 0;
''',
'''    lv_obj_t* menu_overlay_ = nullptr;
    lv_obj_t* menu_card_ = nullptr;
    lv_obj_t* menu_title_ = nullptr;
    std::array<lv_obj_t*, kMenuRowCapacity> menu_rows_{};
    std::array<lv_obj_t*, kMenuRowCapacity> menu_row_labels_{};
    std::array<lv_obj_t*, 3> menu_food_icons_{};
    MenuPage menu_page_ = MenuPage::Main;
    uint8_t menu_selected_ = 0;
''','menu fields')

start=display.find('    void CreateMenuLvgl(lv_obj_t* screen) {')
end=display.find('    void ApplyMochiLayout() {',start)
if start<0 or end<0: raise SystemExit('menu block anchors not found')

new_menu=r'''    void CreateMenuLvgl(lv_obj_t* screen) {
        const int status_h = StatusBarHeightLvgl();
        menu_overlay_ = lv_obj_create(screen);
        lv_obj_set_pos(menu_overlay_, 0, status_h);
        lv_obj_set_size(menu_overlay_, LV_HOR_RES, LV_VER_RES - status_h);
        lv_obj_set_style_radius(menu_overlay_, 0, 0);
        lv_obj_set_style_bg_color(menu_overlay_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_overlay_, static_cast<lv_opa_t>(92), 0);
        lv_obj_set_style_border_width(menu_overlay_, 0, 0);
        lv_obj_set_style_pad_all(menu_overlay_, 0, 0);
        lv_obj_set_scrollbar_mode(menu_overlay_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_CLICKABLE);

        menu_card_ = lv_obj_create(menu_overlay_);
        lv_obj_set_size(menu_card_, 192, 194);
        lv_obj_align(menu_card_, LV_ALIGN_CENTER, -8, 0);
        lv_obj_set_style_radius(menu_card_, 14, 0);
        lv_obj_set_style_bg_color(menu_card_, lv_color_hex(0x17202A), 0);
        lv_obj_set_style_bg_opa(menu_card_, static_cast<lv_opa_t>(220), 0);
        lv_obj_set_style_border_width(menu_card_, 1, 0);
        lv_obj_set_style_border_color(menu_card_, lv_color_hex(0x7F8C8D), 0);
        lv_obj_set_style_pad_all(menu_card_, 8, 0);
        lv_obj_set_style_pad_row(menu_card_, 3, 0);
        lv_obj_set_flex_flow(menu_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(menu_card_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(menu_card_, LV_SCROLLBAR_MODE_OFF);

        menu_title_ = lv_label_create(menu_card_);
        lv_label_set_text(menu_title_, "MOCHI");
        lv_obj_set_style_text_color(menu_title_, lv_color_hex(0xFFD98A), 0);

        for (size_t i = 0; i < kMenuRowCapacity; ++i) {
            menu_rows_[i] = lv_obj_create(menu_card_);
            lv_obj_set_size(menu_rows_[i], 164, 27);
            lv_obj_set_style_radius(menu_rows_[i], 7, 0);
            lv_obj_set_style_border_width(menu_rows_[i], 0, 0);
            lv_obj_set_style_pad_all(menu_rows_[i], 4, 0);
            lv_obj_set_scrollbar_mode(menu_rows_[i], LV_SCROLLBAR_MODE_OFF);
            lv_obj_clear_flag(menu_rows_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(menu_rows_[i], LV_OBJ_FLAG_CLICKABLE);

            if (i < menu_food_icons_.size()) {
                menu_food_icons_[i] = lv_image_create(menu_rows_[i]);
                lv_image_set_src(menu_food_icons_[i], kMochiFoodAssets[i]);
                lv_obj_align(menu_food_icons_[i], LV_ALIGN_LEFT_MID, 0, 0);
                lv_obj_clear_flag(menu_food_icons_[i], LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(menu_food_icons_[i], LV_OBJ_FLAG_HIDDEN);
            }

            menu_row_labels_[i] = lv_label_create(menu_rows_[i]);
            lv_obj_set_style_text_color(menu_row_labels_[i], lv_color_white(), 0);
            lv_obj_align(menu_row_labels_[i], LV_ALIGN_LEFT_MID, 2, 0);
        }

        lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        menu_open_.store(false);
    }

    void StyleMenuSelectionLvgl(size_t visible_count) {
        for (size_t i = 0; i < kMenuRowCapacity; ++i) {
            if (menu_rows_[i] == nullptr) continue;
            if (i >= visible_count) {
                lv_obj_add_flag(menu_rows_[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_remove_flag(menu_rows_[i], LV_OBJ_FLAG_HIDDEN);
            if (i == menu_selected_) {
                lv_obj_set_style_bg_color(menu_rows_[i], lv_color_hex(0x3B536B), 0);
                lv_obj_set_style_bg_opa(menu_rows_[i], static_cast<lv_opa_t>(190), 0);
            } else {
                lv_obj_set_style_bg_opa(menu_rows_[i], LV_OPA_TRANSP, 0);
            }
        }
    }

    void UpdateMenuRowsLvgl() {
        if (menu_overlay_ == nullptr) return;
        auto& engine = PetStateEngine::GetInstance();
        const auto pet = engine.GetSnapshot();

        if (menu_page_ == MenuPage::Main) {
            if (menu_title_ != nullptr) lv_label_set_text(menu_title_, "MOCHI");
            const char* texts[kMainMenuItemCount] = {
                "Feed",
                "Pet",
                "Play (-10 E)",
                pet.sleeping ? "Wake" : "Sleep",
                "Close"
            };
            for (size_t i = 0; i < kMainMenuItemCount; ++i) {
                if (i < menu_food_icons_.size() && menu_food_icons_[i] != nullptr)
                    lv_obj_add_flag(menu_food_icons_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(menu_rows_[i], 164, 27);
                lv_obj_align(menu_row_labels_[i], LV_ALIGN_LEFT_MID, 2, 0);
                char line[36];
                std::snprintf(line, sizeof(line), "%s %s", i == menu_selected_ ? ">" : " ", texts[i]);
                lv_label_set_text(menu_row_labels_[i], line);
            }
            StyleMenuSelectionLvgl(kMainMenuItemCount);
            return;
        }

        if (menu_title_ != nullptr) lv_label_set_text(menu_title_, "FEED");
        const PetStateEngine::FoodType types[3] = {
            PetStateEngine::FoodType::FishCake,
            PetStateEngine::FoodType::Meal,
            PetStateEngine::FoodType::Dessert
        };
        const char* names[3] = {"Fish Cake", "Meal Bowl", "Dessert"};
        for (size_t i = 0; i < 3; ++i) {
            lv_obj_set_size(menu_rows_[i], 164, 38);
            if (menu_food_icons_[i] != nullptr) lv_obj_remove_flag(menu_food_icons_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(menu_row_labels_[i], LV_ALIGN_LEFT_MID, 34, 0);
            const bool free_today = engine.IsDailyFoodFreeAvailable(types[i]);
            char line[44];
            if (free_today) {
                std::snprintf(line, sizeof(line), "%s%s FREE", i == menu_selected_ ? ">" : " ", names[i]);
            } else {
                std::snprintf(line, sizeof(line), "%s%s %ldc", i == menu_selected_ ? ">" : " ", names[i],
                              static_cast<long>(PetStateEngine::GetFoodPrice(types[i])));
            }
            lv_label_set_text(menu_row_labels_[i], line);
        }
        lv_obj_set_size(menu_rows_[3], 164, 32);
        lv_obj_align(menu_row_labels_[3], LV_ALIGN_LEFT_MID, 2, 0);
        lv_label_set_text(menu_row_labels_[3], menu_selected_ == 3 ? "> Back" : "  Back");
        StyleMenuSelectionLvgl(kFoodMenuItemCount);
    }

    void OpenMenuLvgl() {
        if (menu_overlay_ == nullptr) return;
        menu_page_ = MenuPage::Main;
        menu_selected_ = 0;
        UpdateMenuRowsLvgl();
        lv_obj_remove_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(menu_overlay_);
        menu_open_.store(true);
        action_ticks_left_ = 8;
    }

    void CloseMenuLvgl() {
        if (menu_overlay_ != nullptr) lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        menu_page_ = MenuPage::Main;
        menu_open_.store(false);
        menu_move_requested_.store(0);
        menu_select_requested_.store(false);
        idle_stable_ticks_ = 0;
    }

    void SelectMenuItemLvgl() {
        if (menu_page_ == MenuPage::Food) {
            const FoodMenuItem food_item = static_cast<FoodMenuItem>(menu_selected_);
            if (food_item == FoodMenuItem::Back || food_item == FoodMenuItem::Count) {
                menu_page_ = MenuPage::Main;
                menu_selected_ = 0;
                UpdateMenuRowsLvgl();
                return;
            }

            const auto food_type = static_cast<PetStateEngine::FoodType>(static_cast<uint8_t>(food_item));
            CloseMenuLvgl();
            Application::GetInstance().Schedule([food_type]() {
                PetStateEngine::GetInstance().FeedFood(food_type);
            });
            return;
        }

        const PetMenuItem item = static_cast<PetMenuItem>(menu_selected_);
        if (item == PetMenuItem::Feed) {
            menu_page_ = MenuPage::Food;
            menu_selected_ = 0;
            UpdateMenuRowsLvgl();
            return;
        }

        const auto pet = PetStateEngine::GetInstance().GetSnapshot();
        CloseMenuLvgl();
        Application::GetInstance().Schedule([item, was_sleeping = pet.sleeping]() {
            auto& engine = PetStateEngine::GetInstance();
            switch (item) {
                case PetMenuItem::Pet:
                    engine.Pet();
                    break;
                case PetMenuItem::Play:
                    engine.Play();
                    break;
                case PetMenuItem::SleepWake:
                    if (was_sleeping) engine.Wake();
                    else engine.Sleep();
                    break;
                case PetMenuItem::Feed:
                case PetMenuItem::Close:
                case PetMenuItem::Count:
                    break;
            }
        });
    }

    void ProcessMenuRequestsLvgl() {
        const auto state = Application::GetInstance().GetDeviceState();
        if (state != kDeviceStateIdle) {
            menu_toggle_requested_.store(false);
            menu_move_requested_.store(0);
            menu_select_requested_.store(false);
            if (menu_open_.load()) CloseMenuLvgl();
            return;
        }

        if (menu_toggle_requested_.exchange(false)) {
            if (menu_open_.load()) CloseMenuLvgl();
            else OpenMenuLvgl();
        }

        if (!menu_open_.load()) {
            menu_move_requested_.store(0);
            menu_select_requested_.store(false);
            return;
        }

        const int move = menu_move_requested_.exchange(0);
        if (move != 0) {
            int next = static_cast<int>(menu_selected_) + move;
            const int count = static_cast<int>(menu_page_ == MenuPage::Main ?
                                               kMainMenuItemCount : kFoodMenuItemCount);
            next %= count;
            if (next < 0) next += count;
            menu_selected_ = static_cast<uint8_t>(next);
            UpdateMenuRowsLvgl();
        }

        if (menu_select_requested_.exchange(false)) SelectMenuItemLvgl();
    }

'''
display=display[:start]+new_menu+display[end:]
DISPLAY.write_text(display,encoding='utf-8')
print('Applied Mochi 15J: Feed submenu, daily freebies, persisted food economy')
