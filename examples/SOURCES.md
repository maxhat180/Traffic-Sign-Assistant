# Stage 2 example sources

The tracked `tiny.ppm` is a synthetic 2-by-2 fixture with known RGB bytes.
The C tests also construct primary colors, black, white, neutral gray, and
lower-maximum-value fixtures to verify exact arithmetic and memory layout.

The optional demo selects these JPEGs from the user's local dataset:

| Output name | Local source under archive/car/test/images | Purpose |
| --- | --- | --- |
| speed30.ppm | 00001_00006_00026_png.rf.49d271a1ad59fd8fc3022b9d27fd55e2.jpg | Bright 30 sign with clear red border |
| speed50.ppm | 00002_00017_00026_png.rf.0e0c876d62e868e7b8bbea45c2854b90.jpg | Darker, blurrier 50 sign |

Dataset attribution: Self-Driving Cars, provided by a Roboflow user, CC BY 4.0,
as recorded in `archive/car/README.dataset.txt` and `README.roboflow.txt`.
Source: https://universe.roboflow.com/selfdriving-car-qtywx/self-driving-cars-lfjou

The local metadata is inconsistent about version numbering: its README says
Version 4 while `data.yaml` points to version 6. The exact filenames above
identify the samples used here. Converted samples are local-only and are not
redistributed in the repository. Conversion decodes JPEG to RGB PPM without
cropping; the demo then resizes from 416-by-416 to 208-by-208 and grayscales in C.
These samples are inspection examples, not a recognition benchmark or training set.
