# Robot to Codex
- This is the public, standalone Windows desktop source. Do not add private firmware backups, runtime data, credentials or machine-specific probes.
- Preserve MIT upstream copyright and third-party notices.
- Keep product version 0.2.1 during development unless the owner explicitly requests a release change.
- UI is robot-only: no computer microphone, floating capsule, global hotkeys or Codex authentication reads.
- Runtime uses paired TLS LAN transport. MCP/serial code is retained for compatibility tests only; do not enable it in the desktop UI.
- New robots use the cvi-robot wire profile. The esp-sparkbot profile remains accepted for existing firmware.
- Preserve physical-action-only expiring Security2 pairing, per-turn authorization, explicit send/exit commands, pause-on-failure and no replay.
- Build on Windows: ./build.ps1; isolated tests: ./scripts/test.ps1. Clipboard integration tests are opt-in and overwrite the clipboard.
- Update docs/PROTOCOL.md and docs/ROBOT_INTEGRATION.md with protocol changes. Do not claim hardware acceptance for untested boards.
