from pathlib import Path

PET = Path('main/pet/pet_state_engine.h')
DISPLAY = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one match, got {count}')
    return text.replace(old, new, 1)


def function_block(text: str, signature: str):
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'function not found: {signature}')
    brace = text.find('{', start)
    if brace < 0:
        raise SystemExit(f'opening brace not found: {signature}')
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return start, i + 1, text[start:i + 1]
    raise SystemExit(f'unclosed function: {signature}')


def replace_in_function(text: str, signature: str, old: str, new: str, label: str):
    start, end, block = function_block(text, signature)
    count = block.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one match inside {signature}, got {count}')
    return text[:start] + block.replace(old, new, 1) + text[end:]


# -----------------------------------------------------------------------------
# Pet core -> lock-free reaction mailbox.
# State/economy remains owned by the existing mutex/NVS code. The visual layer
# only consumes a tiny one-byte event, so the LVGL 200 ms timer never performs
# NVS work and never needs to take PetStateEngine's mutex for reactions.
# -----------------------------------------------------------------------------
pet = PET.read_text(encoding='utf-8')
pet = replace_once(pet, '#include <algorithm>\n', '#include <algorithm>\n#include <atomic>\n', 'atomic include')

pet = replace_once(
    pet,
    '''    enum class FeedResult : uint8_t {
        SuccessFree = 0,
        SuccessPaid,
        InsufficientCoins,
        InvalidFood,
    };
''',
    '''    enum class FeedResult : uint8_t {
        SuccessFree = 0,
        SuccessPaid,
        InsufficientCoins,
        InvalidFood,
    };

    enum class ReactionEvent : uint8_t {
        None = 0,
        FeedFish,
        FeedMeal,
        FeedDessert,
        Pet,
        Play,
        Sleep,
        Wake,
    };
''',
    'reaction enum')

pet = replace_once(
    pet,
    '''    bool CanAfford(int32_t price) {
''',
    '''    ReactionEvent ConsumeReactionEvent() {
        const uint8_t raw = pending_reaction_.exchange(
            static_cast<uint8_t>(ReactionEvent::None), std::memory_order_acq_rel);
        if (raw > static_cast<uint8_t>(ReactionEvent::Wake)) {
            return ReactionEvent::None;
        }
        return static_cast<ReactionEvent>(raw);
    }

    bool CanAfford(int32_t price) {
''',
    'reaction consume API')

pet = replace_once(
    pet,
    '''    Snapshot state_{};
    std::mutex mutex_;
''',
    '''    Snapshot state_{};
    std::mutex mutex_;
    std::atomic<uint8_t> pending_reaction_{static_cast<uint8_t>(ReactionEvent::None)};
''',
    'reaction mailbox field')

pet = replace_in_function(
    pet, '    FeedResult FeedFood(FoodType type)',
    '''        SaveLocked(esp_timer_get_time());

        ESP_LOGI''',
    '''        SaveLocked(esp_timer_get_time());
        pending_reaction_.store(
            static_cast<uint8_t>(ReactionEvent::FeedFish) + static_cast<uint8_t>(type),
            std::memory_order_release);

        ESP_LOGI''',
    'Feed reaction')

pet = replace_in_function(
    pet, '    void Pet()',
    '''        dirty_ = true;
        CheckpointIfDueLocked();''',
    '''        dirty_ = true;
        pending_reaction_.store(static_cast<uint8_t>(ReactionEvent::Pet),
                                std::memory_order_release);
        CheckpointIfDueLocked();''',
    'Pet reaction')

pet = replace_in_function(
    pet, '    bool Play(uint8_t energy_cost = 10, uint8_t mood_bonus = 10)',
    '''        SaveLocked(esp_timer_get_time());
        return true;''',
    '''        SaveLocked(esp_timer_get_time());
        pending_reaction_.store(static_cast<uint8_t>(ReactionEvent::Play),
                                std::memory_order_release);
        return true;''',
    'Play reaction')

pet = replace_in_function(
    pet, '    void Sleep()',
    '''            SaveLocked(esp_timer_get_time());
''',
    '''            SaveLocked(esp_timer_get_time());
            pending_reaction_.store(static_cast<uint8_t>(ReactionEvent::Sleep),
                                    std::memory_order_release);
''',
    'Sleep reaction')

pet = replace_in_function(
    pet, '    void Wake()',
    '''            SaveLocked(esp_timer_get_time());
''',
    '''            SaveLocked(esp_timer_get_time());
            pending_reaction_.store(static_cast<uint8_t>(ReactionEvent::Wake),
                                    std::memory_order_release);
''',
    'Wake reaction')

PET.write_text(pet, encoding='utf-8')


