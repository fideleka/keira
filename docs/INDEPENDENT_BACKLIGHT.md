# Modified-hardware SDK brightness

This feature branch starts from this application's staging branch and uses the
matching SDK `feature/independent-backlight` sibling checkout (`../sdk`).

Use environment **`v2-modified-backlight`** only after isolating the amplifier
module SD connection from GPIO46 and adding independent bias. This inherits the
normal application and enables `-DLILKA_INDEPENDENT_BACKLIGHT=1`; default builds
remain stock-safe.

Hold Select first, then Left/Right to change brightness. The SDK owns PWM,
feedback, delayed NVS saves and sleep/wake restoration. No test-screen startup
is included. Saved zero boots at 5% to avoid appearing dead; zero during use
still turns the backlight fully off.

Read `../sdk/docs/INDEPENDENT_BACKLIGHT.md` for APIs, peripheral reservations,
verification details and remaining hardware checks. No firmware build/flash
was performed for this integration.

Reviewed SDK commit: `efb586104f479d20b2631e73e856e75150c89891`.
