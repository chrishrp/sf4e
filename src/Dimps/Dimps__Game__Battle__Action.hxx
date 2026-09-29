#pragma once

#include <windows.h>

#include "Dimps__Game.hxx"

namespace Dimps {
	namespace Game {
		namespace Battle {
			namespace Action {
				void Locate(HMODULE peRoot);

				using Dimps::Game::GameMementoKey;

				struct Actor
				{
					typedef struct __publicMethods {
						size_t (Actor::* GetMementoSize)();
						int (Actor::* RecordToMemento)(void* memento, GameMementoKey::MementoID* id);
						int (Actor::* RestoreFromMemento)(void* memento, GameMementoKey::MementoID* id);
					} __publicMethods;

					static void Locate(HMODULE peRoot);
					static __publicMethods publicMethods;
				};
			}
		}
	}
}