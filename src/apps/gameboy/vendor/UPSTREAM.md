# Walnut-CGB source

- Upstream: https://github.com/Mr-PauI/Walnut-CGB
- Revision: `a42c186917516cc9c80515ceae1121600a071628`
- Imported files: `walnut_cgb.h`, `LICENSE`, and
  `examples/sdl2/minigb_apu/{minigb_apu.c,minigb_apu.h,LICENSE}` (renamed here
  to `MINIGB_APU_LICENSE`).
- License: MIT; the header retains its original Peanut-GB and SameBoy attributions.
- The Walnut header has no semantic upstream changes; its CRLF line endings
  were normalized to LF for this repository. The MiniGB source has one Keira
  build define (`MINIGB_APU_AUDIO_FORMAT_S16SYS`) before its header include.

The Keira adapter routes Walnut's APU register hooks through the per-instance
`GbCore` context, then sends MiniGB's stereo samples to Lilka's I2S output.
Audio behavior remains unverified on hardware.
