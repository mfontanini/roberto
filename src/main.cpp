#include <functional>
#include <csignal>
#include <string>
#include <fstream>
#include <iostream>
#include <boost/asio/io_service.hpp>
#include <boost/program_options.hpp>
#include <log4cxx/logger.h>
#include <log4cxx/patternlayout.h>
#include <log4cxx/consoleappender.h>
#include "server.h"

using std::function;
using std::signal;
using std::string;
using std::ifstream;
using std::cout;
using std::cerr;
using std::endl;
using std::exception;

using boost::asio::io_service;
using boost::asio::ip::address;
using boost::asio::ip::tcp;

using log4cxx::PatternLayout;
using log4cxx::ConsoleAppender;
using log4cxx::Level;
using log4cxx::Logger;

namespace po = boost::program_options;

using namespace roberto;

function<void()> signal_handler_functor;

void signal_handler(int) {
    if (signal_handler_functor) {
        signal_handler_functor();
        signal_handler_functor = {};
    }
}

void configure_logging(const string& log_level) {
    auto layout = new PatternLayout("%d{yyyy-MM-dd HH:mm:ss.SSS}{GMT} [%c{2}] - %m%n");
    auto appender = new ConsoleAppender(layout);

    auto logger = Logger::getRootLogger();
    logger->setLevel(Level::toLevel(log_level));
    logger->addAppender(appender);
}

int main(int argc, char* argv[]) {
    string config_file;
    string address;
    string log_level;
    uint16_t port;

    po::options_description options("Options");
    options.add_options()
        ("help,h",      "produce this help message")
        ("config-file", po::value<string>(&config_file)->required(),
                        "the path to the config file to use");

    po::options_description config_file_options("Options");
    config_file_options.add_options()
        ("address",   po::value<string>(&address)->default_value("0.0.0.0"),
                      "the address to bind to")
        ("port",      po::value<uint16_t>(&port)->required(),
                      "the port to bind to")
        ("log-level", po::value<string>(&log_level)->default_value("INFO"),
                      "the log level to use (TRACE, DEBUG, INFO, WARN, ERROR)")
        ;

    po::variables_map vm;

    try {
        po::store(po::command_line_parser(argc, argv).options(options).run(), vm);
        if (vm.count("help")) {
            cout << "Usage:" << endl << endl;
            cout << argv[0] << " [options]" << endl << endl;
            cout << options << endl;
            return 1;
        }
        po::notify(vm);
    }
    catch (const po::error& error) {
        cerr << error.what() << endl;
        return 1;
    }

    try {
        ifstream input(config_file);
        if (!input) {
            cerr << "Failed to open config file" << endl;
            return 1;
        }
        po::store(po::parse_config_file(input, config_file_options), vm);
        po::notify(vm);
    }
    catch (const po::error& error) {
        cerr << "Error parsing config file: " << error.what() << endl;
        return 1;
    }

    try {
        configure_logging(log_level);
    }
    catch (const exception& error) {
        cerr << "Failed to configure logging: " << error.what() << endl;
    }

    try {
        tcp::endpoint endpoint(address::from_string(address), port);

        io_service service;
        Server server(service, endpoint);
        server.start();

        signal_handler_functor = [&] {
            service.stop();
        };
        signal(SIGINT, &signal_handler);
        service.run();
    }
    catch (const exception& error) {
        auto logger = Logger::getLogger("r.main");
        LOG4CXX_ERROR(logger, "Error running server: " << error.what());
    }
}
