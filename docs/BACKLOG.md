# Backlog

- **Diagnostics export (Settings → Diagnostics).** Opt-in "Save diagnostic report" writes one text file the user can read before sharing. Contents: Velora and Windows versions, monitor/DPI layout, shell and watchdog log tail, recovery events, gate-relevant timings. Never auto-sent and no upload code; the user attaches it to an issue themselves. Redact user names, paths outside Velora's own folders, window titles and tokens, and say that redaction is best-effort. Document it in a privacy page. Owner C (settings), log source A.

- **switch_gate first-shell-start flake: resolved in #58.** Root cause: the watchdog's heartbeat timeout was armed at hello, before a slow cold-start Enter() sent its first heartbeat. Kept for the record; regression phase `slow first heartbeat` in switch_gate.

- **About dialog as a glass panel.** Restyle the V menu > About dialog (now a system MessageBox) as a Velora glass panel: Dock material, original logo, version, licence link, rounded corners. Owner B.
