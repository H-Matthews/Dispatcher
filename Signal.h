#pragma once

#include <string>
#include <iostream>

namespace sd {
class Signal {

public:
    Signal() = default;

    Signal(const Signal&) = default;
    Signal& operator=(const Signal&) = default;

    virtual ~Signal() = default;

    void setSenderId(std::string id) { senderID = std::move(id); }

private:
    std::string senderID;
};

class AlertSignal : public Signal {
public:
    explicit AlertSignal(std::string text) :
        alertText(std::move(text)) {}

    const std::string& getAlertText() const { return alertText; }
private:
    std::string alertText;
};

class CommandSignal : public Signal {
public:
    explicit CommandSignal(std::string theCommand) :
        command(std::move(theCommand)) {}

private:
    std::string command;
};

} // NAMEPACE SD
