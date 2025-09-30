# Current TODO Items - UI Integration with vitaki-fork Backend

## 🎯 Current Phase: VitaRPS5 UI Integration (Fresh Start)

**Goal**: Replace vitaki-fork's simple UI with VitaRPS5's modern PlayStation-inspired UI while keeping vitaki-fork's proven backend (discovery, streaming, registration).

### Status: Architecture Decoupling Required

We discovered that VitaRPS5 UI is tightly coupled to VitaRPS5 backend. We're now creating a bridge layer to decouple the UI and wire it to vitaki-fork's backend.

## 📋 Active TODO Items

### Phase 1: Architecture Setup ✅
- [x] Create clean branch from ywnico (working vitaki-fork)
- [x] Copy entire VitaRPS5 project for reference
- [x] Copy VitaRPS5 UI modules to vita/src/ui/
- [x] Copy VitaRPS5 core/utils modules
- [x] Update CMakeLists.txt structure
- [x] Document architecture challenge

### Phase 2: Dependency Analysis & Stub Creation 🔄
- [ ] **Document VitaRPS5 UI → Backend dependencies**
  - Map all includes from UI files
  - Identify which functions UI calls
  - Document expected data structures

- [ ] **Create stub headers** for missing VitaRPS5 modules:
  - `audio/audio_decoder.h` - Audio system interface
  - `system/vita_system_info.h` - System info queries
  - `discovery/ps5_discovery.h` - Console discovery interface
  - `chiaki/chiaki_base64_vitaki.h` - Base64 utilities

### Phase 3: Copy Remaining VitaRPS5 Modules
- [ ] Copy audio/ module files
- [ ] Copy system/ module files
- [ ] Copy discovery/ module files
- [ ] Copy chiaki/ integration files
- [ ] Update CMakeLists.txt with new modules

### Phase 4: Bridge Implementation
- [ ] **Discovery Bridge**
  - Map VitaRPS5 `ps5_discovery_*` → vitaki-fork `start_discovery/stop_discovery`
  - Convert between VitaRPS5 and vitaki-fork host structures

- [ ] **Registration Bridge**
  - Map VitaRPS5 registration → vitaki-fork `host_register()`
  - Handle credential storage compatibility

- [ ] **Streaming Bridge**
  - Map VitaRPS5 session start → vitaki-fork `host_stream()`
  - Handle session state management

- [ ] **Settings Bridge**
  - Map VitaRPS5 settings → vitaki-fork config.toml
  - Ensure setting persistence works

### Phase 5: Compilation & Testing
- [ ] Fix all compilation errors
- [ ] Build successful VPK
- [ ] Test UI initialization
- [ ] Test basic navigation
- [ ] Test console discovery display
- [ ] Test registration flow
- [ ] Test streaming initiation

### Phase 6: Full Integration Testing
- [ ] Test complete discovery → registration → streaming flow
- [ ] Verify settings persistence
- [ ] Test error handling
- [ ] Performance testing
- [ ] Memory leak checks

## 🚧 Current Blocker

**Compilation fails** due to missing VitaRPS5 backend module headers:
```
fatal error: ../audio/audio_decoder.h: No such file or directory
fatal error: ../system/vita_system_info.h: No such file or directory
fatal error: ../discovery/ps5_discovery.h: No such file or directory
fatal error: ../chiaki/chiaki_base64_vitaki.h: No such file or directory
```

**Next Step**: Copy remaining VitaRPS5 modules, then create bridge layer to map calls to vitaki-fork backend.

## 📊 Progress Metrics

| Component | Status | Progress |
|-----------|--------|----------|
| Branch Setup | ✅ Complete | 100% |
| UI Files Copied | ✅ Complete | 100% |
| Core/Utils Copied | ✅ Complete | 100% |
| Dependencies Analysis | 🔄 In Progress | 10% |
| Stub Creation | ⏳ Pending | 0% |
| Bridge Implementation | ⏳ Pending | 0% |
| Build Success | ❌ Blocked | 0% |
| Hardware Testing | ⏳ Pending | 0% |

## 🎯 Success Criteria

1. ✅ Clean separation: VitaRPS5 UI frontend + vitaki-fork backend
2. ✅ Working build producing VPK
3. ✅ UI displays correctly on hardware
4. ✅ All backend functions work (discovery, registration, streaming)
5. ✅ No duplicate code between UI and backend layers

## 📝 Documentation Requirements

- Keep TODO.md updated with each phase completion
- Document bridge API in separate file
- Archive completed phases to DONE.md
- Maintain vitaki-fork backend API documentation