# Walnut-CGB source

- Upstream: https://github.com/Mr-PauI/Walnut-CGB
- Revision: `a42c186917516cc9c80515ceae1121600a071628`
- Imported files: `walnut_cgb.h`, `LICENSE`
- License: MIT; the header retains its original Peanut-GB and SameBoy attributions.
- No semantic upstream source changes in this import; the header's CRLF line
  endings were normalized to LF for this repository.

The Keira adapter defines `ENABLE_SOUND=0` before including the core. Sound
requires a separate APU and Keira I2S integration. Do not represent this first
source milestone as a finished GB/GBC emulator.
