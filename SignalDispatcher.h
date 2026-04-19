#pragma once

#include "Signal.h"

#include <functional>
#include <shared_mutex>
#include <typeindex>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <iostream>

namespace sd {
// Internal Types

// A thunk wraps a static_cast for the signal type and a member function pointer call.
// Generated at bind() time when the concrete type is known.
using Thunk = std::function<void(const Signal&)>;

// Stored in alias and type maps
struct ThunkEntry {
    std::string stringID;
    Thunk thunk;
};

// Alias map key - alias name + signal type combined
struct AliasKey {
    std::string alias;
    std::type_index signalType;

    bool operator==(const AliasKey& other) const noexcept {
        return alias == other.alias && signalType == other.signalType;
    }
};

// Defines how to hash alias key;
struct AliasKeyHash {
    AliasKeyHash() = default;

    std::size_t operator()(const AliasKey& k) const {
        const std::size_t h1 = std::hash<std::string>{}(k.alias);
        const std::size_t h2 = std::hash<std::type_index>{}(k.signalType);
        return h1 ^ (h2 << 1);
    }
};

// Per endpoint registration record -- stored in endpoint map
struct EndpointRecord {
    struct Binding {
        AliasKey aliasKey;
        std::type_index signalType;

        Binding(AliasKey k, std::type_index t) :
            aliasKey(std::move(k)), signalType(t) {}
    };

    std::vector<Binding> bindings;
};

// Free type alias
template<typename TSignal, typename TEndpoint>
using MemberHandler = void(TEndpoint::*)(const TSignal&);


// SignalDiapatcher
//
// Central mediator that routes signals between endpoints.
//
// Three sending modes:
//     send()      - Point to Point via alias + type_index
//     broadcast() - point to many, all endpoints bound to the signal type
//     mutlicast() - point to subset, specific list of aliases
//
// Thread Safety:
//     std::shared_mutex protects all three maps
//     Read lock held only for map lookup -- released before thunk invocation
//     Write lock acquired for bind(), unbind(), and disconnect()
//

class SignalDispatcher {
public:
    SignalDispatcher() = default;
    ~SignalDispatcher() = default;

    // Non copyable, non-movable -- owns map state
    SignalDispatcher(const SignalDispatcher&) = delete;
    SignalDispatcher& operator=(const SignalDispatcher&) = delete;

    // bind() -- Bind a member function handler for a specific signal type
    //
    // First call for a stringID creates the endpoint record.
    // Subsequent calls add new signal type bindings.
    //
    // Validation (All failures logged as warnings, registration rejected):
    //  1. stringID + signal type already bound
    //  2. Alias + signal type combination already taken
    template<typename TSignal, typename TEndpoint>
    void bind(
        const std::string& stringID,
        const std::string& alias,
        TEndpoint* endpoint,
        MemberHandler<TSignal, TEndpoint> handler) {

        static_assert(
            std::is_base_of_v<Signal, TSignal>,
            "TSignal must derive from sd::Signal"
        );

        const std::type_index signalType = typeid(TSignal);
        const AliasKey key = { alias, signalType };

        // Validation

        // Check 1: stringID + signal type already bound
        if(auto ep = endpointMap.find(stringID); ep != endpointMap.end()) {
            for(const auto& binding : ep->second.bindings) {
                if (binding.signalType == signalType) {
                    std::cerr << "SignalDispatcher bind() rejected - "
                            << "stringID '" << stringID << "' already bound to "
                            << "signal type'" << signalType.name() <<"'\n";
                    return;
                }
            }
        }

        // Check 2. alias + signal type combination already taken
        if (aliasMap.count(key)) {
            std::cerr << "SignalDispatcher bind() rejected - "
                    << "alias'" << alias <<"' + signal type '" << signalType.name()
                    << "' already taken by another endpoint\n";
            return;
        }

        // Generate Thunk -- Captures endpoint pointer and member function pointer at bind() time
        // static_cast is safe here due to type identity guranteed by type_index match

        ThunkEntry entry {
            stringID,
            [endpoint, handler](const Signal& sig) {
                (endpoint->*handler)(static_cast<const TSignal&>(sig));
            }
        };

        // Write to all three maps
        std::unique_lock lock(mutex);

        aliasMap.emplace(key, entry);
        typeMap[signalType].push_back(entry);
        endpointMap[stringID].bindings.emplace_back(key, signalType);
    }
    
