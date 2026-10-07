from pathlib import Path

DISPLAY = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one match, got {count}')
    return text.replace(old, new, 1)


display = DISPLAY.read_text(encoding='utf-8')

display = replace_once(
    display,
    '#include "mochi_food_assets.h"',
    '#include "mochi_food_assets.h"\n#include "mochi_action_assets_15l.h"',
    '15L action asset include')

display = replace_once(
    display,
    '''    lv_obj_t* reaction_icon_ = nullptr;
    PetStateEngine::ReactionEvent active_reaction_ = PetStateEngine::ReactionEvent::None;
    int reaction_ticks_left_ = 0;
    int reaction_elapsed_ticks_ = 0;
''',
    '''    lv_obj_t* reaction_icon_ = nullptr;
    lv_obj_t* reaction_aux_ = nullptr;
    PetStateEngine::ReactionEvent active_reaction_ = PetStateEngine::ReactionEvent::None;
    int reaction_ticks_left_ = 0;
    int reaction_elapsed_ticks_ = 0;
    int reaction_food_index_ = -1;
    int reaction_side_ = 1;
    int reaction_base_x_ = 0;
''',
    '15L reaction fields')

start = display.find('    void CreateReactionObjectsLvgl(lv_obj_t* screen) {')
end = display.find('    void ApplyMochiLayout() {', start)
if start < 0 or end < 0 or end <= start:
    raise SystemExit('15K reaction helper block anchors not found')

