# Zconfig

**Version:** 26H1

Zconfig is a modern, powerful, English-based configuration language designed as a Kconfig-compatible backend with a rich, interactive TUI powered by FTXUI. It enables robust configuration management tailored for bare-metal systems, providing first-class validation, dependency implication resolution, and dynamic frontend interaction capabilities.

## Dependencies

- **C++23 Compiler:** `clang++` (recommended) or `g++`.
- **Build System:** CMake 3.31+ and Ninja (or Make).
- **Libraries:**
  - [FTXUI](https://github.com/ArthurSonzogni/FTXUI) (TUI rendering UI)

*Note: The CMake configuration links against `fmt`, `ftxui-component`, `ftxui-dom`, and `ftxui-screen`.*

## Compilation

Build the project out-of-tree using CMake and Ninja:

```bash
mkdir build
cd build
cmake -GNinja ..
ninja
```

## Usage

Run the compiled executable. If no path is provided, it will typically default to looking for the root `Zconfig` entry file.

```bash
./build/zconfig
```

**TUI Keybindings:**
- `↑` / `↓` / `j` / `k`: Navigate selection
- `→` / `l`: Expand node or enter options panel
- `←` / `h`: Collapse node or return to includes panel
- `Space`: Toggle boolean options
- `Enter`: Edit current option / Jump to source (in validation menu)
- `/`: Open global fuzzy finder
- `v`: Show validation report
- `s`: Save configuration and invoke generator backend
- `d`: Reset option to default
- `h` / `F2`: Details/Extended Help
- `?` / `F1`: Show help popup
- `q` / `Esc`: Quit

## Language Specification

refer to the language proposition in `doc/language.md`.

## License

This project is licensed under the **Apache License Version 2.0**. See the `LICENSE` file for full details.
