#pragma once

namespace boost { namespace system { class error_code; } }

namespace roberto {
namespace utils {

bool is_operation_aborted(const boost::system::error_code& error);

} // utils
} // roberto
