// Copyright 2023 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dawn/native/d3d/SharedTextureMemoryD3D.h"

#include <utility>

#include "dawn/native/ChainUtils.h"
#include "dawn/native/d3d/D3DError.h"
#include "dawn/native/d3d/DeviceD3D.h"
#include "dawn/native/d3d/Forward.h"
#include "dawn/native/d3d/QueueD3D.h"
#include "dawn/native/d3d/SharedFenceD3D.h"

namespace dawn::native::d3d {

SharedTextureMemory::SharedTextureMemory(d3d::Device* device,
                                         const char* label,
                                         SharedTextureMemoryProperties properties,
                                         IUnknown* resource)
    : SharedTextureMemoryBase(device, label, properties) {
    // If the resource has IDXGIKeyedMutex interface, it will be used for synchronization.
    // TODO(dawn:1906): remove the mDXGIKeyedMutex when it is not used in chrome.
    resource->QueryInterface(IID_PPV_ARGS(&mDXGIKeyedMutex));
}

MaybeError SharedTextureMemory::BeginAccessImpl(
    TextureBase* texture,
    const UnpackedPtr<BeginAccessDescriptor>& descriptor) {
    DAWN_TRY(descriptor.ValidateSubset<>());
    for (size_t i = 0; i < descriptor->fenceCount; ++i) {
        SharedFenceBase* fence = descriptor->fences[i];

        SharedFenceExportInfo exportInfo;
        DAWN_TRY(fence->ExportInfo(&exportInfo));
        switch (exportInfo.type) {
            case wgpu::SharedFenceType::DXGISharedHandle:
                DAWN_INVALID_IF(!GetDevice()->HasFeature(Feature::SharedFenceDXGISharedHandle),
                                "Required feature (%s) for %s is missing.",
                                wgpu::FeatureName::SharedFenceDXGISharedHandle,
                                wgpu::SharedFenceType::DXGISharedHandle);
                break;
            default:
                return DAWN_VALIDATION_ERROR("Unsupported fence type %s.", exportInfo.type);
        }
    }

    // Acquire keyed mutex for the first access.
    if (mDXGIKeyedMutex &&
        (HasWriteAccess() || HasExclusiveReadAccess() || GetReadAccessCount() == 1)) {
        DAWN_TRY(CheckHRESULT(mDXGIKeyedMutex->AcquireSync(kDXGIKeyedMutexAcquireKey, INFINITE),
                              "Acquire keyed mutex"));
    }
    return {};
}

ResultOrError<FenceAndSignalValue> SharedTextureMemory::EndAccessImpl(
    TextureBase* texture,
    UnpackedPtr<EndAccessState>& state) {
    DAWN_TRY(state.ValidateSubset<>());
    DAWN_INVALID_IF(!GetDevice()->HasFeature(Feature::SharedFenceDXGISharedHandle),
                    "Required feature (%s) is missing.",
                    wgpu::FeatureName::SharedFenceDXGISharedHandle);

    // Release keyed mutex for the last access.
    if (mDXGIKeyedMutex && !HasWriteAccess() && !HasExclusiveReadAccess() &&
        GetReadAccessCount() == 0) {
        mDXGIKeyedMutex->ReleaseSync(kDXGIKeyedMutexAcquireKey);
    }

    Ref<SharedFence> sharedFence;
    DAWN_TRY_ASSIGN(sharedFence, ToBackend(GetDevice()->GetQueue())->GetOrCreateSharedFence());

    return FenceAndSignalValue{
        std::move(sharedFence),
        static_cast<uint64_t>(texture->GetSharedTextureMemoryContents()->GetLastUsageSerial())};
}

}  // namespace dawn::native::d3d
