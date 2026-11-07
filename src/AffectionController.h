#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

struct AffectionState {
  int16_t affection = 500;
  int16_t mood = 0;
  int16_t confusion = 0;
  uint32_t seq = 0;
  uint8_t levelIndex = 3;
};

struct AffectionApplyResult {
  bool applied = false;
  bool duplicate = false;
  int16_t delta = 0;
  uint8_t previousLevelIndex = 3;
  uint8_t levelIndex = 3;
  bool levelChanged = false;
  AffectionState state;
};

struct AffectionSyncState {
  String characterId;
  int16_t syncedBaseAffection = 500;
  int16_t unsyncedDelta = 0;
  AffectionState state;
};

class AffectionController {
public:
  struct EventSpec {
    const char* name;
    int8_t affectionWeight;
    int8_t moodWeight;
    int8_t confusionWeight;
    uint16_t cooldownMs;
  };

  void begin(Preferences* preferences);
  AffectionApplyResult update(unsigned long now, bool allowSave=true);

  AffectionApplyResult applyEvent(const char* eventName, float confidence, float intensity,
                                  const char* eventId, unsigned long now);
  AffectionApplyResult reset(int value);
  AffectionApplyResult resetTransient();
  AffectionApplyResult debugAdjust(int delta);
  AffectionApplyResult debugSet(bool hasAffection, int affection,
                                bool hasLevelIndex, uint8_t requestedLevelIndex,
                                bool hasMood, int mood,
                                bool hasConfusion, int confusion,
                                bool persist);
  AffectionApplyResult applySyncAffection(const char* characterId, int affection);
  AffectionSyncState syncStateForCharacter(const char* characterId);

  uint32_t petCooldownRemaining(uint32_t now) const;
  bool pendingSave() const { return dirty_; }
  bool lastSaveOk() const { return lastSaveOk_; }
  bool loadOk() const { return loadOk_; }
  uint32_t saveAttempts() const { return saveAttempts_; }
  size_t savedAffectionBytes() const { return savedAffectionBytes_; }
  size_t savedSequenceBytes() const { return savedSequenceBytes_; }
  int persistedAffection() const { return lastSavedAffection_; }
  uint32_t persistedSeq() const { return lastSavedSeq_; }
  const AffectionState& state() const;
  const char* level() const;
  uint8_t levelIndex() const;
  const char* styleId() const;
  const char* visualTier() const;

private:
  static constexpr uint8_t kRecentEventCount = 8;
  static constexpr uint8_t kEventSpecCount = 10;

  const EventSpec* findSpec(const char* eventName, uint8_t* index);
  bool isDuplicate(const char* eventId) const;
  void rememberEventId(const char* eventId);
  float cooldownFactor(uint8_t eventIndex, unsigned long now) const;
  void markEventApplied(uint8_t eventIndex, unsigned long now);
  bool applyDelta(int delta, int moodDelta, int confusionDelta);
  bool applyTransientState(int mood, int confusion);
  uint8_t initialLevelIndexForAffection(int affection) const;
  uint8_t updateLevelIndexForAffection(int affection);
  int representativeAffectionForLevel(uint8_t index) const;
  const char* levelForIndex(uint8_t index) const;
  const char* styleIdForIndex(uint8_t index) const;
  const char* visualTierForIndex(uint8_t index) const;
  bool loadSyncBaseForCharacter(const char* characterId, int16_t& base);
  bool saveSyncBaseForCharacter(const char* characterId, int base);
  int16_t syncBaseForCharacter(const char* characterId);
  void load();
  void save(bool force);
  bool shouldSave(unsigned long now) const;

  Preferences* preferences_ = nullptr;
  AffectionState state_;
  int16_t lastSavedAffection_ = 500;
  uint32_t lastSavedSeq_ = 0;
  uint8_t levelIndex_ = 3;
  unsigned long lastSaveMs_ = 0;
  unsigned long lastMoodDecayMs_ = 0;
  unsigned long lastConfusionDecayMs_ = 0;
  unsigned long lastEventMs_[kEventSpecCount] = {};
  String recentEventIds_[kRecentEventCount];
  uint8_t recentEventWriteIndex_ = 0;
  bool dirty_ = false, lastSaveOk_=true, loadOk_=false;
  uint32_t saveAttempts_=0,lastSaveAttemptMs_=0;
  size_t savedAffectionBytes_=0,savedSequenceBytes_=0;
};
