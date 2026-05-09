# fische for Windows

A Windows port of [fische](https://github.com/maysl/fische) — a standalone real-time music visualizer with WASAPI loopback audio capture, [Spout](https://spout.zeal.co/) output, GLFW/OpenGL rendering, and a native settings dialog.

---

## Features

- 🎵 **Real-time audio visualization** — reacts to whatever is playing on your speakers; no microphone or virtual cable required
- 🔊 **WASAPI loopback capture** — captures system audio output directly; supports 44.1 kHz, 48 kHz, 96 kHz, and 192 kHz sample rates
- 📡 **Spout output** — share the visualization as a texture with any Spout-compatible app (OBS via Spout2Source, Resolume, MilkDrop, etc.)
- ⚙️ **Settings dialog** — configure audio device, detail level, FPS limit, VSync, Spout and more; all saved to `settings.ini`
- 🖼️ **Fullscreen support** — toggle at any time with `F`
- 🧠 **Nervous mode** — rapid-animation style toggle
- ⏸️ **Pause / unpause**
- 💾 **Vector file persistence** — optionally cache computed vector fields to `vectors/` for faster startup
- 🎒 **Portable executable** — A bootstrapped .exe with settings UI, settings and vector loading/saving

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

1. Copy `fische.exe` and `glfw3.dll` to the same folder.
2. Double-click `fische.exe`.

On first run, `settings.ini` is created next to the executable with default settings. Audio capture starts automatically using the default output device.

---

## Controls

| Key | Action |
|-----|--------|
| `Esc` | Exit |
| `F` | Toggle fullscreen |
| `N` | Toggle nervous mode |
| `O` | Open settings dialog |
| `P` | Pause / unpause |
| `Z` | Toggle Spout output |

---

## Settings

`settings.ini` is created beside the executable on first run:

```ini
[fische]
Audio=
Detail=High
Quality=2
FPSLimit=60
NervousMode=false
UseFilePersistence=false
SpoutSenderEnabled=false
VSync=false
LockAspectRatio=false
WindowWidth=1280
WindowHeight=720
```

| Key | Type | Description |
|-----|------|-------------|
| `Audio` | string | Friendly name of the audio device; empty = system default output |
| `Detail` | Low / Normal / High / Extreme | Visual detail level (also written as `Quality` 0–3) |
| `FPSLimit` | 0–240 | Target frame rate; 0 = unlimited |
| `NervousMode` | true / false | Rapid animation mode |
| `UseFilePersistence` | true / false | Cache vector fields to `vectors/` folder |
| `SpoutSenderEnabled` | true / false | Enable Spout texture sharing |
| `VSync` | true / false | Enable VSync (`glfwSwapInterval`) |
| `LockAspectRatio` | true / false | Letterbox the visualization to 4:3 |
| `WindowWidth` / `WindowHeight` | integer | Last window size; restored on next launch |

---

## Build

See [Build Instructions.md](Build%20Instructions.md).

---

## License

GNU General Public License v2.0 or later.

- Original fische engine: © 2012 Marcel Ebmer, © 2005–2022 Team Kodi
- Spout: © Lynn Jarvis ([@leadedge](https://github.com/leadedge)) — BSD-style
- Windows port and integration: see `LICENSE.md`

---

## Credits

| Project | Author |
|---------|--------|
| [fische](https://github.com/maysl/fische) | Marcel Ebmer (maysl) |
| [visualization.fishbmc](https://github.com/xbmc/visualization.fishbmc) | Team Kodi |
| [GLFW](https://www.glfw.org/) | Camilla Löwy and contributors |
| [Spout](https://spout.zeal.co) | Lynn Jarvis ([leadedge](https://github.com/leadedge)) |
| [BeatDrop loopback capture](https://github.com/OfficialIncubo/BeatDrop-Music-Visualizer/tree/master/audio) | Matthew van Eerde & Incubo_ |
| [fische GLFW project inspiration](https://discord.com/channels/737206408482914387/1500210344621113345) | [proconsule](https://github.com/proconsule) |
| [GLAD](https://glad.dav1d.de/) | David Herberth |
| [GLM](https://github.com/g-truc/glm) | G-Truc Creation |

---

## Related Links

- Original fische website (archived): https://web.archive.org/web/20200801073714/http://26elf.at/musicviz/
- Spout: https://spout.zeal.co/
- WASAPI loopback capture by Matthew van Eerde: https://matthewvaneerde.wordpress.com/2008/12/16/sample-wasapi-loopback-capture-record-what-you-hear/
- BeatDrop Music Visualizer: a custom MilkDrop2 standalone visualization for Windows: https://github.com/OfficialIncubo/BeatDrop-Music-Visualizer/
