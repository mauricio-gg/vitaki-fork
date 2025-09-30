# Current TODO Items - Full VitaRPS5 UI Implementation

## 🎯 Goal: Complete VitaRPS5 UI Experience with vitaki-fork Backend

**Strategy**: Rebuild entire UI layer to match VitaRPS5 design while keeping vitaki-fork's proven backend 100% untouched.

**Status**: Documentation complete, ready for phased implementation.

## 📋 Implementation Phases

### Phase 1: Foundation - Particle System ⏳
**Estimated Time**: 2-3 hours

**Tasks:**
- [ ] Add VitaRPS5 symbol textures to CMakeLists.txt
  - symbol_triangle.png, symbol_circle.png, symbol_ex.png, symbol_square.png
- [ ] Load symbol textures in `load_textures()`
- [ ] Initialize particle system (12 particles)
- [ ] Implement `update_particles()` function (upward drift with rotation)
- [ ] Implement `render_particles()` function
- [ ] Test particle rendering on black background

**Deliverable:** Animated PlayStation symbols floating upward

**Files Modified:**
- `vita/src/ui.c` - Add particle system
- `vita/CMakeLists.txt` - Add symbol texture paths

---

### Phase 2: Wave Navigation Sidebar ⏳
**Estimated Time**: 2-3 hours

**Tasks:**
- [ ] Add wave textures to CMakeLists.txt
  - wave_top.png, wave_bottom.png
- [ ] Load wave and navigation icon textures
- [ ] Implement `render_wave_navigation()` function
  - Draw wave textures as background
  - Render 4 navigation icons (Play, Settings, Controller, Profile)
  - Add selection highlight (PlayStation Blue circle)
  - Apply wave animation (vertical sine wave motion)
- [ ] Add navigation state variables
- [ ] Wire icon selection to screen switching

**Deliverable:** Working wave sidebar with navigation

**Files Modified:**
- `vita/src/ui.c` - Add wave sidebar rendering
- `vita/CMakeLists.txt` - Add wave/icon texture paths

---

### Phase 3: Console Card Display ⏳
**Estimated Time**: 3-4 hours

**Tasks:**
- [ ] Add console card textures to CMakeLists.txt
  - console_card.png, ellipse_*.png
- [ ] Create `ConsoleCardInfo` struct
- [ ] Map vitaki hosts to console card data
- [ ] Implement `render_console_card()` function
  - Card background with shadow
  - PS5 logo (centered, 1/3 from top)
  - Console name bar (1/3 from bottom)
  - Status indicator (top-right)
  - State text ("Ready" / "Standby")
  - State glow (blue for Ready, yellow for Standby)
- [ ] Implement `render_console_grid()` function
- [ ] Add selection highlight (PlayStation Blue border)
- [ ] Update `draw_main_menu()` to use console cards

**Deliverable:** Console cards displaying vitaki hosts

**Files Modified:**
- `vita/src/ui.c` - Replace host tiles with console cards
- `vita/CMakeLists.txt` - Add card texture paths

---

### Phase 4: Touch Screen Interactions ⏳
**Estimated Time**: 2-3 hours

**Tasks:**
- [ ] Implement `handle_touch_input()` function
- [ ] Add `is_point_in_circle()` helper (for wave icons)
- [ ] Add `is_point_in_rect()` helper (for cards/buttons)
- [ ] Implement touch detection for:
  - Wave navigation icons (4 circular hitboxes)
  - Console cards (rectangular hitboxes)
  - Add New button (rectangular hitbox)
  - Wake/Register buttons (when visible)
- [ ] Add visual feedback for touches
- [ ] Wire touch events to actions

**Deliverable:** Full touch screen control

**Files Modified:**
- `vita/src/ui.c` - Add touch input handling

---

### Phase 5: Backend Integration ⏳
**Estimated Time**: 1-2 hours

**Tasks:**
- [ ] Wire console selection to `context.active_host`
- [ ] Wire console connect to `host_stream()`
- [ ] Wire "Add New" button to `start_discovery()`
- [ ] Wire "Wake" button to `host_wakeup()`
- [ ] Wire "Register" button to registration screen
- [ ] Test complete flow: discover → register → connect
- [ ] Verify all vitaki functionality works

**Deliverable:** Full VitaRPS5 UI with vitaki backend

**Files Modified:**
- `vita/src/ui.c` - Wire UI actions to vitaki backend

---

### Phase 6: Polish & Testing ⏳
**Estimated Time**: 2-3 hours

**Tasks:**
- [ ] Add animations and transitions
- [ ] Optimize rendering performance
- [ ] Add "Add New" button rendering
- [ ] Add empty state ("No consoles found. Tap Add New to discover.")
- [ ] Test on hardware
- [ ] Fix bugs and edge cases
- [ ] Performance testing (60 FPS target)

**Deliverable:** Production-ready VitaRPS5 UI

**Files Modified:**
- `vita/src/ui.c` - Polish and bug fixes

---

## 📊 Progress Tracking

| Phase | Status | Est. Hours | Actual Hours | Completion |
|-------|--------|------------|--------------|------------|
| 1. Particle System | ⏳ Pending | 2-3 | - | 0% |
| 2. Wave Navigation | ⏳ Pending | 2-3 | - | 0% |
| 3. Console Cards | ⏳ Pending | 3-4 | - | 0% |
| 4. Touch Interactions | ⏳ Pending | 2-3 | - | 0% |
| 5. Backend Integration | ⏳ Pending | 1-2 | - | 0% |
| 6. Polish & Testing | ⏳ Pending | 2-3 | - | 0% |
| **Total** | | **15-20** | - | **0%** |

## 🎯 Success Criteria

1. ⏳ Animated PlayStation symbol background (12 particles)
2. ⏳ Wave navigation sidebar with 4 icons
3. ⏳ Console cards (400x200) displaying vitaki hosts
4. ⏳ Touch screen interactions working
5. ⏳ All vitaki backend functions operational (discovery, registration, streaming)
6. ⏳ Smooth 60 FPS animations
7. ⏳ No regressions in vitaki functionality

## 📝 Key Documents

- **Implementation Spec**: `VITARPS5_UI_IMPLEMENTATION_SPEC.md` - Complete technical specification
- **Done Work**: `DONE.md` - Path B partial styling (v0.1.51-54)
- **Archived**: `ARCHIVED.md` - Path A (full integration) and Path B (partial styling)

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────┐
│              VitaRPS5 UI Layer (NEW)                     │
│  - Wave navigation sidebar                               │
│  - Animated particle background                          │
│  - Console cards                                         │
│  - Touch interactions                                    │
│  (All in ui.c - complete rewrite of rendering/input)    │
├─────────────────────────────────────────────────────────┤
│       vitaki-fork Backend (UNCHANGED - 100%)             │
│  discovery.c, host.c, config.c, video.c, etc.           │
│  All proven functionality preserved                      │
└─────────────────────────────────────────────────────────┘
```

## 🚀 Next Steps

1. Start with Phase 1 (Particle System)
2. Test each phase independently before moving forward
3. Commit after each successful phase
4. Update this TODO.md with progress
5. Build and test on hardware after Phase 3 (first visible UI)

## ⚠️ Important Notes

- This is purely UI work - vitaki backend remains untouched
- All changes confined to `vita/src/ui.c` and `vita/CMakeLists.txt`
- Existing vitaki functionality must work unchanged
- Both touch and controller input supported
- Can pause between phases for testing/feedback

## 📦 Build Versioning

When phases are complete:
- v0.1.56+ - Incremental builds as phases complete
- Final version will be v0.2.0 (major UI redesign)