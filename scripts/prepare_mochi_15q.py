import subprocess

steps = [
    ['python', 'scripts/prepare_mochi_15p.py'],
    ['python', 'scripts/generate_mochi_action_food_assets_15q_fixed.py'],
    ['python', 'scripts/patch_mochi_15q.py'],
]
for cmd in steps:
    print('+', ' '.join(cmd))
    subprocess.run(cmd, check=True)
print('Prepared Mochi 15Q: approved 15P reactions + clean uncropped fixed-canvas food sprites')
