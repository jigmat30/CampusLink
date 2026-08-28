# CampusLink

A zero-dependency LAN notice/doubt board — works entirely over local WiFi with **no internet connection required**. Built from scratch in C++ using only the standard library and raw POSIX sockets, for the Hackathon Raptors Zero-Dependency 72-Hour Hackathon (Track C — Web & Network).

## What it does

CampusLink lets anyone on the same local WiFi network post and read short messages — notices, doubts, or general posts — through a normal web browser, with **zero internet access needed**. One person runs the server; everyone else on the same network just opens a URL.

<!-- TODO after building: add 1-2 sentences on the real problem this solves (hostel connectivity, offline communication) -->

## Why it matters

<!-- TODO: expand on the "internet goes down but LAN still works" pitch -->

## Features

- [ ] Post a message (with name + category: Notice / Doubt / General)
- [ ] View a live, auto-refreshing feed of messages
- [ ] Messages persist across server restarts
- [ ] Handles multiple simultaneous connections/devices
- [ ] Works with zero internet access — LAN only

## How to Build

```bash
make
```

<!-- TODO: confirm this is accurate once Makefile exists -->

## How to Run

```bash
./campuslink [port]
```

Default port: `8080` (or specify your own, e.g. `./campuslink 9090`)

The terminal will print the address to open, e.g.:
```
CampusLink running at http://192.168.1.5:8080
```

## How to Connect from Another Device

1. Make sure the other device is on the **same WiFi network**
2. Open a browser and go to the address printed above (use your machine's LAN IP, not `localhost`)
3. Start posting/reading messages

<!-- TODO: add a screenshot or two once the UI exists -->

## Tech Details

- Language: C++ (standard library + POSIX sockets only)
- No third-party dependencies — see `STDLIB.md` for what standard-library features replaced typical packages
- Data storage: flat file (`messages.log`), custom pipe-delimited format
- Concurrency: one thread per client connection, guarded by a mutex around file access

## Tests

```bash
<!-- TODO: add exact test-run command once tests exist -->
```

## Reproducible Build (bonus)

<!-- TODO: fill in with build hashes once confirmed reproducible -->

## Demo Video

<!-- TODO: add link to 5-minute demo video -->

## License

MIT — see `LICENSE`.
