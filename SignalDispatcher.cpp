#include "SignalDispatcher.h"

#include <iostream>
#include <unordered_set>
#include <sstream>
#include <iomanip>

namespace sd {

void SignalDispatcher::dispatch(const ThunkEntry& entry, const Signal& signal) const {
    try {
        entry.thunk(signal);
    }
    catch(const std::exception& e) {
        std::cerr << "SignalDispatcher callback threw for endpoint '"
                  << entry.stringID << "' -- " << e.what() << "\n";
    }
    catch(...) {
        std::cerr << "SignalDispatcher callback threw unknown exception "
                  << "for enpoint '" << entry.stringID << "'\n";
    }
}

void SignalDispatcher::disconnect(const std::string& stringID) {

    auto epIT = endpointMap.find(stringID);
    if(epIT == endpointMap.end()) {
        std::cerr << "SignalDispatcher disconnect() called for unknown "
                  << "StringID: '" << stringID << "'\n";
        return;
    }

    std::unique_lock writeLock(mutex);

    const EndpointRecord& record = epIT->second;

    // Cleanup alias map
    for(const auto& binding : record.bindings) {
        aliasMap.erase(binding.aliasKey);

        auto typeIT = typeMap.find(binding.signalType);
        if(typeIT == typeMap.end()) {
            continue;
        }

        std::vector<ThunkEntry>& thunkEntries = typeIT->second;
        thunkEntries.erase(
            std::remove_if(thunkEntries.begin(), thunkEntries.end(),
                [&epIT](const ThunkEntry& e) {
                    return e.stringID == epIT->first;
                }),
            thunkEntries.end()
        );

        if (thunkEntries.empty()) {
            typeMap.erase(typeIT);
        }
    }

    // Cleanup endpoint map
    endpointMap.erase(epIT);
}

void SignalDispatcher::sendTo(const std::string& alias, const Signal& signal) const {

    const AliasKey key { alias, typeid(signal) };

    // Lookup under read lock

    ThunkEntry entry;
    {
        std::shared_lock readLock(mutex);
        auto it = aliasMap.find(key);
        if (it == aliasMap.end()) {
            std::cerr << "SignalDispatcher sendTo() -- no handler function for "
                      << "alias '" << alias << "' + signal type '"
                      << key.signalType.name() << "'\n";
            return;
        }

        entry = it->second;
    }

    dispatch(entry, signal);
}

void SignalDispatcher::broadcast(const std::string& senderStringID, const Signal& signal) const {

    const std::type_index signalType = typeid(signal);

    // Collect thunks under read lock

    std::vector<ThunkEntry> thunks;
    {
        std::shared_lock readLock(mutex);
        auto it = typeMap.find(signalType);
        if( it == typeMap.end()) {
            return;
        }
        thunks = it->second;
    }

    for (const auto& thunk : thunks) {
        if (thunk.stringID == senderStringID) {
            continue;
        }

        dispatch(thunk, signal);
    }
}

void SignalDispatcher::multicast(
    const std::string& senderStringID,
    const std::vector<std::string>& aliases,
    const Signal& signal) const {

    const std::type_index signalType = typeid(signal);

    // Collect thunks under read lock

    std::vector<ThunkEntry> thunkEntries;
    thunkEntries.reserve(aliases.size());
    {
        std::shared_lock readLock(mutex);
        for(const auto& alias : aliases) {
            const AliasKey key { alias, signalType };
            auto it = aliasMap.find(key);
            if( it == aliasMap.end()) {
                std::cerr << "SignalDispatcher multicast() -- no handler "
                          << "found for alias '" << alias << "' + signal type '"
                          << signalType.name() << "' -- skipping\n";
                continue;
            }
            thunkEntries.push_back(it->second);
        }
    }

    for(const auto& entry : thunkEntries) {
        if (entry.stringID == senderStringID) {
            continue;
        }

        dispatch(entry, signal);
    }
}

std::string SignalDispatcher::debugInfo(GroupBy groupBy) const {
    std::shared_lock readLock(mutex);

    const std::size_t endpointCount = endpointMap.size();
    const std::size_t aliasCount = aliasMap.size();

    std::unordered_set<std::type_index> uniqueTypes;
    for(auto& [type, _] : typeMap) {
        uniqueTypes.insert(type);
    }

    std::ostringstream out;

    std::string groupLabel = (groupBy == GroupBy::Endpoint) ? "Endpoint" : "Alias";
    out << "\n=== SignalDispatcher Debug Info (by " << groupLabel << ") ===\n";
    out << "Endpoints: " << endpointCount
        << "   Alias+Type bindings: " << aliasCount
        << "   Unique signal types: " << uniqueTypes.size() << "\n\n";


    // Grouping: Endpoint
    if(groupBy == GroupBy::Endpoint) {
        for(auto& [stringID, record] : endpointMap) {
            out << "[" << stringID << "]\n";
            for(auto& b : record.bindings) {
                out << " alias: " << std::left << std::setw(24) << b.aliasKey.alias
                    << " | type: " << b.signalType.name() << "\n";
            }
        }

        return out.str();
    }

    // Grouping: Alias
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::type_index>>> byAlias;

    for(const auto& [stringID, record] : endpointMap) {
        for(auto& b : record.bindings) {
            byAlias[b.aliasKey.alias].emplace_back(stringID, b.signalType);
        }
    }

    for(auto& [alias, entries] : byAlias) {
        out << "[" << alias << "]\n";
        for(auto& [stringID, type] : entries) {
            out << " signal type: " << std::left << std::setw(40) << type.name()
                << " | endpoint: " << stringID << "\n";
        }
        out << "\n";
    }

    return out.str();
}

} // NAMESPACE SD