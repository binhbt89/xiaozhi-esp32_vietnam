import subprocess

steps = [
    ['python', 'scripts/prepare_mochi_15m.py'],
    ['python', 'scripts/generate_mochi_action_assets_15p.py'],
    ['python', 'scripts/patch_mochi_15p.py'],
]
for cmd in steps:
    print('+', ' '.join(cmd))
    subprocess.run(cmd, check=True)
print('Prepared Mochi 15P: approved 15M art restored, RGB565-quantized, fixed canvas, +300 one-time test coins')
