from pathlib import Path
import subprocess


def replace_once(path, old, new, label):
    p=Path(path); s=p.read_text(encoding='utf-8'); n=s.count(old)
    if n!=1: raise SystemExit(f'{label}: expected one match, got {n}')
    p.write_text(s.replace(old,new,1),encoding='utf-8')

# Hardware-PASS stack sizes.
replace_once('main/application.cc',
             '}, "main_event_loop", 1024 * 3 + 512, this, 3, &main_event_loop_task_handle_);',
             '}, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);',
             'main_event_loop stack')
p=Path('main/audio/audio_service.cc'); s=p.read_text(encoding='utf-8')
if s.count('}, "audio_input", 1024 * 3, this, 8, &audio_input_task_handle_')!=2: raise SystemExit('audio input anchors')
if s.count('}, "audio_output", 1024 * 3, this, 4, &audio_output_task_handle_);')!=2: raise SystemExit('audio output anchors')
s=s.replace('}, "audio_input", 1024 * 3, this, 8, &audio_input_task_handle_, 0);','}, "audio_input", 2048 * 3, this, 8, &audio_input_task_handle_, 0);',1)
s=s.replace('}, "audio_output", 1024 * 3, this, 4, &audio_output_task_handle_);','}, "audio_output", 2048 * 2, this, 4, &audio_output_task_handle_);',1)
s=s.replace('}, "audio_input", 1024 * 3, this, 8, &audio_input_task_handle_);','}, "audio_input", 2048 * 2, this, 8, &audio_input_task_handle_);',1)
s=s.replace('}, "audio_output", 1024 * 3, this, 4, &audio_output_task_handle_);','}, "audio_output", 2048, this, 4, &audio_output_task_handle_);',1)
old='}, "opus_codec", 1024 * 25, this, 2, &opus_codec_task_handle_);'
if s.count(old)!=1: raise SystemExit('opus anchor')
s=s.replace(old,'}, "opus_codec", 2048 * 13, this, 2, &opus_codec_task_handle_);',1)
p.write_text(s,encoding='utf-8')
replace_once('main/audio/wake_words/afe_wake_word.cc',
             '}, "audio_detection", 1024 * 3, this, 3, nullptr);',
             '}, "audio_detection", 4096, this, 3, nullptr);',
             'AFE stack')

# Hardware-PASS bootstrap behavior.
p=Path('main/application.cc'); s=p.read_text(encoding='utf-8')
for old,new in [
 ('const int MAX_RETRY = 10;','const int MAX_RETRY = 3;'),
 ('int retry_delay = 10; // Initial retry delay is 10 seconds','int retry_delay = 5; // Mochi bootstrap: bounded retry'),
 ('retry_delay = 10; // Reset retry delay time','retry_delay = 5; // Reset bounded bootstrap retry'),
 ('display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);','display->SetStatus(Lang::Strings::LOADING_PROTOCOL);')]:
    if s.count(old)!=1: raise SystemExit(f'bootstrap anchor: {old}')
    s=s.replace(old,new,1)
p.write_text(s,encoding='utf-8')

# Pet lifecycle on the existing Application idle clock.
p=Path('main/application.cc'); s=p.read_text(encoding='utf-8')
old='#include "settings.h"\n#include "ota_server.h"'; new='#include "settings.h"\n#include "pet/pet_state_engine.h"\n#include "ota_server.h"'
if s.count(old)!=1: raise SystemExit('pet include anchor')
s=s.replace(old,new,1)
old='            display->UpdateStatusBar();'
new='''            display->UpdateStatusBar();

            if (device_state_ == kDeviceStateIdle && clock_ticks_ % 5 == 0) {
                PetStateEngine::GetInstance().ServiceIdleLifecycle();
            }'''
if s.count(old)!=1: raise SystemExit('pet lifecycle anchor')
p.write_text(s.replace(old,new,1),encoding='utf-8')

# Reconstruct the hardware-tested baseline, then overlay 15M art.
for cmd in [
 ['python','scripts/patch_mochi_15e.py'],
 ['python','scripts/generate_mochi_hud_assets_15g.py'],
 ['python','scripts/patch_mochi_15g.py'],
 ['python','scripts/generate_mochi_food_assets_15j.py'],
 ['python','scripts/patch_mochi_15j.py'],
 ['python','scripts/patch_mochi_15k.py'],
 ['python','scripts/generate_mochi_action_assets_15m.py'],
 ['python','scripts/patch_mochi_15l.py'],
 ['python','scripts/patch_mochi_15m.py'],
]:
    print('+',' '.join(cmd)); subprocess.run(cmd,check=True)

print('Prepared Mochi 15M from hardware-PASS baseline')
