
# CampusLink

A zero-dependency, LAN-only group communication app — chat, polls, and reactions that work entirely over local WiFi, with **no internet connection required**. Built from scratch in C++ using only the standard library and raw POSIX sockets, for the Hackathon Raptors Zero-Dependency 72-Hour Hackathon (Track C — Web & Network).

## What it does

CampusLink lets anyone on the same local WiFi network open a browser and instantly join a shared, live chat space — post messages, reply, react, and create/vote on polls — with zero app installs, zero accounts, and zero internet access required. One person runs the server; everyone else on the same network just opens a URL.

> **The internet connects you to the world. CampusLink connects you to the people around you.**

## The core idea

Most communication tools assume you need the internet to talk to people near you, which is backwards when you think about it — the people are already *right there*, on the same WiFi. CampusLink's real insight isn't just "works when internet is down" (in practice, campus/office WiFi rarely loses internet for long) — it's that **being on the same local network is a fast, honest signal that a group of people are physically together right now**, and that signal alone is enough to spin up an instant shared space for them — no numbers exchanged, no group created, no app downloaded.

That reframes what this is actually useful for: not just an outage backup, but a general-purpose tool for **any situation where a group is temporarily sharing physical space and WiFi**, and wants to communicate right now without setup friction.



## Real-World Use Cases

**Where "everyone's already on the same WiFi" is the whole point:**
- **Classroom Q&A / doubt board** — a teacher shares one IP address, the whole class joins instantly, no numbers exchanged
- **Conference/workshop live Q&A** — a speaker takes audience questions and runs live polls without attendees installing anything
- **Event/fest coordination** — organizers post live schedule or venue changes that everyone nearby sees instantly
- **Meetings/offices** — a quick shared scratchpad or poll for a meeting, gone the moment it's not needed
- **Study groups / hostel floors** — notices, doubts, "what time should we meet?" polls
- **Libraries/labs** — "which study room is free," "is Lab 3 open," scoped to just that facility's network
- **Sports coordination** — "need 2 more players at 5, who's in?"
- **Lost and found** — a local community spreads a lost-item post fast, since everyone reachable is actually nearby
- **Borrow and lend** — "does anyone have a charger/calculator/textbook right now?"
- **Pop-up/temporary gatherings** — hackathons, workshops, one-off meetups where a permanent group chat feels like overkill for a few hours together

**Where it genuinely helps during connectivity issues:**
- Campus/hostel internet drops but the local WiFi stays up — CampusLink keeps working when WhatsApp, Google Forms, and Discord all go dark, since none of those can reach their servers without internet
- Remote areas, travel, or any setting without guaranteed internet but with a local network available

**Where "only people physically present can even reach it" becomes a real feature:**
- **Presence-based headcounts / lightweight attendance** — a teacher shares their laptop's IP as a poll; students on the classroom WiFi mark themselves present with one tap. Anyone outside the room, not connected to that network, simply cannot reach the poll at all.

  ## Future Aspect: A Real Attendance System

Attendance is usually solved with expensive hardware — biometric scanners, RFID readers, dedicated kiosks — because the alternative (a teacher calling out names, or a paper sheet) doesn't scale and is easy to fake anyway. CampusLink's approach costs nothing beyond a laptop: students just connect to WiFi on the phones they already carry, and network reachability alone already does something biometric systems need dedicated hardware for — **proving you're physically in the room**, since a device outside the classroom's network genuinely cannot reach the server at all.

What's built right now is the presence layer — the hard networking part. To turn it into a fully reliable attendance system on top of that, the natural next layer is:

- **One-time session codes** — the teacher starts a fresh, time-boxed session each class; only devices submitting within that window and that code count, closing the door on someone marking attendance from memory later
- **One submission per device, enforced server-side** — already have the technical foundation for this (the same per-device cookie ID that already prevents duplicate poll votes)
- **A moderator view** — the teacher sees each submission's device ID alongside the name, so two "students" from the same device, or a suspiciously reused ID, stand out immediately
- **Optional QR-code join flow** — students scan a code shown only on the classroom projector, rather than typing an IP address, closing the loop on someone joining from outside without ever seeing the address

None of this requires new architecture — it's a natural extension of the identity and one-per-device systems already working for polls. The reason it's future work rather than shipped today is scope, not feasibility: today's version proves the concept (network presence, one-tap marking, zero-cost hardware) and honestly labels what separates it from production use (currently proves *presence*, not yet *verified identity* — see the roadmap above for the gap).

