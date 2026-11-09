# WA2 Dream Communication for Windows

A Windows port of **WHITE ALBUM2 ～ドリームコミュニケーション～** (version 1.0.7), built from the Android
XAPK. The game runs on the same engine it shipped with, Unity 3.5.6f4, with its original data, scenes and
AssetBundles. Only what's needed to run on a PC changes.

The easy way to get it is the patcher on the **Releases** page: download it, point it at your XAPK, and
it builds the complete game. The rest of this page explains what the patcher does and how the build works.

## How the port works

The Android game is a Unity 3.5 Mono title. Its C# code is plain .NET and it has no native game code, so
it can run on the desktop player of the same Unity version. The port is a **player swap**:

1. **Stock player.** A Unity 3.5.6f4 Windows player is built from an empty project. It provides
   `WA2DC.exe`, the Mono runtime and the engine resources.
2. **Game data.** The game's own `Data` folder (scenes, `sharedassets`, resource files) and the 255
   AssetBundles from the OBB are placed next to it. The bundles go in `StreamingAssets`, where the game's
   desktop code path already looks for them.
3. **Retarget.** Every serialized file and every bundle records the platform it was built for. Each one
   is retagged from Android (13) to Windows standalone (5), so the PC player accepts it.
4. **Data fixes.** A few assets are stored in formats the PC player reads differently:
   - **16-bit alpha textures** are stored with the alpha nibble in the opposite position. They're
     reordered so transparent pixels no longer show as yellow squares. This covers the base Data,
     hash-named serialized files and bundles (the PV stage's yellow snowflake card was in a
     hash-named file, not in `sharedassets`).
   - **ETC1 textures** (the loading-fade `black`/`white` and one face bundle) are converted to RGB24,
     because the PC player can't decode ETC1.
   - **Company and product name** in `PlayerSettings` are corrected, so Unity's asset cache resolves to a
     valid path.
5. **Code.** Both game assemblies (`Assembly-CSharp` and the EZGUI `firstpass`) are decompiled,
   patched and recompiled against the stock player's references:
   - **boot:** the save folder is created next to the game;
   - **input:** the EZGUI menus use the mouse instead of touch;
   - **Android stubs:** the billing and OBB-download plugins are replaced with stubs;
   - **owned DLC:** the paid DLC items are marked as owned in the game's own store format;
   - **side strips:** drawn at exact screen pixels, so they stay sharp and the right way round at every
     window size. A high-quality filter handles resizing, a larger `-Background` art is used
     automatically, and a proper black loading fade is shown.
   - **save slots:** the save folder can be a slot under `Documents\Saves\`, chosen by the
     `WA2DC_SAVE_SLOT` environment variable or `-save-slot`. Without one, it's `Documents\` as before.
6. **Saves tab.** Unity's configuration dialog is native code inside the player, so it isn't edited.
   Instead, a small `version.dll` next to `WA2DC.exe` is loaded with the game: it forwards every function
   to the system `version.dll`, and also hooks the dialog when it opens. It replaces the **Input** tab
   (key remapping the game never reads) with a **Saves** tab:
   - one row per save, with its WP left and last-played time, plus a **New save** row;
   - WP is computed the way the game does: the progress WP in `save.bin` plus the store total from
     `share.bin`, both decoded from the game's own obfuscated formats;
   - on **Play!** it sets `WA2DC_SAVE_SLOT` for the chosen save, then Unity starts as usual;
   - it only reads save files, and new saves are created by the game itself.
7. **Launchers.** `WA2DC.bat` opens the default 1024×768 window. `WA2DC-1080p.bat` switches to
   1920×1080 fullscreen.

## The patcher and its data

`WA2DC Patcher.exe` is a standalone Windows installer. It needs no Python, Unity or compiler, and the
patch data is embedded inside it. Everything else is read straight out of your XAPK, from the APK and
the OBB nested inside it.

To install on another PC you only need the exe and the XAPK:
- 64-bit Windows 8 or newer;
- a graphics driver with OpenGL;
- no .NET, Visual C++ or DirectX packages: the patcher, the game and Mono use only standard Windows DLLs.

| Record | What it is | Where the bytes come from |
|---|---|---|
| shipped | the stock Unity player, `version.dll` (the Saves tab), the two launchers, the strip art | the exe's embedded data (LZMS-compressed) |
| from XAPK | unchanged game files | copied from your XAPK, CRC32-checked |
| delta | retagged/fixed data files and the two recompiled assemblies | your XAPK's original plus a Windows `msdelta` delta |
| bundle delta | the 255 AssetBundles | your XAPK's bundle, unpacked, patched, then re-encoded |
| asset | the patcher's own art, icon and version info | the exe's embedded data |

**Bundles** are `UnityWeb` files: a header followed by one LZMA stream. Retagging a bundle changes only a
handful of bytes inside the decompressed payload, but every byte of the compressed stream. So the patcher
works on the decompressed payload:
1. it unpacks your bundle;
2. it applies a delta of a few hundred bytes;
3. it re-encodes the payload with the exact LZMA settings the build used (raw LZMA1, 8 MiB dictionary,
   lc=3 lp=0 pb=2, BT4 match finder, nice_len 123, single-threaded).

That keeps the data file small, about 9.6 MB for a 241 MB game.

**Every step is checked:**
- each file read from the XAPK is checked against its recorded CRC32;
- each delta's result is checked against the target CRC32;
- each re-encoded bundle is checked as a whole file.

A damaged or different XAPK stops the install and names the entry that failed. The install is staged in a
temporary folder and only moved into place once everything has passed, so cancelling leaves nothing
behind. The finished folder is byte-for-byte identical to the build the patcher was made from.

## Building from source

Requirements:
- Windows 10 x64;
- Unity 3.5.6f4 (its editor builds the stock player);
- the .NET Framework 4 `csc`;
- Python with UnityPy 1.25;
- AssetRipper 2.0 (exports the game code for patching);
- Visual Studio Build Tools with the x86 C++ tools (for `version.dll`) and, for the patcher, the x64
  C++ tools, plus a Windows SDK.

Build the game from the XAPK in one command:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Xapk "C:\path\to\game.xapk" `
    -Background "C:\path\to\strip-art.png" -Output "build\WA2DC"
