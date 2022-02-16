#include <felspar/exceptions.hpp>
#include <felspar/io/warden.hpp>


felspar::posix::fd felspar::io::warden::create_socket(
        int domain, int type, int protocol, felspar::source_location loc) {
    posix::fd s{::socket(domain, type, protocol)};
    if (not s) {
        throw felspar::stdexcept::system_error{
                errno, std::system_category(), "Creating server socket",
                std::move(loc)};
    } else {
        return s;
    }
}
