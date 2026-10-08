# Modified-hardware SDK brightness

This feature branch starts from this application's staging branch and uses the
matching SDK `feature/independent-backlight` sibling checkout (`../sdk`).

Use environment **`v2-modified-backlight`** only after isolating the amplifier
module SD connection from GPIO46 and adding independent bias. This inherits the
normal application and enables `-DLILKA_INDEPENDENT_BACKLIGHT=1`; default builds
remain stock-safe.

Hold Select first, then Left/Right to change brightness in **5%** steps. The SDK owns PWM,
feedback, delayed NVS saves and sleep/wake restoration. No test-screen startup
is included. Selected brightness has a **5% minimum**; full off is a separate operation.

Read `../sdk/docs/INDEPENDENT_BACKLIGHT.md` for APIs, peripheral reservations,
verification details and remaining hardware checks. No firmware build/flash
was performed for this integration.

Reviewed SDK commit: `869a48403edb7cfb25fd2fe955989568c179a238`.

## Display settings

Settings -> Display provides brightness, automatic-off timeout and optional idle dim timeout. Both apps use
SDK namespace `backlight`: `level`, `timeoutSeconds` and `dimSeconds`. Both idle timers default to
**Never**; Never/30 sec/1/2/5/10 min presets are available. Up/Down selects;
Left/Right or D/A adjusts. Changes are saved asynchronously after 600 ms quiet.
The overlays use speaker/sun icons and percentages rather than words.

The first button gesture wakes the LCD without activating anything, and stays
consumed until all buttons are released. No MCU sleep or stopping background work.

Keira auto-off is restricted to the idle Launcher; other foreground apps inhibit it.

Idle dim temporarily applies 5% without modifying selected/saved brightness. A key gesture or foreground work restores it. Existing saved timer choices are retained; changing defaults does not erase them. No motion sensor is assumed or polled.
