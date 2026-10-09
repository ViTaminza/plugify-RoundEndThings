# RoundEndThings (Plugify)

Round-end bhop/speed/gravity, ported from the Metamod plugin RoundEndThings (ABKAM). On round_end it enables sv_autobunnyhopping/sv_enablebunnyhopping and writes m_flVelocityModifier/m_flGravityScale/m_flActualGravityScale of every alive player; round_start restores the convars and the fields to 1.0. Players opt out per feature in the 'mm_ret' / '!ret' menu ('Настройки конца раунда'); the two choices persist in the permissions cookies 'roundend_disable_speed' and 'roundend_disable_gravity' (the names of the Metamod original, which used the cookies plugin). Settings: addons/configs/RoundEndThings/settings.ini. No exported methods.

Plugify port (fork) of **RoundEndThings** by ABKAM (Metamod (C++)).
Original: https://github.com/ABKAM2023/RoundEndThings
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, permissions (optional), hudmenu (optional)

## Installation
Download `RoundEndThings-1.0.1.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/RoundEndThings`.

## License
GPL-3.0, see LICENSE.

