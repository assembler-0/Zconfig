# Zconfig Development Conventions

This document defines the standards for extending and maintaining the Zconfig project and for writing `.z` configuration files.

## 1. Project Architecture & C++ Standards

### 1.1 Language & Tooling
*   **Standard**: C++23 (Strict).
*   **Compiler**: LLVM/Clang.
*   **Build System**: CMake 3.31+ with Ninja.

### 1.2 File Structure
*   `include/zconfig/`: Public headers. Use `#pragma once`.
*   `src/`: Implementation files.
*   `doc/`: Specifications and conventions.

### 1.3 Error Handling
*   Never use exceptions. Use `std::expected` via the `Result<T>` alias defined in `error.hpp`.
*   Use the `TRY(x)` macro for propagating errors in the parser.
*   Always provide a `Diagnostic` with file, line, and column for user-facing errors.

---

## 2. Zconfig Language Conventions

### 2.1 File Extensions
*   `Zconfig` or `.zconfig`: The main entry point of a project.
*   `.z`: Modular configuration fragments (included files).

### 2.2 Naming Standards
*   **Symbols**: `UPPER_SNAKE_CASE` (e.g., `CPU_COUNT`, `ENABLE_SMP`). This maintains compatibility with Kconfig expectations.
*   **Namespaces**: `lower_snake_case`. Used when including files with the `as` keyword (e.g., `include "net.z" as net`).
*   **Labels**: Title Case (e.g., `label "Symmetric Multiprocessing"`).

### 2.3 Modularization & Namespacing
*   **Encapsulation**: Prefer namespaced includes for drivers or subsystem-specific fragments to avoid global namespace pollution.
    ```c
    include "drivers/video.z" as video
    ```
*   **Conditional Includes**: Use `when` on includes to keep the registry lean. If a subsystem is disabled, its symbols should not be parsed.
    ```c
    include "wifi.z" as wifi when NET_WIRELESS
    ```

---

## 3. Configuration Best Practices

### 3.1 Metadata Usage
Every user-editable `option` should strive to provide:
*   `label`: A short, descriptive title for the TUI.
*   `help`: A detailed explanation of what the option does.
*   `tags`: Category hints for the TUI search engine (e.g., `tags ["debug", "mem"]`).

### 3.2 Logic & Dependencies
*   **Visibility**: Use `when` for strict dependencies. If `when` is false, the option is hidden and its value is ignored.
*   **Suggestions**: Use `implies` (soft-select) for recommendations. This notifies the user/TUI without forcibly overriding the value like a Kconfig `select`.
*   **Computed Symbols**: Use `computed` for read-only aggregations or feature flags that shouldn't be user-edited.

### 3.3 Ranges & Validation
*   Always define a `range` for `int` types to prevent invalid system states.
*   Use `pattern` (Regex) for `string` types that represent paths, versions, or identifiers.

---

## 4. TUI Optimization

*   **Menus**: Group related options into `menu` blocks to create a navigable hierarchy.
*   **Danger Levels**: Mark high-risk options (e.g., experimental drivers) with `danger warning` or `danger critical` to visually alert the user.
*   **Collapsed State**: For advanced or rarely changed options, use the `collapsed` attribute to keep the TUI clean by default.
