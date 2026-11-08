/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Window lifecycle transitions (close / hide / minimize / restore): the
 * portable policy and pixel helpers used by the Window Engine.
 *
 * Mechanism (see docs/07-advanced-integration-research.md §C/§D):
 * Desktop::HideWindow() — the funnel for BWindow::Hide(), Quit()/close,
 * Minimize() from any source (title tab, Deskbar, Twitcher, keyboard, the
 * application itself) — notifies listeners while it still holds the
 * all-window write lock. No window can repaint before that lock is
 * released, so the departing window's pixels are still on screen. The
 * engine copies them into a pre-registered proxy window and shows the
 * proxy in the same locked transaction: the user sees no gap, and the
 * proxy then animates (shrink, fly to the panel, crossfade) on its own
 * while the application finishes closing normally.
 *
 * Nothing here touches Haiku types; it is unit-tested on the host.
 */
#ifndef LUMINA_CORE_FX_LIFECYCLE_H
#define LUMINA_CORE_FX_LIFECYCLE_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>

#include <lumina/Value.h>
#include <lumina/Rules.h>
#include <lumina/fx/Image.h>

namespace lumina {
namespace fx {

enum class LifecycleKind { Hide, Close, Minimize, Restore };
const char* LifecycleKindName(LifecycleKind kind);

struct LifecycleSettings {
	bool		enabled = false;		// master switch (opt-in)
	bool		close = true;			// hide/close of a window
	bool		minimize = true;
	bool		restore = true;
	bool		fade = false;			// crossfade when a background is known
	float		durationSeconds = 0.20f;
	float		closeScale = 0.90f;		// close/hide shrink factor
	float		minVisible = 0.80f;		// skip if less of the window is visible
	int			maxPixels = 4096 * 2160;	// skip larger windows
	int			cacheBudgetMB = 48;		// restore snapshots + backgrounds
	float		proxyTimeout = 1.5f;	// failsafe: release proxies after this

	// Reads the "lifecycle" object of the engine configuration; keys that
	// are absent keep their current value.
	void Apply(const Value& object);
};

// Window traits the policy needs, captured by the engine from app_server.
struct WindowTraits {
	bool	normalFeel = false;		// B_NORMAL_WINDOW_FEEL / document
	bool	bordered = false;		// titled / document / floating look
	bool	desktopShell = false;	// Tracker desktop, Deskbar, Lumina itself
	bool	inStack = false;		// Stack & Tile group (moves as one)
	bool	onCurrentWorkspace = false;
	float	visibleFraction = 0;	// of the outer frame, 0..1
	int		width = 0;
	int		height = 0;
};

// Why a transition was or wasn't started; also counted in engine stats.
enum class Decision {
	Animate, Disabled, KindDisabled, NotEligible, Obscured, TooLarge,
	NoProxy
};
const char* DecisionName(Decision decision);

Decision Decide(const LifecycleSettings& settings, LifecycleKind kind,
	const WindowTraits& traits, bool proxyAvailable);

// Geometry of the proxy's flight.
//  close/hide: shrink about the centre by closeScale;
//  minimize:   toward `anchor` (a panel/Deskbar button) if valid, else a
//              small rect at the bottom centre of `screen`;
//  restore:    the reverse — the caller swaps from/to.
RectF CloseTarget(const RectF& outer, float scale);
RectF MinimizeTarget(const RectF& outer, const RectF& anchor,
	const RectF& screen);
bool IsValid(const RectF& r);

// Background validity for crossfades. When a window first appears the
// engine records the pixels beneath it plus a `ring`-pixel border around
// them. At close time the ring (never covered by the window) is compared
// with the live screen: if it is unchanged, the remembered background is
// very likely still what the windows below would repaint, and a true
// crossfade can be precomposed; otherwise the transition falls back to a
// shrink. Both images include the ring (size = window + 2*ring).
double RingDifference(const ImageView& a, const ImageView& b, int ring,
	int step = 3);
bool BackgroundStillValid(const ImageView& remembered, const ImageView& live,
	int ring, double threshold = 6.0);

// dst = from*(1-t) + to*t, row by row over raw 32-bit pixel memory (any
// strides, B_RGB32/B_RGBA32 byte order); alpha is forced opaque. This is
// the per-frame cost of a precomposed fade: ~1 ms for 800x600 on 2015-era
// CPUs (see bench in tests).
void CrossFadeRows(const uint8_t* from, int fromStride, const uint8_t* to,
	int toStride, uint8_t* dst, int dstStride, int width, int height,
	double t);

// Byte-budgeted LRU cache of window images keyed by token. Used for
// restore snapshots (the last image of a minimized window) and for
// remembered backgrounds.
class SnapshotCache {
public:
	explicit SnapshotCache(size_t budgetBytes = 48u << 20);

	void SetBudget(size_t budgetBytes);
	// Stores a copy of `image`; evicts least recently used entries to stay
	// within budget. Images larger than the whole budget are not stored.
	bool Put(int32_t key, const ImageView& image);
	// Returns the cached image (and marks it used), or nullptr.
	Image* Get(int32_t key);
	void Erase(int32_t key);
	void Clear();

	size_t Bytes() const { return fBytes; }
	size_t Count() const { return fEntries.size(); }
	size_t Budget() const { return fBudget; }

private:
	void _Evict(size_t needed);

	struct Entry {
		Image					image;
		std::list<int32_t>::iterator	position;
	};
	std::map<int32_t, Entry>	fEntries;
	std::list<int32_t>			fOrder;		// front = most recent
	size_t						fBytes = 0;
	size_t						fBudget;
};

}	// namespace fx
}	// namespace lumina

#endif
