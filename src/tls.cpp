#include <openssl/err.h>
#include <openssl/ssl.h>

#include <felspar/io/tls.hpp>
#include <felspar/io/write.hpp>


/// ## `felspar::io::tls::impl`


struct felspar::io::tls::impl {
    impl(posix::fd f)
    : ctx{SSL_CTX_new(TLS_method())}, ssl{SSL_new(ctx)}, fd{std::move(f)} {
        /// TODO There should be some error handling here
        SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
        BIO_new_bio_pair(&ib, 0, &nb, 0);
        /// Give the internal BIO to `ssl`
        SSL_set_bio(ssl, ib, ib);
    }
    ~impl() {
        if (nb) { BIO_free(nb); }
        if (ssl) { SSL_free(ssl); }
        if (ctx) { SSL_CTX_free(ctx); }
    }
    SSL_CTX *ctx = nullptr;
    SSL *ssl = nullptr;
    /// `ib` is the internal BIO and `nb` is the network BIO
    BIO *ib = nullptr, *nb = nullptr;
    posix::fd fd;

    /**
     * A data buffer that we'll use whilst sending/receiving data. The TLS
     * record size is 16KB so we make this just a little bit bigger.
     */
    std::array<std::byte, (17 << 10)> buffer;

    std::string ssl_error(int const result) const {
        auto const error = SSL_get_error(ssl, result);
        switch (error) {
        default: return "Unknown error " + std::to_string(error);
        }
    }

    /// Loop for handling read/write requests when trying to carry out an operation
    template<typename Op>
    io::warden::task<int> service_operation(
            io::warden &warden,
            std::optional<std::chrono::nanoseconds> timeout,
            felspar::source_location const &loc,
            Op &&op) {
        while (true) {
            auto const result = op(*this);
            auto const error = SSL_get_error(ssl, result);
            switch (error) {
            case SSL_ERROR_NONE: co_return result;

            case SSL_ERROR_WANT_READ:
                co_await bio_read(warden, timeout, loc);
                if (0 == co_await bio_write(warden, timeout, loc)) {
                    co_return 0;
                }
                break;
            case SSL_ERROR_WANT_WRITE:
                co_await bio_read(warden, timeout, loc);
                break;

            case SSL_ERROR_ZERO_RETURN: co_return 0;

            default:
                throw felspar::stdexcept::runtime_error{
                        "Unknown openssl error " + std::to_string(error)};
            }
        }
    }

    io::warden::task<void> bio_read(
            io::warden &warden,
            std::optional<std::chrono::nanoseconds> timeout,
            felspar::source_location const &loc) {
        if (auto bytes = BIO_ctrl_pending(nb); bytes > buffer.size()) {
            throw felspar::stdexcept::logic_error{
                    "Pending read BIO buffer too small"};
        } else if (bytes) {
            if (auto const read_int = BIO_read(nb, buffer.data(), bytes);
                read_int <= 0) {
                throw felspar::stdexcept::runtime_error{
                        "Error reading from BIO"};
            } else if (std::size_t const read_bytes = read_int;
                       read_bytes != bytes) {
                throw felspar::stdexcept::runtime_error{
                        "Reading BIO read bytes mismatch"};
            } else {
                co_await felspar::io::write_all(
                        warden, fd, buffer.data(), bytes, timeout, loc);
            }
        }
    }
    io::warden::task<std::size_t> bio_write(
            io::warden &warden,
            std::optional<std::chrono::nanoseconds> timeout,
            felspar::source_location const &loc) {
        auto const bytes = co_await warden.read_some(fd, buffer, timeout, loc);
        if (bytes == 0) {
            co_return 0;
        } else if (auto const written_int = BIO_write(nb, buffer.data(), bytes);
                   written_int < 0) {
            throw felspar::stdexcept::runtime_error{"Error writing to BIO"};
        } else if (std::size_t const written_bytes = written_int;
                   written_bytes != bytes) {
            throw felspar::stdexcept::runtime_error{
                    "Not all bytes written to BIO"};
        } else {
            co_return written_bytes;
        }
    }
};


/// ## `felspar::io::tls`


felspar::io::tls::tls() = default;
felspar::io::tls::tls(tls &&) = default;
felspar::io::tls::~tls() = default;
felspar::io::tls &felspar::io::tls::operator=(tls &&) = default;

felspar::io::tls::tls(std::unique_ptr<impl> i) : p{std::move(i)} {}


auto felspar::io::tls::connect(
        io::warden &warden,
        char const *const sni_hostname,
        sockaddr const *addr,
        socklen_t addrlen,
        std::optional<std::chrono::nanoseconds> timeout,
        felspar::source_location const &loc) -> warden::task<tls> {
    posix::fd fd = warden.create_socket(AF_INET, SOCK_STREAM, 0);
    co_await warden.connect(fd, addr, addrlen, timeout, loc);

    auto i = std::make_unique<impl>(std::move(fd));
    SSL_set_tlsext_host_name(i->ssl, sni_hostname);
    co_await i->service_operation(
            warden, timeout, loc, [](impl &i) { return SSL_connect(i.ssl); });

    co_return tls{std::move(i)};
}


auto felspar::io::tls::read_some(
        io::warden &warden,
        std::span<std::byte> const s,
        std::optional<std::chrono::nanoseconds> const timeout,
        felspar::source_location const &loc) -> warden::task<std::size_t> {
    int const ret =
            co_await p->service_operation(warden, timeout, loc, [s](impl &i) {
                return SSL_read(i.ssl, s.data(), s.size());
            });
    if (ret < 0) {
        throw felspar::stdexcept::runtime_error{
                "Error performing SSL_read: " + p->ssl_error(ret), loc};
    } else {
        co_return static_cast<std::size_t>(ret);
    }
}


auto felspar::io::tls::write_some(
        io::warden &warden,
        std::span<std::byte const> const s,
        std::optional<std::chrono::nanoseconds> const timeout,
        felspar::source_location const &loc) -> warden::task<std::size_t> {
    int const ret =
            co_await p->service_operation(warden, timeout, loc, [s](impl &i) {
                return SSL_write(i.ssl, s.data(), s.size());
            });
    if (ret <= 0) {
        throw felspar::stdexcept::runtime_error{
                "Error performing SSL_write", loc};
    } else {
        co_return static_cast<std::size_t>(ret);
    }
}
