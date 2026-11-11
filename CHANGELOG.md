# Changelog

## 0.7.2
- Fix: incoming audio could get stuck at 300–600 ms delay after a big DAW buffer or a network hiccup. It now recovers within about a second, and the jitter buffer sizes itself to the real connection.

## 0.7.1
- PREVIEW button (Ableton): captures what the DAW itself outputs, so Ableton's browser/sample preview reaches the room. b2b subtracts its own output first, so nobody hears themselves echoed back.
  Windows: works with WASAPI / MME; ASIO output can't be captured.

## 0.7
- Up to 4 people per room (mesh). Room code chip in the top bar, join mid-session.
- Per-person chips with talk indicator, own volume and mute.
- All 4 can share screen / cam at once; grid video view.
- Files and chat go to everyone, with "delivered 2/3" status.
- REC records your master + mic and every person's voice + DAW as separate WAVs.

## 0.6
- REC button + one-click send of the recording.
- b2b MIDI companion plugin: captures piano-roll / clip playback and sends it as a .mid file.

## 0.5
- Always-on-top pop-out video window, Settings panel, folder transfer, separate VOICE / DAW receive volumes.

## 0.4
- Screen share and webcam (VP8), dB metering.

## 0.3
- Voice (Opus) and DAW audio streaming with jitter buffer.

## 0.2
- Fixed signaling compatibility with the public PeerJS server.

## 0.1
- Host / join with a room code, chat, peer-to-peer file transfer.
