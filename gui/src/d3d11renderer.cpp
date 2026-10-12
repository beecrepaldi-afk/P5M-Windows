// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// P5M for Windows: Direct3D 11 presenter. The swapchain setup (flip model,
// tearing, HDR10 colour space) follows the approach of Moonlight's D3D11VA
// renderer (moonlight-stream/moonlight-qt, GPL-3.0); the code here is our own.

// windows.h would otherwise turn std::min/std::max into its macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "d3d11renderer.h"

#include <QLoggingCategory>
#include <QQuickGraphicsDevice>
#include <QQuickRenderTarget>
#include <QWindow>

#include <atomic>
#include <iterator>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <mutex>
#include <thread>

#include <windows.h>
#include <d3d11_1.h>
#include <d3d10.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
}

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

using Microsoft::WRL::ComPtr;

namespace {

// One vertex shader for both passes: a triangle that covers the viewport.
const char kVertexShader[] = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint id : SV_VertexID)
{
	VSOut o;
	o.uv = float2((id << 1) & 2, id & 2);
	o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
	return o;
}
)";

// Video: YUV (semi-planar or planar) -> RGB, then to the output space.
//   transfer 0 = SDR (BT.1886), 1 = PQ (BT.2100)
//   output   0 = SDR swapchain (BT.709, gamma), 1 = HDR10 swapchain (PQ, BT.2020)
const char kVideoShader[] = R"(
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
SamplerState s : register(s0);
cbuffer C : register(b0)
{
	float4 row0; float4 row1; float4 row2;
	float yscale; int planar; int transfer; int output;
	float sdr_white_nits; float panel_white_nits; float source_peak_nits; float pad;
};

static const float m1 = 0.1593017578125, m2 = 78.84375;
static const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;

float3 pq_to_nits(float3 e)
{
	float3 p = pow(saturate(e), 1.0 / m2);
	return 10000.0 * pow(max(p - c1, 0) / (c2 - c3 * p), 1.0 / m1);
}
float3 nits_to_pq(float3 n)
{
	float3 y = pow(saturate(n / 10000.0), m1);
	return pow((c1 + c2 * y) / (1 + c3 * y), m2);
}
float3 bt2020_to_709(float3 c)
{
	return float3(dot(c, float3( 1.6605, -0.5876, -0.0728)),
	              dot(c, float3(-0.1246,  1.1329, -0.0083)),
	              dot(c, float3(-0.0182, -0.1006,  1.1187)));
}
float3 bt709_to_2020(float3 c)
{
	return float3(dot(c, float3(0.6274, 0.3293, 0.0433)),
	              dot(c, float3(0.0691, 0.9195, 0.0114)),
	              dot(c, float3(0.0164, 0.0880, 0.8956)));
}
// Linear up to 0.8 of the panel, then a smooth shoulder that reaches 1 at the
// source's peak; applied to the largest channel so hue is kept.
float3 tone_map(float3 nits)
{
	float3 x = max(nits, 0) / panel_white_nits;
	float m = max(max(x.r, x.g), x.b);
	if (m <= 0.8)
		return x;
	float t = (m - 0.8) / 0.2;
	float tmax = max((source_peak_nits / panel_white_nits - 0.8) / 0.2, 1.0);
	float y = 0.8 + 0.2 * (t * (1 + t / (tmax * tmax)) / (1 + t));
	return x * (min(y, 1.0) / m);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
	float3 yuv;
	yuv.x = t0.Sample(s, uv).r;
	if (planar)
		yuv.yz = float2(t1.Sample(s, uv).r, t2.Sample(s, uv).r);
	else
		yuv.yz = t1.Sample(s, uv).rg;
	yuv *= yscale;
	float4 y1 = float4(yuv, 1);
	float3 rgb = saturate(float3(dot(row0, y1), dot(row1, y1), dot(row2, y1)));

	if (transfer == 1) {
		if (output == 1)
			return float4(rgb, 1);
		float3 lin = tone_map(bt2020_to_709(pq_to_nits(rgb)));
		return float4(pow(saturate(lin), 1.0 / 2.2), 1);
	}
	if (output == 1) {
		float3 lin = pow(rgb, 2.4) * sdr_white_nits;
		return float4(nits_to_pq(bt709_to_2020(lin)), 1);
	}
	return float4(rgb, 1);
}
)";

// UI: Qt Quick's premultiplied, sRGB-encoded texture.
const char kOverlayShader[] = R"(
Texture2D<float4> t0 : register(t0);
SamplerState s : register(s0);
cbuffer C : register(b0)
{
	float4 row0; float4 row1; float4 row2;
	float yscale; int planar; int transfer; int output;
	float sdr_white_nits; float panel_white_nits; float source_peak_nits; float pad;
};
static const float m1 = 0.1593017578125, m2 = 78.84375;
static const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
	float4 c = t0.Sample(s, uv);
	if (output == 0 || c.a <= 0)
		return c;
	float3 straight = c.rgb / c.a;
	float3 lin = pow(saturate(straight), 2.2) * sdr_white_nits;
	lin = float3(dot(lin, float3(0.6274, 0.3293, 0.0433)),
	             dot(lin, float3(0.0691, 0.9195, 0.0114)),
	             dot(lin, float3(0.0164, 0.0880, 0.8956)));
	float3 y = pow(saturate(lin / 10000.0), m1);
	float3 pq = pow((c1 + c2 * y) / (1 + c3 * y), m2);
	return float4(pq * c.a, c.a);
}
)";

