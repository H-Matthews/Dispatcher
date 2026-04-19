# SignalDispatcher
Central mediator that is capable of routing Signals (messages) between endpoints (models).

Three different types of dispatching:
1. Point to point via alias + type index
2. Point to many, all endpoints bound to the signal type
3. Point to subset, specific list of aliases

Thread Safety:
 - std::shared_mutex ensures synchronization across all three data structures
 - Ensures we only lock during writes


## Public API
 - bind()       (Template)     -- registers an endpoint and associates a signal type handler
 - unbind()     (Template)     -- removes a single signal type binding without touching the endpoint
 - disconnect()                -- full endpoint teardown, cleans all three maps atomically
 - send()                      -- point to point via alias + type
 - broadcast()                 -- point to many, all subscribers of a type
 - multicast()                 -- point to subset of aliases
 - debugInfo()                 -- formatted diagnostic output grouped by endpoint or alias