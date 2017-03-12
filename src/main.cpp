#include <functional>
#include <csignal>
#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <thread>
#include <boost/asio/io_service.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <log4cxx/logger.h>
#include <log4cxx/patternlayout.h>
#include <log4cxx/consoleappender.h>
#include "server.h"
#include "authentication_manager.h"

using std::function;
using std::signal;
using std::string;
using std::ifstream;
using std::cout;
using std::cerr;
using std::endl;
using std::exception;
using std::shared_ptr;
using std::make_shared;
using std::vector;
using std::thread;
using std::runtime_error;

using boost::asio::io_service;
using boost::asio::ip::address;
using boost::asio::ip::tcp;

using boost::algorithm::split;
using boost::is_any_of;

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

shared_ptr<AuthenticationManager> make_auth_manager(const string& raw_credentials) {
    if (raw_credentials.empty()) {
        return {};
    }
    auto output = make_shared<AuthenticationManager>();
    vector<string> credentials_pairs;
    split(credentials_pairs, raw_credentials, is_any_of(","));
    for (const string& raw_credential_pair : credentials_pairs) {
        vector<string> credentials;
        split(credentials, raw_credential_pair, is_any_of(":"));
        if (credentials.size() != 2) {
            throw runtime_error("Credentials need format username:password");
        }
        output->add_credentials(credentials[0], credentials[1]);
    }
    return output;
}

int main(int argc, char* argv[]) {
    string config_file;
    string address;
    string log_level;
    string credentials;
    uint16_t port;
    size_t num_threads;

    po::options_description options("Options");
    options.add_options()
        ("help,h",      "produce this help message")
        ("config-file", po::value<string>(&config_file)->required(),
                        "the path to the config file to use");

    po::options_description config_file_options("Options");
    config_file_options.add_options()
        ("address",     po::value<string>(&address)->default_value("0.0.0.0"),
                        "the address to bind to")
        ("port",        po::value<uint16_t>(&port)->required(),
                        "the port to bind to")
        ("num-threads", po::value<size_t>(&num_threads)->default_value(2),
                        "the amount of threads to use")
        ("log-level",   po::value<string>(&log_level)->default_value("INFO"),
                        "the log level to use (TRACE, DEBUG, INFO, WARN, ERROR)")
        ("credentials", po::value<string>(&credentials),
                        "credentials to be used in the format "
                        "username1:password1[,username2:password2[,...]]")
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
    auto logger = Logger::getLogger("r.main");

    shared_ptr<AuthenticationManager> auth_manager;
    try {
        auth_manager = make_auth_manager(credentials);
    }
    catch (const exception& error) {
        LOG4CXX_ERROR(logger, "Error parsing credentials: " << error.what());
        return 1;
    }
    if (auth_manager) {
        LOG4CXX_INFO(logger, "Using " << auth_manager->get_credentials_count() << " credentials");
    }

    try {
        tcp::endpoint endpoint(address::from_string(address), port);

        io_service service;
        Server server(service, endpoint, move(auth_manager));
        server.start();

        signal_handler_functor = [&] {
            service.stop();
        };
        signal(SIGINT, &signal_handler);

        vector<thread> threads;
        for (size_t i = 0; i < num_threads; ++i) {
            threads.emplace_back([&] { service.run(); });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
    catch (const exception& error) {
        LOG4CXX_ERROR(logger, "Error running server: " << error.what());
        return 1;
    }
}
