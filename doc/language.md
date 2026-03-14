# The zconfig fornat  

### Basic types and declaration
Every symbol is declared with a type, a default, and optional metadata in a trailing block.
```c
# Simple bool flag
option SMP : bool = false {
    label  "Symmetric multiprocessing"
    help   "Enable support for multiple CPU cores."
}

# Integer with range constraint
option NR_CPUS : int = 4 {
    label  "Max CPU count"
    range  1 .. 512
    when   SMP
}

# String with validation pattern
option KERNEL_VERSION : string = "6.1.0" {
    label   "Kernel version string"
    pattern /^\d+\.\d+\.\d+/
}

# Enum type
option ARCH : enum = x86_64 {
    label   "Target architecture"
    choices x86_64 | arm64 | riscv64 | loongarch
}
```

### Grouping — menu
Menus create nested hierarchy with their own visibility condition. Options inside inherit their parent's when automatically.
```c
menu "Memory management" {
    option SPARSEMEM : bool = false {
        label "Sparse memory model"
    }

    option SPARSEMEM_VMEMMAP : bool = true {
        label "Use vmemmap for pfn_to_page"
        when  SPARSEMEM
    }

    menu "NUMA" when SMP {
        option NUMA : bool = false {
            label "Non-uniform memory access"
        }
    }
}
```

### Dependencies
Instead of depends on A && (B || !C), use when with natural boolean expressions. select becomes implies, which is clearer — enabling X doesn't silently smash Y anymore; the user sees it as a suggestion.

```c
option VIRTIO_NET : bool = false {
    label   "VirtIO network driver"
    when    VIRTIO and NET
    implies VIRTIO_RING          # softer than select: shows a prompt, not a silent force
    implies VIRTIO_PCI or VIRTIO_MMIO
}

option EXPERT : bool = false {
    label   "Expose expert/dangerous options"
}

option KALLSYMS_ALL : bool = false {
    label   "Include all symbols in kallsyms"
    when    KALLSYMS and EXPERT
}
```

### Choice group
Instead of the choice...endchoice block that has its own scoping quirks, choices are just an option of enum type. Exclusive and non-exclusive variants are explicit.
```c
# Exclusive — only one may be active (radio)
option PREEMPT_MODEL : enum = voluntary {
    label   "Preemption model"
    choices none | voluntary | full | realtime
    # Each choice can carry its own label/help
    choice none      { label "No preemption (server/HPC)" }
    choice voluntary { label "Voluntary (desktop default)" }
    choice full      { label "Full preemption" }
    choice realtime  { label "PREEMPT_RT (low latency)"; when EXPERT }
}

# Non-exclusive — multiple allowed (checkboxes)
option DEBUG_FEATURES : set = {} {
    label   "Debug feature flags"
    choices lockdep | kasan | ubsan | kcsan
}
```

### Conditional defaults
Defaults can vary based on environment without separate default X if Y lines scattered everywhere.
```c
option HZ : int {
    label  "Timer frequency (Hz)"
    range  100 .. 1000
    default {
        1000 when PREEMPT_MODEL == realtime
        250  when PREEMPT_MODEL == full
        100  # fallback
    }
}
```

### Computed (read-only) symbols
Derived values that the user cannot edit — they reflect the state of other options. Useful for feature-flag aggregation.
```c
computed HAVE_KVM : bool = ARCH == x86_64 or ARCH == arm64
```

### Includes and namespacing
Clean modular splits without the source keyword oddness. Optional namespace prefixing so arch-specific symbols don't bleed globally.
```c
include "arch/x86/Kconfig"          # flat include
include "drivers/net/Kconfig" as net  # namespaced: net.VIRTIO_NET etc.
include "drivers/net/Kconfig" when NET # conditional include
```

### TUI metadata
First-class hints for the FTXUI renderer — search tags, grouping color, danger level, and whether to collapse by default.
```c
option KASAN : bool = false {
    label    "Kernel address sanitizer"
    help     "Detect use-after-free and out-of-bounds. ~2× memory overhead."
    tags     ["debug", "memory", "sanitizer"]
    danger   warning        # none | warning | critical
    collapsed               # collapsed in TUI by default
}
```