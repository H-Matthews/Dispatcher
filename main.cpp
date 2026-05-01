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
            [this](const CommandSignal& sig) { onCommand(sig); });
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
            [this](const AlertSignal& sig) { onAlert(sig); });

        dispatcher.bind<CommandSignal>(
            stringID,
            alias,
            [this](const CommandSignal& sig) { onCommand(sig); });
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

    // Lambda binding example
    dispatcher.bind<sd::AlertSignal>(
        "com.company.test.lambdaHandler",
        "LambdaAlertHandler",
        [](const sd::AlertSignal& sig) {
            std::cout << "Lambda received alert: " << sig.getAlertText() << std::endl;
        }
    );

    // dispatcher.sendTo("CommandHandler", sd::CommandSignal{ "HELLO" });
    // dispatcher.sendTo("AlertHandler", sd::AlertSignal{ "ALERT! ALERT!" });

    dispatcher.sendTo("LambdaAlertHandler", sd::AlertSignal{ "sensor overheat" });

    dispatcher.unbind<sd::AlertSignal>("LambdaAlertHandler");

    dispatcher.sendTo("LambdaAlertHandler", sd::AlertSignal{ "should not fire" });

    dispatcher.broadcast("ME", sd::CommandSignal{ "BROADCAST" });

    const std::string endpointStr = dispatcher.debugInfo();
    std::cout << endpointStr << std::endl;

    return 0;
}
