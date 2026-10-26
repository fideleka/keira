# Compositor activity guards

The presentation loop services the existing idle/wake policy and output power
before any layer/overlay work. During full-off sleep, it marks the external
snapshot for wake refresh, releases the thread-list lock and waits for the next
input/service tick. It does not compose black frames or call shared native
presentation while asleep. Applications/background tasks are not globally
suspended by this presentation guard.

Native overlay preparation is limited to the legacy native-only path. Shared
Both output uses the single immutable composed frame for both readers.
External-only mode does not invoke the native presenter. Three bounded geometry
metadata fields detect width/height/rotation changes independently of native
overlay preparation, including static external-only frames. Wake, route changes,
feedback expiry, and normal canvas invalidation continue to refresh correctly.
No idle timeout, brightness setting, rendering layout, FPS, or task priority is
changed.

`tests/volume_overlay.py` exercises the actual SDK presentation methods and
compositor loop across native/shared/external, sleeping and wake states, with
pixel-sink counters and immutable-source checks. It explicitly enables the
measurement branch and runs its simulated window long enough for the existing
five-second report cadence. Normal diagnostics-off behavior is independently
checked by `tests/release_diagnostics.py`. Host captures and installed-header
syntax checks are not device power/timing or full firmware-build verification.
