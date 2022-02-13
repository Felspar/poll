#include <felspar/io.hpp>
#include <felspar/coro/start.hpp>
#include <felspar/test.hpp>


using namespace std::literals;


namespace {


    auto const suite = felspar::testsuite("timers");


    felspar::coro::task<bool> short_sleep(felspar::io::warden &ward) {
        auto const start = std::chrono::steady_clock::now();
        co_await ward.sleep(20ms);
        auto const slept = std::chrono::steady_clock::now() - start;
        co_return slept >= 20ms and slept <= 30ms;
    }
    auto const ssp = suite.test("timers/poll", [](auto check) {
        felspar::io::poll_warden ward;
        check(ward.run(short_sleep)) == true;
    });
    auto const ssu = suite.test("timers/uring", [](auto check) {
        felspar::io::uring_warden ward{5};
        check(ward.run(short_sleep)) == true;
    });


    felspar::coro::task<void>
            accept_writer(felspar::io::warden &ward, std::uint16_t port) {
        auto fd = ward.create_socket(AF_INET, SOCK_STREAM, 0);
        set_reuse_port(fd);
        bind_to_any_address(fd, port);

        int constexpr backlog = 64;
        if (::listen(fd.native_handle(), backlog) == -1) {
            throw felspar::stdexcept::system_error{
                    errno, std::generic_category(), "Calling listen"};
        }

        auto acceptor = felspar::io::accept(ward, fd);
        auto cnx = co_await acceptor.next();
        co_await ward.sleep(30ms);
    }
    felspar::coro::task<void>
            write_forever(felspar::io::warden &ward, std::uint16_t port) {
        felspar::test::injected check;

        /// TODO Lots of small writes will fail here (i.e. 1<<10)
        std::array<std::byte, 1 << 20> buffer;
        auto fd = ward.create_socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in in;
        in.sin_family = AF_INET;
        in.sin_port = htons(port);
        in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        co_await ward.connect(
                fd, reinterpret_cast<sockaddr const *>(&in), sizeof(in));
        try {
            while (true) { co_await ward.write_some(fd, buffer, 10ms); }
        } catch (felspar::io::timeout const &) {
            check(true) == true;
        } catch (...) { check(false) == true; }
    }
    auto const wp = suite.test("write/poll", []() {
        felspar::io::poll_warden ward;
        felspar::coro::starter<felspar::coro::task<void>> co;
        co.post(accept_writer, ward, 5534);
        ward.run(write_forever, 5534);
    });
    auto const wu = suite.test("write/io_uring", []() {
        felspar::io::uring_warden ward;
        felspar::coro::starter<felspar::coro::task<void>> co;
        co.post(accept_writer, ward, 5536);
        ward.run(write_forever, 5536);
    });


}