// Coverage: passes only where the UI has any opacity. Counted with an
// occlusion query whenever Qt redraws the UI, so the UI pass can be skipped
// while it is empty (most of a game).
const char kCoverageShader[] = R"(
Texture2D<float4> t0 : register(t0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
	if (t0.Load(int3(pos.xy, 0)).a <= 0)
		discard;
	return 0;
}
)";

struct alignas(16) ShaderConstants
{
	float row0[4], row1[4], row2[4];
	float yscale;
	int planar;
	int transfer;
	int output;
	float sdr_white_nits;
	float panel_white_nits;
	float source_peak_nits;
	float pad;
};

QRectF videoRect(const QSize &target, double src_w, double src_h, D3D11Renderer::Fit fit, float zoom_factor)
{
	const double tw = target.width(), th = target.height();
	switch (fit) {
	case D3D11Renderer::Fit::Stretch:
		return QRectF(0, 0, tw, th);
	case D3D11Renderer::Fit::Zoom: {
		// Same rule as the Metal renderer: 0 = fit, 1 (or -1 from older
		// builds) = fill, above 1 keeps going.
		const double fit_s = std::min(tw / src_w, th / src_h);
		const double fill_s = std::max(tw / src_w, th / src_h);
		const double z = zoom_factor == -1 ? 1.0 : std::max(0.0, double(zoom_factor));
		const double s = z <= 1 ? fit_s + (fill_s - fit_s) * z : fill_s * z;
		return QRectF((tw - src_w * s) / 2, (th - src_h * s) / 2, src_w * s, src_h * s);
	}
	case D3D11Renderer::Fit::Normal:
	default: {
		const double s = std::min(tw / src_w, th / src_h);
		return QRectF((tw - src_w * s) / 2, (th - src_h * s) / 2, src_w * s, src_h * s);
	}
	}
}

ComPtr<ID3DBlob> compileShader(const char *source, const char *target)
{
	ComPtr<ID3DBlob> blob, errors;
	HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main", target,
	                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
	if (FAILED(hr)) {
		qCCritical(chiakiGui) << "D3D11 renderer: shader compile failed:"
		                      << (errors ? static_cast<const char *>(errors->GetBufferPointer()) : "unknown error");
		return nullptr;
	}
	return blob;
}

} // namespace

struct D3D11Renderer::Impl
{
	HWND hwnd = nullptr;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<IDXGIFactory2> factory;
	ComPtr<IDXGISwapChain2> swapchain;
	HANDLE latency_object = nullptr;
	ComPtr<ID3D11RenderTargetView> backbuffer_rtv;
	bool tearing_supported = false;
	DXGI_FORMAT swapchain_format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bool hdr_output = false;
	QSize size;

	// Shared with ffmpeg's D3D11VA (lock callbacks) so decoding and our
	// draws never interleave on the immediate context.
	std::recursive_mutex context_mutex;
	AVBufferRef *hw_device_ctx = nullptr;

	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps_video, ps_overlay, ps_coverage;
	ComPtr<ID3D11BlendState> blend_nowrite;

	// UI coverage (see kCoverageShader).
	ComPtr<ID3D11Query> overlay_query;
	bool overlay_dirty = true;
	bool overlay_query_pending = false;
	bool overlay_has_content = true;
	unsigned overlay_drawn = 0, overlay_skipped = 0;

	// frame->screen: when the decoded picture reached us vs. the vblank that
	// showed it (DXGI frame statistics, matched by present id).
	struct PresentRecord { UINT id = 0; LONGLONG arrival = 0, submit = 0; };
	PresentRecord records[16];
	unsigned record_next = 0;
	LONGLONG qpc_freq = 1;
	LONGLONG video_arrival = 0;
	bool video_new = false;
	UINT last_stat_present = 0;
	double f2s_total = 0, f2s_max = 0, s2s_total = 0, s2s_max = 0;
	unsigned f2s_count = 0, s2s_count = 0;
	ComPtr<ID3D11SamplerState> sampler;
	ComPtr<ID3D11BlendState> blend_overlay;
	ComPtr<ID3D11RasterizerState> rasterizer;
	ComPtr<ID3D11Buffer> constants;

	// UI target (Qt Quick renders here).
	ComPtr<ID3D11Texture2D> overlay_tex;
	ComPtr<ID3D11ShaderResourceView> overlay_srv;

	// Current picture: up to three planes.
	ComPtr<ID3D11Texture2D> video_tex[3];
	ComPtr<ID3D11ShaderResourceView> video_srv[3];
	int video_w = 0, video_h = 0;
	int video_format = AV_PIX_FMT_NONE; // texture layout in use
	double video_sar = 1.0;
	bool video_valid = false;
	ShaderConstants video_constants = {};

	std::atomic<bool> vsync{true};
	std::atomic<bool> hdr_requested{false};
	bool hdr_reconfigure = false;

	// Frame-latency waiter.
	std::thread waiter;
	std::mutex waiter_mutex;
	std::condition_variable waiter_cv;
	bool slot_free = false;
	bool stop = false;
	std::function<void()> presented_callback;
	std::function<void()> refresh_callback;

