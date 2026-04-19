#include "SignalDispatcher.h"

#include <iostream>

namespace sd {

void SignalDispatcher::invoke(const ThunkEntry& entry, const Signal& signal) const {
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

    std::unique_lock lock(mutex);

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

void SignalDispatcher::sendTo(const std::string& alias, const Signal& signal) {

    const AliasKey key { alias, typeid(signal) };

    // Lookup under read lock

    ThunkEntry entry;
    {
        std::shared_lock lock(mutex);
        auto it = aliasMap.find(key);
        if (it == aliasMap.end()) {
            std::cerr << "SignalDispatcher send() -- no handler function for "
                      << "alias '" << alias << "' + signal type '"
                      << key.signalType.name() << "'\n";
            return;
        }

        entry = it->second;
    }

    invoke(entry, signal);
}

void SignalDispatcher::broadcast(const std::string& senderStringID, const Signal& signal) {

    const std::type_index signalType = typeid(signal);

    // Collect thunks under read lock

    std::vector<ThunkEntry> entries;
    {
        std::shared_lock lock(mutex);
        auto it = typeMap.find(signalType);
        if( it == typeMap.end()) {
            return;
        }
        entries = it->second;
    }

    for (const auto& entry : entries) {
        if (entry.stringID == senderStringID) {
            continue;
        }

        invoke(entry, signal);
    }
}

void SignalDispatcher::multicast(
    const std::string& senderStringID,
    const std::vector<std::string>& aliases,
    const Signal& signal) {

    const std::type_index signalType = typeid(signal);

    // Collect thunks under read lock

    std::vector<ThunkEntry> entries;
    entries.reserve(aliases.size());
    {
        std::shared_lock lock(mutex);
        for(const auto& alias : aliases) {
            const AliasKey key { alias, signalType };
            auto it = aliasMap.find(key);
            if( it == aliasMap.end()) {
                std::cerr << "SignalDispatcher multicast() -- no handler "
                          << "found for alias '" << alias << "' + signal type '"
                          << signalType.name() << "' -- skipping\n";
                continue;
            }
            entries.push_back(it->second);
        }
    }

    for(const auto& entry : entries) {
        if (entry.stringID == senderStringID) {
            continue;
        }

        invoke(entry, signal);
    }
}

} // NAMESPACE SD