helpers = r'''    static bool IsFeedReaction(PetStateEngine::ReactionEvent event) {
        return event == PetStateEngine::ReactionEvent::FeedFish ||
               event == PetStateEngine::ReactionEvent::FeedMeal ||
               event == PetStateEngine::ReactionEvent::FeedDessert;
    }

    static int FeedIndexForReaction(PetStateEngine::ReactionEvent event) {
        switch (event) {
            case PetStateEngine::ReactionEvent::FeedFish: return 0;
            case PetStateEngine::ReactionEvent::FeedMeal: return 1;
            case PetStateEngine::ReactionEvent::FeedDessert: return 2;
            default: return -1;
        }
    }

    void CreateReactionObjectsLvgl(lv_obj_t* screen) {
        reaction_icon_ = lv_image_create(screen);
        lv_obj_add_flag(reaction_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(reaction_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(reaction_icon_, LV_OBJ_FLAG_SCROLLABLE);

        reaction_aux_ = lv_image_create(screen);
        lv_obj_add_flag(reaction_aux_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(reaction_aux_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(reaction_aux_, LV_OBJ_FLAG_SCROLLABLE);
    }

    void HideReactionObjectLvgl(lv_obj_t* obj) {
        if (obj == nullptr) return;
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    }

    void HideReactionIconLvgl() {
        HideReactionObjectLvgl(reaction_icon_);
        HideReactionObjectLvgl(reaction_aux_);
    }

    void RestoreReactionTransformsLvgl() {
        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_x(emoji_box_, mochi_x_, 0);
            lv_obj_set_style_translate_y(emoji_box_, 50, 0);
        }
    }

    void CancelPetReactionLvgl() {
        active_reaction_ = PetStateEngine::ReactionEvent::None;
        reaction_ticks_left_ = 0;
        reaction_elapsed_ticks_ = 0;
        reaction_food_index_ = -1;
        reaction_side_ = 1;
        HideReactionIconLvgl();
        RestoreReactionTransformsLvgl();
    }

    void SetReactionImageLvgl(lv_obj_t* obj,
                              const lv_image_dsc_t* src,
                              int x,
                              int y,
                              lv_opa_t opa = LV_OPA_COVER) {
        if (obj == nullptr || src == nullptr) return;
        lv_image_set_src(obj, src);
        lv_obj_align(obj, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_opa(obj, opa, 0);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(obj);
        if (hud_panel_ != nullptr) lv_obj_move_foreground(hud_panel_);
    }

    void SetFeedStageLvgl(int stage) {
        if (reaction_food_index_ < 0 || reaction_food_index_ > 2 ||
            stage < 0 || stage > 2) return;
        const int food_x = reaction_base_x_ + reaction_side_ * 45;
        SetReactionImageLvgl(reaction_icon_,
                             kMochiActionFoodStages[reaction_food_index_][stage],
                             food_x, 42);
    }

    void ShowHappyHeartLvgl(int x = 0, int y = -4) {
        SetReactionImageLvgl(reaction_aux_, kMochiActionHeart, x, y);
    }

    void StartPetReactionLvgl(PetStateEngine::ReactionEvent event) {
        if (event == PetStateEngine::ReactionEvent::None) return;

        active_reaction_ = event;
        reaction_elapsed_ticks_ = 0;
        reaction_base_x_ = mochi_x_;
        reaction_food_index_ = FeedIndexForReaction(event);
        reaction_side_ = (mochi_x_ >= 20) ? -1 : 1;
        HideReactionIconLvgl();
        RestoreReactionTransformsLvgl();
        mochi_target_x_ = mochi_x_;

        switch (event) {
            case PetStateEngine::ReactionEvent::FeedFish:
            case PetStateEngine::ReactionEvent::FeedMeal:
            case PetStateEngine::ReactionEvent::FeedDessert:
                reaction_ticks_left_ = 22; // settle -> 3 bites -> happy finish
                SetFeedStageLvgl(0);
                QueueAmbientEmotion("ambient_sit");
                break;
            case PetStateEngine::ReactionEvent::Pet:
                reaction_ticks_left_ = 24; // 3 visible strokes -> heart pulse
                QueueAmbientEmotion("ambient_idle");
                SetReactionImageLvgl(reaction_icon_, kMochiActionHand,
                                     reaction_base_x_ - 24, 0);
                break;
            case PetStateEngine::ReactionEvent::Play:
                reaction_ticks_left_ = 24; // 2 energetic runs -> happy finish
                reaction_side_ = (mochi_x_ > 20) ? -1 :
                                 ((mochi_x_ < -20) ? 1 :
                                  ((esp_random() & 1u) ? 1 : -1));
                QueueAmbientEmotion(reaction_side_ > 0 ?
                                    "ambient_walk_right" : "ambient_walk_left");
                break;
            case PetStateEngine::ReactionEvent::Sleep:
                reaction_ticks_left_ = 12;
                QueueAmbientEmotion("ambient_lie");
                break;
            case PetStateEngine::ReactionEvent::Wake:
                reaction_ticks_left_ = 14;
                QueueAmbientEmotion("ambient_sit");
                break;
            default:
                CancelPetReactionLvgl();
                break;
        }

        action_ticks_left_ = 0;
        idle_stable_ticks_ = kIdleSettleTicks;
    }

    bool StepFeedReactionLvgl() {
        const int e = reaction_elapsed_ticks_;
        const int mouth_fx_x = reaction_base_x_ + reaction_side_ * 25;
        const bool bite = (e == 3 || e == 7 || e == 11);
        const bool release = (e == 4 || e == 8 || e == 12);

        if (bite) {
            if (emoji_box_ != nullptr) {
                lv_obj_set_style_translate_x(emoji_box_, reaction_base_x_ + reaction_side_ * 7, 0);
                lv_obj_set_style_translate_y(emoji_box_, 52, 0);
            }
            SetReactionImageLvgl(reaction_aux_, kMochiActionSparkle,
                                 mouth_fx_x, 20);
        }

        if (release) {
            RestoreReactionTransformsLvgl();
            HideReactionObjectLvgl(reaction_aux_);
            if (e == 4) SetFeedStageLvgl(1);
            if (e == 8) SetFeedStageLvgl(2);
            if (e == 12) {
                HideReactionObjectLvgl(reaction_icon_);
                QueueAmbientEmotion("ambient_idle");
                ShowHappyHeartLvgl(reaction_base_x_, -3);
            }
        }

        if (e >= 13) {
            const int pulse = (e % 4 == 0 || e % 4 == 1) ? 0 : 3;
            if (emoji_box_ != nullptr)
                lv_obj_set_style_translate_y(emoji_box_, 50 - pulse, 0);
            if (reaction_aux_ != nullptr &&
                !lv_obj_has_flag(reaction_aux_, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_align(reaction_aux_, LV_ALIGN_CENTER,
                             reaction_base_x_, -3 - pulse * 2);
                lv_obj_set_style_opa(reaction_aux_,
                    static_cast<lv_opa_t>(pulse == 0 ? 205 : 255), 0);
            }
        }
        return true;
    }

    bool StepPettingReactionLvgl() {
        const int e = reaction_elapsed_ticks_;
        if (e <= 15) {
            const int phase = (e - 1) % 5;
            if (phase < 4) {
                const int hand_x = reaction_base_x_ - 24 + phase * 12;
                const int hand_y = (phase == 2) ? 3 : 0;
                SetReactionImageLvgl(reaction_icon_, kMochiActionHand,
                                     hand_x, hand_y);
                if (emoji_box_ != nullptr)
                    lv_obj_set_style_translate_y(emoji_box_,
                        (phase == 2 || phase == 3) ? 48 : 50, 0);
            } else {
                HideReactionObjectLvgl(reaction_icon_);
                if (emoji_box_ != nullptr)
                    lv_obj_set_style_translate_y(emoji_box_, 50, 0);
            }
        } else {
            if (e == 16) {
                HideReactionObjectLvgl(reaction_icon_);
                QueueAmbientEmotion("ambient_idle");
                ShowHappyHeartLvgl(reaction_base_x_, -5);
            }
            const int pulse = (e % 4 < 2) ? 0 : 3;
            if (emoji_box_ != nullptr)
                lv_obj_set_style_translate_y(emoji_box_, 50 - pulse, 0);
            if (reaction_aux_ != nullptr &&
                !lv_obj_has_flag(reaction_aux_, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_align(reaction_aux_, LV_ALIGN_CENTER,
                             reaction_base_x_, -5 - pulse * 2);
                lv_obj_set_style_opa(reaction_aux_,
                    static_cast<lv_opa_t>(pulse == 0 ? 205 : 255), 0);
            }
        }
        return true;
    }

    bool StepPlayReactionLvgl() {
        const int e = reaction_elapsed_ticks_;
        if (e <= 16) {
            const int phase = (e - 1) % 8;
            const int hop = phase <= 4 ? phase * 4 : (8 - phase) * 4;
            const int offset = reaction_side_ * hop;
            if (emoji_box_ != nullptr) {
                lv_obj_set_style_translate_x(emoji_box_, reaction_base_x_ + offset, 0);
                lv_obj_set_style_translate_y(emoji_box_,
                    50 - ((phase == 2 || phase == 6) ? 3 : 0), 0);
            }
            if (phase == 0)
                QueueAmbientEmotion(reaction_side_ > 0 ?
                                    "ambient_walk_right" : "ambient_walk_left");
            else if (phase == 4)
                QueueAmbientEmotion(reaction_side_ > 0 ?
                                    "ambient_walk_left" : "ambient_walk_right");
        } else {
            if (e == 17) {
                RestoreReactionTransformsLvgl();
                QueueAmbientEmotion("ambient_idle");
                ShowHappyHeartLvgl(reaction_base_x_, -5);
                SetReactionImageLvgl(reaction_icon_, kMochiActionSparkle,
                                     reaction_base_x_ + reaction_side_ * 25, 5);
            }
            const int pulse = (e % 4 < 2) ? 0 : 3;
            if (emoji_box_ != nullptr)
                lv_obj_set_style_translate_y(emoji_box_, 50 - pulse, 0);
            if (reaction_aux_ != nullptr &&
                !lv_obj_has_flag(reaction_aux_, LV_OBJ_FLAG_HIDDEN))
                lv_obj_align(reaction_aux_, LV_ALIGN_CENTER,
                             reaction_base_x_, -5 - pulse * 2);
        }
        return true;
    }

    bool StepPetReactionLvgl() {
        if (active_reaction_ == PetStateEngine::ReactionEvent::None ||
            reaction_ticks_left_ <= 0) return false;

        ++reaction_elapsed_ticks_;
        if (IsFeedReaction(active_reaction_)) {
            StepFeedReactionLvgl();
        } else {
            switch (active_reaction_) {
                case PetStateEngine::ReactionEvent::Pet:
                    StepPettingReactionLvgl();
                    break;
                case PetStateEngine::ReactionEvent::Play:
                    StepPlayReactionLvgl();
                    break;
                case PetStateEngine::ReactionEvent::Sleep:
                    if (reaction_elapsed_ticks_ == 5)
                        QueueAmbientEmotion("ambient_sleep");
                    break;
                case PetStateEngine::ReactionEvent::Wake:
                    if (reaction_elapsed_ticks_ == 5) {
                        QueueAmbientEmotion("ambient_idle");
                        ShowHappyHeartLvgl(reaction_base_x_, -5);
                    }
                    if (reaction_elapsed_ticks_ >= 6 && reaction_aux_ != nullptr &&
                        !lv_obj_has_flag(reaction_aux_, LV_OBJ_FLAG_HIDDEN)) {
                        const int pulse = (reaction_elapsed_ticks_ % 4 < 2) ? 0 : 3;
                        lv_obj_align(reaction_aux_, LV_ALIGN_CENTER,
                                     reaction_base_x_, -5 - pulse * 2);
                    }
                    break;
                default:
                    break;
            }
        }

        --reaction_ticks_left_;
        if (reaction_ticks_left_ > 0) return true;

        HideReactionIconLvgl();
        RestoreReactionTransformsLvgl();
        active_reaction_ = PetStateEngine::ReactionEvent::None;
        reaction_elapsed_ticks_ = 0;
        reaction_food_index_ = -1;
        action_ticks_left_ = 8;
        idle_stable_ticks_ = 0;
        return false;
    }

'''

display = display[:start] + helpers + display[end:]
DISPLAY.write_text(display, encoding='utf-8')
print('Applied Mochi 15L: staged eating, three-stroke petting, happy finish and polished play reaction')
