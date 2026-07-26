#pragma once

// This is a conservative lower bound for perfect staging. It charges every
// stage the longest possible post-burn frame, so an accepted target remains
// reachable even when the flight loop runs at its maximum allowed delta.
static constexpr uint8_t missionValidationStageCount(
    const MissionDefinition &mission, uint8_t index = 0) {
  return index == MAX_STAGE_COUNT || mission.stages[index].name == nullptr
             ? index
             : missionValidationStageCount(mission, index + 1);
}

static constexpr bool missionValidationStagesAreValid(
    const MissionDefinition &mission, uint8_t index = 0, bool foundEmpty = false) {
  return index == MAX_STAGE_COUNT
             ? true
             : mission.stages[index].name == nullptr
                   ? missionValidationStagesAreValid(mission, index + 1, true)
                   : !foundEmpty && mission.stages[index].width > 0 &&
                         mission.stages[index].height > 0 &&
                         mission.stages[index].thrust > SURFACE_GRAVITY &&
                         missionValidationStagesAreValid(mission, index + 1, false);
}

static constexpr float missionValidationTotalHeight(
    const MissionDefinition &mission, uint8_t index = 0) {
  return index == missionValidationStageCount(mission)
             ? 0.0f
             : mission.stages[index].height +
                   missionValidationTotalHeight(mission, index + 1);
}

static constexpr float missionValidationTotalBurnSeconds(
    const MissionDefinition &mission) {
  return BASE_POWERED_SECONDS +
         (missionValidationStageCount(mission) > 2
              ? (missionValidationStageCount(mission) - 2) *
                    EXTRA_POWERED_SECONDS_PER_STAGE
              : 0.0f);
}

static constexpr float missionValidationStageBurnSeconds(
    const MissionDefinition &mission, uint8_t index) {
  return missionValidationTotalBurnSeconds(mission) * mission.stages[index].height /
         missionValidationTotalHeight(mission);
}

static constexpr float missionValidationStageExitVelocity(
    const MissionDefinition &mission, uint8_t index);

static constexpr float missionValidationStageStartVelocity(
    const MissionDefinition &mission, uint8_t index) {
  return index == 0
             ? LIFTOFF_VELOCITY
             : missionValidationStageExitVelocity(mission, index - 1) *
                   PERFECT_STAGE_VELOCITY_RETAINED;
}

static constexpr float missionValidationStageBurnoutVelocity(
    const MissionDefinition &mission, uint8_t index) {
  return missionValidationStageStartVelocity(mission, index) +
         (mission.stages[index].thrust - SURFACE_GRAVITY) *
             missionValidationStageBurnSeconds(mission, index);
}

static constexpr float missionValidationStageExitVelocity(
    const MissionDefinition &mission, uint8_t index) {
  return missionValidationStageBurnoutVelocity(mission, index) -
         SURFACE_GRAVITY * MAX_FRAME_DELTA_SECONDS;
}

static constexpr float missionValidationPoweredAltitude(
    const MissionDefinition &mission, uint8_t index = 0) {
  return index == missionValidationStageCount(mission)
             ? 0.0f
             : missionValidationStageStartVelocity(mission, index) *
                       missionValidationStageBurnSeconds(mission, index) +
                   0.5f * (mission.stages[index].thrust - SURFACE_GRAVITY) *
                       missionValidationStageBurnSeconds(mission, index) *
                       missionValidationStageBurnSeconds(mission, index) +
                   MAX_FRAME_DELTA_SECONDS *
                       missionValidationStageExitVelocity(mission, index) +
                   missionValidationPoweredAltitude(mission, index + 1);
}

static constexpr float missionValidationApogee(
    const MissionDefinition &mission) {
  return missionValidationPoweredAltitude(mission) +
         missionValidationStageExitVelocity(
             mission, missionValidationStageCount(mission) - 1) *
             PERFECT_STAGE_VELOCITY_RETAINED *
             missionValidationStageExitVelocity(
                 mission, missionValidationStageCount(mission) - 1) *
             PERFECT_STAGE_VELOCITY_RETAINED /
         (2.0f * SURFACE_GRAVITY);
}

static constexpr bool missionValidationCanReachTarget(
    const MissionDefinition &mission) {
  return mission.targetAltitude > 0 && missionValidationStageCount(mission) > 0 &&
         missionValidationStagesAreValid(mission) &&
         mission.targetAltitude <= missionValidationApogee(mission);
}

template <size_t missionCount>
static constexpr bool missionValidationAllTargetsReachable(
    const MissionDefinition (&missions)[missionCount], uint8_t index = 0) {
  return index == missionCount
             ? true
             : missionValidationCanReachTarget(missions[index]) &&
                   missionValidationAllTargetsReachable(missions, index + 1);
}
