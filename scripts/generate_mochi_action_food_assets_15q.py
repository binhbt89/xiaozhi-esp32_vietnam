from pathlib import Path
import base64, zlib

OUT = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_action_food_assets_15q.h')

# 15Q clean food sprites reconstructed from the approved full artwork sheet.
# Every frame is a complete standalone 68x54 RGBA canvas with identical anchor.
# Visible pixels are hard-alpha and RGB565-snapped; no runtime scaling or cropping.
DATA = {
    'fish0': 'eNrtWt1u2zgQfSFKgTqO4saOIwSTdoM2aeP0C/xoj1qSNg00RQok+fY9kq1YW64uQ7pkS/BSEsXDM5xzZJH7i/NTKfpng39OB9ynYvvBvwdt/3PBMVl5WZzXb9zWf8XxOsCH3cOhsaH7u/J4WL4HA2dx+MDvHxYoavd/1j1g5V3pYqmcC8XcyQL6zFZK9fblVP1LKNx4ZSn14c0wXj1OPtXyt3Ko0ieINp1g1U2uV5M7CRdKo9T9HuCxrxmqXe1Ouf9Oez1Y0R+/iw/W34p3cYsp+JfgU1xLHdYFOHywva74pbVdcVpXQW5X3DvwU4C1VXnZaqC1PskhUJ8xS4LZCdd2Y8fEnYQbGl+M41pToEG5ZoyvOdKrnlMhu38X+K7fUf5Wpx33LSqImVrJ5vY+/viT4FxekTdDP5s+/7kJp3/8m/9px0c+Vr4VXlRRQhfOX2rZnx0vQrsYx3+8trmDYGdT7+G8xKqUNcP0yYpTdEdhsM2Puhth8sPvxOdR2dsmvVZxvf96YcEXNcxwP2CwuS2wdj6cZ4bO3fNYErsanXhyVN8LYpyKoFpmCxNn1/YlMX0H67KjP9zj0Afn80I6jfs3XxxNZbG8avls3W6p8K2h/Kt+PXl1JvC61EvV6Y7j5QOXxDKuY2GOKTL/Uj+b//V57yhQZ89Xx7xgqcVxwklCXjaMoIl1mbOi0TCbeGbJRgImUVEXWbN82MlrwLLN4e3J5mPXNlc7u2r0m+ex3pJb5iC/XefD+cDd/yK1Y8F2e3/vVj+X7tu1Z7Yk/TG3bX7t+Zp8XldZq9UaSsLX9FbXd4Oquoy8xv5r+yvlbaSXr5WlPOZ4/y/PXPwF41WrY=...