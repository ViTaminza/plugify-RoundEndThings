# RoundEndThings (Plugify)

Round-end bhop/speed/gravity, ported from the Metamod plugin RoundEndThings (ABKAM). On round_end it enables sv_autobunnyhopping/sv_enablebunnyhopping and writes m_flVelocityModifier/m_flGravityScale/m_flActualGravityScale of every alive pawn; round_start restores the convars and the fields to 1.0. Players opt out per feature in the 'mm_ret' (console) / '!ret' (chat) menu; the two choices persist in the official permissions cookies 'roundend_disable_speed'/'roundend_disable_gravity' (the names of the Metamod original). Settings: configs/RoundEndThings/settings.ini (the production KeyValues file, unchanged). Pure s2sdk: no pisexcompat, optional hudmenu/permissions APIs only, no exported methods.

Plugify port (fork) of **RoundEndThings** by ABKAM (Metamod (C++)).
Original: https://github.com/ABKAM2023/RoundEndThings
The code was rewritten for Plugify / s2sdk.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugin version: 1.1.0
- Plugify dependencies: s2sdk (>=2.18.2), permissions (>=1.3.1, optional), hudmenu (>=1.0.0, optional)
- Self-contained s2sdk integration: uses s2sdk directly, without the pisexcompat library.

## Installation
Download `RoundEndThings-1.1.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/RoundEndThings`.

## License
GPL-3.0, see LICENSE.

