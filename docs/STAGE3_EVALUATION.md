# Stage 3: candidate detector evaluation

## Scope and method

This is a small engineering baseline, not a benchmark of driving performance.
The C detector locates possible red-bordered speed signs; it does not read digits.
All inputs are 416x416 images from the user's local `archive/car` dataset.
Attribution and the dataset's version-label ambiguity are in
[examples/SOURCES.md](../examples/SOURCES.md).

`tools/evaluate_detection.ps1` selects the first three filenames in sorted order
from each of four strata in each split. Strata are based on the largest speed-sign
annotation: area below 2% of the frame, 2% to below 20%, at least 20%, or no
speed-sign annotation. Speed classes are dataset class IDs 2..13. Negatives here
include stop-sign closeups; they are not a comprehensive sample of road backgrounds.

Training selection uses `train`, held-out selection uses `valid`. The defaults
were frozen before running validation. This does not prove scene independence:
the dataset contains related closeups, and no sequence-level split audit was done.

The evaluator collapses speed-sign annotations with IoU >= 0.9 into one location,
regardless of speed class. One training image had two conflicting labels on the
same physical sign. Raw duplicate counts remain in the per-image metrics and
manifest. Predicted boxes are matched one-to-one to these locations greedily in
descending IoU order at IoU >= 0.5. IoU is intersection area divided by union area.
Unmatched predictions count as false candidates; unmatched labels count as misses.
No matching uses padded crop extents, and there is no class recognition scoring.

## Training-only threshold selection

Brightness 40/255, red ratio 130%, maximum aspect 2.0 and maximum fill 80% stayed
fixed. Isolated-pixel removal and 3x3 closing were enabled throughout.

| Min area | Min side | Min fill | Matched locations | Extra candidates / 12 images |
| ---: | ---: | ---: | ---: | ---: |
| 12 | 5 | 6% | 7 | 92 |
| 40 | 8 | 10% | 7 | 47 |
| 80 | 12 | 15% | 7 | 26 |
| 120 | 12 | 15% | 7 | 24 |

The final row became the defaults. Early runs counted the duplicated label as
another miss; deduplication changed the denominator from 10 to 9 locations and
did not change the seven matches or false-candidate counts.

## Frozen-setting results

| Split | Images | Sign locations | Matched | Missed | Recall | Extra candidates | Extras/image |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Training subset | 12 | 9 | 7 | 2 | 77.8% | 24 | 2.00 |
| Validation subset | 12 | 10 | 7 | 3 | 70.0% | 21 | 1.75 |

Validation by stratum: small signs 4/4 locations found, medium 2/3, large 1/3.
The three negative images produced ten false candidates in total. These results
do not establish general accuracy, and larger or sequence-separated evaluation
is still required. Timing CSVs include process startup and writing masks/crops,
so they are not isolated detector runtime measurements.

## Visually inspected behavior

- `000062_jpg.rf.cc95cfe9c2ec51e7e600b32dad431408` (validation) has a small
  30 sign; its candidate closely bounds the sign.
- `000002_jpg.rf.d65ebeef4d1cb26e3fc1a826770b729f` (validation) shows a strongly
  oblique 30 sign. The detector misses it; the aspect-ratio rule is a limitation
  for this geometry. Thresholds were not retuned after seeing this validation miss.
- `00000_00000_00002_png.rf.109f031ac8e60eba952da43b054389c0` (validation) is
  heavily pixelated and has a second red sign immediately above the speed sign.
  The resulting box merges them and does not reach IoU 0.5 with the speed-sign label.
- Training image `000000_jpg.rf.b11f308f16626f9f795a148029c46d10` retains its
  30 sign but also produces boxes in vegetation and other background regions.

## Reproduce and inspect

```powershell
.\build.ps1 -Test
.\tools\evaluate_detection.ps1 -Split train
.\tools\evaluate_detection.ps1 -Split valid
```

Outputs are in `output/detection-{split}-a120-s12-f150/`: `manifest.json`,
`metrics.csv`, `summary.json`, and per-image masks, boxes, CSVs, and crops.
Use `tools/preview_ppm.ps1` to convert any canonical detector PPM into a PNG for
inspection. Use the latest CSV to enumerate crops; stale crops from earlier runs
are not deleted. Converted source PPMs are cached under `examples/local/`; remove
or regenerate a cached input if its original JPEG changes.

Core synthetic tests remain dataset-independent. They verify exact masks,
bounding boxes, connectivity, filtering, memory cleanup, output crop pixels,
and failure behavior. They demonstrate implementation correctness, not real-world
detection accuracy.

## C++ / OpenCV transition

The recommended transition is before Stage 4 recognition. Keep the C detector
as a regression baseline and add a C++ adapter for JPEG/PNG loading and geometric
normalization. Convert OpenCV BGR to the existing RGB representation explicitly.
OpenCV provides [image codecs](https://docs.opencv.org/4.x/d4/da8/group__imgcodecs.html)
and [geometric transforms](https://docs.opencv.org/4.x/da/d54/group__imgproc__transform.html).
Digit recognition still requires choosing and evaluating a recognition method;
adopting OpenCV alone does not supply a reliable speed-sign recognizer.
