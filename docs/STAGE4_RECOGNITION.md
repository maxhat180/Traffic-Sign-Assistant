# Stage 4 recognition evaluation

## Method

The recognizer is an OpenCV `RTrees` classifier trained on speed-sign boxes from
`archive/car`. Classes 2 through 13 map to speeds 10, 100, 110, 120, 20, 30, 40,
50, 60, 70, 80, and 90. Exact or near-exact duplicate boxes are collapsed at
IoU 0.9; a location with conflicting speed labels is discarded. This leaves
2,853 training crops. The trainer fixes OpenCV's RNG seed and trains 100 trees.

Each sample is the equalized grayscale interior of a labeled or detected box,
resized to 20 by 20 and flattened to 400 values. The model returns tree votes.
The highest-vote speed is accepted only when its share is at least 0.60; lower
scores become unknown. This threshold was selected on validation behavior, not
test performance.

Train a model and reproduce the crop-level report with:

```powershell
.\output\cmake-opencv\traffic_sign_train_recognizer.exe `
  .\archive\car .\output\speed-recognizer.yml
```

The generated model is about 10 MB and remains under ignored `output/`; it is
not a portable pretrained artifact committed to the repository.

## Crop-level results

These results use labeled boxes, so they measure recognition independently of
the Stage 3 detector.

| Split | Crops | Raw accuracy | Accepted at 0.60 | Coverage | Accepted accuracy |
| --- | ---: | ---: | ---: | ---: | ---: |
| Train | 2,853 | 100.0% | 2,770 | 97.1% | 100.0% |
| Validation | 628 | 90.8% | 409 | 65.1% | 100.0% |
| Test | 513 | 85.2% | 293 | 57.1% | 99.0% |

The test split has three accepted mistakes. The conservative threshold greatly
improves reliability at the cost of returning unknown on about 35% of validation
and 43% of test crops. Speed 10 is particularly underrepresented: only 19 usable
training crops and three test crops are available.

## End-to-end frozen sample

`tools/evaluate_recognition.ps1` selects three deterministic images in each of
four strata (small, medium, large, and no labeled speed sign), runs detection and
recognition, and greedily matches candidates to labels at IoU 0.5. It reports
unmatched candidates separately, because crop-level scores alone do not measure
false detector regions.

| Split | Signs | Localized | Accepted correct | Accepted wrong | Localized unknown | False known | False unknown |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Validation | 10 | 7 | 3 | 0 | 4 | 0 | 21 |
| Test | 9 | 5 | 2 | 0 | 3 | 0 | 31 |

All accepted localized predictions in this small frozen sample are correct, and
all 52 unmatched detector candidates are rejected. End-to-end correct rates are
only 30.0% on validation and 22.2% on test because the detector misses signs and
the recognizer deliberately rejects uncertain localized crops. These 12-image
samples are regression checks, not statistically strong performance estimates.

Reproduce them with:

```powershell
.\tools\evaluate_recognition.ps1 -Split valid
.\tools\evaluate_recognition.ps1 -Split test
```

Reports are written below `output/recognition-<split>-c600/` as a manifest,
per-image metrics, summary JSON, detector artifacts, and recognition CSV files.

## Limitations and next improvements

- The detector remains the largest end-to-end bottleneck and produces many
  visually unrelated candidates.
- Recognition uses an axis-aligned inner crop. Perspective normalization exists
  in the adapter, but automatic sign-corner estimation is not yet connected.
- Tree-vote share is a rejection score, not a calibrated probability.
- The source dataset is imbalanced, region-specific, and too small for strong
  claims about real dashcam reliability.
- Later work should add harder regional data, corner normalization or a learned
  detector, a larger frozen benchmark, and temporal evidence from Stage 5/6.
