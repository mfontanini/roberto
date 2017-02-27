#include "authentication_manager.h"

using std::string;
using std::make_pair;

namespace roberto {

void AuthenticationManager::add_credentials(string username, string password) {
    credentials_.insert(make_pair(username, password));
}

bool AuthenticationManager::validate_credentials(string username, string password) const {
    return credentials_.count(make_pair(username, password)) > 0;   
}

size_t AuthenticationManager::get_credentials_count() const {
    return credentials_.size();
}

} // roberto
