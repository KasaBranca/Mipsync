# Protected Windows players

PC builds store project runtime data in `<Product>_Data/game.mpak`. Images, models,
audio, scenes, materials and animation controllers are encrypted together with
their filenames and index, using Windows AES-256-GCM. Each build generates a new
key and nonce. The copied player executable contains its matching, masked key.
Files cannot be browsed by opening the package as an archive. Damaged packages
and packages belonging to a different player are rejected before startup.

The player decrypts data into memory. Asset loaders, including audio decoders,
read from that memory; no decrypted assets are extracted to temporary folders.
Mips# scripts are compiled before packaging. The package contains bytecode and
runtime metadata, without the original script source or comments.

Runtime formats under `assets/` and `scripts/` are included, including assets
selected dynamically by scripts. Only scenes selected in Build Settings are
included. Authoring formats such as Blender and Photoshop files, installers,
credentials, editor caches and arbitrary project metadata are excluded. Engine
fonts, icons and license notices remain public runtime resources.

Build with the normal Windows build menu, or:

```powershell
MipsyncEngine.exe --build-pc "C:\path\to\project"
```

Distribute the entire generated folder, including the executable, runtime DLLs
and `_Data` folder. Start the product executable or `Play.bat`. Rebuilding a
recognized older player replaces its entire output, so loose project files from
the previous format do not remain in the new distribution. Failed compilation
or packaging keeps the previously successful build.

This protects against ordinary file browsing and archive extraction. A debugger,
memory dump or analysis of the player executable can still recover runtime data.
It is not DRM and does not change asset licensing or redistribution rights.