	// Diary.
	std::chrono::steady_clock::time_point stats_since = std::chrono::steady_clock::now();
	unsigned presents = 0, copies = 0, uploads = 0;
	// Logged once: a lost GPU fails every frame after.
	bool present_failure_logged = false;
	double present_us_total = 0, present_us_max = 0;

	~Impl()
	{
		{
			std::lock_guard<std::mutex> lock(waiter_mutex);
			stop = true;
		}
		waiter_cv.notify_all();
		if (waiter.joinable())
			waiter.join();
		if (latency_object)
			CloseHandle(latency_object);
		av_buffer_unref(&hw_device_ctx);
	}

	void waiterLoop()
	{
		for (;;) {
			{
				std::lock_guard<std::mutex> lock(waiter_mutex);
				if (stop)
					return;
			}
			const DWORD r = WaitForSingleObjectEx(latency_object, 100, FALSE);
			if (r != WAIT_OBJECT_0)
				continue;
			std::function<void()> cb;
			{
				std::unique_lock<std::mutex> lock(waiter_mutex);
				slot_free = true;
				cb = presented_callback;
			}
			if (cb)
				cb();
			// Wait until a present spends the slot before waiting on the
			// object again (each signal is one frame of headroom).
			std::unique_lock<std::mutex> lock(waiter_mutex);
			waiter_cv.wait(lock, [this] { return !slot_free || stop; });
			if (stop)
				return;
		}
	}

