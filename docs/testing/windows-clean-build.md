# Windows clean-build record

## Required command sequence

The project is deliberately checked from a clean target/configuration state:

```powershell
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Flash and monitor are separate HIL operations:

```powershell
idf.py -p COMx flash
idf.py -p COMx monitor
```

## Current platform requirement

Use a clean checkout of the official `v6.0.1` tag at commit
`8c19b156084a0753687347cca1f5355782893533` and its matching installed tools
and Python environment. `IDF_PATH`, `IDF_TOOLS_PATH`, and
`IDF_PYTHON_ENV_PATH` must all describe that same environment. The normal
Espressif `export.ps1` flow is preferred.

This Windows machine requires the process-local `PROCESSOR_ARCHITECTURE=AMD64`
setting because the inherited shell value is empty and ESP-IDF otherwise
detects the unsupported platform string `Windows-`. Do not make a registry or
global-locale change for this workaround. If invoking `idf.py` directly is
needed, set the process locale to `English_United States.utf8` only for that
process, as shown by the recorded command in the final verification entry.

## Historical Callbox environment evidence

`callbox_sews` recorded a previous Windows build with `IDF_PATH` set to
`C:\Espressif\v6.1-dev\esp-idf`, `IDF_TOOLS_PATH=C:\Espressif\tools`, a
Python environment under `C:\Espressif\tools\python\v6.1-dev\venv`, target
`esp32s3`, and an IDF package lock of `6.1.0`. Its build outputs are only
environment clues, never build evidence for this project.

That checkout reported itself as `ESP-IDF v6.1-dev`, not a release tag. Its
2026-08-05 compilation (application image `0x388a0`) remains historical only;
it must not be substituted for a v6.0.1 clean build or for a CI result.

## Recorded Phase 0-1 compilation — 2026-08-06

The root checkout was at tag `v6.0.1`, commit
`8c19b156084a0753687347cca1f5355782893533`. Tools and Python packages were
installed under `C:\Espressif\tools-v6.0.1`:

- Python `3.11.15`:
  `C:\Espressif\tools-v6.0.1\python_env\idf6.0_py3.11_env\Scripts\python.exe`
- Xtensa `esp-15.2.0_20251204`, CMake `4.0.3`, Ninja `1.12.1`, and ROM ELFs
  `20241011`

The normal shell was not exported. For the one build process, `IDF_PATH`,
`IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH`, `ESP_IDF_VERSION=6.0.1`,
`PROCESSOR_ARCHITECTURE=AMD64`, `ESP_ROM_ELF_DIR`, `PYTHONUTF8=1`, and the
matching tool directories on `PATH` were set. The runner set the Python locale
to `English_United States.utf8` only in that process. Because a stale ignored
`build/` directory could not be cleanly removed by `idf.py`, the build output
was isolated outside the repository:

```powershell
idf.py -B C:\Espressif\build-v6.0.1-phase01-final set-target esp32s3
idf.py -B C:\Espressif\build-v6.0.1-phase01-final fullclean
idf.py -B C:\Espressif\build-v6.0.1-phase01-final build
```

Result: **passed** (CMake configure, Ninja compile, and link), producing the
single application firmware `tunglam_esp32_s3_platform.bin`, 217,776 bytes
(`0x352b0`). Image inspection reports ESP32-S3, 16 MB Flash, and 80 MHz.

This is intentionally not labelled pristine release verification: the first
shallow checkout had an incomplete `esp_wifi/lib` submodule, and CMake warned
that `protobuf-c`, `spiffs`, and `unity` submodules were out of date. As a
result `idf.py --version` reported `ESP-IDF v6.0.1-dirty`, despite the root
being at the stated v6.0.1 tag/SHA. The exact `espressif/idf:v6.0.1` CI job
must pass before release-parity or CI verification is claimed.

Generated `build/` and `sdkconfig` remain ignored and are not part of source
changes. Never substitute cached old Callbox artefacts for this check.
