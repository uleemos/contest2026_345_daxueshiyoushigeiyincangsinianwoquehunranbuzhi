# TinyPose real-keypoint training and quality plan — 2026-09-19

## Fixed contract

- Eight points, in order: left/right shoulder, left/right hip, left/right knee,
  left/right ankle.
- Each output is `{x, y, confidence}` in normalized image coordinates.
- The model remains the measured 36,080-parameter network: five stride-2
  Conv2D layers, Flatten, and FullyConnected 24.
- Input and output are fully INT8. Conversion rejects any operator list other
  than five `CONV_2D`, `RESHAPE`, and `FULLY_CONNECTED`.
- Training images stay under `.secrets/tinypose-keypoints/`.

## Dataset protocol

The SC2336 path captures 192x192 RGB frames. Host training downsamples them to
96x96 with area resampling, matching the eventual model input. Data is split by
capture session rather than adjacent frame, so nearly identical frames cannot
appear in both training and acceptance sets.

Initial collection target:

1. Neutral standing and shallow squat transitions: 40 usable frames.
2. Full squat cycles with varied speed and depth: 80 usable frames.
3. Side offsets, clothing and lighting variation: 40 usable frames.
4. Independent acceptance session: at least 24 manually labelled frames.

The collector runs automatically after a preparation delay. The subject moves
slowly and naturally; no exact spoken countdown or fixed inter-pose timing is
required. MoveNet supplies training pre-labels. Frames with fewer than six of
the eight target points above 0.20 confidence are rejected. The independent
acceptance session is manually labelled and is never used for model fitting or
quantization calibration.

## Quality report

The fully INT8 TFLite model is evaluated on the manually labelled holdout at
96x96 coordinates. The report contains mean, median and P95 pixel error,
PCK@5px, PCK@10px, and per-keypoint mean error. Provisional gates are:

- median visible-keypoint error below 5 pixels;
- PCK@10px at least 0.85.

After host quality passes, the trained TFLite replaces only the independent
TinyPose asset in a diagnostic firmware. The existing MoveNet acceptance and
benchmark remain intact. A fixed real-image fixture will then verify host and
ESP32-P4 INT8 output before camera-to-TinyPose end-to-end testing.

## Current hardware finding

The SC2336 probe passes at SCCB address `0x30` with ID `0xcb3a`. The first
private framing frame completed RAW10 capture and RGB conversion, but the lens
was aimed at a dark desk area and contained no person. Median luminance was
17.67/255, 49.65% of pixels were below 16, and all eight MoveNet target-point
confidences were below 0.20. That frame is rejected and is not training data.

The immediate physical prerequisite is to point the camera at a brighter area
where a person 2–3 metres away fits from shoulders through ankles. A second
single-frame framing check should pass before automatic dataset collection.

## LCD-assisted framing check

The current firmware exposes `/dev/fb0` as a 1024x600 RGB565 framebuffer and
reports initialized DSI host, bridge and display DMA state. The command
`sc2336_probe preview 60` completed a real SC2336-to-LCD run with 218 guarded
frames and no capture or display error. Mean times were 104,862 us capture,
150,000 us rendering and 20,000 us presentation per frame. The final displayed
camera frame remains on the LCD after preview exits.

The frame captured immediately after this preview was brighter than the first
check (median luminance 48.33/255), but it still contained room furniture rather
than a person. All eight teacher confidences were between 0.020 and 0.094, so
the second framing frame is also rejected. LCD and camera operation are now
verified; dataset collection remains gated only on camera aim and full-body
placement.

## Accepted framing and first training session

The earlier split preview/capture workflow allowed the subject to leave the
frame between commands. The private collector now supports one serial session
that runs LCD preview and immediately issues the camera dump, with no reconnect
or human-timed gap.

Four-orientation teacher comparison on the accepted full-body frame established
`CW90` as the required model-input transform. It produced 8/8 usable target
points with mean confidence 0.494; the unrotated frame produced 0/8. This
transform is now performed by the collector before private images are saved.

The first squat session captured eight transformed frames. The first two show
the subject entering the scene and were automatically rejected. Six frames
passed the requirement of at least six target points at confidence 0.20: one
frame had 6/8 points and five had 8/8. Accepted-frame mean confidence ranged
from 0.388 to 0.551. The session includes standing, squat and transition poses
and is valid as initial training data, but is too small to train the final
model by itself.

