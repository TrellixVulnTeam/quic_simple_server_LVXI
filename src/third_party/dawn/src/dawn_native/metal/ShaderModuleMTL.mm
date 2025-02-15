// Copyright 2017 The Dawn Authors
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

#include "dawn_native/metal/ShaderModuleMTL.h"

#include "dawn_native/BindGroupLayout.h"
#include "dawn_native/metal/DeviceMTL.h"
#include "dawn_native/metal/PipelineLayoutMTL.h"

#include <spirv_msl.hpp>

#include <sstream>

namespace dawn_native { namespace metal {

    namespace {

        spv::ExecutionModel SpirvExecutionModelForStage(SingleShaderStage stage) {
            switch (stage) {
                case SingleShaderStage::Vertex:
                    return spv::ExecutionModelVertex;
                case SingleShaderStage::Fragment:
                    return spv::ExecutionModelFragment;
                case SingleShaderStage::Compute:
                    return spv::ExecutionModelGLCompute;
                default:
                    UNREACHABLE();
            }
        }

        shaderc_spvc_execution_model ToSpvcExecutionModel(SingleShaderStage stage) {
            switch (stage) {
                case SingleShaderStage::Vertex:
                    return shaderc_spvc_execution_model_vertex;
                case SingleShaderStage::Fragment:
                    return shaderc_spvc_execution_model_fragment;
                case SingleShaderStage::Compute:
                    return shaderc_spvc_execution_model_glcompute;
                default:
                    UNREACHABLE();
                    return shaderc_spvc_execution_model_invalid;
            }
        }

        shaderc_spvc::CompileOptions GetMSLCompileOptions() {
            // If these options are changed, the values in DawnSPIRVCrossGLSLFastFuzzer.cpp need to
            // be updated.
            shaderc_spvc::CompileOptions options;

            // Disable PointSize builtin for https://bugs.chromium.org/p/dawn/issues/detail?id=146
            // Because Metal will reject PointSize builtin if the shader is compiled into a render
            // pipeline that uses a non-point topology.
            // TODO (hao.x.li@intel.com): Remove this once WebGPU requires there is no
            // gl_PointSize builtin (https://github.com/gpuweb/gpuweb/issues/332).
            options.SetMSLEnablePointSizeBuiltIn(false);

            // Always use vertex buffer 30 (the last one in the vertex buffer table) to contain
            // the shader storage buffer lengths.
            options.SetMSLBufferSizeBufferIndex(kBufferLengthBufferSlot);

            return options;
        }
    }  // namespace

    // static
    ResultOrError<ShaderModule*> ShaderModule::Create(Device* device,
                                                      const ShaderModuleDescriptor* descriptor) {
        std::unique_ptr<ShaderModule> module(new ShaderModule(device, descriptor));
        if (!module)
            return DAWN_VALIDATION_ERROR("Unable to create ShaderModule");
        DAWN_TRY(module->Initialize(descriptor));
        return module.release();
    }

    ShaderModule::ShaderModule(Device* device, const ShaderModuleDescriptor* descriptor)
        : ShaderModuleBase(device, descriptor) {
    }

    MaybeError ShaderModule::Initialize(const ShaderModuleDescriptor* descriptor) {
        mSpirv.assign(descriptor->code, descriptor->code + descriptor->codeSize);
        if (GetDevice()->IsToggleEnabled(Toggle::UseSpvc)) {
            DAWN_TRY(CheckSpvcSuccess(
                mSpvcContext.InitializeForMsl(descriptor->code, descriptor->codeSize,
                                              GetMSLCompileOptions()),
                "Unable to initialize instance of spvc"));

            spirv_cross::CompilerMSL* compiler;
            DAWN_TRY(CheckSpvcSuccess(mSpvcContext.GetCompiler(reinterpret_cast<void**>(&compiler)),
                                      "Unable to get cross compiler"));
            DAWN_TRY(ExtractSpirvInfo(*compiler));
        } else {
            spirv_cross::CompilerMSL compiler(mSpirv);
            DAWN_TRY(ExtractSpirvInfo(compiler));
        }
        return {};
    }

