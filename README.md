# Traffic Sign Assistant

A C-first project toward detecting speed-limit signs from dashcam video.
Milestone 1 is implemented: load one binary PPM image into owned RGB memory.
Video decoding and sign detection are future milestones.

See [PROGRESS.md](PROGRESS.md) for completed work and the proposed roadmap.

## Build and run

Requires GCC or Clang with C17 support. No image libraries are needed.
From PowerShell in this directory (GCC must be on PATH):

```powershell
.\build.ps1 -Test
.\traffic_sign_assistant.exe .\examples\tiny.ppm
```

Or build directly:

```sh
gcc -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror main.c frame.c -o traffic_sign_assistant.exe
gcc -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror tests/test_frame.c frame.c -o test_frame.exe
```

On Linux/macOS, use the same commands with `cc` and run the executables using
`./traffic_sign_assistant.exe` and `./test_frame.exe` (the suffix is optional).
Quote input paths containing spaces. Successful runs return 0; usage, file,
format, allocation, and output failures return a nonzero status.

The included 2-by-2 fixture has these RGB values in row order:
`(82,71,66)`, `(49,50,51)`, `(97,98,99)`, `(100,101,102)`.
Its binary samples happen to be printable ASCII bytes. The final newline is
trailing data, not part of the four pixels.

## Supported input

- P6 signature, positive decimal width and height, maximum color value 1..255.
- Header whitespace and `#` comments before header numbers.
- Exactly one whitespace byte after the maximum value, followed by packed RGB.
- One byte per channel; samples retain their original values without scaling.
- Only the first frame is loaded; remaining bytes are left unread.
- P3 text images and 16-bit samples are rejected with an error.

The raster boundary follows the [Netpbm PPM specification](https://netpbm.sourceforge.net/doc/ppm.html).
Use a single LF after the maximum value. A CRLF pair there is not treated as a
single delimiter: CR ends the header and LF is the first pixel byte. This
preserves valid rasters whose first channel happens to be a newline. CRLF
between earlier header fields is supported. Do not let a text editor rewrite
line endings in binary PPM files.

## Code and memory layout

`main.c` owns the file stream, prints metadata and up to five pixels, and frees
the loaded frame. `frame.c` parses the header, checks numeric and allocation
overflow, allocates the raster, verifies the read and sample range, and cleans
up on failure. `frame.h` documents the loader's ownership contract.

`Frame frame = {0}` creates metadata and a null pixel pointer. A successful
load gives `frame.pixels` a separate heap allocation of `width * height * 3`
bytes. Pixel `(x, y)` begins at `(y * width + x) * 3`; the next two bytes hold
green and blue. The pixel buffer remains valid after closing the input file.
`frame_destroy` frees that allocation and resets all fields. Call it before
reusing a frame for another load; never shallow-copy a frame and free both copies.

There is no arbitrary image-size cap; available memory and `size_t` bound the
allocation. Allocation failure is reported. Applications reading untrusted
large inputs may additionally want an application-specific size limit.

## Verification

`tests/test_frame.c` creates temporary binary fixtures and checks exact pixel
bytes, row order, header comments, whitespace-valued first channels, consecutive
frames, cleanup/reset behavior, truncated input, invalid numbers, unsupported
color depth, sample ranges, and arithmetic overflow. `build.ps1 -Test` also
checks the CLI output against the included image. Tests exclusively create
`frame-test.tmp` in the working directory and remove it after each case; an
existing file of that name is never overwritten. Build and test use the same
strict compiler warnings.
