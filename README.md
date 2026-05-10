# fische for Windows

A Windows port of [fische](https://github.com/maysl/fische) — a real-time standalone music visualizer originally built for Linux. This port brings fische to Windows with native WASAPI loopback audio capture, [Spout](https://spout.zeal.co/) texture sharing for sending the visualization to other apps, OpenGL rendering via GLFW, adjustable audio sensitivity, VSync and FPS control, always-on-top, fullscreen, and a native Win32 settings dialog — all packed into a single portable executable.

---

## Screenshots

![fische visualization](screenshots/fischeWindowsScreenshot1.png)
*fische for Windows — real-time audio visualization*

![fische paused](screenshots/fischeWindowsScreenshot2.png)
*Visual paused — title bar shows "fische - PAUSED"*

![fische help screen](screenshots/fischeWindowsScreenshot3.png)
*F1 help screen with keyboard/mouse shortcuts*

![fische OSD](screenshots/fischeWindowsScreenshot4.png)
*On-screen display for hotkey feedback*

![fische settings dialog](screenshots/fischeWindowsScreenshot5.png)
*Native Win32 settings dialog*

![fische Spout output](screenshots/fischeWindowsScreenshot6.png)
*Spout output received in Spout Receiver at 60 FPS*

---

## Features

- 🎵 **Real-time audio visualization** — reacts to whatever is playing on your speakers, microphone or any virtual cable;
- 🔊 **WASAPI loopback capture** — captures system audio output directly; supports 44.1 kHz, 48 kHz, 96 kHz, and 192 kHz sample rates
- 🎚️ **Adjustable audio sensitivity** — boost or reduce the audio input level with Up/Down keys or the settings dialog; ranges from 0 to 15.
- 📡 **Spout output** — share the visualization as a texture with any Spout-compatible app ([OBS](https://obsproject.com) via [Spout Plug-in](https://github.com/Off-World-Live/obs-spout2-plugin), [SpoutCam](https://github.com/leadedge/SpoutCam), [Resolume](https://www.resolume.com), [NestDrop](https://nestimmersion.ca/nestdrop.php), [TouchDesigner](https://derivative.ca) etc.); sent as a DirectX 11 shared texture via Spout interop
- ⚙️ **Settings dialog** — configure audio device, detail level, FPS limit, VSync, Spout and more; all saved to `settings.ini`
- 🖼️ **Fullscreen support** — toggle at any time with `F` or with `Double Left Click`
- 🧠 **Nervous mode** — rapid-animation style toggle
- ⏸️ **Pause / unpause**
- 💾 **Vector file persistence** — optionally cache computed vector fields to `vectors/` for faster startup
- 🎒 **100% Portable** — A bootstrapped .exe with settings UI, settings and vector loading/saving and lots of functionalities

---

## System Requirements

| Requirement | Minimum |
|-------------|---------|
| OS          | Windows 7 or later |
| GPU         | OpenGL 3.2 Core |
| CPU         | Any 64-bit processor (x64) |
| Audio       | Any WASAPI-compatible output device |
| RAM         | 128 MB |
| Spout       | Optional — [Spout](https://spout.zeal.co/) for sending visual renderer |

---

## Running

1. Copy `fische.exe` to any folder you want.
2. Double-click `fische.exe` to run.

On the first run, `settings.ini` is created next to the executable with default settings. Audio capture starts automatically using the default output device.

---

## Controls

| Key | Action |
|-----|--------|
| `Esc` | Exit |
| `F1` | Show / hide help screen |
| `F` (or `Double Left Click`) | Toggle fullscreen |
| `T` | Toggle always on top |
| `N` | Toggle nervous mode |
| `O` (or `Right Click`) | Open settings dialog |
| `P` | Pause / unpause |
| `Z` | Toggle Spout output |
| `R` | Reset audio sensitivity |
| `Up` | Increase audio sensitivity |
| `Down` | Decrease audio sensitivity |

---

## Settings

`settings.ini` is created beside the executable on first run:

```ini
[fische]
Audio=
AudioSensitivity=1
Detail=High
Quality=2
FPSLimit=60
NervousMode=false
UseFilePersistence=false
SpoutSenderEnabled=false
VSync=false
AlwaysOnTop=false
WindowWidth=1280
WindowHeight=720
```

| Key | Type | Description |
|-----|------|-------------|
| `Audio` | string | Friendly name of the audio device; empty/no name match = system default output |
| `AudioSensitivity` | 0-15 | Multiplier applied to audio input before visualization; 1 = no change |
| `Detail` | Low / Normal / High / Extreme | Visual detail level (also written as `Quality` 0-3) |
| `FPSLimit` | 0-240 | Target frame rate; 0 = unlimited |
| `NervousMode` | true / false | Rapid animation mode |
| `UseFilePersistence` | true / false | Cache vector fields to `vectors/` folder |
| `SpoutSenderEnabled` | true / false | Enable Spout texture sharing |
| `VSync` | true / false | Enable VSync (`glfwSwapInterval`) |
| `AlwaysOnTop` | true / false | Keep the visual window above all other windows |
| `WindowWidth` / `WindowHeight` | integer | Last window size; restored on next launch |

---

## Build

See [Build Instructions.md](Build%20Instructions.md).

---

## License

GNU General Public License v2.0 or later.

- Original fische engine: © 2012 Marcel Ebmer ([@maysl](https://github.com/maysl)), © 2005–2022 Team Kodi
- Spout: © Lynn Jarvis ([@leadedge](https://github.com/leadedge)) — BSD-style
- Windows port and integration: see `LICENSE.md`

---

## Credits

| Project | Author |
|---------|--------|
| [fische](https://github.com/maysl/fische) | Marcel Ebmer ([maysl](https://github.com/maysl)) |
| [visualization.fishbmc](https://github.com/xbmc/visualization.fishbmc) ([alternative (OLD)](https://github.com/maysl/fishbmc)) | Team Kodi |
| [GLFW](https://www.glfw.org/) | Camilla Löwy and contributors |
| [Spout](https://spout.zeal.co) | Lynn Jarvis ([leadedge](https://github.com/leadedge)) |
| [BeatDrop loopback capture](https://github.com/OfficialIncubo/BeatDrop-Music-Visualizer/tree/master/audio) | Matthew van Eerde & Incubo_ |
| [fische GLFW project inspiration/derived from](https://discord.com/channels/737206408482914387/1500210344621113345) | [proconsule](https://github.com/proconsule) |
| [GLAD](https://glad.dav1d.de/) | David Herberth |
| [GLM](https://github.com/g-truc/glm) | G-Truc Creation |

---

## Related Links

- Original fische website (archived): https://web.archive.org/web/20200801073714/http://26elf.at/musicviz/
- Spout: https://spout.zeal.co/
- WASAPI loopback capture by Matthew van Eerde: https://matthewvaneerde.wordpress.com/2008/12/16/sample-wasapi-loopback-capture-record-what-you-hear/
- BeatDrop Music Visualizer: a custom MilkDrop2 standalone visualization for Windows: https://github.com/OfficialIncubo/BeatDrop-Music-Visualizer/
