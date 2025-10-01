# Phase 3: Backend Integration & Feature Completion

**Start Date**: October 1, 2025
**Status**: 📋 Ready to Implement
**Goal**: Implement backend-supported features and properly handle unsupported ones

---

## Overview

**Audit Complete**: Comprehensive backend analysis finished (see BACKEND_AUDIT.md)

**Key Finding**: vitaki-fork uses minimal config system. Most UI stubs have NO backend support.

**Revised Strategy**:
1. Implement 3-5 high-value features WITH backend support (6-8 hours)
2. Disable/document features WITHOUT backend support (1 hour)
3. Create foundation for future backend enhancements (Phase 4)

---

## Stubbed Features Audit

### Settings Screen Stubs
**Video Settings Tab:**
- [ ] Aspect Ratio control
- [ ] Brightness control
- [ ] Video Smoothing toggle

**Network Settings Tab:**
- [ ] Connection Type selector
- [ ] Network Timeout configuration
- [ ] MTU Size configuration

**Streaming Tab:**
- [ ] Hardware Decode toggle (currently always on)

### Profile Screen Stubs
- [ ] Real-time latency display (needs stream stats integration)
- [ ] PSN ID editing (needs text input widget)

### Controller Screen Stubs
- [ ] Motion Controls toggle
- [ ] Touchpad as Buttons toggle
- [ ] Map-specific button layouts (different mappings per scheme ID)

---

## Phase 3 Implementation Steps

### Step 1: Quick Wins (HIGH PRIORITY)
**Goal**: Implement features with full backend support
**Estimated Time**: 2-3 hours

**Task 1.1: Map-Specific Button Layouts** ⭐ HIGHEST VALUE
- **Backend**: Fully supported (vita/src/controller.c)
- **Effort**: 1-2 hours
- **Action**: Update `draw_controller_mappings_tab()` to display correct mappings based on `context.config.controller_map_id`
- **Deliverable**: Each scheme (0-7, 25, 99, 100+) shows its actual button layout

**Task 1.2: Real-time Latency Display** ⭐ HIGH VALUE
- **Backend**: Data exists in session
- **Effort**: 1-2 hours
- **Action**: Add latency getter to session manager, wire to Profile screen
- **Deliverable**: Live latency number (e.g., "42 ms") in Connection Info card

**Task 1.3: 720p Resolution Option (EXPERIMENTAL)** ⚠️ TEST CAREFULLY
- **Backend**: Should work for PS5
- **Effort**: 30 minutes
- **Action**: Add 720p to resolution dropdown with warning label
- **Deliverable**: Users can opt-in to 720p (may improve quality on PS5)

---

### Step 2: Text Input & Registration (MEDIUM PRIORITY)
**Goal**: Enable user configuration features
**Estimated Time**: 2-3 hours

**Task 2.1: Simple Text Input Widget**
- **Backend**: PSN ID setter exists
- **Effort**: 2 hours
- **Action**: Create on-screen keyboard for PSN ID editing
- **Deliverable**: Users can edit PSN Account ID from Profile screen

**Task 2.2: Registration Flow Integration**
- **Backend**: Registration system exists
- **Effort**: 1 hour
- **Action**: Wire "Register New Console" button to existing registration
- **Deliverable**: Tap registration section → start registration flow

---

### Step 3: UI Cleanup (LOW PRIORITY)
**Goal**: Handle unsupported features properly
**Estimated Time**: 1-2 hours

**Task 3.1: Remove Hardware Decode Toggle**
- **Reason**: Always required, can't be disabled
- **Action**: Remove from Settings → Streaming tab entirely
- **Code Impact**: Delete draw_settings_streaming_tab() toggle section

**Task 3.2: Disable Video Settings Tab**
- **Reason**: No backend support (aspect ratio, brightness, smoothing)
- **Action**: Gray out all controls, add "Feature requires backend enhancement" message
- **Alternative**: Remove tab entirely (reduce from 3 tabs to 2)

**Task 3.3: Disable Network Settings Tab**
- **Reason**: No backend support (connection type, timeout, MTU)
- **Action**: Gray out all controls, add "Feature requires backend enhancement" message
- **Alternative**: Remove tab entirely (reduce from 3 tabs to 2)

**Task 3.4: Mark Motion Controls as Unavailable**
- **Reason**: Requires gyroscope integration (Phase 4)
- **Action**: Add "(Requires Backend Enhancement)" label, disable toggle

**Task 3.5: Remove Touchpad as Buttons**
- **Reason**: Chiaki protocol doesn't support
- **Action**: Remove from Controller Settings tab entirely

---

### Step 4: Documentation (FINAL STEP)
**Goal**: Complete Phase 3 documentation
**Estimated Time**: 1 hour

**Task 4.1: Update PHASE3_COMPLETE_SUMMARY.md**
- Document implemented features
- List disabled/removed features
- Mark Phase 4 candidates

**Task 4.2: Create USER_FEATURE_MATRIX.md**
- User-facing feature support list
- Clear indicators: ✅ Supported | ⚠️ Experimental | 🔜 Coming Soon | ❌ Not Supported

**Task 4.3: Update TODO.md and ROADMAP.md**
- Mark Phase 3 complete
- Outline Phase 4 scope

---

