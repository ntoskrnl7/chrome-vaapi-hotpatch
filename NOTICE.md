# Notice

This repository contains experimental Linux VAAPI tooling for Chromium-family
binaries. It includes small local utilities plus source excerpts and adapter
code used to build the HEVC delegate injection experiment.

Some files under `h264-h265/delegate-injection/` are derived
from Chromium media/VAAPI sources or are designed to compile against a Chromium
build tree. Keep upstream Chromium license notices intact when updating those
files. See `LICENSE` for the repository license and Chromium-derived file
scope.

No downloaded Chrome packages, patched Chrome binaries, temporary browser
profiles, runtime logs, or generated shared objects are intended to be tracked
in git.
