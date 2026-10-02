# Source SDK 2013

Source code for Source SDK 2013.

Contains the game code for Half-Life 2, HL2: DM and TF2.

**Now including Team Fortress 2! ✨**

## Build instructions

Clone the repository using the following command:

`git clone https://github.com/ValveSoftware/source-sdk-2013`

### Windows

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - Visual Studio 2022 with the following workload and components:
   - Desktop development with C++:
     - MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)
     - Windows 11 SDK (10.0.22621.0) or Windows 10 SDK (10.0.19041.1)
 - Python 3.13 or later

Inside the cloned directory, navigate to `src`, run:
```bat
createallprojects.bat
```
This will generate the Visual Studio project `everything.sln` which will be used to build your mod. The generated game projects are the Episodic ones (`client_win64_episodic`, `server_win64_episodic`, `launcher_main_win64_episodic`); to target TF2 or HL2: DM instead, replace `/episodic` in `createallprojects.bat` (and `buildallprojects`) with `/tf` and/or `/hl2mp`.

Then, on the menu bar, go to `Build > Build Solution`, and wait for everything to build.

You can then select the `Client (Mod Name)` project you wish to run, right click and select `Set as Startup Project` and hit the big green `> Local Windows Debugger` button on the tool bar in order to launch your mod.

The default launch options should be already filled in for the `Release` configuration.

### Episodic mod (Windows x64)

`game\mod_episodic` runs Episode One/Two game code on the x64 Source SDK Base 2013 Multiplayer engine. Its `gameinfo.txt` mounts content from three Steam installs, which must all be present:
 - Source SDK Base 2013 Multiplayer (243750): the engine and its `hl2` content, whose shaders match the engine.
 - Source SDK Base 2013 Singleplayer (243730): the Episode One/Two VPKs this SDK's game code and UI were made for.
 - Half-Life 2 (220): only for the loose episode maps. This engine does not mount the embedded pakfile (patched water materials, cubemaps) of a map read from a VPK.

The client sets `cl_localnetworkbackdoor 0` on x64 because the engine crashes with the singleplayer network backdoor ([#610](https://github.com/ValveSoftware/source-sdk-2013/issues/610)).

The DX12 renderer needs the native shader pack published into the mod once (and after shader changes):
```bat
cd src
devtools\bin\buildshaders_dx12.bat -game ..\game\mod_episodic
```
Without it every DX12 draw fails and the screen stays black. Launch with `game\mod_episodic_win64.exe`, or `game\start_ep2.bat` (extra arguments such as `-dx9` or `+map ep2_outland_01` are passed through).

### DX12 Unicode title DLL (Windows x64)

The `unicode` project builds a source-compatible replacement for the installed x64 `unicode.dll`. It exports the same `CreateInterface` factory and `VENGINEUNICODE001` methods; unless `-dx9` (or `-gl`/`-vulkan`/`-noshaderapi`) is passed, an x64 launch changes an incoming `Direct3D 9` window-title substring to `Direct3D 12`. Do not overwrite the Steam SDK installation. Generate and build the isolated project from `src`:

```bat
cd /d F:\repo\source-sdk-2013\src
devtools\bin\vpc.exe /hl2mp /win64 /define:SOURCESDK +unicode /mksln unicode
msbuild unicode.sln /m /p:Configuration=Release /p:Platform=win64 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.26100.0
```

The VPC post-build step publishes the build artifact directly to the mod launcher's effective lookup path, `game\bin\x64\unicode.dll`. Launch from the `game` directory so the Source loader resolves this artifact before the SDK installation's PATH fallback; do not overwrite the Steam SDK installation.

Run the x64 launcher from `game` and inspect `MainWindowTitle` while each process is running:

```bat
cd /d F:\repo\source-sdk-2013\game
mod_hl2mp_win64.exe -game mod_hl2mp -dx9 -windowed -novid
mod_hl2mp_win64.exe -game mod_hl2mp -windowed -novid
```

Expected result: the `-dx9` launch retains `My First HL2MP Mod - Direct3D 9 - 64 Bit`, while the default launch (native DX12) displays `My First HL2MP Mod - Direct3D 12 - 64 Bit`. Both launches should load `F:\repo\source-sdk-2013\game\bin\x64\unicode.dll`; the read-only Steam SDK `bin\x64\unicode.dll` remains untouched.

### Linux

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - podman

Inside the cloned directory, navigate to `src`, run:
```bash
./buildallprojects
```

This will build all the projects related to the SDK and your mods automatically against the Steam Runtime.

You can then, in the root of the cloned directory, you can navigate to `game` and run your mod by launching the build launcher for your mod project, eg:
```bash
./mod_episodic
```

*Mods that are distributed on Steam MUST be built against the Steam Runtime, which the above steps will automatically do for you.*

## Distributing your Mod

There is guidance on distributing your mod both on and off Steam available at the following link:

https://partner.steamgames.com/doc/sdk/uploading/distributing_source_engine

## Additional Resources

- [Valve Developer Wiki](https://developer.valvesoftware.com/wiki/Source_SDK_2013)

## License

The SDK is licensed to users on a non-commercial basis under the [SOURCE 1 SDK LICENSE](LICENSE), which is contained in the [LICENSE](LICENSE) file in the root of the repository.

For more information, see [Distributing your Mod](#markdown-header-distributing-your-mod).
