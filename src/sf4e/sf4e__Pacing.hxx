#pragma once

#include <cstdint>

namespace sf4e {
	namespace Pacing {
		// Keeps the two PCs' clocks aligned by stretching or squeezing outer
		// frames by a few milliseconds, instead of skipping whole frames when
		// GGPO says one side is ahead. Positive debt means run slower,
		// negative means run faster. The simulation is never touched; only
		// how long a frame waits in the game's limiter.
		struct Controller {
			bool enabled = true;
			double maxStepMs = 3.0;        // most one frame may change
			double minShiftMs = 1.0;       // below this, leave the frame alone
			double deadZoneFrames = 0.75;  // no correction inside this rift
			double gain = 1.0 / 120.0;     // fraction of the excess repaid per tick
			double smoothing = 1.0 / 15.0; // rift average weight per sample
			int holdTicks = 45;            // samples ignored after a prediction stall

			double outstandingMs = 0.0;
			double riftFrames = 0.0;
			bool hasRift = false;
			int holdRemaining = 0;

			// Per match.
			uint64_t samples = 0;
			double slowedMs = 0.0;
			double spedUpMs = 0.0;
			double maxShiftMs = 0.0;
			double maxAbsRiftFrames = 0.0;
			int stallTicks = 0;

			void Reset() {
				outstandingMs = 0.0;
				riftFrames = 0.0;
				hasRift = false;
				holdRemaining = 0;
				samples = 0;
				slowedMs = spedUpMs = maxShiftMs = maxAbsRiftFrames = 0.0;
				stallTicks = 0;
			}

			// GGPO refused input at the prediction limit. The advantage pair
			// is stale for a while after, and the debt no longer matches.
			void OnPredictionStall() {
				holdRemaining = holdTicks;
				outstandingMs = 0.0;
				stallTicks++;
			}

			// Both values are "frames behind the peer" as each side sees it,
			// so half the difference is how far ahead we run. Each side
			// closes half the gap and they meet in the middle.
			void OnRiftSample(double localBehind, double remoteBehind) {
				if (holdRemaining > 0) {
					holdRemaining--;
					return;
				}
				const double rift = (remoteBehind - localBehind) * 0.5;
				riftFrames = hasRift ? riftFrames + (rift - riftFrames) * smoothing : rift;
				hasRift = true;
				samples++;
				if (Abs(riftFrames) > maxAbsRiftFrames) maxAbsRiftFrames = Abs(riftFrames);
				if (!enabled || Abs(riftFrames) <= deadZoneFrames) return;
				const double excess = riftFrames > 0.0 ? riftFrames - deadZoneFrames : riftFrames + deadZoneFrames;
				outstandingMs = Clamp(outstandingMs + excess * (1000.0 / 60.0) * gain, 2.0 * maxStepMs);
			}

			// How much to change the next frame: positive lengthens it.
			double NextShiftMs() const {
				if (!enabled || Abs(outstandingMs) < minShiftMs) return 0.0;
				return Clamp(outstandingMs, maxStepMs);
			}

			// What the limiter really changed, signed like NextShiftMs.
			void OnShiftApplied(double ms) {
				if (ms > 0.0) slowedMs += ms;
				else spedUpMs -= ms;
				if (Abs(ms) > maxShiftMs) maxShiftMs = Abs(ms);
				outstandingMs -= ms;
			}

			static double Abs(double v) { return v < 0.0 ? -v : v; }
			static double Clamp(double v, double limit) { return v > limit ? limit : v < -limit ? -limit : v; }
		};

		// The limiter waits until one period after its previous exit. A shift
		// lengthens or shortens that period for one frame; shortening is
		// bounded by the time still left before the deadline.
		inline double ShiftedPeriodMs(double periodMs, double elapsedMs, double shiftMs) {
			if (shiftMs >= 0.0) return periodMs + shiftMs;
			double slack = periodMs - elapsedMs - 0.25;
			if (slack < 0.0) slack = 0.0;
			return periodMs - (-shiftMs < slack ? -shiftMs : slack);
		}

		// What a shifted frame really changed, from its measured length: never
		// more than asked, never the wrong sign.
		inline double AppliedShiftMs(double periodMs, double frameMs, double shiftMs) {
			double applied = shiftMs >= 0.0 ? frameMs - periodMs : periodMs - frameMs;
			const double limit = Controller::Abs(shiftMs);
			if (applied < 0.0) applied = 0.0;
			if (applied > limit) applied = limit;
			return shiftMs >= 0.0 ? applied : -applied;
		}
	}
}
