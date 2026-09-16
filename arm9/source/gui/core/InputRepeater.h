#pragma once

#include "InputProvider.h"

class InputRepeater
{
	enum State
	{
		STATE_IDLE,
		STATE_FIRST,
		STATE_NEXT
	};

	u16 _trigKeys;
	u16 _repKeys;
	State _state;
	u16 _frameCounter;
	u16 _mask;
	u16 _firstFrame;
	u16 _nextFrame;
public:
	InputRepeater(u16 mask, u16 firstFrame, u16 nextFrame)
		: _trigKeys(0), _repKeys(0), _state(STATE_IDLE), _frameCounter(0), _mask(mask), _firstFrame(firstFrame), _nextFrame(nextFrame)
	{ }

	void Update(const InputProvider* inputProvider);

	// Re-arms the repeater to a clean IDLE state, with no pending trigger.
	// Call this alongside InputProvider::PrimeCurrentState() whenever a
	// controller (and its repeater) is recreated while a masked key may
	// still be physically held (video chaining, L/R/X/Y skips, etc.) -
	// otherwise the next Update() sees the held key and fires an immediate
	// repeat before any real press was ever detected.
	void Reset()
	{
		_trigKeys = 0;
		_repKeys = 0;
		_state = STATE_IDLE;
		_frameCounter = 0;
	}

	u16 GetTriggeredKeys() const { return _trigKeys | _repKeys; }

    bool Triggered(u16 mask) const
    {
        return (_trigKeys | _repKeys) & mask;
    }
};
