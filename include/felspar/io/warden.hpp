#pragma once

#include <felspar/io/completion.hpp>
#include <felspar/io/posix.hpp>
#include <felspar/test/source.hpp>

#include <chrono>
#include <span>

#include <netinet/in.h>


namespace felspar::io {


    class warden {
        template<typename R>
        friend struct felspar::io::iop;

      protected:
        virtual void run_until(felspar::coro::coroutine_handle<>) = 0;
        template<typename R>
        void cancel(completion<R> *c) {
            /// TODO Put the memory back into the pool for later re-use
            delete c;
        }

      public:
        virtual ~warden() = default;

        template<typename Ret, typename... PArgs, typename... MArgs>
        Ret run(coro::task<Ret> (*f)(warden &, PArgs...), MArgs &&...margs) {
            auto task = f(*this, std::forward<MArgs>(margs)...);
            auto handle = task.release();
            run_until(handle.get());
            return handle.promise().consume_value();
        }

        /**
         * ### Time management
         */
        virtual iop<void>
                sleep(std::chrono::nanoseconds,
                      felspar::source_location =
                              felspar::source_location::current()) = 0;

        /**
         * ### Reading and writing
         */
        /// Read or write bytes from the provided buffer returning the number of
        /// bytes read/written.
        virtual iop<std::size_t> read_some(
                int fd,
                std::span<std::byte>,
                felspar::source_location =
                        felspar::source_location::current()) = 0;
        iop<std::size_t> read_some(
                posix::fd const &s,
                std::span<std::byte> b,
                felspar::source_location l =
                        felspar::source_location::current()) {
            return read_some(s.native_handle(), b, std::move(l));
        }
        virtual iop<std::size_t> write_some(
                int fd,
                std::span<std::byte const>,
                felspar::source_location =
                        felspar::source_location::current()) = 0;
        iop<std::size_t> write_some(
                posix::fd const &s,
                std::span<std::byte const> b,
                felspar::source_location l =
                        felspar::source_location::current()) {
            return write_some(s.native_handle(), b, std::move(l));
        }

        /**
         * ### Socket APIs
         */
        virtual posix::fd create_socket(
                int domain,
                int type,
                int protocol,
                felspar::source_location = felspar::source_location::current());

        virtual iop<int>
                accept(int fd,
                       felspar::source_location =
                               felspar::source_location::current()) = 0;
        iop<int>
                accept(posix::fd const &sock,
                       felspar::source_location loc =
                               felspar::source_location::current()) {
            return accept(sock.native_handle(), std::move(loc));
        }
        virtual iop<void>
                connect(int fd,
                        sockaddr const *,
                        socklen_t,
                        felspar::source_location =
                                felspar::source_location::current()) = 0;
        iop<void>
                connect(posix::fd const &sock,
                        sockaddr const *addr,
                        socklen_t addrlen,
                        felspar::source_location loc =
                                felspar::source_location::current()) {
            return connect(sock.native_handle(), addr, addrlen, std::move(loc));
        }


        /**
         * ### File readiness
         */
        virtual iop<void> read_ready(
                int fd,
                felspar::source_location =
                        felspar::source_location::current()) = 0;
        virtual iop<void> write_ready(
                int fd,
                felspar::source_location =
                        felspar::source_location::current()) = 0;
    };


    inline iop<void>::~iop() { comp->ward()->cancel(comp); }
    template<typename R>
    inline iop<R>::~iop() {
        comp->ward()->cancel(comp);
    }


}
