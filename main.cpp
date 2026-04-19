#include "SignalDispatcher.h"

#include <iostream>
#include <string>

namespace sd {
class CommandHandler {
public:
    CommandHandler(SignalDispatcher& dispatcher, std::string id, std::string theAlias) :
        dispatcher(dispatcher),
        stringID(std::move(id)),
        alias(std::move(theAlias)) {

        dispatcher.bind<CommandSignal>(
            stringID,
            alias,
            this,
            &CommandHandler::onCommand);
    }

    ~CommandHandler() {
        dispatcher.disconnect(stringID);
    }

private:
    // Handlers
    void onCommand(const CommandSignal& signal) {
        std::cout << "CommandHandler::onCommand" << std::endl;
    }

private:
    SignalDispatcher& dispatcher;
    std::string stringID;
    std::string alias;
};

class AlertHandler {
public:
    AlertHandler(SignalDispatcher& dispatcher, std::string id, std::string theAlias) :
        dispatcher(dispatcher),
        stringID(std::move(id)),
        alias(std::move(theAlias)) {

        dispatcher.bind<AlertSignal>(
            stringID,
            alias,
            this,
            &AlertHandler::onAlert
        );

        dispatcher.bind<CommandSignal>(
            stringID,
            alias,
            this,
            &AlertHandler::onCommand
        );
    }

    ~AlertHandler() {
        dispatcher.disconnect(stringID);
    }

private:
    void onAlert(const AlertSignal& signal) {
        std::cout << "AlertHandler::onAlert" << std::endl;
    }

    void onCommand(const CommandSignal& signal) {
        std::cout << "AlertHandler::onCommand" << std::endl;
    }

private:
    SignalDispatcher& dispatcher;
    std::string stringID;
    std::string alias;
};

} // NAMESPACE SD

int main() {
    sd::SignalDispatcher dispatcher;

    sd::CommandHandler commandHandler(
        dispatcher,
        "com.company.test.commandHandler",
        "CommandHandler"
    );

    sd::AlertHandler alertHandler(
        dispatcher,
        "com.company.test.alerthandler",
        "AlertHandler"
    );

    // dispatcher.sendTo("CommandHandler", sd::CommandSignal{ "HELLO" });
    // dispatcher.sendTo("AlertHandler", sd::AlertSignal{ "ALERT! ALERT!" });

    dispatcher.broadcast("ME", sd::CommandSignal{ "BROADCAST" });

    return 0;
}
