// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/bindings/bindings_manager.h"

#include <utility>

namespace chromecast {
namespace bindings {

BindingsManager::BindingsManager() = default;

BindingsManager::~BindingsManager() {
  DCHECK(port_handlers_.empty());
}

// TODO(crbug.com/803242): Deprecated and will be shortly removed.
void BindingsManager::RegisterPortHandler(base::StringPiece port_name,
                                          PortConnectedHandler handler) {
  MessagePortConnectedHandler wrapped_handler = base::BindRepeating(
      [](PortConnectedHandler handler, blink::WebMessagePort port) {
        handler.Run(port.PassHandle());
      },
      handler);
  RegisterPortHandler(port_name, wrapped_handler);
}

void BindingsManager::RegisterPortHandler(base::StringPiece port_name,
                                          MessagePortConnectedHandler handler) {
  auto result = port_handlers_.try_emplace(port_name, std::move(handler));
  DCHECK(result.second);
}

void BindingsManager::UnregisterPortHandler(base::StringPiece port_name) {
  size_t deleted = port_handlers_.erase(port_name);
  DCHECK_EQ(deleted, 1u);
}

// TODO(crbug.com/803242): Deprecated and will be shortly removed.
void BindingsManager::OnPortConnected(base::StringPiece port_name,
                                      mojo::ScopedMessagePipeHandle port) {
  OnPortConnected(port_name, blink::WebMessagePort::Create(std::move(port)));
}

void BindingsManager::OnPortConnected(base::StringPiece port_name,
                                      blink::WebMessagePort port) {
  auto handler = port_handlers_.find(port_name);
  if (handler == port_handlers_.end()) {
    LOG(ERROR) << "No handler found for port " << port_name << ".";
    return;
  }

  handler->second.Run(std::move(port));
}

}  // namespace bindings
}  // namespace chromecast
