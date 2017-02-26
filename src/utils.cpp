#include "utils.h"
#include <boost/asio/error.hpp>

using boost::system::error_code;

namespace roberto {
namespace utils {

bool is_operation_aborted(const error_code& error) {
    return error == boost::asio::error::operation_aborted;
}

} // utils
} // roberto
