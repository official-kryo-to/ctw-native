# Third-person character assets

Enhancements can replace CTW's overhead people sprites with animated, textured LCS characters while its
third-person camera is active. The Porter imports the resources locally from a user-supplied **Liberty City
Stories Android 2.4.379 APK**. No donor artwork, animation, lookup tables or executable code is bundled with
this repository or the downloadable Porter. The APK is read as an archive; its code is never run.

## Using it

1. Choose your CTW Android 4.4.243 APK in the Porter and enable mods.
2. Select **Enhancements** in the premade-mod list.
3. Accept its optional 3D-character offer and choose your LCS Android **2.4.379** APK. The required version
   is stated on screen; the importer checks the manifest and archive automatically.
4. Port the game, then press **V** to switch to third person. F4 exposes the camera and 3D-character toggles.

You can skip the extra APK. Third person and lighting still work with CTW's sprites. Switching to the original
camera, disabling the character option/mod, or requesting an unsupported pose restores the original sprites.

For development, put the donor at `input/lcs/GTA-LCS-2.4.379.apk` and use the normal root CMake configure/build
commands. They prepare the local Enhancements asset folder automatically, reuse a successful import and repair
missing imported files. `CTW_LCS_APK` can select another path; `CTW_IMPORT_LCS=OFF` stops this build step.
It is disabled for `CTW_RELEASE=ON`, so distributed builds still require an explicit user-side import.

The imported roster is Toni, one cop, six male civilians and six female civilians. These are LCS appearances;
Toni is not Huang, and CTW's original palettes and mission-specific likenesses are not reconstructed.
Only their required textures are extracted, together with the main pedestrian bank and all four bike banks
(scooter, chopper, dirt bike and sport bike). Existing development imports refresh when the importer changes.
The local pack lives in `mods/Enhancements/lcs/`; do not redistribute it. User imports and settings are excluded
when building the Porter and standalone exports.

## Renderer and SDK

`port/src/gfx/character.*` reads the mobile Leeds RSL packed DFF skeleton/geometry, ANPK animation banks and
ETC1 PVR textures. It validates chunk bounds, hierarchy, joint indices, vertex weights and animation keys.
Skinning, animation interpolation and texture loading belong to the engine; the engine does not select
pedestrians, camera modes or gameplay animations. This is not an FBX/glTF importer.

The append-only **SDK v7/v8/v9** provides:

- `load_character_model(dff, ifp, textures)` and `release_character_model(handle)` for mod-owned resources.
- `on_character_render(mod, callback, user)` for interpolated player/pedestrian poses at their world draw.
- `draw_character_model(handle, pose, scale, animation, phase, loop)` to draw a clip at normalized phase.
- v8 `get_character_bounds(handle, animation, seconds, bounds)` for skinned dimensions and clip duration.
- v8 `draw_character_model_at_time(handle, pose, scale, animation, seconds, loop, ground_pose)` for absolute
  clip time and optional body-to-ground contact. Leave grounding off for walking/jumping and vehicle attachments.
- v9 `draw_character_model_attached(handle, pose, scale, animation, phase, loop, bone_id)` for the native
  vehicle frame. A joint id anchors that animated joint; `-1` anchors the rest floor while retaining animated height.

Check `CTW_MOD_HAS` before using these functions. Handles are session-local, zero is invalid, and released
handles are not reused. Loading and release require the game thread and GL context. Return zero from the
render callback to retain CTW's sprites; disabled mods receive no character callbacks. The pose supplies
identity, gender/body set, heading, position, actual upper/leg animation IDs and phases, movement speed and
attachment/riding/death flags. The v8 pose tail supplies `one_shot_seconds`, interpolated from the simulation
clock, even after a CTW single-frame sprite finishes. Check the pose's `size` before reading the tail. The original
phase drawing API accepts the v7 pose prefix. Rendering does not advance AI, collision, simulation or random numbers.
The v9 tail provides the attachment basis/anchor and owning vehicle, plus continuous reaction/recovery clocks.
Check `size` and `CTW_CHARACTER_ATTACHMENT_FRAME` before using the frame. Bike frames include native lean,
pitch and sway; entry/exit frames follow the physical vehicle orientation. The engine validates finite,
orthonormal frames and joint ids. These interfaces leave donor fitting and animation choices to the mod.

`examples/enhancements/lcs_characters.c` owns the roster and clip mapping. It covers idle, walk, run, sprint,
knockdown, roll, car entry/exit and bike mount/riding/dismount. Models follow the existing interpolated game
positions and world lighting. The original renderer remains the fallback for unsupported states.

## Evidence and remaining limits

The supplied 2.4.379 APK was inspected locally. Python and Rust produced the same 95 selected files;
all 14 models loaded through the game renderer. Live third-person checks showed Toni and male/female/cop pedestrians
textured and animated, plus bike mount, riding and dismount. The donor unit conversion fits Toni's idle-body
width to a 1.25-unit shoulder width instead of the earlier guessed 1.15 scale, preserving natural
proportion differences throughout the roster. This is a mod fitting choice, not an original collision-box measurement:
CTW's dynamic body sphere has radius 1.25. The rider's animated pelvis follows the native upper rider anchor,
without a global Z offset. Chopper/dirt/sport/scooter bank selection is also mod policy; hands and feet still need fitting.
Live vehicle-contact checks showed fatal bodies retaining contact momentum, sliding and stopping on the road.
Both CTW body sets have one frame in animation 0x18, whereas the imported
`KO_skid_back` clip lasts 0.8 seconds; the fall now uses elapsed simulation time rather than that sprite phase.
The game now applies the original dynamic-sphere radius, velocity impulse, run-over damage gates and fatal-task
ordering, plus impact/fallen drag and living recovery states. Fatal hits queue the default fallen task before the
impact response; medium run-over effects in this Android build raise position without replacing contact velocity.
Synthetic tests cover grounded body integration and recovery blocked by an overhead vehicle. Full cache ordering,
AI collision flags, pinned-body crush damage, sprite height interpolation and other combat reactions remain incomplete.
Synthetic tests cover model truncation, hierarchy/skinning, animation interpolation, multiple clip banks,
APK identity/version/bounds and SDK forwarding/legacy-host compatibility.

This is an optional adaptation, not a 1:1 CTW character port. LCS motion and proportions differ: clip selection,
unit conversion and animated-joint fitting are mod policy and need further visual fitting, especially bike hands/feet
and car-door transitions. CTW combat, mission and other unported states do not gain support from importing
more clips. A low camera also still exposes overhead particle/foliage artwork; smoke-card edges need a separate
blend/alpha/order investigation. The waterfront clipping and hidden-player visibility aid remain unresolved.

For an independently distributable character pack, original or appropriately licensed low-poly rigged art is
still the practical route. Image generation can help with concepts/textures or directional sprite artwork;
it does not produce a rigged mesh and animation pipeline. Owning a donor APK does not itself grant permission
to redistribute its resources.
