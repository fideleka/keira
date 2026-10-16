Jellyfin playback on Lilka v2: proposed design
==============================================

Status: design only, 2026-10-16. No player, adapter, device benchmark, or Jellyfin
integration has been implemented or verified by this change.

Goal and scope
--------------

Play Jellyfin library videos on Lilka v2 (ESP32-S3), using the existing external
SPI display path. The server decodes, resizes, and re-encodes the source.
Lilka receives independently decodable JPEG frames and renders RGB565 pixels.
Audio and playback reporting follow after video-only feasibility is established.

MP4 is a container, not a codec. This design does not require Lilka to decode
arbitrary MP4/H.264/H.265 files. The ESP32-S3 has no hardware H.264/H.265 decoder;
software decoding of those codecs is outside the initial scope.

Existing components and alternatives
------------------------------------

Reuse maintained components before implementing a custom media stack:

* Jellyfin remains the library, authentication, and playback-session authority.
* FFmpeg is the candidate server-side transcoder. Reuse its demuxers, decoders,
  scaling filters, and MJPEG encoder.
* Keira declares ``bitbank2/JPEGDEC@1.8.4`` in ``platformio.ini``.
  ``src/apps/launcher/wallpaper.h`` uses JPEGDEC for baseline JPEG images.
  Still-image support is not evidence of sustained video performance.
* Reuse Keira's canvas/presentation and external display-driver routing.
  Do not create another SPI owner or bypass display lifecycle management.

First inspect the installed Jellyfin version, playback API, device profiles,
and FFmpeg build. Prefer a native profile if it can deliver the required stream.
**A custom profile alone is not proven to enable MJPEG output.** If native output
is unsuitable, use a thin adapter with FFmpeg. No plugin compatibility survey
has been performed for this proposal; perform that check before implementation.

Architecture
------------

Recommended prototype::

    Jellyfin library / authorized media source
                     |
           server adapter + FFmpeg
          decode -> resize -> baseline JPEG
                     |
            bounded HTTP frame stream
                     |
       Keira App -> JPEGDEC -> RGB565 canvas
                     |
          existing presentation pipeline
                     |
              external SPI display

The adapter may manage its own FFmpeg process. This does not assume Jellyfin's
built-in transcoder accepts arbitrary output formats. Avoid a redundant
decode/re-encode chain when an authorized source can be decoded once.
Source access must preserve Jellyfin access restrictions.

The adapter handles Jellyfin credentials, item selection, playback sessions,
transcoder lifecycle, and stream adaptation. Lilka implements a small client.
A full Jellyfin client on Lilka is an alternative, but adds API, authentication,
library browsing, and session-management work to firmware.

Initial profile and resource budget
-----------------------------------

* Start with video only, baseline sequential JPEG, at 10 FPS. Try 15 FPS after
  measurement. These are targets, not verified playback rates.
* Use 320 x 240 as a reference envelope. Negotiate actual logical canvas
  dimensions and rotation; existing configurations can use 280 x 240.
* Preserve aspect ratio with server-side scaling and letterboxing.
* Tune JPEG quality against measured frame sizes and decoding time. Do not
  assume a fixed compression ratio or guaranteed network bitrate.
* Bound frame sizes, dimensions, queues, and allocations. Reject malformed or
  oversized frames before decoding. Check JPEGDEC's accepted input variants.

One 320 x 240 RGB565 frame occupies 153,600 bytes (150 KiB); two occupy 300 KiB,
before compressed input, decoder workspace, network buffers, and existing canvas
allocations. Count pipeline-owned buffers before allocating additional copies.
PSRAM capacity does not guarantee sufficient contiguous internal DMA memory.

At an effective 40 MHz SPI clock, a full 320 x 240 RGB565 payload requires at
least 30.72 ms. The payload-only ceiling is about 32.6 FPS. At 280 x 240, the
payload floor is 26.88 ms. Commands, packing, scheduling, and decoding add cost.
These are calculations, not device measurements or end-to-end FPS predictions.
Do not raise the saved display clock as part of this feature.

Network payload rate is approximately ``mean JPEG bytes * FPS * 8`` bits/s,
plus overhead. For illustration only, 20,000 bytes/frame at 15 FPS requires
2.4 Mbit/s before overhead. Measure high-motion and high-detail scenes.

Transport and playback behavior
-------------------------------

