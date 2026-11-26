# Releasing Zetta 3D for Unreal Engine

For maintainers. A release is a version number, a zip per engine version and platform, and a GitHub release (and,
when it's listed there, a Fab update).

## 1. Before you start

- `CHANGELOG.md` has the new version's notes at the top.
- `VersionName` in `Zetta3D.uplugin` is the new version, and `Version` (an integer) is one higher than the last release.
- The backend the release talks to is live: production has the Unreal agent's three tables, `UNREAL_AGENT_ENABLED=true`
  and `UNITY_BYOK_ENABLED=true` (own API keys, shared by every agent), and the frontend's sign-in page knows `unreal`.

## 2. Build the zips

One zip per engine version you support, built on each platform you ship:

```bash
Scripts/package.sh "/Users/Shared/Epic Games/UE_5.8"          # on a Mac: a universal (Apple Silicon + Intel) build
Scripts/package.sh "C:/Program Files/Epic Games/UE_5.8"       # on Windows, from Git Bash
```

Each lands in `dist/` as `Zetta3D-<version>-UE<engine>-<platform>.zip`. The script stamps the engine version into the
packaged `.uplugin`, so the editor loads the binaries instead of asking to rebuild them. Nothing is uploaded.

Check each zip once before publishing: unzip it into a fresh project's `Plugins/` folder, open the project, sign in,
import a model, and send the agent a message.

## 3. Tag and publish

Commit, tag `v<version>` and push, then create the GitHub release for the tag with the CHANGELOG notes and attach the
zips.

## Fab

Code plugins on Fab are submitted as source; Epic builds the binaries for every engine version and platform the listing
supports, so Windows and older engines don't need machines of your own. Submit the packaged folder's contents
(`Zetta3D.uplugin`, `Source/`, `Resources/`, `Config/`, the docs) without `Binaries/` or `Intermediate/`, and keep
`EngineVersion` out of the `.uplugin` you submit (Fab sets it per build).