	bool createDevice()
	{
		UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL got = {};
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
		                               D3D11_SDK_VERSION, &device, &got, &context);
		if (FAILED(hr)) {
			// No video support (old driver, remote desktop): software decode still works.
			flags &= ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
			                       D3D11_SDK_VERSION, &device, &got, &context);
		}
		if (FAILED(hr)) {
			qCWarning(chiakiGui) << "D3D11 renderer: D3D11CreateDevice failed" << Qt::hex << hr;
			return false;
		}
		ComPtr<ID3D10Multithread> mt;
		if (SUCCEEDED(device.As(&mt)))
			mt->SetMultithreadProtected(TRUE);

		ComPtr<IDXGIDevice> dxgi_device;
		ComPtr<IDXGIAdapter> adapter;
		if (FAILED(device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter))
		    || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
			return false;
		DXGI_ADAPTER_DESC desc = {};
		adapter->GetDesc(&desc);
		ComPtr<IDXGIFactory5> factory5;
		BOOL tearing = FALSE;
		if (SUCCEEDED(factory.As(&factory5)))
			factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
		tearing_supported = tearing;
		// Ask the GPU scheduler to favour us; above 0 may need privileges,
		// so the diary says what stuck.
		INT gpu_priority = 0;
		for (INT p = 7; p > 0; p--) {
			if (SUCCEEDED(dxgi_device->SetGPUThreadPriority(p))) {
				gpu_priority = p;
				break;
			}
		}
		qCInfo(chiakiGui) << "D3D11 renderer: GPU thread priority" << gpu_priority;
		qCInfo(chiakiGui).nospace() << "D3D11 renderer: " << QString::fromWCharArray(desc.Description)
		                            << ", feature level " << Qt::hex << int(got) << Qt::dec
		                            << ", tearing " << (tearing_supported ? "supported" : "unsupported")
		                            << ", video " << ((flags & D3D11_CREATE_DEVICE_VIDEO_SUPPORT) ? "yes" : "no");
		return true;
	}

	bool createHwDeviceCtx()
	{
		AVBufferRef *ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
		if (!ref)
			return false;
		auto *hw = reinterpret_cast<AVD3D11VADeviceContext *>(reinterpret_cast<AVHWDeviceContext *>(ref->data)->hwctx);
		device->AddRef();
		hw->device = device.Get();
		hw->lock_ctx = this;
		hw->lock = [](void *ctx) { static_cast<Impl *>(ctx)->context_mutex.lock(); };
		hw->unlock = [](void *ctx) { static_cast<Impl *>(ctx)->context_mutex.unlock(); };
		if (av_hwdevice_ctx_init(ref) < 0) {
			av_buffer_unref(&ref);
			return false;
		}
		hw_device_ctx = ref;
		return true;
	}

	bool createPipeline()
	{
		ComPtr<ID3DBlob> vsb = compileShader(kVertexShader, "vs_5_0");
		ComPtr<ID3DBlob> psv = compileShader(kVideoShader, "ps_5_0");
		ComPtr<ID3DBlob> pso = compileShader(kOverlayShader, "ps_5_0");
		ComPtr<ID3DBlob> psc = compileShader(kCoverageShader, "ps_5_0");
		if (!vsb || !psv || !pso || !psc)
			return false;
		if (FAILED(device->CreatePixelShader(psc->GetBufferPointer(), psc->GetBufferSize(), nullptr, &ps_coverage)))
			return false;
		D3D11_BLEND_DESC nw = {};
		nw.RenderTarget[0].RenderTargetWriteMask = 0;
		D3D11_QUERY_DESC qd = { D3D11_QUERY_OCCLUSION, 0 };
		if (FAILED(device->CreateBlendState(&nw, &blend_nowrite)) || FAILED(device->CreateQuery(&qd, &overlay_query)))
			return false;
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		qpc_freq = f.QuadPart;
		if (FAILED(device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs))
		    || FAILED(device->CreatePixelShader(psv->GetBufferPointer(), psv->GetBufferSize(), nullptr, &ps_video))
		    || FAILED(device->CreatePixelShader(pso->GetBufferPointer(), pso->GetBufferSize(), nullptr, &ps_overlay)))
			return false;

		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&sd, &sampler)))
			return false;

		D3D11_BLEND_DESC bd = {};
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&bd, &blend_overlay)))
			return false;

		D3D11_RASTERIZER_DESC rd = {};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rd, &rasterizer)))
			return false;

		D3D11_BUFFER_DESC cb = {};
		cb.ByteWidth = sizeof(ShaderConstants);
		cb.Usage = D3D11_USAGE_DEFAULT;
		cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		return SUCCEEDED(device->CreateBuffer(&cb, nullptr, &constants));
	}

	bool displayHasHdr()
	{
		if (!swapchain)
			return false;
		ComPtr<IDXGIOutput> output;
		ComPtr<IDXGIOutput6> output6;
		DXGI_OUTPUT_DESC1 desc = {};
		if (FAILED(swapchain->GetContainingOutput(&output)) || FAILED(output.As(&output6))
		    || FAILED(output6->GetDesc1(&desc)))
			return false;
		return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	}

	UINT swapchainFlags() const
	{
		return DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
		       | (tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
	}

	bool createSwapchain(const QSize &pixel_size)
	{
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = pixel_size.width();
		desc.Height = pixel_size.height();
		desc.Format = swapchain_format;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 3;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		desc.Flags = swapchainFlags();
		ComPtr<IDXGISwapChain1> sc1;
		HRESULT hr = factory->CreateSwapChainForHwnd(device.Get(), hwnd, &desc, nullptr, nullptr, &sc1);
		if (FAILED(hr) || FAILED(sc1.As(&swapchain))) {
			qCWarning(chiakiGui) << "D3D11 renderer: CreateSwapChainForHwnd failed" << Qt::hex << hr;
			return false;
		}
		// Qt owns fullscreen (borderless); DXGI must not switch modes on Alt+Enter.
		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
		swapchain->SetMaximumFrameLatency(1);
		latency_object = swapchain->GetFrameLatencyWaitableObject();
		waiter = std::thread([this] { waiterLoop(); });
		return true;
	}

	void applyColorSpace()
	{
		ComPtr<IDXGISwapChain3> sc3;
		if (FAILED(swapchain.As(&sc3)))
			return;
		const DXGI_COLOR_SPACE_TYPE cs = hdr_output ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
		                                            : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		UINT support = 0;
		if (SUCCEEDED(sc3->CheckColorSpaceSupport(cs, &support))
		    && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
			sc3->SetColorSpace1(cs);
		if (hdr_output) {
			ComPtr<IDXGISwapChain4> sc4;
			if (SUCCEEDED(swapchain.As(&sc4))) {
				// BT.2020 primaries, D65, 1000 nits mastering: what the PS5 sends.
				DXGI_HDR_METADATA_HDR10 md = {};
				md.RedPrimary[0] = 35400; md.RedPrimary[1] = 14600;
				md.GreenPrimary[0] = 8500; md.GreenPrimary[1] = 39850;
				md.BluePrimary[0] = 6550; md.BluePrimary[1] = 2300;
				md.WhitePoint[0] = 15635; md.WhitePoint[1] = 16450;
				md.MaxMasteringLuminance = 1000;
				md.MinMasteringLuminance = 1;
				md.MaxContentLightLevel = 1000;
				md.MaxFrameAverageLightLevel = 400;
				sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(md), &md);
			}
		}
	}

	bool createBackbufferView()
	{
		ComPtr<ID3D11Texture2D> back;
		if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&back))))
			return false;
		return SUCCEEDED(device->CreateRenderTargetView(back.Get(), nullptr, &backbuffer_rtv));
	}

	// Rebuilds the buffers for a new size or format (HDR on/off).
	bool configure(const QSize &pixel_size)
	{
		const bool want_hdr = hdr_requested.load() && displayHasHdr();
		const DXGI_FORMAT want_format = want_hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
		if (swapchain && pixel_size == size && want_format == swapchain_format)
			return true;
		std::lock_guard<std::recursive_mutex> lock(context_mutex);
		backbuffer_rtv.Reset();
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->Flush();
		swapchain_format = want_format;
		hdr_output = want_hdr;
		if (!swapchain) {
			if (!createSwapchain(pixel_size))
				return false;
		} else {
			HRESULT hr = swapchain->ResizeBuffers(0, pixel_size.width(), pixel_size.height(), swapchain_format, swapchainFlags());
			if (FAILED(hr)) {
				qCWarning(chiakiGui) << "D3D11 renderer: ResizeBuffers failed" << Qt::hex << hr;
				return false;
			}
		}
		applyColorSpace();
		size = pixel_size;
		qCInfo(chiakiGui).nospace() << "D3D11 renderer: swapchain " << pixel_size.width() << "x" << pixel_size.height()
		                            << (hdr_output ? " HDR10 (PQ, BT.2020)" : " SDR")
		                            << (hdr_requested.load() && !hdr_output ? " (HDR asked, display has HDR off)" : "");
		return createBackbufferView();
	}

	bool createOverlay(const QSize &pixel_size)
	{
		overlay_srv.Reset();
		overlay_tex.Reset();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = pixel_size.width();
		td.Height = pixel_size.height();
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&td, nullptr, &overlay_tex))
		    || FAILED(device->CreateShaderResourceView(overlay_tex.Get(), nullptr, &overlay_srv))) {
			overlay_tex.Reset();
			return false;
		}
		overlay_dirty = true;
		return true;
	}

	// Video planes laid out for this format and size (recreated on change).
	bool ensureVideoTextures(int format, int w, int h, bool hardware)
	{
		if (video_format == format && video_w == w && video_h == h && video_tex[0])
			return true;
		for (int i = 0; i < 3; i++) {
			video_srv[i].Reset();
			video_tex[i].Reset();
		}
		video_format = AV_PIX_FMT_NONE;
		const bool ten_bit = format == AV_PIX_FMT_P010LE || format == AV_PIX_FMT_YUV420P10LE;
		const DXGI_FORMAT plane_y = ten_bit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
		const DXGI_FORMAT plane_uv = ten_bit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;

		auto makeTex = [&](ComPtr<ID3D11Texture2D> &tex, DXGI_FORMAT fmt, int tw, int th) {
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = tw;
			td.Height = th;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = fmt;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			return SUCCEEDED(device->CreateTexture2D(&td, nullptr, &tex));
		};
		auto makeSrv = [&](ID3D11Texture2D *tex, DXGI_FORMAT fmt, ComPtr<ID3D11ShaderResourceView> &srv) {
			D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
			sd.Format = fmt;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			return SUCCEEDED(device->CreateShaderResourceView(tex, &sd, &srv));
		};

		bool ok;
		if (hardware) {
			// One NV12/P010 texture, the copy target of the decoder's slice;
			// sampled through a luma view and a chroma view.
			const DXGI_FORMAT whole = ten_bit ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
			ok = makeTex(video_tex[0], whole, w, h)
			     && makeSrv(video_tex[0].Get(), plane_y, video_srv[0])
			     && makeSrv(video_tex[0].Get(), plane_uv, video_srv[1]);
		} else if (format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_P010LE) {
			ok = makeTex(video_tex[0], plane_y, w, h) && makeSrv(video_tex[0].Get(), plane_y, video_srv[0])
			     && makeTex(video_tex[1], plane_uv, w / 2, h / 2) && makeSrv(video_tex[1].Get(), plane_uv, video_srv[1]);
		} else {
			ok = makeTex(video_tex[0], plane_y, w, h) && makeSrv(video_tex[0].Get(), plane_y, video_srv[0])
			     && makeTex(video_tex[1], plane_y, w / 2, h / 2) && makeSrv(video_tex[1].Get(), plane_y, video_srv[1])
			     && makeTex(video_tex[2], plane_y, w / 2, h / 2) && makeSrv(video_tex[2].Get(), plane_y, video_srv[2]);
		}
		if (!ok) {
			const char *name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
			qCWarning(chiakiGui) << "D3D11 renderer: cannot create video textures for" << (name ? name : "?") << w << "x" << h;
			for (int i = 0; i < 3; i++) {
				video_srv[i].Reset();
				video_tex[i].Reset();
			}
			return false;
		}
		video_format = format;
		video_w = w;
		video_h = h;
		return true;
	}

	void updateColor(const AVFrame *frame, int sw_format)
	{
		// Matrix: BT.2020 when tagged, else BT.709 (the PS5 sends 709 in SDR).
		double kr = 0.2126, kb = 0.0722;
		if (frame->colorspace == AVCOL_SPC_BT2020_NCL || frame->colorspace == AVCOL_SPC_BT2020_CL) {
			kr = 0.2627;
			kb = 0.0593;
		} else if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M) {
			kr = 0.299;
			kb = 0.114;
		}
		const double kg = 1.0 - kr - kb;
		const bool full = frame->color_range == AVCOL_RANGE_JPEG || sw_format == AV_PIX_FMT_YUVJ420P;
		const double ys = full ? 1.0 : 255.0 / 219.0, yo = full ? 0.0 : 16.0 / 255.0;
		const double cs = full ? 1.0 : 255.0 / 224.0, co = 128.0 / 255.0;
		// rgb = M * (yuv - offsets), folded into rows with the offset in w.
		const double m[3][3] = {
			{ ys, 0.0, 2.0 * (1.0 - kr) * cs },
			{ ys, -2.0 * (1.0 - kb) * kb / kg * cs, -2.0 * (1.0 - kr) * kr / kg * cs },
			{ ys, 2.0 * (1.0 - kb) * cs, 0.0 },
		};
		float *rows[3] = { video_constants.row0, video_constants.row1, video_constants.row2 };
		for (int r = 0; r < 3; r++) {
			rows[r][0] = float(m[r][0]);
			rows[r][1] = float(m[r][1]);
			rows[r][2] = float(m[r][2]);
			rows[r][3] = float(-(m[r][0] * yo + m[r][1] * co + m[r][2] * co));
		}
		// 10 bits stored low in 16 (yuv420p10) need scaling; P010 is high.
		video_constants.yscale = sw_format == AV_PIX_FMT_YUV420P10LE ? float(65535.0 / 1023.0) : 1.0f;
		video_constants.planar = (sw_format == AV_PIX_FMT_YUV420P || sw_format == AV_PIX_FMT_YUVJ420P
		                          || sw_format == AV_PIX_FMT_YUV420P10LE) ? 1 : 0;
		video_constants.transfer = frame->color_trc == AVCOL_TRC_SMPTE2084 ? 1 : 0;
		video_constants.sdr_white_nits = 203.0f;
		video_constants.panel_white_nits = 240.0f;
		video_constants.source_peak_nits = 1000.0f;
	}

	void logStats()
	{
		const auto now = std::chrono::steady_clock::now();
		const double secs = std::chrono::duration<double>(now - stats_since).count();
		if (secs < 10.0)
			return;
		// How Windows put our frames on screen: composed by DWM (one copy,
		// usually a frame later) or overlay / independent flip (direct).
		const char *mode = "unknown";
		ComPtr<IDXGISwapChainMedia> media;
		DXGI_FRAME_STATISTICS_MEDIA st = {};
		if (SUCCEEDED(swapchain.As(&media)) && SUCCEEDED(media->GetFrameStatisticsMedia(&st))) {
			switch (st.CompositionMode) {
			case DXGI_FRAME_PRESENTATION_MODE_COMPOSED: mode = "composed (DWM copy)"; break;
			case DXGI_FRAME_PRESENTATION_MODE_OVERLAY: mode = "overlay (direct)"; break;
			case DXGI_FRAME_PRESENTATION_MODE_NONE: mode = "independent flip (direct)"; break;
			case DXGI_FRAME_PRESENTATION_MODE_COMPOSITION_FAILURE: mode = "composition failure"; break;
			}
		}
		qCInfo(chiakiGui).nospace()
			<< "[d3d11] 10s: " << presents << " presents (" << QString::number(presents / secs, 'f', 1) << "/s), "
			<< copies << " GPU copies, " << uploads << " uploads, present call avg "
			<< QString::number(presents ? present_us_total / presents : 0, 'f', 0) << " us max "
			<< QString::number(present_us_max, 'f', 0) << " us, mode " << mode
			<< ", vsync " << (vsync.load() ? "on" : "off") << (hdr_output ? ", HDR10" : "");
		qCInfo(chiakiGui).nospace()
			<< "[d3d11] 10s: frame->screen avg " << QString::number(f2s_count ? f2s_total / f2s_count : 0, 'f', 1)
			<< " max " << QString::number(f2s_max, 'f', 1) << " ms (" << f2s_count << " frames), submit->screen avg "
			<< QString::number(s2s_count ? s2s_total / s2s_count : 0, 'f', 1) << " max " << QString::number(s2s_max, 'f', 1)
			<< " ms; UI pass drawn " << overlay_drawn << ", skipped (empty) " << overlay_skipped;
		stats_since = now;
		presents = copies = uploads = 0;
		f2s_total = f2s_max = s2s_total = s2s_max = 0;
		f2s_count = s2s_count = 0;
		overlay_drawn = overlay_skipped = 0;
		present_us_total = present_us_max = 0;
	}
};