For video-only bring-up, consider HTTP multipart MJPEG with explicit part
lengths and a bounded parser. Test FFmpeg's output framing rather than assuming
compatibility. TCP reads do not correspond to complete JPEG frames.
Pace output in real time; an unrestricted producer can build stale TCP buffers.

Before adding audio, select or define a framing contract with a stream version,
sequence number, presentation timestamp, payload length, dimensions, and explicit
end/error signaling. Multipart MJPEG alone is not a complete audio/video protocol.
Prefer an existing suitable transport if it meets the device resource limits.

* Use a small bounded queue. Discard late video frames, not arbitrary bytes
  inside compressed frames. Prevent latency from growing without limit.
* Pause, seek, stop, and reconnect must reset or terminate the relevant session.
  A seek starts a new timeline and flushes queued video and audio.
* A disconnected client must release its transcoder after a bounded timeout.
* Stream failures must return control without blocking the Keira UI task.

Audio phase
-----------

Choose audio after measuring the available decoder and output path.
PCM simplifies decoding but increases bandwidth; compressed audio needs a
compatible decoder and CPU budget. No codec or sample rate is selected yet.

Use audio playback as the presentation clock when audio is enabled. Schedule
video using timestamps; drop late frames and wait for early frames. Handle
underruns and clock drift explicitly. Define a measurable synchronization
tolerance and pause/seek semantics before acceptance testing.

Firmware integration
--------------------

Implement the player as an ``App`` derived through ``KeiraThread``. Keep player
state in the App; add a background ``Service`` only if its lifecycle requires one.
Use ``esp_http_client_*`` and existing synchronization/cleanup conventions.
Follow ``AGENTS.md``; do not bypass the task hierarchy.

Support External mode first, then test Internal and Both through existing routing.
Check rotation, letterboxing, overlays, stop/back, and buffer ownership.
Display submissions are not necessarily visible frame updates.

Keep Jellyfin credentials on the adapter where practical. Use scoped client
credentials, redact tokens from logs, and do not embed administrator keys in
firmware or URLs. Keep the prototype private. Validate certificates for HTTPS;
do not disable certificate verification as a workaround.

Milestones and acceptance
-------------------------

1. **Compatibility preflight:** record Jellyfin/FFmpeg versions, source access,
   existing integration candidates, native profile capabilities, JPEGDEC input
   restrictions, display dimensions, and memory budget. Decide native output
   versus adapter from evidence.
2. **Offline JPEG baseline:** decode representative frames through the intended
   canvas path. Measure decoding, display time, and memory separately.
3. **Video-only stream:** play a local test clip through FFmpeg and HTTP before
   connecting Jellyfin. Start at 10 FPS, then try 15 FPS if sustainable.
   Record actual displayed FPS and latency at the selected dimensions.
4. **Jellyfin integration:** select an authorized item, start/stop conversion,
   and verify playback reporting. Test pause, seek, source errors, disconnect,
   and reconnect without orphaned FFmpeg processes.
5. **Audio and controls:** select a measured-compatible audio profile, implement
   synchronization, and test the complete user control lifecycle.
6. **Device acceptance:** run at least 30 minutes with high-motion material.
   Record receive/decode/display timing, dropped frames, latency, queue depth,
   free/minimum heap, stack watermark, audio underruns, and synchronization drift.
   Confirm responsive controls, no growing queues or memory loss, and recovery
   from Wi-Fi loss. Record hardware, SDK/firmware revisions, routing mode,
   effective SPI clock, and media profile with each result.

Do not claim a supported profile until device measurements satisfy its selected
frame rate and documented latency/synchronization limits. Lower frame rate,
resolution, or JPEG quality if the pipeline cannot sustain the selected profile.

References and open questions
-----------------------------

Local anchors: ``platformio.ini``, ``src/apps/launcher/wallpaper.h``,
``AGENTS.md``, and ``docs/display-performance.md``. The display document records
transport calculations and display-path limitations separately from measurements.

Primary implementation references (not checked against the installed server
during this documentation task):

* `Jellyfin API <https://api.jellyfin.org/>`_
* `Jellyfin transcoding <https://jellyfin.org/docs/general/post-install/transcoding/>`_
* `FFmpeg formats <https://ffmpeg.org/ffmpeg-formats.html>`_
* `JPEGDEC source and formats <https://github.com/bitbank2/JPEGDEC>`_

Open decisions: native Jellyfin output support, adapter language, exact transport,
source authorization, audio codec, memory budget, and sustainable frame rate.
This document does not deploy services, change credentials, or flash firmware.
