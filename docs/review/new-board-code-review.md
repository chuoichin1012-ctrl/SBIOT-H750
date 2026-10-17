- **P2 — `stm32/firmware/tools/new_board.sh:89`: RDP failure still reports success.** `|| true` swallows programmer failures. If SWD disconnects or option-byte programming fails, the script reaches `done` with exit status 0. Preserve the command result and verify RDP before reporting completion.

- **P2 — `stm32/firmware/tools/new_board.sh:78`: the boot check accepts failed initialization.** The app banner precedes identity validation and store initialization. Logs containing `bind: ok`, the banner, and `app: store FAILED` pass this check and permit RDP. Require successful store mount and an application-ready marker; reject startup failures.

- **P2 — `stm32/firmware/tools/new_board.sh:58–68`: the advertised rework path can boot the wrong firmware.** `--erase` clears internal flash and QSPI, leaving SPI4 staging and factory intact. Old staging can overwrite the newly flashed app, while the generic banner check passes. The factory image also remains old despite line 67’s claim. Prepare SPI4 explicitly for rework, or reject that unsupported state, and verify the intended image after boot.

- **Minor — `stm32/firmware/tools/new_board.sh:64`: bind exit status is ignored.** `flash.sh` prints `bound to` before its final reset; if that reset fails, this wrapper still proceeds. Check the subprocess result as well as its output.

The provisioning order and destructive-action flags are otherwise correct. Shell syntax passed; failure handling was checked with harmless mocks. No board operations were performed.

**CHANGES NEEDED**