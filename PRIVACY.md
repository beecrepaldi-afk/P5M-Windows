# P5M for Windows — Privacy Policy

*Last updated: October 12, 2026*

P5M is a free, open-source Remote Play app for the PlayStation 5. It has no accounts of its own, no analytics, no advertising and no telemetry. We (the P5M project) do not receive any data from the app.

## What P5M talks to, and why

- **Your PS5.** P5M finds your console on your local network and streams video, audio and controller input directly between your PC and your PS5. When you unmute the microphone in a stream, your microphone audio is sent to your PS5 only.
- **Sony (PlayStation Network)** — only if you choose to sign in to PSN, to register your console automatically or to play away from home. Sign-in happens on Sony's own page, and the access token P5M receives is stored on your PC only. Remote play then uses Sony's servers to reach your console, and your router (UPnP) and public STUN servers (Google's, and a public list fetched from GitHub) to set up the connection.
- **psn.flipscreen.games** — only if, while signing in to PSN, you look up your PSN account ID by username: the username you type is sent to that third-party service to find the ID.
- **GitHub** — only if you press *Report a problem*. P5M saves a report file on your Desktop and opens a GitHub page in your browser. Nothing is uploaded unless you attach the file and submit the form yourself.

## What stays on your PC

Your settings, registered consoles, PSN token and session diaries (logs used to measure stream quality) are stored locally in your user profile. Session diaries remove network addresses, accounts and keys when they are written, and again when a report is created. Uninstalling the Microsoft Store version removes them. The portable version keeps them in `%APPDATA%\P5M` and in the registry under `HKEY_CURRENT_USER\Software\P5M`; delete those to remove them.

## Children

P5M does not knowingly collect any information from anyone, including children.

## Contact

Questions about this policy: open an issue at <https://github.com/beecrepaldi-afk/P5M-Windows/issues>.

P5M is not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc.
