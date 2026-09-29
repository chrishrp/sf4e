#pragma once

#include <windows.h>

#include "../Dimps/Dimps__Game.hxx"
#include "../Dimps/Dimps__Game__Battle__Chara.hxx"

namespace sf4e {
	namespace Game {
		namespace Battle {
			namespace Chara {
				void Install();

				// `this` is the shadow's IMementoable subobject (object +0x60).
				struct Afterimage : Dimps::Game::Battle::Chara::Afterimage {
					static void Install();

					size_t GetMementoSize();
					int RecordToMemento(void* memento, Dimps::Game::GameMementoKey::MementoID* id);
					int RestoreFromMemento(void* memento, Dimps::Game::GameMementoKey::MementoID* id);
				};
			}
		}
	}
}
