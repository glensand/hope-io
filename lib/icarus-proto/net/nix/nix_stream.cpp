#include "icarus-proto/coredefs.h"

#ifdef ICARUS_NIX

#include <stdexcept>

#include "icarus-proto/net/stream.h"
#include "icarus-proto/net/init.h"
#include "icarus-proto/net/factory.h"

namespace {

    class nix_stream final : public icarus::io::stream {
    public:
        explicit nix_stream(unsigned long long in_socket) {

        }

        virtual ~nix_stream() override {

        }

    private:
        virtual void connect(const std::string_view ip, const std::string_view port) override {

        }

        virtual void disconnect() override {

        }

        virtual void write(const void* data, std::size_t length) override {

        }

        virtual void read(void* data, std::size_t length) override {

        }
    };

}

namespace icarus::io {

    stream* create_stream(unsigned long long socket) {
        return new nix_stream(socket);
    }

}

#endif
