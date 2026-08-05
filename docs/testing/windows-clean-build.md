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

## Reference environment discovered

`callbox_sews` recorded a previous Windows build with `IDF_PATH` set to
`C:\Espressif\v6.1-dev\esp-idf`, `IDF_TOOLS_PATH=C:\Espressif\tools`, a
Python environment under `C:\Espressif\tools\python\v6.1-dev\venv`, target
`esp32s3`, and an IDF package lock of `6.1.0`. Its build outputs are only
environment clues, never build evidence for this project.

## Recorded Phase 0-1 attempt — 2026-08-05

The exact installed checkout identified itself as `ESP-IDF v6.1-dev`, not the
release tag `v6.1.0`; therefore this verifies compilation against the local
v6.1-dev checkout only. The CI workflow is separately pinned to the release
image `espressif/idf:v6.1.0` and has not yet run remotely.

Observed local tools:

- Python `3.11.15`: `C:\Espressif\tools\python\v6.1-dev\venv\Scripts\python.exe`
- CMake `4.0.3`, Ninja `1.12.1`
- Xtensa toolchain `esp-15.2.0_20250929`
- `ESP_ROM_ELF_DIR=C:\Espressif\tools\esp-rom-elfs\20241011`

The normal shell was not exported: `idf.py` was absent from `PATH`,
`PROCESSOR_ARCHITECTURE` was empty (the IDF tool detector reported
`Windows-`), and Python's startup locale was `English_United States/1252`.
`export.bat` also expected a missing default Python environment. No registry
or global locale setting was changed. For this one process, the following
environment was used:

```powershell
$env:IDF_PATH = 'C:\Espressif\v6.1-dev\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v6.1-dev\venv'
$env:PROCESSOR_ARCHITECTURE = 'AMD64'
$env:ESP_ROM_ELF_DIR = 'C:\Espressif\tools\esp-rom-elfs\20241011'
$env:PYTHONUTF8 = '1'
$env:Path = 'C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20250929\xtensa-esp-elf\bin;C:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20250929\riscv32-esp-elf\bin;C:\Espressif\tools\idf-exe\1.0.3;' + $env:Path

function idf.py {
  & 'C:\Espressif\tools\python\v6.1-dev\venv\Scripts\python.exe' -X utf8 -c "import locale, os, runpy, sys; path=r'C:\Espressif\v6.1-dev\esp-idf\tools\idf.py'; locale.setlocale(locale.LC_ALL, 'English_United States.utf8'); sys.path.insert(0, os.path.dirname(path)); sys.argv=[path]+sys.argv[1:]; runpy.run_path(path, run_name='__main__')" @args
}

idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Result: **passed** after a build-safe source correction for the IDF 6.1 empty
`rmt_copy_encoder_config_t`. The project generated one application firmware,
`tunglam_esp32_s3_platform.bin`, size `0x388a0`; its sole app partition is
`0xff0000`, and generated flash arguments specify ESP32-S3, DIO, 16 MB Flash,
and 80 MHz. Generated `build/` and `sdkconfig` remain ignored and are not part
of this source change.

## Remaining environment limitation

The local installed IDF checkout is `v6.1-dev`. A clean Windows build against
the exact ESP-IDF `v6.1.0` release is still required before claiming release
parity. Do not substitute cached old Callbox artefacts for that check.
