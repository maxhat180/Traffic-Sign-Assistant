#ifndef TRAFFIC_SIGN_RECOGNIZER_H
#define TRAFFIC_SIGN_RECOGNIZER_H

#include "detect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Recognizer Recognizer;

typedef struct {
    unsigned int speed;           /* Accepted speed, or zero when unknown. */
    unsigned int predicted_speed; /* Winning class before thresholding. */
    unsigned int confidence; /* Winning tree-vote share, per mille. */
    bool known;
} Recognition;

/* Load a serialized OpenCV RTrees model. min_confidence is 0..1000. */
Recognizer *recognizer_load(const char *path, unsigned int min_confidence,
                            const char **error);
void recognizer_destroy(Recognizer *recognizer);

bool recognizer_predict(const Recognizer *recognizer, const Frame *source,
                        const Candidate *candidate, Recognition *out,
                        const char **error);

/* Save prefix-recognition.csv for every candidate. Unknown rows use speed 0,
 * while predicted_speed retains the winning class for temporal aggregation.
 */
bool recognition_save(const Recognizer *recognizer, const Frame *source,
                      const Detection *detection, const char *prefix,
                      size_t *known_count, const char **error);

#ifdef __cplusplus
}
#endif

#endif