    MaybeError ShaderModule::GetFunction(const char* functionName,
                                         SingleShaderStage functionStage,
                                         const PipelineLayout* layout,
                                         ShaderModule::MetalFunctionData* out) {
        ASSERT(!IsError());
        ASSERT(out);
        std::unique_ptr<spirv_cross::CompilerMSL> compiler_impl;
        spirv_cross::CompilerMSL* compiler;
        if (GetDevice()->IsToggleEnabled(Toggle::UseSpvc)) {
            // Initializing the compiler is needed every call, because this method uses reflection
            // to mutate the compiler's IR.
            DAWN_TRY(CheckSpvcSuccess(
                mSpvcContext.InitializeForMsl(mSpirv.data(), mSpirv.size(), GetMSLCompileOptions()),
                "Unable to initialize instance of spvc"));
            DAWN_TRY(CheckSpvcSuccess(mSpvcContext.GetCompiler(reinterpret_cast<void**>(&compiler)),
                                      "Unable to get cross compiler"));
        } else {
            // If these options are changed, the values in DawnSPIRVCrossMSLFastFuzzer.cpp need to
            // be updated.
            spirv_cross::CompilerMSL::Options options_msl;

            // Disable PointSize builtin for https://bugs.chromium.org/p/dawn/issues/detail?id=146
            // Because Metal will reject PointSize builtin if the shader is compiled into a render
            // pipeline that uses a non-point topology.
            // TODO (hao.x.li@intel.com): Remove this once WebGPU requires there is no
            // gl_PointSize builtin (https://github.com/gpuweb/gpuweb/issues/332).
            options_msl.enable_point_size_builtin = false;

            // Always use vertex buffer 30 (the last one in the vertex buffer table) to contain
            // the shader storage buffer lengths.
            options_msl.buffer_size_buffer_index = kBufferLengthBufferSlot;

            compiler_impl = std::make_unique<spirv_cross::CompilerMSL>(mSpirv);
            compiler = compiler_impl.get();
            compiler->set_msl_options(options_msl);
        }

        // By default SPIRV-Cross will give MSL resources indices in increasing order.
        // To make the MSL indices match the indices chosen in the PipelineLayout, we build
        // a table of MSLResourceBinding to give to SPIRV-Cross.

        // Create one resource binding entry per stage per binding.
        for (uint32_t group : IterateBitSet(layout->GetBindGroupLayoutsMask())) {
            const auto& bgInfo = layout->GetBindGroupLayout(group)->GetBindingInfo();
            for (uint32_t binding : IterateBitSet(bgInfo.mask)) {
                for (auto stage : IterateStages(bgInfo.visibilities[binding])) {
                    uint32_t index = layout->GetBindingIndexInfo(stage)[group][binding];
                    if (GetDevice()->IsToggleEnabled(Toggle::UseSpvc)) {
                        shaderc_spvc_msl_resource_binding mslBinding;
                        mslBinding.stage = ToSpvcExecutionModel(stage);
                        mslBinding.desc_set = group;
                        mslBinding.binding = binding;
                        mslBinding.msl_buffer = mslBinding.msl_texture = mslBinding.msl_sampler =
                            index;
                        DAWN_TRY(CheckSpvcSuccess(mSpvcContext.AddMSLResourceBinding(mslBinding),
                                                  "Unable to add MSL Resource Binding"));
                    } else {
                        spirv_cross::MSLResourceBinding mslBinding;
                        mslBinding.stage = SpirvExecutionModelForStage(stage);
                        mslBinding.desc_set = group;
                        mslBinding.binding = binding;
                        mslBinding.msl_buffer = mslBinding.msl_texture = mslBinding.msl_sampler =
                            index;

                        compiler->add_msl_resource_binding(mslBinding);
                    }
                }
            }
        }

        {
            if (GetDevice()->IsToggleEnabled(Toggle::UseSpvc)) {
                shaderc_spvc_execution_model executionModel = ToSpvcExecutionModel(functionStage);
                shaderc_spvc_workgroup_size size;
                DAWN_TRY(CheckSpvcSuccess(
                    mSpvcContext.GetWorkgroupSize(functionName, executionModel, &size),
                    "Unable to get workgroup size for shader"));
                out->localWorkgroupSize = MTLSizeMake(size.x, size.y, size.z);
            } else {
                spv::ExecutionModel executionModel = SpirvExecutionModelForStage(functionStage);
                auto size = compiler->get_entry_point(functionName, executionModel).workgroup_size;
                out->localWorkgroupSize = MTLSizeMake(size.x, size.y, size.z);
            }
        }

        {
            // SPIRV-Cross also supports re-ordering attributes but it seems to do the correct thing
            // by default.
            NSString* mslSource;
            if (GetDevice()->IsToggleEnabled(Toggle::UseSpvc)) {
                shaderc_spvc::CompilationResult result;
                DAWN_TRY(CheckSpvcSuccess(mSpvcContext.CompileShader(&result),
                                          "Unable to compile MSL shader"));
                std::string result_str;
                DAWN_TRY(CheckSpvcSuccess(result.GetStringOutput(&result_str),
                                          "Unable to get MSL shader text"));
                mslSource = [NSString stringWithFormat:@"%s", result_str.c_str()];
            } else {
                std::string msl = compiler->compile();
                mslSource = [NSString stringWithFormat:@"%s", msl.c_str()];
            }
            auto mtlDevice = ToBackend(GetDevice())->GetMTLDevice();
            NSError* error = nil;
            id<MTLLibrary> library = [mtlDevice newLibraryWithSource:mslSource
                                                             options:nil
                                                               error:&error];
            if (error != nil) {
                // TODO(cwallez@chromium.org): Switch that NSLog to use dawn::InfoLog or even be
                // folded in the DAWN_VALIDATION_ERROR
                NSLog(@"MTLDevice newLibraryWithSource => %@", error);
                if (error.code != MTLLibraryErrorCompileWarning) {
                    return DAWN_VALIDATION_ERROR("Unable to create library object");
                }
            }

            // TODO(kainino@chromium.org): make this somehow more robust; it needs to behave like
            // clean_func_name:
            // https://github.com/KhronosGroup/SPIRV-Cross/blob/4e915e8c483e319d0dd7a1fa22318bef28f8cca3/spirv_msl.cpp#L1213
            if (strcmp(functionName, "main") == 0) {
                functionName = "main0";
            }

            NSString* name = [NSString stringWithFormat:@"%s", functionName];
            out->function = [library newFunctionWithName:name];
            [library release];
        }

        if (GetDevice()->IsToggleEnabled(Toggle::UseSpvc)) {
            DAWN_TRY(
                CheckSpvcSuccess(mSpvcContext.NeedsBufferSizeBuffer(&out->needsStorageBufferLength),
                                 "Unable to determine if shader needs buffer size buffer"));
        } else {
            out->needsStorageBufferLength = compiler->needs_buffer_size_buffer();
        }

        return {};
    }

}}  // namespace dawn_native::metal
