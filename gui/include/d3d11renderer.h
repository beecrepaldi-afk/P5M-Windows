// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_D3D11RENDERER_H
#define CHIAKI_D3D11RENDERER_H

#include <QSize>
#include <QRectF>

#include <functional>
#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
}

class QWindow;
class QQuickRenderTarget;
class QQuickGraphicsDevice;

// Native Windows presenter, the counterpart of MetalRenderer (same interface,
// so QmlMainWindow drives both the same way).
//
// One D3D11 device for everything: D3D11VA decodes on it (hwDeviceCtx), the
// decoded slice is copied GPU-to-GPU into a texture we sample, Qt Quick
// renders the UI with its D3D11 RHI into a texture we own, and both are drawn
// into a flip-model swapchain (FLIP_DISCARD, tearing allowed when VSync is
// off) with a frame-latency waitable object and a latency of one frame.
// libplacebo, its queue and swapchain are bypassed on this backend.
//
// All methods except create() and hwDeviceCtx() must be called on the render
// thread.
class D3D11Renderer
{
public:
	enum class Fit { Normal, Stretch, Zoom };

	// Called on the GUI thread with the window that will be presented to.
	static std::unique_ptr<D3D11Renderer> create(QWindow *window);
	~D3D11Renderer();

	QQuickGraphicsDevice graphicsDevice() const;
	// AV_HWDEVICE_TYPE_D3D11VA context on our device; owned by the renderer
	// (the decoder takes its own reference).
	AVBufferRef *hwDeviceCtx() const;

	// (Re)creates the swapchain buffers and the UI texture when the pixel
	// size changes. Returns true when the UI render target changed.
	bool resize(const QSize &pixel_size);
	bool hasOverlay() const;
	QQuickRenderTarget overlayRenderTarget() const;
	QSize pixelSize() const;

	void setVSync(bool enabled);
	void setSourceFrameRate(double fps);
	void setRefreshCallback(std::function<void()> callback);
	// HDR10 output (PQ, BT.2020) when the display has HDR on in Windows;
	// otherwise HDR video is tone mapped to SDR.
	void setHdr(bool enabled);
	void setHiddenTop(int pixels);
	void setHoldLimit(int pictures);

	// Takes ownership of the frame. Accepts AV_PIX_FMT_D3D11 from our device
	// and the software formats the decoder or the startup warmup can produce
	// (yuv420p, yuvj420p, yuv420p10, nv12, p010). Returns false if dropped.
	bool setVideoFrame(AVFrame *frame);
	void clearVideo();
	bool hasVideoFrame() const;

	// Frame latency of one: a present may be issued only when the swapchain's
	// waitable object says the previous one was taken. Callers keep the newest
	// picture meanwhile and render when the presented callback fires.
	bool presentSlotFree(bool count_deferred = true);
	// Called on an internal thread when a new present may be issued.
	void setPresentedCallback(std::function<void()> callback);

	// Draws video (if any) and the UI on top, and presents.
	bool render(bool draw_video, bool draw_overlay, Fit fit, float zoom_factor);

	// Qt redrew the UI: recount whether it covers anything.
	void markOverlayDirty();

	void waitIdle();

private:
	D3D11Renderer();
	struct Impl;
	std::shared_ptr<Impl> d;
};

#endif // CHIAKI_D3D11RENDERER_H
