# CLAUDE.md - AI Assistant Instructions

## Project Overview

You are working on **VitaRPS5**, a modern PlayStation 5 Remote Play client for PS Vita forked from vitaki-fork. This is a performance-optimized fork of Vitaki (based on Chiaki) targeting sub-50ms latency with hardware-accelerated decoding and professional code quality.

## Core Principles

### Code Quality (Based on Anthropic Best Practices)
- **No shortcuts**: Write complete, production-ready code
- **No commented code**: Delete unused code instead of commenting it out
- **Proper modularization**: Keep functions in relevant modules, avoid duplication
- **Reuse existing solutions**: Import proven modules rather than reinventing
- **Clean file structure**: Organize code logically with clear separation of concerns

### Development Standards
- **Follow C best practices**: Memory safety, error handling, const correctness
- **Performance-first**: Optimize for PS Vita's ARM Cortex-A9 constraints
- **Test on hardware**: Validate all implementations on actual Vita
- **Document decisions**: Explain non-obvious implementation choices

## Technical Reference

### Essential Documentation
- Important files are in `docs/ai` directory
- the `docs/ai/current_status/` subdir should be used only to keep track of the current development phase and should be updated when we switch to a new phase.
- Keep a list of tasks to be done in `docs/ai/current_status/TODO.md`
- As tasks get completed, move them from `docs/ai/current_status/TODO.md` to `docs/ai/current_status/DONE.md`
- Archived tasks go in `docs/ai/current_status/ARCHIVED.md`
- Always look at and update `docs/ai/ROADMAP.md` so we know what we are working on.
- Phases can have subphases in the roadmap. We should add them as we see fit.
- Always move sequentially across phases. Finish one before starting another. Even if it is a minor phase (unless it is a blocker, in which case ask me if we should re-arrange them).

### Key Technical Facts
- **Hardware Decode**: Use SceVideocodec for sub-50ms latency

## Response Guidelines

### When Writing Code
1. **Complete implementations** - No TODO comments or placeholder code
2. **Proper error handling** - Check all return values with descriptive errors
3. **Performance consideration** - Minimize allocations, use hardware acceleration
4. **Modular design** - Keep related functions together, avoid duplication
5. **Clean structure** - Follow established patterns in the codebase

### Code Quality Checklist
- [ ] Compiles without warnings
- [ ] Includes comprehensive error handling
- [ ] Follows project naming conventions
- [ ] Fits established architecture
- [ ] No code duplication
- [ ] Performance optimized for Vita

### Implementation Approach
1. **Analyze existing code** - Understand current patterns before adding
2. **Reuse proven modules** - Import from Chiaki-ng/Vitaki when appropriate
3. **Focus on integration** - Build on existing foundations, don't recreate
4. **Test incrementally** - Validate each component before proceeding
5. **Document complexity** - Explain non-obvious design decisions

## Key Success Metrics

- **Sub-50ms latency** input-to-display
- **Zero crashes** during 30+ minute sessions
- **Clean modular code** with no duplication
- **Professional quality** matching commercial apps

## Build System

Use only the provided build script:
```bash
./tools/build.sh              # Release build
./tools/build.sh debug        # Debug build
./tools/build.sh shell        # Development shell
./tools/build.sh test         # Execute tests
```

**Never call Docker manually** - always use the build script.

## Common Anti-Patterns to Avoid

- Commenting out dead code instead of deleting
- Duplicating functions across modules
- Implementing custom solutions when proven libraries exist
- Skipping error handling for "simple" operations
- Using magic numbers instead of named constants
- Creating monolithic functions instead of composable modules

Remember: Write code as if it will be maintained by others for years. Every line should have a clear purpose and place in the architecture.
