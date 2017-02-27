#pragma once

#include <set>
#include <string>
#include <utility>

namespace roberto {

class AuthenticationManager {
public:
    void add_credentials(std::string username, std::string password);
    bool validate_credentials(std::string username, std::string password) const;
    size_t get_credentials_count() const;
private:
    using Credentials = std::pair<std::string, std::string>;

    std::set<Credentials> credentials_;
};

} // roberto
