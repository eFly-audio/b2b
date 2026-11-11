# b2b

**Real-time collaboration inside your DAW.** Voice, DAW audio, screen share, webcam, chat, files and MIDI between up to 4 producers, straight from a plugin on your master in FL Studio, Ableton Live or any VST3 / AU host.

By **eFly Audio** · VST3 (Windows, macOS) + AU (macOS) · free and open source (GPLv3)

---

## What it does

- **Rooms of up to 4.** One person hosts and gets a room code, the others join with it, even mid-session.
- **Voice + DAW audio.** Talk over your mic and stream your master to everyone (Opus). Separate VOICE and DAW receive volumes, plus per-person volume and mute.
- **Screen share + webcam.** Everyone can share at once. Grid view, click to focus, always-on-top pop-out window.
- **Files and folders.** Drag stems, bounces or whole folders in. They go peer-to-peer to everyone with delivery status, and you can drag received files straight into your DAW.
- **Chat.**
- **REC.** Records your master + mic and every person's voice + DAW as separate WAVs, then sends the recording to the room in one click.
- **b2b MIDI** companion plugin. Put it on an instrument / MIDI track to capture piano-roll or clip playback and send it as a `.mid` file.
- **PREVIEW (Ableton).** Ableton's browser / sample preview bypasses plugins on the master. PREVIEW captures the DAW's own output so the room hears it too, with b2b's own output subtracted so nobody gets echoed.

## How it connects

b2b is peer-to-peer. There is no b2b server.

1. **Finding each other:** the room code is exchanged through the free public [PeerJS](https://peerjs.com) signaling server (`0.peerjs.com`). It's only used for the few seconds it takes to connect.
2. **Getting through routers:** public STUN servers (Google, Cloudflare) help each side find its public address.
3. **The session itself:** audio, video, chat, files and MIDI go **directly between the computers** over WebRTC data channels, encrypted with DTLS. Nothing passes through a server.

Closing the plugin closes the connection.

## Install

### Windows
1. Download the latest `b2b-vX.Y.Z-windows.zip` from [Releases](../../releases).
2. Copy `b2b.vst3` and `b2b MIDI.vst3` into `C:\Program Files\Common Files\VST3\`.
3. Rescan plugins in your DAW. Put **b2b on the master**.

### macOS
There's no signed Mac build yet, so you build it yourself. It's one command and takes 10–20 minutes, mostly waiting:

```bash
git clone https://github.com/eFly-audio/b2b.git
cd b2b
bash build-mac.sh
```

The script installs Apple's Command Line Tools if needed, downloads the dependencies, builds everything and installs VST3 + AU into `~/Library/Audio/Plug-Ins/`. See [macOS permissions](#macos-permissions) below.

**Everyone in a room needs the same minor version** (e.g. all on 0.7.x).

## Using it

1. Load **b2b** on your master.
2. Type your name in **YOUR NAME**. One person presses **HOST SESSION** and shares the room code (**COPY CODE**, or click the code chip in the top bar).
3. Everyone else types the code and presses **JOIN**.
4. **STREAM DAW** sends your master, **MIC** sends your voice, **SCREEN** / **CAM** share video, **PREVIEW** is for Ableton's browser preview.
5. Received files land in `Documents/b2b/Received`.

If something breaks, attach `Documents/b2b/smashlink.log` to an [issue](../../issues).

## Known limitations

- **Strict networks.** There is no TURN relay yet. Most home connections work, but some mobile / CGNAT / corporate networks can't make a direct connection and show "Could not link (firewall/NAT)". A relay fallback is the top item on the roadmap.
- **Public signaling server.** `0.peerjs.com` is run by the PeerJS project with no uptime guarantee. You can run your own (see below).
- **PREVIEW on Windows** works with WASAPI / MME drivers. Windows can't capture ASIO output.
- **macOS builds are unsigned** (ad-hoc signed by the build script, so they run on the machine that built them).
- **Webcam on macOS** only works if the DAW itself declares camera access.
- **Display name** defaults to the first name of your computer account. Change it in **YOUR NAME** before hosting / joining if you don't want that shown to the room.

## macOS permissions

These are granted to your DAW, not to b2b:
- **Microphone:** allow when macOS asks.
- **Screen Recording:** the first time you press SCREEN, allow your DAW in System Settings → Privacy & Security → Screen Recording, then **restart the DAW**. Needs macOS 14 (Sonoma) or newer.

## Self-hosting the signaling server

b2b speaks the standard PeerJS protocol, so any [PeerJS server](https://github.com/peers/peerjs-server) works:

```bash
npx peer --port 9000 --key peerjs --path /
```

Put it behind HTTPS (or use plain `ws` on a LAN), then on every machine set the `SMASHLINK_SERVER` environment variable **before starting the DAW**:

```
SMASHLINK_SERVER=your-server.example.com:443        # wss (TLS)
SMASHLINK_SERVER=192.168.1.10:9000:ws               # plain ws, e.g. LAN
```

Everyone in the room must point at the same server.

## Building from source

Dependencies (fetched automatically by `scripts/fetch-deps.sh` into `./deps`):
JUCE 7.0.12, libdatachannel 0.24.6, Mbed TLS 3.6.4, Opus 1.5, libvpx 1.14.1.

- **macOS:** `bash build-mac.sh` (see above).
- **Windows:** the released Windows binaries are cross-compiled from Linux with llvm-mingw. A native Windows build script (Visual Studio / MSVC) doesn't exist yet. The CMake project builds the plugin once the dependencies are installed into a prefix passed as `-DB2B_DEPS=<prefix>`. Contributions welcome.

```
src/
  Processor.*      plugin core, wiring
  Editor.*         UI
  LinkEngine.*     PeerJS signaling + WebRTC mesh (no JUCE dependency)
  AudioLink.*      Opus voice / DAW streaming, jitter buffer
  VideoLink.*      VP8 screen share + webcam
  AppCapture*      PREVIEW: DAW output capture + echo subtraction
  MacSupport.*     macOS screen capture / permissions
  midi/            b2b MIDI companion plugin
```

## Contributing

Issues and pull requests are welcome. For bugs, include your OS, DAW, audio driver, b2b version and `smashlink.log`.

## License

b2b is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License v3** as published by the Free Software Foundation. See [`LICENSE`](LICENSE).

It's built with JUCE under JUCE's GPLv3 option. Third-party components and their licenses are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

---

If b2b saves your sessions, you can support development via the **Sponsor** button.
