# CampusLink

A zero-dependency LAN notice/doubt board — works entirely over local WiFi with **no internet connection required**. Built from scratch in C++ using only the standard library and raw POSIX sockets, for the Hackathon Raptors Zero-Dependency 72-Hour Hackathon (Track C — Web & Network).

## What it does

CampusLink lets anyone on the same local WiFi network — a hostel floor, a study group, an office — post and read short messages (notices, doubts, or general posts) through a normal web browser, with **zero internet access required**. One person runs the server on their laptop; everyone else on the same network opens a URL in their browser to read and post.

## Why it matters

Campus/hostel internet drops far more often than the local WiFi itself goes down — but the moment internet is gone, WhatsApp, Discord, and Google Forms all stop working too, since they all require an internet connection to reach their servers. CampusLink only needs the local network, so it keeps working exactly when those tools fail. It's designed for floor/wing-level communication (WiFi range realistically covers a handful of nearby rooms, not an entire multi-floor building) — think "notify your corridor," not "notify the whole hostel."

**Note on institutional WiFi:** some networks enable "client isolation," which can prevent devices from reaching each other directly even while connected to the same WiFi. CampusLink was tested and confirmed working directly over IIT BHU's WiFi (client isolation is not enabled there). As a robust fallback for other networks, the host device can instead create its own personal hotspot, which guarantees direct device-to-device reachability regardless of network policy.

## Features

- [x] Post a message (name, category: General / Notice / Doubt, message text)
- [x] Live feed of messages, newest first, auto-refreshing every 15 seconds
- [x] Messages persist across server restarts (saved to `messages.log`)
- [x] Handles multiple simultaneous connections (one thread per client)
- [x] Works entirely over LAN — zero internet access needed
- [x] Input sanitized (HTML-escaped) to prevent injected content
- [x] Message length capped to prevent abuse

## How to Build

```bash
make
```

Compiles `src/main.cpp` into a `campuslink` binary using `g++` with C++17.

## How to Run

```bash
./campuslink
```

Starts the server on port `8080`. The terminal prints:`.CampusLink running at http://localhost:8080



## How to Connect from Another Device

1. Make sure the other device is on the **same WiFi network** as the machine running the server
2. Find the server machine's local IP address:
   - Mac: System Settings → WiFi → click the (i) next to the connected network → "IP Address"
3. On the other device, open a browser and go to `http://<that-ip>:8080` (not `localhost` — that only works on the same machine running the server)

## Verifying Offline Functionality

To prove this works without internet: put your phone in Airplane Mode, then re-enable just WiFi and turn on Personal Hotspot (guaranteeing zero cellular data is available), connect the server machine to that hotspot, then connect a second device to the same hotspot. Try loading any normal website first (it will fail), then load CampusLink's address (it will work) — proving the app functions with the internet provably unavailable.

## Tech Details

- **Language:** C++17, standard library + POSIX sockets only
- **No third-party dependencies** — see `STDLIB.md` for the full list of what standard-library features replaced typical packages
- **Storage:** flat file (`messages.log`), custom pipe-delimited format with manual escaping
- **Concurrency:** one thread per client connection, with a mutex guarding all file access
- **Security:** all user input is HTML-escaped before being rendered back into pages

## Running the Tests

Start the server in one terminal:
```bash
./campuslink
```
Then, in a second terminal:
```bash
make test
```
This builds and runs `tests/test_main.cpp`, a hand-written test client that sends real HTTP requests over a raw socket and checks the responses — covering the homepage, posting, persistence, empty input, and unknown routes.

## Demo Video

<!-- TODO: add link once recorded -->

## License

MIT — see `LICENSE`.