The second squat session captured 12 additional transformed frames and all 12
passed. Nine had 8/8 usable points; the remaining three had 7/8, 7/8 and 6/8.
Accepted-frame mean confidence averaged 0.472. The cumulative private training
set now contains 18 accepted real-camera frames across two sessions. More
position, viewpoint and lighting diversity is still required before fitting,
and the independent manually labelled acceptance session remains uncollected.

The third session added viewpoint and position variation. Ten of 12 frames
passed; two empty-scene frames were rejected automatically. Eight accepted
frames had 8/8 usable points and two had 7/8, with accepted mean confidence
0.461. The cumulative training pool is now 28 accepted frames and includes
frontal, slight-left, slight-right, standing, transition and squat poses.

The fourth session added 14/14 accepted frames at different subject distances.
Thirteen had 8/8 usable points and one had 6/8; mean confidence was 0.466. The
vertical target-point span ranged from 0.242 to 0.533 of the image, confirming
meaningful scale variation. This session also added appearance and clothing
variation. The cumulative pool now contains 42 accepted real-camera frames.
This is sufficient to fit a first constrained-scene candidate with augmentation;
an independent holdout must be captured and frozen before training starts.

## Trained candidate v1 and quality result

The frozen holdout contains 16 images and 126 visible target points. Two ankles
in one close squat frame are outside the image and excluded. Labels use
MoveNet-seeded positions followed by point-by-point visual review; this is an
assisted manual holdout rather than a blind annotation study. Its manifest was
frozen before training and no holdout image or label entered fitting,
epoch selection, refitting, or quantization calibration.

Sessions 1–3 supplied 28 tuning-train images and session 4 supplied 14 tuning
validation images. Validation selected 22 epochs. A fresh model was then fit
for exactly 22 epochs on all 42 non-holdout images; horizontal reflection made
84 refit samples. The fully INT8 model keeps the exact seven-op latency
contract and is 44,304 bytes.

On the frozen holdout, the INT8 model has mean error 3.84 px, median 3.38 px,
P95 9.27 px, PCK@5px 73.81%, and PCK@10px 98.41% in 96x96 coordinates. It
passes the provisional median-under-5px and PCK@10px-at-least-85% gates. The
right shoulder is the weakest point at 5.86 px mean error. These results support
the captured indoor squat domain; they do not establish broad multi-scene or
multi-population generalization.

The trained model was embedded with a real-camera INT8 fixture and flashed to
the ESP32-P4. Five warmups plus 100 measured complete TFLM Invokes produced
zero host-oracle and repeat mismatches. MIN/AVG/P50/P95/MAX were
10/11.5/10/20/20 ms, so both latency gates pass. Arena use remained 77,284
bytes. Camera capture, CW90 rotation, resize, RGB conversion, rendering and
cold initialization remain outside the reported Invoke latency.

## Portrait-input migration and first new training session — 2026-09-20

The SC2336 is now physically fixed clockwise 90 degrees. Firmware V18 keeps the
existing capture CCW90 and removes the old model-input CW90, producing an
upright person in a `54x96` effective portrait region with left/right padding.
The former trained model and its distance thresholds belong to the old input
geometry and are excluded from portrait BODY_CHECK acceptance.

The first portrait standing/shallow-squat session captured 12 unique RGB192
frames with `transform=none`. The MoveNet teacher accepted 8/12 frames; every
accepted frame had 8/8 required points at confidence >=0.20. Four frames were
rejected because the subject approached the camera or left the image. Accepted
confidence means ranged up to 0.595, and all 12 teacher outputs preserved the
shoulder->hip->knee->ankle y ordering. The session remains private under
`.secrets/tinypose-keypoints-portrait/`.

