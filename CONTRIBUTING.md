# Contributing

Build from your own supported game files using README.md. Changes to generated
files under src/recomp/gen are not committed: implement reproducible adaptations
in scripts/pipeline.py or its hook helpers. Toolkit changes go in numbered
patches against the pinned xboxrecomp revision. Preserve upstream notices.

Run the relevant tests, then the full CTest suite for runtime changes. Report
which platform and actual game behavior were exercised. Public CI cannot test
gameplay without private assets. Use a separate save directory for testing and
preserve normal progression. Keep game dumps, generated game code, binaries,
saves, recordings, credentials and personal paths out of commits and reports.

Contributions are submitted under GPL-2.0-or-later, subject to retained third-party
notices. Describe the concrete issue, resulting behavior and validation in PRs.
