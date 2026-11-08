/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Lifecycle.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lumina {
namespace fx {

namespace {
RectF
MakeRect(double l, double t, double r, double b)
{
	RectF rect;
	rect.left = float(std::round(l));
	rect.top = float(std::round(t));
	rect.right = float(std::round(r));
	rect.bottom = float(std::round(b));
	return rect;
}
}	// namespace

const char*
LifecycleKindName(LifecycleKind kind)
{
	switch (kind) {
		case LifecycleKind::Hide:		return "hide";
		case LifecycleKind::Close:		return "close";
		case LifecycleKind::Minimize:	return "minimize";
		case LifecycleKind::Restore:	return "restore";
	}
	return "?";
}


const char*
DecisionName(Decision decision)
{
	switch (decision) {
		case Decision::Animate:			return "animate";
		case Decision::Disabled:		return "disabled";
		case Decision::KindDisabled:	return "kind-disabled";
		case Decision::NotEligible:		return "not-eligible";
		case Decision::Obscured:		return "obscured";
		case Decision::TooLarge:		return "too-large";
		case Decision::NoProxy:			return "no-proxy";
	}
	return "?";
}


void
LifecycleSettings::Apply(const Value& object)
{
	if (!object.IsObject())
		return;
	if (const Value* v = object.Find("enabled"))
		enabled = v->AsBool(enabled);
	if (const Value* v = object.Find("close"))
		close = v->AsBool(close);
	if (const Value* v = object.Find("minimize"))
		minimize = v->AsBool(minimize);
	if (const Value* v = object.Find("restore"))
		restore = v->AsBool(restore);
	if (const Value* v = object.Find("fade"))
		fade = v->AsBool(fade);
	if (const Value* v = object.Find("durationMs")) {
		durationSeconds = float(std::min(std::max(v->AsNumber(200), 0.0),
			2000.0) / 1000.0);
	}
	if (const Value* v = object.Find("closeScale"))
		closeScale = float(std::min(std::max(v->AsNumber(0.9), 0.3), 1.0));
	if (const Value* v = object.Find("minVisible"))
		minVisible = float(std::min(std::max(v->AsNumber(0.8), 0.0), 1.0));
	if (const Value* v = object.Find("maxPixels"))
		maxPixels = int(std::max<int64_t>(v->AsInt(maxPixels), 0));
	if (const Value* v = object.Find("cacheBudgetMB"))
		cacheBudgetMB = int(std::min<int64_t>(std::max<int64_t>(
			v->AsInt(48), 0), 512));
	if (const Value* v = object.Find("proxyTimeoutMs")) {
		proxyTimeout = float(std::min(std::max(v->AsNumber(1500), 200.0),
			10000.0) / 1000.0);
	}
}


Decision
Decide(const LifecycleSettings& settings, LifecycleKind kind,
	const WindowTraits& traits, bool proxyAvailable)
{
	if (!settings.enabled)
		return Decision::Disabled;
	bool kindOn = false;
	switch (kind) {
		case LifecycleKind::Hide:
		case LifecycleKind::Close:		kindOn = settings.close; break;
		case LifecycleKind::Minimize:	kindOn = settings.minimize; break;
		case LifecycleKind::Restore:	kindOn = settings.restore; break;
	}
	if (!kindOn)
		return Decision::KindDisabled;
	if (!traits.normalFeel || !traits.bordered || traits.desktopShell
		|| traits.inStack || !traits.onCurrentWorkspace
		|| traits.width <= 0 || traits.height <= 0)
		return Decision::NotEligible;
	if (int64_t(traits.width) * traits.height > settings.maxPixels)
		return Decision::TooLarge;
	// A restore draws from a cached image, so visibility doesn't matter.
	if (kind != LifecycleKind::Restore
		&& traits.visibleFraction < settings.minVisible)
		return Decision::Obscured;
	if (!proxyAvailable)
		return Decision::NoProxy;
	return Decision::Animate;
}


bool
IsValid(const RectF& r)
{
	return r.right >= r.left && r.bottom >= r.top;
}


RectF
CloseTarget(const RectF& outer, float scale)
{
	scale = std::min(std::max(scale, 0.05f), 1.0f);
	double cx = (outer.left + outer.right) / 2;
	double cy = (outer.top + outer.bottom) / 2;
	double hw = (outer.right - outer.left) * scale / 2;
	double hh = (outer.bottom - outer.top) * scale / 2;
	return MakeRect(cx - hw, cy - hh, cx + hw, cy + hh);
}


RectF
MinimizeTarget(const RectF& outer, const RectF& anchor, const RectF& screen)
{
	if (IsValid(anchor))
		return anchor;
	// No known destination: a small rect at the bottom centre, where
	// Deskbar-style panels usually are; keeps the motion meaningful.
	double w = std::max(16.0, (outer.right - outer.left) * 0.08);
	double h = std::max(10.0, (outer.bottom - outer.top) * 0.08);
	double cx = (screen.left + screen.right) / 2;
	return MakeRect(cx - w / 2, screen.bottom - h, cx + w / 2, screen.bottom);
}


double
RingDifference(const ImageView& a, const ImageView& b, int ring, int step)
{
	if (!a.IsValid() || !b.IsValid() || a.width != b.width
		|| a.height != b.height || ring <= 0)
		return 255;
	step = std::max(step, 1);
	ring = std::min(ring, std::min(a.width, a.height) / 2);
	long total = 0;
	long samples = 0;
	auto sample = [&](int x, int y) {
		const uint8_t* p = a.Pixel(x, y);
		const uint8_t* q = b.Pixel(x, y);
		total += std::abs(p[0] - q[0]) + std::abs(p[1] - q[1])
			+ std::abs(p[2] - q[2]);
		samples += 3;
	};
	for (int y = 0; y < a.height; y += step) {
		bool band = y < ring || y >= a.height - ring;
		if (band) {
			for (int x = 0; x < a.width; x += step)
				sample(x, y);
		} else {
			for (int x = 0; x < ring; x++)
				sample(x, y);
			for (int x = a.width - ring; x < a.width; x++)
				sample(x, y);
		}
	}
	return samples > 0 ? double(total) / samples : 255;
}


bool
BackgroundStillValid(const ImageView& remembered, const ImageView& live,
	int ring, double threshold)
{
	return RingDifference(remembered, live, ring) <= threshold;
}


void
CrossFadeRows(const uint8_t* from, int fromStride, const uint8_t* to,
	int toStride, uint8_t* dst, int dstStride, int width, int height,
	double t)
{
	int w8 = int(std::lround(std::min(std::max(t, 0.0), 1.0) * 256));
	int f8 = 256 - w8;
	for (int y = 0; y < height; y++) {
		const uint8_t* a = from + size_t(y) * fromStride;
		const uint8_t* b = to + size_t(y) * toStride;
		uint8_t* d = dst + size_t(y) * dstStride;
		for (int x = 0; x < width; x++) {
			d[0] = uint8_t((a[0] * f8 + b[0] * w8) >> 8);
			d[1] = uint8_t((a[1] * f8 + b[1] * w8) >> 8);
			d[2] = uint8_t((a[2] * f8 + b[2] * w8) >> 8);
			d[3] = 255;
			a += 4;
			b += 4;
			d += 4;
		}
	}
}


// #pragma mark - SnapshotCache


SnapshotCache::SnapshotCache(size_t budgetBytes)
	:
	fBudget(budgetBytes)
{
}


void
SnapshotCache::SetBudget(size_t budgetBytes)
{
	fBudget = budgetBytes;
	_Evict(0);
}


bool
SnapshotCache::Put(int32_t key, const ImageView& image)
{
	Erase(key);
	if (!image.IsValid())
		return false;
	size_t bytes = size_t(image.width) * image.height * 4;
	if (bytes > fBudget)
		return false;
	_Evict(bytes);
	fOrder.push_front(key);
	Entry& entry = fEntries[key];
	entry.image.Resize(image.width, image.height);
	Copy(image, entry.image.View());
	entry.position = fOrder.begin();
	fBytes += bytes;
	return true;
}


Image*
SnapshotCache::Get(int32_t key)
{
	auto it = fEntries.find(key);
	if (it == fEntries.end())
		return nullptr;
	fOrder.erase(it->second.position);
	fOrder.push_front(key);
	it->second.position = fOrder.begin();
	return &it->second.image;
}


void
SnapshotCache::Erase(int32_t key)
{
	auto it = fEntries.find(key);
	if (it == fEntries.end())
		return;
	fBytes -= size_t(it->second.image.Width()) * it->second.image.Height() * 4;
	fOrder.erase(it->second.position);
	fEntries.erase(it);
}


void
SnapshotCache::Clear()
{
	fEntries.clear();
	fOrder.clear();
	fBytes = 0;
}


void
SnapshotCache::_Evict(size_t needed)
{
	while (!fOrder.empty() && fBytes + needed > fBudget)
		Erase(fOrder.back());
}

}	// namespace fx
}	// namespace lumina
