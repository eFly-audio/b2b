# Third-party software

b2b is licensed under the GNU GPL v3 (see `LICENSE`). It is built on the following open-source projects. Their full license texts are in the [`licenses/`](licenses) folder and must ship with every binary release.

| Project | Version | License | Used for |
|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 7.0.12 | GPLv3 (or commercial) | Plugin framework, UI, VST3 / AU wrappers, camera |
| [libdatachannel](https://github.com/paullouisageneau/libdatachannel) | 0.24.6 | MPL-2.0 | WebRTC data channels, PeerJS signaling over WebSocket |
| [libjuice](https://github.com/paullouisageneau/libjuice) | bundled with libdatachannel | MPL-2.0 | ICE / STUN (NAT traversal) |
| [usrsctp](https://github.com/sctplab/usrsctp) | bundled with libdatachannel | BSD-3-Clause | SCTP transport for data channels |
| [plog](https://github.com/SergiusTheBest/plog) | bundled with libdatachannel | MIT | Logging inside libdatachannel |
| [nlohmann/json](https://github.com/nlohmann/json) | bundled with libdatachannel | MIT | JSON for signaling messages |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | 3.6.4 | Apache-2.0 (chosen from Apache-2.0 OR GPL-2.0-or-later) | TLS / DTLS encryption |
| [Opus](https://opus-codec.org) | 1.5.x | BSD-3-Clause | Voice and DAW audio codec |
| [libvpx](https://chromium.googlesource.com/webm/libvpx) | 1.14.1 | BSD-3-Clause + patent grant (`licenses/libvpx-PATENTS.txt`) | VP8 video for screen share / webcam |

## Online services used at runtime

These are third-party public services, not run by eFly Audio:

- **PeerJS public signaling server** (`0.peerjs.com`): only used for the few seconds it takes peers to find each other. No audio, video, chat or files pass through it. Can be replaced with your own server (see README, "Self-hosting the signaling server").
- **Public STUN servers** (`stun.l.google.com:19302`, `stun.cloudflare.com:3478`): tell each peer its public address so a direct connection can be made.

All session traffic (audio, video, chat, files, MIDI) goes directly between the peers, encrypted with DTLS.

## Trademarks

VST is a registered trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of Apple Inc. FL Studio and Ableton Live are trademarks of their respective owners. b2b is not affiliated with or endorsed by any of them.
