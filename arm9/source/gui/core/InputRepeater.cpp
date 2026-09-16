#include <nds.h>
#include "InputRepeater.h"

void InputRepeater::Update(const InputProvider* inputProvider)
{
	_trigKeys = inputProvider->GetTriggeredKeys();
	u16 curKeys = inputProvider->GetCurrentKeys();
	_repKeys = 0;
	if (_state != STATE_IDLE)
	{
		if (_state == STATE_FIRST)
		{
			if (curKeys & _mask)
			{
				_frameCounter++;
				if (_frameCounter >= _firstFrame)
				{
					_state = STATE_NEXT;
					_frameCounter = 0;
					_repKeys = curKeys & _mask;
				}
			}
			else
				_state = STATE_IDLE;
		}
		else if (_state == STATE_NEXT)
		{
			if (curKeys & _mask)
			{
				_frameCounter++;
				if (_frameCounter >= _nextFrame)
				{
					_frameCounter = 0;
					_repKeys = curKeys & _mask;
				}
			}
			else
				_state = STATE_IDLE;
		}
	}
	else if (curKeys & _mask)
	{
		// just start the initial-delay countdown here; do NOT arm _repKeys
		// yet (that only happens once _frameCounter reaches _firstFrame,
		// same as the STATE_FIRST branch above). Arming it immediately would
		// fire a spurious repeat on the very first frame a key is seen held
		// - including a key already held over from before this repeater
		// existed (see InputProvider::PrimeCurrentState(), which only syncs
		// the InputProvider side and does nothing for the repeater; call
		// Reset() alongside it when recreating a controller mid-hold).
		_state = STATE_FIRST;
		_frameCounter = 0;
	}
}
