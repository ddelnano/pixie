/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "src/common/event/nats.h"

#include <nats/adapters/libuv.h>
#include <nats/nats.h>

namespace px {
namespace event {

namespace {

std::unique_ptr<NATSPromMetrics> g_nats_metrics = std::make_unique<NATSPromMetrics>();

void ErrorHandler(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure) {
  PX_UNUSED(nc);
  PX_UNUSED(sub);
  if (err == NATS_SLOW_CONSUMER) {
    char *subj = static_cast<char *>(closure);
    LOG(ERROR) << absl::Substitute("Slow consumer subscription topic: $0", subj);
    g_nats_metrics->error_counts_.Add({{"name", "nats_error_count"}, {"natsStatus", natsStatus_GetText(err)}, {"subject", subj}});
  } else {
    LOG(ERROR) << absl::Substitute("Asynchronous error: $0 - $1", err, natsStatus_GetText(err));
    g_nats_metrics->error_counts_.Add({{"name", "nats_error_count"}, {"natsStatus", natsStatus_GetText(err)}});
  }
}

} // namespace

NATSPromMetrics::NATSPromMetrics()
    : disconnects_(prometheus::BuildCounter()
               .Name("nats_disconnects")
               .Help("")
               .Register(GetMetricsRegistry())
               .Add({{"name", "nats_disconnects"}})),
      reconnects_(prometheus::BuildCounter()
               .Name("nats_reconnects")
               .Help("")
               .Register(GetMetricsRegistry())
               .Add({{"name", "nats_reconnects"}})),
      error_counts_(
          prometheus::BuildCounter()
              .Name("nats_error_counts")
              .Help("")
              .Register(GetMetricsRegistry())) {}

Status NATSConnectorBase::ConnectBase(Dispatcher* base_dispatcher, std::string& sub_topic, std::string& /*pub_topic*/) {
  natsOptions* nats_opts = nullptr;
  natsOptions_Create(&nats_opts);
  char* sub_topic_cstr = const_cast<char*>(sub_topic.c_str());
  natsOptions_SetErrorHandler(nats_opts, ErrorHandler, static_cast<void*>(sub_topic_cstr));

  LibuvDispatcher* dispatcher = dynamic_cast<LibuvDispatcher*>(base_dispatcher);
  if (dispatcher == nullptr) {
    return error::InvalidArgument("Only libuv based dispatcher is allowed");
  }

  natsLibuv_Init();

  // Tell nats we are using libuv so it uses the correct thread.
  natsLibuv_SetThreadLocalLoop(dispatcher->uv_loop());

  if (tls_config_ != nullptr) {
    natsOptions_SetSecure(nats_opts, true);
    natsOptions_LoadCATrustedCertificates(nats_opts, tls_config_->ca_cert.c_str());
    natsOptions_LoadCertificatesChain(nats_opts, tls_config_->tls_cert.c_str(),
                                      tls_config_->tls_key.c_str());
  }

  natsOptions_SetMaxReconnect(nats_opts, -1);
  natsOptions_SetDisconnectedCB(nats_opts, DisconnectedCB, this);
  natsOptions_SetReconnectedCB(nats_opts, ReconnectedCB, this);

  auto s = natsOptions_SetEventLoop(nats_opts, dispatcher->uv_loop(), natsLibuv_Attach,
                                    natsLibuv_Read, natsLibuv_Write, natsLibuv_Detach);

  if (s != NATS_OK) {
    nats_PrintLastErrorStack(stderr);
    return error::Unknown("Failed to set NATS event loop, nats_status=$0", s);
  }

  natsOptions_SetURL(nats_opts, nats_server_.c_str());

  auto nats_status = natsConnection_Connect(&nats_connection_, nats_opts);
  natsOptions_Destroy(nats_opts);
  nats_opts = nullptr;

  if (nats_status != NATS_OK) {
    nats_PrintLastErrorStack(stderr);
    return error::Unknown("Failed to connect to NATS, nats_status=$0", nats_status);
  }
  return Status::OK();
}

void NATSConnectorBase::DisconnectedCB(natsConnection* nc, void* closure) {
  PX_UNUSED(nc);
  auto* connector = static_cast<NATSConnectorBase*>(closure);
  LOG(WARNING) << "nats disconnected " << ++connector->disconnect_count_;
  g_nats_metrics->disconnects_.Increment();
}

void NATSConnectorBase::ReconnectedCB(natsConnection* nc, void* closure) {
  PX_UNUSED(nc);
  auto* connector = static_cast<NATSConnectorBase*>(closure);
  LOG(INFO) << "nats reconnected " << ++connector->reconnect_count_;
  g_nats_metrics->reconnects_.Increment();
}

}  // namespace event
}  // namespace px
