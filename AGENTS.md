# ATbox Workspace Notes

## Layout

- `app/` contains the ATbox application project.
- `boot/` contains the ATbox bootloader project.
- Keep the root directory reserved for repository-level files such as `.git` and this `AGENTS.md`.
- `app/MDK-ARM/.git` is a nested Keil project repository; keep it intact.

## Keil Projects

- App project: `app/MDK-ARM/ATbox_V1.0.uvprojx`
- Boot project: `boot/MDK-ARM/ATbox_BootLoader.uvprojx`

## Flash Layout

- Boot region: `0x08000000-0x08007FFF`
- App start: `0x08008000`
- OTA info page: `0x0803F800`

## Working Rules

- Do not revert uncommitted user changes unless explicitly requested.
- Do not hand-edit generated build outputs unless the change comes from an intentional rebuild or the user asks for it.
- Keep app and boot OTA layout constants in sync when changing flash addresses.