The rejected frames exposed an interaction problem: after live preview stopped,
the previous collector gave no persistent LCD indication that slow serial frame
export was still active. V19 adds `sc2336_probe capture_prompt active|done`, and
the host collector's `--lcd-prompts` option now brackets collection with visible
`CAPTURING / DO SQUATS - STAY IN FRAME` and `CAPTURE DONE / YOU MAY STOP`
screens. Both states passed on the ESP32-P4. V19 BIN SHA256 is
`8ec54f938a06826b82949e905c97c56f0feb7e216eb2989d8285cac0c6f6ac32`;
ELF SHA256 is
`73f291fb410f2bd27582a471b99f5373cc723a2bfd78837adf43ad8a5f470537`.

The first supplemental session used these prompts and captured 16/16 usable
frames: 15 had 8/8 required keypoints and one had 7/8 at confidence >=0.20.
Mean per-frame confidence was 0.566 (range 0.476–0.631), and every frame kept
the shoulder->hip->knee->ankle ordering. Visual review confirms standing,
shallow-squat and transition coverage with the full body in frame. Together
with the 8 accepted frames above, the portrait standing/shallow group now has
24 usable frames toward its 40-frame target.

The second supplemental session completed the group. It captured 16/16 usable
frames, all with 8/8 required points at confidence >=0.20; mean per-frame
confidence was 0.563 (range 0.500–0.614). Visual review confirms full-body
standing, shallow-squat bottoms, transitions, and limited side-facing
variation. Across the three portrait sessions, 44 unique frames were captured,
40 passed the six-point admission rule, and 39 of the accepted frames had all
8 points. The frozen group summary is
`.secrets/tinypose-keypoints-portrait/standing-shallow-group-summary.json`
(SHA256 `b8daf826caf575bfca6992a5f82af12f4050952fdeeeda5314b2da159f9a9bfa`).

Portrait full-squat session 01 captured 16/16 usable frames. Fourteen frames
had 8/8 required points and two had 7/8; mean per-frame confidence was 0.566
(range 0.460–0.688). Four frames had shoulder-to-ankle vertical span below
0.40 and visually show the deep bottom position. The batch covers standing,
descent, bottom and ascent and contributes 16/80 planned full-squat frames.

Portrait full-squat session 02 also captured 16/16 usable frames: 12 had 8/8
points and four had 7/8, with mean per-frame confidence 0.512. Seven frames had
shoulder-to-ankle vertical span below 0.40, adding strong deep-bottom coverage.
The portrait full-squat group now has 32/80 usable frames.

Portrait full-squat session 03 captured 16/16 usable frames: 15 had 8/8
required points and one had 7/8, with mean per-frame confidence 0.554. Five
frames were in the compressed deep range and four covered intermediate depth,
with the remainder spanning standing and transitions. The full-squat group now
has 48/80 usable frames.

Portrait full-squat session 04 captured 16/16 admissible frames: 12 had 8/8
points, two had 7/8 and two had 6/8, with mean per-frame confidence 0.521.
Six frames covered the deep range and four covered intermediate depth. The
6–7 point cases occur in realistic low-position limb occlusion and remain
valid under the training admission rule. The group now has 64/80 frames.

Portrait full-squat session 05 captured 15/16 usable frames: 12 accepted frames
had 8/8 points and three had 7/8. The first frame showed the subject still near
the device and was rejected with only three usable points; the remaining frames
covered continuous transitions and low positions. The group is at 79/80. The
collector now waits three seconds after showing the LCD `CAPTURING` prompt
before its first frame, preventing this prompt-to-position race in later runs.

The four-frame supplemental run used the new settling delay and accepted 4/4
frames, bringing the full-squat group to 83 accepted frames from 84 unique
captures. Sixty-eight accepted frames have 8/8 points; 27 cover the deep range
and 18 cover intermediate depth. The frozen summary is
`.secrets/tinypose-keypoints-portrait/full-squat-group-summary.json` (SHA256
`df9b1ab5be890032ae19b3ad7d52b06b27f1c2e0c4cd5e9b2b0c40f41789f711`).

Portrait viewpoint session 01 captured 16/16 usable frames: 14 had 8/8 points
and two had 7/8, with mean per-frame confidence 0.511. It adds a different
subject, clothing, arm positions, shallow squats and mild left/right body
angles. Keypoint center x only ranged from 0.455 to 0.476, so this session adds
appearance and angle diversity but does not satisfy the planned lateral-offset
coverage by itself. The next viewpoint batch must explicitly move the subject
left and right while retaining the full body.

