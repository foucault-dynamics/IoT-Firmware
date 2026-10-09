# Commenting convention

Every comment in `lib/` and `src/` follows the rules below, so the codebase reads
the same everywhere and Doxygen can build the docs site from it. The build fails
on any missing or malformed doc comment (`WARN_AS_ERROR` in the `Doxyfile`).

## Rules

| What | Form | Where |
|---|---|---|
| Every file | `/** @file` then a one line summary, optional paragraph `*/` at the very top | `.h` and `.cpp` |
| Class, struct, enum | `/** */` block directly above it. First sentence is the summary, then the "why" | header |
| Function | `/** */` with `@param[in]`, `@param[out]` or `@param[in,out]` for every parameter, and `@return` or `@retval` for anything that is not `void` | header only, never repeated in the `.cpp` |
| Status codes | One `@retval` per value, e.g. `@retval EXIT_SUCCESS`, `@retval EXIT_FAILURE`, `@retval -1` | header |
| Override that differs from its base | Its own `/** */` describing only what differs, and the full `@param`/`@retval` set | header |
| Struct field, enum value, short member | Trailing `///<` on the same line. Put units in it (`ms`, `baud`, `Hz`, `dBm`) | header |
| One line declaring several fields (`uint8_t rx, tx;`) | `/** */` above, then wrap the line in `///@{` and `///@}` so every field gets the comment | header |
| File local `static` function or variable | Same rules as a public one | `.cpp` |
| Comment inside a function body | Plain `//`, short | `.cpp` |
| Known gap or unfinished work | `@todo`. Doxygen collects every one into the Todo List page | anywhere |
| Pointer to related code | `@see` | anywhere |
| Gotcha the caller must know | `@note` or `@warning` | anywhere |

Always use `@` for commands, never `\`. Never write `@brief`, because the first
sentence already is the brief (`JAVADOC_AUTOBRIEF`). End that first sentence with
a full stop.

## Example

```cpp
/**
 * @file
 * Store and forward buffer for readings waiting on a gateway ACK.
 */

/**
 * Queues a reading for upload.
 *
 * @param[in] p  Reading to copy into the buffer.
 * @retval true   Queued.
 * @retval false  Buffer full, reading dropped.
 */
bool readingBufferPush(const Payload &p);

/** Radio parameters for the SX1276. */
struct LoRaConfig {
  uint32_t band;            ///< Carrier frequency in Hz, e.g. 433E6.
  uint8_t spreadingFactor;  ///< 7 to 12. Higher reaches further but is slower.
};
```

## Building the docs locally

Install the same Doxygen version CI uses (1.18.0) and Graphviz, then run it from
the repo root:

```sh
brew install doxygen graphviz
doxygen
open build/docs/index.html
```

A clean run prints nothing and exits 0. Any output is a missing or broken
comment, with the file and line to fix.

The theme is [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css)
v2.5.0, vendored as `docs/doxygen-awesome.css`. To upgrade, replace that file
with the new release's copy.
