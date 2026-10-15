# Monocypher 4.0.2

Ed25519 signature check for the anti-clone binding (spec §11.5): `crypto_ed25519_check()` from
`monocypher-ed25519.c` (Ed25519 with SHA-512, RFC 8032), on top of `monocypher.c`.

- Source: https://monocypher.org/download/monocypher-4.0.2.tar.gz,
  SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`.
- Files: `src/monocypher.{c,h}`, `src/optional/monocypher-ed25519.{c,h}`, `LICENCE.md`, unchanged.
  They equal tag 4.0.2 on github.com/LoupVaillant/Monocypher except the version line.
- Licence: 2-clause BSD or CC-0, at our choice (`LICENCE.md`).
