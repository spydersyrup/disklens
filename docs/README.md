# DiskSonar Developer Documentation

Welcome to the DiskSonar developer documentation. This directory provides detailed guides covering build configurations, internal architecture, design tokens, and packaging pipelines.

---

## Guides and References

- **[Building and testing](dev/building.md)**: System requirements, toolchains, build targets (`make linux`, `make windows`, `make deb`, `make installer`), and the built-in self-test verification suite.
- **[Architecture](../ARCHITECTURE.md)**: Deep dive into the flat arena memory model, parallel work-stealing scanner, 9 visualization algorithms, and export security guards.
- **[Design System](DESIGN.md)**: Color palettes, contrast ratings (WCAG AA compliant), typography, layout dimensions, and tactile utility design tokens.
- **[Distribution & Packaging](DISTRIBUTION.md)**: Specifications for Debian packages (`.deb`), tarballs, Windows Inno Setup installer (`.iss`), and resource scripts.
- **[Contributing Guidelines](../CONTRIBUTING.md)**: Code style, author standards, zero em dash hygiene rules, and pull request procedures.
- **[Security Policy](../SECURITY.md)**: Vulnerability disclosure policies, formula injection guards, and local offline privacy assurances.
- **[Changelog](../CHANGELOG.md)**: Detailed version history and release notes.
