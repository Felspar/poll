#include <felspar/poll.hpp>
#include <felspar/test.hpp>

#include <iostream>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>


namespace {


    auto const suite = felspar::testsuite("basics");


    felspar::coro::task<void>
            echo_connection(felspar::poll::warden &ward, int fd) {
        felspar::posix::set_non_blocking(fd);
        std::array<std::byte, 256> buffer;
        while (auto bytes = co_await ward.read_some(fd, buffer)) {
            std::cout << "Server FD " << fd << " read " << bytes << " bytes"
                      << std::endl;
            std::span writing{buffer};
            auto written = co_await felspar::poll::write_all(
                    ward, fd, writing.first(bytes));
            std::cout << "Server FD " << fd << " wrote " << written << " bytes"
                      << std::endl;
        }
        ::close(fd);
    }

    felspar::coro::task<void>
            echo_server(felspar::poll::warden &ward, std::uint16_t port) {
        auto fd = ward.create_socket(AF_INET, SOCK_STREAM, 0);

        int optval = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval))
            == -1) {
            throw felspar::stdexcept::system_error{
                    errno, std::generic_category(),
                    "setsockopt SO_REUSEPORT failed"};
        }

        sockaddr_in in;
        in.sin_family = AF_INET;
        in.sin_port = htons(port);
        in.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(fd, reinterpret_cast<sockaddr const *>(&in), sizeof(in))
            != 0) {
            throw felspar::stdexcept::system_error{
                    errno, std::generic_category(), "Binding server socket"};
        }

        int constexpr backlog = 64;
        if (::listen(fd, backlog) == -1) {
            throw felspar::stdexcept::system_error{
                    errno, std::generic_category(), "Calling listen"};
        }

        felspar::poll::coro_owner co{ward};
        std::cout << "Accept ready to start accepting" << std::endl;
        for (auto acceptor = felspar::poll::accept(ward, fd);
             auto cnx = co_await acceptor.next();) {
            std::cout << "Server accepted FD " << *cnx << std::endl;
            co.post(echo_connection, ward, *cnx);
            co.gc();
        }
        std::cout << "Accept done" << std::endl;

        ::close(fd);
    }

    felspar::coro::task<void>
            echo_client(felspar::poll::warden &ward, std::uint16_t port) {
        felspar::test::injected check;

        auto fd = ward.create_socket(AF_INET, SOCK_STREAM, 0);
        felspar::posix::set_non_blocking(fd);

        sockaddr_in in;
        in.sin_family = AF_INET;
        in.sin_port = htons(port);
        in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        co_await ward.connect(
                fd, reinterpret_cast<sockaddr const *>(&in), sizeof(in));

        std::array<std::uint8_t, 6> out{1, 2, 3, 4, 5, 6}, buffer{};
        co_await felspar::poll::write_all(ward, fd, out);

        auto bytes = co_await felspar::poll::read_exactly(ward, fd, buffer);
        check(bytes) == 6u;
        check(buffer[0]) == out[0];
        check(buffer[1]) == out[1];
        check(buffer[2]) == out[2];
        check(buffer[3]) == out[3];
        check(buffer[4]) == out[4];
        check(buffer[5]) == out[5];
    }


    auto const tp = suite.test("echo/poll", []() {
        felspar::poll::poll_warden ward;
        felspar::poll::coro_owner co{ward};
        co.post(echo_server, ward, 5543);
        ward.run(echo_client, 5543);
    });
    auto const tu = suite.test("echo/io_uring", []() {
        felspar::poll::io_uring_warden ward;
        felspar::poll::coro_owner co{ward};
        co.post(echo_server, ward, 5547);
        ward.run(echo_client, 5547);
    });


}
