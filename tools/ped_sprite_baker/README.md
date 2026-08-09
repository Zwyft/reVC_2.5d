# reVC Ped Sprite Baker Contract

The baker reads a local, user-owned GTA Vice City asset root. Original GTA assets and generated sprite atlases must stay out of git. Configure the asset root with `--asset-root` or `REVC_VC_ASSET_ROOT`; configure generated output with `--output` or `REVC_SPRITE_OUTPUT`. Android personal builds can package owned local assets with `-PrevcVcAssetRoot=/path/to/owned/GTA Vice City` or `REVC_VC_ASSET_ROOT`, and can package generated sprites with `-PrevcSpriteOutput=/path/to/generated/sprites`.

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

Run `revc_ped_sprite_baker --emit-bake-plan --asset-root <owned GTA VC root> --output <generated sprite output>` to validate the owned asset root and write `sprites/peds/bake-plan.txt`. The bake plan lists deterministic target models discovered from built-in VC ped model ids, IDE `peds` sections referenced by `DATA/DEFAULT.DAT`/`DATA/ANIMVIEWER.DAT`, and mission specials listed in `DATA/SPECIAL.TXT`. It is an intermediate input for the real render backend; it is not a placeholder atlas and is not used by runtime rendering.

Run `revc_ped_sprite_baker --validate-rw-assets --asset-root <owned GTA VC root> --output <generated sprite output>` from a repo-root build to validate the real RenderWare input path. This loads discovered ped DFFs from `MODELS/GTA3.IMG` plus `MODELS/GTA3.DIR` when present, loads TXDs from loose `MODELS/*.TXD` or `MODELS/TXD.IMG` plus `MODELS/TXD.DIR`, scans `ANIM/*.IFP`, attaches skinned atomics to discovered HAnim hierarchies, validates renderable geometry, and writes `sprites/peds/backend-report.txt` with deterministic geometry/skeleton stats. It creates the offscreen target descriptor used by the upcoming GL capture path but does not write placeholder atlases.

Running the baker without `--validate-rw-assets` performs the same validation and then requires real captured frame pixels for every discovered model, required state, and direction before writing `sprites/peds/*.png` plus `sprites/peds/manifest.txt`. Atlas generation fails if any model/state/direction frame is missing; it does not create transparent or synthetic substitute frames.

Run `revc_ped_sprite_baker --validate-output --output <generated sprite output>` to verify every model has every required state in all 8 directions. Validation also fails if a manifest-referenced atlas PNG is missing.

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
