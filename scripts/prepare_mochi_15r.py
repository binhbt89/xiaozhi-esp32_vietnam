import subprocess

steps = [
    ['python', 'scripts/prepare_mochi_15q.py'],
    ['python', 'scripts/generate_mochi_action_food_assets_15r.py'],
]
for cmd in steps:
    print('+', ' '.join(cmd))
    subprocess.run(cmd, check=True)
print('Prepared Mochi 15R: 15Q clean placement plus 9 full-stage sprites with locked vessels')
