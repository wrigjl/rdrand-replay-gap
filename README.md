# rdrand-replay-gap

Reproducibility artifact for:

> Jason L. Wright and Thomas Baldwin.
> **The Residual Nondeterminism Gap in Forensic Record-and-Replay.**
> In *19th International Workshop on Digital Forensics (WSDF 2026)*,
> co-located with ARES 2026, Linköping, Sweden, August 2026.

This repository contains the scripts, source files, container
recipes, and analysis notes used to produce the experimental
results in the paper.

## Status

Currently **private** while the camera-ready is being prepared.
Will be made public by the conference start date (24 August 2026).
The paper cites this repository as the reproducibility artifact.

## Repository layout

```
scripts/
    scan_rdrand.sh             — find RDRAND/RDSEED instructions
                                 in ELF binaries under given paths
    check_rdrand_gating.sh     — classify each hit as co-located
                                 CPUID, cached CPUID, or ungated

experiments/
    exp1-random-device/        — C++ std::random_device test program
                                 (entropy via libstdc++ dispatch)
    exp2-intrinsic/            — -mrdrnd intrinsic vs control build;
                                 demonstrates rr's CPUID suppression
                                 boundary (paper §4.2)
    exp3-silent/               — RDRAND-derived encryption key;
                                 demonstrates undetectable silent
                                 divergence (paper §5.1)
    exp4-openssl/              — OpenSSL RAND_bytes() and
                                 std::random_device under static
                                 vs dynamic linkage on AMD
                                 (paper §4.3, Table 3)
    exp-rust/                  — Rust target-feature=+rdrand
                                 reproduction
    mrdrnd/                    — minimal RDRAND emission test

prevalence/
    debian8/    debian9/    debian10/
    debian11/   debian12/   gentoo/
        Per-distribution Dockerfile and helper scripts to rebuild
        the binary corpus from which RDRAND-containing binaries
        were extracted.

notes/
    debian{8..12}.md           — per-release disassembly analysis
                                 of every RDRAND-containing binary
    gentoo.md                  — Gentoo -march=native analysis
    gated.md, gating_detail.txt — gating-mechanism classification
    amd-rdrand.md              — AMD CPUID-faulting-gap experiments
    experiments.md             — running experiment notes
```

## Paper-section / artifact map

| Paper claim                                  | Artifact location                        |
|----------------------------------------------|------------------------------------------|
| §3 RDRAND emits without CPUID under `-mrdrnd`| `experiments/exp2-intrinsic/`            |
| §3 Rust `target-feature=+rdrand` emits same  | `experiments/exp-rust/`                  |
| §4 Prevalence scan on Debian 13              | `scripts/scan_rdrand.sh`                 |
| §4 Gating classification                     | `scripts/check_rdrand_gating.sh`         |
| §4 Per-release Debian 8–12 confirmation      | `prevalence/debian{8..12}/`, `notes/`    |
| §4 Gentoo `-march=native` corpus             | `prevalence/gentoo/`, `notes/gentoo.md`  |
| §4.3 AMD CPUID-faulting gap                  | `experiments/exp1-random-device/`,       |
|                                              | `experiments/exp4-openssl/`,             |
|                                              | `notes/amd-rdrand.md`                    |
| §5.1 Silent divergence demo                  | `experiments/exp3-silent/`               |

## Reproducing the experiments

**System used for the paper:**

- Recording/replay host: AMD Ryzen 9 7940HS (Zen 4),
  Linux 6.12, glibc 2.41, gcc 14.2.0, rustc 1.85.0
- `rr` 5.9.0 (built from upstream)
- Container runtime: Docker 27.x

**Prevalence scan** (paper §4):

```bash
./scripts/scan_rdrand.sh /usr/bin /usr/sbin /usr/lib /usr/libexec /usr/share
./scripts/check_rdrand_gating.sh <binary-path>
```

To regenerate the per-release corpora used in the paper, build
each `prevalence/<distro>/Dockerfile` and run `scan_rdrand.sh`
inside. Debian 13 (Trixie) is intended to be scanned directly on
a host install; the older releases use containerized
filesystems.

**Replay-divergence experiments** (paper §4.2, §5.1):

Each `experiments/exp*/` directory contains a shell script that
builds the test program, records it under `rr`, and replays.
Example:

```bash
cd experiments/exp2-intrinsic/
./exp2-intrinsic.sh   # -mrdrnd build: replay diverges
./exp2-control.sh     # control build: replay faithful
```

## Known limitations of this artifact

- The Debian 13 (Trixie) corpus referenced in the paper's
  6,771-binary count is taken from a host installation; no
  separate Dockerfile is needed since current Debian binaries
  are readily available. The artifact's `prevalence/`
  Dockerfiles target the older releases that need archive
  repositories.
- `prevalence/debian11/` currently lacks a Dockerfile; the
  Debian 11 results were derived from an extracted binary
  corpus. Reproducing the Debian 11 scan requires building
  a Debian 11 image from `debian:bullseye` and running the
  scan scripts inside.
- Binary tarballs (the extracted RDRAND-containing binaries
  from each release) are intentionally not checked into this
  repo. They can be regenerated by running the scan scripts
  against a container of the corresponding release.

## Citation

```bibtex
@inproceedings{wright-wsdf-26,
  author    = {Jason L. Wright and Thomas Baldwin},
  title     = {The Residual Nondeterminism Gap in Forensic
               Record-and-Replay},
  booktitle = {19th International Workshop on Digital Forensics
               (WSDF 2026)},
  year      = {2026},
  publisher = {Springer},
  note      = {Co-located with ARES 2026},
}
```

## License

MIT (see `LICENSE`). Notes and Dockerfiles are provided under the
same terms.
