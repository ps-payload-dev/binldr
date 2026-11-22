# binldr

`binldr` is a PS4 TCP and HTTP receiver for binary payloads. It accepts raw TCP
uploads and HTTP `POST /` or `POST /payload` requests. ELF and SELF files are
forwarded to `elfldr` on port 9021, except dependency-free legacy ELF payloads,
which are laid out and started by the bundled runner. If `elfldr` is not
running, binldr starts it from its bundled bootstrap image. Raw BIN payloads
and BIN bootstrappers are started by the bundled runner.

The standalone payload is a launcher. It starts `elfldr` if needed and hands it
the server, which then runs as a process of its own (`binldr.elf`) listening on
port 9090, so it can be killed without touching the process that sent the BIN
or ELF, such as a jailbreak daemon. BIN payloads already run in their own
process, spawned by `elfldr`. Port 9021 is reserved for `elfldr`. The server
takes `-p PORT` when it is run in-process through the library.

`binldr` is not a port of a project by [John Törnblom](https://github.com/john-tornblom),
but it builds on his work. It bundles an ELF loader built from [elfldr][elfldr],
which combines his [PS4 elfldr project][ps4-upstream] and
[PS5 elfldr project][ps5-upstream], and it builds with the unified
[payload SDK][sdk].

## Building

`binldr` currently targets the PS4. Install the [payload SDK][sdk] and Git on a
Debian-based POSIX system, then set the SDK path and build:

```console
sudo apt-get install git
export PS4_PAYLOAD_SDK=/opt/ps-payload-sdk/ps4
make PLATFORM=ps4
```

The build fetches [elfldr][elfldr] and [lib-ptrace][ptrace] into `deps/`, builds
the PS4 `elfldr-ps4.bin` bootstrap image, and produces:

- `build/binldr.elf` is the standalone server payload.
- `build/binldr.bin` is a BIN bootstrapper containing the standalone payload.
- `build/lib-binldr.a` is a static library for use by other payloads.

Object files are written to `build/objects/`. Clean with
`make PLATFORM=ps4 clean` and format the sources with `make PLATFORM=ps4 format`.

To use existing dependency checkouts, set `ELFLDR_DIR` and `PTRACE_DIR`:

```console
make PLATFORM=ps4 ELFLDR_DIR=~/src/elfldr PTRACE_DIR=~/src/lib-ptrace
```

One GitHub Actions workflow builds the PS4 outputs with the [payload SDK][sdk]
and uploads them together as the `binldr-ps4` artifact.

## Run

Start a PS4 ELF loader on port 9020, then send the BIN bootstrapper:

```console
PS4_HOST=ps4
PS4_PORT=9020
nc -q0 "$PS4_HOST" "$PS4_PORT" < build/binldr.bin
```

The server then listens on port 9090. Send a binary payload over raw TCP:

```console
nc -q0 "$PS4_HOST" 9090 < payload.bin
```

Or upload it with HTTP:

```console
curl --data-binary @payload.bin "http://$PS4_HOST:9090/payload"
```

Modern ELF and SELF uploads go to elfldr on port 9021. Legacy ELF and raw BIN
uploads are started by the bundled runner. The runner uses the same entry
thread and sysmodule setup as nexHEN's PS4 BIN runner. The HTTP status endpoint
is available at `/`. Raw BIN files must be flat x86-64 payload images whose
entry point is at byte zero, as produced by the PS4 payload SDKs; custom
containers or encrypted formats need a format-specific callback.

## Library

Include `binldr.h`, create a server with a `binldr_config_t`, then call
`binldr_start` and `binldr_stop`. The configuration sets the listening port,
maximum upload size, request timeout, and optional callback for handling binary
formats other than ELF and SELF. Link `build/lib-binldr.a` with the required PS4
system libraries. Before starting the server, `binldr_set_sysmodules` can be
used to provide public IDs (`sceSysmoduleLoadModule`) or internal IDs
(`sceSysmoduleLoadModuleInternal`) required by the BIN payloads it will receive.
If any requested module fails to load, binldr does not enter that payload. The
runner's built-in common-module preloads remain best-effort for firmware and
payload compatibility. For example, pass `BINLDR_SYSMODULE_MESSAGE_DIALOG`
with `internal` set to `0`, or `BINLDR_SYSMODULE_INTERNAL_NET_CTL` with
`internal` set to `1`.

```c
binldr_sysmodule_t modules[] = {
    { BINLDR_SYSMODULE_MESSAGE_DIALOG, 0 },
    { BINLDR_SYSMODULE_INTERNAL_NET_CTL, 1 },
};
binldr_set_sysmodules(server, modules, sizeof(modules) / sizeof(modules[0]));
```

## Reporting Bugs

If you encounter problems with binldr, please [file a github issue][issues].
If you plan on sending pull requests which affect more than a few lines of
code, please file an issue before you start to work on your changes. This will
allow us to discuss the solution properly before you commit time and effort.

## License

`binldr` is licensed under the GPLv3 or any later version. The bundled ELF
loader image is built from [elfldr][elfldr].

[sdk]: https://github.com/ps-payload-dev/sdk
[elfldr]: https://github.com/ps-payload-dev/elfldr
[ptrace]: https://github.com/ps-payload-dev/lib-ptrace
[ps4-upstream]: https://github.com/ps4-payload-dev/elfldr
[ps5-upstream]: https://github.com/ps5-payload-dev/elfldr
[issues]: https://github.com/ps-payload-dev/binldr/issues/new
