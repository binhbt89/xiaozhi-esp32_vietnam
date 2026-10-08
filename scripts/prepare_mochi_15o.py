import subprocess

steps = [
    ['python', 'scripts/prepare_mochi_15m.py'],
    ['python', 'scripts/generate_mochi_action_assets_15o.py'],
    ['python', 'scripts/patch_mochi_15o.py'],
]
for cmd in steps:
    print('+', ' '.join(cmd))
    subprocess.run(cmd, check=True)
print('Prepared Mochi 15O from 15N placement baseline with fixed-canvas RGB565-friendly action art')