D3D11Renderer::D3D11Renderer() : d(std::make_shared<Impl>()) {}
D3D11Renderer::~D3D11Renderer() = default;

std::unique_ptr<D3D11Renderer> D3D11Renderer::create(QWindow *window)
{
	std::unique_ptr<D3D11Renderer> r(new D3D11Renderer());
	r->d->hwnd = reinterpret_cast<HWND>(window->winId());
	if (!r->d->hwnd || !r->d->createDevice() || !r->d->createPipeline())
		return nullptr;
	if (!r->d->createHwDeviceCtx())
		qCWarning(chiakiGui) << "D3D11 renderer: D3D11VA device context unavailable; video decodes in software";
	return r;
}

QQuickGraphicsDevice D3D11Renderer::graphicsDevice() const
{
	return QQuickGraphicsDevice::fromDeviceAndContext(d->device.Get(), d->context.Get());
}

AVBufferRef *D3D11Renderer::hwDeviceCtx() const
{
	return d->hw_device_ctx;
}

bool D3D11Renderer::resize(const QSize &pixel_size)
{
	if (pixel_size.isEmpty())
		return false;
	if (!d->configure(pixel_size))
		return false;
	D3D11_TEXTURE2D_DESC td = {};
	if (d->overlay_tex)
		d->overlay_tex->GetDesc(&td);
	if (d->overlay_tex && int(td.Width) == pixel_size.width() && int(td.Height) == pixel_size.height())
		return false;
	return d->createOverlay(pixel_size);
}

