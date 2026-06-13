# Chrome VAAPI Probe

This is an isolated experiment directory. It does not modify the Electron or
Chromium source trees.

The probe answers a narrow question: does the installed Google Chrome binary
reach the relevant libva capability or encode/decode submission path, or is the
VAAPI path absent or disabled before libva is queried?

## Build

```bash
cd probe
make
```

The shared object is generated at:

```text
bin/libvaapi_probe.so
```

`bin/`, `logs/`, and `profile*/` are generated artifacts and are ignored by
`.gitignore`.

Clean generated artifacts:

```bash
./clean.sh
```

## Static Scan

```bash
./static-chrome-scan.sh
```

Look for `H264VaapiVideoDecoderDelegate`, `H265VaapiVideoDecoderDelegate`,
`VAProfileH264*`, or `VAProfileHEVC*` strings. Missing H.265 strings are a
strong hint that HEVC cannot be fixed with a small flag or libva hook. H.264 is
more promising when `H264VaapiVideoDecoderDelegate` is present.

## Runtime Observe Mode

```bash
./run-chrome-probe.sh observe
```

Then open a real H.264 or HEVC video test page or local file in that Chrome
profile.
After closing Chrome:

```bash
./analyze-log.sh
```

## Runtime Injection Mode

```bash
./run-chrome-probe.sh inject
```

Injection mode appends `VAProfileHEVCMain` and `VAProfileHEVCMain10` to
`vaQueryConfigProfiles()` results and appends `VAEntrypointVLD` to HEVC
`vaQueryConfigEntrypoints()` results.

This does not add a missing Chrome decoder. It only tests whether Chrome has an
existing HEVC VAAPI path that is blocked by libva capability visibility.

## Useful Environment Variables

- `CHROME=/path/to/chrome`
- `CHROME_VAAPI_PROBE_LOG_ALL_PROFILES=1`
- `CHROME_VAAPI_PROBE_PROFILE=/tmp/some-profile`

## Expected Interpretation

- `vaCreateConfig profile=6/7/13 ...`: Chrome reached H.264 VAAPI config
  creation.
- `vaCreateConfig profile=17/18 ...`: Chrome reached HEVC VAAPI config creation.
- HEVC entrypoint queries but no create: a later capability or attribute check
  is blocking.
- libva queried but no HEVC entrypoint query: Chrome likely is not enabling or
  does not contain the HEVC VAAPI decode path.
- no libva calls: the test did not exercise the VAAPI decode path, or preload
  did not reach the relevant Chrome process.
