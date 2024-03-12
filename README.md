This DLL is a plugin for Unity 2022.3.6f1 that forces Mesh R/W off during AssetBundle build. This can save some assetbundle size (depending on the various Mesh R/W settings across your scene's meshes) if you never need to modify meshes from script.

Usage:
Compile, then place the resulting DLL in /Assets/Editor/Plugins in your project (creating folders that don't exist along the way). On the Inspector page for the plugin, tick the 'Load on startup' checkbox.

