# binldr

`binldr` is a PS4 TCP and HTTP binary payload receiver, available as both a
standalone payload and a reusable static library. It accepts raw TCP uploads
or `POST /` and `POST /payload` requests. ELF and PS4 SELF payloads are
forwarded to the upstream `elfldr` service on port 9021. If that service is not
listening, `binldr` starts the upstream PS4 loader from its bundled BIN image.
Other binary formats run through a bundled BIN runner launched by elfldr.

The library exposes lifecycle and binary handling callbacks. Every ELF and
PS4 SELF is sent to elfldr on port 9021, which binldr starts from its bundled
upstream bootstrapper when needed. Other inputs, including legacy PS4 payload
BINs and flat BIN bootstrappers such as `elfldr-ps4.bin`, are staged briefly
and started by a bundled runner ELF through elfldr. The runner removes its
staging file after opening it. The standalone payload uses
elfldr's console/kernel log format for its startup lines and sends the matching
serving notification. Its listener defaults to port 9090 and accepts
`-p PORT`; the ELF loader port stays fixed at 9021.

## Build

Install the [payload SDK][sdk] and Git. Set the SDK path and run any of these root-level targets:

```sh
export PS4_PAYLOAD_SDK=/opt/ps-payload-sdk/ps4
make all
make clean
```

The build fetches [elfldr][elfldr] and [lib-ptrace][ptrace] into `deps/`, builds
the upstream PS4 loader, embeds its `elfldr-ps4.bin` bootstrap image, then builds
`build/binldr.elf` and `build/lib-binldr.a`.

To use existing checkouts:

```sh
make ELFLDR_DIR=~/src/elfldr PTRACE_DIR=~/src/lib-ptrace
```

## API

Include `binldr.h`, create a server with a nonzero listening port, then start
and stop it with `binldr_start` and `binldr_stop`. Link `build/lib-binldr.a`
and the PS4 system libraries. ELF and SELF requests are sent to `elfldr` on
fixed port 9021. `handle_binary` receives other uploads when provided;
otherwise, binldr starts them with its bundled runner through elfldr.

The listener port, maximum upload size, request timeout, payload callback, and
callback data are supplied through `binldr_config_t`.

## License

`binldr` is licensed under the GPLv3 or any later version. The bundled loader
image is built from the upstream GPL-licensed [elfldr][elfldr] source.

[sdk]: https://github.com/ps-payload-dev/sdk
[elfldr]: https://github.com/ps-payload-dev/elfldr
[ptrace]: https://github.com/ps-payload-dev/lib-ptrace
