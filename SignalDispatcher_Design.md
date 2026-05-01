# SignalDispatcher
## In-Process Communication System

## 1. Overview

SignalDispatcher is a lightweight, in-process communication system that enables point-to-point, point-to-many, and point-to-subset messaging between components. All communication is performed in memory via typed signal dispatch using static cast thunks — no `dynamic_cast`, no virtual dispatch on the hot paths.

The system is designed around three core principles:

- **Fail fast and visibly** — all collision and error conditions are rejected and logged as warnings, never silently dropped or overwritten.
- **Minimal hot path overhead** — dispatch is a single map lookup, a static cast, and a direct member function call.
- **Caller responsibility** — endpoints manage their own lifecycle. Disconnecting before destruction is an explicit contract, not an implicit guarantee.

---

## 2. Architecture

### 2.1 Signal Hierarchy

All signals derive from a common polymorphic base class `Signal`. The virtual destructor makes the type polymorphic, which is required for correct `std::type_index` resolution during dispatch. Derived types are intended to carry their own data.

```cpp
class Signal {
public:
    virtual ~Signal() = default;
    const std::string& senderID() const;
};

class AlertSignal : public Signal {
    float temperature_;
};
```

### 2.2 Internal Maps

The `SignalDispatcher` maintains three internal maps. Each holds the same underlying registration data indexed by a different key, optimised for a specific access pattern:

| Map | Key | Purpose |
|---|---|---|
| Alias Map | `alias + type_index` | Hot path for `sendTo()` and `multicast()`. One lookup returns the thunk directly. |
| Type Map | `type_index` | Hot path for `broadcast()`. Returns all subscriber thunks for a signal type. |
| Endpoint Map | `stringID` | Management only. Used by `bind()` and `disconnect()` to track and clean up all bindings per endpoint. |

All three maps are written together atomically under a write lock during `bind()` and `disconnect()`. They always represent the same state, just indexed differently.

### 2.3 Dispatch Mechanism

At `bind()` time, a typed thunk is generated. The thunk wraps the caller's callable and performs a `static_cast` from the base `Signal` reference to the concrete derived type before invoking it:

```cpp
[f = std::forward<Callable>(callable)](const Signal& sig) {
    f(static_cast<const TSignal&>(sig));
}
```

The `static_cast` is safe because the dispatcher only invokes the thunk after confirming a `type_index` match during map lookup. The type check happens once at the map lookup; the cast itself is zero overhead.

### 2.4 Thread Safety

A `std::shared_mutex` protects all three maps. The read lock is held only for the duration of the map lookup and released before the thunk is invoked. This keeps the critical section as short as possible and allows multiple senders to dispatch concurrently at steady state.

| Operation | Lock Type | Lock Duration |
|---|---|---|
| `sendTo()` / `broadcast()` / `multicast()` | Shared (read) | Map lookup only |
| `bind()` | Exclusive (write) | Full registration |
| `unbind()` | Exclusive (write) | Full type removal |
| `disconnect()` | Exclusive (write) | Full map cleanup |
| `debug_info()` | Shared (read) | Full output build |

---

## 3. Identity Model

Each endpoint has two distinct identifiers that serve different purposes:

| Identifier | Example | Purpose |
|---|---|---|
| String ID | `com.company.sensors.unit1` | Full unique identifier. Used internally by the Broker for registration and lifecycle management. Never used for addressing. |
| Alias | `temp-sensor` | Short human-friendly name used by other endpoints to address messages. Unique per alias + signal type combination. |

The alias and signal type together form a composite key in the alias map. This means two different endpoints can share an alias as long as they handle different signal types under it. However the combination of alias + signal type must be globally unique.

---

## 4. Public API

### 4.1 `bind()`

Registers a callable handler for a specific signal type. Accepts any callable invocable with `const TSignal&` — lambdas, functors, free functions, or `std::function`. The first call for a `stringID` creates the endpoint record. Subsequent calls add additional signal type bindings to the same endpoint.

```cpp
dispatcher.bind<SensorReadingSignal>(
    "com.company.sensors.unit1",                          // stringID
    "temp-sensor",                                        // alias
    [this](const SensorReadingSignal& sig) { handle(sig); }); // callable
```

**Validation** — all failures logged as warnings, registration rejected:
- `stringID` already bound to this signal type
- `alias + signal type` combination already taken by another endpoint

### 4.2 `bind()` Usage Patterns

**Stateless lambda**
```cpp
dispatcher.bind<AlertSignal>(id, alias,
    [](const AlertSignal& sig) { std::cout << sig.getAlertText(); });
```

**Lambda capturing `this` — calling private member functions**

Lambdas defined inside a class body have access to private methods. This is the standard pattern for class-based endpoints:
```cpp
// Inside constructor — onAlert() can be private
dispatcher.bind<AlertSignal>(id, alias,
    [this](const AlertSignal& sig) { onAlert(sig); });
```

**Lambda capturing local state by value**
```cpp
int threshold = 42;
dispatcher.bind<AlertSignal>(id, alias,
    [threshold](const AlertSignal& sig) { /* use threshold */ });
```

**Free function**
```cpp
void handleAlert(const AlertSignal& sig) { ... }

dispatcher.bind<AlertSignal>(id, alias, handleAlert);
```

