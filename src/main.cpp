#include <functional>
#include <csignal>
#include <boost/asio/io_service.hpp>
#include <log4cxx/logger.h>
#include <log4cxx/patternlayout.h>
#include <log4cxx/consoleappender.h>
#include "server.h"

using std::function;
using std::signal;

using boost::asio::io_service;
using boost::asio::ip::address;
using boost::asio::ip::tcp;

using log4cxx::PatternLayout;
using log4cxx::ConsoleAppender;
using log4cxx::Level;
using log4cxx::Logger;

using namespace roberto;

function<void()> signal_handler_functor;

void signal_handler(int) {
    if (signal_handler_functor) {
        signal_handler_functor();
        signal_handler_functor = {};
    }
}

void configure_logging() {
    auto layout = new PatternLayout("%d{yyyy-MM-dd HH:mm:ss.SSS}{GMT} %-5p [%.8t] [%c{2}] - %m%n");
    auto appender = new ConsoleAppender(layout);

    auto logger = Logger::getRootLogger();
    logger->setLevel(Level::getInfo());
    logger->addAppender(appender);
}

int main() {
    configure_logging();

    io_service service;
    tcp::endpoint endpoint(address::from_string("0.0.0.0"), 9999);
    Server server(service, endpoint);
    server.start();

    signal_handler_functor = [&] {
        service.stop();
    };
    signal(SIGINT, &signal_handler);
    service.run();
}
