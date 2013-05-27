#ifndef BOOST_NETWORK_PROTOCOL_HTTP_SERVER_ASYNC_SERVER_HPP_20101025
#define BOOST_NETWORK_PROTOCOL_HTTP_SERVER_ASYNC_SERVER_HPP_20101025

// Copyright 2010 Dean Michael Berris. 
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/protocol/http/server/async_connection.hpp>
#include <boost/network/protocol/http/server/header.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>

namespace boost { namespace network { namespace http {

    template <class Tag, class Handler>
    struct async_server_base {
        struct ctx
        {
            size_t Connections;
            bool Stopped;
            boost::condition Condition;
            boost::mutex Mutex;

            ctx() : Connections(0), Stopped(false) {}
        };
        typedef boost::shared_ptr<ctx> ctx_ptr;

        typedef async_server_base<Tag, Handler> self;
        typedef basic_request<Tag> request;
        typedef basic_response<Tag> response;
        typedef typename string<Tag>::type string_type;
        typedef boost::network::http::response_header<Tag> response_header;
        typedef async_connection<Tag,Handler> connection;
        typedef shared_ptr<connection> connection_ptr;

        async_server_base(string_type const & address
                        , string_type const & port
                        , Handler & handler
                        , utils::thread_pool & thread_pool)
        : handler(handler)
        , thread_pool(thread_pool)
        , io_service()
        , acceptor(io_service)
        , stopping(false)
        , socket_exception(false)
        , ctx_(new ctx())

        {
            using boost::asio::ip::tcp;
            tcp::resolver resolver(io_service);
            tcp::resolver::query query(address, port);
            tcp::endpoint endpoint = *resolver.resolve(query);
            acceptor.open(endpoint.protocol());
            acceptor.bind(endpoint);
            boost::asio::ip::tcp::socket::reuse_address opt(true);
            acceptor.set_option(opt);
            acceptor.listen();
            new_connection.reset(new connection(io_service, handler, thread_pool,
                boost::bind(&self::connection_destroyed, ctx_)));
            acceptor.async_accept(new_connection->socket(),
                boost::bind(
                    &async_server_base<Tag,Handler>::handle_accept
                    , this
                    , boost::asio::placeholders::error));
        }

        void run() {
            if (socket_exception)
            {
                boost::system::error_code ec;
                acceptor.open(boost::asio::ip::tcp::v4(), ec);
            }
            io_service.run();
        };

        void stop() {
            // stop accepting new requests and let all the existing
            // handlers finish.
            stopping = true;

            {
                boost::mutex::scoped_lock lock(ctx_->Mutex);
                ctx_->Stopped = true;
                while(ctx_->Connections > 0)
                    ctx_->Condition.wait(lock);
            }

            if (!socket_exception)
            {
                try
                    { acceptor.cancel(); }
                catch(...)
                    { socket_exception = true; }
            }

            boost::system::error_code ec;
            if (socket_exception)
                acceptor.close(ec);
        }

    private:
        Handler & handler;
        utils::thread_pool & thread_pool;
        asio::io_service io_service;
        asio::ip::tcp::acceptor acceptor;
        bool stopping;
        connection_ptr new_connection;
        bool socket_exception;
        ctx_ptr ctx_;

        void handle_accept(boost::system::error_code const & ec) {
            if (!ec) {
                {
                    boost::mutex::scoped_lock lock(ctx_->Mutex);
                    if(ctx_->Stopped)
                        return;
                    ++ctx_->Connections;
                }

                new_connection->start();
                if (!stopping) {
                    new_connection.reset(
                        new connection(
                            io_service
                            , handler
                            , thread_pool
                            , boost::bind(&self::connection_destroyed, ctx_)
                            )
                        );
                    acceptor.async_accept(new_connection->socket(),
                        boost::bind(
                            &async_server_base<Tag,Handler>::handle_accept
                            , this
                            , boost::asio::placeholders::error
                            )
                        );
                }
            }
        }

        static void connection_destroyed(ctx_ptr c)
        {
            boost::mutex::scoped_lock lock(c->Mutex);
            --c->Connections;
            c->Condition.notify_all();
        }

    };

} /* http */
    
} /* network */
    
} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_SERVER_ASYNC_SERVER_HPP_20101025 */
