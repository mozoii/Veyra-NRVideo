#pragma once
#include <DirectXPackedVector.h>
#include <iostream>
// Test-only readback of a real FP16 graph output, so a hidden 8-bit
// conversion cannot pass merely because screenshots look correct.
inline bool readFp16(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring,ID3D12Resource* texture,std::vector<float>& pixels){
    using namespace veyra;auto desc=texture->GetDesc();if(desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;ctx.device()->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=size;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> read;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read))))return false;
    Status st;uint32_t slot;auto* list=ring.acquireNext(slot,st);if(!list)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=texture;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=texture;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=read.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;list->CopyTextureRegion(&b,0,0,0,&a,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;void* ptr=nullptr;D3D12_RANGE range{0,size};if(FAILED(read->Map(0,&range,&ptr)))return false;
    pixels.resize(size_t(desc.Width)*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y){const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(ptr)+fp.Offset+size_t(y)*fp.Footprint.RowPitch);for(size_t x=0;x<desc.Width*4;++x)pixels[(size_t(y)*desc.Width*4)+x]=DirectX::PackedVector::XMConvertHalfToFloat(row[x]);}
    D3D12_RANGE written{};read->Unmap(0,&written);return true;
}
