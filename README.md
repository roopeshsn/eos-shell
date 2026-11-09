# eosh - an Arista EOS-style shell for your laptop

`eosh` is an interactive CLI written in C that looks and feels like Arista EOS
(`host>`, `host#`, `host(config)#`, `?` help, Tab completion, `| include` filters)
but reports on the machine it runs on. It supports macOS and Linux.

Status: read-only. It inspects the host; it does not change any system
configuration yet (see [Roadmap](#roadmap)).

## Build and run

Requirements: a C11 compiler, `make`, and GNU readline (or libedit as a fallback).

    # macOS:  brew install readline
    # Debian/Ubuntu:  apt install build-essential libreadline-dev
    make
    ./eosh

Makefile targets:

| Command         | Effect                                                        |
|-----------------|---------------------------------------------------------------|
| `make`          | Build `./eosh`                                                |
| `make test`     | Build and run unit tests and golden transcript tests          |
| `make test SAN=1` | Same, with AddressSanitizer and UBSan                       |
| `make clean`    | Remove `build/` and `eosh`                                    |

Readline is located via `brew --prefix readline`, then `pkg-config readline`,
then `-ledit`. The build uses `-Wall -Wextra`; add `WERROR=1` to treat warnings as errors.

## Using the shell

### Modes

| Mode           | Prompt                  | Enter with           | Leave with         |
|----------------|-------------------------|----------------------|--------------------|
| User EXEC      | `host>`                 | (start)              | `exit`, `quit`     |
| Privileged EXEC| `host#`                 | `enable`             | `disable`, `exit`  |
| Global config  | `host(config)#`         | `configure [terminal]` | `exit`, `end`    |
| Interface config | `host(config-if-NAME)#` | (config sub-mode)  | `exit`, `end`      |

`end` returns straight to privileged EXEC. `exit` steps back one level.

### Command-line editing

- **Abbreviations**: any unambiguous prefix works, so `sh cl` runs `show clock`.
  An exact match always wins. Matching is case-insensitive.
- **Tab** completes the current word; Tab twice lists candidates.
- **`?`** shows context-sensitive help for the words typed so far. `<cr>` means
  the command can be run as typed; `|  Output modifiers` means a pipe filter may follow.
- **Errors** follow EOS wording:
  `% Invalid input (at token N: '...')`, `% Ambiguous command (at token N: '...')`,
  `% Incomplete command`.

### Output filters

Append `|` and a filter to any command. Filters chain left to right, and filter
names may be abbreviated. Patterns are POSIX extended regular expressions.

| Filter                    | Meaning                                        |
|---------------------------|------------------------------------------------|
| `\| include <regex>`      | keep matching lines                            |
| `\| exclude <regex>`      | drop matching lines                            |
| `\| grep [-i] [-v] <regex>` | like grep; `-i` ignore case, `-v` invert     |
| `\| begin <regex>`        | start output at the first matching line        |
| `\| count`                | print `Number of lines in output: N`           |

Example: `show ip route | exclude 127. | count`

### Commands

Available in EXEC and privileged EXEC modes unless noted.

| Command                         | Shows                                                    |
|---------------------------------|----------------------------------------------------------|
| `enable`, `disable`             | switch between user and privileged mode                  |
| `configure [terminal]`          | enter global config mode                                 |
| `exit`, `quit`, `end`           | leave a mode / the shell                                 |
| `show version`                  | hostname, hardware model, OS, kernel, arch, uptime, load, memory |
| `show hostname`                 | hostname                                                 |
| `show clock`                    | current date and time                                    |
| `show interfaces [<name>]`      | per-interface state, MTU, MAC, addresses, 64-bit counters |
| `show interfaces status`        | one-line summary: port, status, MTU                      |
| `show ip interface [brief]`     | IPv4 addresses per interface                             |
| `show ip route`                 | IPv4 routing table with gateway of last resort           |
| `show ip route <addr>`          | longest-prefix-match route used to reach an IPv4 address |
| `show ip route summary`         | route counts by source and by mask length                |
| `show ipv6 route [<addr> \| summary]` | same three views for IPv6                          |
| `show arp`, `show ip arp`       | ARP/neighbor table                                       |
| `show processes`                | all processes (PID, PPID, user, state, RSS, CPU, command)|
| `show processes top`            | top 15 processes by resident memory                      |
| `show interfaces counters`      | per-interface packet, byte and error counters            |
| `show users`                    | logged-in sessions (line, user, host, idle, login time); `*` marks your own |
| `show environment`              | power source, battery charge/health/cycles/voltage/temperature, and temperature and fan sensors where the OS exposes them |
| `show logging [<count>]`        | last N system log lines (default 100, max 1000)          |
| `show history`                  | commands entered in this session                         |
| `show tech-support`             | runs the main show commands under section headers        |

MAC addresses are printed in EOS dotted form (`aabb.ccdd.eeff`).

Set `EOSH_HOSTNAME=name` to override the hostname in the prompt (used by tests).

## Architecture

    src/
      main.c         entry point, read-eval loop, hostname setup
      ctx.h          cli_ctx (mode, hostname, interface, output stream) and mode masks
      cmdtree.[ch]   command trie, abbreviation matching, completion, help
      cli.[ch]       readline integration: prompt, Tab completion, `?` binding
      pipe.[ch]      `|` filter parsing and application
      history.[ch]   session command history (own store, independent of readline)
      cmds/          command handlers, one file per area
        core.c         modes: enable, configure, exit, end ...
        show.c         version, hostname, clock
        show_interfaces.c
        show_routing.c routes, route lookup/summary, ARP
        show_processes.c
        show_system.c  users, environment, logging, history
        show_tech.c    tech-support (runs other show commands)
      sys/           platform data layer
        sys.h          neutral API and data structs
        sys_common.c   shared code (getifaddrs, sorting, uptime/MAC formatting)
        sys_darwin.c   macOS backend
        sys_linux.c    Linux backend
    tests/
      test_pipe.c    unit tests for the filter engine
      run.sh         golden transcript runner
      golden/        *.in scripts and *.out expected transcripts

### Command tree

Commands are registered with a path string and a handler:

    cmd_register(M_SHOW, "show interfaces status", "Interface summary", do_status);

- `modes` is a bitmask (`M_EXEC`, `M_PRIV`, `M_CONFIG`, `M_CONFIG_IF`, `M_ALL`,
  `M_SHOW` for both EXEC modes). Each mode has its own root.
- In a path, `<name>` is a single free-form argument and `<text...>` consumes the
  rest of the line. A literal keyword always beats an argument node.
- Handlers have the signature
  `int handler(cli_ctx *ctx, int argc, const char *const *argv)`. Keywords in
  `argv` are expanded to canonical form; arguments are as typed.
- Handlers write only to `ctx->out`. Pipes work by pointing `ctx->out` at an
  in-memory buffer, filtering it, then printing the result.

### Platform layer

All OS-specific code is behind `src/sys/sys.h`, so command handlers contain no
`#ifdef`. `sys_darwin.c` and `sys_linux.c` are each wrapped in
`#ifdef __APPLE__` / `#ifdef __linux__` and the Makefile compiles every
`src/sys/*.c`.

| Data        | macOS                                              | Linux                                  |
|-------------|----------------------------------------------------|----------------------------------------|
| Interfaces  | `getifaddrs`, `ifmib` sysctl for 64-bit counters   | `getifaddrs`, `/sys/class/net/*`       |
| Routes      | `sysctl NET_RT_DUMP`                               | `/proc/net/route`, `/proc/net/ipv6_route` |
| ARP         | `sysctl NET_RT_FLAGS` + `RTF_LLINFO`               | `/proc/net/arp`                        |
| System info | `sysctlbyname`, `host_statistics64`                | `uname`, `/etc/os-release`, `/proc/meminfo`, `/proc/uptime`, DMI |
| Processes   | `sysctl KERN_PROC_ALL` + `proc_pidinfo`            | `/proc/<pid>/stat`                     |
| Users       | `utmpx` (shared)                                   | `utmpx` (shared)                       |
| Battery     | `ioreg -rn AppleSmartBattery`                      | `/sys/class/power_supply/*`            |
| Sensors     | none (needs privileged SMC access)                 | `/sys/class/thermal`, `/sys/class/hwmon` |
| System log  | `log show --last 10m`, else `/var/log/system.log`  | `journalctl`, else `/var/log/syslog`, `/var/log/messages` |

To add a data source, declare it in `sys.h`, implement it in both backends, and
call it from a handler in `src/cmds/`.

### Adding a command

1. Write a handler in the relevant `src/cmds/*.c` (or a new file).
2. Register it in that file's `cmds_register_*` function. A new file needs a
   declaration in `src/cmds/cmds.h` and a call in `main.c`.
3. Add a golden test (below).

## Testing

`make test` runs two layers:

- `build/test_pipe`: unit tests for filter parsing and application.
- `tests/run.sh`: feeds each `tests/golden/*.in` to `EOSH_HOSTNAME=sw1 ./eosh`
  and diffs against the matching `.out`. When stdin is not a terminal, the shell
  echoes the prompt and each command, so the output is a readable transcript.

Golden tests must be machine-independent: assert on header lines, counts and
error messages, not on live values such as addresses or process IDs.

## Known limitations

- Linux backends (`sys_linux.c`) compile-check was in progress: a gcc
  `-Wformat-truncation` error in `sys_common.c` was fixed by enlarging
  `sys_info.kernel` and `.arch`, but the full Linux build and `make test` have
  not yet been confirmed in a container. Treat Linux as untested.
- Output of a piped command is buffered until it finishes, so long-running
  commands (for example a future `ping`) need a path that bypasses the buffer.
- Tab completion does not complete filter names or interface names.
- `show interfaces` lists IPv6 addresses before IPv4.
- macOS has no serial number in `show version` (needs IOKit).

## Roadmap

1. CLI engine: modes, prompt, Tab, `?` - done
2. Pipe filters - done
3. `show` commands on a platform layer - done on macOS; `show history`, `ping`, `traceroute` pending
4. Simulated config mode: `hostname`, `interface`, `description`, `shutdown`,
   `ip address`, `ip name-server`, `show running-config`, `write memory` to
   `~/.eos-shell/startup-config`, `no` prefix - not started
5. Optional: apply config to the real system with confirmation (`ifconfig`,
   `networksetup`, `scutil` / `ip`, `hostnamectl`) - not started
6. Polish: persistent history, `terminal length` pager, `show tech-support` - not started
