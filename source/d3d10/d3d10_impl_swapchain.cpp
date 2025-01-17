/*
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "dll_log.hpp"
#include "d3d10_impl_device.hpp"
#include "d3d10_impl_swapchain.hpp"
#include "d3d10_impl_type_convert.hpp"

reshade::d3d10::swapchain_impl::swapchain_impl(device_impl *device, IDXGISwapChain *swapchain) :
	api_object_impl(swapchain, device, device),
	_app_state(device->_orig)
{
	_renderer_id = device->_orig->GetFeatureLevel();

	if (com_ptr<IDXGIDevice> dxgi_device;
		SUCCEEDED(device->_orig->QueryInterface(&dxgi_device)))
	{
		if (com_ptr<IDXGIAdapter> dxgi_adapter;
			SUCCEEDED(dxgi_device->GetAdapter(&dxgi_adapter)))
		{
			if (DXGI_ADAPTER_DESC desc; SUCCEEDED(dxgi_adapter->GetDesc(&desc)))
			{
				_vendor_id = desc.VendorId;
				_device_id = desc.DeviceId;

				LOG(INFO) << "Running on " << desc.Description;
			}
		}
	}

	on_init();
}
reshade::d3d10::swapchain_impl::~swapchain_impl()
{
	on_reset();
}

void reshade::d3d10::swapchain_impl::get_back_buffer(uint32_t index, api::resource *out)
{
	assert(index == 0);

	*out = { reinterpret_cast<uintptr_t>(_backbuffer.get()) };
}
void reshade::d3d10::swapchain_impl::get_back_buffer_resolved(uint32_t index, api::resource *out)
{
	assert(index == 0);

	*out = { reinterpret_cast<uintptr_t>(_backbuffer_resolved.get()) };
}

bool reshade::d3d10::swapchain_impl::on_init()
{
	assert(_orig != nullptr);

	DXGI_SWAP_CHAIN_DESC swap_desc;
	if (FAILED(_orig->GetDesc(&swap_desc)))
		return false;

	// Get back buffer texture
	if (FAILED(_orig->GetBuffer(0, IID_PPV_ARGS(&_backbuffer))))
		return false;
	assert(_backbuffer != nullptr);

#if RESHADE_ADDON
	invoke_addon_event<addon_event::init_swapchain>(this);
#endif

	if (swap_desc.SampleDesc.Count > 1)
	{
		D3D10_TEXTURE2D_DESC tex_desc = {};
		tex_desc.Width = swap_desc.BufferDesc.Width;
		tex_desc.Height = swap_desc.BufferDesc.Height;
		tex_desc.MipLevels = 1;
		tex_desc.ArraySize = 1;
		tex_desc.Format = swap_desc.BufferDesc.Format;
		tex_desc.SampleDesc = { 1, 0 };
		tex_desc.Usage = D3D10_USAGE_DEFAULT;
		tex_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET;

		if (FAILED(static_cast<device_impl *>(_device)->_orig->CreateTexture2D(&tex_desc, nullptr, &_backbuffer_resolved)))
		{
			LOG(ERROR) << "Failed to create back buffer resolve texture!";
			return false;
		}
		if (FAILED(static_cast<device_impl *>(_device)->_orig->CreateRenderTargetView(_backbuffer.get(), nullptr, &_backbuffer_rtv)))
		{
			LOG(ERROR) << "Failed to create original back buffer render target!";
			return false;
		}
		if (FAILED(static_cast<device_impl *>(_device)->_orig->CreateShaderResourceView(_backbuffer_resolved.get(), nullptr, &_backbuffer_resolved_srv)))
		{
			LOG(ERROR) << "Failed to create back buffer resolve shader resource view!";
			return false;
		}
	}
	else
	{
		assert(swap_desc.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT);

		_backbuffer_resolved = _backbuffer;
	}

	_width = swap_desc.BufferDesc.Width;
	_height = swap_desc.BufferDesc.Height;
	_backbuffer_format = convert_format(swap_desc.BufferDesc.Format);

	return runtime::on_init(swap_desc.OutputWindow);
}
void reshade::d3d10::swapchain_impl::on_reset()
{
	runtime::on_reset();

#if RESHADE_ADDON
	invoke_addon_event<addon_event::destroy_swapchain>(this);
#endif

	_backbuffer.reset();
	_backbuffer_resolved.reset();
	_backbuffer_rtv.reset();
	_backbuffer_resolved_srv.reset();
}

void reshade::d3d10::swapchain_impl::on_present()
{
	if (!is_initialized())
		return;

	ID3D10Device *const immediate_context = static_cast<device_impl *>(_graphics_queue)->_orig;
	_app_state.capture();

	// Resolve MSAA back buffer if MSAA is active
	if (_backbuffer_resolved != _backbuffer)
		immediate_context->ResolveSubresource(_backbuffer_resolved.get(), 0, _backbuffer.get(), 0, convert_format(_backbuffer_format));

	runtime::on_present();

	// Stretch main render target back into MSAA back buffer if MSAA is active
	if (_backbuffer_resolved != _backbuffer)
	{
		immediate_context->IASetInputLayout(nullptr);
		const uintptr_t null = 0;
		immediate_context->IASetVertexBuffers(0, 1, reinterpret_cast<ID3D10Buffer *const *>(&null), reinterpret_cast<const UINT *>(&null), reinterpret_cast<const UINT *>(&null));
		immediate_context->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		immediate_context->VSSetShader(static_cast<device_impl *>(_device)->_copy_vert_shader.get());
		immediate_context->GSSetShader(nullptr);
		immediate_context->PSSetShader(static_cast<device_impl *>(_device)->_copy_pixel_shader.get());
		ID3D10SamplerState *const samplers[] = { static_cast<device_impl *>(_device)->_copy_sampler_state.get() };
		immediate_context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);
		ID3D10ShaderResourceView *const srvs[] = { _backbuffer_resolved_srv.get() };
		immediate_context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
		immediate_context->RSSetState(nullptr);
		const D3D10_VIEWPORT viewport = { 0, 0, _width, _height, 0.0f, 1.0f };
		immediate_context->RSSetViewports(1, &viewport);
		immediate_context->OMSetBlendState(nullptr, nullptr, D3D10_DEFAULT_SAMPLE_MASK);
		immediate_context->OMSetDepthStencilState(nullptr, D3D10_DEFAULT_STENCIL_REFERENCE);
		ID3D10RenderTargetView *const render_targets[] = { _backbuffer_rtv.get() };
		immediate_context->OMSetRenderTargets(ARRAYSIZE(render_targets), render_targets, nullptr);

		immediate_context->Draw(3, 0);
	}

	// Apply previous state from application
	_app_state.apply_and_release();
}
