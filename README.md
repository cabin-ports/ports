# Cabin Foundation Ports

This repository holds the curated **foundation ports** of
[Cabin](https://github.com/cabinpkg/cabin), a package manager and build system for C and C++.  A
foundation port makes an existing C/C++ library that does not yet ship its own `cabin.toml`
available through Cabin: it pins the library's upstream source archive and describes how Cabin
builds it.  Each port is published to the Cabin registry as a `cabin-ports/<name>` package.

## Using foundation ports

You depend on a foundation port the way you depend on any registry package, by its scoped name:

```toml
[dependencies]
"cabin-ports/zlib" = "=1.3.1"
```

Consumers use the published registry packages, never the port definitions in this repository.
[cabinpkg.com](https://cabinpkg.com) lists the packages the hosted registry serves to everyone;
the registry is in private alpha, so it does not yet serve foundation ports to the public.  Cabin's
[Foundation ports](https://cabinpkg.com/docs/foundation-ports/) documentation explains how ports
are consumed and the rules a port meets.

## Repository structure

```
ports/<prefix>/<name>/<version>/  one port: cabin.toml, optional patches/ and tests/
Cargo.toml, src/, tests/          port-publish, the tool that checks and publishes the ports
publish-holds.txt                 port versions held back from automatic publishing
.github/workflows/                CI (ci.yml) and publishing (publish.yml)
```

`<prefix>` is the first two characters of the port's name, as in `ports/zl/zlib/1.3.1/`.

- `cabin.toml` is the manifest the port publishes: the package identity, the pinned upstream
  archive in `[package.upstream]`, and the Cabin targets that build the library.
- `patches/` holds the unified diffs the manifest declares, for the rare port that needs a small
  correction to its upstream source.
- `tests/` holds the port's consumer test, a small program built and run against the packaged
  port.  It is never published.
- `port-publish` stages every port from its upstream archive, builds and tests it, and publishes it
  to the registry.

## Contributing

Contributions of new ports and of fixes to existing ports and the tooling are currently limited only
to maintainers.  The catalog is curated rather than open to every library: each port is reviewed for
whether it fits Cabin's build model and follows the principles below.  Start with
[CONTRIBUTING.md](CONTRIBUTING.md).

Maintainers will find the CI and Cabin-revision procedures in
[docs/maintaining.md](docs/maintaining.md) and the publishing system in
[docs/publishing.md](docs/publishing.md).

## Porting principles

- **Stay faithful to upstream.**  A port preserves the library's functionality and intended build
  semantics.  Patches are limited to what Cabin needs to build it and, where a required notice is
  missing, one file of third-party notices, never feature changes.
- **Pin every source.**  Each port names its upstream archive by URL and SHA-256 checksum; floating
  references such as branches or `latest` are rejected.
- **Respect upstream licenses.**  A port keeps every license and attribution notice that
  redistribution requires, and adds one only where the package would otherwise lack it.
- **Prefer native support.**  A port is retired once its upstream project ships and maintains its
  own `cabin.toml`.

## License

`port-publish` is licensed under [Apache-2.0](LICENSE).  Each ported library remains under its own
upstream license.