**Functor**
```cpp
struct AlertLogger {
    void operator()(const AlertSignal& sig) const { ... }
};

dispatcher.bind<AlertSignal>(id, alias, AlertLogger{});
```

**`std::function`**
```cpp
std::function<void(const AlertSignal&)> handler = /* ... */;
dispatcher.bind<AlertSignal>(id, alias, handler);
```

> **Lifetime warning** — capturing by reference is unsafe if the referenced object can be destroyed before `disconnect()` is called. Prefer capturing by value or via `this` with a `disconnect()` call in the destructor.

### 4.3 `unbind()`

Removes a single signal type binding by alias. Works for all callable binding types. Leaves the endpoint record and all other bindings intact. The `alias + type_index` pair is always removed together, preventing any possibility of desync between the internal maps.

```cpp
dispatcher.unbind<SensorReadingSignal>("temp-sensor");
```

**Validation** — all failures logged as warnings, unbind rejected:
- `alias + signal type` not found

### 4.4 `disconnect()`

Removes all bindings for the given `stringID` from all three maps atomically under a write lock. After `disconnect()`, the alias and type entries are fully released and available for re-registration.

```cpp
dispatcher.disconnect("com.company.sensors.unit1");
```

**Caller responsibility** — the endpoint must ensure no signals are in flight before calling `disconnect()`. The recommended pattern is to call it in the endpoint's destructor:

```cpp
MySensor::~MySensor() {
    dispatcher_.disconnect("com.company.sensors.unit1");
}
```

### 4.5 `sendTo()`

Point-to-point delivery. Routes a signal to the single endpoint registered under the given alias for that signal type. Logs a warning if no handler is found.

```cpp
dispatcher.send("temp-sensor", SensorReadingSignal{ 98.6f });
```

### 4.6 `broadcast()`

Point-to-many delivery. Delivers a signal to all endpoints bound to that signal type, excluding the sender. Returns silently if no subscribers are registered — this is a normal state, not an error.

```cpp
dispatcher.broadcast("com.company.sensors.unit1",
                     SensorReadingSignal{ 98.6f });
```

### 4.7 `multicast()`

Point-to-subset delivery. Delivers a signal to a specific list of aliases, excluding the sender. Continues on missing aliases and logs a warning per missing entry.

```cpp
dispatcher.multicast("com.company.sensors.unit1",
                     { "dashboard-primary", "dashboard-secondary" },
                     AlertSignal{ "High temperature detected" });
```

### 4.8 `debug_info()`

Returns a formatted string describing all current registrations. Accepts a `GroupBy` parameter to control output organisation. Acquires a shared read lock for the full duration — safe to call at any time.

```cpp
// Grouped by endpoint stringID
std::cout << dispatcher.debug_info(SignalDispatcher::GroupBy::Endpoint);

// Grouped by alias
std::cout << dispatcher.debug_info(SignalDispatcher::GroupBy::Alias);
```

---

## 5. Error Handling

The `SignalDispatcher` applies a uniform error handling policy across all operations: swallow and log. No operation throws an exception or crashes the process due to a bad registration or a misbehaving callback.

| Condition | Behaviour |
|---|---|
| Duplicate `stringID` + signal type | `bind()` rejected, warning logged |
| Duplicate `alias` + signal type | `bind()` rejected, warning logged |
| `unbind()` on unknown `stringID` | Rejected, warning logged |
| `unbind()` on unbound signal type | Rejected, warning logged |
| `disconnect()` on unknown `stringID` | Warning logged, returns cleanly |
| `sendTo()` to missing alias or type | Warning logged, returns cleanly |
| `multicast()` with missing alias | Warning logged, continues to remaining aliases |
| Callback throws `std::exception` | Exception swallowed, `what()` logged, dispatch continues |
| Callback throws unknown type | Exception swallowed, generic warning logged, dispatch continues |

---

## 6. Performance Characteristics

| Operation | Complexity | Notes |
|---|---|---|
| `sendTo()` | O(1) average | One hash map lookup. Dominant cost is alias string hashing. |
| `broadcast()` | O(n) subscribers | One `type_index` lookup, then linear fan-out. Inherent to broadcast. |
| `multicast()` | O(m) aliases | One lookup per alias in the target list. |
| `bind()` | O(k) bindings | Setup time only. Not on the hot path. |
| `disconnect()` | O(k) bindings | Teardown only. Not on the hot path. |

Template instantiation does not cause meaningful binary bloat. The only type-specific code generated per instantiation is the thunk lambda — approximately 5 to 10 machine instructions.

---

## 7. Key Design Decisions

**No `dynamic_cast` on the hot path**
The type check happens once via `std::type_index` map lookup. The `static_cast` is guaranteed safe at that point and costs nothing at runtime.

**Inline template implementations**
`bind()` and `unbind()` are defined inline inside the class body. Out-of-class template definitions with type alias parameters caused redeclaration errors. Inline definitions eliminate the ambiguity entirely.

**Caller responsibility for disconnect**
Holding a lock during callback invocation would reintroduce contention on the hot path and risk deadlock if a callback itself calls `sendTo()`. The caller contract — disconnect before destruction — is simple, well-documented, and enforced naturally by destructor ordering.
