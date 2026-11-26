# Contributing

Thanks for helping make Zetta 3D for Unreal Engine better.

## Issues

- **Found a bug?** Open an issue with your plugin version (Edit › Plugins › Zetta 3D), your engine version and
  system, and the steps that show it. The Output Log (filter on `LogZetta`) often has the message that explains it.
- **An idea?** Open an issue and describe what you'd like to do.
- **A security problem?** Please don't open an issue — see [SECURITY.md](SECURITY.md).
- **Your account, credits or a generation?** Those are on zetta3d.ai: [support](https://zetta3d.ai/support).

## Pull requests

Open an issue first for anything larger than a fix, so we can agree on the approach. Keep pull requests focused, and
make sure the plugin builds without warnings on the newest engine version we support.

## How the code is laid out

| Folder | What lives there |
|---|---|
| `Source/Zetta3DEditor/Public` | The core: settings, the key store, the HTTP client, sign-in, typed API calls, the request builder, jobs, the importer, style and shared widgets |
| `Source/Zetta3DEditor/Private/Studio` | The 3D Studio page and its tools |
| `Source/Zetta3DEditor/Private/Library` | My Generations |
| `Source/Zetta3DEditor/Private/Pipelines` | 3D Pipelines: the node canvas, runs, and the service the agent uses |
| `Source/Zetta3DAgent/Private` | The Zetta Agent: its runtime, chat, approval and changes cards, memory and own-key settings |
| `Source/Zetta3DAgent/Private/Tools` | The agent's Unreal tools (level, Blueprints, materials, files, capture, Python) and the editor snapshot |

Conventions: `SZetta…` widgets, `FZetta…` classes, `UZetta…` objects; callbacks run on the game thread and never
block it on the network; every change to a level or an asset is an `FScopedTransaction`; comments explain *why*.
Never name a 3D model vendor in code or text — our models are Zetta N1, N2.5, Np1, Np1.5 and R1.

## Building

Open a C++ project with the plugin in its `Plugins` folder, or package it:

```bash
Scripts/package.sh "/Users/Shared/Epic Games/UE_5.8"
```
