#ifndef TRAFFIC_SIGN_DETECT_H
#define TRAFFIC_SIGN_DETECT_H

#include "frame.h"

typedef struct {
    unsigned int red_min;       /* Brightness normalized to 0..255. */
    unsigned int red_ratio;     /* R/G and R/B minimum, percent. */
    size_t min_area;            /* Connected foreground pixel count. */
    size_t min_side;
    unsigned int max_aspect;    /* max(width/height,height/width), per mille. */
    unsigned int min_fill;      /* Foreground / bounding box area, per mille. */
    unsigned int max_fill;
    bool cleanup;
} DetectorConfig;

typedef struct {
    size_t x, y, width, height, area;
} Candidate;

typedef struct {
    Frame raw_mask;
    Frame clean_mask;
    Candidate *boxes;
    size_t count;
    size_t components;
} Detection;

#ifdef __cplusplus
extern "C" {
#endif

DetectorConfig detector_defaults(void);
/* Source is borrowed, valid packed RGB. out must be initialized to {0}.
 * On success out owns its masks and candidate array. On failure out is unchanged.
 * error must be non-NULL; diagnostic strings are static, never freed by caller.
 */
bool detect_signs(const Frame *source, const DetectorConfig *config,
                  Detection *out, const char **error);
void detection_destroy(Detection *detection);
/* Save prefix-raw.ppm, -clean.ppm, -boxes.ppm, -boxes.csv, -crop-N.ppm.
 * Parent directory must exist. Existing outputs are overwritten. Old crops from
 * earlier runs are not removed: CSV is authoritative for the current run.
 * Failure may leave partial outputs. source/detection must refer to the same image.
 */
bool detection_save(const Frame *source, const Detection *detection,
                    const char *prefix, const char **error);

#ifdef __cplusplus
}
#endif

#endif
