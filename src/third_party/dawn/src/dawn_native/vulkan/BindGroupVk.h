// Copyright 2018 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DAWNNATIVE_VULKAN_BINDGROUPVK_H_
#define DAWNNATIVE_VULKAN_BINDGROUPVK_H_

#include "dawn_native/BindGroup.h"

#include "common/vulkan_platform.h"
#include "dawn_native/vulkan/BindGroupLayoutVk.h"

namespace dawn_native { namespace vulkan {

    class Device;

    class BindGroup : public BindGroupBase {
      public:
        static ResultOrError<BindGroup*> Create(Device* device,
                                                const BindGroupDescriptor* descriptor);
        ~BindGroup();

        VkDescriptorSet GetHandle() const;

      private:
        using BindGroupBase::BindGroupBase;
        MaybeError Initialize();

        // The descriptor set in this allocation outlives the BindGroup because it is owned by
        // the BindGroupLayout which is referenced by the BindGroup.
        DescriptorSetAllocation mAllocation;
    };

}}  // namespace dawn_native::vulkan

#endif  // DAWNNATIVE_VULKAN_BINDGROUPVK_H_
