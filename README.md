# ct — header-only C++14 containers for games

Small containers for C++14 game code. No exceptions, no RTTI, single-threaded,
header-only. They lean on things the STL can't assume: that the element type
is trivial, that the work is per-frame, that a dangling pointer is worse than
a `fatal()`. Wherever a trade-off was made, it's written down next to the code
instead of being hidden.

## What's in the box

| Container | Notes |
|---|---|
| `ct::Vector<T, Alloc>` | `realloc`/`memcpy` for trivial types, `resize` without initialization, allocator policy via EBO |
| `ct::Arena` + `ArenaAlloc` | block bump allocator, `try_expand` (in-place growth), `reset()` recycles per frame |
| `ct::Pool<T>` | typed free list for game objects; raw API, no ctors |
| `ct::String` | 24 B with 23-byte SSO (`std::string`: 32 B, SSO 15); find/split/trim/number/hash; x15-20 in the 16-23 char range |
| `ct::HashMap<K,V>` | open addressing, linear probing, backward-shift erase; x2-13 vs `unordered_map` |
| `ct::HashSet<K>` | same technique; x5.4 vs `unordered_set` |
| `ct::FlatMap<K,V>` | sorted (`Vector` + binary search); x40-50 iteration vs `std::map` |
| `ct::TreeMap<K,V>` | red-black tree, nodes live in a `Pool`; x1.4 insert, x1.2 churn vs `std::map` |
| `ct::Deque<T, Alloc>` | power-of-two ring buffer (wrap = AND); x1.9-2.4 at the ends, x2 FIFO, x1.8 access vs `std::deque`; linear spans for vectorized iteration (x2.6-3) |
| `ct::Stack<T>` | LIFO adaptor over `Vector`, zero overhead; x1.2-3.3 vs `std::stack` (deque-backed), x1.2-2.9 vs vector-backed |
| `ct::Queue<T>` | FIFO adaptor over `Deque`; x1.2-2.1 vs `std::queue`; comes with `reserve()` and `clear()` |
| `ct::Array<T,N>` | fixed array (`sizeof == N*sizeof(T)`, trivial/standard-layout); constexpr access, `at()` is `fatal()`, `fill` via memset, `==`/`<` via memcmp where it's safe |
| `ct::Span<T>` / `ct::StringView` | non-owning views (two words); bind `Vector`/`Array`/`String`/C arrays without copying; `split_once`/`trimmed` parse configs without allocating |
| `ct::Unique<T>` / `Rc<T>` / `Weak<T>` | RAII without atomics; `make_rc` does a single allocation with the control block glued to the object; x2.6 copy vs `shared_ptr` |
| `ct::SlotMap<T>` + `Handle<T>` | generational handles over a dense array; a dead handle is detected instead of leaving a dangling pointer; x15.8 iteration and x1.9 lookup vs `unordered_map` |
| `ct::Json` | full RFC 8259 on `String`+`Vector`; x2.5-3.4 parse and x9.7 lookup vs nlohmann; 32 B per value; 0.14 s compile vs 0.93 s |
| `ct::Xml` | pragmatic subset for reading/writing Tiled TMX/TSX-style XML; no namespaces/DTD/XPath (out of scope on purpose); parse + `dump(indent)` |
| `ct::Function<R(Args...)>` | type-erased callback like `std::function`; 3-pointer SBO without allocating, only big targets go to the heap; copyable if the target is |
| `ct::Variant<Ts...>` | tagged union, closed set of types; no allocation (storage is the largest of `Ts`); get/get_if/is/visit |
| `ct::sort` | O(n) radix for numbers (x2.5-4.5 vs `std::sort`) + generic introsort (x1.15) |