```

The script unpacks the XAPK, builds the stock player, assembles and retargets the data, applies the data
fixes, exports, patches and recompiles both assemblies, installs the launchers, and verifies the result.
It replaces the output folder only after the checks pass. The tested XAPK has SHA-256
`50207CCF544A4AF891F94B06676EE99D5681BCF9AEDC753F2EDA31C848420AF2`; pass it as `-ExpectedXapkSha256` to
reject any other copy.

Package that build as the patcher:

```powershell
python -I scripts\build_patcher.py --build "build\WA2DC" --xapk "C:\path\to\game.xapk" `
    --out "dist\WA2DC Patcher"
```

To check a build:
- `scripts\verify_build.py` checks the data invariants;
- `scripts\sweep_argb4444.py` fails if any 16-bit texture still has the Android nibble order;
- `scripts\compare_builds.py` compares two builds file by file;
- `scripts\playtest.ps1` drives the game through its main screens and takes screenshots;
- `scripts\verify_raw_bundles.py` proves that the LZMA re-encode reproduces every bundle.

## Repository layout

| Folder | Contents |
|---|---|
| `scripts/` | the build (`build_all.ps1` and the steps it runs), the patcher builder, and the checks |
| `patches/` | C# files and launchers the build adds to the game: PC stubs, side-strip art, input, launchers |
| `patcher/` | the patcher's C++ source, with vendored miniz (zip reading) and xz liblzma (bundle re-encoding) |
| `dialoghook/` | the `version.dll` that turns the configuration dialog's Input tab into the Saves tab |

## Playing

- Start with `WA2DC.bat` (1024×768 window) or `WA2DC-1080p.bat` (1920×1080 fullscreen).
- Unity's configuration dialog appears first. Its **Saves** tab lists your existing saves with their
  **WP left** and last-played time, plus a **New save** row; pick one, then click **Play!**.
- **Alt+Enter** toggles fullscreen.
- Saves are in `Documents\` inside the game folder: `save.bin` holds progress, `share.bin` holds store and
  DLC state. A new save goes to `Documents\Saves\Save N\`. Save 1 is the original `Documents\` save.
- The store's real-money purchases are offline. The owned DLC is already unlocked, and gallery pages
  unlock with WP as in the original.
- The window can't be resized while the game runs; use Alt+Enter or the 1080p launcher.
