/* ResidualVM - A 3D game interpreter
 *
 * ResidualVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the AUTHORS
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef VARIABLES_H_
#define VARIABLES_H_

#include "common/hashmap.h"

#include "engines/myst3/myst3.h"

namespace Myst3 {

#define DECLARE_VAR(num, name) \
	void set##name(uint32 value) { engineSet(num, value); } \
	uint32 get##name() { return engineGet(num); }

class Variables {
public:
	Variables(Myst3Engine *vm);
	virtual ~Variables();

	uint32 get(uint16 var);
	void set(uint16 var, uint32 value);
	bool evaluate(int16 condition);
	uint32 valueOrVarValue(int16 value);

	const Common::String describeVar(uint16 var);
	const Common::String describeCondition(int16 condition);

	DECLARE_VAR(61, LocationAge)
	DECLARE_VAR(62, LocationRoom)
	DECLARE_VAR(63, LocationNode)
	DECLARE_VAR(64, BookSavedAge)
	DECLARE_VAR(65, BookSavedRoom)
	DECLARE_VAR(66, BookSavedNode)
	DECLARE_VAR(67, MenuSavedAge)
	DECLARE_VAR(68, MenuSavedRoom)
	DECLARE_VAR(69, MenuSavedNode)

	DECLARE_VAR(115, SunspotIntensity)
	DECLARE_VAR(116, SunspotColor)
	DECLARE_VAR(117, SunspotRadius)

	DECLARE_VAR(142, MovieStartFrame)
	DECLARE_VAR(143, MovieEndFrame)
	DECLARE_VAR(149, MovieConditionBit)
	DECLARE_VAR(150, MoviePreloadToMemory)
	DECLARE_VAR(151, MovieScriptDriven)
	DECLARE_VAR(152, MovieNextFrameSetVar)
	DECLARE_VAR(153, MovieNextFrameGetVar)
	DECLARE_VAR(154, MovieStartFrameVar)
	DECLARE_VAR(155, MovieEndFrameVar)
	DECLARE_VAR(160, MovieSynchronized)
	DECLARE_VAR(163, MovieOverrideCondition)
	DECLARE_VAR(164, MovieUVar)
	DECLARE_VAR(165, MovieVVar)
	DECLARE_VAR(166, MovieOverridePosition)
	DECLARE_VAR(167, MovieOverridePosU)
	DECLARE_VAR(168, MovieOverridePosV)
	DECLARE_VAR(173, MoviePlayingVar)

	DECLARE_VAR(189, LocationNextNode)
	DECLARE_VAR(190, LocationNextRoom)
	DECLARE_VAR(191, LocationNextAge)

	DECLARE_VAR(277, JournalAtrusState)
	DECLARE_VAR(279, JournalSaavedroState)
	DECLARE_VAR(280, JournalSaavedroClosed)
	DECLARE_VAR(281, JournalSaavedroOpen)
	DECLARE_VAR(282, JournalSaavedroLastPage)
	DECLARE_VAR(283, JournalSaavedroChapter)
	DECLARE_VAR(284, JournalSaavedroPageInChapter)
	DECLARE_VAR(480, BookStateTomahna)
	DECLARE_VAR(481, BookStateReleeshahn)

	DECLARE_VAR(1339, MenuLoadBack)
	DECLARE_VAR(1340, MenuSaveBack)
	DECLARE_VAR(1341, MenuSaveAction)
	DECLARE_VAR(1342, MenuOptionsBack)

private:
	Myst3Engine *_vm;

	uint32 _vars[2048];

	struct Description {
		Description() {}
		Description(uint16 v, const char *n, bool u) : var(v), name(n), unknown(u) {}

		uint16 var;
		const char *name;
		bool unknown;
	};

	Common::HashMap<uint16, Description> _descriptions;

	void checkRange(uint16 var);

	uint32 engineGet(uint16 var);
	void engineSet(uint16 var, uint32 value);
};

} /* namespace Myst3 */
#endif /* VARIABLES_H_ */