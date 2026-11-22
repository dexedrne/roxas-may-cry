# Roxas May Cry standalone stage 2

Build an OpenKH mod folder with the player's Steam KH 1.5+2.5 collection. The first battle passed a fresh New Game route on the Linux null port. This build covers part of the campaign.

Install Python 3.11 or newer, NumPy, Pillow with DDS/BC3 encoding, and ImageMagick 7 (`magick` on PATH). From this directory run

```powershell
python -B build.py --packages "D:\SteamLibrary\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\Image\dt" --out rmc-stage2
```

Optionally append `--days-rom "D:\Backups\Days-Europe.nds"` for the exact 21 Neverland textures from a supported European revision 0 cartridge dump. The PC-only build paints original replacement tiles. The builder checks the cartridge and resulting DDS files by SHA-256.

Enable Panacea and the generated folder as a KH2 mod in OpenKH and run **Build and Patch**. The folder includes the public standard rmc-native DLL, its configuration and the three compatibility stubs. It contains the copy manifest, installed assets, output fingerprints, and a list of omitted destinations. Keep the player's generated folder private. Distribute the builder source and recipes.

The local verification used six authenticated KH2 extracts and a retail loose tree. For that layout use `python -B build.py --extracts PATH_TO_CLEAN --loose PATH_TO_RETAIL --out rmc-stage2`. This mode supports partial collection inputs. Use the optional `--records fetched` argument to add the 68 fingerprinted records from `tools/collection/fetch_records.py --image PATH_TO_IMAGE_DT --out fetched` to a diagnostic build. On the local dataset the build emits 2,095 destinations. A full package run additionally requires the cross-title records. Windows and live OpenKH installation still need testing.

The standalone system archive includes the three T100 wording changes and retail-backed names. Other legacy system wording, custom looks, full combat behavior and later progression remain under review.