bool D3D11Renderer::hasOverlay() const
{
	return d->overlay_tex != nullptr;
}

QQuickRenderTarget D3D11Renderer::overlayRenderTarget() const
{
	return QQuickRenderTarget::fromD3D11Texture(d->overlay_tex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, d->size);
}

QSize D3D11Renderer::pixelSize() const
{
	return d->size;
}

void D3D11Renderer::setVSync(bool enabled)
{
	d->vsync.store(enabled);
}

void D3D11Renderer::setSourceFrameRate(double)
{
	// The waitable object paces presents; nothing to tune per source rate yet.
}

void D3D11Renderer::setRefreshCallback(std::function<void()> callback)
{
	d->refresh_callback = std::move(callback);
}

void D3D11Renderer::setHdr(bool enabled)
{
	if (d->hdr_requested.exchange(enabled) != enabled)
		d->hdr_reconfigure = true;
}

void D3D11Renderer::setHiddenTop(int)
{
}

void D3D11Renderer::setHoldLimit(int)
{
}

bool D3D11Renderer::setVideoFrame(AVFrame *frame)
{
	if (!frame)
		return false;
	bool ok = false;
	std::lock_guard<std::recursive_mutex> lock(d->context_mutex);
	if (frame->format == AV_PIX_FMT_D3D11 && frame->hw_frames_ctx) {
		const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
		const int sw = fc->sw_format;
		auto *src = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
		const UINT slice = static_cast<UINT>(reinterpret_cast<intptr_t>(frame->data[1]));
		if (src && (sw == AV_PIX_FMT_NV12 || sw == AV_PIX_FMT_P010LE)
		    && d->ensureVideoTextures(sw, frame->width & ~1, frame->height & ~1, true)) {
			D3D11_BOX box = { 0, 0, 0, UINT(d->video_w), UINT(d->video_h), 1 };
			d->context->CopySubresourceRegion(d->video_tex[0].Get(), 0, 0, 0, 0, src, slice, &box);
			d->updateColor(frame, sw);
			d->video_constants.planar = 0;
			d->copies++;
			ok = true;
		}
	} else if (!frame->hw_frames_ctx) {
		const int f = frame->format;
		if ((f == AV_PIX_FMT_YUV420P || f == AV_PIX_FMT_YUVJ420P || f == AV_PIX_FMT_YUV420P10LE
		     || f == AV_PIX_FMT_NV12 || f == AV_PIX_FMT_P010LE)
		    && d->ensureVideoTextures(f, frame->width & ~1, frame->height & ~1, false)) {
			const int planes = (f == AV_PIX_FMT_NV12 || f == AV_PIX_FMT_P010LE) ? 2 : 3;
			for (int i = 0; i < planes; i++)
				d->context->UpdateSubresource(d->video_tex[i].Get(), 0, nullptr, frame->data[i], frame->linesize[i], 0);
			d->updateColor(frame, f);
			d->uploads++;
			ok = true;
		}
	}
	if (ok) {
		d->video_sar = (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0)
		               ? av_q2d(frame->sample_aspect_ratio) : 1.0;
		d->video_valid = true;
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		d->video_arrival = now.QuadPart;
		d->video_new = true;
	}
	av_frame_free(&frame);
	return ok;
}