# -----------------------------------------------------------------------------
# LVGL reaction controller.
# Voice > reaction > persistent sleep/state > weighted idle.
# We reuse the already hardware-tested Mochi emotions and the approved food/HUD
# bitmap art. No new task, asset partition, audio path, or NVS access is added.
# -----------------------------------------------------------------------------
display = DISPLAY.read_text(encoding='utf-8')

display = replace_once(
    display,
    '''    std::atomic_bool menu_select_requested_{false};

    std::shared_ptr<LvglCBinImage> active_background_;
''',
    '''    std::atomic_bool menu_select_requested_{false};

    // 15K explicit interaction reaction layer. Only AmbientTickLvgl mutates
    // these LVGL objects; PetStateEngine delivers events through an atomic byte.
    lv_obj_t* reaction_icon_ = nullptr;
    PetStateEngine::ReactionEvent active_reaction_ = PetStateEngine::ReactionEvent::None;
    int reaction_ticks_left_ = 0;
    int reaction_elapsed_ticks_ = 0;

    std::shared_ptr<LvglCBinImage> active_background_;
''',
    'reaction display fields')

helpers = r'''    void CreateReactionObjectsLvgl(lv_obj_t* screen) {
        reaction_icon_ = lv_image_create(screen);
        lv_obj_add_flag(reaction_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(reaction_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(reaction_icon_, LV_OBJ_FLAG_SCROLLABLE);
    }

    void HideReactionIconLvgl() {
        if (reaction_icon_ != nullptr) {
            lv_obj_add_flag(reaction_icon_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(reaction_icon_, LV_OPA_COVER, 0);
        }
    }

    void CancelPetReactionLvgl() {
        active_reaction_ = PetStateEngine::ReactionEvent::None;
        reaction_ticks_left_ = 0;
        reaction_elapsed_ticks_ = 0;
        HideReactionIconLvgl();
        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_y(emoji_box_, 50, 0);
        }
    }

    void SetReactionIconLvgl(const lv_image_dsc_t* src) {
        if (reaction_icon_ == nullptr || src == nullptr) return;
        lv_image_set_src(reaction_icon_, src);
        lv_obj_align(reaction_icon_, LV_ALIGN_CENTER, mochi_x_, 4);
        lv_obj_set_style_opa(reaction_icon_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(reaction_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(reaction_icon_);
        if (hud_panel_ != nullptr) lv_obj_move_foreground(hud_panel_);
    }

    void StartPetReactionLvgl(PetStateEngine::ReactionEvent event) {
        if (event == PetStateEngine::ReactionEvent::None) return;

        active_reaction_ = event;
        reaction_elapsed_ticks_ = 0;
        HideReactionIconLvgl();

        switch (event) {
            case PetStateEngine::ReactionEvent::FeedFish:
                reaction_ticks_left_ = 15; // 3.0s
                SetReactionIconLvgl(kMochiFoodAssets[0]);
                QueueAmbientEmotion("ambient_sit");
                break;
            case PetStateEngine::ReactionEvent::FeedMeal:
                reaction_ticks_left_ = 15;
                SetReactionIconLvgl(kMochiFoodAssets[1]);
                QueueAmbientEmotion("ambient_sit");
                break;
            case PetStateEngine::ReactionEvent::FeedDessert:
                reaction_ticks_left_ = 15;
                SetReactionIconLvgl(kMochiFoodAssets[2]);
                QueueAmbientEmotion("ambient_sit");
                break;
            case PetStateEngine::ReactionEvent::Pet:
                reaction_ticks_left_ = 12; // 2.4s
                SetReactionIconLvgl(&mochi_hud_friendship);
                QueueAmbientEmotion("ambient_sit");
                break;
            case PetStateEngine::ReactionEvent::Play:
                reaction_ticks_left_ = 16; // 3.2s
                SetReactionIconLvgl(&mochi_hud_energy);
                QueueAmbientEmotion("ambient_walk_right");
                break;
            case PetStateEngine::ReactionEvent::Sleep:
                reaction_ticks_left_ = 12; // lie down -> sleep
                QueueAmbientEmotion("ambient_lie");
                break;
            case PetStateEngine::ReactionEvent::Wake:
                reaction_ticks_left_ = 12; // sit up -> happy idle
                SetReactionIconLvgl(&mochi_hud_mood);
                QueueAmbientEmotion("ambient_sit");
                break;
            default:
                CancelPetReactionLvgl();
                break;
        }

        action_ticks_left_ = 0;
        idle_stable_ticks_ = kIdleSettleTicks;
    }

    bool StepPetReactionLvgl() {
        if (active_reaction_ == PetStateEngine::ReactionEvent::None ||
            reaction_ticks_left_ <= 0) {
            return false;
        }

        ++reaction_elapsed_ticks_;
        const int bob_phase = reaction_elapsed_ticks_ % 4;
        const int bob = (bob_phase == 1 || bob_phase == 3) ? 3 :
                        (bob_phase == 2 ? 6 : 0);

        const bool is_sleep = active_reaction_ == PetStateEngine::ReactionEvent::Sleep;
        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_y(emoji_box_, is_sleep ? 50 : (50 - bob), 0);
        }
        if (reaction_icon_ != nullptr &&
            !lv_obj_has_flag(reaction_icon_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_align(reaction_icon_, LV_ALIGN_CENTER, mochi_x_, 4 - bob);
        }

        switch (active_reaction_) {
            case PetStateEngine::ReactionEvent::FeedFish:
            case PetStateEngine::ReactionEvent::FeedMeal:
            case PetStateEngine::ReactionEvent::FeedDessert:
                // Food hovers while Mochi sits, then a short pleased idle pose.
                if (reaction_elapsed_ticks_ == 9) QueueAmbientEmotion("ambient_idle");
                break;
            case PetStateEngine::ReactionEvent::Pet:
                // Heart + gentle two-bounce cuddle reaction.
                if (reaction_elapsed_ticks_ == 7) QueueAmbientEmotion("ambient_idle");
                break;
            case PetStateEngine::ReactionEvent::Play:
                // Energetic alternating run without changing the persistent
                // world position, so it cannot collide with the right HUD.
                if (reaction_elapsed_ticks_ == 5) QueueAmbientEmotion("ambient_walk_left");
                if (reaction_elapsed_ticks_ == 9) QueueAmbientEmotion("ambient_walk_right");
                if (reaction_elapsed_ticks_ == 13) QueueAmbientEmotion("ambient_idle");
                break;
            case PetStateEngine::ReactionEvent::Sleep:
                if (reaction_elapsed_ticks_ == 5) QueueAmbientEmotion("ambient_sleep");
                break;
            case PetStateEngine::ReactionEvent::Wake:
                if (reaction_elapsed_ticks_ == 6) QueueAmbientEmotion("ambient_idle");
                break;
            default:
                break;
        }

        --reaction_ticks_left_;
        if (reaction_ticks_left_ > 0) return true;

        HideReactionIconLvgl();
        if (emoji_box_ != nullptr) lv_obj_set_style_translate_y(emoji_box_, 50, 0);
        active_reaction_ = PetStateEngine::ReactionEvent::None;
        reaction_elapsed_ticks_ = 0;
        action_ticks_left_ = 8;
        idle_stable_ticks_ = 0;
        return false;
    }

'''

