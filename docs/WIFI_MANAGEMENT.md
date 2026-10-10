# Wi-Fi management in Keira

Management is in **Keira only**. Keira and Lilplayer share SDK saved-network
storage and automatic fallback through the existing `network` NVS namespace.
Rebuild both apps against the updated matching SDK stage branches.

## Using it

Open **WiFi**:

- **Networks** scans nearby networks. Known networks are green, including saved
  open networks. A connects with a saved password, or opens masked password
  entry for an unknown secured network. C shows network details; B returns.
- **Saved networks** lists saved networks even when their access point is absent.
  Choose one for **Connect**, **Change password**, or **Forget**.
- **Forget** asks for confirmation and defaults to Back. It removes that network's
  saved credentials and selected-network reference; forgetting the current
  network also disconnects it. Automatic recovery may try another saved network
  after leaving the management UI; it cannot retry the forgotten credentials.
- **Disconnect** stops the current connection without deleting credentials.

Scanning and connection waits yield every 50 ms, allow B cancellation and have
15-second outer deadlines. Driver calls are not preempted by those deadlines.
There are no indefinite polling loops. Scans are asynchronous, don't deliberately
tear down the current connection, deduplicate names and show up to 64 results.

Credentials are saved only after the adapter obtains an IP address, not when a
failed connection is attempted. Password change is a new connection attempt;
a failed/cancelled attempt doesn't replace the saved password. Storage errors
are reported rather than claiming settings were saved. Passwords are masked
while entering and are never displayed in saved-network menus or logged.

## Unicode names

The exact SSID bytes are kept for storage, hashing and connection: names are not
normalized, translated or replaced. The 32-byte SSID limit is **bytes**, not
characters. `🦄🌈🎉` is 12 UTF-8 bytes and is covered by the real-source host tests
under both signed-char and unsigned-char compiler modes.

The current SDK font is BMP-only and cannot draw those emoji. Keira shows
`[U+1F984][U+1F308][U+1F389]` as a readable display label; ordinary Ukrainian/Latin
text remains unchanged. Long labels use the existing menu marquee. This is
connection/storage support with a font fallback, **not emoji artwork support**.

## Storage and compatibility

- Up to 16 indexed records under `network/wifi0` ... `wifi15`, each with a format
  marker, active flag, exact SSID and password. Each record is one NVS blob write.
- Selected credentials are mirrored to the existing `last_ssid` and
  `<unsigned-byte-SSID-hash>_pw` keys for Lilplayer/older firmware compatibility.
  Those compatibility keys are separate writes, not one all-or-nothing transaction.
  Partial failures are reported; the indexed record remains available for retry.
- Existing firmware stored password hashes but not every original SSID. The old
  selected SSID is immediately visible. Other old saved passwords are discovered
  when their SSID is scanned and imported without changing the selected network.
  An absent old SSID cannot be reconstructed from its hash alone.
- Forget writes an inactive record before removing the legacy password key. This
  prevents a failed cleanup from resurrecting the network. A pending background
  save is cancelled under the same NVS lock; a queued late event can't recreate it.
- Indexed credentials are compared by full SSID, not the old hash. Hash-collision
  regressions preserve another selected network's compatibility password.
- Credentials aren't reread or rewritten per UI frame. The service checks its
  pending-save flag once per second; callbacks only stage RAM values. Identical
  records/passwords/selected names cause no additional NVS writes.

Automatic connection first tries the last-used saved network. If unavailable,
it scans and tries other visible saved networks strongest first, with 10 seconds
per network, a 5-second scan polling deadline and a 35-second total round budget.
No automatic scan runs without an untried saved candidate. Keira increases
failed-round cooldowns from 30 seconds up to 15 minutes; success or a user
connection request resets the cooldown. Automatic and explicit management scans
process at most 64 borrowed records on a temporary worker. Arduino owns scan
completion and still allocates all discovered results; total scan memory is not
capped. The former direct-IDF collector raced Arduino's result consumer and could
show an empty network list. Cancellation drains the short blocking scan before a new
scan can start. Lilplayer uses the same selector and its existing stream retry
policy. Successful fallback updates the last-used selection without repeated NVS
writes. A working connection is preserved, even if another network is stronger.
Opening this management app pauses background selection so scans don't compete.
Manual Connect tries the chosen network only; Disconnect disables automatic
recovery for the session. See SDK `docs/WIFI_FALLBACK.md` for ownership and limits.

## Verification

Run `python3 tests/wifi_management/run.py` for actual production credential and
NetworkService checks with normal and ASan/UBSan builds. Includes UTF-8 emoji,
open/legacy credentials, hash collisions, capacity, storage failures, successful-IP
save, pending-save/Forget ordering and cancellation. The HAL is deterministic;
it is not a physical Wi-Fi or device stack test.

`tests/wifi_management/render.py` uses the production WiFiConfigApp menu-construction
blocks and actual SDK menu drawing with a recording graphics HAL to render four
native-layout views. It needs local `g++` and `rsvg-convert`; fonts are approximate
and selection backgrounds/icons aren't reproduced. Inspected root, saved list,
network actions and default-cancel Forget layout. Real device rendering, Wi-Fi
behavior and RF conditions still require a device run.

Real ESP32-S3 installed-header syntax and EN/UK localization checks passed. Changed
C++ passes clang-format20; the full Keira format gate still fails on inherited
unrelated files. cppcheck is unavailable locally. No firmware build or flash was
performed. This feature is not claimed as a fix for the weak-signal reset.

Same-SSID Connect with unchanged credentials preserves the working connection;
edited passwords still require association. Explicit Disconnect calls the adapter
once and disables recovery. Fitted menu labels are cached until SSID, state or
available width changes (status is still observed at most once per second).