void D3D11Renderer::clearVideo()
{
	d->video_valid = false;
}

bool D3D11Renderer::hasVideoFrame() const
{
	return d->video_valid;
}

bool D3D11Renderer::presentSlotFree(bool)
{
	std::lock_guard<std::mutex> lock(d->waiter_mutex);
	return d->slot_free;
}

void D3D11Renderer::setPresentedCallback(std::function<void()> callback)
{
	std::lock_guard<std::mutex> lock(d->waiter_mutex);
	d->presented_callback = std::move(callback);
}

bool D3D11Renderer::render(bool draw_video, bool draw_overlay, Fit fit, float zoom_factor)
{
	if (d->hdr_reconfigure && !d->size.isEmpty()) {
		d->hdr_reconfigure = false;
		const QSize s = d->size;
		d->size = QSize();
		d->configure(s);
	}
	if (!d->swapchain || !d->backbuffer_rtv)
		return false;

	std::unique_lock<std::recursive_mutex> lock(d->context_mutex);
	ID3D11DeviceContext *ctx = d->context.Get();
	ID3D11RenderTargetView *rtv = d->backbuffer_rtv.Get();
	const float black[4] = { 0, 0, 0, 1 };
	ctx->OMSetRenderTargets(1, &rtv, nullptr);
	ctx->ClearRenderTargetView(rtv, black);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->IASetInputLayout(nullptr);
	ctx->VSSetShader(d->vs.Get(), nullptr, 0);
	ctx->RSSetState(d->rasterizer.Get());
	ID3D11SamplerState *sampler = d->sampler.Get();
	ctx->PSSetSamplers(0, 1, &sampler);
	ID3D11Buffer *cb = d->constants.Get();
	ctx->PSSetConstantBuffers(0, 1, &cb);
	ctx->OMSetDepthStencilState(nullptr, 0);

	if (draw_video && d->video_valid) {
		const QRectF r = videoRect(d->size, d->video_w * d->video_sar, d->video_h, fit, zoom_factor);
		D3D11_VIEWPORT vp = { float(r.x()), float(r.y()), float(r.width()), float(r.height()), 0, 1 };
		ctx->RSSetViewports(1, &vp);
		d->video_constants.output = d->hdr_output ? 1 : 0;
		ctx->UpdateSubresource(cb, 0, nullptr, &d->video_constants, 0, 0);
		ID3D11ShaderResourceView *srvs[3] = { d->video_srv[0].Get(), d->video_srv[1].Get(), d->video_srv[2].Get() };
		ctx->PSSetShaderResources(0, 3, srvs);
		ctx->PSSetShader(d->ps_video.Get(), nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->Draw(3, 0);
	}

	if (d->overlay_query_pending) {
		UINT64 samples = 0;
		if (ctx->GetData(d->overlay_query.Get(), &samples, sizeof(samples), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
			d->overlay_has_content = samples > 0;
			d->overlay_query_pending = false;
		}
	}
	if (d->overlay_dirty && d->overlay_srv) {
		// Count the UI's covered pixels (no colour written); until the
		// answer comes back the UI is drawn, to be safe.
		d->overlay_dirty = false;
		D3D11_VIEWPORT vp = { 0, 0, float(d->size.width()), float(d->size.height()), 0, 1 };
		ctx->RSSetViewports(1, &vp);
		ID3D11ShaderResourceView *srv = d->overlay_srv.Get();
		ctx->PSSetShaderResources(0, 1, &srv);
		ctx->PSSetShader(d->ps_coverage.Get(), nullptr, 0);
		ctx->OMSetBlendState(d->blend_nowrite.Get(), nullptr, 0xffffffff);
		ctx->Begin(d->overlay_query.Get());
		ctx->Draw(3, 0);
		ctx->End(d->overlay_query.Get());
		d->overlay_query_pending = true;
		d->overlay_has_content = true;
	}
	if (draw_overlay && d->overlay_srv && !d->overlay_has_content)
		d->overlay_skipped++;
	if (draw_overlay && d->overlay_srv && d->overlay_has_content) {
		d->overlay_drawn++;
		D3D11_VIEWPORT vp = { 0, 0, float(d->size.width()), float(d->size.height()), 0, 1 };
		ctx->RSSetViewports(1, &vp);
		if (!(draw_video && d->video_valid)) {
			d->video_constants.output = d->hdr_output ? 1 : 0;
			ctx->UpdateSubresource(cb, 0, nullptr, &d->video_constants, 0, 0);
		}
		ID3D11ShaderResourceView *srv = d->overlay_srv.Get();
		ctx->PSSetShaderResources(0, 1, &srv);
		ctx->PSSetShader(d->ps_overlay.Get(), nullptr, 0);
		ctx->OMSetBlendState(d->blend_overlay.Get(), nullptr, 0xffffffff);
		ctx->Draw(3, 0);
	}

	// Leave nothing bound that Qt renders into next.
	ID3D11ShaderResourceView *none[3] = {};
	ctx->PSSetShaderResources(0, 3, none);
	ctx->OMSetRenderTargets(0, nullptr, nullptr);

	const bool vsync = d->vsync.load();
	const UINT flags = (!vsync && d->tearing_supported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
	const bool showed_new_video = draw_video && d->video_valid && d->video_new;
	d->video_new = false;
	LARGE_INTEGER submit;
	QueryPerformanceCounter(&submit);
	const auto t0 = std::chrono::steady_clock::now();
	HRESULT hr = d->swapchain->Present(vsync ? 1 : 0, flags);
	const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
	lock.unlock();

	{
		std::lock_guard<std::mutex> wl(d->waiter_mutex);
		d->slot_free = false;
	}
	d->waiter_cv.notify_all();

	if (FAILED(hr)) {
		if (!d->present_failure_logged) {
			d->present_failure_logged = true;
			// Device removed: the driver reset the GPU (a hang or a driver
			// crash, often while decoding); the reason tells which.
			const HRESULT reason = (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
			                       ? d->device->GetDeviceRemovedReason() : S_OK;
			qCWarning(chiakiGui) << "D3D11 renderer: Present failed" << Qt::hex << hr
			                     << (hr == DXGI_ERROR_DEVICE_REMOVED ? "(device removed, reason" : "(reason") << reason << ")";
		}
		return false;
	}
	d->presents++;
	d->present_us_total += us;
	d->present_us_max = std::max(d->present_us_max, us);

	UINT present_id = 0;
	if (SUCCEEDED(d->swapchain->GetLastPresentCount(&present_id))) {
		Impl::PresentRecord &r = d->records[d->record_next++ % std::size(d->records)];
		r.id = present_id;
		r.submit = submit.QuadPart;
		r.arrival = showed_new_video ? d->video_arrival : 0;
	}
	DXGI_FRAME_STATISTICS st = {};
	if (SUCCEEDED(d->swapchain->GetFrameStatistics(&st)) && st.PresentCount != d->last_stat_present) {
		d->last_stat_present = st.PresentCount;
		for (const Impl::PresentRecord &r : d->records) {
			if (r.id != st.PresentCount || !r.submit)
				continue;
			const double ms = 1000.0 / double(d->qpc_freq);
			const double s2s = double(st.SyncQPCTime.QuadPart - r.submit) * ms;
			if (s2s >= 0 && s2s < 1000) {
				d->s2s_total += s2s;
				d->s2s_max = std::max(d->s2s_max, s2s);
				d->s2s_count++;
			}
			if (r.arrival) {
				const double f2s = double(st.SyncQPCTime.QuadPart - r.arrival) * ms;
				if (f2s >= 0 && f2s < 1000) {
					d->f2s_total += f2s;
					d->f2s_max = std::max(d->f2s_max, f2s);
					d->f2s_count++;
				}
			}
			break;
		}
	}
	d->logStats();
	return true;
}

void D3D11Renderer::markOverlayDirty()
{
	d->overlay_dirty = true;
}

void D3D11Renderer::waitIdle()
{
	std::lock_guard<std::recursive_mutex> lock(d->context_mutex);
	D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
	ComPtr<ID3D11Query> q;
	if (FAILED(d->device->CreateQuery(&qd, &q)))
		return;
	d->context->End(q.Get());
	d->context->Flush();
	BOOL done = FALSE;
	for (int i = 0; i < 200; i++) {
		if (d->context->GetData(q.Get(), &done, sizeof(done), 0) == S_OK && done)
			return;
		Sleep(1);
	}
}
