/* ResidualVM - A 3D game interpreter
 *
 * ResidualVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
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

#define FORBIDDEN_SYMBOL_EXCEPTION_fprintf
#define FORBIDDEN_SYMBOL_EXCEPTION_fgetc
#define FORBIDDEN_SYMBOL_EXCEPTION_stderr
#define FORBIDDEN_SYMBOL_EXCEPTION_stdin

#include "common/archive.h"
#include "common/debug-channels.h"
#include "common/file.h"
#include "common/foreach.h"
#include "common/fs.h"
#include "common/config-manager.h"

#include "graphics/pixelbuffer.h"

#include "gui/error.h"
#include "gui/gui-manager.h"
#include "gui/message.h"

#include "engines/engine.h"

#include "engines/grim/md5check.h"
#include "engines/grim/md5checkdialog.h"
#include "engines/grim/debug.h"
#include "engines/grim/grim.h"
#include "engines/grim/lua.h"
#include "engines/grim/lua_v1.h"
#include "engines/grim/emi/poolsound.h"
#include "engines/grim/emi/layer.h"
#include "engines/grim/actor.h"
#include "engines/grim/movie/movie.h"
#include "engines/grim/savegame.h"
#include "engines/grim/registry.h"
#include "engines/grim/resource.h"
#include "engines/grim/localize.h"
#include "engines/grim/gfx_base.h"
#include "engines/grim/bitmap.h"
#include "engines/grim/font.h"
#include "engines/grim/primitives.h"
#include "engines/grim/objectstate.h"
#include "engines/grim/set.h"
#include "engines/grim/sound.h"
#include "engines/grim/stuffit.h"
#include "engines/grim/debugger.h"

#include "engines/grim/imuse/imuse.h"

#include "engines/grim/lua/lua.h"

namespace Grim {

GrimEngine *g_grim = NULL;
GfxBase *g_driver = NULL;
int g_imuseState = -1;

GrimEngine::GrimEngine(OSystem *syst, uint32 gameFlags, GrimGameType gameType, Common::Platform platform, Common::Language language) :
		Engine(syst), _currSet(NULL), _selectedActor(NULL), _pauseStartTime(0) {
	g_grim = this;

	_debugger = new Debugger();
	_gameType = gameType;
	_gameFlags = gameFlags;
	_gamePlatform = platform;
	_gameLanguage = language;

	if (getGameType() == GType_GRIM)
		g_registry = new Registry();
	else
		g_registry = NULL;

	g_resourceloader = NULL;
	g_localizer = NULL;
	g_movie = NULL;
	g_imuse = NULL;

	//Set default settings
	ConfMan.registerDefault("soft_renderer", false);
	ConfMan.registerDefault("engine_speed", 60);
	ConfMan.registerDefault("fullscreen", false);
	ConfMan.registerDefault("show_fps", false);
	ConfMan.registerDefault("use_arb_shaders", true);

	_showFps = ConfMan.getBool("show_fps");

	_softRenderer = true;

	_mixer->setVolumeForSoundType(Audio::Mixer::kPlainSoundType, 192);
	_mixer->setVolumeForSoundType(Audio::Mixer::kSFXSoundType, ConfMan.getInt("sfx_volume"));
	_mixer->setVolumeForSoundType(Audio::Mixer::kSpeechSoundType, ConfMan.getInt("speech_volume"));
	_mixer->setVolumeForSoundType(Audio::Mixer::kMusicSoundType, ConfMan.getInt("music_volume"));

	_currSet = NULL;
	_selectedActor = NULL;
	_controlsEnabled = new bool[KEYCODE_EXTRA_LAST];
	_controlsState = new bool[KEYCODE_EXTRA_LAST];
	for (int i = 0; i < KEYCODE_EXTRA_LAST; i++) {
		_controlsEnabled[i] = false;
		_controlsState[i] = false;
	}
	_speechMode = TextAndVoice;
	_textSpeed = 7;
	_mode = _previousMode = NormalMode;
	_flipEnable = true;
	int speed = ConfMan.getInt("engine_speed");
	if (speed <= 0 || speed > 100)
		_speedLimitMs = 1000 / 60;
	else
		_speedLimitMs = 1000 / speed;
	ConfMan.setInt("engine_speed", 1000 / _speedLimitMs);
	_listFilesIter = NULL;
	_savedState = NULL;
	_fps[0] = 0;
	_iris = new Iris();
	_buildActiveActorsList = false;

	Color c(0, 0, 0);

	_printLineDefaults.setX(0);
	_printLineDefaults.setY(100);
	_printLineDefaults.setWidth(0);
	_printLineDefaults.setHeight(0);
	_printLineDefaults.setFGColor(c);
	_printLineDefaults.setFont(NULL);
	_printLineDefaults.setJustify(TextObject::LJUSTIFY);

	_sayLineDefaults.setX(0);
	_sayLineDefaults.setY(100);
	_sayLineDefaults.setWidth(0);
	_sayLineDefaults.setHeight(0);
	_sayLineDefaults.setFGColor(c);
	_sayLineDefaults.setFont(NULL);
	_sayLineDefaults.setJustify(TextObject::CENTER);

	_blastTextDefaults.setX(0);
	_blastTextDefaults.setY(200);
	_blastTextDefaults.setWidth(0);
	_blastTextDefaults.setHeight(0);
	_blastTextDefaults.setFGColor(c);
	_blastTextDefaults.setFont(NULL);
	_blastTextDefaults.setJustify(TextObject::LJUSTIFY);

	const Common::FSNode gameDataDir(ConfMan.get("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "movies"); // Add 'movies' subdirectory for the demo
	SearchMan.addSubDirectoryMatching(gameDataDir, "credits");

	Debug::registerDebugChannels();
}

GrimEngine::~GrimEngine() {
	delete[] _controlsEnabled;
	delete[] _controlsState;

	clearPools();

	delete LuaBase::instance();
	if (g_registry) {
		g_registry->save();
		delete g_registry;
		g_registry = NULL;
	}
	delete g_movie;
	g_movie = NULL;
	delete g_imuse;
	g_imuse = NULL;
	delete g_sound;
	g_sound = NULL;
	delete g_localizer;
	g_localizer = NULL;
	delete g_resourceloader;
	g_resourceloader = NULL;
	delete g_driver;
	g_driver = NULL;
	delete _iris;
	delete _debugger;

	ConfMan.flushToDisk();
	DebugMan.clearAllDebugChannels();
}

void GrimEngine::clearPools() {
	Set::getPool().deleteObjects();
	Actor::getPool().deleteObjects();
	PrimitiveObject::getPool().deleteObjects();
	TextObject::getPool().deleteObjects();
	Bitmap::getPool().deleteObjects();
	Font::getPool().deleteObjects();
	ObjectState::getPool().deleteObjects();

	_currSet = NULL;
}

LuaBase *GrimEngine::createLua() {
	return new Lua_V1();
}

void GrimEngine::createRenderer() {
#ifdef USE_OPENGL
	_softRenderer = ConfMan.getBool("soft_renderer");
#endif

	if (!_softRenderer && !g_system->hasFeature(OSystem::kFeatureOpenGL)) {
		warning("gfx backend doesn't support hardware rendering");
		_softRenderer = true;
	}

	if (_softRenderer) {
		g_driver = CreateGfxTinyGL();
#ifdef USE_OPENGL
	} else {
		g_driver = CreateGfxOpenGL();
#endif
	}
}

const char *GrimEngine::getUpdateFilename() {
	if (!(getGameFlags() & ADGF_DEMO))
		return "gfupd101.exe";
	else
		return 0;
}

Common::Error GrimEngine::run() {
	// Try to see if we have the EMI Mac installer present
	// Currently, this requires the data fork to be standalone
	if (getGameType() == GType_MONKEY4 && SearchMan.hasFile("Monkey Island 4 Installer")) {
		StuffItArchive *archive = new StuffItArchive();

		if (archive->open("Monkey Island 4 Installer"))
			SearchMan.add("Monkey Island 4 Installer", archive, 0, true);
		else
			delete archive;
	}

	ConfMan.registerDefault("check_gamedata", true);
	if (ConfMan.getBool("check_gamedata")) {
		MD5CheckDialog d;
		if (!d.runModal()) {
			Common::String confirmString("ResidualVM found some problems with your game data files.\n"
										 "Running ResidualVM nevertheless may cause game bugs or even crashes.\n"
										 "Do you still want to run ");
			confirmString += (GType_MONKEY4 == getGameType() ? "Escape From Monkey Island?" : "Grim Fandango?");
			GUI::MessageDialog msg(confirmString, "Yes", "No");
			if (!msg.runModal()) {
				return Common::kUserCanceled;
			}
		}

		ConfMan.setBool("check_gamedata", false);
		ConfMan.flushToDisk();
	}

	g_resourceloader = new ResourceLoader();
	bool demo = getGameFlags() & ADGF_DEMO;
	if (getGameType() == GType_GRIM)
		g_movie = CreateSmushPlayer(demo);
	else if (getGameType() == GType_MONKEY4) {
		if (_gamePlatform == Common::kPlatformPS2)
			g_movie = CreateMpegPlayer();
		else
			g_movie = CreateBinkPlayer(demo);
	}
	g_imuse = new Imuse(20, demo);
	g_sound = new SoundPlayer();

	bool fullscreen = ConfMan.getBool("fullscreen");
	createRenderer();
	g_driver->setupScreen(640, 480, fullscreen);

	if (getGameType() == GType_MONKEY4 && SearchMan.hasFile("AMWI.m4b")) {
		// TODO: Play EMI Mac Aspyr logo
		warning("TODO: Play Aspyr logo");
	}

	Bitmap *splash_bm = NULL;
	if (!(_gameFlags & ADGF_DEMO) && getGameType() == GType_GRIM)
		splash_bm = Bitmap::create("splash.bm");
	else if ((_gameFlags & ADGF_DEMO) && getGameType() == GType_MONKEY4)
		splash_bm = Bitmap::create("splash.til");
	else if (getGamePlatform() == Common::kPlatformPS2 && getGameType() == GType_MONKEY4)
		splash_bm = Bitmap::create("load.tga");

	g_driver->clearScreen();

	if (splash_bm != NULL)
		splash_bm->draw();

	// This flipBuffer() may make the OpenGL renderer show garbage instead of the splash,
	// while the TinyGL renderer needs it.
	if (_softRenderer)
		g_driver->flipBuffer();

	LuaBase *lua = createLua();

	lua->registerOpcodes();
	lua->registerLua();

	//Initialize Localizer first. In system-script are already localizeable Strings
	g_localizer = new Localizer();
	lua->loadSystemScript();
	lua->boot();

	_savegameLoadRequest = false;
	_savegameSaveRequest = false;

	// Load game from specified slot, if any
	if (ConfMan.hasKey("save_slot")) {
		loadGameState(ConfMan.getInt("save_slot"));
	}

	g_grim->setMode(NormalMode);
	delete splash_bm;
	g_grim->mainLoop();

	return Common::kNoError;
}

Common::Error GrimEngine::loadGameState(int slot) {
	assert(slot >= 0);
	if (getGameType() == GType_MONKEY4) {
		_savegameFileName = Common::String::format("efmi%03d.gsv", slot);
	} else {
		_savegameFileName = Common::String::format("grim%02d.gsv", slot);
	}
	_savegameLoadRequest = true;
	return Common::kNoError;
}

void GrimEngine::handlePause() {
	if (!LuaBase::instance()->callback("pauseHandler")) {
		error("handlePause: invalid handler");
	}
}

void GrimEngine::handleExit() {
	if (!LuaBase::instance()->callback("exitHandler")) {
		error("handleExit: invalid handler");
	}
}

void GrimEngine::handleUserPaint() {
	if (!LuaBase::instance()->callback("userPaintHandler")) {
		error("handleUserPaint: invalid handler");
	}
}

void GrimEngine::cameraChangeHandle(int prev, int next) {
	LuaObjects objects;
	objects.add(prev);
	objects.add(next);
	LuaBase::instance()->callback("camChangeHandler", objects);
}

void GrimEngine::cameraPostChangeHandle(int num) {
	LuaObjects objects;
	objects.add(num);
	LuaBase::instance()->callback("postCamChangeHandler", objects);
}

void GrimEngine::savegameCallback() {
	if (!LuaBase::instance()->callback("saveGameCallback")) {
		error("GrimEngine::savegameCallback: invalid handler");
	}
}

void GrimEngine::handleDebugLoadResource() {
	void *resource = NULL;
	int c, i = 0;
	char buf[513];

	// Tool for debugging the loading of a particular resource without
	// having to actually make it all the way to it in the game
	fprintf(stderr, "Enter resource to load (extension specifies type): ");
	while (i < 512 && (c = fgetc(stdin)) != EOF && c != '\n')
		buf[i++] = c;

	buf[i] = '\0';
	if (strstr(buf, ".key"))
		resource = (void *)g_resourceloader->loadKeyframe(buf);
	else if (strstr(buf, ".zbm") || strstr(buf, ".bm"))
		resource = (void *)Bitmap::create(buf);
	else if (strstr(buf, ".cmp"))
		resource = (void *)g_resourceloader->loadColormap(buf);
	else if (strstr(buf, ".cos"))
		resource = (void *)g_resourceloader->loadCostume(buf, NULL);
	else if (strstr(buf, ".lip"))
		resource = (void *)g_resourceloader->loadLipSync(buf);
	else if (strstr(buf, ".snm"))
		resource = (void *)g_movie->play(buf, false, 0, 0);
	else if (strstr(buf, ".wav") || strstr(buf, ".imu")) {
		g_imuse->startSfx(buf);
		resource = (void *)1;
	} else if (strstr(buf, ".mat")) {
		CMap *cmap = g_resourceloader->loadColormap("item.cmp");
		warning("Default colormap applied to resources loaded in this fashion");
		resource = (void *)g_resourceloader->loadMaterial(buf, cmap);
	} else {
		warning("Resource type not understood");
	}
	if (!resource)
		warning("Requested resouce (%s) not found", buf);
}

void GrimEngine::drawTextObjects() {
	foreach (TextObject *t, TextObject::getPool()) {
		t->draw();
	}
}

void GrimEngine::drawPrimitives() {
	_iris->draw();

	// Draw text
	if (_mode == SmushMode) {
		if (_movieSubtitle) {
			_movieSubtitle->draw();
		}
	} else {
		drawTextObjects();
	}
}

void GrimEngine::playIrisAnimation(Iris::Direction dir, int x, int y, int time) {
	_iris->play(dir, x, y, time);
}

void GrimEngine::luaUpdate() {
	if (_savegameLoadRequest || _savegameSaveRequest || _changeHardwareState)
		return;

	// Update timing information
	unsigned newStart = g_system->getMillis();
	if (newStart < _frameStart) {
		_frameStart = newStart;
		return;
	}
	_frameTime = newStart - _frameStart;
	_frameStart = newStart;

	if (_mode == PauseMode || _shortFrame) {
		_frameTime = 0;
	}

	LuaBase::instance()->update(_frameTime, _movieTime);

	if (_currSet && (_mode == NormalMode || _mode == SmushMode)) {
		// call updateTalk() before calling update(), since it may modify costumes state, and
		// the costumes are updated in update().
		for (Common::List<Actor *>::iterator i = _talkingActors.begin(); i != _talkingActors.end(); ++i) {
			Actor *a = *i;
			if (!a->updateTalk(_frameTime)) {
				i = _talkingActors.reverse_erase(i);
			}
		}

		// Update the actors. Do it here so that we are sure to react asap to any change
		// in the actors state caused by lua.
		buildActiveActorsList();
		foreach (Actor *a, _activeActors) {
			// Note that the actor need not be visible to update chores, for example:
			// when Manny has just brought Meche back he is offscreen several times
			// when he needs to perform certain chores
			a->update(_frameTime);
		}

		_iris->update(_frameTime);

		foreach (TextObject *t, TextObject::getPool()) {
			t->update();
		}
	}
}

void GrimEngine::updateDisplayScene() {
	_doFlip = true;

	if (_mode == SmushMode) {
		if (g_movie->isPlaying()) {
			_movieTime = g_movie->getMovieTime();
			if (g_movie->isUpdateNeeded()) {
				g_driver->prepareMovieFrame(g_movie->getDstSurface());
				g_movie->clearUpdateNeeded();
			}
			int frame = g_movie->getFrame();
			if (frame >= 0) {
				if (frame != _prevSmushFrame) {
					_prevSmushFrame = g_movie->getFrame();
					g_driver->drawMovieFrame(g_movie->getX(), g_movie->getY());
					if (_showFps)
						g_driver->drawEmergString(550, 25, _fps, Color(255, 255, 255));
				} else
					_doFlip = false;
			} else
				g_driver->releaseMovieFrame();
		}
		// Draw Primitives
		foreach (PrimitiveObject *p, PrimitiveObject::getPool()) {
			p->draw();
		}
		drawPrimitives();
	} else if (_mode == NormalMode || _mode == OverworldMode) {
		updateNormalMode();
	} else if (_mode == DrawMode) {
		updateDrawMode();
	}
}

void GrimEngine::updateNormalMode() {
	if (!_currSet)
		return;

	g_driver->clearScreen();

	drawNormalMode();

	g_driver->drawBuffers();
	drawPrimitives();
}

void GrimEngine::updateDrawMode() {
	_doFlip = false;
	_prevSmushFrame = 0;
	_movieTime = 0;
}

void GrimEngine::drawNormalMode() {
	_prevSmushFrame = 0;
	_movieTime = 0;

	_currSet->drawBackground();

	// Draw underlying scene components
	// Background objects are drawn underneath everything except the background
	// There are a bunch of these, especially in the tube-switcher room
	_currSet->drawBitmaps(ObjectState::OBJSTATE_BACKGROUND);

	// State objects are drawn on top of other things, such as the flag
	// on Manny's message tube
	_currSet->drawBitmaps(ObjectState::OBJSTATE_STATE);

	// Play SMUSH Animations
	// This should occur on top of all underlying scene objects,
	// a good example is the tube switcher room where some state objects
	// need to render underneath the animation or you can't see what's going on
	// This should not occur on top of everything though or Manny gets covered
	// up when he's next to Glottis's service room
	if (g_movie->isPlaying() && _movieSetup == _currSet->getCurrSetup()->_name) {
		_movieTime = g_movie->getMovieTime();
		if (g_movie->isUpdateNeeded()) {
			g_driver->prepareMovieFrame(g_movie->getDstSurface());
			g_movie->clearUpdateNeeded();
		}
		if (g_movie->getFrame() >= 0)
			g_driver->drawMovieFrame(g_movie->getX(), g_movie->getY());
		else
			g_driver->releaseMovieFrame();
	}

	// Underlay objects must be drawn on top of movies
	// Otherwise the lighthouse door will always be open as the underlay for
	// the closed door will be overdrawn by a movie used as background image.
	_currSet->drawBitmaps(ObjectState::OBJSTATE_UNDERLAY);

	// Draw Primitives
	foreach (PrimitiveObject *p, PrimitiveObject::getPool()) {
		p->draw();
	}

	_currSet->setupCamera();

	g_driver->set3DMode();

	if (_setupChanged) {
		cameraPostChangeHandle(_currSet->getSetup());
		_setupChanged = false;
	}

	// Draw actors
	buildActiveActorsList();
	foreach (Actor *a, _activeActors) {
		if (a->isVisible())
			a->draw();
	}

	flagRefreshShadowMask(false);

	// Draw overlying scene components
	// The overlay objects should be drawn on top of everything else,
	// including 3D objects such as Manny and the message tube
	_currSet->drawBitmaps(ObjectState::OBJSTATE_OVERLAY);
}

void GrimEngine::doFlip() {
	_frameCounter++;
	if (!_doFlip) {
		return;
	}

	if (_showFps && _mode != DrawMode)
		g_driver->drawEmergString(550, 25, _fps, Color(255, 255, 255));

	if (_flipEnable)
		g_driver->flipBuffer();

	if (_showFps && _mode != DrawMode) {
		unsigned int currentTime = g_system->getMillis();
		unsigned int delta = currentTime - _lastFrameTime;
		if (delta > 500) {
			sprintf(_fps, "%7.2f", (double)(_frameCounter * 1000) / (double)delta);
			_frameCounter = 0;
			_lastFrameTime = currentTime;
		}
	}
}

void GrimEngine::mainLoop() {
	_movieTime = 0;
	_frameTime = 0;
	_frameStart = g_system->getMillis();
	_frameCounter = 0;
	_lastFrameTime = 0;
	_prevSmushFrame = 0;
	_refreshShadowMask = false;
	_shortFrame = false;
	bool resetShortFrame = false;
	_changeHardwareState = false;
	_changeFullscreenState = false;
	_setupChanged = true;

	for (;;) {
		uint32 startTime = g_system->getMillis();
		if (_shortFrame) {
			if (resetShortFrame) {
				_shortFrame = false;
			}
			resetShortFrame = !resetShortFrame;
		}

		if (shouldQuit())
			return;

		if (_savegameLoadRequest) {
			savegameRestore();
		}
		if (_savegameSaveRequest) {
			savegameSave();
		}

		if (_changeHardwareState || _changeFullscreenState) {
			_changeHardwareState = false;

			bool fullscreen = g_driver->isFullscreen();
			if (_changeFullscreenState) {
				fullscreen = !fullscreen;
			}
			g_system->setFeatureState(OSystem::kFeatureFullscreenMode, fullscreen);
			ConfMan.setBool("fullscreen", fullscreen);

			uint screenWidth = g_driver->getScreenWidth();
			uint screenHeight = g_driver->getScreenHeight();

			EngineMode mode = getMode();

			_savegameFileName = "";
			savegameSave();
			clearPools();

			delete g_driver;
			createRenderer();
			g_driver->setupScreen(screenWidth, screenHeight, fullscreen);
			savegameRestore();

			if (mode == DrawMode) {
				setMode(GrimEngine::NormalMode);
				updateDisplayScene();
				g_driver->storeDisplay();
				g_driver->dimScreen();
			}
			setMode(mode);
			_changeFullscreenState = false;
		}

		g_imuse->flushTracks();
		g_imuse->refreshScripts();

		_debugger->onFrame();

		// Process events
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			// Handle any buttons, keys and joystick operations
			Common::EventType type = event.type;
			if (type == Common::EVENT_KEYDOWN || type == Common::EVENT_KEYUP) {
				if (type == Common::EVENT_KEYDOWN) {
					// Allow us to disgracefully skip movies in the PS2-version:
					if (_mode == SmushMode && getGamePlatform() == Common::kPlatformPS2) {
						if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
							g_movie->stop();
							break;
						}
					} else if (_mode != DrawMode && _mode != SmushMode && (event.kbd.ascii == 'q')) {
						handleExit();
						break;
					} else if (_mode != DrawMode && (event.kbd.keycode == Common::KEYCODE_PAUSE)) {
						handlePause();
						break;
					} else {
						handleChars(type, event.kbd);
					}
				}

				handleControls(type, event.kbd);

				// Allow lua to react to the event.
				// Without this lua_update switching the entries in the menu is slow because
				// if the button is not kept pressed the KEYUP will arrive just after the KEYDOWN
				// and it will break the lua scripts that checks for the state of the button
				// with GetControlState()

				// We do not want the scripts to update while a movie is playing in the PS2-version.
				if (!(getGamePlatform() == Common::kPlatformPS2 && _mode == SmushMode)) {
					luaUpdate();
				}
			}
		}

		if (_mode != PauseMode) {
			// Draw the display scene before doing the luaUpdate.
			// This give a large performance boost as OpenGL stores commands
			// in a queue on the gpu to be rendered later. When doFlip is
			// called the cpu must wait for the gpu to finish its queue.
			// Now, it will queue all the OpenGL commands and draw them on the
			// GPU while the CPU is busy updating the game world.
			updateDisplayScene();
		}

		// We do not want the scripts to update while a movie is playing in the PS2-version.
		if (!(getGamePlatform() == Common::kPlatformPS2 && _mode == SmushMode)) {
			luaUpdate();
		}

		if (_mode != PauseMode) {
			doFlip();
		}

		if (g_imuseState != -1) {
			g_sound->setMusicState(g_imuseState);
			g_imuseState = -1;
		}

		uint32 endTime = g_system->getMillis();
		if (startTime > endTime)
			continue;
		uint32 diffTime = endTime - startTime;
		if (_speedLimitMs == 0)
			continue;
		if (diffTime < _speedLimitMs) {
			uint32 delayTime = _speedLimitMs - diffTime;
			g_system->delayMillis(delayTime);
		}
	}
}

void GrimEngine::changeHardwareState() {
	_changeHardwareState = true;
}

void GrimEngine::saveGame(const Common::String &file) {
	_savegameFileName = file;
	_savegameSaveRequest = true;
}

void GrimEngine::loadGame(const Common::String &file) {
	_savegameFileName = file;
	_savegameLoadRequest = true;
}

void GrimEngine::savegameRestore() {
	debug("GrimEngine::savegameRestore() started.");
	_savegameLoadRequest = false;
	Common::String filename;
	if (_savegameFileName.size() == 0) {
		filename = "grim.sav";
	} else {
		filename = _savegameFileName;
	}
	_savedState = SaveGame::openForLoading(filename);
	if (!_savedState || !_savedState->isCompatible())
		return;
	g_imuse->stopAllSounds();
	g_imuse->resetState();
	g_movie->stop();
	g_imuse->pause(true);
	g_movie->pause(true);
	if (g_registry)
	    g_registry->save();

	_selectedActor = NULL;
	delete _currSet;
	_currSet = NULL;

	Bitmap::getPool().restoreObjects(_savedState);
	Debug::debug(Debug::Engine, "Bitmaps restored successfully.");

	Font::getPool().restoreObjects(_savedState);
	Debug::debug(Debug::Engine, "Fonts restored successfully.");

	ObjectState::getPool().restoreObjects(_savedState);
	Debug::debug(Debug::Engine, "ObjectStates restored successfully.");

	Set::getPool().restoreObjects(_savedState);
	Debug::debug(Debug::Engine, "Sets restored successfully.");

	TextObject::getPool().restoreObjects(_savedState);
	Debug::debug(Debug::Engine, "TextObjects restored successfully.");

	PrimitiveObject::getPool().restoreObjects(_savedState);
	Debug::debug(Debug::Engine, "PrimitiveObjects restored successfully.");

	Actor::getPool().restoreObjects(_savedState);
	Debug::debug(Debug::Engine, "Actors restored successfully.");

	if (getGameType() == GType_MONKEY4) {
		PoolSound::getPool().restoreObjects(_savedState);
		Debug::debug(Debug::Engine, "Pool sounds saved successfully.");

		Layer::getPool().restoreObjects(_savedState);
		Debug::debug(Debug::Engine, "Layers restored successfully.");
	}

	restoreGRIM();
	Debug::debug(Debug::Engine, "Engine restored successfully.");

	g_driver->restoreState(_savedState);
	Debug::debug(Debug::Engine, "Renderer restored successfully.");

	g_sound->restoreState(_savedState);
	Debug::debug(Debug::Engine, "iMuse restored successfully.");

	g_movie->restoreState(_savedState);
	Debug::debug(Debug::Engine, "Movie restored successfully.");

	_iris->restoreState(_savedState);
	Debug::debug(Debug::Engine, "Iris restored successfully.");

	lua_Restore(_savedState);
	Debug::debug(Debug::Engine, "Lua restored successfully.");

	delete _savedState;

	//Re-read the values, since we may have been in some state that changed them when loading the savegame,
	//e.g. running a cutscene, which sets the sfx volume to 0.
	_mixer->setVolumeForSoundType(Audio::Mixer::kSFXSoundType, ConfMan.getInt("sfx_volume"));
	_mixer->setVolumeForSoundType(Audio::Mixer::kSpeechSoundType, ConfMan.getInt("speech_volume"));
	_mixer->setVolumeForSoundType(Audio::Mixer::kMusicSoundType, ConfMan.getInt("music_volume"));

	LuaBase::instance()->postRestoreHandle();
	g_imuse->pause(false);
	g_movie->pause(false);
	debug("GrimEngine::savegameRestore() finished.");

	_shortFrame = true;
	clearEventQueue();
	invalidateActiveActorsList();
	buildActiveActorsList();

	g_driver->refreshBuffers();
	_currSet->setupCamera();
	g_driver->set3DMode();
	foreach (Actor *a, Actor::getPool()) {
		a->restoreCleanBuffer();
	}
}

void GrimEngine::restoreGRIM() {
	_savedState->beginSection('GRIM');

	_mode = (EngineMode)_savedState->readLEUint32();
	_previousMode = (EngineMode)_savedState->readLEUint32();

	// Actor stuff
	int32 id = _savedState->readLESint32();
	if (id != 0) {
		_selectedActor = Actor::getPool().getObject(id);
	}

	//TextObject stuff
	_sayLineDefaults.setFGColor(_savedState->readColor());
	_sayLineDefaults.setFont(Font::getPool().getObject(_savedState->readLESint32()));
	_sayLineDefaults.setHeight(_savedState->readLESint32());
	_sayLineDefaults.setJustify(_savedState->readLESint32());
	_sayLineDefaults.setWidth(_savedState->readLESint32());
	_sayLineDefaults.setX(_savedState->readLESint32());
	_sayLineDefaults.setY(_savedState->readLESint32());
	_sayLineDefaults.setDuration(_savedState->readLESint32());
	if (_savedState->saveMinorVersion() > 5) {
		_movieSubtitle = TextObject::getPool().getObject(_savedState->readLESint32());
	}

	// Set stuff
	_currSet = Set::getPool().getObject(_savedState->readLESint32());
	if (_savedState->saveMinorVersion() > 4) {
		_movieSetup = _savedState->readString();
	} else {
		_movieSetup = _currSet->getCurrSetup()->_name;
	}

	_savedState->endSection();
}

void GrimEngine::storeSaveGameImage(SaveGame *state) {
	int width = 250, height = 188;
	Bitmap *screenshot;

	debug("GrimEngine::StoreSaveGameImage() started.");

	EngineMode mode = g_grim->getMode();
	g_grim->setMode(_previousMode);
	g_grim->updateDisplayScene();
	g_driver->storeDisplay();
	screenshot = g_driver->getScreenshot(width, height);
	g_grim->setMode(mode);
	state->beginSection('SIMG');
	if (screenshot) {
		int size = screenshot->getWidth() * screenshot->getHeight();
		screenshot->setActiveImage(0);
		uint16 *data = (uint16 *)screenshot->getData().getRawBuffer();
		for (int l = 0; l < size; l++) {
			state->writeLEUint16(data[l]);
		}
	} else {
		error("Unable to store screenshot");
	}
	state->endSection();
	delete screenshot;
	debug("GrimEngine::StoreSaveGameImage() finished.");
}

void GrimEngine::savegameSave() {
	debug("GrimEngine::savegameSave() started.");
	_savegameSaveRequest = false;
	Common::String filename;
	if (_savegameFileName.size() == 0) {
		filename = "grim.sav";
	} else {
		filename = _savegameFileName;
	}
	if (getGameType() == GType_MONKEY4 && filename.contains('/')) {
		filename = Common::lastPathComponent(filename, '/');
	}
	_savedState = SaveGame::openForSaving(filename);
	if (!_savedState) {
		//TODO: Translate this!
		GUI::displayErrorDialog("Error: the game could not be saved.");
		return;
	}

	storeSaveGameImage(_savedState);

	g_imuse->pause(true);
	g_movie->pause(true);

	savegameCallback();

	Bitmap::getPool().saveObjects(_savedState);
	Debug::debug(Debug::Engine, "Bitmaps saved successfully.");

	Font::getPool().saveObjects(_savedState);
	Debug::debug(Debug::Engine, "Fonts saved successfully.");

	ObjectState::getPool().saveObjects(_savedState);
	Debug::debug(Debug::Engine, "ObjectStates saved successfully.");

	Set::getPool().saveObjects(_savedState);
	Debug::debug(Debug::Engine, "Sets saved successfully.");

	TextObject::getPool().saveObjects(_savedState);
	Debug::debug(Debug::Engine, "TextObjects saved successfully.");

	PrimitiveObject::getPool().saveObjects(_savedState);
	Debug::debug(Debug::Engine, "PrimitiveObjects saved successfully.");

	Actor::getPool().saveObjects(_savedState);
	Debug::debug(Debug::Engine, "Actors saved successfully.");

	if (getGameType() == GType_MONKEY4) {
		PoolSound::getPool().saveObjects(_savedState);
		Debug::debug(Debug::Engine, "Pool sounds saved successfully.");

		Layer::getPool().saveObjects(_savedState);
		Debug::debug(Debug::Engine, "Layers saved successfully.");
	}

	saveGRIM();
	Debug::debug(Debug::Engine, "Engine saved successfully.");

	g_driver->saveState(_savedState);
	Debug::debug(Debug::Engine, "Renderer saved successfully.");

	g_sound->saveState(_savedState);
	Debug::debug(Debug::Engine, "iMuse saved successfully.");

	g_movie->saveState(_savedState);
	Debug::debug(Debug::Engine, "Movie saved successfully.");

	_iris->saveState(_savedState);
	Debug::debug(Debug::Engine, "Iris saved successfully.");

	lua_Save(_savedState);

	delete _savedState;

	g_imuse->pause(false);
	g_movie->pause(false);
	debug("GrimEngine::savegameSave() finished.");

	_shortFrame = true;
	clearEventQueue();
}

void GrimEngine::saveGRIM() {
	_savedState->beginSection('GRIM');

	_savedState->writeLEUint32((uint32)_mode);
	_savedState->writeLEUint32((uint32)_previousMode);

	//Actor stuff
	if (_selectedActor) {
		_savedState->writeLESint32(_selectedActor->getId());
	} else {
		_savedState->writeLESint32(0);
	}

	//TextObject stuff
	_savedState->writeColor(_sayLineDefaults.getFGColor());
	_savedState->writeLESint32(_sayLineDefaults.getFont()->getId());
	_savedState->writeLESint32(_sayLineDefaults.getHeight());
	_savedState->writeLESint32(_sayLineDefaults.getJustify());
	_savedState->writeLESint32(_sayLineDefaults.getWidth());
	_savedState->writeLESint32(_sayLineDefaults.getX());
	_savedState->writeLESint32(_sayLineDefaults.getY());
	_savedState->writeLESint32(_sayLineDefaults.getDuration());
	_savedState->writeLESint32(_movieSubtitle ? _movieSubtitle->getId() : 0);

	//Set stuff
	_savedState->writeLESint32(_currSet->getId());
	_savedState->writeString(_movieSetup);

	_savedState->endSection();
}

Set *GrimEngine::findSet(const Common::String &name) {
	// Find scene object
	foreach (Set *s, Set::getPool()) {
		if (s->getName() == name)
			return s;
	}
	return NULL;
}

void GrimEngine::setSetLock(const char *name, bool lockStatus) {
	Set *scene = findSet(name);

	if (!scene) {
		Debug::warning(Debug::Engine, "Set object '%s' not found in list", name);
		return;
	}
	// Change the locking status
	scene->_locked = lockStatus;
}

Set *GrimEngine::loadSet(const Common::String &name) {
	Set *s = findSet(name);

	if (!s) {
		Common::String filename(name);
		// EMI-scripts refer to their .setb files as .set
		if (g_grim->getGameType() == GType_MONKEY4) {
			filename += "b";
		}
		Common::SeekableReadStream *stream;
		stream = g_resourceloader->openNewStreamFile(filename.c_str());
		if (!stream)
			error("Could not find scene file %s", name.c_str());

		s = new Set(name, stream);
		delete stream;
	}

	return s;
}

void GrimEngine::setSet(const char *name) {
	setSet(loadSet(name));
}

void GrimEngine::setSet(Set *scene) {
	if (scene == _currSet)
		return;

	if (getGameType() == GType_MONKEY4) {
		foreach (PoolSound *s, PoolSound::getPool()) {
			s->stop();
		}
	}
	// Stop the actors. This fixes bug #289 (https://github.com/residualvm/residualvm/issues/289)
	// and it makes sense too, since when changing set the directions
	// and coords change too.
	foreach (Actor *a, Actor::getPool()) {
		a->stopWalking();
		a->clearCleanBuffer();
		a->setSortOrder(0);
	}
	g_driver->refreshBuffers();

	Set *lastSet = _currSet;
	_currSet = scene;
	_currSet->setSoundParameters(20, 127);
	// should delete the old scene after setting the new one
	if (lastSet && !lastSet->_locked) {
		delete lastSet;
	}
	_shortFrame = true;
	_setupChanged = true;
	invalidateActiveActorsList();
}

void GrimEngine::makeCurrentSetup(int num) {
	int prevSetup = g_grim->getCurrSet()->getSetup();
	if (prevSetup != num) {
		foreach (Actor *a, Actor::getPool()) {
			a->clearCleanBuffer();
		}
		g_driver->refreshBuffers();

		getCurrSet()->setSetup(num);
		getCurrSet()->setSoundParameters(20, 127);
		cameraChangeHandle(prevSetup, num);
		// here should be set sound position

		_setupChanged = true;
	}
}

void GrimEngine::setTextSpeed(int speed) {
	if (speed < 1)
		_textSpeed = 1;
	if (speed > 10)
		_textSpeed = 10;
	_textSpeed = speed;
}

float GrimEngine::getControlAxis(int num) {
	return 0;
}

bool GrimEngine::getControlState(int num) {
	return _controlsState[num];
}

float GrimEngine::getPerSecond(float rate) const {
	return rate * _frameTime / 1000;
}

void GrimEngine::invalidateActiveActorsList() {
	_buildActiveActorsList = true;
}

void GrimEngine::immediatelyRemoveActor(Actor *actor) {
	_activeActors.remove(actor);
	_talkingActors.remove(actor);
}

void GrimEngine::buildActiveActorsList() {
	if (!_buildActiveActorsList) {
		return;
	}

	_activeActors.clear();
	foreach (Actor *a, Actor::getPool()) {
		if (((_mode == NormalMode || _mode == DrawMode) && a->isInSet(_currSet->getName())) ||
		    a->isInOverworld()) {
			_activeActors.push_back(a);
		}
	}
	_buildActiveActorsList = false;
}

void GrimEngine::addTalkingActor(Actor *a) {
	_talkingActors.push_back(a);
}

bool GrimEngine::areActorsTalking() const {
	//This takes into account that there may be actors which are still talking, but in the background.
	bool talking = false;
	foreach (Actor *a, _talkingActors) {
		if (a->isTalkingForeground()) {
			talking = true;
			break;
		}
	}
	return talking;
}

void GrimEngine::setMovieSubtitle(TextObject *to) {
	if (_movieSubtitle != to) {
		delete _movieSubtitle;
		_movieSubtitle = to;
	}
}

void GrimEngine::setMovieSetup() {
	_movieSetup = _currSet->getCurrSetup()->_name;
}

void GrimEngine::setMode(EngineMode mode) {
	_mode = mode;
	invalidateActiveActorsList();
}

void GrimEngine::clearEventQueue() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
	}

	for (int i = 0; i < KEYCODE_EXTRA_LAST; ++i) {
		_controlsState[i] = false;
	}
}

bool GrimEngine::hasFeature(EngineFeature f) const {
	return
		(f == kSupportsRTL) ||
		(f == kSupportsLoadingDuringRuntime);
}

void GrimEngine::openMainMenuDialog() {
	Common::KeyState key(Common::KEYCODE_F1, Common::ASCII_F1);
	handleControls(Common::EVENT_KEYDOWN, key);
	handleControls(Common::EVENT_KEYUP, key);
}

void GrimEngine::pauseEngineIntern(bool pause) {
	g_imuse->pause(pause);
	g_movie->pause(pause);

	if (pause) {
		_pauseStartTime = _system->getMillis();
	} else {
		_frameStart += _system->getMillis() - _pauseStartTime;
	}
}

void GrimEngine::debugLua(const Common::String &str) {
	lua_dostring(str.c_str());
}

} // end of namespace Grim
