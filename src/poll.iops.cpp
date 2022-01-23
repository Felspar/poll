#include "poll.hpp"

#include <felspar/exceptions.hpp>
#include <felspar/poll/connect.hpp>

#include <sys/socket.h>
#include <unistd.h>


felspar::poll::iop<std::size_t> felspar::poll::poll_warden::read_some(
        int fd, std::span<std::byte> buf, felspar::source_location loc) {
    struct c : public completion<std::size_t> {
        c(poll_warden *s,
          int f,
          std::span<std::byte> b,
          felspar::source_location loc)
        : completion<std::size_t>{s}, fd{f}, buf{b}, loc{std::move(loc)} {}
        int fd;
        std::span<std::byte> buf;
        felspar::source_location loc;
        void try_or_resume() override {
            if (auto bytes = ::read(fd, buf.data(), buf.size()); bytes >= 0) {
                result = bytes;
                handle.resume();
            } else if (errno == EAGAIN or errno == EWOULDBLOCK) {
                self->requests[fd].reads.push_back(this);
            } else {
                /// TODO Keep the exception to throw later on when the IOP's
                /// await_resume is executed
                throw felspar::stdexcept::system_error{
                        errno, std::generic_category(), "read", std::move(loc)};
            }
        }
    };
    return {new c{this, fd, buf, std::move(loc)}};
}
felspar::poll::iop<std::size_t> felspar::poll::poll_warden::write_some(
        int fd, std::span<std::byte const> buf, felspar::source_location loc) {
    struct c : public completion<std::size_t> {
        c(poll_warden *s,
          int f,
          std::span<std::byte const> b,
          felspar::source_location loc)
        : completion<std::size_t>{s}, fd{f}, buf{b}, loc{std::move(loc)} {}
        int fd;
        std::span<std::byte const> buf;
        felspar::source_location loc;
        void try_or_resume() override {
            if (auto bytes = ::write(fd, buf.data(), buf.size()); bytes >= 0) {
                result = bytes;
                handle.resume();
            } else if (errno == EAGAIN or errno == EWOULDBLOCK) {
                self->requests[fd].writes.push_back(this);
            } else {
                /// TODO Keep the exception to throw later on when the IOP's
                /// await_resume is executed
                throw felspar::stdexcept::system_error{
                        errno, std::generic_category(), "write",
                        std::move(loc)};
            }
        }
    };
    return {new c{this, fd, buf, std::move(loc)}};
}


felspar::poll::iop<int> felspar::poll::poll_warden::accept(
        int fd, felspar::source_location loc) {
    struct c : public completion<int> {
        c(poll_warden *s, int f, felspar::source_location loc)
        : completion<int>{s}, fd{f}, loc{std::move(loc)} {}
        int fd;
        felspar::source_location loc;
        void try_or_resume() override {
            result = ::accept(fd, nullptr, nullptr);
            if (result >= 0) {
                handle.resume();
            } else if (errno == EWOULDBLOCK or errno == EAGAIN) {
                self->requests[fd].reads.push_back(this);
            } else if (errno == EBADF) {
                handle.resume();
            } else {
                /// TODO Keep the exception to throw later on when the IOP's
                /// await_resume is executed
                throw felspar::stdexcept::system_error{
                        errno, std::generic_category(), "accept",
                        std::move(loc)};
            }
        }
    };
    return {new c{this, fd, std::move(loc)}};
}


felspar::poll::iop<void> felspar::poll::poll_warden::connect(
        int fd,
        sockaddr const *addr,
        socklen_t addrlen,
        felspar::source_location loc) {
    struct c : public completion<void> {
        c(poll_warden *s,
          int f,
          sockaddr const *a,
          socklen_t l,
          felspar::source_location loc)
        : completion<void>{s}, fd{f}, addr{a}, addrlen{l}, loc{std::move(loc)} {}
        ~c() = default;
        int fd;
        sockaddr const *addr;
        socklen_t addrlen;
        felspar::source_location loc;
        void await_suspend(felspar::coro::coroutine_handle<> h) override {
            handle = h;
            if (auto err = ::connect(fd, addr, addrlen); err == 0) {
                handle.resume();
            } else if (errno == EINPROGRESS) {
                self->requests[fd].writes.push_back(this);
            } else {
                /// TODO Keep the exception to throw later on when the IOP's
                /// await_resume is executed
                throw felspar::stdexcept::system_error{
                        errno, std::generic_category(), "connect",
                        std::move(loc)};
            }
        }
        void try_or_resume() override {
            int errvalue{};
            ::socklen_t length{};
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &errvalue, &length) == 0) {
                if (errvalue == 0) {
                    handle.resume();
                } else {
                    /// TODO Keep the exception to throw later on when the IOP's
                    /// await_resume is executed
                    throw felspar::stdexcept::system_error{
                            errno, std::generic_category(), "connect",
                            std::move(loc)};
                }
            } else {
                /// TODO Keep the exception to throw later on when the IOP's
                /// await_resume is executed
                throw felspar::stdexcept::system_error{
                        errno, std::generic_category(), "connect/getsockopt",
                        std::move(loc)};
            }
        }
    };
    return {new c{this, fd, addr, addrlen, std::move(loc)}};
}


felspar::poll::iop<void> felspar::poll::poll_warden::read_ready(
        int fd, felspar::source_location loc) {
    struct c : public completion<void> {
        c(poll_warden *s, int f, felspar::source_location loc)
        : completion<void>{s}, fd{f}, loc{std::move(loc)} {}
        ~c() = default;
        int fd;
        felspar::source_location loc;
        void await_suspend(felspar::coro::coroutine_handle<> h) override {
            handle = h;
            self->requests[fd].reads.push_back(this);
        }
        void try_or_resume() override { handle.resume(); }
    };
    return {new c{this, fd, std::move(loc)}};
}
felspar::poll::iop<void> felspar::poll::poll_warden::write_ready(
        int fd, felspar::source_location loc) {
    struct c : public completion<void> {
        c(poll_warden *s, int f, felspar::source_location loc)
        : completion<void>{s}, fd{f}, loc{std::move(loc)} {}
        ~c() = default;
        int fd;
        felspar::source_location loc;
        void await_suspend(
                felspar::coro::coroutine_handle<> h) noexcept override {
            handle = h;
            self->requests[fd].writes.push_back(this);
        }
        void try_or_resume() override { handle.resume(); }
    };
    return {new c{this, fd, std::move(loc)}};
}