## Implementation Order (Feature-by-Feature)

**Critical Rule**: Implement ONE feature at a time, test, commit before moving to next.

### Feature 1: Map-Specific Button Layouts (Task 1.1)
1. Read `vita/src/controller.c` to understand each scheme
2. Create mapping display logic for each map_id
3. Update `draw_controller_mappings_tab()` with dynamic table
4. Test all schemes (0-7, 25, 99, 100+)
5. Commit: "feat: Add dynamic controller mappings for all schemes"

### Feature 2: Real-time Latency Display (Task 1.2)
1. Find latency data in session/stream stats
2. Add getter function to session manager
3. Wire to Profile screen Connection Info card
4. Format as "Latency: XX ms" with color coding
5. Commit: "feat: Add real-time latency display to Profile screen"

### Feature 3: 720p Resolution Option (Task 1.3)
1. Add 720p to resolution dropdown
2. Add warning label "(Experimental - PS5 only)"
3. Test on PS5 connection (if available)
4. Document risks in code comments
5. Commit: "feat: Add experimental 720p resolution option"

### Feature 4: Remove Unsupported Features (Step 3)
1. Remove Hardware Decode toggle from Streaming tab
2. Decide: Keep or remove Video/Network tabs
3. Remove Touchpad as Buttons from Controller Settings
4. Mark Motion Controls with "(Coming Soon)" label
5. Commit: "refactor: Remove unsupported UI features, mark future work"

### Feature 5: Text Input Widget (Task 2.1) - OPTIONAL
1. Design simple on-screen keyboard layout
2. Implement character selection with D-pad
3. Wire to PSN ID field in Profile screen
4. Test text input flow thoroughly
5. Commit: "feat: Add on-screen keyboard for PSN ID editing"

### Feature 6: Registration Integration (Task 2.2) - OPTIONAL
1. Find registration flow entry point
2. Wire Profile screen "Register" button
3. Test full discovery → register → connect flow
4. Commit: "feat: Wire registration button to existing flow"

---

## Success Criteria (Revised)

### Minimum Viable Phase 3 (Must Have)
1. ✅ Map-specific button layouts functional
2. ✅ Real-time latency display working
3. ✅ Unsupported features clearly marked/removed
4. ✅ No regressions in existing functionality
5. ✅ All `TODO(PHASE2-STUB)` markers addressed

### Extended Phase 3 (Nice to Have)
6. ✅ 720p resolution option available (with warnings)
7. ✅ Text input widget for PSN ID editing
8. ✅ Registration flow integrated
9. ✅ User feature matrix documented

### Out of Scope (Phase 4)
- ❌ Video quality enhancements (brightness, aspect ratio, smoothing)
- ❌ Network configuration (MTU, timeout, connection type)
- ❌ Motion controls integration
- ❌ Backend architecture improvements

---

## Time Estimates (Revised Based on Audit)

| Task | Original Estimate | Revised Estimate | Reason |
|------|------------------|------------------|---------|
| Map-specific layouts | 3-4 hours | 1-2 hours | Backend fully ready |
| Latency display | 2-3 hours | 1-2 hours | Data already exists |
| 720p option | - | 30 min | Simple dropdown addition |
| Remove unsupported | - | 1-2 hours | Code cleanup |
| Text input widget | 2-3 hours | 2 hours | New component needed |
| Registration wiring | 1-2 hours | 1 hour | Flow already exists |
| **TOTAL** | **15-20 hours** | **6-9 hours** | Realistic scope |

---

## Critical Decisions Made

### Decision 1: VitaRPS5Settings Migration
**Status**: ❌ NO - Too risky for Phase 3
**Rationale**: Would require massive refactoring, keep minimal VitaChiakiConfig

### Decision 2: Unsupported Features
**Status**: ✅ Gray out with explanation
**Rationale**: Maintains visual consistency, honest about limitations

### Decision 3: 720p Resolution
**Status**: ⚠️ Add as experimental option with warning
**Rationale**: May work on PS5, let users opt-in with informed consent

### Decision 4: Video/Network Tabs
**Status**: 🤔 To be decided during implementation
**Options**:
A. Keep tabs but gray out all controls
B. Remove tabs entirely (reduce to 1 tab for Settings)
**Recommendation**: Remove tabs to reduce UI clutter

---

## Phase 3 Completion Criteria

**Phase 3 will be considered COMPLETE when**:
1. All HIGH PRIORITY tasks (1.1, 1.2, 1.3) are implemented and tested
2. Unsupported features are removed/disabled (Step 3)
3. Documentation is updated (Step 4)
4. VPK builds without errors
5. No regressions in Phase 2 screens

**OPTIONAL tasks** (2.1, 2.2) may be deferred to Phase 3.5 or Phase 4 if time-constrained.

---

## Next Steps

1. ✅ Backend audit complete (BACKEND_AUDIT.md created)
2. ✅ Phase 3 plan updated based on findings
3. ⏭️ **START IMPLEMENTATION**: Begin with Feature 1 (Map-Specific Layouts)
4. Follow feature-by-feature implementation order
5. Test each feature before moving to next

**Target Version Range**: v0.1.78-0.1.85
**Target Completion**: October 1, 2025 (same day, 6-9 hours work)
**Current Version**: v0.1.77
