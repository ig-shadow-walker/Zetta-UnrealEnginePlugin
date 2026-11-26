# Security

## Reporting a vulnerability

Please don't open a public issue for a security problem. Report it privately instead:

- on GitHub: **Security › Report a vulnerability** on this repository, or
- by email: hello@zetta3d.ai, with "Security" in the subject.

Say what you found, how to reproduce it, and which version of the plugin and of Unreal Engine you used. We'll answer
within a few working days and tell you when it's fixed. Please give us a reasonable time to release a fix before
you talk about it publicly.

Only the latest release is supported.

## What the plugin does with your data

- **Your sign-in.** You sign in on zetta3d.ai in your browser; the editor never sees your password. While you sign
  in, the plugin listens on `127.0.0.1` (your own computer only) for the page to hand the session back, and stops
  listening as soon as it has or after ten minutes. The session is kept in your system's key store — macOS Keychain,
  Windows Credential Manager, or a file only you can read on Linux — never in the project, so it doesn't travel with
  it.
- **Your own API keys** (for the Zetta Agent) are kept in the same key store, never in the editor's settings files or
  your project. They are sent only with your agent's requests to zetta3d.ai, which passes them to the provider you
  chose and does not store them.
- **Where it connects.** The plugin talks only to zetta3d.ai (`https://api.zetta3d.ai`) over HTTPS, and to the
  signed storage links the API gives it to download your models and pictures.
- **What the Zetta Agent sends.** With each message: what you wrote and attached, and a summary of where you are in
  the editor — the engine version, the project's name, modules and enabled plugins, rendering and input settings, the
  open level's path and an outline of its actors, what's selected, recent errors from the Output Log and the last
  compile. When it captures the viewport, that picture is sent too, and so is what its tools read in your project.
  Its file tools only reach your project folder (and files you attach) — never the rest of your computer, and never
  the plugin's own folder.
- **Python.** The agent's `run_python` tool runs editor Python only after you approve it (unless you chose Full
  autonomy), and the plugin refuses code that starts processes, opens network connections, loads native code or
  deletes files, whatever the autonomy.
