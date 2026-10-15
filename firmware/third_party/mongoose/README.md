# Mongoose 7.23

HTTP server, WebSocket server and console of the app (spec §10.1), over lwIP sockets.

- **Source:** https://codeload.github.com/cesanta/mongoose/tar.gz/refs/tags/7.23 (released 2026-08-12), with SHA-256 `93208f164038b05d156935b8b725063e1afb3984a362dbcf3a9ea37b6f1f2255`.
- **Files kept, unchanged:** `mongoose.c`, `mongoose.h`, `LICENSE`.
- **Licence:** GPLv2, or a commercial licence from Cesanta. The user was told when the stack was chosen.
- **Settings:** `app/mongoose_config.h`:
  - `MG_ARCH_ARMGCC` with FreeRTOS and lwIP sockets;
  - no TLS;
  - `mg_random` from the hardware RNG.
