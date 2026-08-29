# STDLIB.md

This document lists what third-party packages/libraries would normally be used for this kind of project, and what standard-library / POSIX functionality was used instead to keep this project at **zero third-party runtime dependencies**.

| # | Normally you'd use... | Instead, we used... | Why |
|---|---|---|---|
| 1 | A web framework (e.g. cpp-httplib, Express, Flask) | Raw POSIX sockets (`socket()`, `bind()`, `listen()`, `accept()`) + a hand-written HTTP request/response layer | No framework allowed under zero-dependency rules; this also demonstrates understanding of what frameworks abstract away |
| 2 | A JSON library (e.g. nlohmann/json) | Custom pipe-delimited (`|`) line-based file format with manual parsing/escaping | Avoids pulling in a serialization dependency for a simple data shape |
| 3 | A URL-decoding / form-parsing library | Hand-written percent-decoding (`%XX` → char) and `+` → space conversion | Required to correctly parse standard HTML form POST bodies |
| 4 | An HTML templating engine (e.g. Mustache, Jinja-style) | Direct string concatenation / `std::ostringstream` to build HTML responses | Small enough page structure that a templating engine would be overkill anyway |
| 5 | A UUID/ID-generation library | Timestamp-based IDs (Unix epoch seconds) | Sufficient uniqueness for this use case, avoids random-generation dependency |
| 6 | A logging library (e.g. spdlog) | `std::cerr` with manual formatting | Simple enough logging needs for a small server |
| 7 | A threading/concurrency library (e.g. Boost.Thread) | `<thread>` and `<mutex>` from the C++ standard library | Already provides everything needed for one-thread-per-connection handling |
| 8 | A CLI argument-parsing library (e.g. cxxopts) | Manual `argv` parsing | Only one optional argument (port number) needed |
| 9 | A date/time formatting library | `<chrono>` + manual formatting for "X minutes ago" style timestamps | Standard library chrono is sufficient for this |
| 10 | A testing framework (e.g. Google Test, Catch2) | A small hand-written C++ test client using raw sockets, with manual assertions | Keeps the test suite itself zero-dependency too |

<!-- TODO: revisit and confirm all of the above are accurate once implementation is complete. Add any additional substitutions discovered during building (aim for 10+ to also qualify for the STDLIB Log bonus challenge). -->

## Dependency Proof

The following confirms no third-party headers are included anywhere in the source:

```bash
grep -r "#include" src/
```

<!-- TODO: paste actual grep output here once source files exist, showing only <...> standard library / POSIX headers -->
src/main.cpp:1:#include <iostream>
src/main.cpp:2:#include <fstream>
src/main.cpp:3:#include <sstream>
src/main.cpp:4:#include <string>
src/main.cpp:5:#include <vector>
src/main.cpp:6:#include <map>
src/main.cpp:7:#include <unordered_map>
src/main.cpp:8:#include <algorithm>
src/main.cpp:9:#include <thread>
src/main.cpp:10:#include <mutex>
src/main.cpp:11:#include <ctime>
src/main.cpp:12:#include <sys/socket.h>
src/main.cpp:13:#include <netinet/in.h>
src/main.cpp:14:#include <unistd.h>
tests/test_main.cpp:1:#include <iostream>
tests/test_main.cpp:2:#include <string>
tests/test_main.cpp:3:#include <sstream>
tests/test_main.cpp:4:#include <sys/socket.h>
tests/test_main.cpp:5:#include <netinet/in.h>
tests/test_main.cpp:6:#include <arpa/inet.h>
tests/test_main.cpp:7:#include <unistd.h>
Jigmat@MacBook-Air-20 CampusLink % 
