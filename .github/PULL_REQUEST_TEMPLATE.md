## Description

<!-- Brief description of what this PR does -->

## Related Issue

<!-- Link to related issue, e.g. Closes #123 or Fixes #456 -->

## Type of Change

- [ ] Bug fix (non-breaking change fixing an issue)
- [ ] New feature (non-breaking change adding functionality)
- [ ] Breaking change (fix or feature causing existing functionality to change)
- [ ] Documentation update
- [ ] CI/Workflow change
- [ ] Refactoring (no functional changes)
- [ ] Other:

## Component

- [ ] MCU Firmware (`app_example/`)
- [ ] LCD Driver (`app_example/drv/lcd/`)
- [ ] PC Collector (`PC/`)
- [ ] CI/Workflows (`.github/workflows/`)
- [ ] Documentation
- [ ] Other:

## How Has This Been Tested?

<!-- Describe the tests you ran -->

- [ ] CI build passed
- [ ] Tested on hardware

### Hardware Setup (if applicable)

| Item | Value |
|------|-------|
| Board | RTL8721F EVB |
| Display | ST7262 800x480 / DBL070 |
| SDK Version | v1.2 |

## Checklist

- [ ] Code follows project style (Allman brackets, 4-space indent)
- [ ] Format specifiers compliant (`%d`/`%i`/`%s`/`%c`/`%p` only)
- [ ] `uint32_t` uses `%d` with `(int)` cast
- [ ] No `sudo chmod` in build scripts
- [ ] README / README_CN updated (if needed)
