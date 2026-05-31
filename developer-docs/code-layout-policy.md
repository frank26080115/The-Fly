## C/C++ File Layout Guide

This document defines the preferred layout for C and C++ implementation files.

The goal is to make source files readable from the highest-level behavior down to the lowest-level implementation details. Prefer a “top-down story” layout over placing tiny helper functions near the top just because they are used early.

### Core Principle

Implementation files should be organized so that the most important, highest-level functions appear first.

Small helper functions should usually appear later, even if this requires a larger function prototype block near the top.

Function declarations/prototypes are allowed and encouraged when they improve file readability.

### Preferred Ordering

Within a `.c`, `.cpp`, `.ino`, or similar implementation file, arrange content in this general order:

1. File header comment, if present
2. Includes
3. Private configuration constants and macros
4. Private type declarations
5. Static/global variable declarations
6. Function prototypes, extern first block, static second block, and then the rest
7. Primary public or top-level functions
8. Major feature functions
9. Medium-sized supporting functions
10. Small helper functions
11. Debug, logging, formatting, or utility helpers

### Function Ordering Rule

Prefer ordering function definitions from longest / most important to shortest / least important.

This is not an exact line-count rule, but it is the preferred direction.

A long function that represents the main behavior of the file should appear before a tiny helper function it calls.

A tiny helper function should not be moved to the top merely to avoid writing a prototype.

### Prototype Block

Use a dedicated prototype block near the top of the file when needed.

Example:

```cpp
// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool initializeStorage();
static bool loadConfiguration();
static void startRecordingSession();
static void stopRecordingSession();
static void updateRecordingDisplay();
static const char* recordingStateToString(RecordingState state);
```

A large prototype block is acceptable.

Do not avoid prototypes by moving helper implementations above the main logic.

### Section Headers

Use clear section headers to make reordering easy.

Preferred style:

```cpp
// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------
```

```cpp
// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------
```

### Main Flow First

Functions such as these should appear early:

```cpp
setup()
loop()
appMain()
initializeSystem()
runMainStateMachine()
handleMainScreen()
startRecording()
stopRecording()
```

These functions define the shape of the program and should be easy to find.

### Helpers Later

Functions such as these should usually appear later:

```cpp
clampValue()
formatBytes()
stateToString()
isWhitespace()
readUint16LE()
printDebugLine()
```

These are implementation details and should not interrupt the main flow.

### Avoid This Pattern

Avoid placing many tiny helpers at the top of the file just because later functions depend on them.

Bad layout:

```cpp
static int clampValue(...)
static bool isValidChar(...)
static const char* stateToString(...)
static void printDebug(...)
static void setupRecording(...)
static void startRecording(...)
static void loop(...)
```

Preferred layout:

```cpp
// prototypes first

static void setupRecording(...)
static void startRecording(...)
static void loop(...)

static int clampValue(...)
static bool isValidChar(...)
static const char* stateToString(...)
static void printDebug(...)
```

### Preserve Behavior

When applying this layout:

* Do not change program behavior.
* Do not rename functions unless explicitly requested.
* Do not change function signatures unless explicitly requested.
* Do not change include order unless needed.
* Do not alter logic just to make the layout prettier.
* Prefer moving whole function definitions as intact blocks.

### Comments

Prefer comments that explain sections and intent.

Avoid excessive comments explaining obvious syntax.

Good:

```cpp
// Handles the high-level recording lifecycle.
```

Less useful:

```cpp
// Increment i by one.
```

### Final Goal

A reader should be able to open the file and understand the major behavior first, then scroll downward into implementation details.

The file should read like:

1. What this file does
2. How the major flow works
3. How each feature works
4. The small details and helpers
