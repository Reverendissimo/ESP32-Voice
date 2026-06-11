# Coding Style Directive

## Goal

This document is mandatory for the whole codebase.

The project must optimize for:
- clarity
- small files
- explicit responsibilities
- maintainability
- safe embedded behavior

## Language and structure rules

- Use C++ with ESP-IDF.
- Prefer composition over inheritance.
- One class per file.
- Header/source pair per class when appropriate.
- File names must match the class name or responsibility.
- Do not place multiple unrelated classes in one file.
- Keep each file focused on one responsibility.
- Avoid files that become “misc”, “utils”, or “manager” dumping grounds unless tightly scoped.

## Naming rules

- Class names: `PascalCase`
- Methods/functions: `camelCase`
- Member variables: `m_foo`
- Constants: `kConstantName`
- Macros: `ALL_CAPS_ONLY_IF_REQUIRED`
- File names: `snake_case` or exact responsibility names, consistent across project

Examples:
- `config_store.hpp`
- `config_store.cpp`
- `wifi_manager.hpp`
- `wifi_manager.cpp`
- `audio_upload_service.hpp`
- `audio_upload_service.cpp`

## File size rules

Targets, not absolute laws:
- header files: preferably under 200 lines
- source files: preferably under 300 lines
- split when a file starts mixing responsibilities

## Commenting and docstrings

This is mandatory.

### File header comment

Every non-trivial file must start with a file header comment explaining:
- what the file is for
- what the class/module owns
- what it does not own

Example:

```cpp
/**
 * @file wifi_manager.hpp
 * @brief Owns Wi-Fi connection lifecycle for station mode and fallback AP mode.
 *
 * Responsibilities:
 * - connect/disconnect
 * - expose current Wi-Fi state
 * - apply Wi-Fi config changes
 *
 * Non-responsibilities:
 * - persistent storage of config
 * - HTTP API handling
 */
```

### Class docstring

Every class must have a docstring comment.

```cpp
/**
 * @brief Manages runtime and persisted configuration state.
 *
 * This class owns:
 * - active config in RAM
 * - loading saved config from NVS
 * - validating config patches
 * - saving active config to NVS on explicit request
 */
class ConfigManager { ... };
```

### Public method docstring

Every public method must have a docstring comment.

```cpp
/**
 * @brief Applies a partial config patch to the active RAM config.
 *
 * @param patch Parsed patch object.
 * @return true if the patch is valid and applied.
 * @return false if validation failed.
 */
bool applyPatch(const ConfigPatch& patch);
```

### Internal comments

- Comment intent, not obvious syntax.
- Explain hardware quirks, timing assumptions, and failure handling.
- Explain why something is done if not obvious.
- Avoid useless comments like `// increment counter`.

## Class design rules

- Prefer small service classes.
- Each class should have one clear reason to change.
- Hide hardware-specific details behind clear interfaces.
- Keep parsing, persistence, transport, and business logic separate.
- Do not mix REST handler code with low-level hardware code.

## Error handling rules

- Return explicit status objects, enums, or `esp_err_t` where appropriate.
- Log failures with enough context.
- Never swallow errors silently.
- Avoid boolean-only returns if debugging context is important.

## Logging rules

- Use ESP-IDF logging consistently.
- One log tag per file or class area.
- Log at appropriate levels: error, warn, info, debug.
- Do not spam logs in hot loops.
- Never log secrets such as Wi-Fi passwords or auth tokens.

## Configuration rules

- Config schema must be centralized.
- Active config in RAM and saved config in NVS must stay clearly distinct.
- Validation logic must not be scattered randomly.
- Thresholds and alarm behavior must be explicit and documented.

## REST/API rules

- Keep request parsing separate from business logic.
- Define typed request/response models where practical.
- Validate all incoming payloads.
- Reject unknown or invalid critical fields cleanly.
- Include device identity and request correlation fields consistently.

## Concurrency rules

- Document ownership of shared state.
- Minimize mutable shared state.
- Protect shared resources explicitly.
- Keep task boundaries clear.
- Avoid doing heavy work directly inside HTTP handlers.

## Forbidden patterns

Do not do these:
- monolithic `main.cpp`
- one huge `device_manager` that owns everything
- giant switch-based files with all endpoints together and no structure
- duplicated config field parsing in multiple places
- direct hardware access from unrelated business logic layers
- undocumented magic numbers
- hidden singleton abuse
