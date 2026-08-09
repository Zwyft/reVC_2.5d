# reVC Ped Sprite Baker Contract

The baker reads a local, user-owned GTA Vice City asset root. Original GTA assets and generated sprite atlases must stay out of git. Configure the asset root with `--asset-root` or `REVC_VC_ASSET_ROOT`; configure generated output with `--output` or `REVC_SPRITE_OUTPUT`. Android packaging can also use `-PrevcSpriteOutput=/path/to/generated/sprites`.

Required input files:

- `MODELS/GTA3.IMG`
- `MODELS/TXD.IMG`
- `MODELS/*.TXD`
- `DATA/DEFAULT.DAT`
- `DATA/ANIMVIEWER.DAT`
- `DATA/SPECIAL.TXT`
- `ANIM/*.IFP`

Generated output root layout:

```text
sprites/peds/manifest.txt
sprites/peds/*.png
```

Manifest format is line-oriented and deterministic. Paths are relative to the runtime game root and must not contain spaces.

```text
model <model_id> <model_name>
atlas <atlas_name> <png_path> <width> <height>
frame <model_id> <state> <direction> <frame_index> <duration_ms> <atlas_name> <x> <y> <w> <h> <pivot_x> <pivot_y> <world_height>
```

Run `revc_ped_sprite_baker --validate-output --output <generated sprite output>` to verify every model has every required state in all 8 directions.

Runtime states expected by `CPedSpriteAnimResolver`:

- `idle`
- `walk`
- `run`
- `sprint`
- `crouch`
- `attack`
- `firearm`
- `hit`
- `death`
- `car_sit`
- `bike_ride`
- `enter_exit`

Directions are 0 through 7, derived from ped heading relative to camera heading. Missing manifest data or atlas files are runtime errors; the renderer does not generate placeholder sprites or fall back to 3D ped rendering when `PED_2_5D_SPRITES` is enabled.