    // unbind() -- Removes a single signal type binding for a specific endpoint.
    // 
    // Leaves the endpoint record and all other bindings intact.
    //
    // Validation (all failures logged as warnings, unbind rejected):
    //  1. stringID not found
    //  2. signal type not bound to this endpoint
    template<typename TSignal>
    void unbind(const std::string& stringID, const std::string& alias) {

        static_assert(std::is_base_of_v<Signal, TSignal>,
                 "TSignal must derive from sd::Signal");

        const std::type_index signalType = typeid(TSignal);
        const AliasKey aliasKey = { alias, signalType };

        // Validation

        // Check 1: stringID not found
        auto epIT = endpointMap.find(stringID);
        if( epIT == endpointMap.end()) {
            std::cerr << "SignalDispatcher unbind() rejected -- "
                    << "stringID '" << stringID << "' not found\n";
            return;
        }

        // Check 2: signal type not bound to this endpoint
        auto& record = epIT->second;
        auto bindIT =
            std::find_if(record.bindings.begin(),
                         record.bindings.end(),
                        [&aliasKey](const EndpointRecord::Binding& b) {
                            return b.aliasKey == aliasKey;
                        });
        
        if(bindIT == record.bindings.end()) {
            std::cerr << "SignalDispatcher unbind() rejected - "
                    << "signal type '" << signalType.name() << "' not bound to '"
                    << stringID << "' under alias '" << alias << "'\n";
            return;
        }

        std::unique_lock lock(mutex);

        // Remove from alias map
        aliasMap.erase(aliasKey);

        // Remove from type map -- erase entry for this stringID
        auto typeIT = typeMap.find(signalType);
        if (typeIT != typeMap.end()) {
            auto& entries = typeIT->second;
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                    [&stringID](const ThunkEntry& e) {
                        return e.stringID == stringID;
                    }),
                entries.end()
            );

            // Delete if no entries remain
            if(entries.empty()) {
                typeMap.erase(typeIT);
            }
        }

        // Remove from endpoint Record
        record.bindings.erase(bindIT);
    }

    // disconnect() -- Removes all bindings for the given stringID.
    //
    // Cleans up all three maps atomically under write lock
    //
    // Caller Responsibility: Ensure no signals are in flight to this endpoint before
    // calling disconnect(). Recommended pattern is to call disconnect() in
    // the endpoints destructor
    //
    // Logs a warning if stringID is not found
    void disconnect(const std::string& stringID);

    // Point to point -- Deliver to one specific alias.
    // Logs a warning if alias + signal type not found.
    void sendTo(const std::string& alias, const Signal& signal);

    // Point to many -- Deliver to a specific list of aliases.
    // Excludes the sender (matched by sender stringID).
    void broadcast(const std::string& senderStringID, const Signal& signal);

    // Point to subset -- Deliver to a specific list of aliases.
    // Exlucde the sender (matched by sender stringID).
    // Continues on missing aliases, logs a warning per missing alias.
    void multicast(
        const std::string& senderStringID,
        const std::vector<std::string>& aliases,
        const Signal& signal
    );

private:
    // Internal Dispatch
    void invoke(const ThunkEntry& entry, const Signal& signal) const;

    // Internal Maps

    // Used by send() and multicast()
    // alias + type_index --> { stringID, thunk }
    std::unordered_map<AliasKey, ThunkEntry, AliasKeyHash> aliasMap;

    // Used by broadcast()
    // type_index --> [ { string_id, thunk } ]
    std::unordered_map<std::type_index, std::vector<ThunkEntry>> typeMap;

    // Mangement -- Used by bind(), unbind(), and disconnect()
    // stringID --> EndpointRecord
    std::unordered_map<std::string, EndpointRecord> endpointMap;

    mutable std::shared_mutex mutex;
};

} // NAMESPACE SD
