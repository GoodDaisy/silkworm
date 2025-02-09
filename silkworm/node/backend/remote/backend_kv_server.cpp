/*
   Copyright 2022 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "backend_kv_server.hpp"

#include <silkworm/infra/concurrency/task.hpp>

#include <silkworm/infra/common/log.hpp>
#include <silkworm/node/backend/remote/grpc/backend_calls.hpp>
#include <silkworm/node/backend/remote/grpc/kv_calls.hpp>

namespace silkworm::rpc {

BackEndKvServer::BackEndKvServer(const ServerSettings& settings, const EthereumBackEnd& backend)
    : Server(settings), backend_(backend) {
    setup_backend_calls(backend);
    setup_kv_calls();
    SILK_INFO << "BackEndKvServer created listening on: " << settings.address_uri;
}

// Register the gRPC services: they must exist for the lifetime of the server built by builder.
void BackEndKvServer::register_async_services(grpc::ServerBuilder& builder) {
    builder.RegisterService(&backend_async_service_);
    builder.RegisterService(&kv_async_service_);
}

void BackEndKvServer::setup_backend_calls(const EthereumBackEnd& backend) {
    EtherbaseCall::fill_predefined_reply(backend);
    NetVersionCall::fill_predefined_reply(backend);
    BackEndVersionCall::fill_predefined_reply();
    ProtocolVersionCall::fill_predefined_reply();
    ClientVersionCall::fill_predefined_reply(backend);
}

void BackEndKvServer::register_backend_request_calls(agrpc::GrpcContext* grpc_context) {
    SILK_TRACE << "BackEndService::register_backend_request_calls START";
    auto service = &backend_async_service_;
    auto& backend = backend_;

    // Register one requested call repeatedly for each RPC: asio-grpc will take care of re-registration on any incoming call
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestEtherbase,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await EtherbaseCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestNetVersion,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await NetVersionCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestNetPeerCount,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await NetPeerCountCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestVersion,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await BackEndVersionCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestProtocolVersion,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await ProtocolVersionCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestClientVersion,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await ClientVersionCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestSubscribe,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await SubscribeCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::ETHBACKEND::AsyncService::RequestNodeInfo,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await NodeInfoCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    SILK_TRACE << "BackEndService::register_backend_request_calls END";
}

void BackEndKvServer::setup_kv_calls() {
    KvVersionCall::fill_predefined_reply();
}

void BackEndKvServer::register_kv_request_calls(agrpc::GrpcContext* grpc_context) {
    SILK_TRACE << "BackEndKvServer::register_kv_request_calls START";
    auto service = &kv_async_service_;
    auto& backend = backend_;

    // Register one requested call repeatedly for each RPC: asio-grpc will take care of re-registration on any incoming call
    request_repeatedly(*grpc_context, service, &remote::KV::AsyncService::RequestVersion,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await KvVersionCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::KV::AsyncService::RequestTx,
                       [&backend, grpc_context](auto&&... args) -> Task<void> {
                           co_await TxCall{*grpc_context, std::forward<decltype(args)>(args)...}(backend);
                       });
    request_repeatedly(*grpc_context, service, &remote::KV::AsyncService::RequestStateChanges,
                       [&backend](auto&&... args) -> Task<void> {
                           co_await StateChangesCall{std::forward<decltype(args)>(args)...}(backend);
                       });
    SILK_TRACE << "BackEndKvServer::register_kv_request_calls END";
}

//! Start server-side RPC requests as required by gRPC async model: one RPC per type is requested in advance.
void BackEndKvServer::register_request_calls() {
    // Start all server-side RPC requests for each available server context
    for (std::size_t i = 0; i < num_contexts(); i++) {
        const auto& context = next_context();
        auto grpc_context = context.server_grpc_context();

        // Register initial requested calls for ETHBACKEND and KV services
        register_backend_request_calls(grpc_context);
        register_kv_request_calls(grpc_context);
    }
}

}  // namespace silkworm::rpc