anchor = '    void ApplyMochiLayout() {'
if display.count(anchor) != 1:
    raise SystemExit('ApplyMochiLayout anchor mismatch')
display = display.replace(anchor, helpers + anchor, 1)

display = replace_once(
    display,
    '''        CreateHudLvgl(screen);
        CreateMenuLvgl(screen);
''',
    '''        CreateReactionObjectsLvgl(screen);
        CreateHudLvgl(screen);
        CreateMenuLvgl(screen);
''',
    'reaction object creation')

old_tick = '''        // Menu is a foreground interaction mode; pause decorative movement while
        // it is open but keep HUD/menu refresh alive.
        if (menu_open_.load()) {
            return;
        }

        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            idle_stable_ticks_ = 0;
            action_ticks_left_ = 8;
            return;
        }
'''
new_tick = '''        // Menu is a foreground interaction mode; pause decorative movement while
        // it is open but keep HUD/menu refresh alive.
        if (menu_open_.load()) {
            return;
        }

        // Voice remains absolute priority. If Talk starts during a reaction,
        // clear only the visual reaction state; persistent pet state is untouched.
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            CancelPetReactionLvgl();
            idle_stable_ticks_ = 0;
            action_ticks_left_ = 8;
            return;
        }

        // Explicit interaction reactions outrank persistent sleep/weighted idle.
        // The mailbox is atomic, so this adds no PetState mutex/NVS work here.
        const auto pending_reaction = PetStateEngine::GetInstance().ConsumeReactionEvent();
        if (pending_reaction != PetStateEngine::ReactionEvent::None) {
            StartPetReactionLvgl(pending_reaction);
        }
        if (active_reaction_ != PetStateEngine::ReactionEvent::None) {
            StepPetReactionLvgl();
            return;
        }
'''
display = replace_once(display, old_tick, new_tick, 'reaction priority in AmbientTick')

DISPLAY.write_text(display, encoding='utf-8')
print('Applied Mochi 15K: Feed/Pet/Play/Sleep/Wake explicit LVGL reactions with voice priority')