Portrait viewpoint session 02 captured 16/16 usable frames: 14 had 8/8 points
and two had 7/8, with mean per-frame confidence 0.554. Keypoint center x
expanded to 0.359–0.500, with eight frames below 0.44, so model-image-left
coverage is now present. Model-image-right coverage remains missing; the final
eight viewpoint frames are reserved for movement toward the subject's own left
while facing the camera. The viewpoint/appearance group is at 32/40 frames.

The right-side supplement accepted 8/8 frames, seven with all eight points.
Across all three viewpoint/appearance sessions, 40/40 unique frames are
usable, 35 have 8/8 points, keypoint center x spans 0.359–0.537, eight frames
cover model-image-left below 0.44, and nine cover model-image-right above 0.50.
The frozen group summary is
`.secrets/tinypose-keypoints-portrait/viewpoint-appearance-group-summary.json`
(SHA256 `bca6e1af471845b7554a0cf6d3ad8ae4ca0996a7b9644d847c552e7e9cf6e267`).

Portrait holdout session 01 captured 12/12 unique, reviewable frames in a fresh
session. MoveNet generated annotation seeds only: 11 frames had 8/8 seeded
points and one had 7/8 at confidence >=0.20. Visual review confirms standing,
mild side angles, descent, low positions and ascent with the full body visible.
These images are isolated from fitting, epoch selection and quantization; their
teacher seeds are not quality ground truth and must be manually reviewed before
evaluation.

## Portrait model v1 training and independent acceptance — 2026-09-20

Holdout session 02 added another 12 fresh frames. Both holdout batches were
manually reviewed and corrected, producing 24 holdout images and 192 visible
keypoints. Image hashes are disjoint from the 115-image training split and the
48-image tuning-validation split. The 24 holdout images were excluded from
fitting, epoch selection, augmentation and representative-data calibration.

The final fit used all 163 non-holdout accepted images, doubled to 326 samples
by augmentation. Tuning selected 43 refit epochs after the validation run
stopped at epoch 63. The fully INT8 model is 44,304 bytes and retains the
prototype operator set: five `CONV_2D` operators followed by `RESHAPE` and
`FULLY_CONNECTED`. Its SHA256 is
`7d88d54e98c57a9bdf3c2b1bb2aad99894957d91c333cac002c5cc5e1f7cfbe4`.

Independent holdout quality passes both provisional gates:

- median keypoint error: 3.467 px (`< 5 px` PASS)
- mean keypoint error: 4.198 px
- P95 keypoint error: 10.171 px
- PCK@5 px: 72.40%
- PCK@10 px: 93.75% (`>= 85%` PASS)

The deployment fixture is a real frame from holdout session 02. Model input is
INT8 `[1,96,96,3]`, scale `0.00585159566`, zero point `43`; output is INT8
`[1,24]`, scale `0.00340311276`, zero point `-128`. The recorded transform is
RAW10 demosaic, `portrait_sample` CCW90, 192-square letterbox, 2x2 area resize
to 96-square without an additional model-input rotation, then normalization.

The ESP-NN firmware was flashed to the real ESP32-P4 revision v3.2. Five warmup
rounds followed by 100 complete TFLM Invokes produced 0 host-oracle mismatches
and 0 repeat mismatches. MIN/AVG/P50/P95/MAX were 10/11.6/10/20/20 ms;
`gate_150ms` and `gate_200ms` both pass. TFLM allocated a 524,288-byte arena
and used 77,284 bytes. Used heap was 1,269,304 bytes before init, 1,794,336
after init and Invoke, and 1,269,712 after deinit. The reported latency excludes
camera capture, portrait conversion, resize, RGB conversion, rendering and cold
model initialization.

Firmware BIN SHA256 is
`7209938f6850a36ba1ead20deb365caa7079c3e11e31f901640a23002200d5cc`;
ELF SHA256 is
`f3aa2fadf7ea6177c9432e196529bde3fa4c9fb0e68bd4e09bd32beeb5bc8bee`.
The structured result is
`hardware-logs/tinypose-portrait-model-v1-acceptance-20260920.json`, and the
serial evidence is
`hardware-logs/tinypose-portrait-model-v1-p4-100round-20260920.log`.
