/**
 * @file trackMotionSeq.cpp
 * @brief TrackMotionSeq behavior stubs. See track.h for the class-level skeleton.
 */
#include "track.h"

void TrackMotionSeq::update(double delta1, double delta2)
{
    Track::update(delta1, delta2);
    // TODO: rework against motionCfg.sequence/motionCfg.motion.
}

void TrackMotionSeq::tick(double dtMs, double qnPhaseDelta)
{
    (void)dtMs;
    // MotionSeq has no free-run mode either (sequencer.h's SeqParams) --
    // same always-synced accumulation as TrackStepSeq::tick().
    if (qnPhaseDelta <= 0.0 || motionCfg.sequence.stepCount == 0)
        return;

    double stepLengthQn = stepRateToQuarterNotes(motionCfg.sequence.stepRate);
    if (stepLengthQn <= 0.0)
        return;

    stepAccumQn += qnPhaseDelta;
    while (stepAccumQn >= stepLengthQn)
    {
        stepAccumQn -= stepLengthQn;
        currentStep = (uint8_t)((currentStep + 1) % motionCfg.sequence.stepCount);
    }

    // TODO: rework against motionCfg.motion/motionCfg.ccs once getMidiOutput() is redesigned.
}