**Note on institutional WiFi:** some networks enable "client isolation," which can prevent devices from reaching each other directly even while connected to the same WiFi. CampusLink was tested and confirmed working directly over IIT BHU's WiFi (client isolation is not enabled there)-which means, in practice, **any student on campus can reach a CampusLink instance over the institute's own WiFi, even though every student logs in with their own separate credentials.** There's no need for everyone to join a common hotspot or share a password in this case; the existing campus network already connects them.
 As a robust fallback for institutions where client isolation *is* enabled, the host device can instead create its own personal hotspot, which guarantees direct device-to-device reachability regardless of network policy — at the cost of a smaller physical range.



## Internet vs. Local Network

CampusLink depends on the **local network reaching the server**, not on public internet access:

| Internet | Local network | CampusLink |
|---|---|---|
| Working | Working | ✅ Works |
| Down | Working | ✅ Still works |
| — | Down | ❌ Unreachable |

It doesn't provide internet access — it just doesn't need it to function.



## Features (currently implemented)

- **Group chat** — post messages with a name, category (General / Notice / Doubt), reply threads (one level deep), and emoji reactions (👍 ❤️ 😂 😮 😢 🙏 🔥)
- **Delete your own messages** — cascades to remove any replies underneath, so nothing is left orphaned
- **Persistent identity** — enter your name once; it's remembered (via browser local storage) so you never retype it, while every action is also tied to a private per-device ID via cookie
- **Live polls** — create a poll (2–4 options) from the same "+" menu used for chat; live percentage results and **who voted for what**, with safe one-vote-per-device enforcement
- **Chat-style interface** — oldest-to-newest flow with a fixed bottom composer, auto-scrolling to new messages (without yanking you away if you're reading older ones)
- **Live updates** — the merged chat+poll timeline refreshes automatically every 4 seconds via the browser's built-in `fetch()`, no page reload
- **Persistence** — all messages, polls, votes, and reactions survive a server restart
- **Concurrent connections** — one thread per client, mutex-protected file access
- **Input sanitized** — all user input HTML-escaped before being rendered back
- **Zero internet required** — everything runs purely over the local network



## How to Build

```bash
make
```
Compiles `src/main.cpp` into a `campuslink` binary using `g++` with C++17.



## How to Run

```bash
./campuslink
```
Starts the server on port `8080`. The terminal prints:CampusLink running at http://localhost:8080




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

## Single File Bonus

The entire application — TCP server, HTTP request parsing, routing, persistent storage, and the full HTML/CSS/JavaScript frontend — lives in a single file: `src/main.cpp`. (`tests/test_main.cpp` is a separate test client, kept apart from the application itself as is standard practice.)

## Reproducible Build

Building `campuslink` twice from a clean state produces byte-identical binaries, verified via SHA-256:

```bash
make clean && make && sha256sum campuslink
make clean && make && sha256sum campuslink
```

Both runs produced the same hash: b33229ae2e50065583f36b109615efb59c2e2ab8f356003dc0f10f2c55999bae


This confirms the build is fully deterministic — no embedded timestamps, no non-reproducible compiler behavior — given the same source and compiler flags.

## Package Killer Bonus

**Package replaced:** [cpp-httplib](https://github.com/yhirose/cpp-httplib) — a widely-used single-header C++ HTTP server/client library, commonly reached for specifically to avoid writing raw socket and HTTP-parsing code by hand.

**What CampusLink implements instead, entirely from POSIX sockets:**
- TCP socket setup (`socket()`, `bind()`, `listen()`, `accept()`)
- A hand-written HTTP request parser — reading raw bytes off the socket, parsing the request line, headers, and body (respecting `Content-Length`)
- A hand-written HTTP response builder (status line, headers, body)
- Routing based on method + path
- Cookie parsing and setting (`Cookie` / `Set-Cookie` headers) for per-device identity, which cpp-httplib would otherwise provide helper methods for
- Form body decoding (`application/x-www-form-urlencoded`, including percent-decoding)

None of this uses cpp-httplib or any HTTP library — every layer between "bytes arrive on a socket" and "a parsed, routable request" was written by hand for this project.



## Demo Video

 [Watch the demo] https://youtu.be/zalToh0KLxQ



## License

MIT — see `LICENSE`.










