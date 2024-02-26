#include "icarus-proto/coredefs.h"

#ifdef ICARUS_NIX

#include "icarus-proto/net/acceptor.h"
#include "icarus-proto/net/stream.h"
#include "icarus-proto/net/acceptor.h"
#include "icarus-proto/net/factory.h"
#include "icarus-proto/net/init.h"

namespace {

    class nix_acceptor final : public icarus::io::acceptor {
    public:
        nix_acceptor() {
            icarus::io::init();
        }

    private:
        virtual void run(std::string_view port, on_new_connection_t&& in_on_new_connection) override {
            on_new_connection = std::move(in_on_new_connection);
            connect(port);
            while(active.load(std::memory_order_acquire)) {
               // listen
            }
        }

        virtual void stop() override {
            active.store(false, std::memory_order_release);
        }

        void connect(std::string_view port) {

        }


        on_new_connection_t on_new_connection;
        std::atomic<bool> active{ false };
    };

}

namespace icarus::io {

    acceptor* create_acceptor() {
        return new nix_acceptor;
    }

}

#endif