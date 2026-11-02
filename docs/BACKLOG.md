# Backlog

- **Diagnostics export (Settings → Diagnostics).** Opt-in "Save diagnostic report" writes one text file the user can read before sharing. Contents: Velora and Windows versions, monitor/DPI layout, shell and watchdog log tail, recovery events, gate-relevant timings. Never auto-sent and no upload code; the user attaches it to an issue themselves. Redact user names, paths outside Velora's own folders, window titles and tokens, and say that redaction is best-effort. Document it in a privacy page. Owner C (settings), log source A.

- **switch_gate first-shell-start flake.** Seen in 2 of ~10 CI runs: `dirty-marker` phase, "shell entered after recovery" (first full shell start of the run). Diagnostics in place since #47 (30 s start timeout, exit state and shell-log tail printed on failure). On the next occurrence, capture the exit state and log tail and fix. Owner A.