Rule of thumb for games: **Arena** for what dies at the end of the frame
(`reset()` recycles everything, memory settles at the peak of one frame);
**Pool** for what is born and dies mid-game (individual free via the free list).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest            # tests
./ct_bench                   # benchmarks vs std
```

## Deque

- Contiguous ring buffer (power-of-two capacity, wrap with AND) instead of
  `std::deque`'s block map — no double indirection, no divisions.
- Deliberate trade-off: growing **invalidates pointers/iterators** (`std::deque`
  keeps the ends stable). Call `reserve()` if you need stability.
- No `insert`/`erase` in the middle — it's a queue/window, not a vector.
- For hot loops, `first_span()`/`second_span()` hand you the (up to) two linear
  segments — iterating by span vectorizes (x2.6-3 vs range-for over `std::deque`).

## Vector

- Trivially copyable types grow with `realloc` (can grow in place) and relocate
  with `memcpy` → 5-9x faster than `std::vector` for `push_back` without `reserve`.
- `resize(n)` on trivial types does **not** initialize the new elements — use
  `resize(n, T())` if you want zeros.
- Don't mark the growth path `noinline`: leaving it inline lets GCC keep `cap_`
  in a register across the hot loop (learned the hard way, reading the asm).

## Json

32-byte value: a `String` with 23-byte SSO inline (most keys and short strings never
malloc), arrays and objects are a pointer to a `Vector`. No exceptions — malformed
input returns `Json::Error` with a message, line and column; using the wrong type
(asking `items()` on a number) is `fatal()`.

- Objects keep **insertion order** (a `Vector` of members plus a linear search), so a
  round-trip returns the file in the same order — unlike nlohmann's `std::map`, which
  reorders keys. Duplicate keys both survive; `find()` returns the first.
- Parse depth is capped (`kMaxDepth`, 200) — a file of `[[[[[...` from outside can't
  blow the stack.
- Numbers: integers stay `Int`/`Uint` (int64/uint64, no precision loss on large ids)
  and only become `double` when they don't fit. `1.0` dumps as `"1.0"` so the type
  survives a round-trip. A number out of double range (`1e999`) is a **parse error**,
  not infinity — accepting it would make `dump` write `null` and silently drop the value.
- **UTF-8 is not validated**: invalid bytes (old latin-1 files) pass through as-is,
  where nlohmann rejects the whole file. On purpose — we'd rather not lose an asset
  file over one mis-encoded `ç`.
- Own dtoa: digits are generated by scaling in `long double` and **round-trip is
  verified** before accepting, with a fallback to `snprintf`. Tested over 3.2 M random
  doubles without a single failure. Locale-independent (the system's `strtod`/`snprintf`
  break under `LC_NUMERIC` with a decimal comma; this one doesn't).

Validation: **397 real JSON files** (7.1 MB, 166 740 nodes) compared node-by-node
against nlohmann — zero value differences, zero failed round-trips. Another 240k of
garbage inputs and mutations of real files under ASan/UBSan, no crash, no leak. And
3.2 M random doubles with no round-trip failure.

Measured against `nlohmann::json` 3.11.3 on real Radion scenes (`ct_bench_json`):

| | ct | nlohmann | |
|---|---|---|---|
| parse (31 KB) | 15.9 ms | 53.3 ms | **x3.34** |
| full walk | 3.6 ms | 9.7 ms | **x2.66** |
| lookup by key | 0.49 ms | 4.77 ms | **x9.74** |
| build from scratch | 8.5 ms | 17.2 ms | **x2.02** |
| compact dump | 13.2 ms | 10.2 ms | x0.77 |
| dump(4) | 15.7 ms | 13.1 ms | x0.84 |

Dump still loses because nlohmann ships grisu2 to format doubles (our serializer
itself is ~1.9x faster — the whole gap is the dtoa). Compile time: `ct/json.hpp` is
16 298 preprocessed lines and 0.14 s per TU, vs 98 564 lines and 0.93 s for
`nlohmann/json.hpp`.

## Xml

A node is always an element (name, attributes, children, text). No exceptions, same
as `Json`: `Xml::Error` for malformed input, getters default for everything else.

- Deliberately small scope — it exists for [Tiled](https://www.mapeditor.org/) maps
  (`.tmx`/`.tsx`), not as a generic XML parser. Out of scope: namespaces, DTD/external
  entities (only the 5 predefined — `&amp; &lt; &gt; &quot; &apos;` — plus numeric refs
  `&#N;`/`&#xN;`), XPath/XSLT/schema validation. A `<!DOCTYPE>` is tolerated and ignored,
  never processed.
- `CDATA` comes in raw (no entity decode) — that's the common path for tile data in
  base64/csv. Text and CDATA concatenate in `text()`; whitespace-only text between
  children (just indentation) is dropped automatically, but a childless element keeps
  it as-is (`text_trimmed()` trims the ends where it doesn't matter, like in `<data>`).
- `dump(indent)` follows `Json::dump`'s convention (`< 0` compact, `>= 0` one element
  per line); `dump_document()` prepends the `<?xml ...?>` declaration.
- Same trick as `Json` for the self-reference problem: `children_` is a separately
  allocated `Vector<Xml>*` (lazily — only on the first child), not a direct value.
- Validated with 48 GTest tests plus 200k random mutations of TMX-like files under
  ASan/UBSan — no crash, no leak.

## Function and Variant

Two generic primitives, not tied to any file format. The difference is the question
they answer:

- **`Function<R(Args...)>`** stores **anything callable** with that signature (free
  function, lambda, functor, `std::bind`) behind type erasure — open set, the
  `Function` neither knows nor cares what the concrete type is.
- **`Variant<Ts...>`** stores **one value from a closed set** of compile-time-known
  types — it never allocates, storage is just the largest of `Ts...` inline.

```cpp
ct::Function<int(int,int)> f = [](int a, int b) { return a + b; };
f(2, 3); // 5

ct::Variant<int, double, ct::String> v = 42;
v.is<int>();       // true
v = ct::String("x");
v.visit(Printer{}); // dispatches to the right operator() for the active type
```

- `Function` copies the target through a 3-pointer SBO (24 B) — covers most callback
  lambdas ("capture `this` plus a couple of values"); only the larger ones hit the heap.
  Copying a `Function` requires the stored target to be copyable too (same as
  `std::function`). Calling an empty `Function` is `fatal()`, not `std::bad_function_call`.
- `Variant` recurses over `Ts...` to destroy/copy/move the active alternative — no
  per-index vtable, and the list is usually short. A wrong `get<T>()` is `fatal()`;
  `get_if<T>()` returns `nullptr` for the path where that's normal.
- Both store the value via `reinterpret_cast` over a byte buffer — the same trick
  `std::function`/`std::variant` use under the hood (legal by `basic.life`).

## SlotMap

The problem it solves: you hold a `Body*` or `Entity*` and it dangles when the object
dies. Here you hold a `Handle<T>` (8 bytes, typed — you can't swap an entity's for a
body's) and the map detects dead handles:

```cpp
ct::SlotMap<Body> bodies;
auto h = bodies.insert(Body{...});
bodies.erase(h);
bodies.get(h);          // nullptr, not garbage — the slot was reused with a new generation
for (Body &b : bodies.items()) b.integrate(dt);   // dense and contiguous
```

- Objects sit in a **dense** array (`erase` back-fills the hole with the last one), so
  iterating is like iterating a `Vector` — none of the cache misses of a node map.
- The generation is odd while a slot is alive, even when free, and bumps on every
  `erase`: an old handle never matches again, even after the slot is reused.
  `Handle{}` (generation 0) is never valid.
- **Addresses are not stable** — the handle is. Don't keep `T*` across frames, and don't
  erase while iterating `items()` (collect handles, erase after).
- `operator[]` is `fatal()` on an invalid handle; `get()` returns `nullptr` for the
  path where object death is normal.

With 100 000 32-byte bodies (`ct_bench_slotmap`):

| | ct::SlotMap | std::unordered_map | |
|---|---|---|---|
| iterate + integrate | 5.7 ms | 90.3 ms | **x15.8** |
| lookup by handle | 10.0 ms | 18.7 ms | **x1.87** |
| erase half, refill | 5.4 ms | 27.7 ms | **x5.09** |

Against a bare `Vector`, iterating `items()` costs the same (x1.03 — it literally is
the same array), and handle access costs **x0.41** vs indexing a raw index: two
dependent reads plus the generation check. That's the price of not having dangling
pointers.

## RAII pointers

```cpp
ct::Unique<Texture> t = ct::make_unique<Texture>("floor.png");  // 8 B, one owner
ct::Rc<Mesh> m = ct::make_rc<Mesh>(...);                       // several owners
ct::Weak<Mesh> obs = m;                                        // observes, doesn't hold
if (ct::Rc<Mesh> alive = obs.lock()) draw(*alive);             // or it comes back empty
```

Counters are **plain, non-atomic** — rendering is single-threaded, like the rest of the
lib. An `Rc` must never cross threads (two concurrent copies corrupt the count).

- `make_rc` does **one** allocation: `[Ctrl][padding][T]`, with the counter on the same
  cache line as the object. `Ctrl` is 16 B (two `uint32` + an operation pointer), vs the
  24-32 B std block with its deleter vtable.
- The block knows how to destroy **the type it was created with**: `Rc<Base> b =
  make_rc<Derived>()` runs `~Derived` even without a virtual destructor (with
  `unique_ptr`/`delete` that's UB).
- `Rc` is 16 B (pointer + block) to allow upcasting. It could be 8 B by deriving the
  block from the object, but then `Rc<Derived>` → `Rc<Base>` would stop compiling — and
  copying wouldn't get any faster anyway; the win is in the counter, not the size.
- `Rc` cycles still leak, as with any refcount: the back edge must be `Weak`.

Pick order: **Arena** (dies at end of frame) → **Pool** or **Unique** (one owner) →
**SlotMap + Handle** (entities/bodies, with death detection) → **Rc/Weak** only for
genuinely shared ownership with a dynamic lifetime (assets referenced by N entities).
An 8-byte `Handle` does the job of a `weak_ptr` without a control block or refcount,
and still iterates dense.

| 2 M operations | ct | std | |
|---|---|---|---|
| copy (refcount) | 20.0 ms | 52.4 ms | **x2.62** |
| make + destroy | 23.9 ms | 27.4 ms | x1.14 |
| `Unique` make + destroy | 19.4 ms | 23.9 ms | x1.23 |

## Tests

`ct_tests` covers every container above; `ct_torture` fuzzes the containers with random
operations. CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs the full
suite with GCC and Clang in strict Debug mode, compiles each public header standalone,
then runs ASan/UBSan plus `ct_torture` with three seeds. No benchmarks run there — those
numbers only make sense on a fixed machine, not on shared CI runners.
